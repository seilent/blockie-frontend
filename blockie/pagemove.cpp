#include "pagemove.h"

#include <algorithm>
#include <array>
#include <cstdlib>

#include "../core/curves.h"
#include "../gfx/warp.h"

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

int shift_px(fx c, fx ratio_h) { return fx_round(fx_mul(fx_mul(c, ratio_h), fx_int(kPortraitH))); }

}

void PageMove::push(int64_t now, int focus, int count)
{
    on_ = true;
    pop_ = false;
    focus_ = focus;
    stagger_ = 0;
    int span = std::max(0, std::max(focus, count - 1 - focus));
    start_ = now;
    split_ = now + ms(tok::motion_homeOut) + ms(tok::motion_homeOutStagger) * span;
    end_ = split_ + ms(tok::motion_turnstileIn);
}

void PageMove::pop(int64_t now, int count) { enter_at(now, now + ms(tok::motion_turnstileOut), count, 0); }

void PageMove::enter(int64_t now, int count, int stagger_ms) { enter_at(now, now, count, stagger_ms); }

void PageMove::enter_at(int64_t now, int64_t split, int count, int stagger_ms)
{
    on_ = true;
    pop_ = true;
    focus_ = 0;
    stagger_ = stagger_ms;
    start_ = now;
    split_ = split;
    int items = ms(tok::motion_homeEnter) + stagger_ * std::max(0, count - 1);
    end_ = split_ + std::max(items, ms(tok::motion_homeEnterPeek));
}

PagePhase PageMove::phase(int64_t now) const
{
    if (!on_ || now >= end_)
        return PagePhase::Idle;
    if (now < split_)
        return pop_ ? PagePhase::PageExit : PagePhase::HomeExit;
    return pop_ ? PagePhase::HomeEnter : PagePhase::PageEnter;
}

ItemPose PageMove::item(int64_t now, int i) const
{
    ItemPose p;
    switch (phase(now)) {
    case PagePhase::Idle:
        break;
    case PagePhase::HomeExit: {
        int64_t delay = (int64_t)std::abs(i - focus_) * ms(tok::motion_homeOutStagger);
        fx c = curve(Curve::ExpOut6, progress(now - start_ - delay, ms(tok::motion_homeOut)));
        p.a256 = alpha256(kOne - c);
        p.dy = shift_px(c, tok::motion_homeOutShift);
        break;
    }
    case PagePhase::PageEnter:
    case PagePhase::PageExit:
        p.a256 = 0;
        break;
    case PagePhase::HomeEnter: {
        int64_t delay = (int64_t)i * stagger_;
        fx e = curve(Curve::Decel, progress(now - split_ - delay, ms(tok::motion_homeEnter)));
        p.a256 = alpha256(e);
        p.dx = fx_round(fx_mul(std_to_px(tok::motion_homeEnterShift), kOne - e));
        break;
    }
    }
    return p;
}

int PageMove::grid_a256(int64_t now) const
{
    switch (phase(now)) {
    case PagePhase::Idle:
        return 256;
    case PagePhase::HomeExit:
        return alpha256(kOne - curve(Curve::ExpOut6, progress(now - start_, ms(tok::motion_homeOut))));
    case PagePhase::PageEnter:
    case PagePhase::PageExit:
        return 0;
    case PagePhase::HomeEnter:
        return alpha256(curve(Curve::Decel, progress(now - split_, ms(tok::motion_homeEnterPeek))));
    }
    return 256;
}

int PageMove::bar_a256(int64_t now) const
{
    PagePhase ph = phase(now);
    if (ph == PagePhase::HomeExit)
        return item(now, focus_).a256;
    if (ph == PagePhase::HomeEnter)
        return item(now, 0).a256;
    return ph == PagePhase::Idle ? 256 : 0;
}

fx PageMove::page_angle(int64_t now) const
{
    switch (phase(now)) {
    case PagePhase::Idle:
        return 0;
    case PagePhase::PageEnter:
        return fx_lerp(tok::motion_turnstileInAngle, 0,
                       curve(Curve::ExpOut6, progress(now - split_, ms(tok::motion_turnstileIn))));
    case PagePhase::PageExit:
        return fx_lerp(0, tok::motion_turnstileInAngle,
                       curve(Curve::ExpIn6, progress(now - start_, ms(tok::motion_turnstileOut))));
    case PagePhase::HomeExit:
    case PagePhase::HomeEnter:
        return tok::motion_turnstileInAngle;
    }
    return 0;
}

int PageMove::page_a256(int64_t now) const
{
    switch (phase(now)) {
    case PagePhase::Idle:
        return 256;
    case PagePhase::PageEnter: {
        int64_t e = now - split_;
        int fade = ms(tok::motion_turnstileInFade);
        return e >= fade ? 256 : (int)(e * 256 / fade);
    }
    case PagePhase::PageExit:
        return now - start_ < ms(tok::motion_turnstileOutHold) ? 256 : 0;
    case PagePhase::HomeExit:
    case PagePhase::HomeEnter:
        return 0;
    }
    return 256;
}

fx PageMove::header_fly(int64_t now) const
{
    if (!pop_ || split_ == start_ || phase(now) == PagePhase::Idle)
        return 0;
    return curve(Curve::ExpOut6, progress(now - start_, ms(tok::motion_headerOut)));
}

int PageMove::header_dy(int64_t now) const { return shift_px(header_fly(now), tok::motion_headerOutShift); }

int PageMove::header_a256(int64_t now) const { return alpha256(kOne - header_fly(now)); }

int turnstile_d_px() { return fx_round(fx_mul(tok::motion_perspectiveRatio, fx_int(kPortraitW))); }

void draw_turnstile(const Surface& page, const Surface& dst, fx angle_deg, int a256, uint32_t bg)
{
    std::array<WarpRow, kPortraitW> rows;
    turnstile_rows(angle_deg, 0, turnstile_d_px(), rows.data());
    warp(page, dst, rows.data(), bg, a256);
}

}
