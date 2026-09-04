#include "ignition.h"
#include "crank.h"
#include "fueling.h"   // para ler TPS atual (tps_getPercent)
#include "esp_timer.h"
#include "driver/gpio.h"

// ============================================================================
// ignition.cpp
//
// ARQUITETURA DE DISPARO (revisada): em vez de uma task em loop apertado
// comparando o ângulo do virabrequim a cada iteração ("polling"), usamos 4
// esp_timer de hardware (ESP_TIMER_ISR) - um para o início do dwell e um para
// o disparo de cada bobina. A task ignition_task() só roda a cada ~1ms
// (chamada de esp32_ecu.ino) para calcular, a partir do RPM/TPS atuais, QUANDO
// (em microssegundos) o próximo evento de cada bobina deve acontecer, e
// agenda esse instante num esp_timer. O próprio callback do esp_timer é quem
// liga/desliga o pino da bobina - direto, sem depender do escalonador do
// FreeRTOS ter chance de rodar a task a tempo.
//
// Isso resolve duas coisas de uma vez:
//  1. O core 0 deixa de ficar girando 100% do tempo só comparando ângulo -
//     ignition_task() dorme a maior parte do tempo (vTaskDelay no .ino) e o
//     WiFi/rádio (que também usa o core 0 por padrão no Arduino-ESP32) fica
//     com CPU sobrando de verdade, em vez de brigar por fatias de tempo com
//     um loop apertado.
//  2. A precisão do disparo passa a depender só da latência do próprio
//     esp_timer em modo ISR (tipicamente poucos microssegundos, documentado
//     pela Espressif), e não da velocidade/carga da task - ou seja, não
//     degrada se o webserver/logger estiverem ocupados no outro core.
//
// Cada bobina só é reagendada quando o timer dela NÃO está mais pendente
// (ou seja, logo depois que ela disparou). Isso evita ficar cancelando e
// reagendando um esp_timer bem no instante em que ele ia disparar (condição
// de corrida), e também é o que ECUs de verdade fazem: o avanço/dwell de um
// evento de centelha é "travado" no início daquele evento, não recalculado
// continuamente no meio dele.
// ============================================================================

IgnitionMap ignMap;
IgnitionConfig ignConfig;

static float lastAdvanceApplied = 10.0f;

// cache lido pelos callbacks de ISR - atualizado só pela ignition_task()
static volatile bool cutSparkCached = true;   // começa em "corte" por segurança

// estado observável das bobinas (para watchdog e telemetria)
static volatile bool coilAEnergized = false;
static volatile bool coilBEnergized = false;
static volatile uint32_t coilAFireCount = 0;
static volatile uint32_t coilBFireCount = 0;

static esp_timer_handle_t timerDwellA = nullptr;
static esp_timer_handle_t timerFireA  = nullptr;
static esp_timer_handle_t timerDwellB = nullptr;
static esp_timer_handle_t timerFireB  = nullptr;

// ----------------------------------------------------------------------------
// Callbacks de ISR (rodam em contexto de interrupção - devem ser curtos,
// IRAM_ATTR, e usar gpio_set_level em vez de digitalWrite para evitar
// depender de código que talvez não esteja garantido em IRAM).
// ----------------------------------------------------------------------------
static void IRAM_ATTR onDwellA(void *arg) {
  if (!cutSparkCached) {
    gpio_set_level((gpio_num_t)PIN_IGN_DRIVER_1, IGN_ACTIVE_HIGH ? 1 : 0);
    coilAEnergized = true;
  }
}
static void IRAM_ATTR onFireA(void *arg) {
  gpio_set_level((gpio_num_t)PIN_IGN_DRIVER_1, IGN_ACTIVE_HIGH ? 0 : 1);
  coilAEnergized = false;
  coilAFireCount++;
}
static void IRAM_ATTR onDwellB(void *arg) {
  if (!cutSparkCached) {
    gpio_set_level((gpio_num_t)PIN_IGN_DRIVER_2, IGN_ACTIVE_HIGH ? 1 : 0);
    coilBEnergized = true;
  }
}
static void IRAM_ATTR onFireB(void *arg) {
  gpio_set_level((gpio_num_t)PIN_IGN_DRIVER_2, IGN_ACTIVE_HIGH ? 0 : 1);
  coilBEnergized = false;
  coilBFireCount++;
}

