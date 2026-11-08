#pragma once
#include <cstdint>
#include <vector>

#include "input_pad.h"

namespace fd {

bool is_direction(PadAction a);

enum : uint8_t { kFromPad = 1, kFromKeys = 2 };

class Repeat {
public:
    Repeat();
    Repeat(int64_t delay_ms, int64_t interval_ms) : delay_(delay_ms), interval_(interval_ms) {}

    void press(PadAction dir, uint8_t src, int64_t now);
    void release(PadAction dir, uint8_t src, int64_t now);
    void release_source(uint8_t src, int64_t now);
    void release_all() { held_.clear(); }
    void rearm(int64_t now);
    bool due(int64_t now, PadAction& out);
    uint8_t sources(PadAction dir) const;
    int64_t deadline() const { return held_.empty() ? -1 : next_; }
    bool active() const { return !held_.empty(); }

private:
    struct Hold {
        PadAction dir;
        uint8_t src;
    };
    void drop(size_t i, int64_t now);

    int64_t delay_ = 0;
    int64_t interval_ = 0;
    std::vector<Hold> held_;
    int64_t next_ = 0;
};

}
