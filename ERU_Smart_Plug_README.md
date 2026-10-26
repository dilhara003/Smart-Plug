# ERU Smart Plug  --- Product and Technical README

**Product:** ERU Smart Plug and Iron\
**Firmware family:** SmartPlug modular firmware\
**Supply/load environment:** 230 V AC mains\
**Controller:** ESP32 DevKit V1 (30-pin board)\
**LCD:** 16 × 2 I²C LCD, address `0x27`\
**Status:** tested and want to apporoved

------------------------------------------------------------------------

## 1. What the product does

ERU Smart Plug is an ESP32-based switching controller for a plug output
and, when the optional BMI160 module is detected and healthy, a separate
iron output. It combines:

-   Physical buttons and a 16 × 2 LCD.
-   PIR human-presence/movement detection for automatic plug operation.
-   Optional BMI160 motion sensing for iron control.
-   ACS712 current sensing for the plug load.
-   Wi-Fi connectivity and a local web interface.
-   Supabase-based remote status and command exchange.
-   Saved settings in ESP32 non-volatile storage (NVS).
-   Relay and sensor fault interlocks intended to put the iron output
    OFF when its BMI160 safety input is unavailable.

.

## 2. Main operating idea

There are two separately controlled outputs:

1.  **Plug output --- Relay 1, GPIO32.** Uses the main mode setting and
    PIR logic.
2.  **Iron output --- Relay 2, GPIO4.** Uses the separate iron mode and
    BMI160 health/movement checks.

The iron relay is not simply mirrored from the plug relay. The two
outputs can have different states. When BMI160 is absent or its reads
fail, firmware blocks iron ON and turns the iron output OFF if it was
on. The plug output can continue to operate without BMI160.

## 3. Hardware and pin map

Use this map when checking the supplied firmware against the PCB. Do not
change pin assignments casually: the PCB and code must agree.

  Function                      ESP32 GPIO
  --------------------------- ------------
  Plug relay (Relay 1)                  32
  Iron relay (Relay 2)                   4
  PIR sensor output                     33
  ACS712 current-sensor ADC             34
  Wi-Fi indicator LED                   13
  Output indicator LED                  14
  POWER/MODE button                     17
  RESET/SETUP button                    19
  UP button                             16
  DOWN button                           18
  I²C SDA (LCD and BMI160)              21
  I²C SCL (LCD and BMI160)              22
  Buzzer                                23
  RGB LED red                           25
  RGB LED green                         26
  RGB LED blue                          27

The button inputs in this firmware are active-HIGH: released is LOW and
pressed is HIGH, using ESP32 internal pull-down configuration. The
actual PCB wiring must match this.

### Main modules

-   **ESP32 DevKit V1:** runs the control, display, Wi-Fi, local server,
    and cloud logic.
-   **PIR sensor:** provides the human-detection input used by the
    plug's AUTO/HYBRID behaviour and as one condition for iron AUTO.
-   **BMI160:** optional I²C accelerometer/gyroscope module. The source
    checks I²C addresses `0x68` and `0x69` and expects chip ID `0xD1`.
-   **ACS712-5A module:** current measurement. The source assumes 185
    mV/A sensitivity and uses a divider of 12 kΩ from sensor OUT to ADC
    node and 22 kΩ from ADC node to ground; confirm the physical circuit
    and exact module before use.
-   **LCD:** I²C 16 × 2 display at `0x27`.
-   **Relay drivers:** drive the relay coils; GPIOs must not directly
    drive relay coils.
-   **Indicators and buzzer:** provide local feedback.

## 4. Power supply and repeated restarting

### Recommended design approach

The ESP32 must receive a **stable, regulated low-voltage DC supply**
with enough current headroom for Wi-Fi transmission, the LCD, sensor
modules, indicator LEDs, buzzer, and relay-driver/relay loads. For a
development build, a regulated **5 V supply rated for at least 1 A is a
starting point for evaluation; 5 V / 2 A can provide more headroom**,

-   Use a reputable, isolated, certified AC-to-DC supply designed for
    the intended mains voltage and enclosure.
-   Feed the ESP32 DevKit through its intended 5 V/VIN input only if
    that is the board's documented input path. Never feed 5 V into a 3.3
    V pin.
-   Ensure the relay coil supply and ESP32 ground/reference are arranged
    according to the driver schematic. Do not power relay coils from an
    ESP32 GPIO.
-   Keep noisy relay-coil current and high-current paths from causing
    voltage drops on the ESP32 supply. Use suitable wiring, decoupling,
    driver circuitry, and flyback suppression as required by the
    relay/driver design.
-   Do not connect an exposed or non-isolated mains supply to a
    user-accessible circuit. All 230 V wiring must remain inside an
    appropriately designed enclosure and be reviewed by a qualified
    person.

