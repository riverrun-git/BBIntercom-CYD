## Quick overview

This is a small ESP32 Arduino-based intercom/display project (BB Intercom). The firmware runs on an ESP32 (`env:esp32dev` in `platformio.ini`) and handles:
- TFT display + touch (TFT_eSPI + XPT2046)
- WiFi connectivity (scans known SSIDs and connects)
- MQTT for intercom signals (PubSubClient)
- A tiny HTTP server for simple control pages and endpoints

Key sources to inspect:
- `platformio.ini` — build environment, libraries (`TFT_eSPI`, `PubSubClient`, XPT2046) and `-include $PROJECT_DIR/include/User_Setup.h`.
- `src/main.cpp` — single-file application logic: WiFi scanning/connection, MQTT broker selection/connection, HTTP routes, display and touch handling.
- `include/credentials-template.h` — template for wifi/mqtt credentials. Copy this to `include/credentials.h` with real secrets before flashing.
- `include/User_Setup.h` — display driver/pin/font configuration used by `TFT_eSPI`.
- `include/constants.h` — app-level constants and Preferences keys (namespace `BBI_PREFS`).

Build, upload and debug
- Build: `pio run -e esp32dev` (or `pio run` to build default env)
- Upload: `pio run -e esp32dev -t upload`
- Serial monitor: `pio device monitor -e esp32dev -b 115200` (Serial output is enabled at 115200 in `setup()`)
- VS Code PlatformIO: use the usual Build / Upload / Monitor buttons for the `esp32dev` environment.

Credentials & secrets
- DO NOT commit `include/credentials.h` with real credentials. The repo provides `include/credentials-template.h` — copy it to `include/credentials.h` and fill in SSIDs, passwords, and MQTT broker entries.
- The code expects arrays of `WiFiCredentials` and `MqttBroker` (see `include/credentials-template.h`). The firmware iterates these with `sizeof(array)/sizeof(type)`.

Runtime & debugging notes
- Serial logs are controlled by `USE_SERIAL` define in `src/main.cpp`. Toggle to enable/disable UART output (useful for unit/CI runs vs device).
- Touch calibration and related constants are persisted via the `Preferences` API under namespace `BBI_PREFS`. Keys (e.g. `tlx`, `tly`, `trx`, `try`, ...) live in `include/constants.h`.
- Intercom hardware: `INTERCOM_PIN` is configured as `INPUT_PULLUP` (value 0 => ringing, 1 => idle). Search `INTERCOM_PIN` and `updateIntercom` in `src/main.cpp` for behavior.
- MQTT topics used by the firmware: `/intercom/active`, `/intercom/info`, `/intercom/time`, `/intercom/uptime`. HTTP endpoints: `/`, `/intercom` (POST), `/uptime`, `/restart`, `/reset`.

Coding conventions & patterns to follow
- C-style strings and small fixed-size char buffers are used throughout (e.g., `char[32]`). Prefer this pattern over heavy std::string usage to control flash/heap on ESP32.
- Global state and free functions: the app is single-file-style with many globals (WiFiClient, PubSubClient, TFT_eSPI, arrays). When adding features prefer adding new helper functions in `src/` and minimal new globals.
- Font and display choices are set in `User_Setup.h` and referenced by macros in `src/main.cpp` (e.g. `FONT_NUMBER`, `FREE_FONT`). Changing fonts usually means editing `User_Setup.h`.
- Keep builds reproducible by not changing `platformio.ini` library URIs unless necessary; dependencies and versions are pinned there.

What an AI agent should do first
1. If asked to run or flash: remind user to copy `include/credentials-template.h` -> `include/credentials.h` and confirm secrets are correct.
2. For feature work, point to `src/main.cpp` and suggest minimal edits or well-scoped new files under `src/`.
3. For display or touch issues, check `include/User_Setup.h` and `display`-related macros in `include/constants.h` first.

If anything in this file is unclear or you'd like examples (e.g. a safe test harness that runs without hardware, or a mock MQTT/HTTP test), say what you want and I'll add it.

Last updated: auto-generated for repository by a tooling pass.
