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
    _bno.enableLinearAccelerometer(IMU_ACCEL_INTERVAL_MS);
    _bno.enableGravity(IMU_GRAVITY_INTERVAL_MS);
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

    while (_bno.getSensorEvent()) {

        switch (_bno.getSensorEventID()) {

            // ── Update gravity vector (used for tilt and axis projection) ───
            case SH2_GRAVITY:
                _gravX = _bno.getGravityX();
                _gravY = _bno.getGravityY();
                _gravZ = _bno.getGravityZ();
                break;

            // ── Pothole detection ───────────────────────────────────────────
            case SH2_LINEAR_ACCELERATION: {
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
                    _inFreefall = false;
                }

                // ── State machine ─────────────────────────────────────────
                switch (_dState) {

                    case DetectState::IDLE:
                        // Trigger: downward spike above threshold, device upright
                        if (!isTilted() && vertAccel > POTHOLE_NEG_THRESHOLD) {
                            _dState     = DetectState::DETECTING;
                            _eventStart = millis();
                            _peakDown   = vertAccel;
                            _peakUp     = 0.0f;
                            _inFreefall = false;
                        }
                        break;

                    case DetectState::DETECTING: {
                        // Track peaks
                        if (vertAccel  >  _peakDown) _peakDown = vertAccel;
                        if (-vertAccel > _peakUp)    _peakUp   = -vertAccel;

                        // Freefall disqualification: sustained weightlessness
                        // means the device left the ground (jump), not a pothole.
                        if (_inFreefall &&
                            (millis() - _freefallStart >= FREEFALL_DISQUALIFY_MS)) {
                            _dState = DetectState::IDLE;
                            break;
                        }

                        // Confirm: upward impact spike after the initial drop
                        if (_peakUp >= POTHOLE_POS_THRESHOLD) {
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

                            _cooldownStart = millis();
                            _dState        = DetectState::COOLDOWN;
                            break;
                        }

                        // Event window expired — not a pothole pattern
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
