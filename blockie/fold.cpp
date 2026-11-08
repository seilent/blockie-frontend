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
constexpr uint32_t kLeftSeed = 4;

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

int64_t ring_unit(const FoldPlan& p) { return p.ring_unit > 0 ? p.ring_unit : kRingUnit; }

constexpr int kEdge = kPaneLeftEdge;
constexpr int kLine = kLeftLine;

Rect cell_rect(const FoldCell& c, int h)
{
    int x1 = c.x + kCell >= kLeft ? kEdge : c.x + kCell;
    return {c.x, c.y, x1, std::min(h, c.y + kCell)};
}

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

FoldPlan fold_left(int ring_frames)
{
    FoldPlan p;
    p.ring_unit = (int64_t)std::max(1, ring_frames) * 1000;
    double sx[kGridRows], sy[kGridRows];
    for (int r = 0; r < kGridRows; r++) {
        Rect tr = grid_tile_rect(r * kGridCols, kGridCols);
        sx[r] = kLeft;
        sy[r] = (tr.y0 + tr.y1) / 2.0;
    }
    uint32_t rnd = kLeftSeed;
    for (int x = 0; x < kLeftCols * kCell; x += kCell)
        for (int y = 0; y < kLeftRows * kCell; y += kCell) {
            FoldCell c = make_cell(x, y, sx, sy, kGridRows, rnd);
            p.maxring = std::max(p.maxring, c.ring);
            p.cells.push_back(c);
        }
    return p;
}

LeftFold left_fold(int ring_frames, Rect header)
{
    LeftFold lf;
    lf.plan = fold_left(ring_frames);
    Rect left{0, 0, kEdge, kPortraitH};
    lf.header = header.intersect(left);
    for (int k = 0; k < kGridTiles; k++)
        lf.slots.push_back(grid_tile_rect(k, kGridCols).grow(kTileFocusBorder).intersect(left));
    return lf;
}

void left_prepare(LeftFold& lf, const Surface& a, const Surface& b)
{
    int h = std::min(a.h, b.h);
    std::vector<uint8_t> now(lf.slots.size(), 0);
    for (size_t k = 0; k < lf.slots.size(); k++) {
        Rect r = lf.slots[k];
        for (int y = r.y0; y < r.y1 && y < h && !now[k]; y++)
            if (!std::equal(a.row(y) + r.x0, a.row(y) + r.x1, b.row(y) + r.x0))
                now[k] = 1;
    }
    bool first = !lf.prepared;
    if (first) {
        lf.changed = now;
        lf.prepared = true;
    }
    std::vector<uint8_t> live(lf.slots.size(), 0);
    for (size_t k = 0; k < lf.slots.size(); k++)
        live[k] = lf.changed[k] && now[k];
    lf.own.assign((size_t)kEdge * h, 0);
    for (size_t k = 0; k < lf.slots.size(); k++) {
        if (live[k])
            continue;
        Rect r = lf.slots[k];
        for (int y = r.y0; y < r.y1 && y < h; y++) {
            auto row = lf.own.begin() + (ptrdiff_t)y * kEdge;
            std::fill(row + r.x0, row + r.x1, (uint8_t)1);
        }
    }
    for (int y = lf.header.y0; y < lf.header.y1 && y < h; y++)
        for (int x = lf.header.x0; x < lf.header.x1; x++)
            if (!lf.own[(size_t)y * kEdge + x])
                lf.own[(size_t)y * kEdge + x] = 2;
    size_t n = lf.plan.cells.size();
    lf.folds.assign(n, 0);
    lf.lines.assign(n * kLine, 0);
    for (size_t ci = 0; ci < n; ci++) {
        const FoldCell& c = lf.plan.cells[ci];
        Rect r = cell_rect(c, h);
        bool touch0 = false, touch = false;
        for (size_t k = 0; k < lf.slots.size(); k++)
            if (!r.intersect(lf.slots[k]).empty()) {
                touch0 = touch0 || lf.changed[k];
                touch = touch || live[k];
            }
        if (first && touch0)
            lf.top_ring = std::max(lf.top_ring, c.ring);
        if (!touch)
            continue;
        lf.folds[ci] = 1;
        uint8_t* ln = &lf.lines[ci * kLine];
        bool rows = c.edge == FoldEdge::Right || c.edge == FoldEdge::Left;
        for (int y = r.y0; y < r.y1; y++) {
            const uint8_t* o = &lf.own[(size_t)y * kEdge];
            const uint32_t* as = a.row(y);
            const uint32_t* bs = b.row(y);
            for (int x = r.x0; x < r.x1; x++)
                if (!o[x] && as[x] != bs[x])
                    ln[rows ? y - r.y0 : x - r.x0] = 1;
        }
    }
}

