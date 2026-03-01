#pragma once
#include <Arduino.h>
#include "config.h"

struct GpsPoint {
    float latitude;
    float longitude;
};

// Simulates GPS by advancing through a hardcoded path on a timer.
// Press the button to start; press again to stop early.
class GpsSim {
public:
    void  begin();
    void  update();

    float latitude()  const { return _active ? _path[_idx].latitude  : 37.9523219f; } //37.952321953778046, -91.77101438903269
    float longitude() const { return _active ? _path[_idx].longitude : -91.771014f; }
    bool  active()    const { return _active; }

private:
    bool     _active      = false;
    int      _idx         = 0;
    uint32_t _lastAdvance = 0;
    bool     _lastBtn     = LOW;   // pulled HIGH; active LOW

    static const GpsPoint _path[];
    static const int      _pathLen;
};

extern GpsSim gpsSim;
