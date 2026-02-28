# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

PickHacks 2026 — an ESP32 embedded device that detects potholes using a BNO086 IMU and reports them to a REST server over HTTPS. GPS is simulated via a button-triggered waypoint path.

## Arduino Libraries Required

Install these via Arduino IDE Library Manager:
- **SparkFun BNO08x Arduino Library** (by SparkFun Electronics)
- **ArduinoJson** v7+ (by Benoit Blanchon)

Board: ESP32 (install via Boards Manager → `esp32` by Espressif)

## File Structure (`ESP32Code/`)

| File | Responsibility |
|------|----------------|
| `config.h` | All tuneable constants — pins, thresholds, credentials, buffer size |
| `event_buffer.h/cpp` | Fixed-size FIFO queue of `PotholeEvent` structs |
| `imu.h/cpp` | BNO086 over VSPI; pothole detection state machine; tilt lockout |
| `gps_sim.h/cpp` | Button-triggered waypoint simulation |
| `wifi_manager.h/cpp` | Non-blocking WiFi reconnect; HTTPS time sync on connect |
| `server_client.h/cpp` | Batch HTTPS POST with API key; ACK-before-remove handshake |
| `ESP32Code.ino` | `setup()` / `loop()` entry point only |

## Hardware

- **SPI bus**: VSPI (SCK=18, MISO=19, MOSI=23). CS and INT pins are constants in `config.h`.
- **BNO086 WAKE pin**: tied on the breakout board, no software control needed.
- **Button**: `GPS_SIM_BUTTON_PIN` with `INPUT_PULLUP`; active LOW.

## Pothole Detection Algorithm

Linear acceleration is projected onto the gravity vector (from `SH2_GRAVITY`) to obtain a mounting-orientation-independent vertical acceleration component.

**State machine: IDLE → DETECTING → COOLDOWN**
- **Trigger**: vertical accel toward ground exceeds `POTHOLE_NEG_THRESHOLD`
- **Confirm**: vertical accel away from ground exceeds `POTHOLE_POS_THRESHOLD` within `POTHOLE_MAX_DURATION_MS`
- **False-positive guards**:
  - Tilt lockout (>45°) — catches fallen/falling bike
  - Freefall disqualification — sustained near-zero linear accel for `FREEFALL_DISQUALIFY_MS` cancels the event (distinguishes jumps from potholes)
  - Cooldown — prevents double-counting
- **Score (1–100)**: linear map of the average peak magnitude between the average threshold and `POTHOLE_SEVERE_ACCEL`

## Server Protocol

- **Time sync**: `GET /api/time` → `{ "unix": <uint32> }` — called once on WiFi connect
- **Reporting**: `POST /api/potholes` with JSON body:
  ```json
  {
    "device_id": "ESP32_001",
    "events": [
      { "timestamp": 1700000000, "latitude": 43.073100, "longitude": -89.401200, "score": 42 }
    ]
  }
  ```
- Events are only removed from the buffer on HTTP 200/201; failures retain events for retry.
- Authentication: `X-API-Key` header.
- TLS: Set `SERVER_ROOT_CA` in `config.h` to your server's root CA (PEM). For local testing, use `client.setInsecure()` (see comments in `wifi_manager.cpp` and `server_client.cpp`).
