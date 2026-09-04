// ============================================================================
// app.js - código compartilhado entre as 3 páginas do webapp
// (sem dependências externas: a ESP32 não tem acesso à internet pra baixar
// CDN, então tudo aqui é vanilla JS + canvas puro)
// ============================================================================

function fetchJSON(url) {
  return fetch(url).then(r => r.json());
}

function postJSON(url, data) {
  return fetch(url, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(data)
  }).then(r => r.json());
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
  let ws;
  function connect() {
    ws = new WebSocket('ws://' + location.host + '/ws');
    ws.onmessage = (evt) => {
      try {
        onMessage(JSON.parse(evt.data));
      } catch (e) { /* ignora frame malformado */ }
    };
    ws.onclose = () => setTimeout(connect, 1000);
    ws.onerror = () => ws.close();
  }
  connect();
}

// ----------------------------------------------------------------------------
// Heatmap do mapa de ignição (estilo TunerStudio/MegaLogViewer): grade RPM x
// TPS, cor com gradiente térmico calibrado pelo valor da célula e texto com
// contraste dinâmico de alta legibilidade.
// ----------------------------------------------------------------------------
function advanceToColor(v, vMin, vMax) {
  if (vMax <= vMin) vMax = vMin + 1;
  const t = Math.max(0, Math.min(1, (v - vMin) / (vMax - vMin)));
  // Gradiente térmico: Azul escuro -> Ciano -> Verde -> Amarelo -> Laranja -> Vermelho
  const stops = [
    [30, 60, 200],   // 0.0: Azul frio (baixo avanço)
    [0, 180, 210],   // 0.25: Ciano
    [40, 200, 80],   // 0.5: Verde
    [240, 210, 30],  // 0.75: Amarelo
    [230, 60, 30]    // 1.0: Vermelho quente (alto avanço)
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
