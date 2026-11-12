#include "storage.h"
#include "map_resample.h"
#include <Preferences.h>
#include "ignition.h"
#include "fueling.h"
#include "crank.h"
#include "thermal.h"
#include "config.h"

static Preferences prefs;

// namespaces separados evitam que uma gravação grande (mapa) invalide
// outras chaves pequenas por fragmentação
#define NS_MAP     "ecu_map"
#define NS_IGN     "ecu_ign"
#define NS_FUEL    "ecu_fuel"
#define NS_CRANK   "ecu_crank"
#define NS_THERMAL "ecu_therm"

void storage_init() {
  storage_loadAll();
}

void storage_loadAll() {
  // Preserva o mapa legado como backup; migra para uma chave de formato novo.
  bool migrated = false;
  prefs.begin(NS_MAP, true);
  if (prefs.getBytesLength("map16") == sizeof(ignMap)) {
    prefs.getBytes("map16", &ignMap, sizeof(ignMap));
  } else if (prefs.getBytesLength("map") == sizeof(StoredMap<12,8>)) {
    StoredMap<12,8> oldMap;
    prefs.getBytes("map", &oldMap, sizeof(oldMap));
    if (validStoredMap(oldMap)) {
      resampleStoredMap(oldMap, ignMap, IGN_MAP_RPM_BINS, IGN_MAP_TPS_BINS);
      migrated = true;
    } else {
      ignition_loadDefaults();
      Serial.println("Mapa legado invalido; carregados valores de fabrica.");
    }
  } else {
    ignition_loadDefaults();
  }
  prefs.end();
  if (migrated) {
    storage_saveIgnitionMap();
    Serial.println("Mapa 12x8 reamostrado para 16x16. Revise a calibracao.");
  }

  // --- config de ignição ---
  prefs.begin(NS_IGN, true);
  ignConfig.dwellMs         = prefs.getFloat("dwellMs", ignConfig.dwellMs);
  ignConfig.maxDwellMs      = prefs.getFloat("maxDwellMs", ignConfig.maxDwellMs);
  ignConfig.twoStepRpm      = prefs.getFloat("twoStepRpm", ignConfig.twoStepRpm);
  ignConfig.maxRpm          = prefs.getFloat("maxRpm", ignConfig.maxRpm);
  ignConfig.ignitionEnabled = prefs.getBool("ignEnabled", true);
  prefs.end();

  // O limitador nao altera eixos nem avancos persistidos.

  // --- config térmica / ventoinha ---
  prefs.begin(NS_THERMAL, true);
  thermalConfig.fanTargetTemp = prefs.getFloat("fanTarget", thermalConfig.fanTargetTemp);
  thermalConfig.fanHysteresis = prefs.getFloat("fanHyst", thermalConfig.fanHysteresis);
  thermalConfig.overheatTemp  = prefs.getFloat("overheat", thermalConfig.overheatTemp);
  prefs.end();

  // --- config de combustível/servo ---
  prefs.begin(NS_FUEL, true);
  fuelConfig.tpsAdcMin = prefs.getInt("tpsMin", fuelConfig.tpsAdcMin);
  fuelConfig.tpsAdcMax = prefs.getInt("tpsMax", fuelConfig.tpsAdcMax);
  fuelConfig.tpsMinPercent = prefs.getFloat("tpsMinPct", fuelConfig.tpsMinPercent);
  fuelConfig.tpsMaxPercent = prefs.getFloat("tpsMaxPct", fuelConfig.tpsMaxPercent);
  fuelConfig.servoAngleAtClosedThrottle = prefs.getInt("servoMin", fuelConfig.servoAngleAtClosedThrottle);
  fuelConfig.servoAngleAtFullThrottle   = prefs.getInt("servoMax", fuelConfig.servoAngleAtFullThrottle);
  fuelConfig.rpmCompensationRef    = prefs.getFloat("rpmCompRef", fuelConfig.rpmCompensationRef);
  fuelConfig.rpmCompensationFactor = prefs.getFloat("rpmCompFac", fuelConfig.rpmCompensationFactor);
  prefs.end();

  // --- calibração da roda fônica / Ponto 0 ---
  prefs.begin(NS_CRANK, true);
  crank.triggerTeethOffset = prefs.getInt("trigTeeth", crank.triggerTeethOffset);
  crank.triggerFineOffsetDeg = prefs.getFloat("trigFine", crank.triggerFineOffsetDeg);
  crank.triggerOffsetDeg = prefs.getFloat("triggerOff", (crank.triggerTeethOffset * DEG_PER_TOOTH) + crank.triggerFineOffsetDeg);
  prefs.end();
}

