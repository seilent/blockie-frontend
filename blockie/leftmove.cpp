#include "leftmove.h"

#include <algorithm>
#include <cassert>
#include <climits>
#include <cstddef>
#include <cstring>
#include <vector>

#include "../core/curves.h"
#include "blend_row.h"
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

constexpr int kPitch = kGridRowPitch;
constexpr int kEdgeLo = kGridX - kTileFocusBorder;
constexpr int kEdgeHi = kGridX + kGridColsWide * kGridRowPitch;
constexpr int kSwapShift = kSwapCells * kGridRowPitch;
constexpr int kLastCol = kGridColsWide - 1;

Rect moved(Rect r, int dx, int dy) { return {r.x0 + dx, r.y0 + dy, r.x1 + dx, r.y1 + dy}; }

BlockPose pose_at(int block, Rect rect, Rect clip)
{
    BlockPose q;
    q.block = block;
    q.rect = rect;
    q.clip = clip;
    q.ring = clip;
    return q;
}

int stepped(int64_t t3, int beats)
{
    if (t3 <= 0 || beats <= 0)
        return 0;
    if (t3 >= (int64_t)beats * kStepThirds)
        return beats * kPitch;
    int j = (int)(t3 / kStepThirds);
    fx tau = (fx)((t3 - (int64_t)j * kStepThirds) * kOne / kStepThirds);
    return j * kPitch + fx_round(kPitch * curve(Curve::EaseIn, tau));
}

int track_start(int k)
{
    if (k < kGridTiles)
        return kGridColsWide * (k / kGridCols) + k % kGridCols;
    return kGridColsWide * (kGridRows - 1) + kGridCols + (k - kGridTiles);
}

int find_game(const LeftPlan& p, bool wide, int game)
{
    for (size_t i = 0; i < p.blocks.size(); i++)
        if (p.blocks[i].wide == wide && p.blocks[i].game == game)
            return (int)i;
    return -1;
}

void swap_poses(const LeftPlan& p, int64_t tl, Rect clip, std::vector<BlockPose>& out)
{
    int g = stepped(tl, kSwapCells), g1 = stepped(tl, 1);
    const std::vector<uint8_t>& full = p.full_rows;
    int rows[kGridRows] = {};
    bool art[kGridRows] = {};
    for (const Block& k : p.blocks)
        if (!k.wide && k.row >= 0 && k.row < kGridRows) {
            rows[k.row]++;
            art[k.row] = k.art;
        }
    auto lone = [&](int row) { return !p.collapse && row >= 0 && row < kGridRows && rows[row] == 1 && art[row]; };
    Rect slot = clip;
    slot.x1 = std::min(slot.x1, kPaneLeftEdge);
    for (int pass = 0; pass < 3; pass++)
        for (size_t i = 0; i < p.blocks.size(); i++) {
            const Block& k = p.blocks[i];
            bool held = k.wide && p.collapse && k.col == kLastCol;
            bool under = k.wide && !held && lone(k.row) && k.col == 0;
            if ((held ? 2 : under ? 0 : 1) != pass)
                continue;
            BlockPose q = pose_at((int)i, k.cell, clip);
            if (!k.wide) {
                q.rect = moved(k.cell, p.collapse ? g - kSwapShift : lone(k.row) ? -g1 : -g, 0);
            } else if (held) {
                q.top = true;
            } else if (under) {
                q.beneath = true;
                if (g1 < kPitch)
                    q.ring = Rect{};
            } else {
                q.rect = moved(k.cell, p.collapse ? g : lone(k.row) ? kPitch - g1 : kSwapShift - g, 0);
                if (k.col != kLastCol && (k.row >= (int)full.size() || !full[(size_t)k.row]))
                    q.clip = q.ring = slot;
            }
            out.push_back(q);
        }
}

void deal_poses(const LeftPlan& p, int64_t tl, Rect clip, std::vector<BlockPose>& out)
{
    int beats = block_beats(p);
    for (int pass = 0; pass < 2; pass++)
        for (size_t wi = 0; wi < p.blocks.size(); wi++) {
            if (!p.blocks[wi].wide)
                continue;
            int k = p.blocks[wi].game;
            int small = find_game(p, false, k);
            int src = p.collapse && small >= 0 ? small : (int)wi;
            const Block& s = p.blocks[(size_t)src];
            int start = track_start(k), h = start - k;
            bool kept = h == 0 && small >= 0;
            if (kept != (pass == 1))
                continue;
            int at = p.collapse ? k * kPitch + stepped(tl, h)
                                : start * kPitch - stepped(tl - (int64_t)(beats - h) * kStepThirds, h);
            int i = at / kPitch, off = at % kPitch;
            int c = i % kGridColsWide, r = i / kGridColsWide;
            int x = kGridX + c * kPitch, y = kTileFirstTop + r * kPitch + p.grid_y;
            auto place = [&](int px, int py) {
                BlockPose q = pose_at(src, moved(s.cell, px - s.cell.x0, py - s.cell.y0), clip);
                if (p.collapse && !s.wide && tl > 0)
                    q.ring = Rect{};
                return q;
            };
            if (kept) {
                BlockPose q = place(x, y);
                q.top = true;
                out.push_back(q);
            } else if (off == 0) {
                out.push_back(place(x, y));
            } else if (c == kLastCol) {
                BlockPose q = place(x + off, y);
                q.clip.x1 = std::min(q.clip.x1, kEdgeHi - kTileFocusBorder);
                out.push_back(q);
                out.push_back(place(kGridX - kPitch + off, y + kPitch));
            } else {
                out.push_back(place(x + off, y));
            }
        }
}

struct IdMap {
    uint8_t* px = nullptr;
    Rect r;
    int16_t* lo = nullptr;
    int16_t* hi = nullptr;

    void mark(int y, int x0, int x1, uint8_t v) const
    {
        if (!px || y < r.y0 || y >= r.y1)
            return;
        x0 = std::max(x0, r.x0);
        x1 = std::min(x1, r.x1);
        if (x0 >= x1)
            return;
        size_t row = (size_t)(y - r.y0);
        uint8_t* d = px + row * (size_t)(r.x1 - r.x0);
        std::fill(d + (x0 - r.x0), d + (x1 - r.x0), v);
        lo[row] = (int16_t)std::min<int>(lo[row], x0 - r.x0);
        hi[row] = (int16_t)std::max<int>(hi[row], x1 - r.x0);
    }
};

struct Under {
    const std::vector<std::vector<std::pair<int, int>>>* rows = nullptr;
    int y0 = 0;
    int x1 = 0;

    const std::vector<std::pair<int, int>>* row(int y) const
    {
        if (!rows || y < y0 || y - y0 >= (int)rows->size() || (*rows)[(size_t)(y - y0)].empty())
            return nullptr;
        return &(*rows)[(size_t)(y - y0)];
    }
    template <class F> void open(int y, int u, int v, F f) const
    {
        if (const std::vector<std::pair<int, int>>* r = row(y))
            for (const std::pair<int, int>& c : *r) {
                if (c.first >= v || c.first >= x1)
                    break;
                int c1 = std::min(c.second, x1);
                if (c1 <= u)
                    continue;
                if (c.first > u)
                    f(u, c.first);
                u = c1;
            }
        if (u < v)
            f(u, v);
    }
    template <class F> void shut(int y, int u, int v, F f) const
    {
        if (const std::vector<std::pair<int, int>>* r = row(y))
            for (const std::pair<int, int>& c : *r) {
                if (c.first >= v || c.first >= x1)
                    break;
                int a = std::max(u, c.first), b = std::min({v, c.second, x1});
                if (a < b)
                    f(a, b);
            }
    }
};

