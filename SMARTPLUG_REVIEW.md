# SmartPlug technical review

Reviewed 26 October 2026. Scope: the README and all ten firmware source/header files in `SmartPlug_eru/`.

## Recommendation

Keep the ESP32 hardware integration and useful firmware modules. Refactor control/state handling, harden authentication and updates, introduce reliable command acknowledgements, and build a separate mobile-friendly web application. Migrate communication incrementally. A full rewrite would discard useful work without automatically resolving the control defects identified below.

WebSockets are useful for the browser experience, but a WebSocket connection alone does not provide durable commands, device authorization, replay protection, state reconciliation, or proof that a relay changed state.

## Scope and verification limits

This folder contains ESP32 Arduino firmware, an embedded HTML dashboard, and a technical README. It does not contain a separate web application, Supabase SQL schema/RPC definitions, database policies, build manifest, dependency lock, automated tests, or hardware schematic. The cloud URL and key in the firmware are placeholders. Server-side ownership, claim security, RPC field handling, and deployed UI behavior cannot be verified from this folder.

Neither Arduino CLI nor PlatformIO is installed in the execution environment. I did not perform a complete ESP32 compilation, flash a device, operate relays, or connect to a live Supabase project.

I compiled and ran a temporary native C++ probe with the actual extracted `setRelay()` and `processIronController()` function bodies and mocked hardware/time dependencies. It reproduced two control defects described below. These probes verify those software branches, not electrical behavior or whole-device correctness. No firmware was changed.

## Existing architecture and strengths

- `SmartPlug_eru.ino`: startup sequencing and main loop.
- `SmartPlugShared.h` / `SmartPlugGlobals.cpp`: declarations, pin map, configuration, and shared state.
- `SmartPlugHardware.cpp`: identity, relay control, settings persistence, restart fallback.
- `SmartPlugSensors.cpp`: PIR processing and background current measurement.
- `IronController.cpp`: BMI160 communication, motion detection, iron interlocks and commands.
- `SmartPlugControl.cpp`: plug modes, timers and button handling.
- `SmartPlugLCD.cpp`: local menus and output selection.
- `SmartPlugWiFi.cpp`: provisioning, embedded UI, status API, local authentication and firmware upload.
- `SmartPlugSupabase.cpp`: registration, cloud polling, telemetry, snapshot application and queues.

Useful foundations include independent plug/iron relay control, local operation, sensor-failure iron shutdown, deferred settings writes, device identity/claiming, and cloud/current work separated from the main loop. Preserve these capabilities while improving their boundaries.

## Findings in priority order

### 1. Saved ON state can disagree with the relay output — high priority

Evidence: `SmartPlugHardware.cpp:129`, `SmartPlugHardware.cpp:194`, and `SmartPlug_eru.ino:118`.

Startup drives Relay 1 OFF, then `loadSettings()` loads the saved power state directly into `actualPowerState`. In non-AUTO startup, `setRelay(actualPowerState, ...)` returns immediately because the requested state equals that variable. The GPIO remains OFF and the ON timer remains zero. Software can therefore report ON without commanding the relay ON; the maximum-ON check also skips a zero start time.

Probe result with saved ON: software state 1, GPIO state 0, GPIO writes 0, start timer 0.

Fix: separate saved desired state from applied output state. Initialize the applied state from the boot GPIO action; apply startup policy through an explicit output transition that initializes timers. Decide deliberately whether restoring ON is permitted for each output.

### 2. Restart recovery deliberately requests plug ON — high priority

Evidence: `SmartPlugHardware.cpp:270`; called before Wi-Fi setup/calibration at `SmartPlug_eru.ino:76`.

Three boots before the stable-runtime reset enter NORMAL mode and request ON. This counts boots, not independently verified deliberate user power cycles. It can turn repeated crashes or supply problems into an ON request. If the saved state was OFF, this path can energize the plug before current calibration despite the calibration's no-current assumption. If saved state was ON, finding 1 can prevent the GPIO write instead.

Fix: use a recovery/setup mode with an explicit output policy, preferably OFF by default. Do not interpret repeated resets as implicit authorization to energize a load. Record reset reasons and make manual restart policy explicit.

### 3. Iron maximum-ON cutoff does not latch — high priority

Evidence: `IronController.cpp:111`.

