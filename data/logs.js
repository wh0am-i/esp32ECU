const channels = [
  {key:1, name:'Rotação', unit:'RPM', color:'#ff4254'},
  {key:2, name:'Borboleta', unit:'%', color:'#ffffff'},
  {key:3, name:'Avanço', unit:'° BTDC', color:'#a6a6b6'},
  {key:4, name:'Temperatura', unit:'°C', color:'#e5b68a'}
];
let samples = [], visible = [], paused = false, imported = false, loading = false, generation = 0;
const canvas = document.getElementById('logCanvas'), slider = document.getElementById('cursor-slider');
const enabled = new Set(channels.map(c => c.key));
for (const c of channels) {
  const card = document.createElement('div'); card.className = 'channel-card'; card.style.setProperty('--channel', c.color);
  card.innerHTML = `<label><input type="checkbox" checked data-channel="${c.key}">${c.name}</label><strong id="value-${c.key}">—</strong><small id="range-${c.key}">Sem dados</small>`;
  card.querySelector('input').onchange = e => { e.target.checked ? enabled.add(c.key) : enabled.delete(c.key); draw(); };
  document.getElementById('channels').append(card);
}

function parseCsv(text) {
  const lines = text.replace(/^\uFEFF/, '').trim().split(/\r?\n/);
  if (lines.shift() !== 't_ms,rpm,tps,advance,temp') throw new Error('CSV incompatível. Cabeçalho esperado: t_ms,rpm,tps,advance,temp');
  let previous = -1;
  return lines.filter(l => l.trim()).map(line => {
    const parts = line.split(','); const row = parts.map(Number);
    if (parts.length !== 5 || parts.some(p => !p.trim()) || row.some(v => !Number.isFinite(v)) || row[0] < previous) throw new Error('CSV contém valores ou tempos inválidos.');
    previous = row[0]; return row;
  });
}