struct Draw {
    std::vector<int>& spans;
    std::vector<int>& tmp;
    IdMap ids;
    bool draw = true;
    Under under;
    int64_t* past = nullptr;
    int past_x = 0;
};

void copy_spans(const Surface& src, const Surface& dst, Rect r, int dx, int dy, const std::vector<Rect>& covers,
                Draw& w, uint8_t v)
{
    r = r.intersect(dst.rect());
    if (r.empty())
        return;
    for (int y = r.y0; y < r.y1; y++) {
        free_spans(r, y, covers, w.spans, w.tmp);
        const uint32_t* s = src.row(y - dy);
        uint32_t* d = dst.row(y);
        for (size_t i = 0; i < w.spans.size(); i += 2) {
            if (w.draw)
                w.under.open(y, w.spans[i], w.spans[i + 1], [&](int u, int e) {
                    copy_px(d + u, s + u - dx, e - u);
                    if (w.past)
                        *w.past += std::max(0, e - std::max(u, w.past_x));
                });
            w.ids.mark(y, w.spans[i], w.spans[i + 1], v);
        }
    }
}

void ring_parts(Rect box, Rect tile, Rect* out)
{
    Rect in = tile.intersect(box);
    if (in.empty()) {
        out[0] = box;
        out[1] = out[2] = out[3] = Rect{};
        return;
    }
    out[0] = {box.x0, box.y0, box.x1, in.y0};
    out[1] = {box.x0, in.y1, box.x1, box.y1};
    out[2] = {box.x0, in.y0, in.x0, in.y1};
    out[3] = {in.x1, in.y0, box.x1, in.y1};
}

bool cover_at(const LeftPlan& p, int x, int y)
{
    Rect cr = p.cover_rect;
    if (!p.cover || x < cr.x0 || x >= cr.x1 || y < cr.y0 || y >= cr.y1)
        return false;
    return (*p.cover)[(size_t)(y - cr.y0) * (size_t)(cr.x1 - cr.x0) + (size_t)(x - cr.x0)];
}

Under cover_under(const LeftPlan& p, std::vector<std::vector<std::pair<int, int>>>& rows, int x_end)
{
    Rect cr = p.cover_rect;
    if (!p.cover || cr.empty())
        return Under{};
    int cw = cr.x1 - cr.x0, kw = std::min(cr.x1, std::max(x_end, block_glow_rect(p).x1)) - cr.x0;
    assert(p.cover->size() == (size_t)cw * (size_t)(cr.y1 - cr.y0));
    rows.resize((size_t)(cr.y1 - cr.y0));
    for (int y = cr.y0; y < cr.y1; y++) {
        std::vector<std::pair<int, int>>& v = rows[(size_t)(y - cr.y0)];
        v.clear();
        const uint8_t* c = p.cover->data() + (size_t)(y - cr.y0) * (size_t)cw;
        for (int k = 0; k < kw;) {
            int e = run_end(c, k, kw);
            if (c[k])
                v.push_back({cr.x0 + k, cr.x0 + e});
            k = e;
        }
    }
    return Under{&rows, cr.y0, x_end};
}

bool ring_hidden(const LeftPlan& p, Rect in, int x, int y)
{
    return cover_at(p, std::clamp(x, in.x0, in.x1 - 1), std::clamp(y, in.y0, in.y1 - 1));
}

void ring_spans(const Surface& src, const Surface& dst, const LeftPlan& p, Rect r, Rect in, int dx, int dy,
                const std::vector<Rect>& covers, Draw& w, uint8_t v)
{
    if (!p.cover || in.empty()) {
        copy_spans(src, dst, r, dx, dy, covers, w, v);
        return;
    }
    for (int y = r.y0; y < r.y1; y++)
        for (int x = r.x0; x < r.x1;) {
            bool hide = ring_hidden(p, in, x, y);
            int x1 = x + 1;
            while (x1 < r.x1 && ring_hidden(p, in, x1, y) == hide)
                x1++;
            if (hide)
                glow_of(p).ring_hid.push_back({x, y, x1, y + 1});
            else
                copy_spans(src, dst, {x, y, x1, y + 1}, dx, dy, covers, w, v);
            x = x1;
        }
}

void draw_pose(const Surface& a, const Surface& b, const Surface& dst, const LeftPlan& p, const BlockPose& q,
               const std::vector<Rect>& covers, Draw& w, uint8_t key)
{
    const Block& k = p.blocks[(size_t)q.block];
    const Surface& src = k.wide ? b : a;
    uint8_t ring = (uint8_t)(key | kGlowRing);
    Rect vis = q.rect.intersect(q.clip);
    if (vis.empty())
        return;
    int dx = q.rect.x0 - k.cell.x0, dy = q.rect.y0 - k.cell.y0;
    if (k.art)
        copy_spans(src, dst, vis, dx, dy, covers, w, key);
    if (!k.focus)
        return;
    Rect box = block_box(p, q), parts[4];
    ring_parts(box, q.rect, parts);
    for (const Rect& r : parts)
        ring_spans(src, dst, p, r, q.rect.intersect(box), dx, dy, covers, w, ring);
}

void raster(const Surface& a, const Surface& b, const Surface& dst, const LeftPlan& p,
            const std::vector<BlockPose>& poses, int x_end, uint32_t ground, const IdMap& ids, bool draw = true,
            const Under& under = Under{}, int64_t* past = nullptr)
{
    int w = std::min(x_end, dst.w);
    for (int y = 0; y < dst.h && draw; y++)
        under.open(y, 0, w, [&](int u, int e) { std::fill(dst.row(y) + u, dst.row(y) + e, ground); });
    Glow& gl = glow_of(p);
    gl.covers.clear();
    gl.ring_hid.clear();
    for (const BlockPose& q : poses)
        if (q.top)
            gl.covers.push_back(block_cover(p, q));
    const std::vector<Rect> none;
    Draw d{gl.spans, gl.tmp, ids, draw, under, past, w};
    for (const BlockPose& q : poses)
        draw_pose(a, b, dst, p, q, q.top ? none : gl.covers, d, (uint8_t)(block_key(p, q.block) + 1));
}

struct AlphaLut {
    uint16_t v[257];

    explicit AlphaLut(int sigma)
    {
        for (int a = 0; a <= 256; a++)
            v[a] = (uint16_t)std::min(256, (a * 256 + sigma / 2) / sigma);
    }
};

const uint16_t* alpha_lut(int sigma)
{
    static const AlphaLut full(256), swap(kSwapSigma);
    return sigma == kSwapSigma ? swap.v : sigma == 256 ? full.v : nullptr;
}

uint32_t shade_px(uint32_t src, uint32_t ground, int e, const GlowShade& h)
{
    if (!h.lut)
        return block_shade(src, ground, e, h.sigma);
    if (e >= kGlowFull && h.sigma == 256)
        return src;
    int a = h.lut[(e * 256 + kGlowFull / 2) / kGlowFull];
    return a >= 256 ? src : a <= 0 ? ground : blend_px(ground, src, a);
}

