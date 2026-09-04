#pragma once
#include <Arduino.h>
#include "config.h"

// ============================================================================
// ignition.h - mapa de ignição (RPM x TPS), interpolação bilinear e
// agendamento dos disparos das duas saídas da bobina (wasted spark).
//
// MODELO DE DISPARO (wasted spark, 2 bobinas internas, 4 cilindros):
//  - Saída 1 (pino 1 da bobina, cil. 1+4) dispara 1x por volta do virabrequim
//  - Saída 2 (pino 3 da bobina, cil. 2+3) dispara 1x por volta, defasada 180°
//    da saída 1.
//  - Ângulo de referência: 0° = TDC do cilindro 1 (após aplicar
//    triggerOffsetDeg calibrado no crank.h).
// ============================================================================

struct IgnitionMap {
  // eixos (breakpoints) em RPM e % TPS
  float rpmAxis[IGN_MAP_RPM_BINS];
  float tpsAxis[IGN_MAP_TPS_BINS];
  // valores de avanço em graus BTDC, [tpsIndex][rpmIndex]
  float advance[IGN_MAP_TPS_BINS][IGN_MAP_RPM_BINS];
};

struct IgnitionConfig {
  float dwellMs = 3.0f;          // tempo de carga da bobina em milissegundos
  float maxDwellMs = 15.0f;      // watchdog de segurança (evita bobina queimar)
  float twoStepRpm = 4500.0f;    // RPM de corte quando botão 2-step pressionado
  bool  ignitionEnabled = true;  // corte geral de emergência (kill switch via webapp)
};

extern IgnitionMap ignMap;
extern IgnitionConfig ignConfig;

void ignition_init();
void ignition_task();                 // chamar periodicamente (~1ms) numa task dedicada -
                                       // só recalcula/reagenda os esp_timer de hardware que
                                       // realmente disparam as bobinas (ver ignition.cpp)
float ignition_getAdvanceForCurrentConditions(float rpm, float tpsPercent);
float ignition_lastAppliedAdvance();  // para telemetria/display
void ignition_loadDefaults();

// --- telemetria para o webapp (ilustração de ordem de disparo em tempo real) ---
uint32_t ignition_coilAFireCount();   // incrementa a cada centelha do par cil. 1+4
uint32_t ignition_coilBFireCount();   // incrementa a cada centelha do par cil. 2+3
bool     ignition_coilAEnergized();   // true = bobina A em dwell (carregando) agora
bool     ignition_coilBEnergized();
