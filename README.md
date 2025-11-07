# BB Intercom (CYD)

> Small ESP32 + TFT touchscreen intercom/display firmware.

This project runs on an ESP32 (PlatformIO env `esp32dev`) and implements a simple intercom display with touch, MQTT integration and a tiny HTTP control server.

## At-a-glance
- Device used: ESP32-2432S028 [Cheap Yellow Display](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display) 
- MCU: ESP32 (PlatformIO environment `esp32dev`)
- Display: TFT (TFT_eSPI) with XPT2046 touch controller
- Network: WiFi (scans configured SSIDs), MQTT (PubSubClient)
- Web: small HTTP server on port 80 with a few endpoints

## Key files
<!-- - `platformio.ini` — build environment, pinned libraries and `-include` of `include/User_Setup.h`. -->
- `src/main.cpp` — entire application logic (WiFi, MQTT, display, touch, HTTP routes).
- `include/User_Setup.h` — TFT driver, pins, fonts and SPI settings used by `TFT_eSPI`.
- `include/constants.h` — app constants and Preferences keys (namespace `BBI_PREFS`).
- `include/credentials-template.h` — template for WiFi and MQTT credentials. COPY to `include/credentials.h` before flashing.

## Build / Upload / Monitor
Use the PlatformIO CLI (recommended) or VS Code PlatformIO extension.

Build:
```bash
pio run -e esp32dev
```

Upload (flash):
```bash
pio run -e esp32dev -t upload
```

Serial monitor (115200):
```bash
pio device monitor -e esp32dev -b 115200
```

## Credentials / Secrets
- NEVER commit `include/credentials.h` with real secrets. Use `include/credentials-template.h` as a starting point.
- Copy the template and fill in your networks and MQTT brokers:

```bash
cp include/credentials-template.h include/credentials.h
# edit include/credentials.h and fill in SSIDs, passwords, broker hosts/ports
```

The firmware iterates `wifiCredentials` and `mqttBrokers` arrays using `sizeof(array)/sizeof(type)` so keep the array style unchanged.

## Runtime behavior & important notes
- Serial logging is controlled by the `USE_SERIAL` #define in `src/main.cpp`. Disable for minimal output in production.
- Touch calibration is stored using the Preferences API under namespace `BBI_PREFS`. Keys are defined in `include/constants.h` (e.g. `tlx`, `tly`, `trx`, `try`).
- `INTERCOM_PIN` (defined in `src/main.cpp`) is configured as `INPUT_PULLUP` — active low (0 = ringing, 1 = idle).
- MQTT topics used by the firmware:
  - `/intercom/active` — publish 0/1 when idle/ringing
  - `/intercom/info` — publishes basic info (IP)
  - `/intercom/time` — incoming time messages (subscribed)
  - `/intercom/uptime` — publishes uptime periodically
- HTTP endpoints (port 80): `/` (status page), `/intercom` (POST, accepts `intercom=1` or `0`), `/uptime`, `/restart`, `/reset`, `/colour` (POST)

## Code & style conventions
- Use C-style fixed-size buffers (e.g. `char[32]`) — the project is designed for constrained flash/heap.
- The app is a mostly single-file design (`src/main.cpp`) with globals for hardware objects (TFT, WiFiClient, PubSubClient). Prefer adding small helper functions or new files under `src/` rather than refactoring big structural changes without testing on-device.
- Font configuration and flash usage matters: `User_Setup.h` controls which fonts are compiled in (`LOAD_FONT*`, `LOAD_GFXFF`) — enabling many fonts increases flash usage.

## Debugging & quick tests
- To remotely trigger the intercom (without touching the device), POST to the `/intercom` endpoint:

```bash
curl -X POST -d "intercom=1" http://<device-ip>/intercom
curl -X POST -d "intercom=0" http://<device-ip>/intercom
```

- MQTT testing: publish to `/intercom/time` to update the display clock or subscribe to `/intercom/active` to see state changes.

## Suggestions for CI / headless testing
- The code is hardware-dependent; for automated CI consider extracting logic into testable modules and providing mock implementations of TFT/WiFi/MQTT interfaces.
- A minimal approach: create a small host-side script that calls the HTTP endpoints (above) against a running device or an emulator.

## Troubleshooting
- If the device doesn’t connect to WiFi, ensure `include/credentials.h` contains the exact SSID string and password and that the SSID is within range — the firmware scans visible networks and tries matches from the credentials list.
- If display output is corrupted, check `include/User_Setup.h` for correct `TFT_WIDTH`, `TFT_HEIGHT`, driver (`ILI9341_2_DRIVER` etc.) and pin mappings.

## Where to start when modifying this project
1. Copy credentials template -> fill your secrets.
2. Build and flash with PlatformIO.
3. Use serial monitor to observe boot logs and WiFi/MQTT connection attempts.
4. Modify small helper functions in `src/main.cpp` and verify on-device. For larger refactors, add tests or mock harnesses.
