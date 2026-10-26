#include "SmartPlugShared.h"

// Menu pages: 0 = section selector, 1 = plug settings, 2 = iron settings, 3 = device settings.
// Home-screen selection: false = Smart Plug, true = Iron.
static bool homeIronSelected = false;
static const int PLUG_ITEMS = 10;   // 0..5 settings, 6..8 info, 9 exit
static const int IRON_ITEMS = 5;   // 0 mode, 1 inactivity, 2 max enable, 3 max time, 4 exit
static const int DEVICE_ITEMS = 4; // 0 IP, 1 live status, 2 claim code, 3 exit

void lcdClear() {
  if (!lcdReady) return;
  lcd.clear();
  lcdCache[0] = "";
  lcdCache[1] = "";
}

void lcdPrintLine(uint8_t row, String text) {
  if (!lcdReady || row > 1) return;
  if (text.length() > 16) text = text.substring(0, 16);
  while (text.length() < 16) text += ' ';
  if (text == lcdCache[row]) return;
  lcdCache[row] = text;
  lcd.setCursor(0, row);
  lcd.print(text);
}

String lcdModeName() {
  if (mode == "MANUAL") return "MANUAL";
  if (mode == "AUTO") return "AUTO";
  if (mode == "HYBRID") return "HYBRID";
  return "NORMAL";
}

// Compact mode letters used on the 16x2 home screen.
static char plugModeLetter() {
  if (mode == "AUTO") return 'A';
  if (mode == "MANUAL") return 'M';
  if (mode == "HYBRID") return 'H';
  return 'N';
}

static char ironModeLetter() {
  if (ironMode == "AUTO") return 'A';
  if (ironMode == "MANUAL") return 'M';
  return 'O'; // Iron mode OFF
}

static int currentMenuItems() {
  if (menuPage == 1) return PLUG_ITEMS;
  if (menuPage == 2) return IRON_ITEMS;
  if (menuPage == 3) return DEVICE_ITEMS;
  return (bmi160Detected && bmi160Healthy) ? 4 : PLUG_ITEMS;
}

static void closeMenu() {
  menuOpen = false;
  menuEditing = false;
  menuPage = 0;
  menuIndex = 0;
  lcdClear();
  lastLcdRefresh = 0;
}

