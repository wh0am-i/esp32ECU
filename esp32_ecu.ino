/*
 * ============================================================================
 * ECU ESP32 - Chevette (roda fônica 60-2 Unique + bobina Magneti Marelli
 * BI0017MM / F000ZS0210 / 032905106B / U2003)
 * ============================================================================
 *
 * ATENÇÃO - LEIA ANTES DE LIGAR NO MOTOR:
 *   - Este firmware NUNCA foi testado num motor real. Teste primeiro em
 *     bancada: gire o sensor manualmente ou com furadeira e confira no
 *     display/webapp se RPM e sincronismo aparecem corretos, e confira com
 *     osciloscópio se os pinos IGN_DRIVER_1/2 têm o comportamento esperado
 *     ANTES de conectar aos drivers de verdade e à bobina.
 *   - Confirme a polaridade do sinal de disparo (IGN_ACTIVE_HIGH em
 *     config.h) com osciloscópio antes de energizar a bobina de fato.
 *   - O avanço de ignição default (ignition_loadDefaults em ignition.cpp) é
 *     um mapa CONSERVADOR de partida. Ele precisa ser calibrado com lâmpada
 *     de ponto e, idealmente, banco de potência/dinamômetro.
 *   - NÃO ligue os GPIOs diretamente nos pinos 1/3 da bobina. Use o circuito
 *     driver (transistor/MOSTFET + isolamento) discutido anteriormente.
 *
 * BIBLIOTECAS NECESSÁRIAS (Arduino Library Manager):
 *   - ESPAsyncWebServer (me-no-dev / mathieucarbou fork compatível)
 *   - AsyncTCP
 *   - ArduinoJson (v6.x)
 *   - Adafruit GFX Library
 *   - Adafruit ST7735 and ST7789 Library
 *   - ESP32Servo
 *   - LittleFS (já vem no core ESP32 Arduino >= 2.x)
 *
 * ANTES DE COMPILAR: faça o upload da pasta data/ para o LittleFS
 * (Ferramentas > ESP32 Sketch Data Upload, ou "pio run -t uploadfs" no
 * PlatformIO) - é ela que contém o webapp (HTML/CSS/JS).
 * ============================================================================
 */

#include "config.h"
#include "crank.h"
#include "ignition.h"
#include "fueling.h"
#include "display.h"
#include "storage.h"
#include "logger.h"
#include "webserver.h"

static TaskHandle_t ignitionTaskHandle = nullptr;

// ----------------------------------------------------------------------------
// Task dedicada de ignição - roda no CORE 0, isolada do WiFi/webserver/
// display (que ficam no CORE 1 via loop() padrão do Arduino).
//
// IMPORTANTE: esta task NÃO fica em loop apertado comparando ângulo a cada
// iteração. Quem realmente dispara a bobina são 4 esp_timer de hardware
// (ver ignition.cpp), cujos callbacks rodam em contexto de interrupção com
// poucos microssegundos de latência, independente do que mais está
// acontecendo no sistema. Esta task só acorda a cada 1ms pra recalcular o
// avanço (RPM x TPS) e reagendar, em cada bobina, o PRÓXIMO evento - e só
// quando o evento anterior daquela bobina já disparou. Isso mantém o core 0
// livre pro WiFi na maior parte do tempo, em vez de ocupado 100% num
// polling que, além de esquentar a CPU à toa, dificulta diagnosticar
// qualquer outro problema de timing que apareça depois.
// ----------------------------------------------------------------------------
static void ignitionTaskFn(void *param) {
  for (;;) {
    ignition_task();
    vTaskDelay(pdMS_TO_TICKS(1)); // ~1kHz é de sobra: só precisa reagir bem antes
                                   // do próximo evento de bobina (ms) chegar
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== ECU ESP32 Chevette - iniciando ===");

  storage_init();      // carrega mapa/config salvos (ou aplica defaults)
  crank_init();
  ignition_init();
  fueling_init();
  display_init();
  logger_init();
  webserver_init();

  // task de ignição isolada no core 0, prioridade alta (mas abaixo de tasks
  // do sistema WiFi que ficam no core 0 padrão do Arduino-ESP32 - por isso
  // fixamos ela lá mesmo: ignição tem que competir bem com o rádio, então
  // damos prioridade alta pra ela vencer a disputa de CPU quando importa)
  xTaskCreatePinnedToCore(
    ignitionTaskFn,
    "ignition_task",
    4096,
    nullptr,
    configMAX_PRIORITIES - 1,   // prioridade máxima permitida ao usuário
    &ignitionTaskHandle,
    0  // core 0
  );

  Serial.println("=== Setup concluido ===");
}

// ----------------------------------------------------------------------------
// loop() roda no core 1 por padrão no Arduino-ESP32: cuida de tudo que NÃO
// é tempo-crítico em microssegundos (fueling, display, logging, webserver).
// ----------------------------------------------------------------------------
void loop() {
  fueling_task();     // lê TPS e move o servo (não precisa de altíssima frequência)
  logger_task();       // amostra pro gráfico de log (taxa definida em config.h)
  webserver_task();    // push periódico via WebSocket pro webapp

  static uint32_t lastDisplayMs = 0;
  uint32_t now = millis();
  if (now - lastDisplayMs >= 150) { // ~6-7 fps é suficiente pro display
    lastDisplayMs = now;
    bool twoStep = (digitalRead(PIN_BUTTON_2STEP) == LOW) && (crank_getRPM() > ignConfig.twoStepRpm);
    display_update(tps_getPercent(), crank_getRPM(), ignition_lastAppliedAdvance(), twoStep, crank_isSynced());
  }

  delay(1); // dá espaço pro scheduler/WiFi sem atrasar percetivelmente o resto
}