int64_t left_span(const LeftFold& lf)
{
    if (lf.top_ring < 0)
        return 0;
    return ((lf.top_ring + 1) * ring_unit(lf.plan) + kFoldFps - 1) / kFoldFps;
}

void left_compose(const Surface& a, const Surface& b, const Surface& dst, const LeftFold& lf, int64_t t_ms, int head)
{
    int h = std::min(dst.h, (int)(lf.own.size() / kEdge));
    for (int y = 0; y < h; y++) {
        const uint8_t* o = &lf.own[(size_t)y * kEdge];
        const uint32_t* as = a.row(y);
        const uint32_t* bs = b.row(y);
        uint32_t* d = dst.row(y);
        for (int x = 0; x < kEdge; x++) {
            if (o[x] == 1)
                d[x] = bs[x];
            else if (o[x] == 2)
                d[x] = head <= 0 ? as[x] : head >= 256 ? bs[x] : blend_px(as[x], bs[x], head);
        }
    }
    int64_t t = std::max<int64_t>(0, t_ms) * kFoldFps;
    int64_t unit = ring_unit(lf.plan);
    uint32_t tmp[kLine * kCell];
    for (size_t ci = 0; ci < lf.plan.cells.size(); ci++) {
        const FoldCell& c = lf.plan.cells[ci];
        Rect r = cell_rect(c, h);
        if (r.empty())
            continue;
        int cw = r.x1 - r.x0, ch = r.y1 - r.y0;
        int64_t local = t - c.ring * unit;
        const Surface* whole = nullptr;
        if (!lf.folds[ci] || local >= unit)
            whole = &b;
        else if (local <= 0)
            whole = &a;
        if (whole) {
            for (int y = r.y0; y < r.y1; y++) {
                const uint8_t* o = &lf.own[(size_t)y * kEdge];
                const uint32_t* s = whole->row(y);
                uint32_t* d = dst.row(y);
                for (int x = r.x0; x < r.x1; x++)
                    if (!o[x])
                        d[x] = s[x];
            }
            continue;
        }
        for (int y = 0; y < ch; y++)
            copy_px(tmp + y * cw, b.row(r.y0 + y) + r.x0, cw);
        fx scale = kOne - (fx)(local * kOne / unit);
        const uint8_t* ln = &lf.lines[ci * kLine];
        if (c.edge == FoldEdge::Right || c.edge == FoldEdge::Left) {
            int nw = std::max(1, fx_round(cw * scale));
            int x0 = c.edge == FoldEdge::Right ? cw - nw : 0;
            for (int y = 0; y < ch; y++) {
                if (!ln[y])
                    continue;
                const uint32_t* s = a.row(r.y0 + y) + r.x0;
                uint32_t* d = tmp + y * cw + x0;
                for (int i = 0; i < nw; i++) {
                    int p0 = i * cw / nw, p1 = std::max(p0 + 1, (i + 1) * cw / nw);
                    d[i] = mean_px(s + p0, p1 - p0, 1);
                }
            }
        } else {
            int nh = std::max(1, fx_round(ch * scale));
            int y0 = c.edge == FoldEdge::Down ? ch - nh : 0;
            for (int x = 0; x < cw; x++) {
                if (!ln[x])
                    continue;
                for (int i = 0; i < nh; i++) {
                    int p0 = i * ch / nh, p1 = std::max(p0 + 1, (i + 1) * ch / nh);
                    tmp[(y0 + i) * cw + x] = mean_px(&a.at(r.x0 + x, r.y0 + p0), p1 - p0, (size_t)a.stride);
                }
            }
        }
        for (int y = r.y0; y < r.y1; y++) {
            const uint8_t* o = &lf.own[(size_t)y * kEdge];
            const uint32_t* s = tmp + (y - r.y0) * cw - r.x0;
            uint32_t* d = dst.row(y);
            for (int x = r.x0; x < r.x1; x++)
                if (!o[x])
                    d[x] = s[x];
        }
    }
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

int64_t fold_duration(const FoldPlan& p) { return ((p.maxring + 1) * ring_unit(p) + kFoldFps - 1) / kFoldFps; }

void fold_compose(const Surface& from, const Surface& to, const Surface& dst, const FoldPlan& p, int64_t t_ms)
{
    int w = dst.w, h = dst.h;
    int64_t t = std::max<int64_t>(0, t_ms) * kFoldFps;
    int64_t unit = ring_unit(p);
    for (const FoldCell& c : p.cells) {
        Rect r{c.x, c.y, std::min(w, c.x + kCell), std::min(h, c.y + kCell)};
        if (r.empty())
            continue;
        int64_t local = t - c.ring * unit;
        if (local <= 0) {
            copy_cell(from, dst, r);
        } else if (local >= unit) {
            copy_cell(to, dst, r);
        } else {
            copy_cell(to, dst, r);
            fold_cell(from, dst, r, kOne - (fx)(local * kOne / unit), c.edge);
        }
    }
}

}
