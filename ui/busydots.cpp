#include "busydots.h"

#include <cmath>

#include "accent.h"
#include "roles.h"

namespace fd {

namespace {

constexpr fx kDotSize = 2 << kFxBits;

}

double busy_dot_at(int64_t ms)
{
    auto in = [](double t) { return (std::exp(t) - 1) / (std::exp(1.0) - 1); };
    if (ms < 500)
        return 0.1 + 33.0 * (1 - in(1 - ms / 500.0));
    if (ms < 2000)
        return 33.1 + 33.0 * (ms - 500) / 1500.0;
    return 66.1 + 34.0 * in((ms - 2000) / 500.0);
}

int busy_dot_x(Rect bar, int dot, int64_t t)
{
    int64_t lt = t % kBusyLoopMs - dot * kBusyStaggerMs;
    if (lt < 0 || lt >= kBusyEndMs)
        return -1;
    return bar.x0 + (int)(busy_dot_at(lt) * (bar.x1 - bar.x0) / 100.0);
}

void busy_surface(Canvas& c, Rect r)
{
    c.set_clip(r);
    page_ground(c, kScreen);
}

void busy_dots(Canvas& c, Rect bar, uint32_t accent, int64_t t)
{
    c.set_clip(bar);
    int dot = px(kDotSize);
    for (int i = 0; i < kBusyDots; i++) {
        int x = busy_dot_x(bar, i, t);
        if (x >= 0)
            c.fill({x, bar.y0, x + dot, bar.y0 + dot}, accent);
    }
}

}
