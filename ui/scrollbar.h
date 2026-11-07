#pragma once
#include <cstdint>

#include "../core/scroll.h"
#include "../core/tween.h"
#include "../gfx/canvas.h"

namespace fd {

class ListView;

struct Thumb {
    Rect r;
    fx a = 0;

    bool on() const { return !r.empty() && a > 0; }
    Thumb faded(fx f) const { return {r, fx_mul(a, f)}; }
    bool operator==(const Thumb& o) const
    {
        return r.x0 == o.r.x0 && r.y0 == o.r.y0 && r.x1 == o.r.x1 && r.y1 == o.r.y1 && a == o.a;
    }
    bool operator!=(const Thumb& o) const { return !(*this == o); }
};

class ThumbFade {
public:
    void show(int64_t now);
    void track(bool on, bool moving, int64_t now);
    fx at(int64_t now) const;
    void release(int64_t now);
    bool fading(int64_t now) const;
    int64_t wake_at(int64_t now) const;

private:
    int64_t hold_end() const;

    Tween in_;
    int64_t seen_ = INT64_MIN / 4, drawn_ = 0;
    bool on_ = false, held_ = false;
};

Thumb thumb_of(int top, int bottom, int64_t view, int64_t content, int64_t scrolled);
Thumb thumb_of(const Scroll& s, fx top, fx bottom);
Thumb thumb_of(const ListView& l);
void draw_thumb(Canvas& c, const Thumb& t);
Rect thumb_column(const Thumb& a, const Thumb& b);

}
