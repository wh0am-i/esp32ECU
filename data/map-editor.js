let mapSelection = null, mapAnchor = null, mapDrag = false, mapHistory = [];
const grid = () => document.getElementById('mapGrid');
function selectionContains(r,c) {
  return !mapSelection || (r>=mapSelection.r0 && r<=mapSelection.r1 && c>=mapSelection.c0 && c<=mapSelection.c1);
}
function rememberMapEdit() {
  mapHistory.push(mapData.table.map(row=>row.slice()));
  if (mapHistory.length>30) mapHistory.shift();
  document.getElementById('map-undo').disabled=false;
}
function undoMapEdit() {
  if (!mapHistory.length) return;
  mapData.table=mapHistory.pop(); markMapDirty(); renderMap();
  document.getElementById('map-undo').disabled=!mapHistory.length;
}
function resetMapEditor() {
  mapSelection=null; mapHistory=[]; mapAnchor=null; mapDrag=false;
  document.getElementById('map-undo').disabled=true;
}
function clearMapSelection() { mapSelection=null; grid().querySelector('input')?.remove(); paintSelection(); }
function transformMapSelection(fn) {
  rememberMapEdit();
  mapData.table=mapData.table.map((row,r)=>row.map((v,c)=>selectionContains(r,c)?fn(v,r,c):v));
}
function paintSelection() {
  grid().querySelectorAll('[data-row]').forEach(td=>{
    const selected=!!mapSelection && selectionContains(+td.dataset.row,+td.dataset.col);
    td.classList.toggle('selected',selected); td.setAttribute('aria-selected',selected);
  });
  const count=mapSelection?(mapSelection.r1-mapSelection.r0+1)*(mapSelection.c1-mapSelection.c0+1):0;
  document.getElementById('selection-info').textContent=count?`${count} célula(s) selecionada(s) · digite na célula e pressione Enter para aplicar a todas`:'Sem seleção · ajustes em lote afetam o mapa inteiro';
}
function selectRectangle(a,b) {
  mapSelection={r0:Math.min(a.r,b.r),r1:Math.max(a.r,b.r),c0:Math.min(a.c,b.c),c1:Math.max(a.c,b.c)};
  paintSelection();
}
function openCellEditor(r,c) {
  grid().querySelector('input')?.remove();
  const td=grid().querySelector(`[data-row="${r}"][data-col="${c}"]`);
  if (!td) return;
  const input=document.createElement('input'); input.type='text'; input.inputMode='decimal'; input.className='inline-map-input';
  input.setAttribute('aria-label',`Avanço TPS ${mapData.tpsAxis[r].toFixed(1)}%, RPM ${Math.round(mapData.rpmAxis[c])}`);
  input.value=mapData.table[r][c].toFixed(1); td.append(input); input.focus({preventScroll:true}); input.select();
  input.onpointerdown=e=>e.stopPropagation();
  input.onblur=()=>queueMicrotask(()=>{ if(input.isConnected) input.remove(); });
  input.onkeydown=e=>{
    if (e.key==='Escape') { e.preventDefault(); input.remove(); grid().focus({preventScroll:true}); return; }
    if (!['Enter','Tab'].includes(e.key)) return;
    e.preventDefault();
    const raw=input.value.trim().replace(',','.'); const value=Number(raw);
    if (!raw || !Number.isFinite(value)) { input.setAttribute('aria-invalid','true'); notify('Informe um avanço numérico válido.',true); return; }
    transformMapSelection(()=>Math.round(value*10)/10); input.remove(); markMapDirty(); renderMap();
    if (e.key==='Tab') {
      const next=Math.max(0,Math.min(mapData.rpmAxis.length-1,c+(e.shiftKey?-1:1)));
      mapAnchor={r,c:next}; selectRectangle(mapAnchor,mapAnchor); openCellEditor(r,next);
    } else grid().focus({preventScroll:true});
  };
}
function renderMapGrid() {
  const host=grid(); host.replaceChildren();
  const table=document.createElement('table'); table.className='ignition-table';
  const head=table.createTHead().insertRow();
  const corner=document.createElement('th'); corner.textContent='TPS % / RPM'; head.append(corner);
  for (const rpm of mapData.rpmAxis) { const th=document.createElement('th'); th.scope='col'; th.textContent=Math.round(rpm); head.append(th); }
  const body=table.createTBody();
  for (let r=mapData.tpsAxis.length-1;r>=0;r--) {
    const tr=body.insertRow(),th=document.createElement('th'); th.scope='row'; th.textContent=mapData.tpsAxis[r].toFixed(1)+'%'; tr.append(th);
    mapData.table[r].forEach((value,c)=>{
      const td=tr.insertCell(); td.dataset.row=r; td.dataset.col=c; td.setAttribute('role','gridcell');
      td.textContent=value.toFixed(1); const color=advanceToColor(value,5,45); td.style.background=color.css;
      td.style.color=(color.r*299+color.g*587+color.b*114)/1000>140?'#151518':'#fff';
    });
  }
  host.append(table); paintSelection();
  host.onpointerdown=e=>{
    if (e.button!==0) return;
    const td=e.target.closest('[data-row]'); if (!td) return;
    e.preventDefault(); host.querySelector('input')?.remove();
    const cell={r:+td.dataset.row,c:+td.dataset.col};
    if (!e.shiftKey || !mapAnchor) mapAnchor=cell;
    selectRectangle(mapAnchor,cell); mapDrag=true; host.setPointerCapture(e.pointerId);
  };
  host.onpointermove=e=>{
    if (!mapDrag) return;
    const td=document.elementFromPoint(e.clientX,e.clientY)?.closest('[data-row]');
    if (td && host.contains(td)) selectRectangle(mapAnchor,{r:+td.dataset.row,c:+td.dataset.col});
  };
  host.onpointerup=e=>{
    if (!mapDrag) return; mapDrag=false;
    if (host.hasPointerCapture(e.pointerId)) host.releasePointerCapture(e.pointerId);
    openCellEditor(mapAnchor.r,mapAnchor.c);
  };
  host.onpointercancel=()=>{mapDrag=false;};
  host.onkeydown=e=>{
    if (e.target.tagName==='INPUT') return;
    if (e.key==='Escape') { clearMapSelection(); return; }
    if ((e.ctrlKey||e.metaKey)&&e.key.toLowerCase()==='z') { e.preventDefault(); undoMapEdit(); return; }
    if (mapSelection && (e.key==='Enter'||/^[0-9.-]$/.test(e.key))) {
      e.preventDefault(); openCellEditor(mapAnchor.r,mapAnchor.c);
      if (e.key!=='Enter') host.querySelector('input').value=e.key;
    }
  };
}