static void killAllOutputs() {
  if (timerDwellA && esp_timer_is_active(timerDwellA)) esp_timer_stop(timerDwellA);
  if (timerFireA  && esp_timer_is_active(timerFireA))  esp_timer_stop(timerFireA);
  if (timerDwellB && esp_timer_is_active(timerDwellB)) esp_timer_stop(timerDwellB);
  if (timerFireB  && esp_timer_is_active(timerFireB))  esp_timer_stop(timerFireB);
  gpio_set_level((gpio_num_t)PIN_IGN_DRIVER_1, IGN_ACTIVE_HIGH ? 0 : 1);
  gpio_set_level((gpio_num_t)PIN_IGN_DRIVER_2, IGN_ACTIVE_HIGH ? 0 : 1);
  coilAEnergized = false;
  coilBEnergized = false;
}

void ignition_loadDefaults() {
  // eixos padrão: RPM de 500 a 7500, TPS de 0 a 100%
  for (int i = 0; i < IGN_MAP_RPM_BINS; i++) {
    ignMap.rpmAxis[i] = 500.0f + i * ((7500.0f - 500.0f) / (IGN_MAP_RPM_BINS - 1));
  }
  for (int j = 0; j < IGN_MAP_TPS_BINS; j++) {
    ignMap.tpsAxis[j] = j * (100.0f / (IGN_MAP_TPS_BINS - 1));
  }
  // mapa conservador de partida: baixo avanço em carga alta/RPM alto,
  // mais avanço em carga baixa/RPM médio. AJUSTE ISSO NO BANCO/DINAMÔMETRO.
  for (int j = 0; j < IGN_MAP_TPS_BINS; j++) {
    for (int i = 0; i < IGN_MAP_RPM_BINS; i++) {
      float base = 10.0f + (ignMap.rpmAxis[i] / 7500.0f) * 20.0f;   // 10-30 graus
      float loadCorrection = (100.0f - ignMap.tpsAxis[j]) / 100.0f * 8.0f; // até +8 graus em carga baixa
      ignMap.advance[j][i] = base + loadCorrection;
    }
  }
}

static esp_timer_handle_t createIsrTimer(esp_timer_cb_t cb, const char *name) {
  esp_timer_handle_t h = nullptr;
  esp_timer_create_args_t args = {};
  args.callback = cb;
  args.arg = nullptr;
  args.dispatch_method = ESP_TIMER_ISR;  // callback roda direto em contexto de ISR
  args.name = name;
  esp_err_t err = esp_timer_create(&args, &h);
  if (err != ESP_OK) {
    Serial.printf("ERRO: falha ao criar esp_timer '%s' (err=%d)\n", name, err);
  }
  return h;
}

void ignition_init() {
  pinMode(PIN_IGN_DRIVER_1, OUTPUT);
  pinMode(PIN_IGN_DRIVER_2, OUTPUT);
  pinMode(PIN_BUTTON_2STEP, INPUT_PULLUP);
  killAllOutputs();

  timerDwellA = createIsrTimer(&onDwellA, "ign_dwellA");
  timerFireA  = createIsrTimer(&onFireA,  "ign_fireA");
  timerDwellB = createIsrTimer(&onDwellB, "ign_dwellB");
  timerFireB  = createIsrTimer(&onFireB,  "ign_fireB");

  ignition_loadDefaults();
}

static float interpolate1D(float x, const float *axis, int n, int *loIdxOut, float *fracOut) {
  if (x <= axis[0]) { *loIdxOut = 0; *fracOut = 0; return 0; }
  if (x >= axis[n - 1]) { *loIdxOut = n - 2; *fracOut = 1; return 0; }
  for (int i = 0; i < n - 1; i++) {
    if (x >= axis[i] && x <= axis[i + 1]) {
      *loIdxOut = i;
      *fracOut = (x - axis[i]) / (axis[i + 1] - axis[i]);
      return 0;
    }
  }
  *loIdxOut = 0; *fracOut = 0;
  return 0;
}

float ignition_getAdvanceForCurrentConditions(float rpm, float tpsPercent) {
  int rI, tI; float rF, tF;
  interpolate1D(rpm, ignMap.rpmAxis, IGN_MAP_RPM_BINS, &rI, &rF);
  interpolate1D(tpsPercent, ignMap.tpsAxis, IGN_MAP_TPS_BINS, &tI, &tF);

  float v00 = ignMap.advance[tI][rI];
  float v10 = ignMap.advance[tI][rI + 1];
  float v01 = ignMap.advance[tI + 1][rI];
  float v11 = ignMap.advance[tI + 1][rI + 1];

  float vTop = v00 + (v10 - v00) * rF;
  float vBot = v01 + (v11 - v01) * rF;
  return vTop + (vBot - vTop) * tF;
}

