#include "imu.h"
#include "config.h"
#include <SparkFun_BNO08x_Arduino_Library.h>
#include <SPI.h>
#include <math.h>
#include <time.h>

// ─── Axis convention ──────────────────────────────────────────────────────────
// The BNO086 is assumed to be mounted with its Z axis pointing vertically.
// "vertAccel" is computed by projecting linear acceleration onto the gravity
// vector, so it works regardless of whether Z is up or down — only the
// vertical axis needs to be roughly aligned with gravity.
//
// vertAccel > 0 : accelerating toward ground  (dropping into pothole)
// vertAccel < 0 : accelerating away from ground (impact / bounce)

static BNO08x _bno;

ImuSensor imu;

bool ImuSensor::begin() {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    SPI.begin();   // VSPI defaults: SCK=18, MISO=19, MOSI=23
    if (!_bno.beginSPI(BNO_CS, BNO_INT, BNO_RST)) {
        Serial.println("[IMU] BNO086 not found — check wiring and pin constants");
        return false;
    }
    setupReports();
    Serial.println("[IMU] BNO086 ready");
    return true;
}

void ImuSensor::setupReports() {
    if (!_bno.enableLinearAccelerometer(IMU_ACCEL_INTERVAL_MS))
        Serial.println("[IMU] enableLinearAccelerometer failed");
    if (!_bno.enableGravity(IMU_GRAVITY_INTERVAL_MS))
        Serial.println("[IMU] enableGravity failed");
}

bool ImuSensor::isTilted() const {
    float gMag = sqrtf(_gravX * _gravX + _gravY * _gravY + _gravZ * _gravZ);
    if (gMag < 1.0f) return false;   // Degenerate vector — assume not tilted

    // cos(45°) ≈ 0.7071; if |gz|/|g| is less than this, tilt > 45°
    static const float kThreshold = cosf(TILT_LOCKOUT_DEGREES * (float)M_PI / 180.0f);
    return (fabsf(_gravZ) / gMag) < kThreshold;
}

uint8_t ImuSensor::computeScore(float peakDown, float peakUp) const {
    // Use the average peak magnitude as the severity metric.
    // Maps linearly from the average detection threshold → score 1
    //                  to POTHOLE_SEVERE_ACCEL              → score 100
    float severity = (peakDown + peakUp) * 0.5f;
    float low      = (POTHOLE_NEG_THRESHOLD + POTHOLE_POS_THRESHOLD) * 0.5f;
    float score    = 1.0f + 99.0f * (severity - low) / (POTHOLE_SEVERE_ACCEL - low);
    int   clamped  = (int)roundf(score);
    if (clamped < 1)   clamped = 1;
    if (clamped > 100) clamped = 100;
    return (uint8_t)clamped;
}

