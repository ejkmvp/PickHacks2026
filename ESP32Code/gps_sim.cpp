#include "gps_sim.h"

// ─── Simulated path ───────────────────────────────────────────────────────────
// Replace these waypoints with your desired test route.
const GpsPoint GpsSim::_path[] = {
    { 43.0731f, -89.4012f },
    { 43.0733f, -89.4015f },
    { 43.0736f, -89.4019f },
    { 43.0739f, -89.4022f },
    { 43.0742f, -89.4025f },
    { 43.0745f, -89.4028f },
    { 43.0748f, -89.4031f },
    { 43.0751f, -89.4034f },
};
const int GpsSim::_pathLen = sizeof(GpsSim::_path) / sizeof(GpsSim::_path[0]);

GpsSim gpsSim;

void GpsSim::begin() {
    pinMode(GPS_SIM_BUTTON_PIN, INPUT_PULLUP);
}

void GpsSim::update() {
    // Detect falling edge (button press, active LOW)
    bool btn = digitalRead(GPS_SIM_BUTTON_PIN);
    if (_lastBtn == HIGH && btn == LOW) {
        if (!_active) {
            _active      = true;
            _idx         = 0;
            _lastAdvance = millis();
            Serial.println("[GPS] Simulation started");
        } else {
            _active = false;
            Serial.println("[GPS] Simulation stopped");
        }
    }
    _lastBtn = btn;

    if (!_active) return;

    // Advance to the next waypoint on the interval
    if (millis() - _lastAdvance >= GPS_SIM_INTERVAL_MS) {
        _lastAdvance = millis();
        if (_idx < _pathLen - 1) {
            _idx++;
            Serial.printf("[GPS] Waypoint %d: %.6f, %.6f\n",
                          _idx, _path[_idx].latitude, _path[_idx].longitude);
        } else {
            // Reached the end of the path
            _active = false;
            Serial.println("[GPS] Simulation complete");
        }
    }
}
