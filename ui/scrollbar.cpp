#include "scrollbar.h"

#include <algorithm>

#include "list.h"
#include "roles.h"
#include "size.h"

namespace fd {

namespace {

int px64(pos64 u) { return (int)((((u * ppu()) >> kFxBits) + kHalf) >> kFxBits); }

constexpr int kHold = tok::scrollbar_hold >> kFxBits;
constexpr int kFade = tok::scrollbar_fade >> kFxBits;

}

void ThumbFade::show(int64_t now)
{
    in_.set(kOne);
    seen_ = now;
    held_ = false;
}

void ThumbFade::track(bool on, bool moving, int64_t now)
{
    if (on && !on_) {
        show(now);
    } else if (moving) {
        if (now >= hold_end())
            in_.run(now, at(now), kOne, kFade, Curve::Pop);
        seen_ = now;
    }
    on_ = on;
    held_ = on && moving;
    drawn_ = now;
}

void ThumbFade::release(int64_t now)
{
    if (held_) {
        held_ = false;
        seen_ = now;
    }
}

int64_t ThumbFade::hold_end() const { return seen_ + kHold; }

fx ThumbFade::at(int64_t now) const
{
    int64_t end = hold_end();
    if (held_ || now < end)
        return in_.at(now);
    int64_t e = now - end;
    if (e >= kFade)
        return 0;
    return fx_lerp(in_.at(end), 0, curve(Curve::Pop, (fx)(e * kOne / kFade)));
}

bool ThumbFade::fading(int64_t now) const
{
    if (!on_)
        return false;
    int64_t end = hold_end();
    return held_ || now < end ? in_.dur > 0 && drawn_ < in_.end() : drawn_ < end + kFade;
}

int64_t ThumbFade::wake_at(int64_t now) const { return on_ && !held_ && now < hold_end() ? hold_end() : -1; }

Thumb thumb_of(int top, int bottom, int64_t view, int64_t content, int64_t scrolled)
{
    int t = bottom - top, m = px(tok::scrollbar_min);
    if (view <= 0 || content <= view || t - m <= px(kOne))
        return {};
    int len = m + (int)(((int64_t)(t - m) * view + content / 2) / content);
    int64_t room = content - view;
    int y = top + (int)(((int64_t)(t - len) * std::clamp<int64_t>(scrolled, 0, room) + room / 2) / room);
    return {{px(canvas_w() - tok::scrollbar_width), y, px(canvas_w()), y + len}, tok::scrollbar_alpha};
}

Thumb thumb_of(const Scroll& s, fx top, fx bottom)
{
    pos64 view = (pos64)bottom - top;
    return thumb_of(px(top), px(bottom), view, s.base() + view, std::min(s.pos(), s.base()));
}

Thumb thumb_of(const ListView& l)
{
    const Scroll& s = l.scroll();
    if (!l.model() || s.base() <= l.collapse())
        return {};
    pos64 off = std::min(s.pos(), s.base()), lift = std::clamp<pos64>(off, 0, l.collapse());
    pos64 top = (pos64)l.top() - lift;
    return thumb_of(px64(top), px64(l.bottom()), (pos64)l.bottom() - top, (pos64)l.model()->count() * l.pitch(),
                    off - lift);
}

void draw_thumb(Canvas& c, const Thumb& t)
{
    if (t.on())
        c.blend(t.r, kWhite, alpha256(t.a));
}

Rect thumb_column(const Thumb& a, const Thumb& b) { return a.r.unite(b.r); }

}
