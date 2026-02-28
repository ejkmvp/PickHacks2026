#pragma once
#include "event_buffer.h"

// Periodically sends all buffered pothole events to the server over HTTPS.
// Events are only removed from the buffer after the server responds with 2xx.
// If the send fails, they remain in the buffer and are retried next interval.
class ServerClient {
public:
    void update(EventBuffer& buffer);

private:
    bool     sendBatch(EventBuffer& buffer);
    uint32_t _lastAttempt = 0;
};

extern ServerClient serverClient;
