#include "glide.h"

namespace fd {

void Glide::set(int v)
{
    from_ = to_ = v;
    t0_ = kIdle;
    dur_ = 0;
}

void Glide::to(int target, int64_t now, int dur_ms, Curve c)
{
    if (target == to_)
        return;
    from_ = at(now);
    to_ = target;
    t0_ = now;
    dur_ = dur_ms;
    curve_ = c;
}

int Glide::at(int64_t now) const
{
    int64_t e = now - t0_;
    if (dur_ <= 0 || e >= dur_ || e < 0)
        return to_;
    fx k = curve(curve_, (fx)(e * kOne / dur_));
    return from_ + (int)(((int64_t)(to_ - from_) * k) >> kFxBits);
}

}
