/* ERU SmartPlug 1.0 — open SmartPlug_eru.ino, ESP32 Dev Module. */
#include "SmartPlugShared.h"

void setup() {

  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println();
  Serial.println("========================================");
  Serial.println("      SMART PLUG");
  Serial.println("========================================");

  // GPIO
  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);
  pinMode(PIR_PIN, INPUT);
  pinMode(WIFI_LED_PIN, OUTPUT);
  pinMode(OUTPUT_LED_PIN, OUTPUT);

  // Active-HIGH buttons: released LOW, pressed HIGH (internal pull-down)
  pinMode(POWER_BUTTON_PIN, INPUT_PULLDOWN);
  pinMode(RESET_BUTTON_PIN, INPUT_PULLDOWN);
  pinMode(UP_BUTTON_PIN, INPUT_PULLDOWN);
  pinMode(DOWN_BUTTON_PIN, INPUT_PULLDOWN);

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RGB_RED_PIN, OUTPUT);
  pinMode(RGB_GREEN_PIN, OUTPUT);
  pinMode(RGB_BLUE_PIN, OUTPUT);

  // Safe startup relay state
  digitalWrite(RELAY1_PIN, RELAY_OFF);
  digitalWrite(RELAY2_PIN, RELAY_OFF);
  digitalWrite(WIFI_LED_PIN, LOW);
  digitalWrite(OUTPUT_LED_PIN, LOW);
  setRGB(RGB_OFF);

  // ADC
  analogReadResolution(12);
  analogSetPinAttenuation(ACS712_PIN, ADC_11db);

  // I2C + LCD
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setTimeOut(10);
  Wire.setClock(400000);          // fast I2C (400 kHz)

  lcd.init();
  lcd.backlight();
  lcdReady = true;
  lcdClear();
  lcdPrintLine(0, "SMART PLUG");
  lcdPrintLine(1, "Starting...");

  // Device ID
  deviceID = createDeviceID();

  Serial.print("Device ID: ");
  Serial.println(deviceID);

  // Load saved data
  loadWiFiCredentials();
  loadSettings();
  // BMI160 is optional; its absence must not affect Relay 1.
  initBMI160();
  digitalWrite(RELAY2_PIN, RELAY_OFF);
  ironPowerState = false;

  // Start boot timer
  bootTime = millis();

  WiFi.mode(WIFI_STA); // Enable hardware entropy before generating credentials.
  loadOrCreateIdentity();
  bootID = randomString(16, "0123456789ABCDEF");
  localSessionToken = randomString(32, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
  setupApName = "ERU-SETUP-" + deviceID.substring(deviceID.length()-4);
  setupApPassword = "ERU-" + claimCode;
  Serial.println("Setup Wi-Fi: " + setupApName);
  Serial.println("Setup Wi-Fi password: " + setupApPassword);
  Serial.println("First local login: admin / " + setupApPassword);

  // Power cycle check
  checkPowerCycleFallback();

  // Initialize buttons (active-HIGH: pressed = HIGH)
  auto initButton = [](ButtonState &b, uint8_t pin) {
    bool pressed = digitalRead(pin) == HIGH;
    b.stableState = pressed;
    b.lastReading = pressed;
    b.lastDebounceTime = millis();
    b.pressStartTime = pressed ? millis() : 0;
    b.longPressHandled = pressed;   // ignore a button already held at boot
  };

  initButton(powerButton, POWER_BUTTON_PIN);
  initButton(resetButton, RESET_BUTTON_PIN);
  initButton(upButton, UP_BUTTON_PIN);
  initButton(downButton, DOWN_BUTTON_PIN);

  // WiFi
  bool wifiOK = connectWiFi();

  // Start the same HTTP server in STA mode too
  configureWebServer();

  if (!wifiOK) {
    // The saved network is unreachable. Open the setup hotspot so a phone
    // or computer can enter a different Wi-Fi without the GPIO19 button.
    Serial.println("[WIFI] Saved network failed. Starting PLUG_SETUP.");
    startSetupAP();
  }

  // Wi-Fi provides entropy before generating the runtime WebSocket token.
  localSessionToken = randomString(32, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
  configTime(0, 0, "pool.ntp.org", "time.google.com");

  Serial.print("Claim code: ");
  Serial.println(formatClaimCode());

  // ACS712 calibration (no current should flow now)
  calibrateACS712();

  // Current sampling runs in its own task from now on
  currentReadingsQueue=xQueueCreate(1,sizeof(CurrentReading));
  if(currentReadingsQueue && xTaskCreatePinnedToCore(currentTask,"currentTask",4096,NULL,1,NULL,0)!=pdPASS)
    Serial.println("[CURRENT] Task creation failed; measurement unavailable");

  // Startup is always electrically OFF; automation may act once loop starts.
  setRelay(false, "Safe startup OFF");
  startLocalRealtime();

  // Cloud runs in its own task: setup() never waits for it
  startCloudTask();

  updateOutputIndicators();

  Serial.println();
  Serial.println("SYSTEM READY");
  Serial.println("Real PIR: ENABLED");
  Serial.println("Real ACS712: ENABLED (background task)");
  Serial.println("Cloud: ENABLED (background task, device secret)");
  Serial.println("Offline operation: ENABLED");

  printStatus();
  renderLCD();
  startSafetyMonitor();
}

void loop() {
  controllerHeartbeat=millis();
  serviceSafetyTrip();

#if DEBUG_LOOP_TIME
  static unsigned long lastLoopTs = 0;
  unsigned long dt = millis() - lastLoopTs;
  if (lastLoopTs != 0 && dt > 50) {
    Serial.printf("[LOOP] slow pass: %lu ms\n", dt);
  }
  lastLoopTs = millis();
#endif

  // Buttons first: they must react immediately
  processMenuButton();
  processUpDownButtons();
  processResetButton();

  // Local web server (setup page in AP mode, controls in STA mode)
  if (webServerStarted) {
    setupServer.handleClient();
    serviceSafetyTrip();
  }

  processLocalRealtime();

  // WiFi
  if (wifiSSID.length() > 0) {
    maintainWiFi();
  }

  // Sensors
  processCurrentSamples();
  processPIR();
  updateBMI160();

  // Maximum-duration interlocks run before automatic ON decisions.
  processMaximumOnTime();
  processSmartEngine();
  processIronController();

  // Display
  updateLCD();

  // Cloud (non-blocking queue / apply)
  processCloud();

  // Deferred flash write
  processDeferredSave();

  // Clear power-cycle counter once after stable operation
  clearPowerCycleCounter();

  // LEDs
  updateOutputIndicators();

  delay(1);
}
