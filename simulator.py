"""Validação local da interface ECU. Python 3.10+, sem dependências externas.

Execute: python simulator.py --open
Não emula temporização, eletrônica ou o firmware da ESP32.
"""
import argparse
import copy
import json
import math
from pathlib import Path
import threading
import time
import webbrowser
from collections import deque
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parent
MAP_COLS = MAP_ROWS = 16

def sample_map(mapping, rpm, tps):
    def bracket(axis, value):
        value = max(axis[0], min(axis[-1], value))
        i = next((i for i in range(len(axis)-1) if value <= axis[i+1]), len(axis)-2)
        return i, (value-axis[i])/(axis[i+1]-axis[i])
    i, x = bracket(mapping['rpmAxis'], rpm)
    j, y = bracket(mapping['tpsAxis'], tps)
    table = mapping['table']
    return (table[j][i]*(1-x)+table[j][i+1]*x)*(1-y)+(table[j+1][i]*(1-x)+table[j+1][i+1]*x)*y


def migrate_map(mapping):
    if len(mapping['rpmAxis']) == MAP_COLS and len(mapping['tpsAxis']) == MAP_ROWS:
        return mapping
    rpm = [mapping['rpmAxis'][0]+i*(mapping['rpmAxis'][-1]-mapping['rpmAxis'][0])/(MAP_COLS-1) for i in range(MAP_COLS)]
    tps = [mapping['tpsAxis'][0]+j*(mapping['tpsAxis'][-1]-mapping['tpsAxis'][0])/(MAP_ROWS-1) for j in range(MAP_ROWS)]
    return dict(rpmAxis=rpm, tpsAxis=tps, table=[[sample_map(mapping,r,t) for r in rpm] for t in tps])
DEFAULTS = dict(dwellMs=3.0, maxDwellMs=15.0, twoStepRpm=4500, maxRpm=7000,
    ignitionEnabled=True, tpsAdcMin=200, tpsAdcMax=3800, tpsMinPercent=1,
    tpsMaxPercent=99, servoAngleAtClosedThrottle=10, servoAngleAtFullThrottle=170,
    rpmCompensationRef=6000, rpmCompensationFactor=1, triggerTeethOffset=14,
    triggerFineOffsetDeg=0, fanTargetTemp=92, fanHysteresis=5, overheatTemp=100)


