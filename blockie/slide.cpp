#include "slide.h"

#include <algorithm>
#include <cmath>

#include "../core/curves.h"
#include "tile_grid.h"

namespace fd {

namespace {

constexpr int kSlideMs = 400;
constexpr double kX1 = 0.25, kY1 = 0.46, kX2 = 0.45, kY2 = 0.94;

double cbez(double a, double b, double s)
{
    double m = 1 - s;
    return 3 * m * m * s * a + 3 * m * s * s * b + s * s * s;
}

int text_ms() { return fx_round(tok::motion_trackShow); }

int text_from() { return std_px(tok::motion_trackShiftIn); }

int mix(int a, int b, double f) { return a + (int)std::lround((b - a) * f); }

int64_t right_ms(const SlidePlan& p) { return p.mode == SlideMode::System ? kSlideMs + text_ms() : kSlideMs; }

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

SlidePlan slide_plan(SlideMode mode, int target, LeftMove left, int ring_frames, Rect header)
{
    SlidePlan p;
    p.mode = mode;
    p.target = target;
    p.left = left;
    if (left == LeftMove::Cells)
        p.cells = left_fold(ring_frames, header);
    return p;
}

int64_t slide_ms() { return kSlideMs; }

int64_t slide_left_ms(const SlidePlan& p)
{
    if (p.left == LeftMove::Fade)
        return kSlideMs;
    return std::max<int64_t>(kSlideMs, left_span(p.cells));
}

int64_t slide_duration(const SlidePlan& p) { return std::max(right_ms(p), slide_left_ms(p)); }

SlidePose slide_pose(const SlidePlan& p, int64_t t)
{
    SlidePose s;
    double e = slide_ease((double)std::clamp<int64_t>(t, 0, kSlideMs) / kSlideMs);
    s.boundary = mix(kPaneSplit, p.target, e);
    s.pane_a256 = p.mode == SlideMode::System ? mix(256, 0, e) : 256;
    s.left_a256 = mix(0, 256, e);
    if (p.mode != SlideMode::System)
        return s;
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
    if (p.left == LeftMove::Cells)
        left_prepare(p.cells, a, b);
}

Rect slide_compose(const Surface& a, const Surface& b, const Surface& dst, const SlidePlan& p, int64_t t, int64_t prev,
                   uint32_t bg, const SlideText& text)
{
    int w = dst.w, h = dst.h;
    SlidePose s = slide_pose(p, t);
    bool skip_left = settled(t, prev, slide_left_ms(p));
    bool skip_mid = settled(t, prev, kSlideMs);
    bool skip_pane = settled(t, prev, right_ms(p));
    if (!skip_left) {
        if (p.left == LeftMove::Cells)
            left_compose(a, b, dst, p.cells, t, s.left_a256);
        else
            left_fade(a, b, dst, s.left_a256);
    }
    int bx = std::clamp(s.boundary, kPaneSplit, w);
    int shift = bx - kPaneSplit;
    int edge = std::min(w, bx + kTileFocusBorder);
    bool recent = p.mode == SlideMode::Recent;
    uint32_t g = LiveF::pack(bg);
    int x_lo = skip_mid ? kPaneSplitWide : kPaneLeftEdge;
    int x_hi = skip_pane ? std::min(w, kPaneSplitWide) : w;
    int to_end = std::min(recent ? edge : bx, x_hi);
    int g0 = std::max(x_lo, to_end), g1 = std::min(std::max(edge, x_lo), x_hi);
    int r0 = std::max({x_lo, edge, to_end});
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
        if (recent) {
            copy_px(d + r0, as + r0 - shift, x_hi - r0);
            continue;
        }
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
        add(0, kPaneLeftEdge);
    if (!skip_mid)
        add(kPaneLeftEdge, std::min(w, kPaneSplitWide));
    if (!skip_pane)
        add(std::min(w, kPaneSplitWide), w);
    return dmg.x0 < dmg.x1 ? dmg : Rect{0, 0, 0, 0};
}

}
