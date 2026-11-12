// ============================================================================
// app.js - código compartilhado entre as 3 páginas do webapp
// (sem dependências externas: a ESP32 não tem acesso à internet pra baixar
// CDN, então tudo aqui é vanilla JS + canvas puro)
// ============================================================================

function fetchJSON(url) {
  return fetch(url, { cache: 'no-store', signal: AbortSignal.timeout(5000) }).then(checkResponse);
}

async function checkResponse(response) {
  const data = await response.json();
  if (!response.ok || data.error) throw new Error(data.error || `HTTP ${response.status}`);
  return data;
}

function notify(message, error = false) {
  let el = document.getElementById('notification');
  if (!el) {
    el = document.createElement('div'); el.id = 'notification';
    el.setAttribute('role', 'status'); document.body.append(el);
  }
  el.textContent = message; el.className = error ? 'notification error' : 'notification';
  el.hidden = false; clearTimeout(el.timer); el.timer = setTimeout(() => el.hidden = true, 6000);
}
window.addEventListener('unhandledrejection', e => { e.preventDefault(); notify(e.reason?.message || 'Falha de comunicação', true); });

function postJSON(url, data) {
  if (Object.values(data).some(v => typeof v === 'number' && !Number.isFinite(v))) {
    return Promise.reject(new Error('Preencha todos os campos com números válidos.'));
  }
  return fetch(url, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(data), signal: AbortSignal.timeout(5000)
  }).then(checkResponse);
}

function flashSaved(elId) {
  const el = document.getElementById(elId);
  if (!el) return;
  el.classList.add('show');
  setTimeout(() => el.classList.remove('show'), 1500);
}

