#include "SmartPlugShared.h"

void markLocalChange() {
  telemetryUrgent = true;
}

String createDeviceID() {

  uint64_t chipID = ESP.getEfuseMac();

  char id[30];

  sprintf(
    id,
    "SP-%04X%08X",
    (uint16_t)(chipID >> 32),
    (uint32_t)chipID
  );

  return String(id);
}

String randomString(size_t length, const char* alphabet) {

  size_t a = strlen(alphabet);
  String s;

  for (size_t i = 0; i < length; i++) {
    s += alphabet[esp_random() % a];
  }

  return s;
}

void loadOrCreateIdentity() {

  identityPrefs.begin("identity", false);

  deviceSecret = identityPrefs.getString("secret", "");
  claimCode = identityPrefs.getString("claim", "");

  if (deviceSecret.length() != 24 || claimCode.length() != 8) {

    // Claim alphabet has no 0/O/1/I so labels are easy to read.
    deviceSecret = randomString(
      24,
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
    );

    claimCode = randomString(
      8,
      "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"
    );

    identityPrefs.putString("secret", deviceSecret);
    identityPrefs.putString("claim", claimCode);

    Serial.println("[ID] New identity created");
  }

  identityPrefs.end();
}

String formatClaimCode() {

  if (claimCode.length() != 8) return "N/A";

  return claimCode.substring(0, 4) + "-" + claimCode.substring(4);
}

bool isValidCloudConfig() {

  String url = String(SUPABASE_URL);
  String key = String(SUPABASE_KEY);

  if (url.length() < 10) return false;
  if (key.length() < 10) return false;
  if (url.indexOf("YOUR_SUPABASE") >= 0) return false;
  if (key.indexOf("YOUR_PUBLISHABLE") >= 0) return false;

  return true;
}

void rgb(bool r, bool g, bool b) {
  digitalWrite(RGB_RED_PIN,   r ? HIGH : LOW);
  digitalWrite(RGB_GREEN_PIN, g ? HIGH : LOW);
  digitalWrite(RGB_BLUE_PIN,  b ? HIGH : LOW);
}

void setRGB(RGBColor color) {

  switch (color) {
    case RGB_OFF:     rgb(false, false, false); break;
    case RGB_RED:     rgb(true,  false, false); break;
    case RGB_GREEN:   rgb(false, true,  false); break;
    case RGB_BLUE:    rgb(false, false, true);  break;
    case RGB_YELLOW:  rgb(true,  true,  false); break;
    case RGB_CYAN:    rgb(false, true,  true);  break;
    case RGB_MAGENTA: rgb(true,  false, true);  break;
    case RGB_WHITE:   rgb(true,  true,  true);  break;
  }
}

void beep(uint16_t frequency, uint16_t duration) {
  tone(BUZZER_PIN, frequency, duration);
}

void updateOutputIndicators() {

  // Output LED follows the relay
  digitalWrite(OUTPUT_LED_PIN, actualPowerState ? HIGH : LOW);

  // RGB LED shows the operating mode only
  if (mode == "MANUAL") {
    setRGB(RGB_RED);
  } else if (mode == "AUTO") {
    setRGB(RGB_MAGENTA);
  } else if (mode == "HYBRID") {
    setRGB(RGB_BLUE);
  } else if (mode == "NORMAL") {
    setRGB(RGB_GREEN);
  } else {
    setRGB(RGB_OFF);
  }
}

void setRelay(bool state, const String& reason) {

  if (state && !SafetyRules::mayEnergize(maintenanceMode || controllerStallTrip.load(), plugTripped, false, true)) {
    powerReason = maintenanceMode ? "Blocked: maintenance" : "Blocked: rearm required";
    telemetryUrgent = true;
    return;
  }
  powerReason = reason;

  // Always drive the output, even if logical state already agrees.
  digitalWrite(RELAY1_PIN, state ? RELAY_ON : RELAY_OFF);
  if (actualPowerState == state) {
    updateOutputIndicators();
    return;
  }

  actualPowerState = state;
  telemetryUrgent = true;

  // telemetryUrgent queues a device-reported state change.

  digitalWrite(RELAY1_PIN, state ? RELAY_ON : RELAY_OFF);

  // Relay 2 (iron) is controlled independently by setIronPower().
  // Never mirror smart-plug relay changes onto the iron relay.

  if (state) {
    powerOnStartTime = millis();
  } else {
    powerOnStartTime = 0;
  }

  Serial.print("[RELAY] ");
  Serial.print(state ? "ON" : "OFF");
  Serial.print(" | ");
  Serial.println(powerReason);

  updateOutputIndicators();

  beep(state ? 2400 : 1600, 60);
}

void loadWiFiCredentials() {

  wifiPrefs.begin("wifi", true);
  wifiSSID = wifiPrefs.getString("ssid", "");
  wifiPassword = wifiPrefs.getString("password", "");
  wifiPrefs.end();

  Serial.print("[WIFI] Saved SSID: ");
  Serial.println(wifiSSID.length() ? wifiSSID : String("NONE"));
}

void saveWiFiCredentials(const String& ssid, const String& password) {

  wifiPrefs.begin("wifi", false);
  wifiPrefs.putString("ssid", ssid);
  wifiPrefs.putString("password", password);
  wifiPrefs.end();

  wifiSSID = ssid;
  wifiPassword = password;

  Serial.println("[WIFI] Credentials saved");
}