### Why it may restart continuously

Repeated booting can be caused by a weak or defective supply, a voltage
dip when Wi-Fi transmits or a relay switches, poor connections, a short,
incorrect grounding, excessive peripheral load, electrical interference,
or a firmware crash. Do not assume the supply is the cause until the
reset reason and voltage have been checked.

### Safe troubleshooting sequence

1.  Disconnect the mains load and iron. Work only on the isolated
    low-voltage side for initial diagnosis.
2.  Open Arduino Serial Monitor at **115200 baud** and record the
    boot/reset messages. Look for brownout messages, watchdog resets,
    panic/backtrace output, or a repeated startup sequence.
3.  Verify the regulated supply voltage at the ESP32 input while the
    board starts and while Wi-Fi and relays operate. A multimeter may
    miss short dips; use an oscilloscope or suitable logger if
    available.
4.  Test with a known-good regulated supply of adequate current
    capacity, correct polarity, and the correct input voltage for the
    board.
5.  Check for loose connectors, shorts, incorrect relay-driver wiring,
    hot components, and wiring that lets relay current disturb the ESP32
    supply.
6.  Add or change decoupling only according to a reviewed schematic; do
    not randomly increase capacitors or bypass protection.
7.  If restarts continue with a stable supply and peripherals safely
    isolated, investigate firmware panic/watchdog logs and peripheral
    faults.

**Never probe or adjust live 230 V wiring as a troubleshooting step.**
Mains testing, supply replacement inside the enclosure,
insulation/clearance checks, and final load tests must be handled by a
qualified person.

### Power-cycle fallback behaviour in this source

The firmware stores a boot counter. If it detects **three power cycles
before the stable-runtime counter is cleared**, it enters a fallback
state, sets the main mode to `NORMAL`, and requests the plug relay ON.
After 30 seconds of stable runtime, the counter is cleared. This is a
recovery behaviour, not proof that the supply is faulty and not a
substitute for fixing a repeated-reset cause. Before testing repeated
power interruptions, remove hazardous loads and understand that fallback
can energize the plug output.

## 5. LCD behaviour

At startup the LCD title is:

``` text
SMART PLUG
Starting...
```

The home display uses a selection marker (`>`) to indicate the selected
output.

-   **BMI160 not detected:** the display is plug-only. The plug line
    includes the current plug state and mode letter, for example
    `>PLUG:ON (A)`. The second line displays the current reading, such
    as `CURRENT:0.35A`.
-   **BMI160 detected and healthy:** the display can show Plug and Iron
    states with their respective mode letters. UP/DOWN moves the
    selection marker between the two outputs.
-   The state/mode shown should be read as the firmware's reported
    state; it is not independent electrical feedback from the relay
    contacts unless separate feedback hardware is installed.

Mode letters used on the plug display: - `A` --- AUTO - `M` --- MANUAL -
`H` --- HYBRID - `N` --- NORMAL

Iron has its own mode (`OFF`, `MANUAL`, or `AUTO`) in the current
source; do not assume the iron uses the same four-mode list as the plug.

## 6. Plug operating modes

### MANUAL (`M`)

Automatic PIR switching is disabled. The user controls the plug state
through the available local/app/web control path.

### AUTO (`A`)

PIR human detection can turn the plug ON. When human detection ends, the
human-on timer determines when the plug turns OFF. The default in the
source is 300 seconds, unless changed by saved settings or cloud
settings.

### HYBRID (`H`)

The source uses the same PIR-triggered ON and human-timeout pattern as
AUTO in this build. Check the exact current firmware before assuming
additional hybrid behaviour.

### NORMAL (`N`)

The plug operates as a straightforward ON/OFF output; the automatic PIR
engine is bypassed.

### Additional plug settings

The firmware includes PIR sensitivity, detection delay, human-on
duration, and optional maximum-ON timer settings. Defaults in the source
include sensitivity level 1, human-on duration 300 seconds, detection
delay 0 seconds, and maximum ON disabled with a stored maximum duration
of 7200 seconds. Persisted/cloud settings may change these values.

## 7. Iron operation and BMI160 safety logic

The iron feature is optional. It is enabled only when BMI160 is detected
and reads are healthy.

-   **BMI160 missing/unhealthy:** Relay 2 is forced OFF or blocked from
    turning ON. The plug output (Relay 1) is not disabled solely because
    BMI160 is missing.
-   **Iron OFF:** forces the iron output OFF.
-   **Iron MANUAL:** a manual ON request is allowed only while BMI160 is
    detected and healthy. The inactivity timeout still applies.
-   **Iron AUTO:** firmware requires both PIR human detection **AND**
    significant BMI160 movement to activate the iron output.