void renderLCD() {
  if (!lcdReady) return;

  if (!menuOpen) {
    if (!(bmi160Detected && bmi160Healthy)) homeIronSelected = false;
    if (bmi160Detected && bmi160Healthy) {
      if (homeIronSelected) {
        lcdPrintLine(0, String(" PLUG:") + (actualPowerState ? "ON" : "OFF") + " (" + plugModeLetter() + ")");
        lcdPrintLine(1, String(">IRON:") + (ironPowerState ? "ON" : "OFF") + " (" + ironModeLetter() + ")");
      } else {
        lcdPrintLine(0, String(">PLUG:") + (actualPowerState ? "ON" : "OFF") + " (" + plugModeLetter() + ")");
        lcdPrintLine(1, String(" IRON:") + (ironPowerState ? "ON" : "OFF") + " (" + ironModeLetter() + ")");
      }
      return;
    }
    // Plug-only screen still displays the selected operating mode when BMI160 is absent.
    lcdPrintLine(0, String(">PLUG:") + (actualPowerState ? "ON" : "OFF") + " (" + plugModeLetter() + ")");
    if (!acsSensorPresent) lcdPrintLine(1, "CURRENT: -- A");
    else {
      char buf[17];
      snprintf(buf, sizeof(buf), "CURRENT:%.2fA", currentAmps);
      lcdPrintLine(1, String(buf));
    }
    return;
  }

  String label, value;

  // When BMI160 is healthy, always show a section selector first.
  if (menuPage == 0 && bmi160Detected && bmi160Healthy) {
    switch (menuIndex) {
      case 0: label = "Plug Settings"; value = "Select: ENTER"; break;
      case 1: label = "Iron Settings"; value = "Select: ENTER"; break;
      case 2: label = "Device Settings"; value = "Select: ENTER"; break;
      default: label = "Exit Menu"; value = "Press ENTER"; break;
    }
    lcdPrintLine(0, String(">") + label);
    lcdPrintLine(1, value);
    return;
  }

  if (menuPage == 1 || (menuPage == 0 && !(bmi160Detected && bmi160Healthy))) {
    switch (menuIndex) {
      case 0: label = "Operating Mode"; value = lcdModeName(); break;
      case 1: label = "PIR Input"; value = "Digital motion"; break;
      case 2: label = "Human ON Time"; value = String(humanOnSeconds) + " sec"; break;
      case 3: label = "Detect Delay"; value = String(detectionDelaySeconds) + " sec"; break;
      case 4: label = "Max ON Limit"; value = maxOnEnabled ? "ON" : "OFF"; break;
      case 5: label = "Max ON Time"; value = String(maxOnSeconds / 60) + " min"; break;
      case 6: label = "Network IP"; value = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA ? WiFi.softAPIP().toString() : "WiFi Offline"); break;
      case 7: label = "Live Status"; value = actualPowerState ? "PLUG: ON" : "PLUG: OFF"; break;
      case 8: label = "Claim Code"; value = deviceOwned ? "CLAIMED" : formatClaimCode(); break;
      default: label = "Exit Menu"; value = "Press ENTER"; break;
    }
  } else if (menuPage == 2) {
    switch (menuIndex) {
      case 0: label = "Iron Mode"; value = ironMode; break;
      case 1: label = "Inactivity Time"; value = String(ironInactivitySeconds) + " sec"; break;
      case 2: label = "Iron Max ON"; value = ironMaxOnEnabled ? "ON" : "OFF"; break;
      case 3: label = "Max ON Time"; value = String(ironMaxOnSeconds / 60) + " min"; break;
      default: label = "Exit Section"; value = "Press ENTER"; break;
    }
  } else {
    switch (menuIndex) {
      case 0: label = "Network IP"; value = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA ? WiFi.softAPIP().toString() : "WiFi Offline"); break;
      case 1: label = "Live Status"; value = String("P:") + (actualPowerState ? "ON" : "OFF") + " I:" + (ironPowerState ? "ON" : "OFF"); break;
      case 2: label = "Claim Code"; value = deviceOwned ? "CLAIMED" : formatClaimCode(); break;
      default: label = "Exit Section"; value = "Press ENTER"; break;
    }
  }

  lcdPrintLine(0, String(menuEditing ? '*' : '>') + label);
  lcdPrintLine(1, value);
}

void updateLCD() {
  if (!lcdReady) return;
  unsigned long now = millis();
  if (now - lastLcdRefresh < 200) return;
  lastLcdRefresh = now;
  if (menuOpen && now - lastMenuActivity >= MENU_TIMEOUT_MS) closeMenu();
  renderLCD();
}

void processMenuButton() {
  unsigned long now = millis();

  // Long press opens/closes menu. Healthy BMI160 opens the section selector first.
  if (powerButton.stableState && !powerButton.longPressHandled && now - powerButton.pressStartTime >= LONG_PRESS_MS) {
    powerButton.longPressHandled = true;
    lastMenuActivity = now;
    if (menuOpen) closeMenu();
    else {
      menuOpen = true;
      menuPage = (bmi160Detected && bmi160Healthy) ? 0 : 1;
      menuIndex = 0;
      menuEditing = false;
      lcdClear();
      lastLcdRefresh = 0;
    }
    beep(2200, 45);
    return;
  }

  if (!updateButton(powerButton, POWER_BUTTON_PIN)) return;
  lastMenuActivity = now;

  if (!menuOpen) {
    // One short press toggles the device currently selected on the home screen.
    if (homeIronSelected && bmi160Detected && bmi160Healthy) {
      if (ironTripped) { rearmOutput(true); beep(1800,80); }
      else if (ironMode == "MANUAL") {
        setIronPower(!ironPowerState, "Local home-screen button");
      } else {
        Serial.println("[IRON] Select MANUAL mode in Iron Settings before local toggle");
        beep(900, 120);
      }
    } else {
      manualToggle();
    }
    lastLcdRefresh = 0;
    return;
  }

  // Section selector: press ENTER to enter the chosen section.
  if (menuPage == 0 && bmi160Detected && bmi160Healthy) {
    if (menuIndex == 0) { menuPage = 1; menuIndex = 0; }
    else if (menuIndex == 1) { menuPage = 2; menuIndex = 0; }
    else if (menuIndex == 2) { menuPage = 3; menuIndex = 0; }
    else { closeMenu(); }
    menuEditing = false;
    beep(2200, 45);
    lastLcdRefresh = 0;
    return;
  }

  int exitIndex = currentMenuItems() - 1;
  if (menuIndex == exitIndex) {
    if (bmi160Detected && bmi160Healthy) {
      menuPage = 0;
      menuIndex = 0;
      menuEditing = false;
      lcdClear();
    } else closeMenu();
  } else if (menuPage == 1 || (menuPage == 0 && !(bmi160Detected && bmi160Healthy))) {
    if (menuIndex == 1 || (menuIndex >= 6 && menuIndex <= 8)) menuIndex++;
    else if (menuIndex == 0) {
      if (!menuEditing) menuEditing = true;
      else { nextMode(); menuEditing = false; saveSettings(); }
    } else if (menuIndex >= 1 && menuIndex <= 5) {
      if (!menuEditing) menuEditing = true;
      else { menuEditing = false; saveSettings(); }
    }
  } else if (menuPage == 2) {
    if (!menuEditing) menuEditing = true;
    else { menuEditing = false; saveSettings(); }
  } else if (menuPage == 3) {
    menuIndex++;
  }
  beep(2200, 45);
  lastLcdRefresh = 0;
}

