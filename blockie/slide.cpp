#include "slide.h"

#include <algorithm>
#include <cmath>

#include "../core/curves.h"
#include "fold.h"
#include "leftmove.h"
#include "screen_selector.h"
#include "tile_grid.h"

namespace fd {

namespace {

constexpr int kSlideMs = 400;
constexpr int64_t kFoldMs = kSlideMs + fx_round(tok::motion_trackShow);
static_assert(swap_ms() <= kSlideMs, "the row swap ends inside the slide");
static_assert(kFoldMs == kBlockFoldMs, "the block forms time their collapse against the fold");
static_assert(stepper_max_ms() + kBlockTailMs <= kFoldMs, "the steps and their glow tail end inside the fold");
constexpr int kLiftPx = kPortraitH;
constexpr int kLiftSteps = 3;
constexpr int kMenuCut = 37;
constexpr int kMenuRows = kMenuVisibleRows + 2;
constexpr int kRowWave[kMenuRows] = {0, 0, 0, 1, 1, 1, 2, 2};
static_assert(kMenuCut + kMenuVisibleRows * kMenuPitchPx == 613, "the menu items end where the watermark starts");
constexpr int64_t kRowLeg3 = (int64_t)kLiftSteps * eng::kStepThirds;
constexpr int64_t row_start3(int r) { return (int64_t)kRowWave[r] * eng::kStepThirds; }
constexpr int64_t kPanelStart3 = row_start3(kMenuRows - 1);
constexpr int64_t kPaneThirds = kPanelStart3 + kRowLeg3;
constexpr int64_t kStepRightMs = (kPaneThirds + 2) / 3;
constexpr int64_t lang_fold_for(int64_t left, int64_t right) { return std::max(left, right + kBlockTailMs); }
constexpr int64_t kStepFold = lang_fold_for(block_left_for(stepper_max_ms(), kStepRightMs), kStepRightMs);
static_assert(std::max(stepper_max_ms(), kStepRightMs) + kBlockTailMs <= kStepFold && kStepFold <= kBlockFoldMs,
              "step: the grid and the rows end with their tail inside the fold, inside 800 ms");
constexpr double kX1 = 0.25, kY1 = 0.46, kX2 = 0.45, kY2 = 0.94;

double cbez(double a, double b, double s)
{
    double m = 1 - s;
    return 3 * m * m * s * a + 3 * m * s * s * b + s * s * s;
}

int text_ms() { return fx_round(tok::motion_trackShow); }

int text_from() { return std_px(tok::motion_trackShiftIn); }

int mix(int a, int b, double f) { return a + (int)std::lround((b - a) * f); }

int64_t right_ms() { return kFoldMs; }

int px_of(int px, fx f) { return (int)(((int64_t)px * f + kHalf) >> kFxBits); }

int64_t lang_s(const SlidePlan& p, int64_t t) { return p.moves.collapse ? p.moves.fold - t : t; }

int lang_lift(int64_t s3)
{
    if (s3 <= 0)
        return 0;
    return px_of(kLiftPx / kLiftSteps, eng::step_progress(s3, kLiftSteps));
}

Rect menu_row(int r, int w, int h)
{
    if (r == 0)
        return {kPaneLeftEdge, 0, w, kMenuCut};
    if (r == kMenuRows - 1)
        return {kPaneLeftEdge, kMenuCut + kMenuVisibleRows * kMenuPitchPx, w, h};
    int y0 = kMenuCut + (r - 1) * kMenuPitchPx;
    return {kPaneLeftEdge, y0, w, y0 + kMenuPitchPx};
}

bool settled(int64_t t, int64_t prev, int64_t end) { return prev >= end && t >= end; }

}

double slide_ease(double x)
{
    if (x <= 0)
        return 0;
    if (x >= 1)
        return 1;
    double lo = 0, hi = 1;
    for (int i = 0; i < 48; i++) {
        double mid = (lo + hi) / 2;
        if (cbez(kX1, kX2, mid) < x)
            lo = mid;
        else
            hi = mid;
    }
    return cbez(kY1, kY2, (lo + hi) / 2);
}

int64_t grow_ms(GrowClock c)
{
    switch (c) {
    case GrowClock::G: return fx_round(tok::motion_t3mTile);
    case GrowClock::G6: return fx_round(tok::motion_quickplay);
    case GrowClock::GS: return kSlideMs;
    case GrowClock::None: break;
    }
    return 0;
}

fx grow_at(GrowClock c, int64_t t)
{
    int64_t d = grow_ms(c);
    if (d <= 0 || t >= d)
        return kOne;
    if (t <= 0)
        return 0;
    fx u = (fx)(t * kOne / d);
    switch (c) {
    case GrowClock::G: return cubic_out(u);
    case GrowClock::G6: return curve(Curve::EaseOut, u);
    case GrowClock::GS: return (fx)std::lround(slide_ease((double)t / d) * kOne);
    case GrowClock::None: break;
    }
    return kOne;
}

PaneSurface pane_surface()
{
    PaneSurface s;
    s.pane = std::make_shared<eng::Instance>(Rect{kPaneLeftEdge, 0, kPortraitW, kPortraitH});
    s.frame = std::make_shared<eng::Frame>();
    return s;
}

void slide_lang(SlidePlan& p, const PaneSurface& s)
{
    p.lang_on = true;
    p.left = LeftMove::Stepper;
    p.moves.pane_end = kStepRightMs;
    p.moves.fold = lang_fold_for(block_left_ms(p.moves), kStepRightMs);
    p.pane_stop = kStepRightMs;
    p.pane = s.pane;
    p.pane_frame = s.frame;
}

int pane_rest(const SlidePlan& p, int64_t ft, bool reversed, bool settled)
{
    int64_t d = p.moves.fold, u = std::clamp<int64_t>(lang_s(p, ft), 0, d);
    if (reversed ? u <= 0 : u >= d)
        return settled ? 1 : 0;
    return (reversed ? u >= d : u <= 0) ? -1 : 0;
}

void slide_advance(const SlidePlan& p, int64_t t0, int64_t now)
{
    block_advance(p.moves, t0, now);
    if (!p.pane)
        return;
    if (p.pane->t0() != t0 || p.pane->reversed() != p.moves.reversed)
        p.pane->begin(t0, p.moves.reversed);
    p.pane->advance(t0, now);
}

SlidePlan slide_plan(int target, LeftMove left, Rect header)
{
    SlidePlan p;
    p.target = target;
    p.left = left;
    p.moves.header = header.intersect({0, 0, kPaneLeftEdge, kPortraitH});
    return p;
}

int64_t slide_ms() { return kSlideMs; }

int64_t slide_left_ms(const SlidePlan& p)
{
    if (p.lang_on)
        return block_left_ms(p.moves);
    if (p.left == LeftMove::T5)
        return std::max<int64_t>(kSlideMs, swap_ms());
    return kSlideMs;
}

int64_t slide_duration(const SlidePlan& p) { return p.lang_on ? p.moves.fold : std::max(right_ms(), slide_left_ms(p)); }

SlidePose slide_pose(const SlidePlan& p, int64_t t)
{
    SlidePose s;
    if (p.lang_on) {
        s.boundary = p.target;
        s.pane_a256 = 256;
        s.left_a256 = 256;
        return s;
    }
    double e = slide_ease((double)std::clamp<int64_t>(t, 0, kSlideMs) / kSlideMs);
    s.boundary = mix(kPaneSplit, p.target, e);
    s.pane_a256 = mix(256, 0, e);
    s.left_a256 = mix(0, 256, e);
    s.text_shift = text_from();
    if (t < kSlideMs)
        return s;
    int64_t u = std::min<int64_t>(t - kSlideMs, text_ms());
    fx k = curve(Curve::EaseIn, (fx)(u * kOne / text_ms()));
    s.text_shift = text_from() - (int)(((int64_t)text_from() * k) >> kFxBits);
    s.text_a256 = (int)((256 * (int64_t)k) >> kFxBits);
    return s;
}

void slide_prepare(SlidePlan& p, const Surface& a, const Surface& b)
{
    if (p.left == LeftMove::T5)
        swap_prepare(p.moves, a, b);
}

void pane_frame_at(const SlidePlan& p, int64_t u, int w, int h, eng::Frame& f)
{
    f.poses.clear();
    Rect menu{kPaneLeftEdge, 0, w, h}, title{kPaneSplitWide + kTileFocusBorder, 0, w, h};
    Rect clip{kPaneLeftEdge, 0, w, h};
    bool back = p.moves.collapse;
    eng::Block rest;
    rest.id = 1;
    rest.ink = eng::Ink::Text;
    rest.source = 1;
    rest.src = title;
    eng::Shade sh;
    sh.step = eng::kTextStep;
    eng::Pose qt;
    qt.block = 0;
    qt.rect = title;
    qt.clip = clip;
    f.ground = {};
    f.blocks = {rest};
    f.shades = {sh};
    for (int r = 0; r < kMenuRows; r++) {
        eng::Block row;
        row.id = (uint16_t)(2 + r);
        row.ink = eng::Ink::Text;
        row.z = 2;
        row.src = menu_row(r, w, h);
        f.blocks.push_back(row);
        f.shades.push_back(sh);
    }
    int64_t u3 = 3 * u;
    auto lift = [&](int64_t s3) {
        int o = lang_lift(back ? u3 - (kPaneThirds - s3 - kRowLeg3) : u3 - s3);
        return back ? kLiftPx - o : o;
    };
    int po = lift(kPanelStart3);
    if (po < h) {
        f.ground = {menu.x0, 0, menu.x1, h - po};
        qt.clip.y0 = h - po;
    }
    f.poses.push_back(qt);
    for (int r = 0; r < kMenuRows; r++) {
        const Rect& s = f.blocks[(size_t)r + 1].src;
        eng::Pose q;
        q.block = r + 1;
        q.clip = clip;
        int ro = lift(row_start3(r));
        q.rect = {s.x0, s.y0 - ro, s.x1, s.y1 - ro};
        f.poses.push_back(q);
    }
}

Rect lang_compose(const Surface& a, const Surface& b, const Surface& dst, const SlidePlan& p, int64_t t, int64_t prev,
                  uint32_t bg)
{
    int w = dst.w, h = dst.h;
    int64_t u = lang_s(p, t), pu = prev < 0 ? -1 : lang_s(p, prev);
    int bx = std::clamp(slide_pose(p, t).boundary, kPaneSplit, w);
    int left_end = std::max(bx, kPaneLeftEdge);
    uint32_t g = LiveF::pack(bg);
    bool pane_done = !p.pane || (pu >= p.pane_stop && u >= p.pane_stop && p.pane->settled());
    bool left_done = !p.moves.collapse && settled(u, pu, slide_left_ms(p)) && block_settled(p.moves);
    if (left_done)
        block_skip(p.moves);
    if (!pane_done) {
        if (left_done)
            p.pane->invalidate();
        eng::Frame own, f0;
        eng::Frame& f = p.pane_frame ? *p.pane_frame : own;
        pane_frame_at(p, u, w, h, f);
        if (!p.pane->inited()) {
            int64_t c0 = p.pane->clock_us ? p.pane->clock_us() : 0;
            pane_frame_at(p, 0, w, h, f0);
            if (p.pane->clock_us)
                p.pane->add_build(p.pane->clock_us() - c0);
        }
        p.pane->compose(dst, f, {a, b}, g, w, p.pane->inited() ? nullptr : &f0);
    } else if (p.pane) {
        p.pane->skip();
        p.pane->invalidate();
    }
    if (!left_done && p.pane) {
        p.moves.cover = &p.pane->cover();
        p.moves.cover_rect = p.pane->rect();
        block_compose(a, b, dst, p.moves, t, block_time(p.moves, t), bx, left_end, g);
        p.moves.cover = nullptr;
    } else if (!left_done) {
        block_compose(a, b, dst, p.moves, t, block_time(p.moves, t), bx, left_end, g);
    }
    if (pane_done && left_done)
        return Rect{0, 0, 0, 0};
    if (pane_done)
        return Rect{0, 0, left_end, h};
    return Rect{0, 0, w, h};
}

Rect slide_compose(const Surface& a, const Surface& b, const Surface& dst, const SlidePlan& p, int64_t t, int64_t prev,
                   uint32_t bg, const SlideText& text)
{
    if (p.lang_on)
        return lang_compose(a, b, dst, p, t, prev, bg);
    int w = dst.w, h = dst.h;
    SlidePose s = slide_pose(p, t);
    bool skip_left = settled(t, prev, slide_left_ms(p));
    bool skip_mid = settled(t, prev, kSlideMs);
    bool skip_pane = settled(t, prev, right_ms());
    bool grow = p.moves.grow != GrowClock::None;
    int bx = std::clamp(s.boundary, kPaneSplit, w);
    int left_end = std::max(bx, kPaneLeftEdge);
    uint32_t g = LiveF::pack(bg);
    if (!skip_left && !grow) {
        if (p.left == LeftMove::T5)
            swap_compose(a, b, dst, p.moves, p.moves.collapse ? kSlideMs - t : t, left_end, g);
        else
            left_fade(a, b, dst, s.left_a256);
    }
    int shift = bx - kPaneSplit;
    int edge = std::min(w, bx + kTileFocusBorder);
    int x_lo = skip_mid ? kPaneSplitWide : grow ? bx : p.left == LeftMove::T5 ? left_end : kPaneLeftEdge;
    int x_hi = skip_pane ? std::min(w, kPaneSplitWide) : w;
    int to_end = std::min(bx, x_hi);
    int g0 = std::max(x_lo, to_end), g1 = std::min(std::max(edge, x_lo), x_hi);
    int r0 = std::max(x_lo, edge);
    for (int y = 0; y < h && x_lo < x_hi; y++) {
        const uint32_t* bs = b.row(y);
        const uint32_t* as = a.row(y);
        uint32_t* d = dst.row(y);
        if (x_lo < to_end)
            copy_px(d + x_lo, bs + x_lo, to_end - x_lo);
        for (int x = g0; x < g1; x++)
            d[x] = g;
        if (r0 >= x_hi)
            continue;
        for (int x = r0; x < x_hi; x++) {
            uint32_t v = as[x - shift];
            d[x] = s.pane_a256 <= 0 || v == g ? g : s.pane_a256 >= 256 ? v : blend_px(g, v, s.pane_a256);
        }
    }
    if (!skip_pane && s.text_a256 > 0 && text) {
        Canvas c(dst);
        text(c, s.text_shift, s.text_a256);
    }
    Rect dmg{w, 0, 0, h};
    auto add = [&](int x0, int x1) {
        dmg.x0 = std::min(dmg.x0, x0);
        dmg.x1 = std::max(dmg.x1, x1);
    };
    if (!skip_left)
        add(0, left_end);
    if (!skip_mid)
        add(kPaneLeftEdge, std::min(w, kPaneSplitWide));
    if (!skip_pane)
        add(std::min(w, kPaneSplitWide), w);
    return dmg.x0 < dmg.x1 ? dmg : Rect{0, 0, 0, 0};
}

}
