// Instrumentação compartilhada: os mesmos canais no simulador e na placa.
const strip = document.createElement('div'); strip.className = 'instrument-strip';
strip.setAttribute('aria-label', 'Telemetria resumida');
const stripChannels = [['rpm','RPM',0],['tps','TPS %',1],['advance','BTDC °',1],['temp','CLT °C',1],['servoAngle','DOSADOR °',0]];
strip.innerHTML = '<b>LIVE DATA</b>' + stripChannels.map(([key,label]) => `<span>${label}<strong id="strip-${key}">—</strong></span>`).join('') + '<span>SYNC<strong id="strip-sync">—</strong></span>';
document.querySelector('nav').after(strip);

const dashboard = document.querySelector('.gauges');
if (dashboard) {
  const meter = document.createElement('div'); meter.className='rpm-instrument';
  meter.innerHTML='<div class="meter-heading"><b>ROTAÇÃO / RPM</b><span id="rpm-limit-label">CORTE —</span></div><div class="rpm-track"><div id="rpm-fill"></div></div><div id="rpm-scale" class="rpm-scale"></div>';
  dashboard.before(meter);
  const diagnostic = document.createElement('div'); diagnostic.className='diagnostic-grid';
  diagnostic.innerHTML = `<section class="panel"><h2>01 / Entradas & saídas</h2><dl id="io-values"></dl></section><section class="panel"><h2>02 / Proteções & limites</h2><dl id="limit-values"></dl></section><section class="panel"><h2>03 / Ignição & contadores</h2><dl id="coil-values"></dl></section>`;
  document.querySelector('.status-row').after(diagnostic);
}
function entries(id, rows) {
  const el=document.getElementById(id); if (!el) return;
  el.replaceChildren(...rows.flatMap(([key,value]) => {
    const dt=document.createElement('dt'),dd=document.createElement('dd');
    dt.textContent=key; dd.textContent=value; return [dt,dd];
  }));
}
window.addEventListener('ecu-status', ({detail:d}) => {
  for (const [key,,digits] of stripChannels) document.getElementById(`strip-${key}`).textContent=Number.isFinite(d[key])?d[key].toFixed(digits):'—';
  document.getElementById('strip-sync').textContent=d.synced?'OK':'SEM SINAL';
  if (!dashboard) return;
  const maximum=Math.max(8000, Math.ceil(d.maxRpm/1000)*1000);
  document.getElementById('rpm-fill').style.width=`${Math.min(100,d.rpm/maximum*100)}%`;
  document.getElementById('rpm-limit-label').textContent=`CORTE ${Math.round(d.maxRpm)} / ATUAL ${Math.round(d.rpm)}`;
  document.getElementById('rpm-scale').replaceChildren(...Array.from({length:9},(_,i)=>{const el=document.createElement('span');el.textContent=Math.round(i/8*maximum);return el;}));
  entries('io-values', [['TPS / ADC',d.tpsRaw],['CLT / ADC',d.tempRaw],['Servo / comando',`${d.servoAngle}°`],['Servo / modo',d.servoOverride===-1?'AUTOMÁTICO':'MANUAL'],['Ventoinha / saída',d.fanOn?'LIGADA':'DESLIGADA'],['Ventoinha / modo',d.fanOverride===-1?'AUTOMÁTICO':'MANUAL']]);
  entries('limit-values', [['Limitador',`${d.maxRpm} RPM`],['2-step',`${d.twoStepRpm} RPM`],['Corte de giro',d.revLimitCutting?'ATIVO':'INATIVO'],['Ventoinha / liga',`${d.fanOnTemp} °C`],['Ventoinha / desliga',`${d.fanOffTemp} °C`],['Alarme CLT',`${d.overheatTemp} °C`]]);
  entries('coil-values', [['Ignição',d.ignitionEnabled?'HABILITADA':'DESABILITADA'],['Bobina A / disparos',d.coilAFireCount],['Bobina B / disparos',d.coilBFireCount],['Bobina A / carga',d.coilACharging?'DWELL':'INATIVA'],['Bobina B / carga',d.coilBCharging?'DWELL':'INATIVA'],['Override de teste',d.testMode?'ATIVO':'INATIVO']]);
});