int step_by(int step, int64_t dt3) { return (int)((int64_t)step * 256 * dt3 / 50); }

int ramp(int64_t s3, int64_t from3, bool up)
{
    int64_t d = s3 <= from3 ? 0 : (int64_t)kGlowDimStep * 256 * (s3 - from3) / 50;
    return up ? (int)std::min<int64_t>(kGlowFull, kGlowDim + d) : (int)std::max<int64_t>(kGlowDim, kGlowFull - d);
}

bool departs(const LeftPlan& p, const Block& k, bool flip)
{
    if (p.redeal)
        return false;
    return flip || p.collapse ? k.wide : !k.wide;
}

GlowShade forward_shade(const LeftPlan& p, const Block& k, int64_t s3)
{
    GlowShade h;
    h.sigma = block_sigma(p, k);
    h.lut = alpha_lut(h.sigma);
    h.dep = departs(p, k, false);
    bool col = p.collapse;
    h.step = col ? kGlowStepDim : kGlowStepFull;
    int64_t d3 = block_thirds(p);
    if (h.dep) {
        h.end = kGlowDim;
        h.t = col ? ramp(s3, 0, false) : kGlowDim;
        return h;
    }
    h.end = col ? kGlowDim : kGlowFull;
    if (p.redeal) {
        int lag = track_start(k.game) - k.game;
        int64_t land3 = lag == 0 ? 0 : col ? (int64_t)lag * kStepThirds : d3;
        h.t = ramp(s3, land3, !col);
    } else {
        h.t = h.end;
    }
    return h;
}

GlowShade glow_shade(const LeftPlan& p, int b, int64_t s3)
{
    const Block& k = p.blocks[(size_t)b];
    if (!glow_view(p).flip)
        return forward_shade(p, k, s3);
    GlowShade h;
    h.sigma = block_sigma(p, k);
    h.lut = alpha_lut(h.sigma);
    h.dep = departs(p, k, true);
    h.step = kGlowStepDim;
    const Glow& gl = glow_view(p);
    int back = (size_t)b < gl.back_t.size() ? gl.back_t[(size_t)b] : kGlowDim;
    if (!k.wide || (p.redeal && find_game(p, false, k.game) >= 0)) {
        int lag = p.redeal && k.wide ? track_start(k.game) - k.game : 0;
        int64_t land3 = (int64_t)(block_beats(p) - lag) * kStepThirds;
        int64_t since3 = 3 * std::max<int64_t>(0, (gl.due >= 0 ? gl.due : gl.last) - gl.t0);
        if (lag > 0)
            since3 = std::max<int64_t>(0, since3 - std::max<int64_t>(0, gl.flip_s3 - land3));
        h.end = kGlowDim;
        h.t = std::max(kGlowDim, back - step_by(kGlowDimStep, since3));
    } else {
        h.t = h.end = back;
    }
    return h;
}

bool runs_match(const Glow& gl, int gw, int gh)
{
    for (int r = 0; r < gh; r++) {
        int x = 0;
        size_t row = (size_t)r * gw;
        const uint8_t* wr = gl.ids.data() + row;
        const uint16_t* er = gl.e.data() + row;
        const uint8_t* ar = gl.age.data() + row;
        const uint8_t* hr = gl.hid.empty() ? nullptr : gl.hid.data() + row;
        for (const RowRuns<GlowRun>::Run* g = gl.runs.first(r); g < gl.runs.last(r); g++) {
            if (g->x0 != x)
                return false;
            for (int k = g->x0; k < g->x1; k++)
                if (wr[k] != g->p.was || er[k] != g->p.e || ar[k] != g->p.age || (hr ? hr[k] : 0) != g->p.hid)
                    return false;
            x = g->x1;
        }
        if (x != gw)
            return false;
    }
    return true;
}

void glow_age(Glow& gl, int64_t dt, int gw)
{
    for (size_t r = 0; r < gl.act_lo.size(); r++)
        for (int x = gl.act_lo[r]; x < gl.act_hi[r]; x++) {
            size_t i = r * (size_t)gw + (size_t)x;
            if (gl.ids[i])
                continue;
            int age = (int)std::min<int64_t>(kGlowAgeMs, (gl.age[i] & 0x7F) + dt);
            if (age >= kGlowAgeMs)
                gl.e[i] = 0;
            gl.age[i] = (uint8_t)(age | ((gl.age[i] & 0x80) && age < kGlowAgeMs ? 0x80 : 0));
        }
    size_t gh = gl.act_lo.size();
    if (!gl.runs_ok)
        return;
    if (gl.runs.at.size() != gh + 1) {
        gl.runs_ok = false;
        return;
    }
    gl.runs_next.begin((int)gh);
    for (size_t r = 0; r < gh; r++) {
        gl.runs_next.row((int)r);
        int a0 = gl.act_lo[r], a1 = gl.act_hi[r];
        for (const RowRuns<GlowRun>::Run* g = gl.runs.first((int)r); g < gl.runs.last((int)r); g++) {
            int u = std::max<int>(g->x0, a0), v = std::min<int>(g->x1, a1);
            if (g->p.was || u >= v) {
                gl.runs_next.emit(g->x0, g->x1, g->p);
                continue;
            }
            GlowRun aged = g->p;
            int age = (int)std::min<int64_t>(kGlowAgeMs, (aged.age & 0x7F) + dt);
            if (age >= kGlowAgeMs)
                aged.e = 0;
            aged.age = (uint8_t)(age | ((aged.age & 0x80) && age < kGlowAgeMs ? 0x80 : 0));
            gl.runs_aged += !(aged == g->p);
            gl.runs_next.emit(g->x0, u, g->p);
            gl.runs_next.emit(u, v, aged);
            gl.runs_next.emit(v, g->x1, g->p);
        }
    }
    gl.runs_next.end((int)gh);
    gl.runs.swap(gl.runs_next);
    if (gl.verify) {
        gl.verify_scans++;
        gl.runs_scans++;
        gl.verify_fails += !runs_match(gl, gw, (int)gh);
    }
}

struct GlowIn {
    bool ring;
    bool hold;
    bool dep;
    bool flip;
    bool hid;
    bool stale;
    bool glowing;
    int t;
    int prev;
    int step;
    int back;
    int was;
};

inline int glow_rule(const GlowIn& r, int64_t dt3, int dim_inc, int ring_inc, bool& lit, int& branch)
{
    int t = r.t, e = t, base = r.stale ? 0 : r.prev;
    lit = false;
    if (r.hid) {
        branch = 0;
        e = std::min(t, r.ring ? ring_inc : step_by(r.step, dt3));
        lit = e < t;
    } else if (r.ring && r.hold) {
        branch = 1;
        e = r.was ? std::min(r.prev, t) : std::min(r.back, t);
    } else if (r.ring) {
        branch = 2;
        e = std::min(kGlowFull, base + ring_inc);
        lit = (r.stale || r.glowing) && e < kGlowFull;
    } else if (r.dep) {
        branch = r.flip ? 3 : 4;
        if (r.flip)
            e = r.was && !(r.was & kGlowRing) ? std::min(r.prev, t) : std::min(r.back, t);
    } else if (r.stale || r.glowing) {
        branch = 5;
        e = std::min(t, base + step_by(r.step, dt3));
        lit = e < t;
    } else {
        branch = 6;
        e = t > base ? std::min(t, base + dim_inc) : t;
    }
    return e;
}

