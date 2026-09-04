#pragma once
#include <Arduino.h>

// ============================================================================
// fueling.h - Leitura do TPS e controle do servo MG996R (dosador de
// combustível) em modo Alpha-N (posição do servo proporcional à % de TPS).
// ============================================================================

struct FuelingConfig {
  // calibração do TPS (valores ADC brutos, 0-4095, no batente fechado/aberto)
  int   tpsAdcMin = 200;    // ADC na borboleta fechada
  int   tpsAdcMax = 3800;   // ADC na borboleta 100% aberta

  // zona morta e limiares de corte em % (ex: 1.0% vira 0% no código)
  float tpsMinPercent = 1.0f;  // Leituras <= tpsMinPercent são tratadas como 0.0%
  float tpsMaxPercent = 99.0f; // Leituras >= tpsMaxPercent são tratadas como 100.0%

  // faixa de movimento do servo (em graus, 0-180) mapeada para TPS 0-100%
  int   servoAngleAtClosedThrottle = 10;
  int   servoAngleAtFullThrottle   = 170;

  // correção simples por RPM (enriquecimento em alta rotação), opcional.
  // fatorRpmMax multiplica a abertura do servo no RPM de referência rpmRef.
  float rpmCompensationRef = 6000.0f;
  float rpmCompensationFactor = 1.0f;  // 1.0 = desativado (sem correção)
};

extern FuelingConfig fuelConfig;

void fueling_init();
void fueling_task();          // lê TPS, aplica no servo - chamar periodicamente (não precisa ser tão rápido quanto ignition_task)
float tps_getPercent();       // 0.0 - 100.0
int   tps_getRawADC();
int   fueling_lastServoAngle();