float ignition_lastAppliedAdvance() {
  return lastAdvanceApplied;
}

uint32_t ignition_coilAFireCount() { return coilAFireCount; }
uint32_t ignition_coilBFireCount() { return coilBFireCount; }
bool     ignition_coilAEnergized() { return coilAEnergized; }
bool     ignition_coilBEnergized() { return coilBEnergized; }

static inline float normalizeAngle(float a) {
  while (a < 0) a += 360.0f;
  while (a >= 360.0f) a -= 360.0f;
  return a;
}

// distância angular PARA FRENTE de `from` até `to` (0-360, nunca negativa)
static inline float angleForwardDelta(float from, float to) {
  float d = to - from;
  while (d < 0) d += 360.0f;
  while (d >= 360.0f) d -= 360.0f;
  return d;
}

static const int64_t MIN_SCHEDULE_US = 5; // esp_timer exige um atraso > 0

static inline void scheduleTimer(esp_timer_handle_t timer, float deltaDeg, float usPerDeg) {
  if (!timer) return;
  int64_t us = (int64_t)(deltaDeg * usPerDeg);
  if (us < MIN_SCHEDULE_US) us = MIN_SCHEDULE_US;
  esp_timer_start_once(timer, (uint64_t)us);
}

// ----------------------------------------------------------------------------
// ignition_task(): chamar a cada ~1ms (task dedicada, ver esp32_ecu.ino).
// NÃO dispara bobina diretamente - só (re)agenda os esp_timer de hardware
// que fazem isso. Cada bobina só é reagendada quando já disparou (timer
// correspondente livre), travando o avanço calculado para aquele evento.
// ----------------------------------------------------------------------------
void ignition_task() {
  if (!crank_isEngineRunning() || !crank_isSynced()) {
    killAllOutputs();
    return;
  }

  float rpm = crank_getRPM();
  if (rpm < 50.0f) { // RPM espúrio/motor de arranque girando devagar demais pra confiar
    killAllOutputs();
    return;
  }

  float angle = crank_getCrankAngleDeg();
  if (angle < 0) return; // ainda sincronizando

  bool dwellAIdle = !timerDwellA || !esp_timer_is_active(timerDwellA);
  bool fireAIdle  = !timerFireA  || !esp_timer_is_active(timerFireA);
  bool dwellBIdle = !timerDwellB || !esp_timer_is_active(timerDwellB);
  bool fireBIdle  = !timerFireB  || !esp_timer_is_active(timerFireB);
  if (!(dwellAIdle || fireAIdle || dwellBIdle || fireBIdle)) {
    return; // tudo já agendado, nada a fazer até o próximo evento disparar
  }

  float tps = tps_getPercent();

  bool cutSpark = !ignConfig.ignitionEnabled;
  if (digitalRead(PIN_BUTTON_2STEP) == LOW && rpm > ignConfig.twoStepRpm) cutSpark = true;
  cutSparkCached = cutSpark;

  float advance = ignition_getAdvanceForCurrentConditions(rpm, tps);
  lastAdvanceApplied = advance;

  // ângulo de disparo (0° = TDC cil.1): "advance" graus ANTES do TDC
  float fireAngleA = normalizeAngle(360.0f - advance);
  float fireAngleB = normalizeAngle(fireAngleA + 180.0f);

  // dwell efetivo nunca ultrapassa o watchdog térmico maxDwellMs
  float effectiveDwellMs = min(ignConfig.dwellMs, ignConfig.maxDwellMs);
  float dwellDeg = effectiveDwellMs * rpm * 0.006f; // deg = ms * RPM * 0.006
  if (dwellDeg > 300.0f) dwellDeg = 300.0f;         // clamp de segurança (RPM baixíssimo)
  float dwellStartA = normalizeAngle(fireAngleA - dwellDeg);
  float dwellStartB = normalizeAngle(fireAngleB - dwellDeg);

  float usPerDeg = 166666.666f / rpm; // graus -> microssegundos na RPM atual

  if (dwellAIdle) scheduleTimer(timerDwellA, angleForwardDelta(angle, dwellStartA), usPerDeg);
  if (fireAIdle)  scheduleTimer(timerFireA,  angleForwardDelta(angle, fireAngleA),  usPerDeg);
  if (dwellBIdle) scheduleTimer(timerDwellB, angleForwardDelta(angle, dwellStartB), usPerDeg);
  if (fireBIdle)  scheduleTimer(timerFireB,  angleForwardDelta(angle, fireAngleB),  usPerDeg);
}
