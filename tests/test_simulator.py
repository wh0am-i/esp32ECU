import json
from pathlib import Path
import sys
import tempfile
import threading
import unittest
from urllib.error import HTTPError
from urllib.request import Request, urlopen
from http.server import ThreadingHTTPServer

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from simulator import ECU, make_handler, sample_map


class SimulatorTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.state = Path(self.temp.name) / 'state.json'
        self.ecu = ECU(self.state)
        self.server = ThreadingHTTPServer(('127.0.0.1', 0), make_handler(self.ecu))
        threading.Thread(target=self.server.serve_forever, daemon=True).start()
        self.url = 'http://127.0.0.1:' + str(self.server.server_port)

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.temp.cleanup()

    def request(self, path, data=None):
        request = Request(self.url + path, data=json.dumps(data).encode() if data is not None else None,
                          headers={'Content-Type': 'application/json'})
        with urlopen(request) as response: return response.read().decode()

    def test_config_persistence_and_axis(self):
        previous = json.dumps(self.ecu.map)
        self.request('/api/config', {'maxRpm': 8000})
        restored = ECU(self.state)
        self.assertEqual(restored.config['maxRpm'], 8000)
        self.assertEqual(restored.map['rpmAxis'][-1], 7000)
        self.assertEqual(json.dumps(restored.map), previous)

    def test_legacy_migration_and_backup(self):
        rpm = [500+i*6500/11 for i in range(12)]
        tps = [j*100/7 for j in range(8)]
        old = dict(rpmAxis=rpm, tpsAxis=tps, table=[[10+r*.002-t*.04 for r in rpm] for t in tps])
        saved = dict(config=self.ecu.config, map=old)
        self.state.write_text(json.dumps(saved), encoding='utf-8')
        migrated = ECU(self.state)
        self.assertEqual(len(migrated.map['table']), 16)
        self.assertTrue(all(len(row)==16 for row in migrated.map['table']))
        for r,t in [(500,0),(7000,100),(2347,18.6),(4500,42)]:
            self.assertAlmostEqual(sample_map(migrated.map,r,t),sample_map(old,r,t),places=5)
        self.assertEqual(json.loads(self.state.with_suffix('.12x8.bak.json').read_text()), saved)

    def test_full_16_map_roundtrip(self):
        table = [[10+j*.123+i*.456 for i in range(16)] for j in range(16)]
        self.request('/api/map', {'table':table})
        self.assertEqual(json.loads(self.request('/api/map'))['table'],table)
        self.assertEqual(ECU(self.state).map['table'],table)

    def test_invalid_config_leaves_state_unchanged(self):
        with self.assertRaises(HTTPError) as err:
            self.request('/api/config', {'maxRpm': 9000, 'tpsAdcMin': 4000})
        self.assertEqual(err.exception.code, 400)
        self.assertEqual(self.ecu.config['maxRpm'], 7000)

    def test_invalid_map_is_atomic(self):
        old = json.dumps(self.ecu.map)
        with self.assertRaises(HTTPError): self.request('/api/map', {'table': [[12]]})
        self.assertEqual(old, json.dumps(self.ecu.map))
        self.request('/api/map', {'row': 0, 'col': 0, 'value': 19})
        self.assertEqual(ECU(self.state).map['table'][0][0], 19)

    def test_actuator_and_scenarios(self):
        self.request('/api/actuator/test', {'fan': 1, 'servo': 45})
        self.ecu.tick()
        self.assertTrue(self.ecu.status['testMode'])
        self.assertEqual(self.ecu.status['servoAngle'], 45)
        self.request('/api/actuator/test', {'stop': True})
        self.request('/api/simulator', {'scenario': 'overheat'})
        self.ecu.tick()
        self.assertFalse(self.ecu.status['testMode'])
        self.assertTrue(self.ecu.status['overheat'])

    def test_logs_and_static_files(self):
        for _ in range(2405): self.ecu.tick()
        csv = self.request('/api/logs')
        self.assertEqual(len(csv.splitlines()), 2401)
        self.assertTrue(csv.startswith('t_ms,rpm,tps,advance,temp'))
        self.request('/api/logs/clear', {})
        self.assertEqual(len(self.request('/api/logs').splitlines()), 1)
        self.assertIn('Painel do motor', self.request('/'))
        self.assertTrue(json.loads(self.request('/api/status'))['simulator'])


if __name__ == '__main__': unittest.main()
