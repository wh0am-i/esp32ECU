#include "crank.h"
#include "config.h"

CrankState crank;

// Janela de média móvel simples (evita alocação dinâmica, é só um filtro leve)
static uint32_t periodHistory[4] = {0, 0, 0, 0};
static uint8_t periodHistoryIdx = 0;

void crank_init() {
  pinMode(PIN_CRANK_SENSOR, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_CRANK_SENSOR), crank_isr, RISING);
}

void IRAM_ATTR crank_isr() {
  uint32_t now = micros();
  uint32_t period = now - crank.lastToothTime_us;
  crank.lastToothTime_us = now;
  crank.lastToothISR_us = now;

  // ignora ruído/bounce: período mínimo plausível a 10000 RPM num motor 4cc
  // (60 dentes/volta): a 10000rpm cada dente dura ~1000us. Abaixo disso é ruído.
  if (period < 300) {
    return;
  }

  // Atualiza média móvel curta ANTES de decidir se este é o "gap", usando
  // apenas períodos considerados normais (não o próprio gap)
  bool looksLikeGap = false;
  if (crank.avgPeriod_us > 0) {
    // gap de roda 60-2: o intervalo até o 1º dente após o buraco equivale a
    // ~3 dentes físicos (2 que faltam + 1 normal), então o período fica ~2x
    // a 3x maior que o período normal de um dente.
    looksLikeGap = period > (crank.avgPeriod_us * 3) / 2;
  }

  if (!looksLikeGap) {
    periodHistory[periodHistoryIdx] = period;
    periodHistoryIdx = (periodHistoryIdx + 1) % 4;
    uint32_t sum = 0;
    for (uint8_t i = 0; i < 4; i++) sum += periodHistory[i];
    crank.avgPeriod_us = sum / 4;
    crank.lastPeriod_us = period;
  }

  if (looksLikeGap) {
    // Dente de referência: reseta o índice. A partir daqui toothIndex=0
    // representa a posição física logo após o buraco.
    crank.toothIndex = 0;
    crank.synced = true;
  } else {
    if (crank.synced) {
      crank.toothIndex++;
      if (crank.toothIndex >= (WHEEL_TEETH_TOTAL - WHEEL_TEETH_MISSING)) {
        // não deveria chegar aqui sem ver outro gap antes; perdemos sincronismo
        crank.synced = false;
        crank.toothIndex = -1;
      }
    }
  }

  // RPM a partir do último período de dente válido (não o do gap)
  if (crank.lastPeriod_us > 0) {
    // graus/us -> RPM: (DEG_PER_TOOTH / periodo_us) * 1e6 * 60 / 360
    crank.rpm = (DEG_PER_TOOTH / (float)crank.lastPeriod_us) * 166666.666f;
  }
}

float crank_getRPM() {
  noInterrupts();
  float r = crank.rpm;
  interrupts();
  return r;
}

bool crank_isSynced() {
  return crank.synced;
}

bool crank_isEngineRunning() {
  return (micros() - crank.lastToothISR_us) < 300000UL; // 300ms sem dente = parado
}

float crank_getCrankAngleDeg() {
  if (!crank.synced) return -1.0f;

  noInterrupts();
  int32_t idx = crank.toothIndex;
  uint32_t lastTime = crank.lastToothTime_us;
  uint32_t period = crank.lastPeriod_us;
  interrupts();

  if (idx < 0 || period == 0) return -1.0f;

  // ângulo da última posição de dente conhecida
  float baseAngle = idx * DEG_PER_TOOTH;

  // extrapola o quanto o motor girou desde o último dente, usando o período
  // do dente anterior como estimativa de velocidade angular constante
  // (válido para a janela curta de 6 graus entre dentes)
  uint32_t elapsed = micros() - lastTime;
  float extraDeg = (DEG_PER_TOOTH * (float)elapsed) / (float)period;
  if (extraDeg > DEG_PER_TOOTH) extraDeg = DEG_PER_TOOTH; // clamp de segurança

  float totalOffset = (crank.triggerTeethOffset * DEG_PER_TOOTH) + crank.triggerFineOffsetDeg;
  crank.triggerOffsetDeg = totalOffset;

  // Quando o dente atual atinge o triggerTeethOffset, o ângulo é 0° (TDC cil. 1)
  float angle = (baseAngle + extraDeg) - totalOffset;
  while (angle < 0.0f) angle += 360.0f;
  while (angle >= 360.0f) angle -= 360.0f;
  return angle;
}
