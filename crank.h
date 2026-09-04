#pragma once
#include <Arduino.h>

// ============================================================================
// crank.h - Decodificação da roda fônica 60-2 (Unique/Expert p/ Chevette)
//
// Estratégia (igual à usada por Speeduino/MegaSquirt para rodas -2/-1):
//  1. ISR mede o tempo (µs) entre dentes consecutivos.
//  2. Compara o período atual com o período médio recente. Se o período
//     atual for ~2x maior (o "buraco" onde faltam 2 dentes), marcamos essa
//     posição como referência (dente índice 0).
//  3. RPM é calculado a partir do período de um dente físico (6° cada).
//  4. O ângulo de virabrequim é mantido incrementando 6° a cada dente,
//     resetado no dente de referência, e ajustado por um offset de
//     calibração (ver `triggerOffsetDeg`) que você ajusta com lâmpada de
//     ponto contra a marca de TDC real do motor.
// ============================================================================

struct CrankState {
  volatile uint32_t lastToothTime_us = 0;
  volatile uint32_t lastPeriod_us = 0;
  volatile uint32_t avgPeriod_us = 0;       // média móvel curta, usada p/ detectar o gap
  volatile int32_t  toothIndex = -1;        // -1 = ainda não sincronizado
  volatile bool     synced = false;
  volatile float    rpm = 0;
  volatile uint32_t lastToothISR_us = 0;    // para watchdog de "motor parado"

  // Calibração do Ponto 0 (PMS / TDC Cilindro 1):
  // Quantos dentes após a falha/marca da polia o motor atinge o PMS (ponto 0),
  // somado ao ajuste fino em graus aferido com lâmpada estroboscópica.
  int   triggerTeethOffset = 14;     // Dentes após o gap (ex: 14 dentes * 6° = 84°)
  float triggerFineOffsetDeg = 0.0f; // Ajuste fino em graus (+/- graus)
  float triggerOffsetDeg = 84.0f;    // Offset total em graus calculado
};

extern CrankState crank;

void crank_init();
void IRAM_ATTR crank_isr();
float crank_getRPM();
float crank_getCrankAngleDeg();   // 0-359.99, referenciado ao TDC cil. 1 (após offset)
bool  crank_isSynced();
bool  crank_isEngineRunning();    // watchdog: sem dente há >300ms = parado
