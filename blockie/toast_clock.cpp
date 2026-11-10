#include "toast_clock.h"

#include "../core/curves.h"

namespace fd {

namespace {

int ms(fx v) { return fx_round(v); }

fx progress(int64_t e, int d) { return e >= d || d <= 0 ? kOne : e <= 0 ? 0 : (fx)(e * kOne / d); }

fx exp_out6_inverse(fx e)
{
    fx lo = 0, hi = kOne;
    for (int i = 0; i < 17; i++) {
        fx mid = (lo + hi) / 2;
        if (curve(Curve::ExpOut6, mid) < e)
            lo = mid;
        else
            hi = mid;
    }
    return hi;
}

}

void ToastClock::show(int64_t at, const std::string& line1, const std::string& line2, bool forever)
{
    int p = phase(at);
    forever_ = forever;
    if (on_ && (p == 1 || p == 2)) {
        bool next = swap_ >= 0;
        if (line1 == (next ? next_l1_ : l1_) && line2 == (next ? next_l2_ : l2_)) {
            if (p == 2)
                hold_ = at;
            return;
        }
        if (swapped(at)) {
            l1_ = next_l1_;
            l2_ = next_l2_;
        }
        next_l1_ = line1;
        next_l2_ = line2;
        swap_ = at;
        if (p == 2)
            hold_ = at;
        return;
    }
    on_ = true;
    in_ = at;
    hold_ = at + in_ms_;
    swap_ = -1;
    l1_ = line1;
    l2_ = line2;
}

bool ToastClock::leave(int64_t now)
{
    if (!on_ || forever_ || phase(now) == 0)
        return false;
    fx p = exp_out6_inverse(kOne - eased(now));
    hold_ = now - ms(tok::motion_toastHold) - (int64_t)out_ms_ * p / kOne;
    return true;
}

int ToastClock::phase(int64_t now) const
{
    if (!on_)
        return 0;
    if (now < hold_)
        return 1;
    if (forever_ || now < hold_ + ms(tok::motion_toastHold))
        return 2;
    if (now < end())
        return 3;
    return 0;
}

int64_t ToastClock::end() const { return hold_ + ms(tok::motion_toastHold) + out_ms_; }

bool ToastClock::swapped(int64_t now) const { return swap_ >= 0 && now >= swap_ + ms(tok::motion_valueChange); }

fx ToastClock::eased(int64_t now) const
{
    switch (phase(now)) {
    case 1:
        return curve(Curve::ExpOut6, progress(now - in_, in_ms_));
    case 2:
        return kOne;
    case 3:
        return kOne - curve(Curve::ExpOut6, progress(now - hold_ - ms(tok::motion_toastHold), out_ms_));
    default:
        return 0;
    }
}

fx ToastClock::text(int64_t now) const
{
    if (swap_ < 0)
        return kOne;
    int64_t e = now - swap_;
    int d = ms(tok::motion_valueChange);
    if (e < d)
        return fx_lerp(kOne, tok::motion_valueDip, progress(e, d));
    return fx_lerp(tok::motion_valueDip, kOne, progress(e - d, d));
}

bool ToastClock::animating(int64_t now) const
{
    int p = phase(now);
    return p == 1 || p == 3 || (swap_ >= 0 && now < swap_ + 2 * ms(tok::motion_valueChange));
}

int64_t ToastClock::deadline(int64_t now) const
{
    if (!on_ || forever_)
        return -1;
    int64_t out = hold_ + ms(tok::motion_toastHold);
    if (out > now)
        return out;
    return end() > now ? end() : -1;
}

}