class ECU:
    def __init__(self, state_file):
        self.state_file = state_file
        self.lock = threading.RLock()
        self.config = DEFAULTS.copy()
        self.map = dict(rpmAxis=[500 + i * 6500 / (MAP_COLS-1) for i in range(MAP_COLS)],
                        tpsAxis=[j * 100 / (MAP_ROWS-1) for j in range(MAP_ROWS)])
        self.map['table'] = [[10 + r / 7000 * 20 + (100-t) / 100 * 8
                             for r in self.map['rpmAxis']] for t in self.map['tpsAxis']]
        if state_file.exists():
            saved = json.loads(state_file.read_text(encoding='utf-8'))
            self.config.update(saved['config'])
            self.map = migrate_map(saved['map'])
            if self.map is not saved['map']:
                backup = state_file.with_suffix('.12x8.bak.json')
                if not backup.exists(): backup.write_text(json.dumps(saved), encoding='utf-8')
                self.persist()
        self.start = time.monotonic()
        self.logs = deque(maxlen=2400)
        self.scenario = 'sweep'
        self.fan_override = self.servo_override = -1
        self.fan = False
        self.status = {}
        self.tick()

    def persist(self):
        temp = self.state_file.with_suffix('.tmp')
        temp.write_text(json.dumps(dict(config=self.config, map=self.map)), encoding='utf-8')
        temp.replace(self.state_file)

    def tick(self):
        with self.lock:
            t = time.monotonic() - self.start
            c = self.config
            tps = 45 + 40 * math.sin(t / 7)
            rpm = 900 + tps / 100 * (c['maxRpm'] - 900)
            temp = 85 + 9 * math.sin(t / 22)
            if self.scenario == 'stopped': rpm, tps = 0, 0
            if self.scenario == 'idle': rpm, tps = 900 + 20 * math.sin(t * 3), 0
            if self.scenario == 'overheat': temp = c['overheatTemp'] + 5
            if self.scenario == 'limit': rpm = c['maxRpm'] + 100
            two = self.scenario == 'two-step'
            if two: rpm = c['twoStepRpm'] + 100 * math.sin(t * 6)
            if temp >= c['fanTargetTemp']: self.fan = True
            elif temp <= c['fanTargetTemp'] - c['fanHysteresis']: self.fan = False
            # Interpolação bilinear do mesmo mapa exibido na interface.
            def bracket(axis, value):
                value = max(axis[0], min(axis[-1], value))
                i = next((i for i in range(len(axis)-1) if value <= axis[i+1]), len(axis)-2)
                return i, (value-axis[i])/(axis[i+1]-axis[i])
            i, x = bracket(self.map['rpmAxis'], rpm)
            j, y = bracket(self.map['tpsAxis'], tps)
            table = self.map['table']
            adv = (table[j][i]*(1-x)+table[j][i+1]*x)*(1-y)+(table[j+1][i]*(1-x)+table[j+1][i+1]*x)*y
            servo = round(c['servoAngleAtClosedThrottle'] + tps/100 * (c['servoAngleAtFullThrottle']-c['servoAngleAtClosedThrottle']))
            self.status = dict(simulator=True, scenario=self.scenario, rpm=rpm, tps=tps,
                tpsRaw=round(c['tpsAdcMin'] + tps/100*(c['tpsAdcMax']-c['tpsAdcMin'])),
                advance=adv, running=rpm>0, synced=rpm>0, temp=temp, tempRaw=1200,
                servoAngle=servo if self.servo_override == -1 else self.servo_override,
                fanOn=self.fan if self.fan_override == -1 else bool(self.fan_override),
                fanOverride=self.fan_override, servoOverride=self.servo_override,
                testMode=self.fan_override != -1 or self.servo_override != -1,
                overheat=temp>=c['overheatTemp'], twoStepPressed=two,
                twoStepCutting=two and rpm>c['twoStepRpm'], revLimitCutting=rpm>=c['maxRpm'],
                coilAFireCount=int(t*15) if rpm and c['ignitionEnabled'] else 0,
                coilBFireCount=int(t*15) if rpm and c['ignitionEnabled'] else 0,
                coilACharging=False, coilBCharging=False,
                **{k:c[k] for k in ['maxRpm','twoStepRpm','ignitionEnabled','fanTargetTemp','fanHysteresis','overheatTemp']},
                fanOnTemp=c['fanTargetTemp'], fanOffTemp=c['fanTargetTemp']-c['fanHysteresis'])
            self.logs.append((round(t*1000), rpm, tps, adv, temp))

    def update(self, path, data):
        if not isinstance(data, dict): raise ValueError('Esperado objeto JSON')
        with self.lock:
            if path == '/api/config':
                candidate = self.config | data
                for key, value in data.items():
                    if key not in DEFAULTS: raise ValueError('Campo desconhecido: ' + key)
                    if key == 'ignitionEnabled':
                        if not isinstance(value, bool): raise ValueError('Ignição deve ser booleana')
                    elif type(value) not in (int, float) or not math.isfinite(value):
                        raise ValueError('Valor numérico inválido: ' + key)
                if not 500 < candidate['maxRpm'] <= 20000: raise ValueError('RPM máximo inválido')
                if candidate['tpsAdcMin'] >= candidate['tpsAdcMax']: raise ValueError('TPS mínimo deve ser menor que máximo')
                if not 0 < candidate['dwellMs'] <= candidate['maxDwellMs']: raise ValueError('Dwell inválido')
                self.config = candidate
                self.persist()
            elif path == '/api/map':
                table = copy.deepcopy(self.map['table'])
                if 'table' in data: table = data['table']
                else:
                    r, c = data['row'], data['col']
                    if type(r) is not int or type(c) is not int or not (0 <= r < MAP_ROWS and 0 <= c < MAP_COLS): raise ValueError('Célula inválida')
                    table[r][c] = data['value']
                if not isinstance(table, list) or len(table)!=MAP_ROWS or any(not isinstance(row,list) or len(row)!=MAP_COLS for row in table): raise ValueError('Mapa deve ser 16 × 16')
                if any(type(v) not in (int,float) or not math.isfinite(v) for row in table for v in row): raise ValueError('Mapa inválido')
                self.map['table'] = table
                self.persist()
            elif path == '/api/actuator/test':
                if data.get('stop'): self.fan_override = self.servo_override = -1
                else:
                    if 'fan' in data:
                        if data['fan'] not in (-1,0,1): raise ValueError('Ventoinha inválida')
                        self.fan_override = data['fan']
                    if 'servo' in data:
                        if type(data['servo']) is not int or not -1 <= data['servo'] <= 180: raise ValueError('Servo inválido')
                        self.servo_override = data['servo']
            elif path == '/api/logs/clear': self.logs.clear()
            elif path == '/api/simulator':
                if data.get('scenario') not in ('sweep','idle','stopped','overheat','limit','two-step'): raise ValueError('Cenário inválido')
                self.scenario = data['scenario']
            else: raise LookupError('Endpoint desconhecido')