AUTO starts the iron whenever human detection and motion are true. The maximum-duration check switches it OFF later in the same function. On the next pass, the same AUTO conditions can immediately turn it back ON and restart its duration timer.

Probe result: OFF after the maximum-duration check; ON again on the next pass 50 ms later with motion still active.

Fix: represent timer trip as a latched state with a documented rearm condition. Route every ON request through that interlock, including cloud, buttons, menus, local web, and AUTO. Define whether a new motion event can rearm inactivity shutdown separately from a maximum-duration trip.

### 4. Cloud TLS server verification is disabled — high priority

Evidence: `SmartPlugSupabase.cpp:351`, `cloudClient.setInsecure()`.

The connection does not authenticate the cloud server certificate. This matters because device credentials and control data pass through it. Use CA verification, hostname checking and a workable device-time initialization policy. Plan certificate updates rather than relying on skipped verification.

Reference: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/protocols/esp_tls.html

### 5. Long web/setup work shares the control loop — high priority

Evidence: `SmartPlug_eru.ino:173`, `SmartPlugWiFi.cpp:307`, `SmartPlugWiFi.cpp:128`, `SmartPlugWiFi.cpp:438`.

`setupServer.handleClient()` executes before sensor processing and safety timers. Firmware upload, synchronous network scans, and setup delays execute through this path. Consequently, control response depends on how long these operations take. A cloud worker task does not isolate local web operations from the main controller.

Fix: enter an explicit maintenance state with outputs OFF for OTA and define safe provisioning behavior. Run the control state machine on a bounded schedule with messages from network/UI tasks; validate actual worst-case response time. Add update authenticity, correct-board/version checks, rollback and recovery appropriate to the deployment. Those guarantees are not established by the supplied source/build configuration.

### 6. Local security and provisioning need consolidation

Evidence: `SmartPlugGlobals.cpp:40`, `SmartPlugWiFi.cpp:10`, `SmartPlugWiFi.cpp:94`, `SmartPlugWiFi.cpp:128`, `SmartPlugWiFi.cpp:417`.

- The setup network uses a shared default password and a shared SSID. Use a unique device suffix and per-device setup credential, with a bounded commissioning window.
- The actual local login comes from `loadWebAuth()` with an NVS override and a fixed fallback; the separate `LOCAL_WEB_*` globals are not the credentials used by that function. Consolidate this misleading configuration.
- Local Basic authentication runs over port-80 HTTP. Provide a deliberate trusted-LAN/commissioning security model; use HTTPS through a gateway if direct-device TLS is impractical.
- Factory reset changes state through GET. Use POST and CSRF protection for state-changing browser requests; validate origins where appropriate.
- SSIDs and configured values are concatenated into HTML without escaping. Escape text and attribute contexts, or build DOM values safely. Nearby Wi-Fi names are external input too.
- The reset routine clears Wi-Fi/settings, but not identity or local-login namespaces. Name the action accurately and define separate network reset, credential recovery, and ownership-transfer flows.

### 7. Cloud state exchange lacks explicit command identity

Evidence: `SmartPlugSupabase.cpp:47`, `SmartPlugSupabase.cpp:145`, `SmartPlugSupabase.cpp:310`.

The cloud sends a state snapshot rather than a command with its own identity and lifetime. Comparing values with `cloudLastSeen` cannot distinguish two separate ON requests with the same value. After a safety shutdown, a repeated unchanged ON can be ignored. Conversely, reconnect reconciliation lacks a command expiry/revision boundary. `localChangeSeq` protects a local outbox acknowledgement; it is not a complete end-to-end command protocol.

Iron configuration is also included in ordinary telemetry payloads while plug configuration is limited to full pushes. Whether this overwrites newer remote intent depends on the missing RPC implementation and must be checked there.

Fix: separate desired configuration, commands, reported state, and telemetry. Add a command ID, target output, explicit action, server-controlled expiry, ordering/revision, and device acknowledgement with accepted/rejected/applied status and reason. Deduplicate command execution. Acknowledge GPIO application accurately; do not claim electrical contact feedback without feedback hardware.

### 8. Polling is expensive and state updates are less immediate than comments suggest

Evidence: `SmartPlugGlobals.cpp:57`, `SmartPlugSupabase.cpp:169`, `SmartPlugHardware.cpp:142`.