void shade_span(uint32_t* d, int n, uint32_t ground, int e, const GlowShade& h)
{
    if (!h.lut) {
        for (int k = 0; k < n; k++)
            d[k] = block_shade(d[k], ground, e, h.sigma);
        return;
    }
    if (e >= kGlowFull && h.sigma == 256)
        return;
    int a = h.lut[(e * 256 + kGlowFull / 2) / kGlowFull];
    if (a >= 256)
        return;
    if (a <= 0)
        std::fill(d, d + n, ground);
    else
        blend_row(d, n, ground, a);
}

void glow_frame(const LeftPlan& p, const Surface& dst, int w, uint32_t ground, int64_t ft, std::vector<uint8_t>& ids,
                bool shade = true)
{
    Glow& gl = glow_of(p);
    Rect gr = block_glow_rect(p);
    int gw = gr.x1 - gr.x0, gh = gr.y1 - gr.y0;
    size_t n = (size_t)gw * (size_t)gh;
    bool update = gl.due >= 0;
    bool prepped = gl.prep_fresh && gl.prep_w == gw && gl.prep_h == gh;
    if (update && gl.prep_fresh)
        gl.l2 = prepped ? 'a' : 'n';
    if (update) {
        if (gl.e.size() != n || (gl.prep_fresh && !prepped)) {
            gl.e.assign(n, 0);
            gl.age.assign(n, kGlowAgeMs);
            gl.ids.assign(n, 0);
            gl.act_lo.assign((size_t)gh, (int16_t)gw);
            gl.act_hi.assign((size_t)gh, 0);
            gl.hid.clear();
            gl.low.clear();
            gl.runs.clear();
            gl.runs_ok = false;
            gl.inited = false;
        }
        if (gl.t0 != gl.due_t0) {
            if (gl.inited && p.reversed && !gl.flip) {
                auto scan = [&]() {
                    std::vector<int> v(p.blocks.size(), kGlowFull + 1);
                    for (size_t i = 0; i < n; i++) {
                        int id = gl.ids[i];
                        if (id && !(id & kGlowRing))
                            v[(size_t)(id & 0x7F) - 1] = std::min(v[(size_t)(id & 0x7F) - 1], (int)gl.e[i]);
                    }
                    return v;
                };
                bool kept = gl.seg && gl.low.size() == p.blocks.size();
                if (!kept)
                    gl.low = scan();
                if (gl.verify && kept) {
                    gl.verify_scans++;
                    gl.back_scans++;
                    gl.verify_fails += gl.low != scan();
                }
                const std::vector<int>& low = gl.low;
                gl.back.assign(p.blocks.size(), 0);
                gl.back_t.assign(p.blocks.size(), kGlowDim);
                for (size_t b = 0; b < p.blocks.size(); b++) {
                    gl.back[b] = (uint16_t)(low[b] > kGlowFull ? 0 : low[b]);
                    gl.back_t[b] = (uint16_t)forward_shade(p, p.blocks[b], 3 * (gl.due_t0 - gl.t0)).t;
                }
                gl.flip_s3 = 3 * ((p.collapse ? p.fold - ft : ft) + gl.due - gl.due_t0);
                gl.flip = true;
            }
            gl.t0 = gl.due_t0;
        }
    }
    bool runs_on = gl.seg && update && gl.e.size() == n;
    if (runs_on && gl.inited && !gl.runs_ok) {
        gl.runs.begin(gh);
        for (int r = 0; r < gh; r++) {
            gl.runs.row(r);
            size_t row = (size_t)r * gw;
            const uint8_t* wr = gl.ids.data() + row;
            const uint8_t* ar = gl.age.data() + row;
            const uint8_t* hr = gl.hid.empty() ? nullptr : gl.hid.data() + row;
            const uint16_t* er = gl.e.data() + row;
            for (int x = 0; x < gw; x++)
                gl.runs.emit(x, x + 1, {wr[x], ar[x], hr ? hr[x] : (uint8_t)0, er[x]});
        }
        gl.runs.end(gh);
        gl.runs_built++;
        if (gl.clock_us)
            gl.times.runs_builds++;
        gl.runs_ok = true;
    }
    Rect both = p.cover ? gr.intersect(p.cover_rect) : Rect{};
    Rect cr = p.cover_rect;
    size_t cw = (size_t)(cr.x1 - cr.x0);
    assert(both.empty() || p.cover->size() == cw * (size_t)(cr.y1 - cr.y0));
    assert(both.empty() || gl.cover_rows.size() == (size_t)(cr.y1 - cr.y0));
    if (update && p.cover && !prepped)
        gl.hid_now.assign(n, 0);
    if (update)
        gl.prep_fresh = false;
    for (int y = both.y0; y < both.y1; y++) {
        size_t at = (size_t)(y - gr.y0) * gw + (size_t)(both.x0 - gr.x0);
        uint8_t* ir = ids.data() + at;
        uint8_t* hn = update ? gl.hid_now.data() + at : nullptr;
        const uint8_t* hd = gl.hid.empty() ? nullptr : gl.hid.data() + at;
        for (const std::pair<int, int>& c : gl.cover_rows[(size_t)(y - cr.y0)]) {
            int u = std::max(c.first, both.x0) - both.x0, v = std::min(c.second, both.x1) - both.x0;
            for (int k = u; hn && k < v; k++)
                hn[k] = ir[k] || (hd && hd[k]);
            if (u < v)
                std::fill(ir + u, ir + v, (uint8_t)0);
        }
    }
    if (update && p.cover)
        for (const Rect& r : gl.ring_hid) {
            Rect q = r.intersect(gr);
            for (int y = q.y0; y < q.y1; y++)
                for (int x = q.x0; x < q.x1; x++) {
                    size_t i = (size_t)(y - gr.y0) * gw + (size_t)(x - gr.x0);
                    if (!ids[i])
                        gl.hid_now[i] = 1;
                }
        }
    int64_t s3 = 3 * (p.collapse ? p.fold - ft : ft);
    gl.shades.resize(p.blocks.size());
    for (size_t b = 0; b < gl.shades.size(); b++)
        gl.shades[b] = glow_shade(p, (int)b, s3);
    const std::vector<GlowShade>& sh = gl.shades;
    int64_t dt = update && gl.last >= 0 ? gl.due - gl.last : 0;
    int64_t dt3 = 3 * dt;
    int dim_inc = step_by(kGlowDimStep, dt3), ring_inc = step_by(kGlowRingStep, dt3);
    bool first = update && !gl.inited;
    bool have = gl.inited && gl.e.size() == n;
    bool aging = update && !first;
    bool settled = true;
    bool flip = gl.flip;
    const GlowShade* shp = sh.data();
    const uint16_t* bk = gl.back.data();
    bool sized = gl.e.size() == n;
    uint16_t* ep = sized ? gl.e.data() : nullptr;
    uint8_t* ap = sized ? gl.age.data() : nullptr;
    const uint8_t* wp = sized ? gl.ids.data() : nullptr;
    const uint8_t* hp = gl.hid.empty() ? nullptr : gl.hid.data();
    bool segs = gl.seg && update && sized;
    if (segs)
        gl.seg_frames++;
    if (aging)
        gl.aging_frames++;
    if (update && gl.seg)
        gl.low_now.assign(p.blocks.size(), kGlowFull + 1);
    if (segs)
        gl.runs_next.begin(gh);
    if (p.cover)
        std::sort(gl.ring_hid.begin(), gl.ring_hid.end(),
                  [](const Rect& a, const Rect& b) { return a.y0 != b.y0 ? a.y0 < b.y0 : a.x0 < b.x0; });
    size_t rh = 0;
    for (int r = 0; r < gh; r++) {
        int y = gr.y0 + r;
        int x0 = gl.lo[(size_t)r], x1 = gl.hi[(size_t)r];
        if (aging) {
            x0 = std::min<int>(x0, gl.act_lo[(size_t)r]);
            x1 = std::max<int>(x1, gl.act_hi[(size_t)r]);
        }
        assert(x0 >= x1 || (x0 >= 0 && x1 <= gw));
        size_t row = (size_t)r * gw;
        const uint8_t* ir = ids.data() + row;
        uint16_t* er = sized ? ep + row : nullptr;
        uint8_t* ar = sized ? ap + row : nullptr;
        const uint8_t* wr = sized ? wp + row : nullptr;
        const uint8_t* hr = hp ? hp + row : nullptr;
        int act0 = gw, act1 = 0;
        uint32_t* d = dst.row(y);
        auto run = [&](int xi, int xe) {
            int x = gr.x0 + xi;
            int id = x < w ? ir[xi] : 0;
            if (!id) {
                int age = (int)std::min<int64_t>(kGlowAgeMs, (ar[xi] & 0x7F) + dt);
                uint8_t aged = (uint8_t)(age | ((ar[xi] & 0x80) && age < kGlowAgeMs ? 0x80 : 0));
                if (age >= kGlowAgeMs)
                    std::fill(er + xi, er + xe, (uint16_t)0);
                std::fill(ar + xi, ar + xe, aged);
                if (age < kGlowAgeMs) {
                    act0 = std::min(act0, xi);
                    act1 = xe;
                }
                return;
            }
            act0 = std::min(act0, xi);
            act1 = xe;
            bool ring = id & kGlowRing;
            const GlowShade& h = shp[(size_t)(id & 0x7F) - 1];
            bool hold = flip && h.dep;
            int t = ring && !hold ? kGlowFull : h.t;
            int age = ar[xi] & 0x7F;
            bool stale = age >= kGlowAgeMs, lit = false;
            GlowIn in{ring,
                      hold,
                      h.dep,
                      flip,
                      hr && hr[xi],
                      stale,
                      (ar[xi] & 0x80) != 0,
                      t,
                      er[xi],
                      h.step,
                      bk ? bk[(size_t)(id & 0x7F) - 1] : 0,
                      wr[xi]};
            int branch = 0;
            int e = glow_rule(in, dt3, dim_inc, ring_inc, lit, branch);
            gl.seg_hits[branch]++;
            if (!ring)
                gl.low_now[(size_t)(id & 0x7F) - 1] = std::min(gl.low_now[(size_t)(id & 0x7F) - 1], e);
            std::fill(er + xi, er + xe, (uint16_t)e);
            std::fill(ar + xi, ar + xe, (uint8_t)(lit ? 0x80 : 0));
            if (e != (ring && !hold ? kGlowFull : h.end))
                settled = false;
            if (shade)
                shade_span(d + x, xe - xi, ground, e, h);
        };
        auto at_t = [&](int xi, int xe) {
            int x = gr.x0 + xi;
            int id = x < w ? ir[xi] : 0;
            if (!id)
                return;
            act0 = std::min(act0, xi);
            act1 = xe;
            bool ring = id & kGlowRing;
            const GlowShade& h = shp[(size_t)(id & 0x7F) - 1];
            bool hold = flip && h.dep;
            int t = ring && !hold ? kGlowFull : h.t;
            std::fill(er + xi, er + xe, (uint16_t)t);
            std::fill(ar + xi, ar + xe, (uint8_t)0);
            if (!ring)
                gl.low_now[(size_t)(id & 0x7F) - 1] = std::min(gl.low_now[(size_t)(id & 0x7F) - 1], t);
            if (t != (ring && !hold ? kGlowFull : h.end))
                settled = false;
            if (shade)
                shade_span(d + x, xe - xi, ground, t, h);
        };
        if (segs) {
            std::vector<int>& ex = gl.extra;
            ex.clear();
            for (int c : {0, gw, x0, x1, w - gr.x0})
                ex.push_back(c);
            std::vector<std::pair<int, int>>& cov = gl.cover_row;
            cov.clear();
            if (p.cover && y >= both.y0 && y < both.y1)
                for (const std::pair<int, int>& c : gl.cover_rows[(size_t)(y - cr.y0)]) {
                    int u = std::max(c.first, both.x0) - gr.x0, v = std::min(c.second, both.x1) - gr.x0;
                    if (u < v)
                        cov.push_back({u, v});
                }
            const uint8_t* hnr = p.cover ? gl.hid_now.data() + row : nullptr;
            for (const std::pair<int, int>& c : cov) {
                ex.push_back(c.first);
                ex.push_back(c.second);
                for (int x = c.first; hnr && x < c.second;) {
                    x = run_end(hnr, x, c.second);
                    ex.push_back(x);
                }
            }
            gl.ring_row.clear();
            while (p.cover && rh < gl.ring_hid.size() && gl.ring_hid[rh].y0 < y)
                rh++;
            for (size_t q = rh; p.cover && q < gl.ring_hid.size() && gl.ring_hid[q].y0 == y; q++) {
                int u = std::max(gl.ring_hid[q].x0, gr.x0) - gr.x0, v = std::min(gl.ring_hid[q].x1, gr.x1) - gr.x0;
                if (u < v) {
                    gl.ring_row.push_back({u, v});
                    ex.push_back(u);
                    ex.push_back(v);
                }
            }
            std::sort(ex.begin(), ex.end());
            std::vector<int>& bd = gl.bounds;
            bd.clear();
            const RowRuns<GlowRun>::Run* la = first ? nullptr : gl.runs.first(r);
            const RowRuns<GlowRun>::Run* le = first ? nullptr : gl.runs.last(r);
            size_t k = 0;
            int nid = 0;
            for (;;) {
                int v = k < ex.size() ? ex[k] : INT_MAX;
                if (nid <= gw)
                    v = std::min(v, nid);
                if (la < le)
                    v = std::min<int>(v, la->x0);
                if (v == INT_MAX)
                    break;
                if (v >= 0 && v <= gw && (bd.empty() || bd.back() != v))
                    bd.push_back(v);
                if (nid == v)
                    nid = v < gw ? run_end(ir, v, gw) : gw + 1;
                if (la < le && la->x0 == v)
                    la++;
                while (k < ex.size() && ex[k] == v)
                    k++;
            }
            gl.runs_next.row(r);
            size_t ci = 0;
            for (size_t b = 0; b + 1 < bd.size(); b++) {
                int xi = bd[b], xe = bd[b + 1];
                gl.run_pieces++;
                if (xi >= x0 && xe <= x1 && first)
                    at_t(xi, xe);
                else if (xi >= x0 && xe <= x1)
                    run(xi, xe);
                while (ci < cov.size() && cov[ci].second <= xi)
                    ci++;
                bool covered = ci < cov.size() && cov[ci].first <= xi;
                bool in_ring = false;
                for (const std::pair<int, int>& q : gl.ring_row)
                    in_ring = in_ring || (xi >= q.first && xi < q.second);
                uint8_t hn = hnr ? hnr[xi] : 0;
                bool zero = p.cover && (covered || (in_ring && hn));
                gl.runs_next.emit(xi, xe,
                                  {ir[xi], zero ? (uint8_t)kGlowAgeMs : ar[xi], hn, zero ? (uint16_t)0 : er[xi]});
            }
        }
        for (int xi = x0; !segs && xi < x1; xi++) {
            int x = gr.x0 + xi;
            int id = x < w ? ir[xi] : 0;
            if (!id) {
                if (aging) {
                    int age = (int)std::min<int64_t>(kGlowAgeMs, (ar[xi] & 0x7F) + dt);
                    if (age >= kGlowAgeMs)
                        er[xi] = 0;
                    ar[xi] = (uint8_t)(age | ((ar[xi] & 0x80) && age < kGlowAgeMs ? 0x80 : 0));
                    if (age < kGlowAgeMs) {
                        act0 = std::min(act0, xi);
                        act1 = xi + 1;
                    }
                }
                continue;
            }
            act0 = std::min(act0, xi);
            act1 = xi + 1;
            bool ring = id & kGlowRing;
            const GlowShade& h = shp[(size_t)(id & 0x7F) - 1];
            bool hold = flip && h.dep;
            int t = ring && !hold ? kGlowFull : h.t;
            int e = t;
            if (aging) {
                int age = ar[xi] & 0x7F;
                bool stale = age >= kGlowAgeMs, lit = false;
                GlowIn in{ring,
                          hold,
                          h.dep,
                          flip,
                          hr && hr[xi],
                          stale,
                          (ar[xi] & 0x80) != 0,
                          t,
                          er[xi],
                          h.step,
                          bk ? bk[(size_t)(id & 0x7F) - 1] : 0,
                          wr[xi]};
                int branch = 0;
                e = glow_rule(in, dt3, dim_inc, ring_inc, lit, branch);
                er[xi] = (uint16_t)e;
                ar[xi] = lit ? 0x80 : 0;
            } else if (update) {
                er[xi] = (uint16_t)e;
                ar[xi] = 0;
            } else if (have) {
                e = er[xi];
            }
            if (gl.seg && update && !ring)
                gl.low_now[(size_t)(id & 0x7F) - 1] = std::min(gl.low_now[(size_t)(id & 0x7F) - 1], e);
            if (e != (ring && !hold ? kGlowFull : h.end))
                settled = false;
            if (shade)
                d[x] = shade_px(d[x], ground, e, h);
        }
        if (update) {
            gl.act_lo[(size_t)r] = (int16_t)act0;
            gl.act_hi[(size_t)r] = (int16_t)act1;
        }
    }
    if (!update)
        return;
    if (p.cover) {
        for (int y = both.y0; y < both.y1; y++) {
            size_t at = (size_t)(y - gr.y0) * gw + (size_t)(both.x0 - gr.x0);
            uint16_t* er = gl.e.data() + at;
            uint8_t* ar = gl.age.data() + at;
            for (const std::pair<int, int>& c : gl.cover_rows[(size_t)(y - cr.y0)]) {
                int u = std::max(c.first, both.x0) - both.x0, v = std::min(c.second, both.x1) - both.x0;
                if (u < v) {
                    std::fill(er + u, er + v, (uint16_t)0);
                    std::fill(ar + u, ar + v, (uint8_t)kGlowAgeMs);
                }
            }
        }
        for (const Rect& r : gl.ring_hid) {
            Rect q = r.intersect(gr);
            for (int y = q.y0; y < q.y1; y++)
                for (int x = q.x0; x < q.x1; x++) {
                    size_t i = (size_t)(y - gr.y0) * gw + (size_t)(x - gr.x0);
                    if (gl.hid_now[i]) {
                        gl.e[i] = 0;
                        gl.age[i] = kGlowAgeMs;
                    }
                }
        }
        gl.hid.swap(gl.hid_now);
    } else {
        gl.hid.clear();
    }
    gl.ids.swap(ids);
    if (gl.seg)
        gl.low.swap(gl.low_now);
    else
        gl.low.clear();
    if (segs) {
        gl.runs_next.end(gh);
        gl.runs.swap(gl.runs_next);
    }
    gl.runs_ok = segs;
    if (segs && gl.verify) {
        gl.verify_scans++;
        gl.runs_scans++;
        gl.verify_fails += !runs_match(gl, gw, gh);
    }
    gl.last = gl.due;
    gl.due = -1;
    gl.inited = true;
    gl.settled = settled;
}

