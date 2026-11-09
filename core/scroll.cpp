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

constexpr int kLead = (int)(tok::motion_overscrollLead >> kFxBits);
constexpr pos64 kLand = tok::motion_overscrollLand;
constexpr int64_t kNoTime = INT64_MIN;

struct Tables {
    int64_t keep[kMaxStep + 1];
    int64_t travel[kMaxStep + 1];
    int64_t back[kMaxStep + 1];
    int64_t decay;
};

constexpr Tables make(fx retention, int back_ms)
{
    Tables t{};
    double ln_r = cln((double)retention / kOne), k = 1000.0 / kTick * -ln_r;
    for (int dt = 0; dt <= kMaxStep; dt++) {
        double f = cexp(ln_r * dt / kTick);
        t.keep[dt] = (int64_t)(f * (1 << kQ) + 0.5);
        t.travel[dt] = (int64_t)((1 - f) / k * (1 << kQ) + 0.5);
        t.back[dt] = back_ms > 0 ? (int64_t)(cexp(-(double)dt / back_ms) * (1 << kQ) + 0.5) : 0;
    }
    t.decay = (int64_t)(k * (1 << kQ) + 0.5);
    return t;
}

constexpr Tables kTab[] = {
    make(tok::motion_listFlingRetention, (int)(tok::motion_overscrollBackList >> kFxBits)),
    make(tok::motion_qpFlingRetention, (int)(tok::motion_overscrollBackQp >> kFxBits)),
    make(tok::motion_flingRetention, 0),
};

const Tables& tab(ScrollKind k) { return kTab[(int)k]; }

int64_t abs64(int64_t v) { return v < 0 ? -v : v; }

int64_t decay(const int64_t* keep, int64_t v, int64_t dt)
{
    for (; dt > 0 && v; dt -= kMaxStep)
        v = (v * keep[std::min<int64_t>(dt, kMaxStep)]) >> kQ;
    return v;
}

int64_t stop_of(ScrollKind k) { return std_u(k == ScrollKind::Quickplay ? tok::motion_qpFlingStop : tok::flingStop); }

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

pos64 fling_travel(int64_t v0, ScrollKind k) { return (v0 << kQ) / tab(k).decay; }

bool Scroll::ends() const { return kind_ != ScrollKind::Grid && base_ > 0 && !reach_ && !dropping_; }

void Scroll::set_limit(pos64 limit)
{
    base_ = std::max<pos64>(0, limit);
    limit_ = std::max(base_, reach_);
    pos64 room = (held_ || back_) && ends() ? std_u(tok::motion_overscrollCap) : 0;
    pos_ = std::clamp<pos64>(pos_, -room, limit_ + room);
    if (back_ && pos_ >= 0 && pos_ <= limit_)
        back_ = false;
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
    if (back_)
        pos_ = end();
    back_ = held_ = false;
    fling_ = false;
    anim_ = false;
    v_ = 0;
}

bool Scroll::grab(int64_t at)
{
    const int64_t* keep = tab(kind_).keep;
    int64_t v = abs64(v_), min = std_u(tok::flingMin);
    bool fast = fling_ && (at < last_ ? v >= decay(keep, min, last_ - at) : decay(keep, v, at - last_) >= min);
    if (anim_)
        pos_ = to_;
    stop();
    settle();
    return fast;
}

bool Scroll::land()
{
    if (!anim_ && !back_)
        return false;
    pos64 was = pos_;
    if (anim_)
        pos_ = to_;
    anim_ = false;
    if (back_)
        pos_ = end();
    back_ = false;
    settle();
    return pos_ != was;
}

void Scroll::begin(fx finger)
{
    stop();
    settle();
    fit_at_ = -1;
    start_pos_ = pos_;
    start_finger_ = finger;
    held_ = true;
}

void Scroll::drag(fx finger)
{
    pos64 room = ends() ? std_u(tok::motion_overscrollCap) : 0;
    pos_ = std::clamp<pos64>(start_pos_ - ((pos64)finger - start_finger_), -room, limit_ + room);
    held_ = true;
}

void Scroll::end_drag()
{
    if (!held_)
        return;
    held_ = false;
    if (pos_ < 0 || pos_ > limit_) {
        back_ = true;
        last_ = kNoTime;
    }
}

void Scroll::release(int64_t now, int64_t v_finger)
{
    stop();
    fit_at_ = -1;
    if (pos_ < 0 || pos_ > limit_) {
        back_ = true;
        last_ = now;
        return;
    }
    int64_t v = fling_velocity(v_finger);
    if (abs64(v) < stop_of(kind_))
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
    const Tables& t = tab(kind_);
    if (back_) {
        int64_t dt = last_ == kNoTime ? 0 : std::max<int64_t>(0, now - last_);
        last_ = now;
        pos64 e = end(), d = pos_ - e;
        for (; dt > 0 && abs64(d) >= kLand; dt -= kMaxStep)
            d = (d * t.back[std::min<int64_t>(dt, kMaxStep)]) >> kQ;
        pos_ = abs64(d) < kLand ? e : e + d;
        back_ = pos_ != e;
        return true;
    }
    if (!fling_)
        return settle();
    int dt = (int)std::clamp<int64_t>(now - last_, 0, kMaxStep);
    last_ = now;
    if (dt == 0)
        return true;
    pos64 p = pos_ + ((v_ * t.travel[dt]) >> kQ);
    if ((p < 0 || p > limit_) && ends()) {
        pos64 lead = std::min<pos64>(abs64(v_) * kLead / 1000, std_u(tok::motion_overscrollCap));
        pos_ = p < 0 ? -lead : limit_ + lead;
        fling_ = false;
        v_ = 0;
        back_ = pos_ < 0 || pos_ > limit_;
        return true;
    }
    bool inside = write(p);
    v_ = (v_ * t.keep[dt]) >> kQ;
    if (!inside || abs64(v_) < stop_of(kind_))
        stop();
    return true;
}

}