Configured intervals are a cloud pull every second and ordinary telemetry every ten seconds. Under ideal steady connected operation, that is about 86,400 pulls plus 8,640 pushes per device per day, before retries/local changes. Actual counts depend on network/request duration; these are request-rate estimates, not billing predictions.

Setting `lastOutboxBuild = 0` does not bypass the interval comparison once uptime is nonzero: during the first interval after boot, an intended immediate report can still be delayed. Use an explicit pending-state-change flag, and coalesce sensor telemetry separately from control transitions.

Use event-driven state/fault/command reports plus slower heartbeats. Send live measurements at an intentional rate and store aggregated history rather than writing every sample indefinitely.

### 9. Mode behavior and visible settings are inconsistent

Evidence: `SmartPlugControl.cpp:36`, `SmartPlugControl.cpp:42`, `SmartPlugControl.cpp:59`, `SmartPlugSensors.cpp:7`.

AUTO and HYBRID currently implement the same PIR ON/OFF behavior. MANUAL and NORMAL both bypass the smart engine. The physical plug toggle does not select MANUAL, while local web power commands do; AUTO can therefore counteract a physical toggle.

`pirSensitivity` is displayed, stored and synchronized, but is not consulted by PIR processing. The LOW/MEDIUM/HIGH setting currently does not alter detection behavior.

Define a smaller, unambiguous mode model. For example: Manual, Presence, and Presence with a timed manual override. Remove or implement sensitivity honestly; do not imply software adjusts the sensor's physical detection range.

### 10. Sensor health and measurement labels need improvement

Evidence: `IronController.cpp:21`, `SmartPlugSensors.cpp:78`, `SmartPlugSensors.cpp:118`, `SmartPlugSensors.cpp:154`.

BMI160 initialization ignores configuration-write results and then sets healthy. Validate writes/readback and reset motion baseline when recovering from a sensor fault. Test stale/frozen samples and mounting-specific motion behavior, not only I2C disconnects.

Current acquisition uses RMS variation about the sampled mean, and estimates apparent load magnitude as current times nominal 230 V. It does not measure actual voltage or power factor. Label the result as an estimate/apparent power as appropriate; do not present it as measured real watts or billing-quality energy. Validate ACS712 range and ADC scaling against the physical circuit. Hardware ratings and mains protection cannot be assessed from code alone.

### 11. Embedded dashboard can show stale data

Evidence: `SmartPlugWiFi.cpp:216`, `SmartPlugWiFi.cpp:266`, `SmartPlugWiFi.cpp:375`.

The embedded dashboard polls `/api/status` every two seconds. Its script refreshes plug/network/current fields but not the iron state, fault, sensor health or motion fields. The iron section is rendered conditionally at page load, so sensor recovery/failure does not reliably change the visible section. Failed requests are silently ignored, leaving old values looking current.

The JSON status payload omits the plug power reason, timer countdowns, firmware version and timestamps useful for an honest monitoring UI. Controls submit forms and navigate away; they do not provide a pending-to-confirmed command experience.

Fix these data and interaction issues alongside visual redesign. Keep forms from being overwritten by streaming status while the user edits settings.

### 12. Reproducibility and observability are missing

There is no build manifest or pinned ESP32 core/library version. README/sketch comments refer to filenames that differ from the supplied sketch. This folder also is not a Git repository. Initialize version control, pin dependencies, document board/partition settings, and automate compilation. Add reset reason, firmware/build ID, free heap, task stack headroom, command latency, last successful cloud exchange, and fault/event history. Check task/mutex creation failures and synchronize cross-task data deliberately; `volatile` flags alone are not a full concurrency design.

## Recommended communication architecture

### First migration: preserve Supabase and reduce risk

1. Correct controller defects and define command/acknowledgement semantics.
2. Build a separate responsive React/TypeScript application, with Supabase authentication and authorization-backed device access.
3. Use private Supabase Realtime channels to update the browser. Validate channel access against device ownership/sharing; a channel name is not authorization.
4. Initially retain ESP32 HTTPS RPC polling behind a transport abstraction. Browser Realtime improves live display after telemetry arrives, but it does not eliminate the ESP32 polling delay.
5. Migrate the device transport after the protocol and recovery behavior are tested.

Supabase recommends Broadcast for scalable database-change subscriptions and documents private channel authorization. Direct ESP32 integration is possible, but requires implementing its channel protocol, joins, heartbeat, reconnection and authentication lifecycle; it is more than opening an arbitrary WebSocket.

