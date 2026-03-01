#include "server_client.h"
#include "config.h"
#include "wifi_manager.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

ServerClient serverClient;

void ServerClient::update(EventBuffer& buffer) {
    if (!wifiManager.connected())  return;
    if (!wifiManager.timeSynced()) return;
    if (buffer.empty())            return;
    if (millis() - _lastAttempt < REPORT_INTERVAL_MS) return;

    _lastAttempt = millis();
    sendBatch(buffer);
}

bool ServerClient::sendBatch(EventBuffer& buffer) {
    // Snapshot all buffered events without removing them yet.
    // They are only removed after the server acknowledges receipt.
    PotholeEvent snapshot[BUFFER_SIZE];
    int count = buffer.peekAll(snapshot, BUFFER_SIZE);
    if (count == 0) return true;

    // ── Build JSON payload ────────────────────────────────────────────────────
    // Requires ArduinoJson v7. For v6 replace JsonDocument with DynamicJsonDocument(2048).
    JsonDocument doc;
    doc["device_id"] = DEVICE_ID;
    JsonArray events = doc["events"].to<JsonArray>();

    for (int i = 0; i < count; i++) {
        JsonObject obj     = events.add<JsonObject>();
        obj["timestamp"]   = snapshot[i].timestamp;
        obj["latitude"]    = serialized(String(snapshot[i].latitude,  6));
        obj["longitude"]   = serialized(String(snapshot[i].longitude, 6));
        obj["score"]       = snapshot[i].score;
    }

    String payload;
    serializeJson(doc, payload);

    // ── HTTPS POST ────────────────────────────────────────────────────────────
    WiFiClientSecure client;
    //client.setCACert(SERVER_ROOT_CA); TODO maybe try to get the CERT working
    // For local testing without a valid cert, comment the line above and uncomment:
     client.setInsecure();

    HTTPClient https;
    String url = String(SERVER_BASE_URL) + SERVER_REPORT_ENDPOINT;

    if (!https.begin(client, url)) {
        Serial.println("[Server] Failed to begin HTTPS");
        return false;
    }

    https.addHeader("Content-Type", "application/json");
    https.addHeader("X-API-Key",    API_KEY);

    int code = https.POST(payload);
    https.end();

    if (code == 200 || code == 201) {
        // Server acknowledged — safe to remove the events we just sent
        buffer.popN(count);
        Serial.printf("[Server] Sent %d events (HTTP %d). Buffer remaining: %d\n",
                      count, code, buffer.count());
        return true;
    }

    Serial.printf("[Server] Send failed (HTTP %d) — %d events kept in buffer\n",
                  code, count);
    return false;
}
