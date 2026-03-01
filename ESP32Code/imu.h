#pragma once
#include <Arduino.h>
#include "event_buffer.h"

class ImuSensor {
public:
    // Returns false if the BNO086 could not be initialised.
    bool begin();

    // Call every loop(). Appends a PotholeEvent to `buffer` when a pothole
    // is confirmed. `lat` and `lon` are the current GPS coordinates to embed.
    void update(EventBuffer& buffer, float lat, float lon);

private:
    void    setupReports();
    bool    isTilted() const;
    uint8_t computeScore(float peakDown, float peakUp) const;

    // Gravity vector in sensor body frame (from SH2_GRAVITY report, m/s²)
    float _gravX = 0.0f, _gravY = 0.0f, _gravZ = -9.81f;

    // ── Pothole detection state machine ──────────────────────────────────────
    enum class DetectState { IDLE, DETECTING, COOLDOWN };
    DetectState _dState = DetectState::IDLE;

    uint32_t _eventStart    = 0;
    uint32_t _cooldownStart = 0;

    float    _peakDown = 0.0f;   // max downward vertical accel (positive, m/s²)
    float    _peakUp   = 0.0f;   // max upward vertical accel magnitude (positive, m/s²)

    // Freefall tracking
    bool     _inFreefall    = false;
    uint32_t _freefallStart = 0;

    // LED indicator: millis() value at which to turn the LED off (0 = off)
    uint32_t _ledOffAt = 0;
};

extern ImuSensor imu;
