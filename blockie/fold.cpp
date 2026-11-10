#include "fold.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "tile_grid.h"

namespace fd {

namespace {

constexpr int kCell = kFoldCell;
constexpr int kFoldFrames = 5;
constexpr int kFoldFps = 60;
constexpr int64_t kRingUnit = kFoldFrames * 1000;
constexpr double kJitter = 1.5;
constexpr int kLeft = 432;
constexpr int kRowPitch = 96;
constexpr int kPageShiftX = -108;
constexpr int kSeeds = 8;
constexpr uint32_t kPageSeed = 3;

struct SeedU {
    int x, y;
};

constexpr SeedU kPageSeeds[kSeeds] = {{302, 84}, {336, 68}, {372, 94}, {408, 72},
                                      {444, 90}, {330, 104}, {400, 58}, {460, 110}};
constexpr int kLeftSeeds = 4;

uint32_t next_rand(uint32_t& s)
{
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

double jitter(uint32_t& s) { return ((next_rand(s) >> 8) / 16777216.0 * 2 - 1) * kJitter; }

FoldCell make_cell(int x, int y, const double* sx, const double* sy, int n, uint32_t& rnd)
{
    double cx = x + kCell / 2.0, cy = y + kCell / 2.0;
    int best = 0;
    double bestd = 1e18;
    for (int k = 0; k < n; k++) {
        double d = std::hypot(cx - sx[k], cy - sy[k]);
        if (d < bestd) {
            bestd = d;
            best = k;
        }
    }
    double dx = cx - sx[best], dy = cy - sy[best];
    FoldCell c;
    c.x = x;
    c.y = y;
    if (std::fabs(dx) >= std::fabs(dy))
        c.edge = dx > 0 ? FoldEdge::Right : FoldEdge::Left;
    else
        c.edge = dy > 0 ? FoldEdge::Down : FoldEdge::Up;
    c.ring = (int)(std::max(0.0, bestd + jitter(rnd) * kCell) / kCell);
    return c;
}

uint32_t mean_px(const uint32_t* p, int n, size_t step)
{
    if (n == 1)
        return *p;
    uint32_t r = 0, g = 0, b = 0;
    for (int k = 0; k < n; k++, p += step) {
        r += *p >> 16 & 0xFF;
        g += *p >> 8 & 0xFF;
        b += *p & 0xFF;
    }
    uint32_t h = (uint32_t)n / 2;
    return (r + h) / n << 16 | (g + h) / n << 8 | (b + h) / n;
}

void copy_cell(const Surface& src, const Surface& dst, Rect r)
{
    for (int y = r.y0; y < r.y1; y++)
        copy_px(dst.row(y) + r.x0, src.row(y) + r.x0, r.x1 - r.x0);
}

void fold_cell(const Surface& src, const Surface& dst, Rect r, fx scale, FoldEdge e)
{
    int cw = r.x1 - r.x0, ch = r.y1 - r.y0;
    if (e == FoldEdge::Right || e == FoldEdge::Left) {
        int nw = std::max(1, fx_round(cw * scale));
        int x0 = e == FoldEdge::Right ? r.x1 - nw : r.x0;
        for (int y = r.y0; y < r.y1; y++) {
            const uint32_t* s = src.row(y) + r.x0;
            uint32_t* d = dst.row(y) + x0;
            for (int i = 0; i < nw; i++) {
                int a = i * cw / nw, b = std::max(a + 1, (i + 1) * cw / nw);
                d[i] = mean_px(s + a, b - a, 1);
            }
        }
        return;
    }
    int nh = std::max(1, fx_round(ch * scale));
    int y0 = e == FoldEdge::Down ? r.y1 - nh : r.y0;
    for (int i = 0; i < nh; i++) {
        int a = i * ch / nh, b = std::max(a + 1, (i + 1) * ch / nh);
        uint32_t* d = dst.row(y0 + i);
        for (int x = r.x0; x < r.x1; x++)
            d[x] = mean_px(&src.at(x, r.y0 + a), b - a, (size_t)src.stride);
    }
}

constexpr int kEdge = kPaneLeftEdge;

}

FoldPlan fold_page(int v)
{
    FoldPlan p;
    double sx[kSeeds], sy[kSeeds];
    for (int k = 0; k < kSeeds; k++) {
        sx[k] = std_px(fx_int(kPageSeeds[k].x)) + kPageShiftX;
        sy[k] = std_px(fx_int(kPageSeeds[k].y)) + (v - 1) * kRowPitch;
    }
    double lx[kLeftSeeds], ly[kLeftSeeds];
    for (int k = 0; k < kLeftSeeds; k++) {
        Rect r = grid_tile_rect(k, kGridCols);
        lx[k] = (r.x0 + r.x1) / 2.0;
        ly[k] = (r.y0 + r.y1) / 2.0;
    }
    uint32_t rnd = kPageSeed;
    for (int x = 0; x < kPortraitW; x += kCell)
        for (int y = 0; y < kPortraitH; y += kCell) {
            FoldCell c = x < kLeft ? make_cell(x, y, lx, ly, kLeftSeeds, rnd) : make_cell(x, y, sx, sy, kSeeds, rnd);
            p.maxring = std::max(p.maxring, c.ring);
            p.cells.push_back(c);
        }
    return p;
}

FoldPlan fold_page_row(int cy)
{
    FoldPlan p;
    double mx = 0, my = 0;
    for (const SeedU& s : kPageSeeds) {
        mx += std_px(fx_int(s.x));
        my += std_px(fx_int(s.y));
    }
    mx /= kSeeds;
    my /= kSeeds;
    double sx[kSeeds], sy[kSeeds];
    for (int k = 0; k < kSeeds; k++) {
        sx[k] = std_px(fx_int(kPageSeeds[k].x)) - mx + kPortraitW / 2.0;
        sy[k] = std_px(fx_int(kPageSeeds[k].y)) - my + cy;
    }
    uint32_t rnd = kPageSeed;
    for (int x = 0; x < kPortraitW; x += kCell)
        for (int y = 0; y < kPortraitH; y += kCell) {
            FoldCell c = make_cell(x, y, sx, sy, kSeeds, rnd);
            p.maxring = std::max(p.maxring, c.ring);
            p.cells.push_back(c);
        }
    return p;
}

void left_fade(const Surface& a, const Surface& b, const Surface& dst, int to_a256)
{
    Rect r{0, 0, std::min(kEdge, dst.w), dst.h};
    if (to_a256 >= 256) {
        copy_cell(b, dst, r);
        return;
    }
    if (to_a256 <= 0) {
        copy_cell(a, dst, r);
        return;
    }
    for (int y = r.y0; y < r.y1; y++) {
        const uint32_t* as = a.row(y);
        const uint32_t* bs = b.row(y);
        uint32_t* d = dst.row(y);
        for (int x = r.x0; x < r.x1; x++)
            d[x] = blend_px(as[x], bs[x], to_a256);
    }
}

int64_t fold_duration(const FoldPlan& p) { return ((p.maxring + 1) * kRingUnit + kFoldFps - 1) / kFoldFps; }

void fold_compose(const Surface& from, const Surface& to, const Surface& dst, const FoldPlan& p, int64_t t_ms)
{
    int w = dst.w, h = dst.h;
    int64_t t = std::max<int64_t>(0, t_ms) * kFoldFps;
    for (const FoldCell& c : p.cells) {
        Rect r{c.x, c.y, std::min(w, c.x + kCell), std::min(h, c.y + kCell)};
        if (r.empty())
            continue;
        int64_t local = t - c.ring * kRingUnit;
        if (local <= 0) {
            copy_cell(from, dst, r);
        } else if (local >= kRingUnit) {
            copy_cell(to, dst, r);
        } else {
            copy_cell(to, dst, r);
            fold_cell(from, dst, r, kOne - (fx)(local * kOne / kRingUnit), c.edge);
        }
    }
}

}
