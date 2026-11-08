#include "t9.h"

#include <limits>

#include "../core/curves.h"

namespace fd {

namespace {

int step(int a, int b, fx k) { return a + (int)(((int64_t)(b - a) * k) >> kFxBits); }

fx progress(int64_t e, int dur)
{
    if (dur <= 0 || e >= dur)
        return kOne;
    if (e <= 0)
        return 0;
    return (fx)(e * kOne / dur);
}

}

int T9::out_ms() { return fx_round(tok::motion_trackHide); }

int T9::in_ms() { return fx_round(tok::motion_trackShow); }

int T9::out_px() { return std_px(tok::motion_trackShiftOut); }

int T9::in_px() { return std_px(tok::motion_trackShiftIn); }

void T9::set(const T9Text& s)
{
    shown_ = pending_ = s;
    phase_ = Phase::Idle;
}

void T9::advance(int64_t now)
{
    if (phase_ == Phase::Out && now - t0_ >= out_ms()) {
        shown_ = pending_;
        phase_ = Phase::In;
        t0_ += out_ms();
    }
    if (phase_ == Phase::In && now - t0_ >= in_ms())
        phase_ = Phase::Idle;
}

void T9::change(const T9Text& s, int64_t now)
{
    advance(now);
    if (phase_ == Phase::Out) {
        pending_ = s;
        return;
    }
    if (s == shown_)
        return;
    from_ = pose(now);
    pending_ = s;
    phase_ = Phase::Out;
    t0_ = now;
}

void T9::land()
{
    if (phase_ == Phase::Out)
        shown_ = pending_;
    phase_ = Phase::Idle;
}

T9Pose T9::pose(int64_t now) const
{
    int64_t e = now - t0_;
    Phase ph = phase_;
    if (ph == Phase::Out) {
        if (e < out_ms()) {
            fx k = curve(Curve::EaseOut, progress(e, out_ms()));
            return {step(from_.shift, out_px(), k), step(from_.a256, 0, k)};
        }
        e -= out_ms();
        ph = Phase::In;
    }
    if (ph != Phase::In || e >= in_ms())
        return {};
    fx k = curve(Curve::EaseIn, progress(e, in_ms()));
    return {step(in_px(), 0, k), step(0, 256, k)};
}

const T9Text& T9::shown(int64_t now) const
{
    return phase_ == Phase::Out && now - t0_ >= out_ms() ? pending_ : shown_;
}

int64_t T9::end() const
{
    if (phase_ == Phase::Out)
        return t0_ + out_ms() + in_ms();
    if (phase_ == Phase::In)
        return t0_ + in_ms();
    return std::numeric_limits<int64_t>::min();
}

}
