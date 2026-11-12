#include "webserver.h"
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <math.h>
#include "config.h"
#include "ignition.h"
#include "fueling.h"
#include "crank.h"
#include "thermal.h"
#include "logger.h"
#include "storage.h"

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");
static uint32_t lastWsPush = 0;

// O corpo pode chegar em varios pacotes. O servidor libera _tempObject
// ao destruir a requisicao, inclusive se o cliente desconectar no meio.
static bool readJsonBody(AsyncWebServerRequest *request, JsonDocument &doc,
                         uint8_t *data, size_t len, size_t index, size_t total) {
  if (!total || total > 16384) {
    if (!index) request->send(413, "application/json", "{\"error\":\"corpo excede 16 KB\"}");
    return false;
  }
  if (!index) {
    request->_tempObject = malloc(total);
    if (!request->_tempObject) {
      request->send(503, "application/json", "{\"error\":\"sem memoria\"}");
      return false;
    }
  }
  if (!request->_tempObject) return false;
  if (index > total || len > total-index) {
    request->send(400, "application/json", "{\"error\":\"corpo invalido\"}");
    free(request->_tempObject); request->_tempObject = nullptr;
    return false;
  }
  memcpy(static_cast<uint8_t*>(request->_tempObject)+index, data, len);
  if (index+len != total) return false;
  auto err = deserializeJson(doc, static_cast<const char*>(request->_tempObject), total);
  free(request->_tempObject); request->_tempObject = nullptr;
  if (err || !doc.is<JsonObject>()) {
    request->send(400, "application/json", "{\"error\":\"json invalido\"}");
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// /api/status (também espelhado via WebSocket) - dados de monitoramento
// ---------------------------------------------------------------------------
static void buildStatusJson(JsonDocument &doc) {
  float rpm = crank_getRPM();
  doc["rpm"] = rpm;
  doc["tps"] = tps_getPercent();
  doc["tpsRaw"] = tps_getRawADC();
  doc["advance"] = ignition_lastAppliedAdvance();
  doc["synced"] = crank_isSynced();
  doc["running"] = crank_isEngineRunning();
  doc["servoAngle"] = fueling_lastServoAngle();
  
  // Status do botão 2-step e corte de giro
  bool twoStepPressed = (digitalRead(PIN_BUTTON_2STEP) == LOW);
  doc["twoStepPressed"] = twoStepPressed;
  doc["twoStepCutting"] = twoStepPressed && (rpm > ignConfig.twoStepRpm);
  doc["twoStepRpm"] = ignConfig.twoStepRpm;
  
  // Corte de giro final (hard cut max RPM)
  doc["maxRpm"] = ignConfig.maxRpm;
  doc["revLimitCutting"] = crank_isEngineRunning() && (rpm >= ignConfig.maxRpm);

  doc["ignitionEnabled"] = ignConfig.ignitionEnabled;

  // Dados térmicos e ventoinha
  doc["temp"] = thermal_getTemperature();
  doc["tempRaw"] = thermal_getRawADC();
  doc["fanOn"] = thermal_isFanOn();
  doc["fanOverride"] = thermal_getFanOverride();
  doc["servoOverride"] = fueling_getServoOverride();
  doc["testMode"] = (thermal_getFanOverride() != -1) || (fueling_getServoOverride() != -1);
  doc["overheat"] = thermal_isOverheat();
  doc["fanTargetTemp"] = thermalConfig.fanTargetTemp;
  doc["fanHysteresis"] = thermalConfig.fanHysteresis;
  doc["fanOnTemp"] = thermalConfig.fanTargetTemp;
  doc["fanOffTemp"] = thermalConfig.fanTargetTemp - thermalConfig.fanHysteresis;
  doc["overheatTemp"] = thermalConfig.overheatTemp;

  // Telemetria de bobinas
  doc["coilAFireCount"] = ignition_coilAFireCount();
  doc["coilBFireCount"] = ignition_coilBFireCount();
  doc["coilACharging"] = ignition_coilAEnergized();
  doc["coilBCharging"] = ignition_coilBEnergized();
}

// ---------------------------------------------------------------------------
// /api/config  GET/POST
// ---------------------------------------------------------------------------
static void handleGetConfig(AsyncWebServerRequest *request) {
  DynamicJsonDocument doc(1024);
  doc["dwellMs"] = ignConfig.dwellMs;
  doc["maxDwellMs"] = ignConfig.maxDwellMs;
  doc["twoStepRpm"] = ignConfig.twoStepRpm;
  doc["maxRpm"] = ignConfig.maxRpm;
  doc["ignitionEnabled"] = ignConfig.ignitionEnabled;

  doc["tpsAdcMin"] = fuelConfig.tpsAdcMin;
  doc["tpsAdcMax"] = fuelConfig.tpsAdcMax;
  doc["tpsMinPercent"] = fuelConfig.tpsMinPercent;
  doc["tpsMaxPercent"] = fuelConfig.tpsMaxPercent;
  doc["servoAngleAtClosedThrottle"] = fuelConfig.servoAngleAtClosedThrottle;
  doc["servoAngleAtFullThrottle"] = fuelConfig.servoAngleAtFullThrottle;
  doc["rpmCompensationRef"] = fuelConfig.rpmCompensationRef;
  doc["rpmCompensationFactor"] = fuelConfig.rpmCompensationFactor;

  doc["triggerTeethOffset"] = crank.triggerTeethOffset;
  doc["triggerFineOffsetDeg"] = crank.triggerFineOffsetDeg;
  doc["triggerOffsetDeg"] = crank.triggerOffsetDeg;

  doc["fanTargetTemp"] = thermalConfig.fanTargetTemp;
  doc["fanHysteresis"] = thermalConfig.fanHysteresis;
  doc["fanOnTemp"] = thermalConfig.fanTargetTemp;
  doc["fanOffTemp"] = thermalConfig.fanTargetTemp - thermalConfig.fanHysteresis;
  doc["overheatTemp"] = thermalConfig.overheatTemp;

  String out;
  serializeJson(doc, out);
  request->send(200, "application/json", out);
}

// body handler acumula o corpo do POST (pode vir em vários pacotes)
static void handlePostConfigBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  DynamicJsonDocument doc(1024);
  if (!readJsonBody(request, doc, data, len, index, total)) return;

  if (doc.containsKey("dwellMs")) ignConfig.dwellMs = doc["dwellMs"];
  if (doc.containsKey("maxDwellMs")) ignConfig.maxDwellMs = doc["maxDwellMs"];
  if (doc.containsKey("twoStepRpm")) ignConfig.twoStepRpm = doc["twoStepRpm"];
  if (doc.containsKey("maxRpm")) {
    ignConfig.maxRpm = doc["maxRpm"];
    // O limitador nao modifica o mapa.
  }
  if (doc.containsKey("ignitionEnabled")) ignConfig.ignitionEnabled = doc["ignitionEnabled"];
  storage_saveIgnitionConfig();

  if (doc.containsKey("fanTargetTemp")) thermalConfig.fanTargetTemp = doc["fanTargetTemp"];
  else if (doc.containsKey("fanOnTemp")) thermalConfig.fanTargetTemp = doc["fanOnTemp"];
  
  if (doc.containsKey("fanHysteresis")) thermalConfig.fanHysteresis = doc["fanHysteresis"];
  else if (doc.containsKey("fanOffTemp")) thermalConfig.fanHysteresis = thermalConfig.fanTargetTemp - doc["fanOffTemp"].as<float>();

  if (doc.containsKey("overheatTemp")) thermalConfig.overheatTemp = doc["overheatTemp"];
  storage_saveThermalConfig();

  if (doc.containsKey("tpsAdcMin")) fuelConfig.tpsAdcMin = doc["tpsAdcMin"];
  if (doc.containsKey("tpsAdcMax")) fuelConfig.tpsAdcMax = doc["tpsAdcMax"];
  if (doc.containsKey("tpsMinPercent")) fuelConfig.tpsMinPercent = doc["tpsMinPercent"];
  if (doc.containsKey("tpsMaxPercent")) fuelConfig.tpsMaxPercent = doc["tpsMaxPercent"];
  if (doc.containsKey("servoAngleAtClosedThrottle")) fuelConfig.servoAngleAtClosedThrottle = doc["servoAngleAtClosedThrottle"];
  if (doc.containsKey("servoAngleAtFullThrottle")) fuelConfig.servoAngleAtFullThrottle = doc["servoAngleAtFullThrottle"];
  if (doc.containsKey("rpmCompensationRef")) fuelConfig.rpmCompensationRef = doc["rpmCompensationRef"];
  if (doc.containsKey("rpmCompensationFactor")) fuelConfig.rpmCompensationFactor = doc["rpmCompensationFactor"];
  storage_saveFuelingConfig();

  if (doc.containsKey("triggerTeethOffset") || doc.containsKey("triggerFineOffsetDeg") || doc.containsKey("triggerOffsetDeg")) {
    if (doc.containsKey("triggerTeethOffset")) crank.triggerTeethOffset = doc["triggerTeethOffset"];
    if (doc.containsKey("triggerFineOffsetDeg")) crank.triggerFineOffsetDeg = doc["triggerFineOffsetDeg"];
    if (doc.containsKey("triggerOffsetDeg") && !doc.containsKey("triggerTeethOffset")) {
      crank.triggerOffsetDeg = doc["triggerOffsetDeg"];
    } else {
      crank.triggerOffsetDeg = (crank.triggerTeethOffset * DEG_PER_TOOTH) + crank.triggerFineOffsetDeg;
    }
    storage_saveCrankCalibration();
  }

  request->send(200, "application/json", "{\"ok\":true}");
}

// ---------------------------------------------------------------------------
// /api/actuator/test  POST - Modo de teste manual de atuadores (ventoinha e servo)
// ---------------------------------------------------------------------------
static void handlePostActuatorTestBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  DynamicJsonDocument doc(512);
  if (!readJsonBody(request, doc, data, len, index, total)) return;

  if (doc.containsKey("stop") && doc["stop"].as<bool>()) {
    // Parar todos os testes e retornar ao modo automático imediato
    thermal_setFanOverride(-1);
    fueling_setServoOverride(-1);
  } else {
    if (doc.containsKey("fan")) {
      int f = doc["fan"].as<int>();
      thermal_setFanOverride(f);
    }
    if (doc.containsKey("servo")) {
      int s = doc["servo"].as<int>();
      fueling_setServoOverride(s);
    }
  }

  request->send(200, "application/json", "{\"ok\":true}");
}

