#pragma once
#include <stdint.h>
#include "config.h"

struct PotholeEvent {
    uint32_t timestamp;   // Unix time (0 if clock not yet synced)
    float    latitude;
    float    longitude;
    uint8_t  score;       // 1–100
};

// Fixed-size FIFO queue. When full, new events are silently dropped.
class EventBuffer {
public:
    EventBuffer() : _head(0), _tail(0), _count(0) {}

    // Add an event. Returns false if buffer is full (event discarded).
    bool push(const PotholeEvent& event);

    // Copy up to `max` events into `out` without removing them.
    // Returns the number of events copied.
    int  peekAll(PotholeEvent* out, int max) const;

    // Remove the oldest `n` events (call after a successful server send).
    void popN(int n);

    int  count() const { return _count; }
    bool full()  const { return _count >= BUFFER_SIZE; }
    bool empty() const { return _count == 0; }

private:
    PotholeEvent _buf[BUFFER_SIZE];
    int _head;   // index of oldest event
    int _tail;   // index of next write slot
    int _count;
};
