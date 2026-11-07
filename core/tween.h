#pragma once
#include "curves.h"

namespace fd {

struct Tween {
    int64_t start = 0;
    int delay = 0;
    int dur = 0;
    Curve shape = Curve::Linear;
    fx from = 0;
    fx to = 0;

    void set(fx v)
    {
        from = to = v;
        dur = 0;
        delay = 0;
    }

    void run(int64_t now, fx f, fx t, int ms, Curve c, int delay_ms = 0)
    {
        start = now;
        delay = delay_ms;
        dur = ms;
        shape = c;
        from = f;
        to = t;
    }

    void retarget(int64_t now, fx t, int ms, Curve c) { run(now, at(now), t, ms, c); }

    fx progress(int64_t now) const
    {
        int64_t e = now - start - delay;
        if (dur <= 0 || e >= dur)
            return kOne;
        if (e <= 0)
            return 0;
        return (fx)(e * kOne / dur);
    }

    fx at(int64_t now) const
    {
        fx p = progress(now);
        if (p >= kOne)
            return to;
        return fx_lerp(from, to, curve(shape, p));
    }

    int64_t end() const { return dur <= 0 ? 0 : start + delay + dur; }
    bool done(int64_t now) const { return dur <= 0 || now - start - delay >= dur; }
};

}
