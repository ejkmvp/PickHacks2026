#include "config.h"
#include "event_buffer.h"
#include "wifi_manager.h"
#include "imu.h"
#include "gps_sim.h"
#include "server_client.h"
#include <esp_system.h>

EventBuffer eventBuffer;

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("[Boot] PickHacks2026 Pothole Detector");
    Serial.printf("[Boot] Reset reason: %d\n", esp_reset_reason());

    gpsSim.begin();
    wifiManager.begin();

    if (!imu.begin()) {
        Serial.println("[Boot] Fatal: IMU init failed. Check wiring.");
        while (true) delay(1000);
    }
}

void loop() {
    wifiManager.update();
    gpsSim.update();
    imu.update(eventBuffer, gpsSim.latitude(), gpsSim.longitude());
    serverClient.update(eventBuffer);
    Serial.flush();
}