// ---------------------------------------------------------------------------
// /api/map  GET/POST  - mapa de ignição (heatmap RPM x TPS)
// ---------------------------------------------------------------------------
static void handleGetMap(AsyncWebServerRequest *request) {
  DynamicJsonDocument doc(12288);
  JsonArray rpmAxis = doc.createNestedArray("rpmAxis");
  for (int i = 0; i < IGN_MAP_RPM_BINS; i++) rpmAxis.add(ignMap.rpmAxis[i]);
  JsonArray tpsAxis = doc.createNestedArray("tpsAxis");
  for (int j = 0; j < IGN_MAP_TPS_BINS; j++) tpsAxis.add(ignMap.tpsAxis[j]);
  JsonArray table = doc.createNestedArray("table");
  for (int j = 0; j < IGN_MAP_TPS_BINS; j++) {
    JsonArray row = table.createNestedArray();
    for (int i = 0; i < IGN_MAP_RPM_BINS; i++) row.add(ignMap.advance[j][i]);
  }
  String out;
  serializeJson(doc, out);
  request->send(200, "application/json", out);
}

static void handlePostMapBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  DynamicJsonDocument doc(12288);
  if (!readJsonBody(request, doc, data, len, index, total)) return;

  // Valida o corpo inteiro antes de modificar qualquer celula.
  if (doc.containsKey("table")) {
    JsonArray table = doc["table"].as<JsonArray>();
    bool valid = table.size() == IGN_MAP_TPS_BINS;
    for (JsonVariant rowValue : table) {
      JsonArray row = rowValue.as<JsonArray>();
      if (row.size() != IGN_MAP_RPM_BINS) valid = false;
      for (JsonVariant value : row)
        if (!value.is<float>() || !isfinite(value.as<float>())) valid = false;
    }
    if (!valid) {
      request->send(400, "application/json", "{\"error\":\"mapa deve conter 16x16 numeros finitos\"}");
      return;
    }
    for (int j=0; j<IGN_MAP_TPS_BINS; ++j)
      for (int i=0; i<IGN_MAP_RPM_BINS; ++i) ignMap.advance[j][i] = table[j][i].as<float>();
  } else {
    if (!doc["row"].is<int>() || !doc["col"].is<int>() || !doc["value"].is<float>() ||
        !isfinite(doc["value"].as<float>()) || doc["row"].as<int>() < 0 ||
        doc["row"].as<int>() >= IGN_MAP_TPS_BINS || doc["col"].as<int>() < 0 ||
        doc["col"].as<int>() >= IGN_MAP_RPM_BINS) {
      request->send(400, "application/json", "{\"error\":\"celula invalida\"}");
      return;
    }
    ignMap.advance[doc["row"].as<int>()][doc["col"].as<int>()] = doc["value"].as<float>();
  }
  storage_saveIgnitionMap();
  request->send(200, "application/json", "{\"ok\":true}");
}

