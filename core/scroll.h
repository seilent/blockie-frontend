#pragma once
#include <cstdint>

#include "curves.h"

namespace fd {

using pos64 = int64_t;

constexpr pos64 pos_u(fx u) { return (pos64)u; }
constexpr pos64 pos_int(int64_t u) { return u << kFxBits; }

class Velocity {
public:
    void clear() { n_ = 0; }
    void add(int64_t ms, fx y);
    int64_t per_s() const;

private:
    struct S {
        int64_t ms;
        fx y;
    };
    static constexpr int kCap = 32;
    S s_[kCap];
    int n_ = 0;
};

enum class ScrollKind : uint8_t { List, Quickplay, Grid };

class Scroll {
public:
    explicit Scroll(ScrollKind k) : kind_(k) {}

    ScrollKind kind() const { return kind_; }
    void set_limit(pos64 limit);
    pos64 limit() const { return limit_; }
    pos64 base() const { return base_; }
    pos64 pos() const { return pos_; }
    pos64 pos_in() const { return pos_ < 0 ? 0 : pos_ > limit_ ? limit_ : pos_; }
    void set(pos64 p);
    void fit(pos64 limit, pos64 lo, pos64 hi);

    void begin(fx finger);
    void drag(fx finger);
    void end_drag();
    void release(int64_t now, int64_t v_finger);
    void animate(int64_t now, pos64 to, int ms, Curve c);
    void lift(int64_t now, pos64 by, int ms, Curve c);
    void drop(int64_t now, int ms, Curve c);
    bool step(int64_t now);
    void stop();
    bool grab(int64_t at);
    bool land();

    bool flinging() const { return fling_; }
    bool animating() const { return anim_; }
    bool returning() const { return back_; }
    bool moving() const { return fling_ || anim_ || back_; }
    int64_t velocity() const { return v_; }
    pos64 target() const { return anim_ ? to_ : back_ ? end() : pos_; }

private:
    bool write(pos64 p);
    bool settle();
    bool ends() const;
    pos64 end() const { return pos_ < 0 ? 0 : limit_; }

    ScrollKind kind_;
    bool back_ = false, held_ = false;
    pos64 pos_ = 0, limit_ = 0, base_ = 0, reach_ = 0, rest_ = 0, fit_at_ = -1;
    bool dropping_ = false;
    pos64 start_pos_ = 0;
    fx start_finger_ = 0;
    bool fling_ = false, anim_ = false;
    int64_t v_ = 0, last_ = 0;
    pos64 from_ = 0, to_ = 0;
    int64_t anim_start_ = 0;
    int anim_ms_ = 0;
    Curve curve_ = Curve::Linear;
};

int64_t fling_velocity(int64_t v_finger);
pos64 fling_travel(int64_t v0, ScrollKind k);

}
