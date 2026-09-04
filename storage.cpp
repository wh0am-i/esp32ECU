#include "storage.h"
#include <Preferences.h>
#include "ignition.h"
#include "fueling.h"
#include "crank.h"
#include "config.h"

static Preferences prefs;

// namespaces separados evitam que uma gravação grande (mapa) invalide
// outras chaves pequenas por fragmentação
#define NS_MAP    "ecu_map"
#define NS_IGN    "ecu_ign"
#define NS_FUEL   "ecu_fuel"
#define NS_CRANK  "ecu_crank"
#define NS_WIFI   "ecu_wifi"

void storage_init() {
  storage_loadAll();
}

void storage_loadAll() {
  // --- mapa de ignição ---
  prefs.begin(NS_MAP, true); // somente leitura
  size_t expectedLen = sizeof(ignMap);
  bool hasMap = prefs.isKey("map");
  if (hasMap && prefs.getBytesLength("map") == expectedLen) {
    prefs.getBytes("map", &ignMap, expectedLen);
  } else {
    ignition_loadDefaults(); // primeira vez / versão mudou -> usa default seguro
  }
  prefs.end();

  // --- config de ignição ---
  prefs.begin(NS_IGN, true);
  ignConfig.dwellMs      = prefs.getFloat("dwellMs", ignConfig.dwellMs);
  ignConfig.maxDwellMs   = prefs.getFloat("maxDwellMs", ignConfig.maxDwellMs);
  ignConfig.twoStepRpm   = prefs.getFloat("twoStepRpm", ignConfig.twoStepRpm);
  ignConfig.ignitionEnabled = prefs.getBool("ignEnabled", true);
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
  prefs.putBytes("map", &ignMap, sizeof(ignMap));
  prefs.end();
}

void storage_saveIgnitionConfig() {
  prefs.begin(NS_IGN, false);
  prefs.putFloat("dwellMs", ignConfig.dwellMs);
  prefs.putFloat("maxDwellMs", ignConfig.maxDwellMs);
  prefs.putFloat("twoStepRpm", ignConfig.twoStepRpm);
  prefs.putBool("ignEnabled", ignConfig.ignitionEnabled);
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

void storage_saveWifiConfig() {
  // reservado para caso queira permitir trocar SSID/senha pelo webapp no futuro
  prefs.begin(NS_WIFI, false);
  prefs.putString("ssid", WIFI_AP_SSID);
  prefs.putString("pass", WIFI_AP_PASSWORD);
  prefs.end();
}
