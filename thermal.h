#pragma once
#include <Arduino.h>

// ============================================================================
// thermal.h - Sensor de Temperatura do Motor (CLT) e Controle da Ventoinha
// com Janela de Histerese configurável e Modo de Teste Manual
// ============================================================================

struct ThermalConfig {
  float fanTargetTemp = 92.0f;   // Temperatura de acionamento da ventoinha (°C)
  float fanHysteresis = 5.0f;    // Janela de histerese em °C (desliga em target - histerese, ex: 92 - 5 = 87°C)
  float overheatTemp = 100.0f;   // Temperatura de alerta crítico / superaquecimento (°C)
  
  // Calibração do sensor NTC (divisor com resistor pull-up 2.2k para 3.3V)
  float beta = 3950.0f;          // Coeficiente Beta típico do NTC automotivo
  float rPullup = 2200.0f;       // Resistor pull-up em ohms
  float r0 = 2000.0f;            // Resistência do sensor a 25°C
  float t0 = 298.15f;            // 25°C em Kelvin
};

extern ThermalConfig thermalConfig;

void thermal_init();
void thermal_task();
float thermal_getTemperature();
int   thermal_getRawADC();
bool  thermal_isFanOn();
bool  thermal_isOverheat();

// Controle manual / modo de teste (-1 = automático ECU, 0 = força desligado, 1 = força ligado)
void thermal_setFanOverride(int state);
int  thermal_getFanOverride();