References:
- https://supabase.com/docs/guides/realtime/subscribing-to-database-changes
- https://supabase.com/docs/guides/realtime/protocol

### Device transport when persistent communication is justified

Recommended product direction: MQTT over verified TLS from ESP32 to a broker, an authenticated backend worker connecting device messages to Supabase, and Supabase Realtime/WebSockets to browsers. Keep the embedded page for setup, maintenance and local fallback.

Logical paths:

```text
Web app -> authorized command API -> command record -> worker -> MQTT -> ESP32
ESP32 -> MQTT reported state / acknowledgement -> worker -> Supabase
Supabase -> private Realtime channel -> Web app
Local buttons / local web -> same ESP32 controller and interlocks
```

MQTT supplies IoT-oriented publish/subscribe, QoS, retained state and last-will mechanisms. These are useful building blocks; a QoS acknowledgement is not confirmation that a load physically switched. A broker and a continuously running worker add operational responsibility, so this stage is optional for a small prototype.

Apply per-device topic ACLs and credentials. Use explicit `set ON`/`set OFF` actions rather than retryable `toggle`. Use command IDs and expiry even with QoS. Do not retain hazardous ON commands for later reconnect delivery. Retained reported state must include freshness; use a heartbeat timeout as well as last-will status. Reconcile a fresh snapshot after reconnect and reject expired commands. Keep a bounded queue/backoff policy and local control independent of the cloud.

MQTT reference: https://docs.oasis-open.org/mqtt/mqtt/v5.0/mqtt-v5.0.html

A custom backend WebSocket connection is also viable if avoiding a broker is a firm requirement, but then your application owns delivery/retry/session/reconciliation behavior. Do not expose the ESP32 directly to the public internet. Add local WebSocket or SSE streaming only if local dashboard requirements justify it; a pleasant local UI can also use ordinary HTTP commands and a well-handled status stream/poll.

## UI direction

Build a mobile-first device dashboard with a clean light surface, clear type, restrained accent color and distinct plug/iron cards. Prioritize readable state and reliable controls over decorative charts.

- Home: device name, cloud/local connectivity, last updated time, output cards, mode, current, and any fault requiring attention.
- Output detail: explicit ON/OFF controls, pending/applied/rejected feedback, reason for state, inactivity countdown and maximum-duration cutoff/rearm state.
- Automation: clear mode descriptions, durations in readable units, validation, and a summary such as “Turn off after 5 minutes without detected movement.”
- History: command source, applied/rejected result, timer trips, sensor faults, disconnect/reconnect and firmware events. Add measurement charts only after collection/storage semantics are sound.
- Device settings: Wi-Fi, access/sharing, naming, firmware and recovery. Separate destructive actions from daily controls.

Design for offline, reconnecting, stale, pending, rejected, unsupported sensor and sensor-fault states. A power button should show “Sending…” until the device reports application. If the result is uncertain, say so; avoid showing a confirmed ON based only on a successful browser request. Preserve a visible iron fault card when its sensor fails rather than hiding the capability.

Suggested database domains: devices/ownership and sharing, desired configuration with revision, reported state with freshness, expiring commands, command acknowledgements, events, and aggregated telemetry. Validate the existing database before introducing migrations because its schema is absent here.

## Implementation order and acceptance checks

1. Establish reproducible build/version control and fix relay startup, recovery policy and timer rearm behavior.
2. Consolidate all command sources behind one controller with documented state transitions and local priority rules.
3. Harden commissioning, TLS, local actions and OTA; isolate control timing from network/UI work.
4. Implement command identity, expiry, acknowledgements, freshness and reconnect reconciliation through current RPCs.
5. Build the web UI and private Realtime subscriptions; validate actual backend ownership rules.
6. Introduce MQTT if device count, latency requirements or polling overhead justify the extra infrastructure.

Before treating the result as ready, test saved ON/OFF startup in every mode; repeated resets; both outputs under simultaneous commands; offline local operation; duplicate and expired commands; cloud/local conflicts; stale snapshots on reconnect; sensor loss and recovery; timer expiry/rearm; slow or aborted OTA; and telemetry displayed after connectivity loss. Verify physical relay behavior with suitable isolated test hardware, because mocked probes do not establish mains safety or contact state.
