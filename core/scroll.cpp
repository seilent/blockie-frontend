#include "scroll.h"

#include <algorithm>

namespace fd {

namespace {

constexpr int kTick = (int)(tok::motion_flingTick >> kFxBits);
constexpr int kMaxStep = (int)(tok::motion_flingMaxStep >> kFxBits);
constexpr int kWindow = (int)(tok::motion_flingWindow >> kFxBits);
constexpr int kQ = 24;

constexpr double cln(double x)
{
    double y = (x - 1) / (x + 1), y2 = y * y, term = y, sum = 0;
    for (int i = 1; i < 80; i += 2) {
        sum += term / i;
        term *= y2;
    }
    return 2 * sum;
}

constexpr double cexp(double x)
{
    double r = x / 1024, term = 1, sum = 1;
    for (int i = 1; i < 14; i++) {
        term *= r / i;
        sum += term;
    }
    for (int i = 0; i < 10; i++)
        sum *= sum;
    return sum;
}

constexpr double kLnR = cln((double)tok::motion_flingRetention / kOne);
constexpr double kDecay = 1000.0 / kTick * -kLnR;

struct Tables {
    int64_t keep[kMaxStep + 1];
    int64_t travel[kMaxStep + 1];
};

constexpr Tables make()
{
    Tables t{};
    for (int dt = 0; dt <= kMaxStep; dt++) {
        double f = cexp(kLnR * dt / kTick);
        t.keep[dt] = (int64_t)(f * (1 << kQ) + 0.5);
        t.travel[dt] = (int64_t)((1 - f) / kDecay * (1 << kQ) + 0.5);
    }
    return t;
}

constexpr Tables kTab = make();
constexpr int64_t kDecayQ = (int64_t)(kDecay * (1 << kQ) + 0.5);

int64_t abs64(int64_t v) { return v < 0 ? -v : v; }

int64_t decay(int64_t v, int64_t dt)
{
    for (; dt > 0 && v; dt -= kMaxStep)
        v = (v * kTab.keep[std::min<int64_t>(dt, kMaxStep)]) >> kQ;
    return v;
}

}

void Velocity::add(int64_t ms, fx y)
{
    int keep = 0;
    int old = -1;
    for (int i = 0; i < n_; i++)
        if (s_[i].ms < ms - kWindow)
            old = i;
    for (int i = std::max(old, 0); i < n_; i++)
        s_[keep++] = s_[i];
    n_ = keep;
    if (n_ == kCap) {
        for (int i = 1; i < n_; i++)
            s_[i - 1] = s_[i];
        n_--;
    }
    s_[n_++] = {ms, y};
}

int64_t Velocity::per_s() const
{
    if (n_ < 2)
        return 0;
    const S& r = s_[n_ - 1];
    int64_t at = r.ms - kWindow;
    int64_t at_ms = s_[0].ms;
    int64_t at_y = s_[0].y;
    if (at_ms < at) {
        for (int i = 0; i + 1 < n_; i++) {
            const S& a = s_[i];
            const S& b = s_[i + 1];
            if (a.ms <= at && b.ms >= at) {
                at_y = b.ms > a.ms ? a.y + ((int64_t)b.y - a.y) * (at - a.ms) / (b.ms - a.ms) : a.y;
                at_ms = at;
                break;
            }
        }
    }
    int64_t dt = r.ms - at_ms;
    if (dt <= 0)
        return 0;
    return ((int64_t)r.y - at_y) * 1000 / dt;
}

int64_t fling_velocity(int64_t v)
{
    if (abs64(v) < std_u(tok::flingMin))
        return 0;
    v = std::clamp<int64_t>(v, -(int64_t)std_u(tok::flingCap), std_u(tok::flingCap));
    return -v;
}

pos64 fling_travel(int64_t v0) { return (v0 << kQ) / kDecayQ; }

void Scroll::set_limit(pos64 limit)
{
    base_ = std::max<pos64>(0, limit);
    limit_ = std::max(base_, reach_);
    if (pos_ > limit_)
        pos_ = limit_;
}

void Scroll::lift(int64_t now, pos64 by, int ms, Curve c)
{
    dropping_ = false;
    reach_ = target() + by;
    set_limit(base_);
    animate(now, reach_, ms, c);
}

void Scroll::drop(int64_t now, int ms, Curve c)
{
    if (!reach_)
        return;
    if (std::max(pos_, target()) <= base_) {
        reach_ = 0;
        set_limit(base_);
        return;
    }
    dropping_ = true;
    animate(now, std::min(target(), base_), ms, c);
    if (!anim_)
        settle();
}

bool Scroll::settle()
{
    if (!dropping_)
        return false;
    pos64 was = pos_;
    dropping_ = false;
    reach_ = 0;
    set_limit(base_);
    return pos_ != was;
}

bool Scroll::write(pos64 p)
{
    pos64 c = std::clamp<pos64>(p, 0, limit_);
    pos_ = c;
    return c == p;
}

void Scroll::set(pos64 p)
{
    stop();
    settle();
    write(p);
}

void Scroll::fit(pos64 limit, pos64 lo, pos64 hi)
{
    pos64 at = target();
    if (at != fit_at_)
        rest_ = at;
    set_limit(limit);
    set(std::min(std::max(rest_, lo), hi));
    fit_at_ = pos_;
}

void Scroll::stop()
{
    fling_ = false;
    anim_ = false;
    v_ = 0;
}

bool Scroll::grab(int64_t at)
{
    int64_t v = abs64(v_), min = std_u(tok::flingMin);
    bool fast = fling_ && (at < last_ ? v >= decay(min, last_ - at) : decay(v, at - last_) >= min);
    if (anim_)
        pos_ = to_;
    stop();
    settle();
    return fast;
}

void Scroll::begin(fx finger)
{
    stop();
    settle();
    fit_at_ = -1;
    start_pos_ = pos_;
    start_finger_ = finger;
}

void Scroll::drag(fx finger) { write(start_pos_ - ((pos64)finger - start_finger_)); }

void Scroll::release(int64_t now, int64_t v_finger)
{
    stop();
    fit_at_ = -1;
    int64_t v = fling_velocity(v_finger);
    if (abs64(v) < std_u(tok::flingStop))
        return;
    v_ = v;
    last_ = now;
    fling_ = true;
}

void Scroll::animate(int64_t now, pos64 to, int ms, Curve c)
{
    stop();
    fit_at_ = -1;
    to = std::clamp<pos64>(to, 0, limit_);
    if (to == pos_ || ms <= 0) {
        pos_ = to;
        return;
    }
    from_ = pos_;
    to_ = to;
    anim_start_ = now;
    anim_ms_ = ms;
    curve_ = c;
    anim_ = true;
}

bool Scroll::step(int64_t now)
{
    if (anim_) {
        int64_t e = now - anim_start_;
        if (e >= anim_ms_) {
            pos_ = to_;
            anim_ = false;
            settle();
        } else if (e > 0) {
            fx t = (fx)(e * kOne / anim_ms_);
            pos_ = from_ + (((to_ - from_) * curve(curve_, t)) >> kFxBits);
        }
        return true;
    }
    if (!fling_)
        return settle();
    int dt = (int)std::clamp<int64_t>(now - last_, 0, kMaxStep);
    last_ = now;
    if (dt == 0)
        return true;
    bool inside = write(pos_ + ((v_ * kTab.travel[dt]) >> kQ));
    v_ = (v_ * kTab.keep[dt]) >> kQ;
    if (!inside || abs64(v_) < std_u(tok::flingStop))
        stop();
    return true;
}

}