void loadSettings() {

  devicePrefs.begin("settings", true);

  mode = devicePrefs.getString("mode", "AUTO");
  // Safe reboot policy: persisted state is never authority to energize a load.
  actualPowerState = false;
  plugTripped = devicePrefs.getBool("plugTrip", false);
  ironTripped = devicePrefs.getBool("ironTrip", false);
  pirSensitivity = devicePrefs.getInt("pir", 1);
  humanOnSeconds = devicePrefs.getULong("humanOn", 300);
  detectionDelaySeconds = devicePrefs.getULong("delay", 0);
  maxOnEnabled = devicePrefs.getBool("maxEnable", false);
  maxOnSeconds = devicePrefs.getULong("maxSec", 7200);
  ironMode = "OFF"; // Explicitly re-enable iron operation after every boot.
  ironInactivitySeconds = devicePrefs.getULong("ironTimeout", 300);
  ironMaxOnEnabled = devicePrefs.getBool("ironMaxEn", true);
  ironMaxOnSeconds = devicePrefs.getULong("ironMaxSec", 1800);

  devicePrefs.end();

  if (mode != "MANUAL" && mode != "AUTO" &&
      mode != "HYBRID" && mode != "NORMAL") {
    mode = "AUTO";
  }

  if (pirSensitivity < 0) pirSensitivity = 0;
  if (pirSensitivity > 2) pirSensitivity = 2;
  if (humanOnSeconds < 1 || humanOnSeconds > 86400) humanOnSeconds = 300;
  if (detectionDelaySeconds > 3600) detectionDelaySeconds = 0;
  if (maxOnSeconds < 1 || maxOnSeconds > 86400) maxOnSeconds = 7200;
  if (ironMode != "OFF" && ironMode != "MANUAL" && ironMode != "AUTO") ironMode = "OFF";
  if (ironInactivitySeconds < 1 || ironInactivitySeconds > 86400) ironInactivitySeconds = 300;
  if (ironMaxOnSeconds < 1 || ironMaxOnSeconds > 86400) ironMaxOnSeconds = 7200;

  Serial.println();
  Serial.println("[NVS] SETTINGS");
  Serial.print("Mode: ");               Serial.println(mode);
  Serial.print("Power: ");              Serial.println(actualPowerState ? "ON" : "OFF");
  Serial.print("PIR sensitivity: ");    Serial.println(pirSensitivity);
  Serial.print("Human ON seconds: ");   Serial.println(humanOnSeconds);
  Serial.print("Detection delay: ");    Serial.println(detectionDelaySeconds);
  Serial.print("Max ON: ");             Serial.println(maxOnEnabled ? "ENABLED" : "DISABLED");
  Serial.print("Max ON seconds: ");     Serial.println(maxOnSeconds);
}

void writeNvs() {

  devicePrefs.begin("settings", false);

  devicePrefs.putString("mode", mode);
  devicePrefs.putBool("power", false);
  devicePrefs.putBool("plugTrip", plugTripped);
  devicePrefs.putBool("ironTrip", ironTripped);
  devicePrefs.putInt("pir", pirSensitivity);
  devicePrefs.putULong("humanOn", humanOnSeconds);
  devicePrefs.putULong("delay", detectionDelaySeconds);
  devicePrefs.putBool("maxEnable", maxOnEnabled);
  devicePrefs.putULong("maxSec", maxOnSeconds);
  devicePrefs.putString("ironMode", ironMode);
  devicePrefs.putULong("ironTimeout", ironInactivitySeconds);
  devicePrefs.putBool("ironMaxEn", ironMaxOnEnabled);
  devicePrefs.putULong("ironMaxSec", ironMaxOnSeconds);

  devicePrefs.end();
}

void saveSettings() {
  nvsSavePending = true;
  nvsSaveTime = millis();
  markLocalChange();
}

void flushSettings() {
  if (nvsSavePending) {
    nvsSavePending = false;
    writeNvs();
  }
}

void processDeferredSave() {
  if (nvsSavePending && millis() - nvsSaveTime >= 1000UL) {
    nvsSavePending = false;
    writeNvs();
  }
}

void checkPowerCycleFallback() {

  cyclePrefs.begin("cycles", false);

  uint8_t count = cyclePrefs.getUChar("count", 0);
  count++;
  cyclePrefs.putUChar("count", count);

  cyclePrefs.end();

  Serial.print("[BOOT] Power cycle count: ");
  Serial.println(count);

  if (count >= POWER_CYCLE_LIMIT) {
    fallbackMode = true;
    mode = "MANUAL";
    setRelay(false, "Repeated restart recovery: OFF");
    Serial.println("[RECOVERY] Repeated restarts detected; outputs remain OFF");
  }

}

void clearPowerCycleCounter() {

  if (powerCycleCounterCleared) return;

  if (millis() - bootTime < POWER_CYCLE_RESET_TIME) return;

  cyclePrefs.begin("cycles", false);
  cyclePrefs.putUChar("count", 0);
  cyclePrefs.end();

  powerCycleCounterCleared = true;

  Serial.println("[BOOT] Stable runtime - power cycle counter cleared");
}
