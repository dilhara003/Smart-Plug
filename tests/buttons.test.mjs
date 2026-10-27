import { test } from 'node:test';
import { readFileSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { execFileSync } from 'node:child_process';

function firmwareFunction(file, signature) {
  const source = readFileSync(new URL('../SmartPlug_eru/' + file, import.meta.url), 'utf8');
  const start = source.indexOf(signature);
  if (start < 0) throw new Error('Firmware function missing: ' + signature);
  let cursor = source.indexOf('{', start) + 1;
  let depth = 1;
  while (depth && cursor < source.length) {
    if (source[cursor] === '{') depth++;
    else if (source[cursor] === '}') depth--;
    cursor++;
  }
  return source.slice(start, cursor);
}

test('firmware buttons debounce and distinguish short and long presses', () => {
  const functionSource = firmwareFunction('SmartPlugControl.cpp', 'ButtonEvent updateButton(');
  const dir = mkdtempSync(join(tmpdir(), 'eru-buttons-'));
  try {
    const code = `#include <cassert>
using uint8_t = unsigned char;
constexpr int HIGH = 1;
constexpr unsigned long BUTTON_DEBOUNCE_MS = 20;
enum class ButtonEvent : uint8_t { None, Short, Long };
struct ButtonState {
  bool stableState = false, lastReading = false, longPressHandled = false;
  unsigned long lastDebounceTime = 0, pressStartTime = 0;
};
unsigned long clockMs = 0;
bool pressed = false;
unsigned long millis() { return clockMs; }
int digitalRead(int) { return pressed ? HIGH : 0; }
${functionSource}
ButtonEvent sample(ButtonState& button, unsigned long time, bool level, unsigned long holdMs) {
  clockMs = time; pressed = level;
  return updateButton(button, 17, holdMs);
}
int main() {
  ButtonState power;
  assert(sample(power, 100, true, 700) == ButtonEvent::None);
  assert(sample(power, 111, false, 700) == ButtonEvent::None); // bounce
  assert(sample(power, 150, true, 700) == ButtonEvent::None);
  assert(sample(power, 171, true, 700) == ButtonEvent::None);
  assert(sample(power, 300, false, 700) == ButtonEvent::None);
  assert(sample(power, 321, false, 700) == ButtonEvent::Short);
  assert(sample(power, 340, false, 700) == ButtonEvent::None);

  ButtonState held;
  sample(held, 1000, true, 700);
  sample(held, 1021, true, 700);
  assert(sample(held, 1700, true, 700) == ButtonEvent::Long);
  sample(held, 1800, false, 700);
  assert(sample(held, 1821, false, 700) == ButtonEvent::None);

  ButtonState missedThreshold;
  sample(missedThreshold, 2000, true, 700);
  sample(missedThreshold, 2021, true, 700);
  sample(missedThreshold, 2800, false, 700);
  assert(sample(missedThreshold, 2821, false, 700) == ButtonEvent::Long);
  assert(sample(missedThreshold, 2840, false, 700) == ButtonEvent::None);

  ButtonState nearThreshold;
  sample(nearThreshold, 3000, true, 700);
  sample(nearThreshold, 3021, true, 700);
  sample(nearThreshold, 3680, false, 700);
  assert(sample(nearThreshold, 3701, false, 700) == ButtonEvent::Short);

  ButtonState reset;
  sample(reset, 4000, true, 1500);
  sample(reset, 4021, true, 1500);
  sample(reset, 5600, false, 1500);
  assert(sample(reset, 5621, false, 1500) == ButtonEvent::Long);
}`;
    const cpp = join(dir, 'buttons.cpp');
    const binary = join(dir, 'buttons');
    writeFileSync(cpp, code);
    execFileSync(process.env.CXX || 'clang++', ['-std=c++17', cpp, '-o', binary], { stdio: 'pipe' });
    execFileSync(binary, [], { stdio: 'pipe' });
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test('confirming plug mode does not change it again', () => {
  const dir = mkdtempSync(join(tmpdir(), 'eru-menu-'));
  try {
    const code = `#include <cassert>
#include <string>
using String = std::string;
using uint8_t = unsigned char;
enum class ButtonEvent : uint8_t { None, Short, Long };
struct ButtonState {};
ButtonState powerButton;
constexpr int POWER_BUTTON_PIN = 17;
constexpr unsigned long LONG_PRESS_MS = 700;
unsigned long tick = 1000, lastMenuActivity = 0, lastLcdRefresh = 0;
unsigned long millis() { return tick; }
ButtonEvent nextEvent = ButtonEvent::Short;
ButtonEvent updateButton(ButtonState&, int, unsigned long) { return nextEvent; }
bool menuOpen = true, menuEditing = true, bmi160Detected = false;
bool bmi160Healthy = false, homeIronSelected = false, ironTripped = false;
bool ironPowerState = false;
int menuPage = 1, menuIndex = 0, saves = 0, modeChanges = 0;
String ironMode = "OFF", mode = "AUTO";
void closeMenu() { menuOpen = false; }
void lcdClear() {}
void beep(int, int) {}
void saveSettings() { saves++; }
void nextMode() { modeChanges++; mode = "HYBRID"; }
void manualToggle() {}
bool rearmOutput(bool) { return true; }
void setIronPower(bool, const String&) {}
int currentMenuItems() { return 10; }
struct Log { void println(const char*) {} } Serial;
${firmwareFunction('SmartPlugLCD.cpp', 'void processMenuButton(')}
int main() {
  processMenuButton();
  assert(mode == "AUTO" && modeChanges == 0 && saves == 1 && !menuEditing);
  processMenuButton();
  assert(mode == "AUTO" && modeChanges == 0 && saves == 1 && menuEditing);
}`;
    const cpp = join(dir, 'menu.cpp');
    const binary = join(dir, 'menu');
    writeFileSync(cpp, code);
    execFileSync(process.env.CXX || 'clang++', ['-std=c++17', cpp, '-o', binary], { stdio: 'pipe' });
    execFileSync(binary, [], { stdio: 'pipe' });
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});