void processUpDownButtons() {
  bool up = updateButton(upButton, UP_BUTTON_PIN);
  bool down = updateButton(downButton, DOWN_BUTTON_PIN);
  if (!up && !down) return;
  int direction = up ? -1 : 1;

  // On the home screen, UP/DOWN selects which output a short POWER press controls.
  if (!menuOpen) {
    if (bmi160Detected && bmi160Healthy) {
      homeIronSelected = up ? false : true;
      beep(1800, 35);
      lastLcdRefresh = 0;
      renderLCD();
    } else {
      homeIronSelected = false;
    }
    return;
  }

  lastMenuActivity = millis();
  if (!menuEditing) {
    menuIndex += direction;
    int count = currentMenuItems();
    if (menuIndex < 0) menuIndex = count - 1;
    if (menuIndex >= count) menuIndex = 0;
  } else if (menuPage == 1 || (menuPage == 0 && !(bmi160Detected && bmi160Healthy))) {
    switch (menuIndex) {
      case 0: nextMode(); break;
      case 1: break; // Digital PIR sensitivity is not adjustable in software.
      case 2: if (up) humanOnSeconds = min(86400UL,humanOnSeconds + 10); else if (humanOnSeconds > 10) humanOnSeconds -= 10; else humanOnSeconds = 1; break;
      case 3: if (up) detectionDelaySeconds = min(3600UL,detectionDelaySeconds + 1); else if (detectionDelaySeconds > 0) detectionDelaySeconds--; break;
      case 4: maxOnEnabled = !maxOnEnabled; break;
      case 5: if (up) maxOnSeconds = min(86400UL,maxOnSeconds + 60); else if (maxOnSeconds > 60) maxOnSeconds -= 60; break;
    }
    saveSettings();
  } else if (menuPage == 2) {
    switch (menuIndex) {
      case 0:
        if (ironMode == "OFF") ironMode = up ? "MANUAL" : "AUTO";
        else if (ironMode == "MANUAL") ironMode = up ? "AUTO" : "OFF";
        else ironMode = up ? "OFF" : "MANUAL";
        if (ironMode == "OFF" || ironMode == "AUTO") setIronPower(false, "Iron mode changed from LCD");
        break;
      case 1: if (up) ironInactivitySeconds = min(86400UL,ironInactivitySeconds + 10); else if (ironInactivitySeconds > 10) ironInactivitySeconds -= 10; else ironInactivitySeconds = 1; break;
      case 2: ironMaxOnEnabled = !ironMaxOnEnabled; break;
      case 3: if (up) ironMaxOnSeconds = min(86400UL,ironMaxOnSeconds + 60); else if (ironMaxOnSeconds > 60) ironMaxOnSeconds -= 60; break;
    }
    saveSettings();
  }
  beep(1800, 35);
  lastLcdRefresh = 0;
  renderLCD();
}
