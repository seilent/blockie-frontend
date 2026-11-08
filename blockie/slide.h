#pragma once
#include <cstdint>
#include <functional>

#include "../gfx/canvas.h"
#include "fold.h"

namespace fd {

enum class LeftMove { Fade, Cells };

struct SlidePlan {
    int target = 0;
    LeftMove left = LeftMove::Cells;
    LeftFold cells;
};

struct SlidePose {
    int boundary = 0;
    int pane_a256 = 256;
    int left_a256 = 0;
    int text_shift = 0;
    int text_a256 = 0;
};

using SlideText = std::function<void(Canvas&, int shift, int a256)>;

double slide_ease(double x);
SlidePlan slide_plan(int target, LeftMove left, int ring_frames, Rect header);
int64_t slide_ms();
int64_t slide_left_ms(const SlidePlan& p);
int64_t slide_duration(const SlidePlan& p);
SlidePose slide_pose(const SlidePlan& p, int64_t t);
void slide_prepare(SlidePlan& p, const Surface& a, const Surface& b);
Rect slide_compose(const Surface& a, const Surface& b, const Surface& dst, const SlidePlan& p, int64_t t, int64_t prev,
                   uint32_t bg, const SlideText& text);

}
