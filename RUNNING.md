# Running ERU SmartPlug

## What is ready on this computer

- The frontend is configured for `https://udmbcrnrdmxyhvgxcyfe.supabase.co`.
- The migration in `supabase/migrations/202610260001_smartplug.sql` has been applied to that project.
- The owner account for the requested email was created without an invitation email. Open `artifacts/OWNER_LOGIN.txt` for the generated application password. It is separate from the database password. Settings > Account lets you change it yourself.
- Production frontend files are generated in `dist/` by `npm run build`.
- ESP32 binaries are generated in `artifacts/firmware/` by `npm run firmware:build`.
- Enterprise-capable firmware 1.0.2 has now been installed on the connected ESP32 and its USB status verified. Controlled-load operation remains untested.

The release ZIP deliberately excludes credentials, `node_modules`, build caches, and original firmware backups. Your private login stays on this computer.

## Dashboard

Use Node.js 22.12+ or 24+ and npm. Node 26 was used for verification here.

```bash
cd "/Users/dilharabandara/My Projects/SmartPlug"
npm ci
cp .env.example .env.local
npm run dev
```

Open http://127.0.0.1:5173/. Do not copy the database password or a Supabase secret/service-role key into any `VITE_` variable. Browser configuration contains only the public URL and publishable key.

To view the production build:

```bash
npm run build
npm run preview
```

The preview URL is printed by Vite, normally http://127.0.0.1:4173/. Run the development server again after building if a previously open dev tab reports outdated optimized dependencies. `npm run dev -- --force` refreshes Vite's dependency cache.

### Show and control the dashboard on a phone

1. Put the Mac and phone on the same trusted local network. A personal hotspot or private router is useful for presentations because campus Wi-Fi can isolate devices. The ESP32 may remain on Kelani Wi-Fi: both browsers and the ESP32 use Supabase over the internet.
2. On the Mac, run `npm run build` and then `npm run preview:phone`. Leave that Terminal window open and allow incoming connections if macOS asks. This uses port 4173, so an existing development server on port 5173 can keep running.
3. Use the **Network** URL that Vite prints (for example, `http://192.168.1.23:4173/`) in the phone's browser. Use the Mac's local IP address, not `127.0.0.1` or the ESP32's IP. Sign in with the owner dashboard account; demo mode cannot control the real plug.
4. Before the presentation, check that the device says **Connected** and test a command with a safe load. The Mac, phone and ESP32 all need internet access; the Mac must stay awake while serving the page. If the URL will not open, check that the devices are on the same network, the Mac firewall allows the connection, and the network does not block local device traffic.

Stop the server with Ctrl+C when finished. For a public URL that works without the Mac, deploy the production `dist/` build to an HTTPS host and configure Supabase Authentication redirect URLs for that host.

The owner dashboard initially has no devices. This is intentional: add your real device with its physical claim code. Demo mode is separate, uses sample data in memory, and never sends real device commands.

For additional user sign-ups or a hosted frontend, set the production Site URL and allowed confirmation redirect URLs in Supabase Authentication settings. The pre-created owner account can sign in directly and does not depend on email confirmation links.

## Firmware prerequisites

The code retains the original ESP32 DevKit V1 pin map. The verified target is a classic ESP32 with **4 MB flash**. A different board, flash size, relay polarity or sensor circuit needs an explicit hardware review and corresponding build configuration.

Install Arduino CLI from Arduino's official distribution, or install Arduino IDE. This Mac's build script detects the CLI bundled with Arduino IDE. You can also set `ARDUINO_CLI` to an explicit executable path.

For a fresh Arduino CLI installation:

```bash
arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core install esp32:esp32@3.3.12 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
npm run firmware:build
```

The build script downloads the exact Arduino-registry libraries and verifies their SHA-256 hashes:

| Library | Version |
| --- | --- |
| ArduinoJson | 7.3.2 |
| LiquidCrystal I2C | 1.1.2 |
| WebSockets by Markus Sattler | 2.7.2 |

It installs them inside `firmware-libs/` without changing your global Arduino libraries. The selected board is `esp32:esp32:esp32:PartitionScheme=min_spiffs`. Do not select **Huge APP / No OTA** if you intentionally enable browser OTA later. The existing LiquidCrystal library emits an AVR architecture metadata warning; the ESP32 build succeeds. Actual LCD behavior still needs hardware verification.

The public cloud settings are in `SmartPlug_eru/SmartPlugConfig.h`. For another project, change both that file and `.env.local`, and run the migration in that project's SQL Editor. Custom local build overrides may go in the ignored `SmartPlugConfig.local.h`. Never embed the Supabase database password or server key in firmware.

## Upload

Before firmware replacement, disconnect the controlled mains loads and verify your USB board and relay wiring. This matters because changing firmware changes startup/control behavior. The code cannot establish electrical safety or correct relay-contact wiring.

```bash
python3 scripts/flash_firmware.py --port /dev/cu.usbserial-0001
```

