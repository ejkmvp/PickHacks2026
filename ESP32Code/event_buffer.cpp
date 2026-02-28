#include "event_buffer.h"
#include <algorithm>

bool EventBuffer::push(const PotholeEvent& event) {
    if (_count >= BUFFER_SIZE) return false;
    _buf[_tail] = event;
    _tail = (_tail + 1) % BUFFER_SIZE;
    _count++;
    return true;
}

int EventBuffer::peekAll(PotholeEvent* out, int max) const {
    int n = std::min(_count, max);
    for (int i = 0; i < n; i++) {
        out[i] = _buf[(_head + i) % BUFFER_SIZE];
    }
    return n;
}

void EventBuffer::popN(int n) {
    n = std::min(n, _count);
    _head  = (_head + n) % BUFFER_SIZE;
    _count -= n;
}
