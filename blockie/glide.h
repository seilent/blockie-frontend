#pragma once
#include <cstdint>

#include "../core/curves.h"

namespace fd {

class Glide {
public:
    void set(int v);
    void to(int target, int64_t now, int dur_ms, Curve c);
    int at(int64_t now) const;
    int target() const { return to_; }
    bool running(int64_t now) const { return dur_ > 0 && now >= t0_ && now - t0_ < dur_; }
    void land() { set(to_); }

private:
    static constexpr int64_t kIdle = -1000000000;

    int from_ = 0;
    int to_ = 0;
    int64_t t0_ = kIdle;
    int dur_ = 0;
    Curve curve_ = Curve::Linear;
};

}