// O intervalo de análise usa os timestamps originais; zoom não altera os dados.
let viewRange=null, chartDrag=null;
const plotLeft=100, plotRight=1410, overview=document.getElementById('logOverview');
function fullRange() { return [samples[0]?.[0]??0,samples.at(-1)?.[0]??1]; }
function freezeView() { if (!paused) { paused=true; generation++; setMode(); } }
function setRange(start,end) {
  if (samples.length<2) return;
  freezeView();
  const [lo,hi]=fullRange(), minimum=Math.min(50,hi-lo);
  let span=Math.max(minimum,Math.min(hi-lo,end-start));
  start=Math.max(lo,Math.min(hi-span,start));
  viewRange=[start,start+span]; render();
}
function currentRange() {
  if (viewRange) return viewRange;
  const [start,end]=fullRange(), seconds=Number(document.getElementById('time-window').value);
  return [seconds?Math.max(start,end-seconds*1000):start,end];
}
function render() {
  const [start,end]=currentRange();
  visible=samples.filter(row=>row[0]>=start && row[0]<=end);
  slider.max=Math.max(0,visible.length-1); slider.value=slider.max;
  document.getElementById('log-count').textContent=`${visible.length.toLocaleString('pt-BR')} amostras`;
  const duration=Math.max(0,(end-start)/1000);
  document.getElementById('log-duration').textContent=`${duration.toFixed(3)} s na janela`;
  const sampledDuration=visible.length>1?(visible.at(-1)[0]-visible[0][0])/1000:0;
  document.getElementById('log-rate').textContent=sampledDuration?`${((visible.length-1)/sampledDuration).toFixed(1)} Hz (média)`:'— Hz';
  document.getElementById('export-log').disabled=!samples.length;
  const [lo,hi]=fullRange();
  document.getElementById('zoom-info').textContent=`${((hi-lo)/Math.max(1,end-start)).toFixed(1)}× · ${((start-lo)/1000).toFixed(2)}–${((end-lo)/1000).toFixed(2)} s`;
  draw();
}
function draw() {
  const ctx=canvas.getContext('2d'), active=channels.filter(c=>enabled.has(c.key));
  canvas.height=Math.max(240,active.length*155+65);
  const [start,end]=currentRange(), span=Math.max(1,end-start), origin=samples[0]?.[0]??0;
  const selected=visible[Number(slider.value)], x=t=>plotLeft+(t-start)/span*(plotRight-plotLeft);
  const top=28, bottom=canvas.height-40, lane=(bottom-top)/Math.max(1,active.length);
  ctx.fillStyle='#0b0b0e'; ctx.fillRect(0,0,canvas.width,canvas.height); ctx.font='12px Consolas, monospace';
  document.getElementById('log-cursor').textContent=selected?`Cursor: ${((selected[0]-origin)/1000).toFixed(3)} s`:'Cursor: —';
  for (const c of channels) {
    document.getElementById(`value-${c.key}`).textContent=selected?`${selected[c.key].toFixed(c.key===1?0:1)} ${c.unit}`:'—';
    const values=visible.map(r=>r[c.key]); let min=Infinity,max=-Infinity;
    for (const v of values) { min=Math.min(min,v); max=Math.max(max,v); }
    document.getElementById(`range-${c.key}`).textContent=values.length?`Mín ${min.toFixed(1)} · Máx ${max.toFixed(1)} ${c.unit}`:'Sem amostras na janela';
  }
  if (!active.length||!samples.length) {
    ctx.fillStyle='#ddd'; ctx.fillText(!active.length?'Selecione um canal para analisar.':'Sem dados: aguarde a coleta ou abra um CSV.',plotLeft,80); drawOverview(); return;
  }
  active.forEach((c,index)=>{
    const y0=top+index*lane+20,y1=top+(index+1)*lane-14;
    let min=[0,0,0,0,-20][c.key],max=[0,8000,100,50,140][c.key];
    if (visible.length) {
      let low=Infinity,high=-Infinity;
      for (const row of visible) { low=Math.min(low,row[c.key]); high=Math.max(high,row[c.key]); }
      if (document.getElementById('auto-scale').checked) { const pad=Math.max(1,(high-low)*.1); min=low-pad; max=high+pad; }
      else { min=Math.min(min,Math.floor(low/10)*10); max=Math.max(max,Math.ceil(high/10)*10); }
    }
    const y=v=>y1-(v-min)/(max-min)*(y1-y0);
    ctx.fillStyle=index%2?'#151518':'#101013'; ctx.fillRect(0,top+index*lane,canvas.width,lane);
    ctx.fillStyle=c.color; ctx.textAlign='left'; ctx.font='bold 12px Consolas'; ctx.fillText(`${String(index+1).padStart(2,'0')} / ${c.name.toUpperCase()} [${c.unit}]`,plotLeft,top+index*lane+12);
    ctx.font='11px Consolas';
    for(let j=0;j<=4;j++) {
      const yy=y0+j/4*(y1-y0); ctx.strokeStyle='#36363f'; ctx.lineWidth=1;
      ctx.beginPath();ctx.moveTo(plotLeft,yy);ctx.lineTo(plotRight,yy);ctx.stroke();
      ctx.fillStyle='#c7c7d0';ctx.textAlign='right';ctx.fillText((max-j/4*(max-min)).toFixed(c.key===1?0:1),plotLeft-12,yy+4);
    }
    for(let i=0;i<=40;i++) {
      const xx=plotLeft+i/40*(plotRight-plotLeft);ctx.strokeStyle=i%4?'#24242a':'#44444e';
      ctx.beginPath();ctx.moveTo(xx,y0);ctx.lineTo(xx,y1);ctx.stroke();
    }
    ctx.save();ctx.beginPath();ctx.rect(plotLeft,y0,plotRight-plotLeft,y1-y0);ctx.clip();
    if (visible.length) {
      ctx.beginPath();visible.forEach((row,i)=>{if(!i)ctx.moveTo(x(row[0]),y(row[c.key]));else ctx.lineTo(x(row[0]),y(row[c.key]));});
      ctx.strokeStyle=c.color;ctx.lineWidth=2;ctx.stroke();
      ctx.lineTo(x(visible.at(-1)[0]),y1);ctx.lineTo(x(visible[0][0]),y1);ctx.closePath();ctx.fillStyle=c.color+'18';ctx.fill();
      if (selected) {ctx.fillStyle=c.color;ctx.beginPath();ctx.arc(x(selected[0]),y(selected[c.key]),4,0,Math.PI*2);ctx.fill();}
    }
    ctx.restore();
    if(selected) {ctx.textAlign='right';ctx.fillStyle=c.color;ctx.fillText(`${selected[c.key].toFixed(c.key===1?0:2)} ${c.unit}`,plotRight,top+index*lane+12);}
  });
  ctx.font='12px Consolas';ctx.textAlign='center';ctx.fillStyle='#ddd';
  for(let i=0;i<=10;i++) ctx.fillText(`${((start-origin+i/10*span)/1000).toFixed(2)}s`,plotLeft+i/10*(plotRight-plotLeft),canvas.height-16);
  if(selected) {ctx.strokeStyle='#fff';ctx.lineWidth=1;ctx.setLineDash([5,3]);ctx.beginPath();ctx.moveTo(x(selected[0]),top);ctx.lineTo(x(selected[0]),bottom);ctx.stroke();ctx.setLineDash([]);}
  if(chartDrag) {ctx.fillStyle='#ff425438';ctx.fillRect(Math.min(chartDrag.a,chartDrag.b),top,Math.abs(chartDrag.b-chartDrag.a),bottom-top);ctx.strokeStyle='#ff6573';ctx.strokeRect(Math.min(chartDrag.a,chartDrag.b),top,Math.abs(chartDrag.b-chartDrag.a),bottom-top);}
  drawOverview();
}
function drawOverview() {
  const ctx=overview.getContext('2d'),[lo,hi]=fullRange(),[start,end]=currentRange(),span=Math.max(1,hi-lo);
  ctx.fillStyle='#18181d';ctx.fillRect(0,0,overview.width,overview.height);
  if(!samples.length)return;
  let peak=1000;for(const r of samples)peak=Math.max(peak,r[1]);
  ctx.beginPath();samples.forEach((r,i)=>{const x=plotLeft+(r[0]-lo)/span*(plotRight-plotLeft),y=75-r[1]/peak*60;if(!i)ctx.moveTo(x,y);else ctx.lineTo(x,y);});ctx.strokeStyle='#e3303e';ctx.stroke();
  const x=plotLeft+(start-lo)/span*(plotRight-plotLeft),width=(end-start)/span*(plotRight-plotLeft);
  ctx.fillStyle='#ffffff12';ctx.fillRect(x,4,width,80);ctx.strokeStyle='#fff';ctx.strokeRect(x,4,width,80);
  ctx.fillStyle='#fff';ctx.fillRect(x,4,3,80);ctx.fillRect(x+width-3,4,3,80);
}
function eventX(e,target=canvas) { const rect=target.getBoundingClientRect();return Math.max(plotLeft,Math.min(plotRight,(e.clientX-rect.left)/rect.width*target.width)); }
function timeAt(x,range=currentRange()) { return range[0]+(x-plotLeft)/(plotRight-plotLeft)*(range[1]-range[0]); }
function zoom(factor,anchor=.5) { const [a,b]=currentRange(),center=a+(b-a)*anchor,span=(b-a)*factor;setRange(center-span*anchor,center+span*(1-anchor)); }
slider.oninput=draw;
canvas.onpointerdown=e=>{if(!samples.length||e.button!==0)return;freezeView();const x=eventX(e);chartDrag={a:x,b:x};canvas.setPointerCapture(e.pointerId);draw();};
canvas.onpointermove=e=>{if(chartDrag){chartDrag.b=eventX(e);draw();}};
canvas.onpointerup=e=>{
  if(!chartDrag)return;const {a,b}=chartDrag;chartDrag=null;
  if(canvas.hasPointerCapture(e.pointerId))canvas.releasePointerCapture(e.pointerId);
  if(Math.abs(b-a)>8)setRange(timeAt(Math.min(a,b)),timeAt(Math.max(a,b)));
  else {const target=timeAt(a);let nearest=0;visible.forEach((r,i)=>{if(Math.abs(r[0]-target)<Math.abs(visible[nearest][0]-target))nearest=i;});slider.value=nearest;draw();}
};
canvas.onpointercancel=()=>{chartDrag=null;draw();};
canvas.addEventListener('wheel',e=>{if(samples.length<2)return;e.preventDefault();zoom(e.deltaY>0?1.25:.8,(eventX(e)-plotLeft)/(plotRight-plotLeft));},{passive:false});
let overviewStart=null;
overview.onpointerdown=e=>{if(e.button!==0||!samples.length)return;overviewStart=eventX(e,overview);overview.setPointerCapture(e.pointerId);};
overview.onpointerup=e=>{if(overviewStart===null)return;const a=overviewStart,b=eventX(e,overview);overviewStart=null;if(overview.hasPointerCapture(e.pointerId))overview.releasePointerCapture(e.pointerId);if(Math.abs(b-a)>8)setRange(timeAt(Math.min(a,b),fullRange()),timeAt(Math.max(a,b),fullRange()));else{const [start,end]=currentRange(),t=timeAt(b,fullRange());setRange(t-(end-start)/2,t+(end-start)/2);}};
overview.onpointercancel=()=>{overviewStart=null;};
document.getElementById('zoom-in').onclick=()=>zoom(.5);
document.getElementById('zoom-out').onclick=()=>zoom(2);
document.getElementById('zoom-reset').onclick=()=>{viewRange=null;document.getElementById('time-window').value='0';render();};
for(const [id,direction] of [['pan-left',-1],['pan-right',1]])document.getElementById(id).onclick=()=>{const [a,b]=currentRange(),shift=(b-a)*.5*direction;setRange(a+shift,b+shift);};
document.getElementById('auto-scale').onchange=draw;
document.getElementById('time-window').onchange=()=>{viewRange=null;render();};
function setMode() {
  document.getElementById('pause-log').textContent=paused?'Retomar ao vivo':'Pausar visualização';
  document.getElementById('log-source').textContent=imported?'Arquivo CSV · análise local':paused?'Visualização pausada · ECU continua registrando':'Ao vivo · buffer circular da ECU';
}
async function loadLogs() {
  if (loading) return;
  loading=true; const token=generation;
  try {
    const response=await fetch('/api/logs',{cache:'no-store',signal:AbortSignal.timeout(5000)});
    if (!response.ok) throw new Error(`Falha ao ler log: HTTP ${response.status}`);
    const parsed=parseCsv(await response.text());
    if (token===generation) { samples=parsed; render(); }
  } catch(e) { notify(e.message,true); } finally { loading=false; }
}
document.getElementById('pause-log').onclick=() => { generation++; paused=!paused; if (!paused) { imported=false; viewRange=null; loadLogs(); } setMode(); };
document.getElementById('refresh-log').onclick=() => { generation++; imported=false; viewRange=null; setMode(); loadLogs(); };
document.getElementById('clear-log').onclick=async () => {
  if (!confirm('Apagar as amostras da memória da ECU? Exporte o CSV para preservá-las.')) return;
  generation++; await postJSON('/api/logs/clear',{}); samples=[]; viewRange=null; imported=false; setMode(); render();
};
document.getElementById('export-log').onclick=() => {
  const csv='t_ms,rpm,tps,advance,temp\n'+samples.map(r=>r.join(',')).join('\n');
  const url=URL.createObjectURL(new Blob([csv],{type:'text/csv'})); const a=document.createElement('a');
  a.href=url; a.download=`ecu-${new Date().toISOString().replace(/[:.]/g,'-')}.csv`; a.click(); setTimeout(()=>URL.revokeObjectURL(url),1000);
};
document.getElementById('import-log').onchange=async e => {
  const file=e.target.files[0]; if (!file) return;
  try {
    if (file.size>5*1024*1024) throw new Error('CSV deve ter até 5 MB.');
    const parsed=parseCsv(await file.text()); generation++; paused=true; imported=true; viewRange=null; samples=parsed; setMode(); render();
  } catch(err) { notify(err.message,true); }
  e.target.value='';
};
connectStatusSocket(() => {});
loadLogs(); setInterval(() => { if (!paused) loadLogs(); },3000);
