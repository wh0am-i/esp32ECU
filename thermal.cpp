#include "thermal.h"
#include "config.h"
#include <math.h>

ThermalConfig thermalConfig;

static float lastTempCelsius = 25.0f;
static volatile int lastRawCltAdc = 0;
static bool fanRunning = false;
static int fanOverrideState = -1; // -1 = Auto, 0 = Force OFF, 1 = Force ON
static float filteredCltAdc = -1.0f;

void thermal_init() {
  pinMode(PIN_FAN_RELAY, OUTPUT);
  digitalWrite(PIN_FAN_RELAY, FAN_ACTIVE_HIGH ? LOW : HIGH);
  analogSetPinAttenuation(PIN_CLT_ADC, ADC_11db);
}

float thermal_getTemperature() {
  return lastTempCelsius;
}

int thermal_getRawADC() {
  return lastRawCltAdc;
}

bool thermal_isFanOn() {
  return fanRunning;
}

bool thermal_isOverheat() {
  return lastTempCelsius >= thermalConfig.overheatTemp;
}

void thermal_setFanOverride(int state) {
  fanOverrideState = state;
}

int thermal_getFanOverride() {
  return fanOverrideState;
}

void thermal_task() {
  int raw = analogRead(PIN_CLT_ADC);
  lastRawCltAdc = raw;

  if (filteredCltAdc < 0) filteredCltAdc = raw;
  filteredCltAdc = filteredCltAdc * 0.85f + raw * 0.15f;

  // Cálculo de temperatura via NTC (Beta formula)
  float adcVal = filteredCltAdc;
  if (adcVal < 10.0f) adcVal = 10.0f;
  if (adcVal > 4085.0f) adcVal = 4085.0f;

  float rNtc = thermalConfig.rPullup / ((4095.0f / adcVal) - 1.0f);
  if (rNtc > 0 && thermalConfig.r0 > 0 && thermalConfig.beta > 0) {
    float steinhart = log(rNtc / thermalConfig.r0) / thermalConfig.beta;
    steinhart += 1.0f / thermalConfig.t0;
    float tempK = 1.0f / steinhart;
    lastTempCelsius = tempK - 273.15f;
  }

  // Lógica de acionamento da ventoinha com histerese
  if (fanOverrideState != -1) {
    // Modo de teste manual ativo
    fanRunning = (fanOverrideState == 1);
  } else {
    // Modo automático: liga no target e desliga após cair a janela de histerese
    float turnOnTemp = thermalConfig.fanTargetTemp;
    float turnOffTemp = thermalConfig.fanTargetTemp - thermalConfig.fanHysteresis;

    if (lastTempCelsius >= turnOnTemp) {
      fanRunning = true;
    } else if (lastTempCelsius <= turnOffTemp) {
      fanRunning = false;
    }
  }

  digitalWrite(PIN_FAN_RELAY, (fanRunning == FAN_ACTIVE_HIGH) ? HIGH : LOW);
}
