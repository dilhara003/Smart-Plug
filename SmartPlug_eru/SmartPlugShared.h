#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include <atomic>
#include <math.h>
#include "SafetyRules.h"
#include "SmartPlugConfig.h"

#define DEBUG_LOOP_TIME 0
#define RELAY1_PIN 32
#define RELAY2_PIN 4
#define PIR_PIN 33
#define ACS712_PIN 34
#define WIFI_LED_PIN 13
#define OUTPUT_LED_PIN 14
#define POWER_BUTTON_PIN 17
#define RESET_BUTTON_PIN 19
#define UP_BUTTON_PIN 16
#define DOWN_BUTTON_PIN 18
#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22
#define BUZZER_PIN 23
#define RGB_RED_PIN 25
#define RGB_GREEN_PIN 26
#define RGB_BLUE_PIN 27
#define RELAY_ON HIGH
#define RELAY_OFF LOW

struct ButtonState {
  bool stableState = false;
  bool lastReading = false;
  unsigned long lastDebounceTime = 0;
  unsigned long pressStartTime = 0;
  bool longPressHandled = false;
};

enum RGBColor { RGB_OFF, RGB_RED, RGB_GREEN, RGB_BLUE, RGB_YELLOW, RGB_CYAN, RGB_MAGENTA, RGB_WHITE };

struct CloudData {
  bool valid = false;
  String commandId, target, action, args;
  uint32_t receivedAt = 0, ttlMs = 0;
};

extern const char* SUPABASE_URL;
extern const char* SUPABASE_KEY;
extern String deviceID, deviceSecret, claimCode;
extern const char* LOCAL_WEB_USERNAME;
extern const unsigned long WIFI_RECONNECT_INTERVAL, WIFI_CONNECT_TIMEOUT;
extern bool setupApActive;
extern const unsigned long PIR_SAMPLE_INTERVAL;
extern const unsigned long BUTTON_DEBOUNCE_MS, LONG_PRESS_MS, RESET_LONG_PRESS_MS;
extern const uint8_t POWER_CYCLE_LIMIT;
extern const unsigned long POWER_CYCLE_RESET_TIME;
extern const float ACS712_SENSITIVITY, ADC_DIVIDER_RATIO, CURRENT_DEADBAND, NOMINAL_VOLTAGE;
extern float acsZeroVoltage;
extern WebServer setupServer;
extern bool webServerStarted;
extern Preferences wifiPrefs, devicePrefs, cyclePrefs, identityPrefs;
extern String wifiSSID, wifiPassword;
extern String mode;
extern bool actualPowerState;
extern int pirSensitivity;
extern unsigned long humanOnSeconds, detectionDelaySeconds;
extern bool maxOnEnabled;
extern unsigned long maxOnSeconds;
extern bool humanDetected, lastPirState, detectionPending;
extern unsigned long detectionStartTime, lastHumanTime, powerOnStartTime;
extern String powerReason;
extern bool fallbackMode;
extern float currentAmps, estimatedPowerWatts;
extern bool acsSensorPresent;
extern bool bmi160Detected, bmi160Healthy, ironPowerState, ironMaxOnEnabled;
extern bool ironMotionDetected, ironFault;
extern String ironMode, ironPowerReason;
extern unsigned long ironInactivitySeconds, ironMaxOnSeconds, ironPowerOnStartTime, ironLastMotionTime, lastBmiSample;
extern float bmiAccelX, bmiAccelY, bmiAccelZ, bmiMotionScore;
extern unsigned long bootTime, lastWiFiReconnectAttempt, lastPirSample;
extern bool powerCycleCounterCleared;
extern bool nvsSavePending;
extern unsigned long nvsSaveTime;
extern ButtonState powerButton, resetButton, upButton, downButton;
extern LiquidCrystal_I2C lcd;
extern bool lcdReady, menuOpen, menuEditing;
extern int menuIndex, menuPage;
extern unsigned long lastLcdRefresh, lastMenuActivity;
extern const unsigned long MENU_TIMEOUT_MS;
extern const int MENU_ITEMS;
extern String lcdCache[2];
extern SemaphoreHandle_t cloudMutex;
extern CloudData cloudIncoming;
extern bool cloudIncomingReady;
extern String outboxPayload;
extern bool outboxPending;
extern std::atomic<bool> cloudRegistered, cloudOnline, deviceOwned;
extern bool plugTripped, ironTripped, maintenanceMode, plugOverrideActive;
extern unsigned long plugOverrideStart;
extern String bootID, localSessionToken, setupApName, setupApPassword;
extern std::atomic<bool> cloudSocketOnline;
extern bool telemetryUrgent;
void enterMaintenance(const String& reason);
bool rearmOutput(bool iron);
void applyOutputSettings(bool iron, JsonObject args);
bool executeCommand(bool iron, const String& action, JsonObject args, String& reason);
String statusPayload();
void startLocalRealtime();
void processLocalRealtime();
void revokeLocalSessions();
String protectHtml(String html);
struct CurrentReading { float amps, apparentVA; };
extern QueueHandle_t currentReadingsQueue;
void processCurrentSamples();
extern std::atomic<bool> controllerStallTrip;
extern std::atomic<uint32_t> controllerHeartbeat;
void startSafetyMonitor();
void safetyMonitorTick();
void serviceSafetyTrip();

// Cross-module function declarations
void markLocalChange();
String createDeviceID();
String randomString(size_t length, const char* alphabet);
void loadOrCreateIdentity();
String formatClaimCode();
bool isValidCloudConfig();
void rgb(bool r, bool g, bool b);
void setRGB(RGBColor color);
void beep(uint16_t frequency = 2000, uint16_t duration = 80);
void updateOutputIndicators();
void setRelay(bool state, const String& reason);
void loadWiFiCredentials();
void saveWiFiCredentials(const String& ssid, const String& password);
void loadSettings();
void writeNvs();
void saveSettings();
void flushSettings();
void processDeferredSave();
bool connectWiFi();
void maintainWiFi();
bool requireWebAuth();
String htmlHeader();
String htmlFooter();
void handleSetupRoot();
void handleWiFiPage();
void handleWiFiConnect();
void handleFactoryReset();
void handleRootPage();
void handleLocalPowerCommand();
void handleLocalStatus();
void handleLocalSettings();
void handleChangeWebLogin();
void handleFirmwareUpload();
void handleFirmwareUploadResult();
void configureWebServer();
void startSetupAP();
void checkPowerCycleFallback();
void clearPowerCycleCounter();
bool readPIR();
void processPIR();
void calibrateACS712();
float readCurrent();
void currentTask(void* param);
void setMode(const String& newMode);
void nextMode();
void manualToggle();
void processAutoMode();
void processHybridMode();
void processMaximumOnTime();
void processSmartEngine();
void initBMI160();
void updateBMI160();
void processIronController();
void setIronPower(bool state, const String& reason);
void handleIronPowerCommand();
void handleIronSettings();
bool updateButton(ButtonState& button, int pin);
void processResetButton();
String buildPayload(bool full);
void applyCloudSnapshot();
void processCloud();
int cloudRpc(const char* functionName, const String& body, String& response);
bool cloudRegister();
bool cloudSend(const String& payload);
bool cloudFetch();
void cloudTask(void* param);
void startCloudTask();
void printStatus();
void lcdClear();
void lcdPrintLine(uint8_t row, String text);
String lcdModeName();
void renderLCD();
void updateLCD();
void processMenuButton();
void processUpDownButtons();
