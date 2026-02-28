#include "wifi_manager.h"
#include "config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <sys/time.h>

WifiManager wifiManager;

static const uint32_t kConnectTimeoutMs = 15000UL;

void WifiManager::begin() {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);   // We handle reconnect manually
    startConnect();
}

void WifiManager::update() {
    switch (_state) {
        case WifiState::DISCONNECTED:
            if (millis() - _lastAttempt >= WIFI_RETRY_INTERVAL_MS) {
                startConnect();
            }
            break;

        case WifiState::CONNECTING:
            if (WiFi.status() == WL_CONNECTED) {
                _state = WifiState::CONNECTED;
                Serial.print("[WiFi] Connected, IP: ");
                Serial.println(WiFi.localIP());
                if (!_timeSynced) syncTime();
            } else if (millis() - _lastAttempt >= kConnectTimeoutMs) {
                WiFi.disconnect(true);
                _state       = WifiState::DISCONNECTED;
                _lastAttempt = millis();
                Serial.println("[WiFi] Connect timed out");
            }
            break;

        case WifiState::CONNECTED:
            if (WiFi.status() != WL_CONNECTED) {
                _state       = WifiState::DISCONNECTED;
                _lastAttempt = millis();
                Serial.println("[WiFi] Connection lost");
            }
            break;
    }
}

bool WifiManager::connected() const {
    return _state == WifiState::CONNECTED && WiFi.status() == WL_CONNECTED;
}

void WifiManager::startConnect() {
    _state       = WifiState::CONNECTING;
    _lastAttempt = millis();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.println("[WiFi] Connecting...");
}

void WifiManager::syncTime() {
    WiFiClientSecure client;
    client.setCACert(SERVER_ROOT_CA);
    // For local testing without a valid cert, comment the line above and uncomment:
    // client.setInsecure();

    HTTPClient https;
    String url = String(SERVER_BASE_URL) + SERVER_TIME_ENDPOINT;

    if (!https.begin(client, url)) {
        Serial.println("[Time] Failed to begin HTTPS");
        return;
    }
    https.addHeader("X-API-Key", API_KEY);

    int code = https.GET();
    if (code == 200) {
        String body = https.getString();
        JsonDocument doc;
        if (deserializeJson(doc, body) == DeserializationError::Ok) {
            uint32_t unixTime = doc["unix"].as<uint32_t>();
            struct timeval tv  = { (time_t)unixTime, 0 };
            settimeofday(&tv, nullptr);
            _timeSynced = true;
            Serial.printf("[Time] Synced to %lu\n", unixTime);
        } else {
            Serial.println("[Time] JSON parse error");
        }
    } else {
        Serial.printf("[Time] HTTP %d\n", code);
    }
    https.end();
}
