#include "fueling.h"
#include "config.h"
#include <ESP32Servo.h>   // biblioteca "ESP32Servo" (Kevin Harrington) - instalar via Library Manager

FuelingConfig fuelConfig;

static Servo fuelServo;
static volatile int lastRawADC = 0;
static volatile float lastTpsPercent = 0;
static int lastServoAngle = 0;

// filtro simples (média móvel exponencial) pra reduzir ruído do ADC
static float filteredAdc = -1;

void fueling_init() {
  analogReadResolution(12); // 0-4095
  analogSetPinAttenuation(PIN_TPS_ADC, ADC_11db); // faixa até ~3.3V

  ESP32PWM::allocateTimer(0);
  fuelServo.setPeriodHertz(50);
  fuelServo.attach(PIN_SERVO_FUEL, 500, 2400); // pulso min/max típico em µs p/ MG996R
}

float tps_getPercent() {
  return lastTpsPercent;
}

int tps_getRawADC() {
  return lastRawADC;
}

int fueling_lastServoAngle() {
  return lastServoAngle;
}

void fueling_task() {
  int raw = analogRead(PIN_TPS_ADC);

  if (filteredAdc < 0) filteredAdc = raw;
  filteredAdc = filteredAdc * 0.8f + raw * 0.2f; // suaviza ruído sem atrasar demais a resposta

  lastRawADC = (int)filteredAdc;

  int span = fuelConfig.tpsAdcMax - fuelConfig.tpsAdcMin;
  float rawPct = 0;
  if (span != 0) {
    rawPct = (filteredAdc - fuelConfig.tpsAdcMin) * 100.0f / (float)span;
  }
  rawPct = constrain(rawPct, 0.0f, 100.0f);

  // Aplica limiar e zona morta configurada (ex: se tpsMinPercent=1%, leituras <= 1% viram 0.0% no código)
  float minT = fuelConfig.tpsMinPercent;
  float maxT = fuelConfig.tpsMaxPercent;
  if (maxT <= minT) maxT = minT + 1.0f;

  float pct = (rawPct - minT) * 100.0f / (maxT - minT);
  pct = constrain(pct, 0.0f, 100.0f);
  lastTpsPercent = pct;

  // mapeamento Alpha-N: ângulo do servo proporcional ao TPS
  float angleRange = fuelConfig.servoAngleAtFullThrottle - fuelConfig.servoAngleAtClosedThrottle;
  float angle = fuelConfig.servoAngleAtClosedThrottle + (pct / 100.0f) * angleRange;

  // correção opcional por RPM (declarada extern pra evitar dependência circular pesada)
  extern float crank_getRPM();
  float rpm = crank_getRPM();
  if (fuelConfig.rpmCompensationFactor != 1.0f && fuelConfig.rpmCompensationRef > 0) {
    float rpmRatio = constrain(rpm / fuelConfig.rpmCompensationRef, 0.0f, 1.0f);
    float extra = (fuelConfig.rpmCompensationFactor - 1.0f) * rpmRatio;
    angle = angle * (1.0f + extra);
  }

  angle = constrain(angle, 0.0f, 180.0f);
  lastServoAngle = (int)angle;
  fuelServo.write(lastServoAngle);
}