// ---------------------------------------------------------------------------
// /api/logs  GET  - histórico para o gráfico de linhas
// ---------------------------------------------------------------------------
static void handleGetLogs(AsyncWebServerRequest *request) {
  int count = logger_getCount();
  const LogSample *buf = logger_getBuffer();
  int head = logger_getHead();

  AsyncResponseStream *response = request->beginResponseStream("text/csv");
  response->print("t_ms,rpm,tps,advance,temp\n");
  int start = (count < LOG_BUFFER_SAMPLES) ? 0 : head;
  for (int k = 0; k < count; k++) {
    int idx = (start + k) % LOG_BUFFER_SAMPLES;
    response->printf("%u,%.1f,%.1f,%.2f,%.1f\n", buf[idx].t_ms, buf[idx].rpm, buf[idx].tps, buf[idx].advance, buf[idx].temp);
  }
  request->send(response);
}

static void handleClearLogs(AsyncWebServerRequest *request) {
  logger_clear();
  request->send(200, "application/json", "{\"ok\":true}");
}

void webserver_init() {
  if (!LittleFS.begin(true)) {
    Serial.println("ERRO: falha ao montar LittleFS - faça o upload da pasta data/");
  }

  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD, WIFI_AP_CHANNEL);
  Serial.print("AP criado. IP: ");
  Serial.println(WiFi.softAPIP());

  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/config", HTTP_POST, [](AsyncWebServerRequest *request) {}, nullptr, handlePostConfigBody);

  server.on("/api/actuator/test", HTTP_POST, [](AsyncWebServerRequest *request) {}, nullptr, handlePostActuatorTestBody);

  server.on("/api/map", HTTP_GET, handleGetMap);
  server.on("/api/map", HTTP_POST, [](AsyncWebServerRequest *request) {}, nullptr, handlePostMapBody);

  server.on("/api/logs", HTTP_GET, handleGetLogs);
  server.on("/api/logs/clear", HTTP_POST, handleClearLogs);

  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(2048);
    buildStatusJson(doc);
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
  });

  ws.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client,
                AwsEventType type, void *arg, uint8_t *data, size_t len) {
  });
  server.addHandler(&ws);

  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  server.onNotFound([](AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Nao encontrado");
  });

  server.begin();
}

void webserver_task() {
  uint32_t now = millis();
  if (now - lastWsPush < WEBSOCKET_PUSH_MS) return;
  lastWsPush = now;

  if (ws.count() > 0) {
    DynamicJsonDocument doc(2048);
    buildStatusJson(doc);
    String out;
    serializeJson(doc, out);
    ws.textAll(out);
  }
  ws.cleanupClients();
}
