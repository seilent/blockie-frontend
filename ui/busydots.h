#pragma once
#include <cstdint>

#include "../gfx/canvas.h"

namespace fd {

constexpr int kBusyDots = 5;
constexpr int64_t kBusyLoopMs = 4400, kBusyStaggerMs = 200, kBusyEndMs = 2500, kBusyFrameMs = 33;

double busy_dot_at(int64_t ms);
int busy_dot_x(Rect bar, int dot, int64_t t);
void busy_surface(Canvas& c, Rect r);
void busy_dots(Canvas& c, Rect bar, uint32_t accent, int64_t t);

}