void storage_saveIgnitionMap() {
  prefs.begin(NS_MAP, false);
  prefs.putBytes("map16", &ignMap, sizeof(ignMap));
  prefs.end();
}

void storage_saveIgnitionConfig() {
  prefs.begin(NS_IGN, false);
  prefs.putFloat("dwellMs", ignConfig.dwellMs);
  prefs.putFloat("maxDwellMs", ignConfig.maxDwellMs);
  prefs.putFloat("twoStepRpm", ignConfig.twoStepRpm);
  prefs.putFloat("maxRpm", ignConfig.maxRpm);
  prefs.putBool("ignEnabled", ignConfig.ignitionEnabled);
  prefs.end();
}

void storage_saveThermalConfig() {
  prefs.begin(NS_THERMAL, false);
  prefs.putFloat("fanTarget", thermalConfig.fanTargetTemp);
  prefs.putFloat("fanHyst", thermalConfig.fanHysteresis);
  prefs.putFloat("overheat", thermalConfig.overheatTemp);
  prefs.end();
}

void storage_saveFuelingConfig() {
  prefs.begin(NS_FUEL, false);
  prefs.putInt("tpsMin", fuelConfig.tpsAdcMin);
  prefs.putInt("tpsMax", fuelConfig.tpsAdcMax);
  prefs.putFloat("tpsMinPct", fuelConfig.tpsMinPercent);
  prefs.putFloat("tpsMaxPct", fuelConfig.tpsMaxPercent);
  prefs.putInt("servoMin", fuelConfig.servoAngleAtClosedThrottle);
  prefs.putInt("servoMax", fuelConfig.servoAngleAtFullThrottle);
  prefs.putFloat("rpmCompRef", fuelConfig.rpmCompensationRef);
  prefs.putFloat("rpmCompFac", fuelConfig.rpmCompensationFactor);
  prefs.end();
}

void storage_saveCrankCalibration() {
  prefs.begin(NS_CRANK, false);
  prefs.putInt("trigTeeth", crank.triggerTeethOffset);
  prefs.putFloat("trigFine", crank.triggerFineOffsetDeg);
  crank.triggerOffsetDeg = (crank.triggerTeethOffset * DEG_PER_TOOTH) + crank.triggerFineOffsetDeg;
  prefs.putFloat("triggerOff", crank.triggerOffsetDeg);
  prefs.end();
}

// storage_saveWifiConfig() foi REMOVIDA nesta correção: ela nunca era chamada
// em lugar nenhum do firmware, e mesmo se fosse, só regravava os #define
// fixos WIFI_AP_SSID/WIFI_AP_PASSWORD (não uma credencial editável) - ou
// seja, era código morto que sugeria uma funcionalidade ("trocar Wi-Fi pelo
// app") que não existe. O SSID/senha do Access Point são e continuam sendo
// definidos em tempo de compilação em config.h (WIFI_AP_SSID / WIFI_AP_PASSWORD).
// Se quiser essa configuração editável de fato, é necessário: 1) ler/gravar
// nas chaves NS_WIFI em storage_loadAll()/aqui, e 2) usar as variáveis
// carregadas (em vez das macros) em webserver_init() no WiFi.softAP(...).