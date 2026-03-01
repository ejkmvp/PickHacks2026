#pragma once

// ─── Device Identity ──────────────────────────────────────────────────────────
#define DEVICE_ID                   "ESP32_001"   // Unique per device

// ─── WiFi ─────────────────────────────────────────────────────────────────────
#define WIFI_SSID                   "TestStation"
#define WIFI_PASSWORD               "esp32password"
#define WIFI_RETRY_INTERVAL_MS      5000UL        // ms between reconnect attempts

// ─── Server ───────────────────────────────────────────────────────────────────
#define SERVER_BASE_URL             "https://192.168.137.1:5000"
#define SERVER_TIME_ENDPOINT        "/api/time"       // GET → { "unix": 1234567890 }
#define SERVER_REPORT_ENDPOINT      "/api/potholes"   // POST → JSON batch
#define API_KEY                     "767aa98087416fc12491a1962c68ef91656249dc00c3646615140faab25a4c8f"

// Root CA certificate for your server in PEM format.
// Obtain it with: openssl s_client -connect your.server.com:443 -showcerts
// For local testing only you may temporarily use client.setInsecure() in server_client.cpp.
#define SERVER_ROOT_CA \
"-----BEGIN CERTIFICATE-----\n" \
"REPLACE_WITH_YOUR_ROOT_CA_CERTIFICATE\n" \
"-----END CERTIFICATE-----\n"

// ─── SPI / BNO086 Pins (VSPI: SCK=18, MISO=19, MOSI=23) ─────────────────────
#define BNO_CS                      5     // Chip Select — change as needed
#define BNO_INT                     22     // Interrupt   — change as needed
#define BNO_RST                     4    
// The WAKE pin is assumed tied on the breakout board; no software control needed.

// ─── GPS Simulation ───────────────────────────────────────────────────────────
#define GPS_SIM_BUTTON_PIN          21     // Change as needed; uses INPUT_PULLUP
#define GPS_SIM_INTERVAL_MS         2000UL

// ─── Pothole Detection ────────────────────────────────────────────────────────
// All accelerations are the vertical component (projected onto gravity vector, m/s²).
// Positive = toward ground, negative = away from ground.

// Downward acceleration that begins an event detection window
#define POTHOLE_NEG_THRESHOLD       3.0f
// Upward acceleration that confirms a pothole impact
#define POTHOLE_POS_THRESHOLD       4.0f
// Acceleration magnitude (average of peaks) that maps to a score of 100
#define POTHOLE_SEVERE_ACCEL        25.0f
// Maximum time (ms) the downward spike may last before the event is cancelled
// (a real pothole impact is brief; a slow push-off or hop lasts much longer)
#define POTHOLE_IMPACT_MAX_MS       200UL
// Maximum total time (ms) from IMPACT start to REBOUND confirmation
#define POTHOLE_MAX_DURATION_MS     400UL
// Minimum time (ms) between consecutive recorded events (prevents double-counting)
#define POTHOLE_COOLDOWN_MS         1500UL
// Acceptable ratio of rebound impulse to impact impulse.
// A pothole returns the sensor to roughly the same level (ratio near 1.0).
// Jumping off a curb produces a large impact with little rebound (ratio << 1).
// Jumping onto a curb produces a small impact with large rebound (ratio >> 1).
#define POTHOLE_IMPULSE_RATIO_MIN   0.5f
#define POTHOLE_IMPULSE_RATIO_MAX   2.0f

// Device tilt beyond this angle (degrees from vertical) disables detection
#define TILT_LOCKOUT_DEGREES        30.0f

// Linear acceleration magnitude (m/s²) below which freefall is assumed.
// Keep this low so normal road vibration (brief low-accel moments) doesn't
// falsely trigger freefall and block pothole detection via FREEFALL_REARM_MS.
#define FREEFALL_THRESHOLD          0.8f
// If freefall persists this long (ms), the event is cancelled (e.g., jumping off a ledge)
#define FREEFALL_DISQUALIFY_MS      120UL
// Time (ms) after freefall ends before a new pothole trigger is allowed.
// Prevents the landing impact from being misread as a pothole.
#define FREEFALL_REARM_MS           150UL

// BNO086 report interval for linear acceleration (ms). Lower = more responsive.
#define IMU_ACCEL_INTERVAL_MS       10    // 100 Hz
// BNO086 report interval for gravity vector (ms). Tilt changes slowly.
#define IMU_GRAVITY_INTERVAL_MS     50    // 20 Hz

// ─── Pothole Indicator LED ────────────────────────────────────────────────────
// GPIO 2 has a built-in LED on most ESP32 dev boards (HIGH = on).
#define LED_PIN                     2
// Flash duration range (ms): score 1 → MIN, score 100 → MAX
#define LED_FLASH_MIN_MS            200UL
#define LED_FLASH_MAX_MS            2000UL

// ─── Event Buffer ─────────────────────────────────────────────────────────────
#define BUFFER_SIZE                 64

// ─── Server Reporting ─────────────────────────────────────────────────────────
#define REPORT_INTERVAL_MS          10000UL   // How often to attempt sending
