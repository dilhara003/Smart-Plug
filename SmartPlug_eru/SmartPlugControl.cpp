#include "SmartPlugShared.h"

void setMode(const String& next) {
  if (next != "MANUAL" && next != "AUTO" && next != "HYBRID" && next != "NORMAL") return;
  mode = next == "NORMAL" ? "MANUAL" : next;
  plugOverrideActive = false;
  detectionPending = false;
  fallbackMode = false;
  saveSettings();
  updateOutputIndicators();
}
void nextMode() {
  setMode(mode == "MANUAL" ? "AUTO" : mode == "AUTO" ? "HYBRID" : "MANUAL");
  beep(2600, 100);
}
void manualToggle() {
  if(plugTripped && !maintenanceMode) { rearmOutput(false); beep(1800,80); return; }
  String reason;
  JsonDocument args;
  executeCommand(false, actualPowerState ? "off" : "on", args.to<JsonObject>(), reason);
}
void processAutoMode() {
  if (humanDetected) { setRelay(true, "Presence detected"); return; }
  if (actualPowerState && SafetyRules::elapsed(millis(), lastHumanTime, humanOnSeconds))
    setRelay(false, "Presence timeout");
}
void processHybridMode() {
  if (plugOverrideActive) {
    if (!SafetyRules::elapsed(millis(), plugOverrideStart, humanOnSeconds)) return;
    plugOverrideActive = false;
    telemetryUrgent = true;
  }
  processAutoMode();
}
void processMaximumOnTime() {
  if (maxOnEnabled && actualPowerState && SafetyRules::elapsed(millis(), powerOnStartTime, maxOnSeconds)) {
    plugTripped = true;
    setRelay(false, "Maximum ON duration; rearm required");
    saveSettings();
  }
}
void processSmartEngine() {
  if (maintenanceMode || fallbackMode || plugTripped) return;
  if (mode == "AUTO") processAutoMode();
  else if (mode == "HYBRID") processHybridMode();
}
void enterMaintenance(const String& reason) {
  maintenanceMode = true;
  mode = "MANUAL";
  ironMode = "OFF";
  setRelay(false, reason);
  setIronPower(false, reason);
  saveSettings();
  flushSettings();
  telemetryUrgent = true;
}
bool rearmOutput(bool iron) {
  if (maintenanceMode || controllerStallTrip.load()) return false;
  if (iron) {
    if (!bmi160Healthy || !bmi160Detected || ironFault) return false;
    setIronPower(false, "Rearmed; awaiting explicit ON or mode choice");
    ironTripped = false;
    ironMode = "OFF";
  } else {
    setRelay(false, "Rearmed; awaiting explicit ON or mode choice");
    plugTripped = false;
    mode = "MANUAL";
    plugOverrideActive = false;
  }
  saveSettings();
  return true;
}
void applyOutputSettings(bool iron, JsonObject a) {
  if (!iron) {
    if (a["mode"].is<const char*>()) setMode(a["mode"].as<String>());
    if (a["human_on_seconds"].is<unsigned long>()) humanOnSeconds = constrain(a["human_on_seconds"].as<unsigned long>(), 1UL, 86400UL);
    if (a["detection_delay_seconds"].is<unsigned long>()) detectionDelaySeconds = constrain(a["detection_delay_seconds"].as<unsigned long>(), 0UL, 3600UL);
    if (a["max_on_enabled"].is<bool>()) maxOnEnabled = a["max_on_enabled"];
    if (a["max_on_seconds"].is<unsigned long>()) maxOnSeconds = constrain(a["max_on_seconds"].as<unsigned long>(), 1UL, 86400UL);
  } else {
    if (a["iron_mode"].is<const char*>()) {
      String m = a["iron_mode"].as<String>();
      if (m=="OFF" || m=="AUTO" || m=="MANUAL") { ironMode=m; setIronPower(false, "Iron mode changed"); }
    }
    if (a["iron_inactivity_seconds"].is<unsigned long>()) ironInactivitySeconds = constrain(a["iron_inactivity_seconds"].as<unsigned long>(), 1UL, 86400UL);
    if (a["iron_max_on_enabled"].is<bool>()) ironMaxOnEnabled = a["iron_max_on_enabled"];
    if (a["iron_max_on_seconds"].is<unsigned long>()) ironMaxOnSeconds = constrain(a["iron_max_on_seconds"].as<unsigned long>(), 1UL, 86400UL);
  }
  saveSettings();
}
bool executeCommand(bool iron, const String& action, JsonObject args, String& reason) {
  // OFF is always allowed, including during a sensor fault or timer trip.
  if (action == "off") {
    if (iron) { ironMode="OFF"; setIronPower(false, "Manual OFF"); }
    else {
      if (mode=="HYBRID") { plugOverrideActive=true; plugOverrideStart=millis(); }
      else mode="MANUAL";
      setRelay(false, "Manual OFF");
    }
    saveSettings(); reason="Output OFF"; return true;
  }
  if (maintenanceMode || controllerStallTrip.load()) { reason="Maintenance or controller-timeout interlock active"; return false; }
  if (action == "rearm") { bool ok=rearmOutput(iron); reason=ok?"Rearmed; output remains OFF":"Sensor must be healthy before rearming"; return ok; }
  if (iron && (!bmi160Detected || !bmi160Healthy || ironFault)) { reason="BMI160 unavailable; iron locked OFF"; return false; }
  if (action == "settings") { applyOutputSettings(iron,args); reason="Settings applied"; return true; }
  if (action != "on") { reason="Unsupported action"; return false; }
  if (iron ? ironTripped : plugTripped) { reason="Maximum-time or sensor cutoff: rearm first"; return false; }
  fallbackMode=false;
  if (iron) { ironMode="MANUAL"; setIronPower(true, "Manual ON"); }
  else {
    if (mode=="HYBRID") { plugOverrideActive=true; plugOverrideStart=millis(); }
    else mode="MANUAL";
    setRelay(true, "Manual ON");
  }
  saveSettings(); reason="Output ON"; return true;
}

bool updateButton(ButtonState& button, int pin) {

  bool reading = digitalRead(pin) == HIGH;

  unsigned long now = millis();

  if (reading != button.lastReading) {
    button.lastDebounceTime = now;
    button.lastReading = reading;
  }

  if (now - button.lastDebounceTime > BUTTON_DEBOUNCE_MS) {

    if (reading != button.stableState) {

      button.stableState = reading;

      if (reading) {

        button.pressStartTime = now;
        button.longPressHandled = false;

      } else {

        // Button released
        if (!button.longPressHandled) {
          return true;
        }
      }
    }
  }

  return false;
}

void processResetButton() {

  unsigned long now = millis();

  // Long press = setup mode
  if (resetButton.stableState && !resetButton.longPressHandled &&
      now - resetButton.pressStartTime >= RESET_LONG_PRESS_MS) {

    resetButton.longPressHandled = true;

    Serial.println("[BUTTON] Long press - setup mode");

    flushSettings();
    startSetupAP();
    return;
  }

  // Short press = restart
  if (updateButton(resetButton, RESET_BUTTON_PIN)) {

    Serial.println("[BUTTON] Restart");

    flushSettings();
    delay(300);
    ESP.restart();
  }
}
