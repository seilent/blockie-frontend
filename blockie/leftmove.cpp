#include "leftmove.h"

#include <algorithm>
#include <vector>

#include "../core/curves.h"
#include "tile_grid.h"

namespace fd {

namespace {

struct Slot {
    Rect own;
    int row = 0;
};

int out_ms() { return fx_round(tok::motion_swapOut); }

int rows_ms() { return fx_round(tok::motion_swapRows); }

int fade_ms() { return fx_round(tok::motion_swapRowFade); }

bool same_rect(const Rect& a, const Rect& b) { return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1; }

bool same_px(const Surface& a, const Surface& b, Rect r)
{
    for (int y = r.y0; y < r.y1; y++)
        if (!std::equal(a.row(y) + r.x0, a.row(y) + r.x1, b.row(y) + r.x0))
            return false;
    return true;
}

Rect own_rect(Rect tile, Rect focus)
{
    Rect g = tile.grow(kTileFocusBorder);
    return same_rect(g, focus) ? g : tile;
}

std::vector<Rect> row_bands(const std::vector<Slot>& slots, const std::vector<uint8_t>& kept)
{
    std::vector<int> ids;
    std::vector<Rect> rows;
    for (size_t i = 0; i < slots.size(); i++) {
        if (kept[i])
            continue;
        size_t r = std::find(ids.begin(), ids.end(), slots[i].row) - ids.begin();
        if (r == ids.size()) {
            ids.push_back(slots[i].row);
            rows.push_back({});
        }
        rows[r] = rows[r].unite(slots[i].own);
    }
    return rows;
}

void free_spans(Rect band, int y, const std::vector<Rect>& kept, std::vector<int>& out, std::vector<int>& tmp)
{
    out.assign({band.x0, band.x1});
    for (const Rect& k : kept) {
        if (y < k.y0 || y >= k.y1)
            continue;
        tmp.clear();
        for (size_t i = 0; i < out.size(); i += 2) {
            if (out[i] < k.x0) {
                tmp.push_back(out[i]);
                tmp.push_back(std::min(out[i + 1], k.x0));
            }
            if (out[i + 1] > k.x1) {
                tmp.push_back(std::max(out[i], k.x1));
                tmp.push_back(out[i + 1]);
            }
        }
        out.swap(tmp);
    }
}

}

fx cubic_out(fx t)
{
    fx m = kOne - t;
    return kOne - fx_mul(fx_mul(m, m), m);
}

SwapLeg swap_leave(int64_t t, int dir)
{
    int out = out_ms();
    if (t <= 0)
        return {0, 256};
    fx s = t >= out ? kOne : curve(Curve::Strip, (fx)(t * kOne / out));
    return {-dir * fx_round(std_to_px(fx_mul(tok::motion_swapShift, s))), alpha256(kOne - s)};
}

SwapLeg swap_arrive(int64_t t, int dir)
{
    int64_t k = t - out_ms();
    int rows = rows_ms(), fade = fade_ms();
    fx m = kOne - cubic_out((fx)(std::clamp<int64_t>(k, 0, rows) * kOne / rows));
    int a = k <= 0 ? 0 : k >= fade ? 256 : alpha256((fx)(k * kOne / fade));
    return {dir * fx_round(std_to_px(fx_mul(tok::motion_swapRowInShift, m))), a};
}

int swap_header(int64_t t, bool collapse)
{
    if (collapse)
        return swap_arrive(t, -1).a256;
    int d = fx_round(tok::motion_leave);
    if (t <= 0)
        return 256;
    if (t >= d)
        return 0;
    return alpha256(kOne - (fx)(t * kOne / d));
}

void swap_prepare(LeftPlan& p, const Surface& a, const Surface& b)
{
    if (p.prepared)
        return;
    p.prepared = true;
    std::vector<Slot> from, to;
    Rect clip = p.band.intersect({0, kBandTop, kPortraitW, kPortraitH});
    auto lay = [&clip](std::vector<Slot>& out, const GridSpec& g, int grid_y, Rect focus) {
        int top = kTileFirstTop + grid_y;
        for (int m = (clip.y0 - top - g.side - kTileFocusBorder) / g.pitch - 1;
             top + m * g.pitch - kTileFocusBorder < clip.y1; m++)
            for (int c = 0; c < g.cols; c++) {
                int x = kGridX + c * (g.side + g.gutter), y = top + m * g.pitch;
                Rect own = own_rect({x, y, x + g.side, y + g.side}, focus).intersect(clip);
                if (!own.empty())
                    out.push_back({own, m});
            }
    };
    if (p.from_set)
        lay(from, p.from_grid, p.from_y, p.from_focus);
    else
        for (int k = 0; k < kGridTiles; k++)
            from.push_back({own_rect(grid_tile_rect(k, kGridCols), p.focus_from), k / kGridCols});
    lay(to, p.grid, p.grid_y, p.focus_to);
    std::vector<uint8_t> kept_from(from.size(), 0), kept_to(to.size(), 0);
    for (size_t i = 0; i < from.size(); i++)
        for (size_t j = 0; j < to.size(); j++)
            if (!kept_to[j] && same_rect(from[i].own, to[j].own) && same_px(a, b, from[i].own)) {
                kept_from[i] = kept_to[j] = 1;
                p.kept.push_back(from[i].own);
            }
    p.leaves = row_bands(from, kept_from);
    p.bands = row_bands(to, kept_to);
}

void swap_compose(const Surface& a, const Surface& b, const Surface& dst, const LeftPlan& p, int64_t t, int x_end,
                  uint32_t ground)
{
    int w = std::min(x_end, dst.w), h = dst.h;
    bool back = p.collapse;
    const Surface& from = back ? b : a;
    const Surface& to = back ? a : b;
    if (back ? t < 0 : t <= 0) {
        for (int y = 0; y < h; y++)
            copy_px(dst.row(y), from.row(y), w);
        return;
    }
    if (t >= swap_ms()) {
        int e = back ? std::min(w, kPaneLeftEdge) : w;
        for (int y = 0; y < h; y++) {
            copy_px(dst.row(y), to.row(y), e);
            std::fill(dst.row(y) + e, dst.row(y) + w, ground);
        }
        return;
    }
    for (int y = 0; y < h; y++)
        std::fill_n(dst.row(y), w, ground);
    Rect left{0, 0, w, h};
    for (const Rect& r : p.kept) {
        Rect k = r.intersect(left);
        for (int y = k.y0; y < k.y1; y++)
            copy_px(dst.row(y) + k.x0, to.row(y) + k.x0, k.x1 - k.x0);
    }
    int head = swap_header(t, back);
    Rect hr = p.header.intersect(left);
    for (int y = hr.y0; y < hr.y1 && head > 0; y++) {
        const uint32_t* s = a.row(y);
        uint32_t* d = dst.row(y);
        for (int x = hr.x0; x < hr.x1; x++)
            d[x] = blend_px(ground, s[x], head);
    }
    bool leaving = t < out_ms();
    int dir = back ? -1 : 1;
    SwapLeg leg = leaving ? swap_leave(t, dir) : swap_arrive(t, dir);
    if (leg.a256 <= 0)
        return;
    const Surface& src = leaving ? from : to;
    const std::vector<Rect>& from_rows = back ? p.bands : p.leaves;
    const std::vector<Rect>& to_rows = back ? p.leaves : p.bands;
    std::vector<int> reads, writes, tmp;
    for (const Rect& band : leaving ? from_rows : to_rows) {
        Rect row{0, band.y0, w, band.y1};
        for (int y = std::max(0, band.y0); y < std::min(h, band.y1); y++) {
            free_spans(band, y, p.kept, reads, tmp);
            free_spans(row, y, p.kept, writes, tmp);
            const uint32_t* s = src.row(y);
            uint32_t* d = dst.row(y);
            for (size_t i = 0; i < writes.size(); i += 2)
                for (size_t j = 0; j < reads.size(); j += 2) {
                    int lo = std::max(writes[i], reads[j] + leg.dx);
                    int hi = std::min(writes[i + 1], reads[j + 1] + leg.dx);
                    if (lo >= hi)
                        continue;
                    if (leg.a256 >= 256)
                        copy_px(d + lo, s + lo - leg.dx, hi - lo);
                    else
                        for (int x = lo; x < hi; x++)
                            d[x] = blend_px(ground, s[x - leg.dx], leg.a256);
                }
        }
    }
}

}
