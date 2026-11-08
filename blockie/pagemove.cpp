#include "pagemove.h"

#include <algorithm>

#include "../core/curves.h"

namespace fd {

namespace {

int ms(fx v) { return fx_round(v); }

fx progress(int64_t e, int dur)
{
    if (dur <= 0 || e >= dur)
        return kOne;
    if (e <= 0)
        return 0;
    return (fx)(e * kOne / dur);
}

}

void PageMove::enter(int64_t now, int count, int stagger_ms)
{
    on_ = true;
    stagger_ = stagger_ms;
    start_ = now;
    int items = ms(tok::motion_homeEnter) + stagger_ * std::max(0, count - 1);
    end_ = start_ + std::max(items, ms(tok::motion_homeEnterPeek));
}

ItemPose PageMove::item(int64_t now, int i) const
{
    ItemPose p;
    if (!running(now))
        return p;
    int64_t delay = (int64_t)i * stagger_;
    fx e = curve(Curve::Decel, progress(now - start_ - delay, ms(tok::motion_homeEnter)));
    p.a256 = alpha256(e);
    p.dx = fx_round(fx_mul(std_to_px(tok::motion_homeEnterShift), kOne - e));
    return p;
}

int PageMove::grid_a256(int64_t now) const
{
    if (!running(now))
        return 256;
    return alpha256(curve(Curve::Decel, progress(now - start_, ms(tok::motion_homeEnterPeek))));
}

int PageMove::bar_a256(int64_t now) const { return item(now, 0).a256; }

}
