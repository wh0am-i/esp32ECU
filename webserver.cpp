#include "webserver.h"
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "config.h"
#include "ignition.h"
#include "fueling.h"
#include "crank.h"
#include "logger.h"
#include "storage.h"

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");
static uint32_t lastWsPush = 0;

// ---------------------------------------------------------------------------
// /api/status (também espelhado via WebSocket) - dados de monitoramento
// ---------------------------------------------------------------------------
static void buildStatusJson(JsonDocument &doc) {
  doc["rpm"] = crank_getRPM();
  doc["tps"] = tps_getPercent();
  doc["tpsRaw"] = tps_getRawADC();
  doc["advance"] = ignition_lastAppliedAdvance();
  doc["synced"] = crank_isSynced();
  doc["running"] = crank_isEngineRunning();
  doc["servoAngle"] = fueling_lastServoAngle();
  // "apertado" (estado do botão) e "cortando" (efeito real, só acima do RPM
  // de corte) são coisas diferentes - o webapp mostra as duas.
  bool twoStepPressed = (digitalRead(PIN_BUTTON_2STEP) == LOW);
  doc["twoStepPressed"] = twoStepPressed;
  doc["twoStepCutting"] = twoStepPressed && (crank_getRPM() > ignConfig.twoStepRpm);
  doc["twoStepRpm"] = ignConfig.twoStepRpm;
  doc["ignitionEnabled"] = ignConfig.ignitionEnabled;

  // telemetria para a ilustração de ordem de disparo em tempo real (ver
  // index.html): contador de centelhas por bobina (o app detecta a mudança
  // de contagem pra disparar a animação) + se cada bobina está em dwell
  // (carregando) agora.
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

  String out;
  serializeJson(doc, out);
  request->send(200, "application/json", out);
}

// body handler acumula o corpo do POST (pode vir em vários pacotes)
static void handlePostConfigBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  DynamicJsonDocument doc(1024);
  DeserializationError err = deserializeJson(doc, data, len);
  if (err) {
    request->send(400, "application/json", "{\"error\":\"json invalido\"}");
    return;
  }

  if (doc.containsKey("dwellMs")) ignConfig.dwellMs = doc["dwellMs"];
  if (doc.containsKey("maxDwellMs")) ignConfig.maxDwellMs = doc["maxDwellMs"];
  if (doc.containsKey("twoStepRpm")) ignConfig.twoStepRpm = doc["twoStepRpm"];
  if (doc.containsKey("ignitionEnabled")) ignConfig.ignitionEnabled = doc["ignitionEnabled"];
  storage_saveIgnitionConfig();

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
// /api/map  GET/POST  - mapa de ignição (heatmap RPM x TPS)
// ---------------------------------------------------------------------------
static void handleGetMap(AsyncWebServerRequest *request) {
  DynamicJsonDocument doc(4096);
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
  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, data, len);
  if (err) {
    request->send(400, "application/json", "{\"error\":\"json invalido\"}");
    return;
  }

  // aceita ou uma edição de célula única {row, col, value} ou a tabela inteira
  if (doc.containsKey("row") && doc.containsKey("col") && doc.containsKey("value")) {
    int row = doc["row"];
    int col = doc["col"];
    float value = doc["value"];
    if (row >= 0 && row < IGN_MAP_TPS_BINS && col >= 0 && col < IGN_MAP_RPM_BINS) {
      ignMap.advance[row][col] = value;
    }
  } else if (doc.containsKey("table")) {
    JsonArray table = doc["table"];
    int j = 0;
    for (JsonArray row : table) {
      if (j >= IGN_MAP_TPS_BINS) break;
      int i = 0;
      for (JsonVariant v : row) {
        if (i >= IGN_MAP_RPM_BINS) break;
        ignMap.advance[j][i] = v.as<float>();
        i++;
      }
      j++;
    }
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

  // resposta em CSV é mais leve que JSON pra até 2400 amostras
  AsyncResponseStream *response = request->beginResponseStream("text/csv");
  response->print("t_ms,rpm,tps,advance\n");
  int start = (count < LOG_BUFFER_SAMPLES) ? 0 : head;
  for (int k = 0; k < count; k++) {
    int idx = (start + k) % LOG_BUFFER_SAMPLES;
    response->printf("%u,%.1f,%.1f,%.2f\n", buf[idx].t_ms, buf[idx].rpm, buf[idx].tps, buf[idx].advance);
  }
  request->send(response);
}

static void handleClearLogs(AsyncWebServerRequest *request) {
  logger_clear();
  request->send(200, "application/json", "{\"ok\":true}");
}

void webserver_init() {
  // ---- LittleFS (arquivos do webapp) ----
  if (!LittleFS.begin(true)) {
    Serial.println("ERRO: falha ao montar LittleFS - faça o upload da pasta data/");
  }

  // ---- Access Point ----
  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD, WIFI_AP_CHANNEL);
  Serial.print("AP criado. IP: ");
  Serial.println(WiFi.softAPIP()); // normalmente 192.168.4.1

  // ---- rotas de API ----
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/config", HTTP_POST, [](AsyncWebServerRequest *request) {}, nullptr, handlePostConfigBody);

  server.on("/api/map", HTTP_GET, handleGetMap);
  server.on("/api/map", HTTP_POST, [](AsyncWebServerRequest *request) {}, nullptr, handlePostMapBody);

  server.on("/api/logs", HTTP_GET, handleGetLogs);
  server.on("/api/logs/clear", HTTP_POST, handleClearLogs);

  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(512);
    buildStatusJson(doc);
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
  });

  // ---- WebSocket para push em tempo real ----
  ws.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client,
                AwsEventType type, void *arg, uint8_t *data, size_t len) {
    // não precisamos tratar mensagens recebidas do cliente por enquanto
  });
  server.addHandler(&ws);

  // ---- arquivos estáticos do webapp (monitor=home, config, logs) ----
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
    DynamicJsonDocument doc(512);
    buildStatusJson(doc);
    String out;
    serializeJson(doc, out);
    ws.textAll(out);
  }
  ws.cleanupClients();
}
