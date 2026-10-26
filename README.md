# ERU SmartPlug

ESP32 firmware, a responsive React dashboard, and a Supabase backend for independent plug and soldering-iron control.

## Run the dashboard

Requires Node.js 22.12+ or 24+ and npm.

```bash
npm ci
cp .env.example .env.local
npm run dev
```

Open **http://127.0.0.1:5173/**. The supplied public configuration points to your Supabase project. Its migration has already been applied. Your owner login is in the private, Git-ignored file `artifacts/OWNER_LOGIN.txt` on this computer. **Explore the demo** works without an account or physical hardware; all demo readings and commands are clearly labelled.

## Build and upload firmware

The verified build uses ESP32 Arduino core **3.3.10**, **ESP32 Dev Module**, and the **Minimal SPIFFS / 1.9 MB APP with OTA** partition layout on a 4 MB ESP32.

```bash
npm run firmware:build
python3 scripts/flash_firmware.py --port YOUR_USB_PORT
```

On this Mac, a USB serial interface was detected at `/dev/cu.usbserial-0001`. Confirm the connected board and port before running the upload command. The firmware was compiled but **has not been flashed or electrically tested**. Compiled files are in `artifacts/firmware/`.

After upload, open Serial Monitor at **115200 baud**. The device prints its unique setup Wi-Fi and first local-login credentials. Connect to `ERU-SETUP-xxxx`, open `http://192.168.4.1`, and enter your router Wi-Fi. Add the device to the cloud dashboard with the claim code from its LCD menu. The device-local page uses HTTP port 80 and authenticated status streaming on WebSocket port 81.

Read **[RUNNING.md](RUNNING.md)** for first-time Arduino setup, upload details, startup behavior, recovery, and limitations. The previous hardware/pin documentation remains in [ERU_Smart_Plug_README.md](ERU_Smart_Plug_README.md); this README and RUNNING.md describe the current firmware behavior.

## Verify

```bash
npm test                 # Native C++ probes using the actual controller functions
npm run build            # TypeScript check and production frontend
npx playwright install chromium
npm run test:e2e         # Controls, settings, activity, and 320/390/1440 px layouts
```

`scripts/test_database.py` runs the migration twice and permission/command regression checks against an **isolated local PostgreSQL instance**; see RUNNING.md. `scripts/verify_live.mjs` uses the owner login and public key for read-only live authentication/WebSocket checks. Neither runtime nor frontend requires a server key or database password.

## Layout

| Path | Purpose |
| --- | --- |
| `web/` | React/TypeScript dashboard |
| `SmartPlug_eru/` | ESP32 firmware and original pin assignments |
| `supabase/migrations/` | Re-runnable backend migration |
| `scripts/` | Build, upload, and verification tools |
| `tests/` | Controller, database, and browser regression tests |
| `artifacts/` | Private local credentials, build outputs, screenshots and evidence; ignored by Git |

Cloud commands use expiring IDs and application acknowledgements. Browsers receive private owner-scoped Supabase WebSocket notifications. Devices receive content-free WebSocket wake hints, then fetch executable commands over verified HTTPS with their device secret. HTTPS polling recovers from lost WebSocket notifications. Physical controls and local operation remain independent of cloud availability.