// ----------------------------------------------------------------------------
// WebSocket de telemetria em tempo real, com reconexão automática
// ----------------------------------------------------------------------------
function connectStatusSocket(onMessage) {
  let ws, last = 0;
  const badge = document.getElementById('connection-state');
  function receive(d) {
    last = Date.now();
    if (badge) { badge.textContent = d.simulator ? 'SIMULADOR · online' : 'ECU · online'; badge.className = 'badge ok'; }
    document.body.classList.remove('telemetry-stale');
    if (d.simulator && !document.getElementById('sim-controls')) {
      const panel = document.createElement('div'); panel.id = 'sim-controls'; panel.className = 'sim-controls';
      panel.innerHTML = '<b>Validação Windows</b><span>Dados sintéticos</span><label for="scenario">Cenário</label><select id="scenario"><option value="sweep">Aceleração</option><option value="idle">Marcha lenta</option><option value="stopped">Motor parado</option><option value="overheat">Superaquecimento</option><option value="limit">Limitador</option><option value="two-step">2-step</option></select>';
      document.querySelector('main').prepend(panel);
      panel.querySelector('select').value = d.scenario;
      panel.querySelector('select').onchange = e => postJSON('/api/simulator', {scenario:e.target.value});
    }
    window.dispatchEvent(new CustomEvent("ecu-status", {detail:d}));
    onMessage(d);
  }
  setInterval(() => {
    if (Date.now()-last > 3000) {
      if (badge) { badge.textContent = 'Sem telemetria · dados desatualizados'; badge.className = 'badge danger'; }
      document.body.classList.add('telemetry-stale');
    }
  }, 1000);
  async function poll() {
    try { receive(await fetchJSON('/api/status')); } catch (_) { /* indicador de conexão */ }
    setTimeout(poll, 100);
  }
  function connect() {
    ws = new WebSocket((location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/ws');
    ws.onmessage = (evt) => {
      try {
        receive(JSON.parse(evt.data));
      } catch (e) { /* ignora frame malformado */ }
    };
    ws.onclose = () => setTimeout(connect, 1000);
    ws.onerror = () => ws.close();
  }
  fetchJSON('/api/status').then(d => { receive(d); d.simulator ? poll() : connect(); }).catch(() => connect());
}

// ----------------------------------------------------------------------------
// Heatmap do mapa de ignição (estilo TunerStudio/MegaLogViewer): grade RPM x
// TPS, cor com gradiente térmico calibrado pelo valor da célula e texto com
// contraste dinâmico de alta legibilidade.
// ----------------------------------------------------------------------------
function advanceToColor(v, vMin, vMax) {
  if (vMax <= vMin) vMax = vMin + 1;
  const t = Math.max(0, Math.min(1, (v - vMin) / (vMax - vMin)));
  // Escala sequencial de avanco: grafite, vermelho, branco.
  const stops = [
    [36, 37, 40], [80, 39, 44], [143, 36, 45], [218, 45, 53], [255, 218, 220]
  ];
  const scaled = t * (stops.length - 1);
  const i = Math.min(stops.length - 2, Math.floor(scaled));
  const frac = scaled - i;
  const c = stops[i].map((v0, k) => Math.round(v0 + (stops[i + 1][k] - v0) * frac));
  return {
    css: `rgb(${c[0]},${c[1]},${c[2]})`,
    r: c[0], g: c[1], b: c[2]
  };
}

function drawHeatmap(canvas, rpmAxis, tpsAxis, table, opts) {
  opts = opts || {};
  const ctx = canvas.getContext('2d');
  const padLeft = 60, padTop = 20, padBottom = 35, padRight = 20;
  const cols = rpmAxis.length, rows = tpsAxis.length;
  const cellW = (canvas.width - padLeft - padRight) / cols;
  const cellH = (canvas.height - padTop - padBottom) / rows;

  let vMin = opts.minVal !== undefined ? opts.minVal : Infinity;
  let vMax = opts.maxVal !== undefined ? opts.maxVal : -Infinity;
  if (opts.minVal === undefined || opts.maxVal === undefined) {
    for (const row of table) for (const v of row) { vMin = Math.min(vMin, v); vMax = Math.max(vMax, v); }
    // se todos valores forem iguais, cria uma margem saudável
    if (vMin === vMax) { vMin -= 5; vMax += 5; }
  }

  ctx.clearRect(0, 0, canvas.width, canvas.height);
  ctx.font = 'bold 12px "Segoe UI", Arial, sans-serif';

  for (let r = 0; r < rows; r++) {
    // linhas de baixo pra cima = TPS crescente pra cima, como um mapa de verdade
    const rowIdxDraw = rows - 1 - r;
    for (let c = 0; c < cols; c++) {
      const val = table[rowIdxDraw][c];
      const x = padLeft + c * cellW;
      const y = padTop + r * cellH;
      const col = advanceToColor(val, vMin, vMax);
      ctx.fillStyle = col.css;
      ctx.fillRect(x, y, cellW - 1, cellH - 1);

      // Contraste dinâmico: calcula a luminosidade percebida da célula
      const lum = (col.r * 299 + col.g * 587 + col.b * 114) / 1000;
      ctx.fillStyle = lum > 140 ? '#0f172a' : '#ffffff';
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';
      ctx.fillText(val.toFixed(1), x + cellW / 2, y + cellH / 2);
      if (opts.selectedCell?.row === rowIdxDraw && opts.selectedCell?.col === c) {
        ctx.strokeStyle = '#ffffff'; ctx.lineWidth = 2;
        ctx.strokeRect(x + 2, y + 2, cellW - 5, cellH - 5);
      }
    }
  }

  // eixo RPM (embaixo)
  ctx.font = '11px "Segoe UI", Arial, sans-serif';
  ctx.fillStyle = '#94a3b8';
  ctx.textAlign = 'center';
  for (let c = 0; c < cols; c++) {
    ctx.fillText(Math.round(rpmAxis[c]), padLeft + c * cellW + cellW / 2, canvas.height - padBottom + 16);
  }
  ctx.fillText('RPM (Rotação do Motor)', padLeft + (cols * cellW) / 2, canvas.height - 4);

  // eixo TPS (esquerda)
  ctx.textAlign = 'right';
  for (let r = 0; r < rows; r++) {
    const rowIdxDraw = rows - 1 - r;
    ctx.fillText(Math.round(tpsAxis[rowIdxDraw]) + '%', padLeft - 8, padTop + r * cellH + cellH / 2 + 4);
  }

  // título do eixo TPS (vertical, rotacionado, à esquerda dos valores)
  ctx.save();
  ctx.translate(14, padTop + (rows * cellH) / 2);
  ctx.rotate(-Math.PI / 2);
  ctx.textAlign = 'center';
  ctx.textBaseline = 'middle';
  ctx.font = 'bold 11px "Segoe UI", Arial, sans-serif';
  ctx.fillStyle = '#94a3b8';
  ctx.fillText('TPS (%)', 0, 0);
  ctx.restore();

  canvas.onclick = function (evt) {
    const rect = canvas.getBoundingClientRect();
    const x = (evt.clientX - rect.left) * (canvas.width / rect.width);
    const y = (evt.clientY - rect.top) * (canvas.height / rect.height);
    if (x < padLeft || y < padTop || y > canvas.height - padBottom) return;
    const c = Math.floor((x - padLeft) / cellW);
    const rDraw = Math.floor((y - padTop) / cellH);
    const rowIdx = rows - 1 - rDraw;
    if (c < 0 || c >= cols || rowIdx < 0 || rowIdx >= rows) return;
    if (opts.onCellClick) opts.onCellClick(rowIdx, c, table[rowIdx][c]);
  };
}

// ----------------------------------------------------------------------------
// Gerador de Superfície 3D Paramétrica R³: z = f(RPM, TPS)
// ----------------------------------------------------------------------------
function generateR3Surface(rpmAxis, tpsAxis, p) {
  const table = [];
  const rpmMin = p.idleRpm || rpmAxis[0];
  const rpmMax = p.maxRpm || rpmAxis[rpmAxis.length - 1];
  const gamma = p.rpmGamma || 1.0;

  for (let j = 0; j < tpsAxis.length; j++) {
    const tps = tpsAxis[j]; // 0 a 100%
    const tpsRatio = tps / 100.0;
    const row = [];
    for (let i = 0; i < rpmAxis.length; i++) {
      const rpm = rpmAxis[i];
      const rpmRatio = Math.max(0, Math.min(1, (rpm - rpmMin) / (rpmMax - rpmMin || 1)));
      const rpmCurve = Math.pow(rpmRatio, gamma);

      // Avanço centrífugo de base
      const baseAdv = p.idleAdvance + (p.maxAdvance - p.idleAdvance) * rpmCurve;

      // Compensação por carga de borboleta (retardo com borboleta aberta)
      const loadCorr = -(p.tpsRetard * tpsRatio);

      let val = baseAdv + loadCorr;
      val = Math.round(val * 10) / 10;
      row.push(val);
    }
    table.push(row);
  }
  return table;
}

// ----------------------------------------------------------------------------
// Ferramentas de edição em lote de tabela
// ----------------------------------------------------------------------------
function modifyTableDelta(table, delta) {
  return table.map(row => row.map(v => Math.round((v + delta) * 10) / 10));
}

function modifyTableFactor(table, factor) {
  return table.map(row => row.map(v => Math.round((v * factor) * 10) / 10));
}

function smoothTable(table) {
  const rows = table.length;
  const cols = table[0].length;
  const out = [];
  for (let r = 0; r < rows; r++) {
    const newRow = [];
    for (let c = 0; c < cols; c++) {
      let sum = table[r][c] * 2;
      let count = 2;
      if (r > 0) { sum += table[r - 1][c]; count++; }
      if (r < rows - 1) { sum += table[r + 1][c]; count++; }
      if (c > 0) { sum += table[r][c - 1]; count++; }
      if (c < cols - 1) { sum += table[r][c + 1]; count++; }
      newRow.push(Math.round((sum / count) * 10) / 10);
    }
    out.push(newRow);
  }
  return out;
}

// ----------------------------------------------------------------------------
// Gráfico de linhas múltiplas ("espaguete") para os logs: RPM, TPS%, Ponto
// ----------------------------------------------------------------------------
function drawLineChart(canvas, series, colors) {
  const ctx = canvas.getContext('2d');
  const padLeft = 50, padTop = 20, padBottom = 30, padRight = 90;
  const w = canvas.width - padLeft - padRight;
  const h = canvas.height - padTop - padBottom;

  ctx.clearRect(0, 0, canvas.width, canvas.height);

  const n = series[0] ? series[0].data.length : 0;
  if (n < 2) {
    ctx.fillStyle = '#8a8f98';
    ctx.fillText('Sem dados de log ainda - rode o motor/gire o sensor e volte aqui.', padLeft, padTop + 20);
    return;
  }

  series.forEach((s, idx) => {
    const vMin = Math.min(...s.data);
    const vMax = Math.max(...s.data);
    const range = (vMax - vMin) || 1;

    ctx.strokeStyle = colors[idx];
    ctx.lineWidth = 2;
    ctx.beginPath();
    s.data.forEach((v, i) => {
      const x = padLeft + (i / (n - 1)) * w;
      const y = padTop + h - ((v - vMin) / range) * h;
      if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
    });
    ctx.stroke();

    // legenda + faixa de valores à direita
    ctx.fillStyle = colors[idx];
    ctx.textAlign = 'left';
    ctx.font = '12px Segoe UI, Arial';
    ctx.fillText(`${s.label}`, canvas.width - padRight + 8, padTop + 16 + idx * 34);
    ctx.fillStyle = '#8a8f98';
    ctx.fillText(`${vMin.toFixed(0)} - ${vMax.toFixed(0)}`, canvas.width - padRight + 8, padTop + 30 + idx * 34);
  });
}