-   **Movement classifier:** the current source compares the change in
    the 3-axis acceleration vector between samples. Its initial
    threshold is 0.35 g, requiring two qualifying samples, with an 800
    ms motion hold. These are tuning values and must be validated on the
    final mounting and real device; they are not a certified movement or
    safety detector.
-   **Inactivity timeout:** default 300 seconds. If no qualifying
    movement refreshes the timer, the iron output turns OFF.
-   **Maximum ON timer:** optional; stored default duration is 7200
    seconds and the feature is disabled by default.
-   **Fault response:** BMI160 I²C read failure marks the sensor
    unhealthy and turns the iron output OFF if it was on.

.

## 8. Current measurement

The ACS712 measures current in the monitored conductor. The source uses:

-   Nominal AC voltage for approximate power calculation: 230 V.
-   ACS712 sensitivity assumption: 0.185 V/A.
-   ADC divider ratio: `22 kΩ / (12 kΩ + 22 kΩ)`.
-   Current deadband: 0.10 A in this source, used to suppress small
    no-load fluctuations.
-   A zero-current calibration during startup, with no load current
    flowing.

The displayed value is a sensor-derived estimate, not a calibrated
billing meter. Estimated power is calculated approximately as
`current × 230 V`; for AC loads this is not necessarily true real power
because power factor and waveform are not measured. If calibration is
performed while a load is drawing current, readings can be inaccurate.
Do not exceed the sensor, conductor, PCB, connector, relay, or product
rating.

## 9. Buttons and indicators

  -----------------------------------------------------------------------
  Control                             Source behaviour
  ----------------------------------- -----------------------------------
  POWER/MODE (GPIO17)                 Short press is used for the local
                                      power/mode interaction implemented
                                      by the firmware; long press (about
                                      700 ms) is handled as a long press
                                      by the button logic.

  RESET/SETUP (GPIO19)                Short press requests restart;
                                      holding about 1.5 seconds enters
                                      Wi-Fi setup mode.

  UP (GPIO16)                         Moves through LCD selections/menu
                                      options.

  DOWN (GPIO18)                       Moves through LCD selections/menu
                                      options.
  -----------------------------------------------------------------------

The exact short-press/long-press action depends on the current menu
state. Buttons use a 20 ms debounce setting. RGB and separate indicator
LEDs provide local status/mode feedback; the buzzer provides audible
feedback.

## 10. Wi-Fi setup and local web control

### Normal connection

The firmware loads saved Wi-Fi credentials from ESP32 NVS and attempts
to connect. If connected, it starts the local HTTP server on port 80 and
reports the local IP in serial/LCD/menu where the interface provides it.

### Enter setup mode

1.  Press and hold **RESET/SETUP** for approximately 1.5 seconds.
2.  Connect a phone or computer to the ESP32 setup network named
    `PLUG_SETUP`.
3.  Open `http://192.168.4.1` in the browser.
4.  Enter the target Wi-Fi credentials on the setup page and submit
    them.
5.  Reconnect the phone/computer to the normal router network after the
    ESP32 joins it.
6.  Read the ESP32's local IP from the LCD/menu or serial output. Open
    that IP from a device on the same local network.

The local IP may change when the router reconnects or renews DHCP
leases. A router DHCP reservation can make it more consistent.

### Local control while internet is down

When the router and local Wi-Fi network remain operational but the
internet or Supabase is unavailable, the local web interface is designed
to continue working on the same LAN. If the router itself is down,
ordinary local-IP access will not work; use physical controls and the
configured recovery process.



## 11. Supabase and remote control

The firmware communicates with Supabase through RPC functions named
`device_register`, `device_push`, and `device_pull` (as implemented in
the source). The device identity/secret is stored locally and is used
for device authentication. The cloud task periodically fetches commands
and pushes status/telemetry.

The source reports values including: - Plug actual state, mode,
PIR/human status, current estimate, power reason. - Wi-Fi SSID, local IP
and signal strength. - BMI160 detected/healthy state, iron state/mode,
motion indication, timers and iron fault/reason.

**Backend compatibility requirement:** the Supabase `device_push` and
`device_pull` functions and database schema must explicitly support the
iron fields for end-to-end cloud iron telemetry/control. A working plug
integration does not automatically prove that iron cloud fields are
supported. Review and test database/RPC changes carefully; do not
blindly rerun old migrations or remove working row-level security
policies. Keep ownership/claiming and account-sharing authorization
enforced server-side.

Remote control requires both the ESP32 and internet/cloud path to be
available. A local IP is reachable only from the same LAN; it is not a
public internet address. Do not expose the local web server directly to
the public internet.

## 12. Device identity and ownership

