#include "SmartPlugShared.h"

// BMI160 register-level driver (I2C address 0x68 or 0x69). No extra library required.
static uint8_t bmiAddress = 0x68;
static bool bmiWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(bmiAddress); Wire.write(reg); Wire.write(val); return Wire.endTransmission() == 0;
}
static bool bmiRead(uint8_t reg, uint8_t *buf, size_t n) {
  Wire.beginTransmission(bmiAddress); Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  size_t got = Wire.requestFrom((int)bmiAddress, (int)n);
  if (got != n) { while (Wire.available()) Wire.read(); return false; }
  for (size_t i=0;i<n;i++) buf[i] = Wire.read();
  return true;
}
static bool bmiTryAddress(uint8_t addr) {
  bmiAddress = addr; uint8_t id = 0;
  return bmiRead(0x00, &id, 1) && id == 0xD1;
}

void initBMI160() {
  bmi160Detected = bmiTryAddress(0x68) || bmiTryAddress(0x69);
  bmi160Healthy = false; ironFault = !bmi160Detected;
  if (!bmi160Detected) { Serial.println("[BMI160] Not detected; iron locked OFF"); return; }
  // Accelerometer normal mode, gyro normal mode; wait for sensor startup.
  bool configured = bmiWrite(0x7E, 0x11); delay(50);
  configured = bmiWrite(0x7E, 0x15) && configured; delay(80);
  configured = bmiWrite(0x41, 0x05) && configured;
  uint8_t range=0;
  configured = bmiRead(0x41,&range,1) && range==0x05 && configured;
  bmi160Healthy = configured; ironFault = !configured;
  if (!configured) { Serial.println("[BMI160] Initialization failed; iron OFF"); return; }
  Serial.printf("[BMI160] Detected at 0x%02X\n", bmiAddress);
}

void updateBMI160() {
  // Sample at 50 ms. Use a high 3-axis change threshold so small vibration and
  // normal sensor noise do not count as motion. A strong movement can trigger
  // repeatedly while it continues, not just on the first movement.
  static bool havePreviousSample = false;
  static uint8_t majorMotionConfirmations = 0;
  static uint32_t lastMajorMotionSample = 0;
  const float MAJOR_MOTION_THRESHOLD_G = 0.35f; // tune on the final mounting
  const uint8_t REQUIRED_CONFIRMATIONS = 2;
  const uint32_t MOTION_HOLD_MS = 800;

  if (millis() - lastBmiSample < 50) return;
  lastBmiSample = millis();
  if (!bmi160Healthy) {
    static unsigned long lastRecovery=0;
    if(millis()-lastRecovery<1000) return;
    lastRecovery=millis();
    initBMI160();
    havePreviousSample=false;
    if(!bmi160Healthy) return;
  }
  if (!bmi160Detected) {
    if (bmiTryAddress(0x68) || bmiTryAddress(0x69)) initBMI160();
    else { bmi160Healthy = false; ironFault = true; if (ironPowerState) setIronPower(false, "BMI160 unavailable"); }
    havePreviousSample = false;
    return;
  }
  uint8_t b[6];
  if (!bmiRead(0x12, b, 6)) {
    bmi160Healthy = false; ironFault = true; havePreviousSample = false;
    if (ironPowerState) { ironTripped=true; setIronPower(false, "BMI160 read fault; rearm required"); saveSettings(); }
    return;
  }
  int16_t rx = (int16_t)((uint16_t)b[1] << 8 | b[0]);
  int16_t ry = (int16_t)((uint16_t)b[3] << 8 | b[2]);
  int16_t rz = (int16_t)((uint16_t)b[5] << 8 | b[4]);
  // BMI160 +/-4 g range = 8192 LSB/g.
  float nx = rx / 8192.0f, ny = ry / 8192.0f, nz = rz / 8192.0f;
  bmi160Healthy = true; ironFault = false;

  if (!havePreviousSample) {
    bmiAccelX = nx; bmiAccelY = ny; bmiAccelZ = nz;
    havePreviousSample = true;
    majorMotionConfirmations = 0;
    return; // establish a baseline without declaring motion
  }

  float dx = nx - bmiAccelX;
  float dy = ny - bmiAccelY;
  float dz = nz - bmiAccelZ;
  // Euclidean delta avoids over-weighting diagonal sensor noise.
  bmiMotionScore = sqrtf(dx * dx + dy * dy + dz * dz);
  bmiAccelX = nx; bmiAccelY = ny; bmiAccelZ = nz;

  if (bmiMotionScore >= MAJOR_MOTION_THRESHOLD_G) {
    if (majorMotionConfirmations < REQUIRED_CONFIRMATIONS) majorMotionConfirmations++;
    if (majorMotionConfirmations >= REQUIRED_CONFIRMATIONS) {
      lastMajorMotionSample = millis();
      ironLastMotionTime = lastMajorMotionSample;
      ironMotionDetected = true;
    }
  } else {
    majorMotionConfirmations = 0;
    if (ironMotionDetected && millis() - lastMajorMotionSample >= MOTION_HOLD_MS) {
      ironMotionDetected = false;
    }
  }
}

