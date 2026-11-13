#pragma once
#include <cstdint>
#include <functional>

#include <memory>

#include "../gfx/canvas.h"
#include "engine.h"
#include "leftmove.h"

namespace fd {

struct SlidePlan {
    int target = 0;
    LeftMove left = LeftMove::T5;
    LeftPlan moves;
    bool lang_on = false;
    int64_t pane_stop = 0;
    std::shared_ptr<eng::Instance> pane;
    std::shared_ptr<eng::Frame> pane_frame;
};

struct PaneSurface {
    std::shared_ptr<eng::Instance> pane;
    std::shared_ptr<eng::Frame> frame;
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
int64_t grow_ms(GrowClock c);
fx grow_at(GrowClock c, int64_t t);
SlidePlan slide_plan(int target, LeftMove left, Rect header);
PaneSurface pane_surface();
void slide_lang(SlidePlan& p, const PaneSurface& s);
int pane_rest(const SlidePlan& p, int64_t ft, bool reversed, bool settled);
void slide_advance(const SlidePlan& p, int64_t t0, int64_t now);
int64_t slide_ms();
int64_t slide_left_ms(const SlidePlan& p);
int64_t slide_duration(const SlidePlan& p);
SlidePose slide_pose(const SlidePlan& p, int64_t t);
void slide_prepare(SlidePlan& p, const Surface& a, const Surface& b);
void pane_frame_at(const SlidePlan& p, int64_t u, int w, int h, eng::Frame& f);
Rect slide_compose(const Surface& a, const Surface& b, const Surface& dst, const SlidePlan& p, int64_t t, int64_t prev,
                   uint32_t bg, const SlideText& text);

}
