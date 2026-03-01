#pragma once
#include <Arduino.h>

enum class WifiState { DISCONNECTED, CONNECTING, CONNECTED };

// Manages WiFi connection non-blocking. On connect, syncs system time
// from the custom server HTTPS endpoint (requires wifiManager.timeSynced()
// to be true before using event timestamps).
class WifiManager {
public:
    void begin();
    void update();

    bool connected()  const;
    bool timeSynced() const { return _timeSynced; }

private:
    void startConnect();
    void syncTime();

    WifiState _state           = WifiState::DISCONNECTED;
    uint32_t  _lastAttempt     = 0;
    bool      _timeSynced      = false;
    uint32_t  _lastSyncAttempt = 0;
};

extern WifiManager wifiManager;