void legs_into(const LeftPlan& p, int64_t tl, int boundary, std::vector<BlockPose>& out)
{
    Rect clip = block_clip(p, boundary);
    if (p.redeal)
        deal_poses(p, tl, clip, out);
    else
        swap_poses(p, tl, clip, out);
}

void poses_into(const LeftPlan& p, int64_t ft, int64_t tl, int boundary, std::vector<BlockPose>& out)
{
    out.clear();
    int rest = block_rest(p, ft, tl);
    if (rest == 0) {
        legs_into(p, tl, boundary, out);
        return;
    }
    Rect clip = block_clip(p, boundary);
    bool wide = rest > 0 ? !p.collapse : p.collapse;
    for (size_t i = 0; i < p.blocks.size(); i++)
        if (p.blocks[i].wide == wide) {
            BlockPose q = pose_at((int)i, p.blocks[i].cell, clip);
            if (!wide && p.redeal)
                q.ring = Rect{};
            out.push_back(q);
        }
}

}

static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__);

int run_end(const uint8_t* p, int x, int end)
{
    uint8_t v = p[x];
    uint64_t pat = 0x0101010101010101ull * v;
    int i = x + 1;
    for (; i + 8 <= end; i += 8) {
        uint64_t q;
        std::memcpy(&q, p + i, sizeof q);
        uint64_t d = q ^ pat;
        if (d)
            return i + (__builtin_ctzll(d) >> 3);
    }
    while (i < end && p[i] == v)
        i++;
    return i;
}