void ImuSensor::update(EventBuffer& buffer, float lat, float lon) {
    if (_bno.wasReset()) setupReports();

    // LED timeout check
    if (_ledOffAt && millis() >= _ledOffAt) {
        digitalWrite(LED_PIN, LOW);
        _ledOffAt = 0;
    }

    while (_bno.getSensorEvent()) {
        uint8_t eventId = _bno.getSensorEventID();
        //Serial.printf("Received event with ID %d\n", eventId);
        switch (eventId) {

            // ── Update gravity vector (used for tilt and axis projection) ───
            case SH2_GRAVITY:
                //Serial.println("Received gravity");
                _gravX = _bno.getGravityX();
                _gravY = _bno.getGravityY();
                _gravZ = _bno.getGravityZ();
                break;

            // ── Pothole detection ───────────────────────────────────────────
            case SH2_LINEAR_ACCELERATION: {
                //Serial.println("Received Lin Accel");
                float ax = _bno.getLinAccelX();
                float ay = _bno.getLinAccelY();
                float az = _bno.getLinAccelZ();
                float linMag = sqrtf(ax * ax + ay * ay + az * az);

                // Project linear acceleration onto the gravity (down) direction.
                float gMag = sqrtf(_gravX * _gravX + _gravY * _gravY + _gravZ * _gravZ);
                float vertAccel = 0.0f;
                if (gMag > 1.0f) {
                    vertAccel = (ax * _gravX + ay * _gravY + az * _gravZ) / gMag;
                }

                // ── Freefall tracking (sustained near-zero linear accel) ───
                if (linMag < FREEFALL_THRESHOLD) {
                    if (!_inFreefall) {
                        _inFreefall    = true;
                        _freefallStart = millis();
                    }
                } else {
                    if (_inFreefall) _freefallEndedAt = millis();
                    _inFreefall = false;
                }

                // ── State machine ─────────────────────────────────────────
                switch (_dState) {

                    // ── Phase 1: wait for a downward spike ────────────────
                    case DetectState::IDLE:
                        // Freefall precondition: block triggers from mid-air or
                        // immediately after landing (same-sample clearing problem).
                        if (!isTilted() && !_inFreefall &&
                            millis() - _freefallEndedAt >= FREEFALL_REARM_MS &&
                            vertAccel > POTHOLE_NEG_THRESHOLD) {
                            _dState      = DetectState::IMPACT;
                            _eventStart  = millis();
                            _peakDown    = vertAccel;
                            _peakUp      = 0.0f;
                            _impactSum   = 0.0f;
                            _reboundSum  = 0.0f;
                        }
                        break;

                    // ── Phase 2: ride out the downward spike ──────────────
                    case DetectState::IMPACT:
                        // Any freefall during the spike means no road contact — cancel
                        if (_inFreefall) { _dState = DetectState::IDLE; break; }

                        if (vertAccel > _peakDown) _peakDown = vertAccel;
                        _impactSum += vertAccel;

                        // Spike has cleared — move to looking for the rebound
                        if (vertAccel < POTHOLE_NEG_THRESHOLD * 0.5f) {
                            _dState     = DetectState::REBOUND;
                            _inFreefall = false;   // fresh freefall window for rebound
                            break;
                        }

                        // Spike lasted too long — hop push-off or other slow motion
                        if (millis() - _eventStart > POTHOLE_IMPACT_MAX_MS) {
                            _dState = DetectState::IDLE;
                        }
                        break;

                    // ── Phase 3: wait for the upward rebound ──────────────
                    case DetectState::REBOUND: {
                        if (-vertAccel > _peakUp) _peakUp = -vertAccel;
                        _reboundSum -= vertAccel;

                        // Sustained freefall during rebound = jump, not pothole
                        if (_inFreefall &&
                            millis() - _freefallStart >= FREEFALL_DISQUALIFY_MS) {
                            _dState = DetectState::IDLE;
                            break;
                        }

                        // Confirm: upward spike reached threshold AND impulse is balanced
                        if (_peakUp >= POTHOLE_POS_THRESHOLD &&
                            _impactSum > 0.0f && _reboundSum > 0.0f &&
                            _reboundSum / _impactSum >= POTHOLE_IMPULSE_RATIO_MIN &&
                            _reboundSum / _impactSum <= POTHOLE_IMPULSE_RATIO_MAX) {
                            PotholeEvent event;
                            event.score     = computeScore(_peakDown, _peakUp);
                            event.latitude  = lat;
                            event.longitude = lon;
                            time_t now;
                            time(&now);
                            event.timestamp = (uint32_t)now;

                            bool stored = buffer.push(event);
                            Serial.printf("[IMU] Pothole detected! Score=%d lat=%.6f lon=%.6f %s\n",
                                          event.score, lat, lon,
                                          stored ? "" : "(buffer full, dropped)");

                            uint32_t flashMs = LED_FLASH_MIN_MS +
                                (uint32_t)((LED_FLASH_MAX_MS - LED_FLASH_MIN_MS) *
                                           (event.score - 1) / 99);
                            digitalWrite(LED_PIN, HIGH);
                            _ledOffAt = millis() + flashMs;

                            _cooldownStart = millis();
                            _dState        = DetectState::COOLDOWN;
                            break;
                        }

                        // Total event budget expired
                        if (millis() - _eventStart > POTHOLE_MAX_DURATION_MS) {
                            _dState = DetectState::IDLE;
                        }
                        break;
                    }

                    case DetectState::COOLDOWN:
                        if (millis() - _cooldownStart >= POTHOLE_COOLDOWN_MS) {
                            _dState = DetectState::IDLE;
                        }
                        break;
                }
                break;
            }

            default:
                break;
        }
    }
}