The helper uses Arduino CLI and the generated bootloader, partition table and application files. It does not deliberately erase NVS, so existing device identity, Wi-Fi credentials and local login can survive. Do not flash the generated `*.merged.bin` across the whole chip unless you explicitly intend a factory-style image: that can erase stored identity/settings. The release package includes the component binaries used by the upload helper.

If the bootloader does not respond, check the USB cable/serial driver and board port; some boards require holding BOOT while the uploader connects. Use a stable power supply. Do not diagnose a failed upload by manipulating live mains wiring.

For Arduino IDE: open `SmartPlug_eru/SmartPlug_eru.ino`, install the library versions above using Library Manager, select **ESP32 Dev Module**, **4 MB**, and **Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)**, then verify/upload. Keep all `.cpp` and `.h` files with the sketch.

## Kelani Wi-Fi / university username and password

Firmware 1.0.2 supports university WPA2-Enterprise PEAP / MSCHAPv2. The university CA is embedded in `KelaniWiFiCA.h`, with server hostname `eduroam.kln.ac.lk` and certificate validity checks enabled. The CA was downloaded from https://rict.kln.ac.lk/images/Kelani_WiFi.cer; its validity is 10 September 2019 to 19 May 2033. See the official guide: https://rict.kln.ac.lk/images/Guidelines/How_to_connect_Kelani_WiFi__New.pdf.

Keep your Mac connected to campus Wi-Fi. Close Arduino Serial Monitor, upload the new firmware, and run:

```bash
python3 scripts/flash_firmware.py --port /dev/cu.usbserial-0001
python3 scripts/connect_kelani_wifi.py --port /dev/cu.usbserial-0001
```

The helper detects the advertised spelling of the campus SSID, asks for your **Kelani Net ID** and university Wi-Fi password, and provisions them directly over USB. Use the same username you use when joining campus Wi-Fi; it may differ from your full email address. Password input stays hidden in Terminal and is not written to source, shell arguments, logs or Supabase. No third-party Python package is required on macOS/Linux.

The Mac's current time is also provisioned over USB, because certificate validity must be checked before Wi-Fi/NTP is available. The ESP32 saves a last-known clock baseline and updates it during operation. If a long-unpowered device rejects a newer server certificate, rerun the helper to refresh its clock; certificate verification is not bypassed.

The helper prints `CONNECTED` and the local IP only after the ESP32 confirms a real Wi-Fi connection. The campus network must offer a reachable 2.4 GHz access point and allow your account/device. Campus client isolation may block Mac-to-ESP32 local IP access even when cloud control works. This firmware does not bypass university account or device restrictions.

The `/wifi` setup page also has a Personal/PEAP selector and university username field. For Kelani Wi-Fi, choose PEAP; ordinary home networks continue to use Personal. Credentials remain in the device's existing NVS storage, not in the cloud. Reprovisioning enters maintenance and switches both outputs OFF before saving/restarting.

## First connection

1. Open Serial Monitor at 115200 baud after reset.
2. Both output GPIOs initialize OFF. Plug Presence mode can respond to new sensor input once startup completes; iron mode always initializes Disabled.
3. With no usable saved Wi-Fi, join `ERU-SETUP-xxxx`. Serial Monitor prints its password and the initial local username/password. The initial local password is derived from the device's unique claim code, not a shared fleet password.
4. Open http://192.168.4.1 and authenticate. On the Wi-Fi page, the scan is asynchronous; refresh after a few seconds if the list is empty.
5. Save your router network. The device restarts with outputs OFF and reconnects. If credentials fail, setup Wi-Fi returns.
6. The LCD/Serial Monitor reports the router-assigned IP. Open it from the same LAN for local control, live sensor/status updates and local login changes.
7. Sign in to the cloud dashboard and choose Add device. Enter the claim code from the LCD menu and a name.

Saved Wi-Fi passwords remain in ESP32 NVS as in the original design. The local page uses HTTP and is intended for your trusted LAN. Do not port-forward ports 80/81 to the internet. For remote access, use the authenticated cloud dashboard.

## Controls and safety behavior

Plug modes:

- **Manual:** explicit ON/OFF control.
- **Presence:** PIR motion turns the output ON; the presence timer controls OFF.
- **Presence + override:** PIR automation with a timed manual override. A manual ON or OFF holds for the presence timeout before returning to automation.

Iron modes: Disabled, Manual, or Presence + significant BMI160 movement. Sensor health and inactivity checks apply to iron operation. A maximum-duration trip stays latched. Sensor loss while powered also latches iron OFF. A healthy sensor read alone does not re-energize a tripped output.

Use **Rearm** on the local/cloud dashboard to clear a trip. Rearming leaves power OFF and selects Manual for the plug or Disabled for the iron. Choose a mode or explicitly send ON afterwards. On the physical home screen, a short POWER press on a tripped selected output rearms it without energizing it; subsequent control follows its mode. Do not treat the LCD's legacy sensitivity field as a software-adjustable PIR range; it is now informational.

Repeated-reset recovery selects Manual/OFF. The iron never restores an ON state from flash. Startup does not energize the plug from a persisted power boolean.