int cell_edge_after(int x)
{
    int cx = x - kGridX;
    if (cx < 0)
        return kGridX;
    int c = cx / kPitch;
    if (c >= kGridColsWide)
        return INT_MAX;
    return cx % kPitch < kTileSide ? kGridX + c * kPitch + kTileSide : kGridX + (c + 1) * kPitch;
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

bool block_form(LeftMove m) { return m == LeftMove::Stepper; }

void block_rows(LeftPlan& p)
{
    p.full_rows.clear();
    for (const Block& k : p.blocks)
        if (k.wide && k.col == kLastCol && k.art) {
            if ((int)p.full_rows.size() <= k.row)
                p.full_rows.resize((size_t)k.row + 1, 0);
            p.full_rows[(size_t)k.row] = 1;
        }
}

int block_beats(const LeftPlan& p)
{
    int h = 0;
    for (const Block& k : p.blocks)
        if (k.wide)
            h = std::max(h, track_start(k.game) - k.game);
    return h;
}

int64_t block_thirds(const LeftPlan& p)
{
    if (p.redeal)
        return (int64_t)block_beats(p) * kStepThirds;
    return (int64_t)kSwapCells * kStepThirds;
}

int64_t block_ms(const LeftPlan& p) { return (block_thirds(p) + 2) / 3; }

int64_t block_time(const LeftPlan& p, int64_t ft)
{
    int64_t d = block_thirds(p);
    if (p.collapse)
        return std::clamp<int64_t>(3 * (p.fold - ft), 0, d);
    return std::min<int64_t>(3 * ft, d);
}

int64_t block_left_ms(const LeftPlan& p) { return block_left_for(block_ms(p), p.pane_end); }

int block_rest(const LeftPlan& p, int64_t ft, int64_t tl)
{
    if (p.collapse)
        return ft <= 0 ? 1 : tl <= 0 ? -1 : 0;
    int64_t d3 = std::max<int64_t>(block_thirds(p), 3 * p.pane_end);
    bool held = 3 * ft >= d3 + 3 * (int64_t)kBlockTailMs;
    if (held && (!p.glow_on || glow_view(p).ready || ft >= block_left_ms(p)))
        return 1;
    return ft <= 0 ? -1 : 0;
}

Rect block_clip(const LeftPlan& p, int boundary)
{
    int xr = std::min(kEdgeHi, std::max(boundary, kPaneLeftEdge));
    return p.band.intersect({kEdgeLo, 0, xr, kPortraitH});
}

std::vector<BlockPose> block_legs(const LeftPlan& p, int64_t tl, int boundary)
{
    std::vector<BlockPose> out;
    legs_into(p, tl, boundary, out);
    return out;
}

std::vector<BlockPose> block_poses(const LeftPlan& p, int64_t ft, int64_t tl, int boundary)
{
    std::vector<BlockPose> out;
    poses_into(p, ft, tl, boundary, out);
    return out;
}

Rect block_box(const LeftPlan& p, const BlockPose& q)
{
    const Block& k = p.blocks[(size_t)q.block];
    Rect vis = q.rect.intersect(q.clip);
    if (vis.empty() || !k.focus || q.ring.empty())
        return vis;
    return vis.grow(kTileFocusBorder).intersect(q.rect.grow(kTileFocusBorder)).intersect(q.ring);
}

Rect block_cover(const LeftPlan& p, const BlockPose& q)
{
    const Block& k = p.blocks[(size_t)q.block];
    Rect box = block_box(p, q);
    if (!box.empty() && !p.redeal && k.wide && k.col == kLastCol)
        box.x1 = std::max(box.x1, kEdgeHi);
    return box;
}

void block_paint(const Surface& a, const Surface& b, const Surface& dst, const LeftPlan& p,
                 const std::vector<BlockPose>& poses, int x_end, uint32_t ground)
{
    raster(a, b, dst, p, poses, x_end, ground, IdMap{});
}

Rect glow_rect_of(Rect band) { return band.intersect({0, 0, kPaneSplitWide, kPortraitH}); }

Rect block_glow_rect(const LeftPlan& p) { return glow_rect_of(p.band); }

bool glow_prepare(Glow& g, uint64_t gen, Rect gr, int rows)
{
    int gw = gr.x1 - gr.x0, gh = gr.y1 - gr.y0;
    if (gw <= 0 || gh <= 0)
        return true;
    if (g.prep_fresh && g.gen == gen && g.prep_w == gw && g.prep_h == gh)
        return true;
    if (g.gen != 0 || g.prep_target != gen || g.prep_w != gw || g.prep_h != gh) {
        glow_reset(g);
        g.prep_target = gen;
        g.prep_w = gw;
        g.prep_h = gh;
    }
    int r1 = std::min(gh, g.prep_row + std::max(1, rows));
    size_t n1 = (size_t)r1 * (size_t)gw;
    g.e.resize(n1, 0);
    g.age.resize(n1, kGlowAgeMs);
    g.ids.resize(n1, 0);
    g.cur.resize(n1, 0);
    g.hid_now.resize(n1, 0);
    g.act_lo.resize((size_t)r1, (int16_t)gw);
    g.act_hi.resize((size_t)r1, 0);
    g.prep_row = r1;
    if (r1 < gh)
        return false;
    g.gen = gen;
    g.prep_fresh = true;
    return true;
}

int block_key(const LeftPlan& p, int block)
{
    const Block& k = p.blocks[(size_t)block];
    if (p.redeal && !k.wide) {
        int w = find_game(p, true, k.game);
        if (w >= 0)
            return w;
    }
    return block;
}

int block_sigma(const LeftPlan& p, const Block& k) { return !p.redeal && !k.wide ? kSwapSigma : 256; }

void glow_reset(Glow& g)
{
    Glow f;
    f.seg = g.seg;
    f.verify = g.verify;
    f.dry = g.dry;
    f.times = g.times;
    f.clock_us = std::move(g.clock_us);
    auto take = [](auto& to, auto& from) {
        from.clear();
        to.swap(from);
    };
    take(f.e, g.e);
    take(f.age, g.age);
    take(f.ids, g.ids);
    take(f.cur, g.cur);
    take(f.spare, g.spare);
    take(f.lo, g.lo);
    take(f.hi, g.hi);
    take(f.act_lo, g.act_lo);
    take(f.act_hi, g.act_hi);
    take(f.back, g.back);
    take(f.low, g.low);
    take(f.low_now, g.low_now);
    take(f.back_t, g.back_t);
    take(f.shades, g.shades);
    take(f.poses, g.poses);
    take(f.covers, g.covers);
    take(f.spans, g.spans);
    take(f.tmp, g.tmp);
    take(f.sig_now, g.sig_now);
    take(f.hid, g.hid);
    take(f.hid_now, g.hid_now);
    take(f.ring_hid, g.ring_hid);
    take(f.cover_rows, g.cover_rows);
    take(f.runs, g.runs);
    take(f.runs_next, g.runs_next);
    take(f.cover_row, g.cover_row);
    take(f.ring_row, g.ring_row);
    take(f.bounds, g.bounds);
    take(f.extra, g.extra);
    g = std::move(f);
    assert(g.e.empty() && !g.inited && g.t0 == -1 && g.gen == 0);
}

Glow& glow_of(const LeftPlan& p)
{
    if (!p.shared)
        return p.glow;
    if (p.shared->gen != p.gen) {
        int64_t c0 = p.shared->clock_us ? p.shared->clock_us() : 0;
        char why = p.shared->prep_fresh ? 'g' : p.shared->prep_row > 0 ? 'p' : '-';
        glow_reset(*p.shared);
        p.shared->gen = p.gen;
        p.shared->l2 = why;
        if (p.shared->clock_us)
            p.shared->times.reset += p.shared->clock_us() - c0;
    }
    return *p.shared;
}

const Glow& glow_view(const LeftPlan& p)
{
    static const Glow fresh;
    if (!p.shared)
        return p.glow;
    return p.shared->gen == p.gen ? *p.shared : fresh;
}

bool block_departs(const LeftPlan& p, int block)
{
    return departs(p, p.blocks[(size_t)block], glow_view(p).flip);
}

uint32_t block_shade(uint32_t src, uint32_t ground, int e, int sigma)
{
    int a = (int)(((int64_t)e * 256 + kGlowFull / 2) / kGlowFull);
    a = std::min(256, (a * 256 + sigma / 2) / sigma);
    return a >= 256 ? src : a <= 0 ? ground : blend_px(ground, src, a);
}

void block_advance(const LeftPlan& p, int64_t t0, int64_t now)
{
    Glow& gl = glow_of(p);
    gl.due = now;
    gl.due_t0 = t0;
}

bool block_settled(const LeftPlan& p) { return !p.glow_on || (glow_view(p).inited && glow_view(p).settled); }

void block_skip(const LeftPlan& p) { glow_of(p).due = -1; }

void block_compose(const Surface& a, const Surface& b, const Surface& dst, const LeftPlan& p, int64_t ft, int64_t tl,
                   int boundary, int x_end, uint32_t ground)
{
    int w = std::min(x_end, dst.w), h = dst.h;
    Glow& gl = glow_of(p);
    Under under = cover_under(p, gl.cover_rows, x_end);
    if (gl.due >= 0)
        gl.ready = gl.inited && gl.settled;
    int rest = block_rest(p, ft, tl);
    const Surface& from = p.collapse ? b : a;
    const Surface& to = p.collapse ? a : b;
    bool start = rest < 0;
    bool side_small = start ? !p.collapse : p.collapse;
    bool fresh = gl.due < 0 || gl.due_t0 == gl.t0;
    bool copy = rest != 0 && (!p.glow_on || (gl.inited && gl.settled && fresh && !(p.redeal && side_small)));
    if (gl.due >= 0)
        gl.copied = copy && p.glow_on;
    if (copy && start) {
        for (int y = 0; y < h; y++)
            under.open(y, 0, w, [&](int u, int v) { copy_px(dst.row(y) + u, from.row(y) + u, v - u); });
    } else if (copy) {
        int e = p.collapse ? std::min(w, kPaneLeftEdge) : w;
        for (int y = 0; y < h; y++) {
            under.open(y, 0, e, [&](int u, int v) { copy_px(dst.row(y) + u, to.row(y) + u, v - u); });
            under.open(y, e, w, [&](int u, int v) { std::fill(dst.row(y) + u, dst.row(y) + v, ground); });
        }
    } else if (!p.glow_on) {
        poses_into(p, ft, tl, boundary, gl.poses);
        raster(a, b, dst, p, gl.poses, x_end, ground, IdMap{}, true, under);
    } else {
        Rect gr = block_glow_rect(p);
        int gw = gr.x1 - gr.x0, gh = gr.y1 - gr.y0;
        auto paint = [&](int64_t at, int64_t at_l, int bx, std::vector<uint8_t>& ids, bool seed) {
            if (!(seed && &ids == &gl.cur && gl.prep_fresh && gl.prep_w == gw && gl.prep_h == gh))
                ids.assign((size_t)gw * (size_t)gh, 0);
            gl.lo.assign((size_t)gh, (int16_t)gw);
            gl.hi.assign((size_t)gh, 0);
            poses_into(p, at, at_l, bx, gl.poses);
            gl.sig_now.assign(p.blocks.size(), 0);
            for (const BlockPose& q : gl.poses) {
                int64_t& v = gl.sig_now[(size_t)q.block];
                for (int64_t f : {(int64_t)q.rect.x0, (int64_t)q.rect.y0, (int64_t)q.rect.x1, (int64_t)q.rect.y1})
                    v = v * 1000003 + f + 1;
            }
            bool marks = seed;
            for (const BlockPose& q : gl.poses)
                marks = marks && block_box(p, q).x1 <= w;
            if (marks)
                gl.seed_frames++;
            if (marks && gl.clock_us)
                gl.times.seed_marks++;
            if (seed && gl.clock_us) {
                gl.times.seed_why = 0;
                for (const BlockPose& q : gl.poses)
                    gl.times.seed_why |= block_box(p, q).x1 > w ? 1u << ('w' - 'a') : 0u;
            }
            bool draw = !marks && (seed || !gl.dry);
            int64_t r0 = gl.clock_us ? gl.clock_us() : 0;
            raster(a, b, dst, p, gl.poses, x_end, ground, IdMap{ids.data(), gr, gl.lo.data(), gl.hi.data()}, draw,
                   under, seed && gl.clock_us ? &gl.times.seed_past : nullptr);
            int64_t r1 = gl.clock_us ? gl.clock_us() : 0;
            glow_frame(p, dst, w, ground, at, ids, draw);
            if (gl.clock_us) {
                int64_t r2 = gl.clock_us();
                (seed ? gl.times.seed_raster : gl.times.frame_raster) += r1 - r0;
                (seed ? gl.times.seed_glow : gl.times.frame_glow) += r2 - r1;
            }
        };
        int64_t f0 = p.collapse ? p.fold : 0;
        if (gl.due >= 0 && !gl.inited && ft != f0) {
            int64_t due = gl.due;
            gl.due = gl.due_t0;
            int64_t c0 = gl.clock_us ? gl.clock_us() : 0;
            int seeds = gl.seed_frames;
            paint(f0, block_time(p, f0), kPaneSplitWide, gl.cur, true);
            if (gl.clock_us) {
                gl.times.seed += gl.clock_us() - c0;
                bool hides = false;
                Rect cr = p.cover_rect;
                for (int y = std::max(cr.y0, gr.y0); p.cover && !hides && y < std::min(cr.y1, gr.y1); y++)
                    for (const std::pair<int, int>& c : gl.cover_rows[(size_t)(y - cr.y0)])
                        hides = hides || (c.first < gr.x1 && c.second > gr.x0);
                gl.guard_sig = gl.sig_now;
                gl.guard_t.clear();
                for (const GlowShade& s : gl.shades)
                    gl.guard_t.push_back(s.t);
                gl.guard_open = seeds != gl.seed_frames && !gl.flip && !p.reversed && !hides;
                gl.times.guard_seeds++;
            }
            gl.due = due;
        }
        int64_t c1 = gl.clock_us ? gl.clock_us() : 0;
        paint(ft, tl, boundary, gl.due >= 0 ? gl.cur : gl.spare, false);
        if (gl.clock_us) {
            gl.times.frame += gl.clock_us() - c1;
            if (gl.guard_open) {
                bool same_t = gl.guard_t.size() == gl.shades.size();
                for (size_t b = 0; same_t && b < gl.shades.size(); b++)
                    same_t = gl.guard_t[b] == gl.shades[b].t;
                gl.times.guard_pass += gl.guard_sig == gl.sig_now && same_t;
                gl.guard_open = false;
            }
        }
    }
    if (copy && p.glow_on && gl.due >= 0) {
        Rect gr = block_glow_rect(p);
        glow_age(gl, gl.last >= 0 ? gl.due - gl.last : 0, gr.x1 - gr.x0);
        gl.last = gl.due;
        gl.due = -1;
    }
    int head = swap_header(ft, false);
    Rect hr = p.header.intersect({0, 0, w, h});
    for (int y = hr.y0; y < hr.y1; y++) {
        const uint32_t* s = a.row(y);
        uint32_t* d = dst.row(y);
        under.open(y, hr.x0, hr.x1, [&](int u, int v) {
            for (int x = u; x < v; x++)
                d[x] = head >= 256 ? s[x] : head <= 0 ? ground : blend_px(ground, s[x], head);
        });
    }
}

}
