#include "config.h"
#include "event_buffer.h"
#include "wifi_manager.h"
#include "imu.h"
#include "gps_sim.h"
#include "server_client.h"

EventBuffer eventBuffer;

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("[Boot] PickHacks2026 Pothole Detector");

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
}