def make_handler(ecu):
    class Handler(SimpleHTTPRequestHandler):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, directory=str(ROOT / 'data'), **kwargs)

        def reply(self, value, code=200, content_type='application/json'):
            raw = (json.dumps(value) if content_type=='application/json' else value).encode('utf-8')
            self.send_response(code)
            self.send_header('Content-Type', content_type + '; charset=utf-8')
            self.send_header('Content-Length', str(len(raw)))
            self.send_header('Cache-Control', 'no-store')
            self.end_headers()
            self.wfile.write(raw)

        def do_GET(self):
            path = urlsplit(self.path).path
            with ecu.lock:
                if path == '/api/status': return self.reply(ecu.status)
                if path == '/api/config': return self.reply(ecu.config | dict(triggerOffsetDeg=ecu.config['triggerTeethOffset']*6+ecu.config['triggerFineOffsetDeg']))
                if path == '/api/map': return self.reply(ecu.map)
                if path == '/api/logs': return self.reply('t_ms,rpm,tps,advance,temp\n' + ''.join(','.join(str(round(v,2)) for v in row)+'\n' for row in ecu.logs), content_type='text/csv')
            if path.startswith('/api/'): return self.reply({'error':'Endpoint desconhecido'},404)
            return super().do_GET()

        def do_POST(self):
            try:
                length = int(self.headers.get('Content-Length', '0'))
                if not 0 < length <= 16384: raise ValueError('Corpo ausente ou muito grande')
                data = json.loads(self.rfile.read(length))
                ecu.update(urlsplit(self.path).path, data)
                self.reply({'ok':True})
            except LookupError as err: self.reply({'error':str(err)},404)
            except (ValueError, TypeError) as err: self.reply({'error':str(err)},400)

        def log_message(self, *_): pass
    return Handler


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=8080)
    parser.add_argument('--open', action='store_true', help='Abrir navegador')
    parser.add_argument('--state', type=Path, default=ROOT / '.simulator-state.json')
    args = parser.parse_args()
    ecu = ECU(args.state)
    server = ThreadingHTTPServer(('127.0.0.1', args.port), make_handler(ecu))
    stop = threading.Event()
    def sample():
        while not stop.wait(.05): ecu.tick()
    threading.Thread(target=sample, daemon=True).start()
    url = f'http://127.0.0.1:{args.port}'
    print(f'Simulador ECU: {url}\nDados sintéticos. Ctrl+C para encerrar.', flush=True)
    if args.open: webbrowser.open(url)
    try: server.serve_forever()
    except KeyboardInterrupt: pass
    finally:
        stop.set()
        server.server_close()


if __name__ == '__main__': main()
