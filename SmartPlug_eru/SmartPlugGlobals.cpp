#include "SmartPlugShared.h"

// Shared controller state and hardware constants.
extern const char* SUPABASE_URL = ERU_SUPABASE_URL;
extern const char* SUPABASE_KEY = ERU_SUPABASE_PUBLISHABLE_KEY;

// ============================================================
// ===================== PAGE 2 - DEVICE IDENTITY AND PIN MAP =====================
// DEVICE IDENTITY
// ============================================================

String deviceID;
String deviceSecret;    // random, stored in flash, never displayed
String claimCode;       // 8 chars, shown on LCD while unclaimed

// ============================================================
// PINS
// ============================================================

// ============================================================
// RELAY LOGIC
// ============================================================

// ============================================================
// WIFI SETUP
// ============================================================

// Setup and initial local credentials are generated per device.
extern const char* LOCAL_WEB_USERNAME = "admin";

extern const unsigned long WIFI_RECONNECT_INTERVAL = 10000UL;
extern const unsigned long WIFI_CONNECT_TIMEOUT = 20000UL;

bool setupApActive = false;     // true while the PLUG_SETUP hotspot is on

// ============================================================
// ===================== PAGE 3 - TIMING AND DEVICE CONFIGURATION =====================
// CLOUD TIMING
// ============================================================

// ============================================================
// SENSOR TIMING
// ============================================================

extern const unsigned long PIR_SAMPLE_INTERVAL = 50UL;

// ============================================================
// BUTTON
// ============================================================

extern const unsigned long BUTTON_DEBOUNCE_MS = 20UL;
extern const unsigned long LONG_PRESS_MS = 700UL;          // POWER button hold
extern const unsigned long RESET_LONG_PRESS_MS = 1500UL;   // RESET button hold

// ============================================================
// POWER CYCLE FALLBACK
// ============================================================

extern const uint8_t POWER_CYCLE_LIMIT = 3;
extern const unsigned long POWER_CYCLE_RESET_TIME = 30000UL;

// ============================================================
// ACS712
// ============================================================

extern const float ACS712_SENSITIVITY = 0.185f;
extern const float ADC_DIVIDER_RATIO = 22.0f / (12.0f + 22.0f);
float acsZeroVoltage = 2.5f;
extern const float CURRENT_DEADBAND = 0.10f;   // suppress no-load noise < 100 mA

// ============================================================
// APPROXIMATE POWER
// ============================================================

extern const float NOMINAL_VOLTAGE = 230.0f;

// ============================================================
// WEB SERVER
// ============================================================

WebServer setupServer(80);
bool webServerStarted = false;

// ============================================================
// PREFERENCES
// ============================================================

Preferences wifiPrefs;
Preferences devicePrefs;
Preferences cyclePrefs;
Preferences identityPrefs;

// ============================================================
// WIFI
// ============================================================

String wifiSSID = "";
String wifiPassword = "";

// ============================================================
// SETTINGS
// ============================================================

String mode = "AUTO";
bool actualPowerState = false;
int pirSensitivity = 1;
unsigned long humanOnSeconds = 300;
unsigned long detectionDelaySeconds = 0;
bool maxOnEnabled = false;
unsigned long maxOnSeconds = 7200;

// ============================================================
// ===================== PAGE 4 - SHARED STATE AND STATUS =====================
// STATUS
// ============================================================

bool humanDetected = false;
bool lastPirState = false;
bool detectionPending = false;
unsigned long detectionStartTime = 0;
unsigned long lastHumanTime = 0;
unsigned long powerOnStartTime = 0;
String powerReason = "Startup";

// ============================================================
// FALLBACK
// ============================================================

bool fallbackMode = false;

// ============================================================
// CURRENT (written by currentTask, read by loop)
// ============================================================

float currentAmps = 0.0f;
float estimatedPowerWatts = 0.0f;
bool acsSensorPresent = false;

// ============================================================
// TIMERS
// ============================================================

unsigned long bootTime = 0;
unsigned long lastWiFiReconnectAttempt = 0;
unsigned long lastPirSample = 0;
bool powerCycleCounterCleared = false;

// ============================================================
// DEFERRED FLASH SAVE
// ============================================================

bool nvsSavePending = false;
unsigned long nvsSaveTime = 0;

// ============================================================
// BUTTON STRUCTURE
// ============================================================

ButtonState powerButton;
ButtonState resetButton;
ButtonState upButton;
ButtonState downButton;

// ============================================================
// LCD / MENU STATE
// ============================================================

LiquidCrystal_I2C lcd(0x27, 16, 2);
bool lcdReady = false;
bool menuOpen = false;
bool menuEditing = false;
int menuIndex = 0;
int menuPage = 0;
unsigned long lastLcdRefresh = 0;
unsigned long lastMenuActivity = 0;
extern const unsigned long MENU_TIMEOUT_MS = 30000UL;
extern const int MENU_ITEMS = 10;     // 0..5 editable, 6 IP, 7 status, 8 claim, 9 exit
String lcdCache[2];

// ============================================================
// RGB COLORS
// ============================================================

// ============================================================
// CLOUD SYNC STATE
// ============================================================

SemaphoreHandle_t cloudMutex = nullptr;

// shared between loop() and cloudTask (protected by cloudMutex)
CloudData cloudIncoming;
bool cloudIncomingReady = false;
String outboxPayload;
bool outboxPending = false;

// simple flags written by cloudTask, read by loop()
std::atomic<bool> cloudRegistered = false;
std::atomic<bool> cloudOnline = false;
std::atomic<bool> deviceOwned = false;       // true once a user has claimed this plug

// used by loop() only

// Called whenever the USER changes something locally

// SmartPlug V7 iron/BMI160 state
bool bmi160Detected = false;
bool bmi160Healthy = false;
bool ironPowerState = false;
bool ironMaxOnEnabled = false;
bool ironMotionDetected = false;
bool ironFault = false;
String ironMode = "OFF";
String ironPowerReason = "Startup safe OFF";
unsigned long ironInactivitySeconds = 300;
unsigned long ironMaxOnSeconds = 7200;
unsigned long ironPowerOnStartTime = 0;
unsigned long ironLastMotionTime = 0;
unsigned long lastBmiSample = 0;
float bmiAccelX = 0, bmiAccelY = 0, bmiAccelZ = 0, bmiMotionScore = 0;

// Controller-owned state. Network tasks exchange immutable JSON via cloudMutex.
bool plugTripped=false, ironTripped=false, maintenanceMode=false, plugOverrideActive=false;
unsigned long plugOverrideStart=0;
String bootID, localSessionToken, setupApName, setupApPassword;
std::atomic<bool> cloudSocketOnline=false;
bool telemetryUrgent=true;

QueueHandle_t currentReadingsQueue=nullptr;
