#include "fold.h"

#include <algorithm>
#include <cmath>

#include "tile_grid.h"

namespace fd {

namespace {

constexpr int kCell = 48;
constexpr int kFoldFrames = 5;
constexpr int kFoldFps = 60;
constexpr int64_t kRingUnit = kFoldFrames * 1000;
constexpr double kJitter = 1.5;
constexpr int kLeft = 432;
constexpr int kScopeBottom = 420;
constexpr int kRowPitch = 96;
constexpr int kToDetailsShiftX = -108;
constexpr int kToSelectorShiftX = 80;
constexpr int kSeeds = 8;

struct SeedU {
    int x, y;
};

constexpr SeedU kToDetailsSeeds[kSeeds] = {{302, 84}, {336, 68}, {372, 94}, {408, 72},
                                           {444, 90}, {330, 104}, {400, 58}, {460, 110}};
constexpr SeedU kToSelectorSeeds[kSeeds] = {{226, 58}, {236, 84}, {240, 116}, {258, 158},
                                            {230, 100}, {252, 140}, {224, 72}, {264, 176}};
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

void fade_cell(const Surface& from, const Surface& to, const Surface& dst, Rect r, int a)
{
    if (a <= 0) {
        copy_cell(to, dst, r);
        return;
    }
    if (a >= 256) {
        copy_cell(from, dst, r);
        return;
    }
    for (int y = r.y0; y < r.y1; y++) {
        const uint32_t* f = from.row(y);
        const uint32_t* t = to.row(y);
        uint32_t* d = dst.row(y);
        for (int x = r.x0; x < r.x1; x++)
            d[x] = blend_px(t[x], f[x], a);
    }
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

void add_tiles(FoldPlan& p)
{
    Rect o = grid_tile_rect(0);
    std::vector<double> dist;
    for (int k = 0; k < kGridTiles; k++) {
        FoldTile t;
        t.r = grid_tile_rect(k);
        p.tiles.push_back(t);
        dist.push_back(std::hypot((t.r.x0 - o.x0) * 1.0, (t.r.y0 - o.y0) * 1.0));
    }
    std::vector<double> levels = dist;
    std::sort(levels.begin(), levels.end());
    levels.erase(std::unique(levels.begin(), levels.end(), [](double a, double b) { return std::fabs(a - b) < 0.5; }),
                 levels.end());
    for (size_t i = 0; i < p.tiles.size(); i++)
        for (size_t k = 0; k < levels.size(); k++)
            if (std::fabs(dist[i] - levels[k]) < 0.5)
                p.tiles[i].step = (int)k;
    p.tile_steps = (int)levels.size();
}

void page_half(const Surface& src, const Surface& dst, Rect r, int spine, bool right, double open)
{
    int half = right ? r.x1 - spine : spine - r.x0;
    int nw = std::clamp((int)std::lround(half * open), 0, half);
    if (nw <= 0)
        return;
    int s0 = right ? spine : r.x0;
    int d0 = right ? spine : spine - nw;
    for (int y = r.y0; y < r.y1; y++) {
        const uint32_t* s = src.row(y) + s0;
        uint32_t* d = dst.row(y) + d0;
        for (int j = 0; j < nw; j++)
            d[j] = s[(int)((int64_t)j * half / nw)];
    }
}

void book_tile(const Surface& from, const Surface& to, const Surface& dst, Rect r, int64_t local, int64_t unit)
{
    int spine = (r.x0 + r.x1) / 2;
    Rect lhalf{r.x0, r.y0, spine, r.y1};
    Rect rhalf{spine, r.y0, r.x1, r.y1};
    copy_cell(from, dst, lhalf);
    copy_cell(to, dst, rhalf);
    if (local < unit)
        page_half(from, dst, r, spine, true, std::cos(M_PI / 2 * (double)local / unit));
    else
        page_half(to, dst, r, spine, false, std::sin(M_PI / 2 * (double)(local - unit) / unit));
}

int tile_span(int tile_steps) { return tile_steps > 0 ? tile_steps + 1 : 0; }

}

FoldPlan fold_plan(FoldDir dir, int v, int title_bottom_px)
{
    FoldPlan p;
    p.dir = dir;
    bool page = dir == FoldDir::PagePush || dir == FoldDir::PagePop;
    int bottom = std::max(kScopeBottom + std::max(0, v - 1) * kRowPitch, title_bottom_px);
    p.scope = page ? Rect{0, 0, kPortraitW, kPortraitH} : Rect{kLeft, 0, kPortraitW, std::min(kPortraitH, bottom)};
    bool details = dir != FoldDir::ToSelector;
    const SeedU* seeds = details ? kToDetailsSeeds : kToSelectorSeeds;
    double sx[kSeeds], sy[kSeeds];
    for (int k = 0; k < kSeeds; k++) {
        sx[k] = std_px(fx_int(seeds[k].x)) + (details ? kToDetailsShiftX : kToSelectorShiftX);
        sy[k] = std_px(fx_int(seeds[k].y)) + (details ? (v - 1) * kRowPitch : 0);
    }
    double lx[kLeftSeeds], ly[kLeftSeeds];
    for (int k = 0; k < kLeftSeeds; k++) {
        Rect r = grid_tile_rect(k);
        lx[k] = (r.x0 + r.x1) / 2.0;
        ly[k] = (r.y0 + r.y1) / 2.0;
    }
    uint32_t rnd = dir == FoldDir::ToDetails ? 1 : dir == FoldDir::ToSelector ? 2 : 3;
    if (!page)
        add_tiles(p);
    for (int x = page ? 0 : kLeft; x < kPortraitW; x += kCell)
        for (int y = 0; y < kPortraitH; y += kCell) {
            bool left = x < kLeft;
            FoldCell c = left ? make_cell(x, y, lx, ly, kLeftSeeds, rnd) : make_cell(x, y, sx, sy, kSeeds, rnd);
            double cx = x + kCell / 2.0, cy = y + kCell / 2.0;
            c.inside = cx >= p.scope.x0 && cx <= p.scope.x1 && cy >= p.scope.y0 && cy <= p.scope.y1;
            if (c.inside)
                p.maxring = std::max(p.maxring, c.ring);
            p.cells.push_back(c);
        }
    return p;
}

int64_t fold_duration(const FoldPlan& p)
{
    int steps = std::max(p.maxring + 1, tile_span(p.tile_steps));
    return (steps * kRingUnit + kFoldFps - 1) / kFoldFps;
}

void fold_compose(const Surface& from, const Surface& to, const Surface& dst, const FoldPlan& p, int64_t t_ms)
{
    int w = dst.w, h = dst.h;
    bool rev = p.dir == FoldDir::PagePop;
    if (rev)
        t_ms = fold_duration(p) - std::min(t_ms, fold_duration(p));
    int64_t t = std::max<int64_t>(0, t_ms) * kFoldFps;
    int64_t span = std::max(p.maxring + 1, tile_span(p.tile_steps)) * kRingUnit;
    const Surface& a = rev ? to : from;
    const Surface& b = rev ? from : to;
    int fade = (int)std::clamp<int64_t>(256 - t * 256 / span, 0, 256);
    if (!p.tiles.empty()) {
        int64_t tspan = tile_span(p.tile_steps) * kRingUnit;
        int lfade = (int)std::clamp<int64_t>(256 - t * 256 / tspan, 0, 256);
        fade_cell(a, b, dst, Rect{0, 0, std::min(kLeft, w), h}, lfade);
        for (const FoldTile& tl : p.tiles) {
            int64_t local = t - tl.step * kRingUnit;
            if (local <= 0)
                copy_cell(a, dst, tl.r);
            else if (local >= 2 * kRingUnit)
                copy_cell(b, dst, tl.r);
            else
                book_tile(a, b, dst, tl.r, local, kRingUnit);
        }
    }
    for (const FoldCell& c : p.cells) {
        Rect r{c.x, c.y, std::min(w, c.x + kCell), std::min(h, c.y + kCell)};
        if (r.empty())
            continue;
        if (!c.inside) {
            fade_cell(a, b, dst, r, fade);
            continue;
        }
        int64_t local = t - c.ring * kRingUnit;
        if (local <= 0) {
            copy_cell(a, dst, r);
        } else if (local >= kRingUnit) {
            copy_cell(b, dst, r);
        } else {
            copy_cell(b, dst, r);
            fold_cell(a, dst, r, kOne - (fx)(local * kOne / kRingUnit), c.edge);
        }
    }
}

}