The firmware creates a device identity and claim code. The serial output
reports the device ID and claim code. Treat the claim code and device
credentials as sensitive. Follow the app's supported add/claim workflow,
and only share device access through the application's intended
ownership/sharing system. Never put secrets into public screenshots or
support posts.

## 13. Startup sequence

At a high level, startup performs the following:

1.  Starts serial diagnostics at 115200 baud.
2.  Configures GPIO and places both relay outputs OFF initially.
3.  Starts I²C and initializes the LCD; displays `SMART PLUG` and
    `Starting...`.
4.  Loads saved Wi-Fi credentials and settings from NVS.
5.  Attempts to detect and initialize BMI160. If unavailable, iron is
    locked OFF.
6.  Checks the power-cycle fallback counter.
7.  Connects to Wi-Fi if credentials are saved and configures the local
    web server.
8.  Loads/creates device identity and claim code.
9.  Initializes cloud communication and the sensor/control tasks.
10. Runs the control loop for PIR, current sampling, iron
    health/movement, button/menu input, cloud sync, display refresh, and
    indicators.

Actual relay startup behaviour is governed by the firmware and saved
mode/fallback state. Always test relay behaviour with hazardous loads
disconnected after a firmware change.

## 14. Troubleshooting guide

  -----------------------------------------------------------------------
  Symptom                             Likely checks
  ----------------------------------- -----------------------------------
  ESP32 continuously restarts         Read serial reset reason; verify
                                      stable supply at the board during
                                      Wi-Fi/relay events; inspect shorts,
                                      wiring, relay-driver noise, and
                                      crash logs.

  LCD backlight is on but text is     Adjust the LCD contrast trimmer
  faint/blank                         gently; verify I²C address `0x27`,
                                      SDA GPIO21, SCL GPIO22, supply and
                                      ground.

  Plug does not turn ON in AUTO       Check selected mode, PIR output,
                                      PIR supply/wiring, saved human
                                      timeout/detection delay, and serial
                                      reason messages.

  Iron will not turn ON               Check BMI160 address/wiring/3.3 V
                                      compatibility, sensor health, iron
                                      mode, inactivity/max timer, and
                                      Relay 2 driver. Firmware
                                      intentionally blocks iron ON when
                                      BMI160 is unhealthy.

  Iron turns OFF after inactivity     This may be expected from the iron
                                      inactivity timer or maximum-ON
                                      setting.

  Current reads zero or appears       Confirm ACS712 wiring, divider
  inaccurate                          values, sensor power, calibration
                                      with no current, deadband and
                                      sensor range.

  Local IP page cannot be reached     Confirm phone and ESP32 are on the
                                      same router/LAN, find the current
                                      IP, check client isolation/firewall
                                      and whether the ESP32 is connected.

  Remote control fails but local      Check internet connectivity, device
  control works                       ownership/claim, device online
                                      status, Supabase RPC/schema
                                      compatibility, and device
                                      authentication. Avoid changing
                                      working RLS policies without a
                                      reviewed plan.

  BMI160 not detected                 Verify SDA/SCL, common ground, safe
                                      3.3 V-compatible interface, address
                                      `0x68`/`0x69`, and chip ID.

  Restart fallback unexpectedly turns The firmware may have counted
  plug ON                             repeated boots and entered
                                      fallback. Remove hazardous loads,
                                      investigate reset cause, then
                                      clear/recover the counter only
                                      using a deliberate, reviewed
                                      procedure.
  -----------------------------------------------------------------------

.

## 15. Firmware project layout

Keep every source file together in the `SmartPlug_V7` folder:

-   `SmartPlug_V7.ino` --- startup and main loop.
-   `SmartPlugShared.h` --- shared declarations, pin map and
    configuration.
-   `SmartPlugGlobals.cpp` --- shared state and constants.
-   `SmartPlugHardware.cpp` --- GPIO, relay helpers, settings, identity
    and fallback handling.
-   `SmartPlugControl.cpp` --- plug modes and button control.
-   `SmartPlugSensors.cpp` --- PIR and ACS712 processing.
-   `IronController.cpp` --- BMI160 and iron interlock/control.
-   `SmartPlugLCD.cpp` --- LCD and menu rendering.
-   `SmartPlugWiFi.cpp` --- Wi-Fi setup and local web server.
-   `SmartPlugSupabase.cpp` --- cloud RPC, command polling and status
    reporting.

Open `SmartPlug_V7.ino` in Arduino IDE with the ESP32 board package
installed and the required libraries available (`ArduinoJson`,
`LiquidCrystal_I2C`, plus ESP32 core libraries). Select the correct
ESP32 Dev Module board and COM port. Compile before uploading; keep a
backup of the last known-good firmware and record the exact source
version used for each hardware build.



**End of README --- ERU Smart Plug **