void setIronPower(bool state, const String& reason) {
  if (state && !SafetyRules::mayEnergize(maintenanceMode || controllerStallTrip.load(), ironTripped, true, bmi160Detected && bmi160Healthy && !ironFault)) {
    state = false;
    ironPowerReason = "Blocked: sensor, maintenance, or cutoff interlock";
  } else ironPowerReason = reason;
  digitalWrite(RELAY2_PIN, state ? RELAY_ON : RELAY_OFF);
  if (state == ironPowerState) return;
  telemetryUrgent = true;
  ironPowerState = state;
  digitalWrite(RELAY2_PIN, state ? RELAY_ON : RELAY_OFF);
  if (state) { ironPowerOnStartTime = millis(); ironLastMotionTime = millis(); }
  else ironPowerOnStartTime = 0;
  saveSettings();
  updateOutputIndicators();
  Serial.printf("[IRON] %s (%s)\n", state ? "ON" : "OFF", ironPowerReason.c_str());
}

void processIronController() {
  if (maintenanceMode || !bmi160Detected || !bmi160Healthy || ironFault) {
    if (ironPowerState) { ironTripped=true; setIronPower(false, "Sensor fault; rearm required"); saveSettings(); }
    return;
  }
  if (ironMode=="OFF" || ironTripped) { setIronPower(false, ironTripped?"Cutoff latched; rearm required":"Iron disabled"); return; }
  if (ironMaxOnEnabled && ironPowerState && SafetyRules::elapsed(millis(), ironPowerOnStartTime, ironMaxOnSeconds)) {
    ironTripped=true; setIronPower(false, "Maximum ON duration; rearm required"); saveSettings(); return;
  }
  if (ironPowerState && SafetyRules::elapsed(millis(), ironLastMotionTime, ironInactivitySeconds)) {
    setIronPower(false, "Iron inactivity timeout");
    return;
  }
  if (ironMode=="AUTO" && humanDetected && ironMotionDetected) {
    if (!ironPowerState) setIronPower(true, "Presence and iron movement");
  }
}

void handleIronPowerCommand() {
  if (!requireWebAuth()) return;
  JsonDocument a; String reason;
  bool ok=executeCommand(true, setupServer.uri()=="/iron/on"?"on":"off", a.to<JsonObject>(), reason);
  setupServer.send(ok?200:409,"text/plain",reason);
}

void handleIronSettings() {
  if (!requireWebAuth()) return;
  String m = setupServer.arg("ironMode");
  if (m == "OFF" || m == "MANUAL" || m == "AUTO") ironMode = m;
  long t = setupServer.arg("ironTimeout").toInt(); if (t < 1) t=1; if (t>86400) t=86400;
  long mt = setupServer.arg("ironMaxSec").toInt(); if (mt < 1) mt=1; if (mt>86400) mt=86400;
  ironInactivitySeconds = (unsigned long)t; ironMaxOnSeconds = (unsigned long)mt;
  ironMaxOnEnabled = setupServer.arg("ironMaxEn") == "1";
  if (ironMode == "OFF") setIronPower(false, "Iron mode OFF");
  else if (ironMode == "AUTO") { if (ironPowerState) setIronPower(false, "Switching to AUTO"); }
  saveSettings(); markLocalChange();
  setupServer.sendHeader("Location", "/"); setupServer.send(303, "text/plain", "Iron settings saved");
}