An independent priority-3 task checks the control-loop heartbeat every 20 ms. A gap over 1000 ms latches both output GPIOs OFF, including during a slow local HTTP client. The main controller enters maintenance when it resumes. Restart and rearm after diagnosing the stall. This software measure does not replace correctly rated electrical protection, hardware pull-downs, or independent thermal protection for a heating tool.

Hold RESET/SETUP for 1.5 seconds to enter provisioning; both outputs turn OFF. Wi-Fi/network reset preserves device identity and the changed local login. It is not an ownership-transfer action.

## Firmware updates

Browser OTA is **disabled by default** (`ERU_ENABLE_LOCAL_OTA=0`). USB upload is the verified delivery path. To use local OTA intentionally, define the macro as 1 in a local override, build with an OTA-capable partition, and first upload that build over USB. OTA enters maintenance and forces both outputs OFF. Authentication and CSRF checks apply, and an empty/failed upload cannot report success.

The repository does not implement signed-release distribution, secure-boot provisioning, encrypted NVS, or a certified safety controller. Do not interpret the optional local uploader as a secure public firmware distribution service.

## Communication and data

The browser signs in through Supabase Auth. SQL functions enforce device ownership. RLS limits reads to owned devices; secret hashes are not selectable by browser accounts. Private Realtime topics are `sp:user:<auth user ID>`.

An ON/OFF/settings request has a unique ID, a 20-second expiry, the current device boot ID and an applied/rejected acknowledgement. OFF may supersede queued work. A device restart expires commands from the previous boot. The UI waits for reported state and marks expired unconfirmed work honestly; it does not infer successful switching from an HTTP request alone.

Device WebSockets carry only empty wake hints on an unguessable topic derived from the device secret's hash. They never carry commands or sensor readings. The ESP32 authenticates each actual command fetch/status exchange using its own secret over verified HTTPS. A forged wake cannot create a command. Wake processing is rate-limited. A ten-second exchange/heartbeat normally updates cloud readings; if the device WebSocket disconnects, two-second HTTPS polling recovers command delivery with exponential backoff after failures. Local status streams update about once per second.

The ESP32 needs a valid clock from NTP to validate cloud certificates. Until time is synchronized, cloud traffic remains disabled and local operation continues. Embedded public Google/ISRG/Amazon trust anchors live in `SmartPlugTrustedRoots.h`; update these if your endpoint's issuing CA changes. Certificate verification is never disabled as a fallback.

The cloud stores one current-measurement snapshot per minute for seven days, and events/commands for 30 days. It does not claim measured real watts or billing energy. Apparent power is estimated from RMS current and nominal 230 V. Neither output state is independent relay-contact feedback.

## Tests and evidence

- `npm test`: compiles actual controller function bodies in a native C++ harness. Covers boot OFF, maximum-time latches, repeated AUTO/manual ON attempts, rearming, sensor recovery, hybrid override, maintenance, independent stall cutoff and timer wraparound.
- `npm run build`: TypeScript and production asset build.
- `npm run test:e2e`: demo-mode control/settings/activity interactions and no horizontal overflow at 320, 390 and 1440 pixels. Demo tests do not prove physical operation.
- `python3 scripts/test_database.py`: applies the migration twice in isolated PostgreSQL and validates ownership, secret-column access, bad input/credentials, duplicate IDs, delivery/ACK, notification routing, OFF supersession and boot expiry. Requires a local database containing only test fixtures. Default DSN is a temporary Unix-socket test instance; override `SMARTPLUG_TEST_DB` explicitly if needed. Never point it at your hosted project.
- `node scripts/verify_live.mjs`: read-only live owner sign-in, protected columns, invalid-registration rejection, own/foreign private-channel authorization and exact ESP32 WebSocket join format. Uses `artifacts/owner-login.json` and the public key. Update that private file if you change the owner password before rerunning it.

On this Mac, PostgreSQL 16 test setup was:

```bash
initdb -D /private/tmp/eru-smartplug-pg -A trust -U postgres --no-locale -E UTF8
pg_ctl -D /private/tmp/eru-smartplug-pg -l /private/tmp/eru-smartplug-pg/server.log -o '-k /private/tmp/eru-smartplug-pg -h "" -p 55439' start
python3 scripts/test_database.py
pg_ctl -D /private/tmp/eru-smartplug-pg stop
```

The test cluster uses a private directory and Unix socket; it does not expose a TCP database service. Supabase `auth`/`realtime` functions are stand-ins only for these local SQL tests. Live channel authorization was verified separately against the actual service. The broader live synthetic fixture test was not run; physical relay, sensor calibration, disconnect/recovery timing and mains-load behavior still require hardware testing.

Firmware 1.0.2 corrects exact TLS server-name matching. The public campus server certificate stored on the Mac has CN `eduroam.kln.ac.lk`; its signature was verified against the official university CA. Android documentation uses `kln.ac.lk` as a domain suffix; the ESP-IDF API uses an exact hostname. CA, hostname and validity checks remain enabled. To read diagnostics without changing settings: `python3 scripts/connect_kelani_wifi.py --status --port /dev/cu.usbserial-0001`.
