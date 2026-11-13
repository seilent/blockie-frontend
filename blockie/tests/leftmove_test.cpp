#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <unistd.h>

#include "../../core/curves.h"
#include "../dumpz.h"
#include "glow_cmp.h"
#include "../leftmove.h"
#include "../slide.h"
#include "../tile_grid.h"

using namespace fd;

namespace {

int g_fail = 0;
int g_total = 0;

void check(bool ok, const char* what)
{
    g_total++;
    if (ok) {
        std::printf("PASS %s\n", what);
    } else {
        std::printf("FAIL %s\n", what);
        g_fail++;
    }
}

constexpr uint32_t kGround = 0x111111;
constexpr uint32_t kHeadInk = 0x808080;
constexpr uint32_t kBorder = 0xFEFEFE;
constexpr uint32_t kSentinel = 0x00FF00;
constexpr Rect kHead{120, 0, 300, 76};
constexpr Rect kBand{0, kBandTop, kPaneSplitWide, kBandBottom};

const uint32_t kFrom[8] = {0x200000, 0x210000, 0x220000, 0x230000, 0x240000, 0x250000, 0x260000, 0x270000};
const uint32_t kTo[12] = {0x200000, 0x210000, 0x002200, 0x002300, 0x002400, 0x002500,
                          0x002600, 0x002700, 0x002800, 0x002900, 0x002A00, 0x002B00};
const uint32_t kSys[16] = {0x000030, 0x000031, 0x000032, 0x000033, 0x000034, 0x000035, 0x000036, 0x000037,
                           0x000038, 0x000039, 0x00003A, 0x00003B, 0x00003C, 0x00003D, 0x00003E, 0x00003F};

bool same_rect(Rect a, Rect b) { return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1; }

bool in(Rect r, int x, int y) { return x >= r.x0 && x < r.x1 && y >= r.y0 && y < r.y1; }

bool same_rects(const std::vector<Rect>& got, const std::vector<Rect>& want)
{
    if (got.size() != want.size())
        return false;
    for (size_t i = 0; i < got.size(); i++)
        if (!same_rect(got[i], want[i]))
            return false;
    return true;
}

void border(Canvas& c, Rect r)
{
    Rect g = r.grow(kTileFocusBorder);
    c.fill({g.x0, g.y0, g.x1, r.y0}, kBorder);
    c.fill({g.x0, r.y1, g.x1, g.y1}, kBorder);
    c.fill({g.x0, r.y0, r.x0, r.y1}, kBorder);
    c.fill({r.x1, r.y0, g.x1, r.y1}, kBorder);
}

void collapsed(const Surface& s, int focus)
{
    Canvas c(s);
    c.fill(s.rect(), kGround);
    c.fill({124, 20, 296, 70}, kHeadInk);
    for (int k = 0; k < kGridTiles; k++)
        c.fill(grid_tile_rect(k, kGridCols), kFrom[k]);
    if (focus >= 0)
        border(c, grid_tile_rect(focus, kGridCols));
    for (int i = 0; i < 6; i++)
        c.fill({512, 56 + i * 96, 692, 96 + i * 96}, 0x909090);
}

void expanded(const Surface& s, const uint32_t* tiles, int n, int focus, int grid_y)
{
    Canvas c(s);
    c.fill(s.rect(), kGround);
    for (int k = 0; k < n; k++) {
        Rect r = grid_tile_rect(k, 3);
        r.y0 += grid_y;
        r.y1 += grid_y;
        r = r.intersect(kBand);
        if (!r.empty())
            c.fill(r, tiles[k]);
    }
    if (focus >= 0)
        border(c, grid_tile_rect(focus, 3));
}

void large(const Surface& s)
{
    Canvas c(s);
    c.fill(s.rect(), kGround);
    for (int k = 0; k < 6; k++)
        c.fill(big_tile_rect(k).intersect(kBand), kSys[k]);
}

void fill(const Surface& s, uint32_t v)
{
    for (int y = 0; y < s.h; y++)
        for (int x = 0; x < s.w; x++)
            s.at(x, y) = v;
}

LeftPlan plan_for(Rect focus_from, Rect focus_to)
{
    LeftPlan p = slide_plan(kPaneSplitWide, LeftMove::T5, kHead).moves;
    p.focus_from = focus_from;
    p.focus_to = focus_to;
    return p;
}

struct Masks {
    std::vector<Rect> kept;
    std::vector<Rect> collapsed;
    std::vector<Rect> expanded;
    Rect header;
};

Masks masks_of(const LeftPlan& p) { return {p.kept, p.leaves, p.bands, p.header}; }

uint32_t ref_px(const Surface& a, const Surface& b, const Masks& m, bool collapse, int64_t t, int x, int y, uint32_t g)
{
    const Surface& from = collapse ? b : a;
    const Surface& to = collapse ? a : b;
    if (collapse ? t < 0 : t <= 0)
        return from.at(x, y);
    if (t >= 250)
        return collapse && x >= kPaneLeftEdge ? g : to.at(x, y);
    auto kept = [&](int px) {
        for (const Rect& k : m.kept)
            if (in(k, px, y))
                return true;
        return false;
    };
    if (kept(x))
        return to.at(x, y);
    uint32_t v = g;
    int head = collapse ? swap_arrive(t, -1).a256 : swap_header(t, false);
    if (in(m.header, x, y) && head > 0)
        v = blend_px(g, a.at(x, y), head);
    bool leaving = t < 50;
    int dir = collapse ? -1 : 1;
    SwapLeg leg = leaving ? swap_leave(t, dir) : swap_arrive(t, dir);
    const Surface& src = leaving ? from : to;
    int sx = x - leg.dx;
    for (const Rect& band : leaving == collapse ? m.expanded : m.collapsed)
        if (leg.a256 > 0 && y >= band.y0 && y < band.y1 && in(band, sx, y) && !kept(sx))
            v = leg.a256 >= 256 ? src.at(sx, y) : blend_px(g, src.at(sx, y), leg.a256);
    return v;
}

bool matches(const Surface& a, const Surface& b, const Masks& m, bool collapse, const Surface& d, int64_t t, int x_end,
             uint32_t g)
{
    for (int y = 0; y < d.h; y++)
        for (int x = 0; x < x_end; x++)
            if (d.at(x, y) != ref_px(a, b, m, collapse, t, x, y, g))
                return false;
    return true;
}

struct ComposeResult {
    bool model = true;
    bool sentinel = true;
    bool right = true;
    bool kept = true;
};

ComposeResult compose_all(const Surface& a, const Surface& b, const LeftPlan& p, uint32_t g)
{
    ComposeResult r;
    Masks m = masks_of(p);
    const Surface& to = p.collapse ? a : b;
    Buffer out;
    Surface d = out.surface();
    for (int64_t t : {-16, 0, 1, 17, 25, 33, 49, 50, 51, 67, 100, 133, 150, 200, 233, 249, 250, 300})
        for (int x_end : {436, 520, 648}) {
            fill(d, kSentinel);
            swap_compose(a, b, d, p, t, x_end, g);
            if (!matches(a, b, m, p.collapse, d, t, x_end, g))
                r.model = false;
            for (int y = 0; y < d.h; y++)
                for (int x = 0; x < d.w; x++) {
                    uint32_t v = d.at(x, y);
                    if (x >= x_end) {
                        if (v != kSentinel)
                            r.right = false;
                        continue;
                    }
                    if (v == kSentinel)
                        r.sentinel = false;
                    for (const Rect& k : m.kept)
                        if (in(k, x, y) && t > 0 && v != to.at(x, y))
                            r.kept = false;
                }
        }
    return r;
}

struct BlockCase {
    int recent = 0;
    int sys = 0;
    bool redeal = false;
    bool collapse = false;
    int grid_y = 0;
    int focus = 0;
    std::vector<int> artless;
    std::vector<int> artless_sys;
    bool twin = false;
};

struct BlockRun {
    bool written = true;
    bool pure = true;
    bool legs = true;
    bool ends = true;
    bool lands = true;
    bool lower = true;
    bool upper = true;
    bool apart = true;
    bool kept = true;
    bool artless = true;
    bool twin = false;
    int dip = 0;
    int rise = 0;
    int bad_x = -1;
    int bad_y = -1;
    int bad_block = -1;
    uint32_t bad_v = 0;
    int leg = -1;
    int leg_x = -1;
    int leg_y = -1;
    uint32_t leg_got = 0;
    uint32_t leg_want = 0;
};

constexpr int kAreaBar = 8000;

int64_t frame_ms(int n) { return (int64_t)n * 1000 / 60; }

bool listed(const std::vector<int>& v, int k) { return std::find(v.begin(), v.end(), k) != v.end(); }

uint32_t block_ink(const Block& k) { return 0x204000u + (uint32_t)k.id * 0x000301u; }

SlidePlan block_slide(const BlockCase& c)
{
    SlidePlan sp = slide_plan(kPaneSplitWide, LeftMove::Stepper, kHead);
    LeftPlan& p = sp.moves;
    p.redeal = c.redeal;
    p.collapse = c.collapse;
    p.grid_y = c.grid_y;
    p.focus_ink = kBorder;
    for (int k = 0; k < std::min(c.recent, kGridTiles); k++) {
        Block b;
        b.id = 100 + k;
        b.game = k;
        b.col = k % kGridCols;
        b.row = k / kGridCols;
        b.cell = grid_tile_rect(k, kGridCols);
        b.art = !listed(c.artless, k);
        b.focus = c.redeal && k == c.focus;
        p.blocks.push_back(b);
    }
    int n = c.redeal ? std::min(c.recent, kGridColsWide * kGridRows) : c.sys;
    for (int k = 0; k < n; k++) {
        Rect r = grid_tile_rect(k, kGridColsWide);
        r.y0 += c.grid_y;
        r.y1 += c.grid_y;
        if (r.grow(kTileFocusBorder).intersect(p.band).empty())
            continue;
        Block b;
        b.id = c.redeal || (c.twin && k == 1) ? 100 + k : 200 + k;
        b.game = k;
        b.wide = true;
        b.col = k % kGridColsWide;
        b.row = k / kGridColsWide;
        b.cell = r;
        b.art = !listed(c.artless, k) && !listed(c.artless_sys, k);
        b.focus = k == c.focus;
        p.blocks.push_back(b);
    }
    block_rows(p);
    return sp;
}

void paint_side(const Surface& s, const LeftPlan& p, bool wide, bool ring = true)
{
    Canvas c(s);
    c.fill(s.rect(), kGround);
    if (!wide)
        c.fill({124, 20, 296, 70}, kHeadInk);
    c.set_clip(p.band);
    for (const Block& k : p.blocks)
        if (k.wide == wide) {
            if (k.art)
                c.fill(k.cell, block_ink(k));
            if (k.focus && ring)
                border(c, k.cell);
        }
}

void area_of(const Surface& d, int x_end, int& l, int& m)
{
    l = m = 0;
    for (int y = kBandTop; y < kBandBottom; y++)
        for (int x = 0; x < std::min(x_end, kPaneSplitWide); x++)
            if (d.at(x, y) != kGround)
                (x < kPaneSplit ? l : m)++;
}

bool region_is(const Surface& d, const Surface& s, int x0, int x1)
{
    for (int y = 0; y < d.h; y++)
        for (int x = x0; x < x1; x++)
            if (d.at(x, y) != s.at(x, y))
                return false;
    return true;
}

bool first_diff(const Surface& d, const Surface& s, int x0, int x1, int& bx, int& by, uint32_t& got, uint32_t& want)
{
    for (int y = kBandTop; y < kBandBottom; y++)
        for (int x = x0; x < x1; x++)
            if (d.at(x, y) != s.at(x, y)) {
                bx = x;
                by = y;
                got = d.at(x, y);
                want = s.at(x, y);
                return true;
            }
    return false;
}

bool band_is(const Surface& d, const Surface& s, int x0, int x1)
{
    for (int y = kBandTop; y < kBandBottom; y++)
        for (int x = x0; x < x1; x++)
            if (d.at(x, y) != s.at(x, y))
                return false;
    return true;
}

bool region_ground(const Surface& d, int x0, int x1, int y0, int y1)
{
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            if (d.at(x, y) != kGround)
                return false;
    return true;
}

bool inside(Rect r, int x, int y) { return x >= r.x0 && x < r.x1 && y >= r.y0 && y < r.y1; }

void menu_side(const Surface& s, const LeftPlan& p)
{
    Canvas c(s);
    c.fill(s.rect(), kGround);
    c.set_clip(p.band);
    int a = alpha256(tok::motion_quickplayAlpha);
    for (const Block& k : p.blocks)
        if (!k.wide && k.art)
            c.fill(k.cell, blend_px(kGround, block_ink(k), a));
}

struct GlowDrive {
    SlidePlan sp;
    Buffer ra, rb, rd;
    int64_t t0 = 1000;
    int64_t back_e = -1;
    int64_t back_t0 = 0;

    explicit GlowDrive(const BlockCase& c) : sp(block_slide(c))
    {
        sp.moves.glow_on = true;
        slide_lang(sp, pane_surface());
        paint_side(ra.surface(), sp.moves, false);
        paint_side(rb.surface(), sp.moves, true);
    }

    int64_t ft_at(int64_t now) const
    {
        if (back_e >= 0)
            return std::max<int64_t>(0, back_e - (now - back_t0));
        int64_t s = now - t0;
        return sp.moves.collapse ? std::max<int64_t>(0, sp.moves.fold - s) : std::min<int64_t>(s, sp.moves.fold);
    }

    int x_end(int64_t ft) const
    {
        return std::max(std::clamp(slide_pose(sp, ft).boundary, kPaneSplit, kPortraitW), kPaneLeftEdge);
    }

    void frame(int64_t now, bool advance = true)
    {
        int64_t ft = ft_at(now);
        if (advance)
            block_advance(sp.moves, back_e >= 0 ? back_t0 : t0, now);
        int bx = std::clamp(slide_pose(sp, ft).boundary, kPaneSplit, kPortraitW);
        block_compose(ra.surface(), rb.surface(), rd.surface(), sp.moves, ft, block_time(sp.moves, ft), bx,
                      std::max(bx, kPaneLeftEdge), kGround);
    }

    void back(int64_t now)
    {
        back_e = now - t0;
        back_t0 = now;
        sp.moves.reversed = true;
    }

    bool landed(int64_t now) const
    {
        return back_e >= 0 ? now - back_t0 >= back_e + kBlockTailMs : now - t0 >= sp.moves.fold;
    }

    const Glow& glow() const { return glow_view(sp.moves); }
};

struct LangDrive {
    SlidePlan sp;
    Buffer ra, rb, rd;
    int64_t t0 = 1000;
    int64_t prev = -1;
    int64_t back_at = -1;
    uint32_t ground = kGround;
    Rect drawn;

    explicit LangDrive(const BlockCase& c) : sp(block_slide(c))
    {
        sp.moves.glow_on = true;
        slide_lang(sp, pane_surface());
        paint_side(ra.surface(), sp.moves, false);
        paint_side(rb.surface(), sp.moves, true);
    }

    void frame(int64_t s)
    {
        int64_t now = 1000 + s, ft;
        if (back_at >= 0 && s >= back_at) {
            if (!sp.moves.reversed) {
                sp.moves.reversed = true;
                t0 = now;
            }
            ft = std::max<int64_t>(0, back_at - (now - t0));
        } else {
            ft = sp.moves.collapse ? std::max<int64_t>(0, sp.moves.fold - s) : std::min<int64_t>(s, sp.moves.fold);
        }
        slide_advance(sp, t0, now);
        drawn = slide_compose(ra.surface(), rb.surface(), rd.surface(), sp, ft, prev, ground, SlideText{});
        prev = ft;
    }
};

void collapsed_rest(const Surface& out, LangDrive& d)
{
    Buffer menu;
    menu_side(menu.surface(), d.sp.moves);
    Surface ms = menu.surface(), sa = d.ra.surface();
    bool redeal = d.sp.moves.redeal;
    for (int y = 0; y < out.h; y++)
        for (int x = 0; x < out.w; x++)
            out.row(y)[x] =
                redeal && x < kPaneLeftEdge && y >= kBandTop && y < kBandBottom ? ms.at(x, y) : sa.at(x, y);
}

size_t glow_at(const LeftPlan& p, int x, int y)
{
    Rect gr = block_glow_rect(p);
    return (size_t)(y - gr.y0) * (size_t)(gr.x1 - gr.x0) + (size_t)(x - gr.x0);
}

bool arriving(const LeftPlan& p, int id) { return id && !(id & kGlowRing) && !block_departs(p, (id & 0x7F) - 1); }

bool departing(const LeftPlan& p, int id) { return id && !(id & kGlowRing) && block_departs(p, (id & 0x7F) - 1); }

BlockRun run_blocks(const BlockCase& c)
{
    BlockRun r;
    SlidePlan sp = block_slide(c);
    const LeftPlan& p = sp.moves;
    Buffer ba, bb, d1, d2;
    Surface as = ba.surface(), bs = bb.surface(), s1 = d1.surface(), s2 = d2.surface();
    paint_side(as, p, false);
    paint_side(bs, p, true);
    const Surface& from = c.collapse ? bs : as;
    const Surface& to = c.collapse ? as : bs;
    Buffer bz;
    paint_side(bz.surface(), p, false, false);
    const Surface& leg_to = c.redeal && c.collapse ? bz.surface() : to;
    int64_t fold = slide_duration(sp);
    std::vector<int> ls, ms;
    int frames = (int)(fold * 60 / 1000);
    int start_l = 0, start_m = 0, end_l = 0, end_m = 0;
    int64_t d3 = block_thirds(p), cut = (d3 + 2) / 3;
    for (int e = 0; e < (d3 > 0 ? 2 : 1); e++) {
        int64_t at = c.collapse ? (e == 0 ? cut + kBlockTailMs : 0) : (e == 0 ? 0 : cut);
        int bx = std::clamp(slide_pose(sp, at).boundary, kPaneSplit, kPortraitW);
        int x_end = std::max(bx, kPaneLeftEdge);
        fill(s1, kSentinel);
        block_paint(as, bs, s1, p, block_legs(p, e == 0 ? 0 : d3, bx), x_end, kGround);
        int keep = c.collapse ? std::min(x_end, kPaneLeftEdge) : x_end;
        const Surface& want = e == 0 ? from : leg_to;
        bool bad = first_diff(s1, want, 0, e == 0 ? x_end : keep, r.leg_x, r.leg_y, r.leg_got, r.leg_want) ||
                   (e == 1 && !region_ground(s1, keep, x_end, kBandTop, kBandBottom));
        if (bad && r.legs) {
            r.legs = false;
            r.leg = e;
        }
    }
    for (int n = 0; n <= frames; n++) {
        int64_t s = frame_ms(n);
        int64_t ft = c.collapse ? fold - s : s;
        int64_t tl = block_time(p, ft);
        int bx = std::clamp(slide_pose(sp, ft).boundary, kPaneSplit, kPortraitW);
        int x_end = std::max(bx, kPaneLeftEdge);
        fill(s1, kSentinel);
        block_compose(as, bs, s1, p, ft, tl, bx, x_end, kGround);
        for (int y = 0; y < s1.h && r.written; y++)
            for (int x = 0; x < x_end; x++)
                if (s1.at(x, y) == kSentinel) {
                    r.written = false;
                    break;
                }
        if (n % 4 == 0) {
            int64_t near = std::min(fold, ft + 16);
            fill(s2, kGround);
            block_compose(as, bs, s2, p, near, block_time(p, near), bx, x_end, kGround);
            block_compose(as, bs, s2, p, ft, tl, bx, x_end, kGround);
            if (!region_is(s2, s1, 0, x_end))
                r.pure = false;
        }
        int l = 0, m = 0;
        area_of(s1, x_end, l, m);
        ls.push_back(l);
        ms.push_back(m);
        if (n == 0) {
            area_of(from, x_end, start_l, start_m);
            if (!region_is(s1, from, 0, x_end))
                r.lands = false;
        }
        if (n == frames) {
            int e = c.collapse ? std::min(x_end, kPaneLeftEdge) : x_end;
            area_of(to, e, end_l, end_m);
            if (!region_is(s1, to, 0, e) || !region_ground(s1, e, x_end, 0, s1.h))
                r.lands = false;
        }
        if (block_rest(p, ft, tl) != 0)
            continue;
        std::vector<BlockPose> poses = block_poses(p, ft, tl, bx);
        for (size_t i = 0; i < poses.size(); i++) {
            const BlockPose& q = poses[i];
            const Block& k = p.blocks[(size_t)q.block];
            Rect box = block_box(p, q);
            if (q.top && !same_rect(q.rect, k.cell))
                r.kept = false;
            if (q.top && !c.redeal && !(c.collapse && k.wide && k.col == kGridColsWide - 1))
                r.kept = false;
            if (!q.top && !q.beneath)
                for (size_t j = i + 1; j < poses.size(); j++)
                    if (!poses[j].top && !poses[j].beneath && poses[j].block != q.block &&
                        !box.intersect(block_box(p, poses[j])).empty())
                        r.apart = false;
            if (k.id == 101 && c.twin && !c.redeal)
                for (const BlockPose& o : poses)
                    if (o.block != q.block && p.blocks[(size_t)o.block].id == 101 && !block_box(p, o).empty() &&
                        !box.empty())
                        r.twin = true;
            if (k.art || box.empty())
                continue;
            auto reach = [&](const BlockPose& o) {
                const Block& ko = p.blocks[(size_t)o.block];
                bool covers = o.top;
                if (!covers && !ko.art && !(ko.focus && !o.ring.empty()))
                    return Rect{};
                return covers ? block_cover(p, o) : block_box(p, o);
            };
            for (int y = box.y0; y < box.y1; y++)
                for (int x = box.x0; x < box.x1; x++) {
                    bool other = false;
                    for (size_t j = 0; j < poses.size() && !other; j++)
                        if (j != i && poses[j].block != q.block && inside(reach(poses[j]), x, y))
                            other = true;
                    if (other)
                        continue;
                    uint32_t v = s1.at(x, y);
                    bool held = inside(q.rect, x, y) ? v == kGround : v == kBorder;
                    if (!held && r.artless) {
                        r.artless = false;
                        r.bad_x = x;
                        r.bad_y = y;
                        r.bad_v = v;
                        r.bad_block = q.block;
                    }
                }
        }
    }
    if (ls.front() != start_l || ms.front() != start_m || ls.back() != end_l || ms.back() != end_m)
        r.ends = false;
    int lo_l = std::min(start_l, end_l), hi_l = std::max(start_l, end_l);
    int lo_m = std::min(start_m, end_m), hi_m = std::max(start_m, end_m);
    for (size_t i = 0; i < ls.size(); i++) {
        r.dip = std::min({r.dip, ls[i] - lo_l, ms[i] - lo_m});
        r.rise = std::max({r.rise, ls[i] - hi_l, ms[i] - hi_m});
    }
    r.lower = r.dip >= -kAreaBar;
    r.upper = r.rise <= kAreaBar;
    return r;
}

}

int main()
{
    uint32_t g = LiveF::pack(kGround);

    check(fx_round(tok::motion_swapOut) == 50 && std_px(tok::motion_swapShift) == 68 &&
              fx_round(tok::motion_swapRows) == 150 && std_px(tok::motion_swapRowInShift) == 50 &&
              fx_round(tok::motion_swapRowFade) == 200 && fx_round(tok::motion_leave) == 133,
          "D's tokens: out 50 ms, shift 68 px, rows 150 ms, row in shift 50 px, row fade 200 ms, header 133 ms");
    check(swap_ms() == 250, "D ends at 250 ms");

    {
        SwapLeg s0 = swap_leave(0, 1), s50 = swap_leave(50, 1), sm = swap_leave(-10, 1);
        bool formula = true, mono = true;
        SwapLeg prev = s0;
        for (int64_t t = 1; t < 50; t++) {
            SwapLeg l = swap_leave(t, 1);
            fx s = curve(Curve::Strip, (fx)(t * kOne / 50));
            if (l.dx != -fx_round(std_to_px(fx_mul(fx_int(34), s))) || l.a256 != alpha256(kOne - s))
                formula = false;
            if (l.dx > prev.dx || l.a256 > prev.a256)
                mono = false;
            prev = l;
        }
        check(s0.dx == 0 && s0.a256 == 256 && sm.dx == 0 && sm.a256 == 256 && s50.dx == -68 && s50.a256 == 0,
              "the leave starts in place and opaque and ends 68 px left at alpha 0 at 50 ms");
        check(formula, "the leave runs x 0 to -68 px and o 1 to 0 on strip over 50 ms");
        check(mono, "the leave only moves left and only fades");
    }
    {
        SwapLeg a50 = swap_arrive(50, 1), a125 = swap_arrive(125, 1), a200 = swap_arrive(200, 1);
        SwapLeg a250 = swap_arrive(250, 1);
        bool formula = true, mono = true;
        SwapLeg prev = a50;
        for (int64_t t = 51; t <= 260; t++) {
            SwapLeg l = swap_arrive(t, 1);
            double k = (double)(t - 50);
            double dx = 50 * std::pow(1 - std::min(k, 150.0) / 150, 3);
            double a = 256 * std::min(k, 200.0) / 200;
            if (std::abs(l.dx - dx) > 1 || std::abs(l.a256 - a) > 1)
                formula = false;
            if (l.dx > prev.dx || l.a256 < prev.a256)
                mono = false;
            prev = l;
        }
        check(a50.dx == 50 && a50.a256 == 0 && a125.dx == 6 && a125.a256 == 96 && a200.dx == 0 &&
                  a200.a256 == 192 && a250.dx == 0 && a250.a256 == 256,
              "the rows come in from +50 px: 6 px and alpha 96 at 125, in place at 200, opaque at 250");
        check(formula, "the rows run dx +50 px to 0 on cubicOut over 150 ms and alpha 0 to 1 linear over 200 ms");
        check(mono, "the rows only move left and only fade in");
    }
    {
        bool mirror = true;
        for (int64_t t = -10; t <= 300; t++) {
            SwapLeg l = swap_leave(t, 1), lb = swap_leave(t, -1), r = swap_arrive(t, 1), rb = swap_arrive(t, -1);
            if (lb.dx != -l.dx || lb.a256 != l.a256 || rb.dx != -r.dx || rb.a256 != r.a256)
                mirror = false;
        }
        SwapLeg o50 = swap_leave(50, -1), i50 = swap_arrive(50, -1), i125 = swap_arrive(125, -1);
        SwapLeg i250 = swap_arrive(250, -1);
        check(mirror && o50.dx == 68 && o50.a256 == 0 && i50.dx == -50 && i50.a256 == 0 && i125.dx == -6 &&
                  i125.a256 == 96 && i250.dx == 0 && i250.a256 == 256,
              "dir -1: the rows go out 68 px right and come in from 50 px left on the same clock, end 250");
    }
    {
        bool linear = true, rise = true;
        for (int64_t t = 1; t < 133; t++)
            if (std::abs(swap_header(t, false) - 256.0 * (133 - t) / 133) > 1)
                linear = false;
        for (int64_t t = -10; t <= 300; t++)
            if (swap_header(t, true) != swap_arrive(t, -1).a256)
                rise = false;
        check(swap_header(0, false) == 256 && swap_header(133, false) == 0 && swap_header(400, false) == 0 && linear,
              "the header leaves o 1 to 0 linear over 133 ms");
        check(rise && swap_header(25, true) == 0 && swap_header(150, true) == 128 && swap_header(250, true) == 256,
              "collapse: the header comes back on the arrival's alpha, 0 to 1 linear over 200 from 50");
    }
    {
        double c = cubic_out(kOne / 2) / (double)kOne;
        check(cubic_out(0) == 0 && cubic_out(kOne) == kOne && std::fabs(c - 0.875) < 0.001,
              "cubicOut is 1 - (1 - t)^3");
    }

    Buffer ra, rb;
    collapsed(ra.surface(), 0);
    expanded(rb.surface(), kTo, 12, 0, 0);
    Surface as = ra.surface(), bs = rb.surface();
    Rect f0 = grid_tile_rect(0, kGridCols).grow(kTileFocusBorder);
    {
        LeftPlan p = plan_for(f0, f0);
        swap_prepare(p, as, bs);
        check(p.prepared && same_rects(p.kept, {f0, grid_tile_rect(1, kGridCols)}),
              "Recent in, re-dealt: games 0 and 1 stay, game 0 with its border");
        check(same_rects(p.leaves, {{120, 238, 432, 392}, {120, 396, 432, 550}, {120, 554, 432, 708}}),
              "the leave is one band per row of the origin's changed slots");
        check(same_rects(p.bands,
                         {{436, 80, 590, 234}, {120, 238, 590, 392}, {120, 396, 590, 550}, {120, 554, 590, 708}}),
              "the arrival is one band per row of the destination's changed slots, row 0 only its third column");
        check(same_rect(p.header, {120, 0, 300, 76}), "the header leaves from its rect left of 436");
        ComposeResult r = compose_all(as, bs, p, g);
        check(r.model, "every frame is ground, the kept slots, the fading header and the moving rows over ground");
        check(r.sentinel, "a sentinel full write: every pixel left of b is written at every t");
        check(r.right, "nothing is written at or right of b");
        check(r.kept, "the kept slots equal the destination at every t");

        Buffer d;
        swap_compose(as, bs, d.surface(), p, 0, 648, g);
        bool origin = true, dest = true;
        for (int y = 0; y < 720; y++)
            for (int x = 0; x < 648; x++)
                if (d.surface().at(x, y) != as.at(x, y))
                    origin = false;
        swap_compose(as, bs, d.surface(), p, 250, 648, g);
        for (int y = 0; y < 720; y++)
            for (int x = 0; x < 648; x++)
                if (d.surface().at(x, y) != bs.at(x, y))
                    dest = false;
        check(origin && dest, "forward time 0 is the origin and 250 the destination, exact copies");

        swap_compose(as, bs, d.surface(), p, 49, 648, g);
        SwapLeg l49 = swap_leave(49, 1);
        bool end = d.surface().at(200, 300) == blend_px(g, as.at(200 - l49.dx, 300), l49.a256) && l49.a256 < 16;
        check(end, "the leave is nearly gone at 49 ms");
        swap_compose(as, bs, d.surface(), p, 50, 648, g);
        bool gap = d.surface().at(200, 300) == g && d.surface().at(500, 150) == g &&
                   d.surface().at(150, 150) == bs.at(150, 150);
        check(gap, "at 50 ms the old rows are gone and the new ones not yet in, the kept slots stay");
        swap_compose(as, bs, d.surface(), p, 25, 648, g);
        SwapLeg l25 = swap_leave(25, 1);
        bool left = l25.dx < 0 && d.surface().at(120 + l25.dx, 300) == blend_px(g, as.at(120, 300), l25.a256) &&
                    d.surface().at(119 + l25.dx, 300) == g;
        check(left, "at 25 ms a leaving row shows left of its band's x0, where its shift puts it, over ground");
    }
    for (Rect band : {kBand, kScreen}) {
        LeftPlan p = plan_for(f0, f0);
        p.band = band;
        swap_prepare(p, as, bs);
        bool below = true;
        for (const Rect& r : p.bands)
            below = below && r.y0 >= kBandTop;
        Buffer d;
        bool uniform = true;
        for (int64_t t : {67, 100}) {
            swap_compose(as, bs, d.surface(), p, t, 648, g);
            uint32_t want = blend_px(g, as.at(124, 20), swap_header(t, false));
            for (int y = 20; y < 70; y++)
                for (int x = 124; x < 296; x++)
                    if (d.surface().at(x, y) != want)
                        uniform = false;
        }
        check(below && uniform, band.y0 == kBandTop
                                    ? "the header fades as a whole at 67 and 100 ms, no row reaches above y 76"
                                    : "with the full screen as the clip the header still fades as a whole");
    }
    {
        LeftPlan p = plan_for(f0, f0);
        p.collapse = true;
        swap_prepare(p, as, bs);
        LeftPlan e = plan_for(f0, f0);
        swap_prepare(e, as, bs);
        check(same_rects(p.kept, e.kept) && same_rects(p.leaves, e.leaves) && same_rects(p.bands, e.bands),
              "collapse: the same kept slots and rows as the expand");
        ComposeResult r = compose_all(as, bs, p, g);
        check(r.model && r.sentinel && r.right && r.kept,
              "collapse: the expanded rows leave right and the collapsed rows arrive from the left, over ground");
        Buffer d;
        Surface ds = d.surface();
        bool before = true, after = true;
        for (int x_end : {436, 520, 648}) {
            for (int64_t k : {-100, -1}) {
                fill(ds, kSentinel);
                swap_compose(as, bs, ds, p, k, x_end, g);
                for (int y = 0; y < 720; y++)
                    for (int x = 0; x < x_end; x++)
                        if (ds.at(x, y) != bs.at(x, y))
                            before = false;
            }
            for (int64_t k : {250, 300}) {
                fill(ds, kSentinel);
                swap_compose(as, bs, ds, p, k, x_end, g);
                for (int y = 0; y < 720; y++)
                    for (int x = 0; x < x_end; x++)
                        if (ds.at(x, y) != (x < kPaneLeftEdge ? as.at(x, y) : g))
                            after = false;
            }
        }
        check(before, "collapse: before its clock the left pane is the presented expanded frame over [0, x_end)");
        check(after, "collapse: from 250 the collapsed destination over [0, 436) and ground to x_end");
        Masks m{{f0, grid_tile_rect(1, kGridCols)},
                {{120, 238, 432, 392}, {120, 396, 432, 550}, {120, 554, 432, 708}},
                {{436, 80, 590, 234}, {120, 238, 590, 392}, {120, 396, 590, 550}, {120, 554, 590, 708}},
                {120, 0, 300, 76}};
        swap_compose(as, bs, ds, p, 25, 648, g);
        bool k25 = matches(as, bs, m, true, ds, 25, 648, g) && swap_leave(25, -1).dx > 0 &&
                   ds.at(150, 150) == as.at(150, 150) && ds.at(150, 30) == g;
        swap_compose(as, bs, ds, p, 150, 648, g);
        bool k150 = matches(as, bs, m, true, ds, 150, 648, g) && swap_arrive(150, -1).dx < 0 &&
                    ds.at(150, 30) == blend_px(g, as.at(150, 30), 128);
        check(k25, "collapse at k = 25: the expanded rows shifted right and faded, kept slots unchanged, no header");
        swap_compose(as, bs, ds, p, 25, 720, g);
        SwapLeg r25 = swap_leave(25, -1);
        bool right = r25.dx > 0 && ds.at(589 + r25.dx, 300) == blend_px(g, bs.at(589, 300), r25.a256) &&
                     ds.at(590 + r25.dx, 300) == g;
        check(right, "collapse at k = 25: a leaving row shows right of its band's x1, where its shift puts it");
        check(k150, "collapse at k = 150: the collapsed rows from the left, the header at half its alpha");
    }
    {
        LeftPlan p = plan_for(f0, f0);
        swap_prepare(p, as, bs);
        std::vector<Rect> kept = p.kept, leaves = p.leaves, bands = p.bands;
        Buffer nb;
        expanded(nb.surface(), kTo, 12, 0, 0);
        Canvas(nb.surface()).fill(grid_tile_rect(1, 3), 0x445566);
        Canvas(nb.surface()).fill(grid_tile_rect(5, 3), 0x000000);
        swap_prepare(p, as, nb.surface());
        check(same_rects(p.kept, kept) && same_rects(p.leaves, leaves) && same_rects(p.bands, bands),
              "the change set and the rows are fixed at the first prepare");
        Buffer d;
        swap_compose(as, nb.surface(), d.surface(), p, 100, 648, g);
        check(d.surface().at(300, 150) == 0x445566, "a kept slot redrawn in the destination shows from the next frame");
    }
    {
        Buffer sb;
        expanded(sb.surface(), kSys, 12, 0, 0);
        LeftPlan p = plan_for(Rect{}, grid_tile_rect(0, 3).grow(kTileFocusBorder));
        swap_prepare(p, as, sb.surface());
        check(p.kept.empty() && p.leaves.size() == 4 && p.bands.size() == 4 &&
                  same_rect(p.bands[0], {116, 76, 590, 238}),
              "system enter: every slot changes, the focused tile's band carries its border");
        ComposeResult r = compose_all(as, sb.surface(), p, g);
        check(r.model && r.sentinel && r.right, "system enter: every frame matches the row swap over ground");
        LeftPlan c = p;
        c.collapse = true;
        r = compose_all(as, sb.surface(), c, g);
        check(r.model && r.sentinel && r.right, "system exit: every frame matches the back row swap over ground");
    }
    {
        Buffer sb;
        int gy = -276;
        expanded(sb.surface(), kSys, 16, -1, gy);
        LeftPlan p = plan_for(Rect{}, Rect{});
        p.grid_y = gy;
        swap_prepare(p, as, sb.surface());
        check(same_rects(p.bands, {{120, 76, 590, 116},
                                   {120, 120, 590, 274},
                                   {120, 278, 590, 432},
                                   {120, 436, 590, 590},
                                   {120, 594, 590, 712}}),
              "a collapse from a scrolled grid takes its rows at the drawn offset, cut by the band");
        LeftPlan c = p;
        c.collapse = true;
        ComposeResult r = compose_all(as, sb.surface(), c, g);
        check(r.model && r.sentinel && r.right, "the scrolled rows leave right over ground");
    }
    {
        Buffer lb;
        large(lb.surface());
        LeftPlan p = plan_for(Rect{}, Rect{});
        p.grid = grid_spec(TileMode::Large);
        swap_prepare(p, as, lb.surface());
        check(p.kept.empty() &&
                  same_rects(p.bands, {{120, 80, 600, 312}, {120, 328, 600, 560}, {120, 576, 600, 712}}),
              "large: the rows are 2 columns of 232, the third cut by the band");
        ComposeResult r = compose_all(as, lb.surface(), p, g);
        check(r.model && r.sentinel && r.right, "large: every frame matches the row swap over ground");
        LeftPlan c = p;
        c.collapse = true;
        r = compose_all(as, lb.surface(), c, g);
        check(r.model && r.sentinel && r.right, "large: every collapse frame matches the back row swap over ground");
    }

    {
        BlockCase c;
        c.recent = 12;
        c.sys = 16;
        SlidePlan sp = block_slide(c);
        bool steps = true;
        const int want[] = {0, 6, 25, 57, 101, 158};
        for (int f = 0; f <= 5; f++) {
            int64_t tl = 50 * f;
            std::vector<BlockPose> q = block_poses(sp.moves, (tl + 2) / 3, tl, kPaneSplit);
            if (f > 0 && (q.empty() || q[0].rect.x0 != kGridX - want[f]))
                steps = false;
        }
        check(steps, "stepper swap: one pitch in 5 frames, EaseIn t^2: 6, 25, 57, 101, 158 px");
        check(block_thirds(sp.moves) == 500, "stepper swap: D is two beats of 250/3 ms");
    }
    {
        bool beats = true;
        const int64_t want[] = {0, 0, 0, 250, 250, 500, 500, 750, 750, 750, 750, 750, 750};
        for (int n = 0; n <= 12; n++) {
            BlockCase c;
            c.recent = n;
            c.redeal = true;
            SlidePlan sp = block_slide(c);
            if (block_thirds(sp.moves) != want[n])
                beats = false;
        }
        check(beats, "re-deal: D is 0 to n 2, one beat to 4, two to 6, three to 12");
        BlockCase c;
        c.recent = 12;
        c.redeal = true;
        SlidePlan sp = block_slide(c);
        bool tunnel = false;
        for (int64_t tl = 1; tl < 750 && !tunnel; tl++) {
            std::vector<BlockPose> q = block_poses(sp.moves, (tl + 2) / 3, tl, kPaneSplitWide);
            for (size_t i = 0; i + 1 < q.size(); i++)
                if (q[i].block == q[i + 1].block && q[i + 1].rect.x0 < kGridX && q[i].rect.x0 > kPaneSplit)
                    tunnel = true;
        }
        check(tunnel, "re-deal: a block off a row's left end comes in at the right end of the row above, in two parts");
        bool sum = true, seen = false;
        for (int64_t tl = 1; tl < 750; tl++) {
            std::vector<BlockPose> q = block_poses(sp.moves, (tl + 2) / 3, tl, kPaneSplitWide);
            for (size_t i = 0; i + 1 < q.size(); i++)
                if (q[i].block == q[i + 1].block && q[i + 1].rect.y1 <= q[i + 1].clip.y1) {
                    Rect a = q[i].rect.intersect(q[i].clip), b = q[i + 1].rect.intersect(q[i + 1].clip);
                    int wa = a.empty() ? 0 : a.x1 - a.x0, wb = b.empty() ? 0 : b.x1 - b.x0;
                    seen = true;
                    if (wa + wb != kTileSide || q[i].clip.x1 > kGridX + 3 * kGridRowPitch - kTileFocusBorder ||
                        q[i].ring.x1 != kGridX + 3 * kGridRowPitch)
                        sum = false;
                }
        }
        check(seen && sum, "re-deal: the two parts of a tunnelling block show 154 px between them, the column 2 part "
                           "cut at 590 and its ring at 594");
    }
    {
        Buffer ra, rb, rd;
        BlockCase c;
        c.recent = 12;
        c.sys = 16;
        SlidePlan sp = block_slide(c);
        paint_side(ra.surface(), sp.moves, false);
        paint_side(rb.surface(), sp.moves, true);
        int64_t ft = 67;
        int bx = std::clamp(slide_pose(sp, ft).boundary, kPaneSplit, kPortraitW);
        block_compose(ra.surface(), rb.surface(), rd.surface(), sp.moves, ft, block_time(sp.moves, ft), bx,
                      std::max(bx, kPaneLeftEdge), kGround);
        uint32_t want = blend_px(kGround, kHeadInk, alpha256(kOne - (fx)(ft * kOne / 133)));
        check(rd.surface().at(150, 30) == want, "the header recent fades 0.6 to 0 over 133 ms on an expand (C1)");
        BlockCase cb = c;
        cb.collapse = true;
        SlidePlan bp = block_slide(cb);
        ft = 800 - 700;
        bx = std::clamp(slide_pose(bp, ft).boundary, kPaneSplit, kPortraitW);
        block_compose(ra.surface(), rb.surface(), rd.surface(), bp.moves, ft, block_time(bp.moves, ft), bx,
                      std::max(bx, kPaneLeftEdge), kGround);
        check(rd.surface().at(150, 30) == blend_px(kGround, kHeadInk, alpha256(kOne - (fx)(ft * kOne / 133))),
              "the header fades back in over s 667 to 800 on a collapse, ending with the fold (C1)");
    }
    {
        BlockCase c;
        c.recent = 12;
        c.sys = 3;
        c.artless_sys = {2};
        SlidePlan sp = block_slide(c);
        bool empty = sp.moves.full_rows.empty() || !sp.moves.full_rows[0];
        BlockCase f = c;
        f.artless_sys.clear();
        SlidePlan fp = block_slide(f);
        bool full = !fp.moves.full_rows.empty() && fp.moves.full_rows[0];
        check(empty && full, "a row whose column 2 tile has no cover takes the column 2 slot, as an empty one does");
        std::vector<BlockPose> before = block_poses(sp.moves, 50, 150, kPaneSplitWide);
        for (Block& k : sp.moves.blocks)
            k.art = true;
        std::vector<BlockPose> after = block_poses(sp.moves, 50, 150, kPaneSplitWide);
        bool fixed = before.size() == after.size();
        for (size_t i = 0; fixed && i < before.size(); i++)
            fixed = same_rect(before[i].clip, after[i].clip);
        check(fixed, "a cover that arrives mid fold draws but does not change the slot clip set at plan time");
    }
    {
        BlockCase c;
        c.recent = 12;
        c.sys = 3;
        c.artless_sys = {2};
        SlidePlan sp = block_slide(c);
        for (Block& k : sp.moves.blocks)
            if (k.wide && k.game == 2)
                k.art = true;
        bool shown = true, met = false;
        for (int64_t tl = 1; tl < block_thirds(sp.moves); tl++)
            for (const BlockPose& q : block_poses(sp.moves, (tl + 2) / 3, tl, kPaneSplitWide)) {
                const Block& k = sp.moves.blocks[(size_t)q.block];
                if (!k.wide || k.game != 2 || q.rect.intersect({kPaneLeftEdge, 0, 594, kPortraitH}).empty())
                    continue;
                met = true;
                if (q.rect.intersect(q.clip).empty())
                    shown = false;
            }
        check(met && shown, "stepper: the slot never clips a row's own column 2 block, a late cover shows (N1)");
    }
    {
        BlockCase c;
        c.recent = 12;
        c.sys = 16;
        c.collapse = true;
        SlidePlan sp = block_slide(c);
        slide_lang(sp, pane_surface());
        Buffer ra, rb, rd;
        paint_side(ra.surface(), sp.moves, false);
        paint_side(rb.surface(), sp.moves, true);
        bool gutter = true;
        int passes = 0;
        for (int n = 0; frame_ms(n) <= sp.moves.fold; n++) {
            int64_t ft = sp.moves.fold - frame_ms(n);
            int64_t tl = block_time(sp.moves, ft);
            if (block_rest(sp.moves, ft, tl) != 0)
                continue;
            int bx = std::clamp(slide_pose(sp, ft).boundary, kPaneSplit, kPortraitW);
            block_compose(ra.surface(), rb.surface(), rd.surface(), sp.moves, ft, tl, bx, std::max(bx, kPaneLeftEdge),
                          kGround);
            for (const BlockPose& q : block_poses(sp.moves, ft, tl, bx))
                if (sp.moves.blocks[(size_t)q.block].wide && q.rect.y0 == kTileFirstTop && q.rect.x1 > 590 &&
                    q.rect.x0 < 594 && !q.top)
                    passes++;
            if (!region_ground(rd.surface(), 590, 594, kTileFirstTop, kTileFirstTop + kTileSide))
                gutter = false;
        }
        check(passes > 0 && gutter, "stepper exit: a held row's [590, 594) stays ground while a block passes it");
    }
    {
        bool once = true, read = true;
        {
            BlockCase c;
            c.recent = 8;
            c.sys = 12;
            GlowDrive a(c), b(c);
            for (int n = 0; n <= 48; n++) {
                int64_t now = a.t0 + frame_ms(n);
                a.frame(now);
                b.frame(now);
                std::vector<uint16_t> e = b.glow().e;
                b.frame(now + 9, false);
                read = read && b.glow().e == e;
                b.frame(now);
                once = once && b.glow().e == a.glow().e &&
                       region_is(a.rd.surface(), b.rd.surface(), 0, a.x_end(a.ft_at(now)));
            }
        }
        check(read, "C7: a compose that does not advance reads e and leaves it as it was");
        check(once, "C7: e moves once per frame, a second compose at the same time draws the same frame");
    }
    {
        bool settled = true, exact = true;
        int landings = 0;
        for (bool col : {false, true})
            for (int cad : {0, 176}) {
                BlockCase c;
                c.recent = 12;
                c.sys = 16;
                c.collapse = col;
                GlowDrive d(c);
                for (int n = 0; n < 100; n++) {
                    int64_t now = d.t0 + (cad ? (int64_t)n * cad / 10 : frame_ms(n));
                    int64_t ft = d.ft_at(now);
                    d.frame(now);
                    if (block_rest(d.sp.moves, ft, block_time(d.sp.moves, ft)) <= 0)
                        continue;
                    landings++;
                    int x_end = d.x_end(ft);
                    int keep = col ? std::min(x_end, kPaneLeftEdge) : x_end;
                    Surface to = col ? d.ra.surface() : d.rb.surface();
                    settled = settled && block_settled(d.sp.moves);
                    exact = exact && region_is(d.rd.surface(), to, 0, keep) &&
                            region_ground(d.rd.surface(), keep, x_end, 0, kPortraitH);
                    break;
                }
            }
        check(landings == 4 && settled,
              "C7 tail: a forward fold's left pane has every e at T on its landing frame, at 60 fps and 17.6 ms");
        check(exact, "C7 tail: the landing frame is the destination rest, exact");
    }
    {
        bool settled = true, equal = true;
        int landings = 0;
        for (bool reverse : {false, true}) {
            BlockCase c;
            c.recent = 12;
            c.redeal = true;
            c.collapse = !reverse;
            GlowDrive d(c);
            Buffer menu;
            menu_side(menu.surface(), d.sp.moves);
            for (int n = 0; n < 100; n++) {
                int64_t now = d.t0 + frame_ms(n);
                if (reverse && n == 12)
                    d.back(now);
                d.frame(now);
                if (!d.landed(now))
                    continue;
                landings++;
                settled = settled && block_settled(d.sp.moves);
                equal = equal && band_is(d.rd.surface(), menu.surface(), 0, kPaneLeftEdge);
                break;
            }
        }
        check(landings == 2 && settled, "C8: a details landing has every e at T, Recent out and reversed Recent in");
        check(equal, "C8: a details landing equals the menu focus rest, dimmed to 0.6 with no ring, no one frame fall");
    }
    {
        bool held = true, settled = true;
        int seen = 0;
        for (int back_n : {6, 12, 18}) {
            BlockCase c;
            c.recent = 8;
            c.sys = 12;
            GlowDrive d(c);
            for (int n = 0; n < 100; n++) {
                int64_t now = d.t0 + frame_ms(n);
                if (n == back_n)
                    d.back(now);
                std::vector<uint16_t> pe = d.glow().e;
                std::vector<uint8_t> pid = d.glow().ids;
                d.frame(now);
                if (d.back_e < 0)
                    continue;
                if (d.landed(now)) {
                    settled = settled && block_settled(d.sp.moves);
                    break;
                }
                const Glow& gl = d.glow();
                for (size_t i = 0; i < gl.e.size() && i < pe.size(); i++) {
                    int id = gl.ids[i], was = pid[i];
                    if (!departing(d.sp.moves, id))
                        continue;
                    seen++;
                    int cap = was && !(was & kGlowRing) ? pe[i] : gl.back[(size_t)(id & 0x7F) - 1];
                    if (gl.e[i] > cap)
                        held = false;
                }
            }
        }
        check(seen > 0 && held, "C7 (g): after Back a departing pixel draws at most its e the frame before, a newly "
                                "covered one at most its block's lowest e at Back");
        check(settled, "C7: a reversal lands with every e at T inside the 150 ms tail");
    }
    {
        bool at_t = true;
        int resumed = 0;
        {
            BlockCase c;
            c.recent = 12;
            c.sys = 16;
            c.collapse = true;
            GlowDrive d(c);
            for (int n = 0; n <= 48; n++) {
                std::vector<uint16_t> pe = d.glow().e;
                std::vector<uint8_t> pid = d.glow().ids;
                d.frame(d.t0 + frame_ms(n));
                const Glow& gl = d.glow();
                for (size_t i = 0; i < gl.e.size() && i < pe.size(); i++) {
                    int id = gl.ids[i], was = pid[i];
                    if (!arriving(d.sp.moves, id) || !was || (was & kGlowRing) || was == id || pe[i] <= kGlowDim)
                        continue;
                    resumed++;
                    if (gl.e[i] != kGlowDim)
                        at_t = false;
                }
            }
        }
        check(resumed > 0 && at_t, "C7 (i): art arriving over brighter art on an exit draws at its own T at once");
    }
    {
        bool same = true;
        int frames = 0;
        for (bool redeal : {false, true}) {
            BlockCase c;
            c.recent = 8;
            c.sys = redeal ? 0 : 12;
            c.redeal = redeal;
            GlowDrive f(c), r(c);
            std::vector<std::vector<uint8_t>> ids;
            for (int n = 0; n <= 12; n++) {
                f.frame(f.t0 + frame_ms(n));
                ids.push_back(f.glow().ids);
                r.frame(r.t0 + frame_ms(n));
            }
            r.back(r.t0 + frame_ms(12));
            for (int j = 12; j >= 1; j--) {
                int64_t now = r.back_t0 + frame_ms(12) - frame_ms(j);
                r.frame(now);
                frames++;
                if (r.ft_at(now) != frame_ms(j) || r.glow().ids != ids[(size_t)j])
                    same = false;
            }
        }
        check(frames > 0 && same, "reversal: every reversed frame draws the block id map of the forward frame at its "
                                  "forward time, the geometry is the forward fold's");
    }
    {
        BlockCase c;
        c.recent = 3;
        c.sys = 16;
        SlidePlan sp = block_slide(c);
        const LeftPlan& p = sp.moves;
        int64_t d3 = block_thirds(p);
        int old = -1;
        for (size_t i = 0; i < p.blocks.size(); i++)
            if (!p.blocks[i].wide && p.blocks[i].row == 1)
                old = (int)i;
        bool one = old >= 0, exits = false, starts = true, beneath = false;
        for (int64_t tl = 0; tl <= d3; tl = tl == d3 ? d3 + 1 : std::min(d3, tl + 3))
            for (const BlockPose& q : block_legs(p, tl, kPaneSplitWide)) {
                const Block& k = p.blocks[(size_t)q.block];
                if (q.block == old) {
                    int dx = q.rect.x0 - k.cell.x0;
                    if (dx > 0 || dx < -kGridRowPitch || q.rect.y0 != k.cell.y0)
                        one = false;
                    if (tl == d3)
                        exits = dx == -kGridRowPitch && q.rect.x1 == kGridX - kTileFocusBorder;
                }
                if (tl != 0 || !k.wide || k.row != 1)
                    continue;
                if (k.col == 0)
                    beneath = q.beneath && same_rect(q.rect, k.cell);
                else if (q.rect.x0 != kGridX + (k.col + 1) * kGridRowPitch)
                    starts = false;
            }
        Buffer ra, rb, rd;
        paint_side(ra.surface(), p, false);
        paint_side(rb.surface(), p, true);
        block_paint(ra.surface(), rb.surface(), rd.surface(), p, block_legs(p, 0, kPaneSplitWide),
                    kPaneSplitWide, kGround);
        bool covered = old >= 0;
        Rect cell = covered ? p.blocks[(size_t)old].cell : Rect{};
        for (int y = cell.y0; y < cell.y1 && covered; y++)
            for (int x = cell.x0; x < cell.x1; x++)
                if (rd.surface().at(x, y) != block_ink(p.blocks[(size_t)old]))
                    covered = false;
        bool area = true;
        for (int recent : {1, 3, 5, 7})
            for (int sys : {12, 16}) {
                BlockCase lc = c;
                lc.recent = recent;
                lc.sys = sys;
                BlockRun r = run_blocks(lc);
                area = area && r.lower && r.upper && r.apart && r.legs && r.ends;
            }
        BlockCase ac = c;
        ac.artless = {2};
        BlockRun ar = run_blocks(ac);
        bool lone_geom = ar.apart && ar.legs && ar.ends;
        std::printf("  %s case recent 3 sys 16 game 2 artless: Q-area reported, not gated: dip %d rise %d\n",
                    "stepper", ar.dip, ar.rise);
        SlidePlan ap = block_slide(ac);
        bool bare = true;
        for (const BlockPose& q : block_legs(ap.moves, 0, kPaneSplitWide)) {
            const Block& k = ap.moves.blocks[(size_t)q.block];
            if (q.beneath || (k.wide && k.row == 1 && k.col == 0 && q.rect.x0 != k.cell.x0 + 2 * kGridRowPitch))
                bare = false;
        }
        const char* f = "stepper";
        char line[200];
        std::snprintf(line, sizeof line, "%s lone row: an artless Recent block's row conveys 2 cells, none rests", f);
        check(bare, line);
        std::snprintf(line, sizeof line, "%s lone row: the old block conveys exactly 1 cell and exits at x 116", f);
        check(one && exits, line);
        std::snprintf(line, sizeof line, "%s lone row: the column 0 block rests beneath, fully covered at t_l 0", f);
        check(beneath && covered, line);
        std::snprintf(line, sizeof line, "%s lone row: columns 1 and 2 start at x 436 and 594", f);
        check(starts, line);
        std::snprintf(line, sizeof line, "%s lone row: Recent 1, 3, 5, 7 to systems 12 and 16 pass Q-area", f);
        check(area, line);
        std::snprintf(line, sizeof line, "%s lone row: Recent 3, game 2 artless: legs at t_l 0 and D, ends, apart", f);
        check(lone_geom, line);
    }
    {
        std::vector<BlockCase> cases;
        auto add = [&](int recent, int sys, bool redeal, bool collapse) {
            BlockCase c;
            c.recent = recent;
            c.sys = sys;
            c.redeal = redeal;
            c.collapse = collapse;
            cases.push_back(c);
            return &cases.back();
        };
        for (bool back : {false, true}) {
            for (int r : {0, 3, 12})
                add(r, 16, false, back);
            add(12, 1, false, back);
            add(12, 4, false, back);
            for (int n : {1, 2, 3, 5, 8, 9, 12})
                add(n, 0, true, back);
            add(12, 0, true, back)->artless = {3, 9};
            add(12, 16, false, back)->artless = {3, 9};
            add(5, 12, false, back)->artless = {0};
            const int twins[][2] = {{2, 2}, {3, 2}, {5, 4}, {12, 16}};
            for (const auto& rs : twins)
                add(rs[0], rs[1], false, back)->twin = true;
            add(12, 3, false, back)->artless_sys = {2};
            add(12, 6, false, back)->artless_sys = {5};
        }
        BlockCase* sc = add(12, 22, false, true);
        sc->grid_y = -2 * kGridRowPitch;
        sc->focus = 6;
        BlockCase* gc = add(12, 22, false, true);
        gc->grid_y = -2 * kGridRowPitch + kGridRowPitch / 2;
        gc->focus = 6;
        bool written = true, pure = true, legs = true, ends = true, lands = true, lower = true, upper = true,
             apart = true;
        bool kept = true, artless = true, twin = true;
        int dip = 0, rise = 0;
        for (const BlockCase& c : cases) {
            BlockRun r = run_blocks(c);
            written = written && r.written;
            pure = pure && r.pure;
            legs = legs && r.legs;
            ends = ends && r.ends;
            lands = lands && r.lands;
            bool gated = c.artless.empty();
            lower = lower && (r.lower || !gated);
            upper = upper && (r.upper || !gated);
            apart = apart && r.apart;
            kept = kept && r.kept;
            artless = artless && r.artless;
            if (c.twin && !r.twin)
                twin = false;
            if (gated) {
                dip = std::min(dip, r.dip);
                rise = std::max(rise, r.rise);
            }
            if (!r.legs || !r.artless)
                std::printf("  %s case recent %d sys %d %s %s artless %zu artless_sys %zu: legs %d (leg %d first diff "
                            "x %d y %d got %06x want %06x) artless %d (first bad x %d y %d value %06x block "
                            "%d)\n",
                            "stepper", c.recent,
                            c.sys, c.redeal ? "re-deal" : "swap", c.collapse ? "collapse" : "expand", c.artless.size(),
                            c.artless_sys.size(), r.legs ? 1 : 0, r.leg, r.leg_x, r.leg_y, r.leg_got, r.leg_want,
                            r.artless ? 1 : 0, r.bad_x, r.bad_y, r.bad_v, r.bad_block);
            if (!gated)
                std::printf("  %s case recent %d sys %d %s %s artless %zu artless_sys %zu: Q-area reported, not gated: "
                            "dip %d rise %d\n",
                            "stepper", c.recent,
                            c.sys, c.redeal ? "re-deal" : "swap", c.collapse ? "collapse" : "expand", c.artless.size(),
                            c.artless_sys.size(), r.dip, r.rise);
            else if (!r.lower || !r.upper)
                std::printf("  %s case recent %d sys %d %s %s grid_y %d: dip %d rise %d\n",
                            "stepper", c.recent,
                            c.sys, c.redeal ? "re-deal" : "swap", c.collapse ? "collapse" : "expand", c.grid_y, r.dip,
                            r.rise);
        }
        const char* f = "stepper";
        char line[200];
        std::snprintf(line, sizeof line, "%s: every frame writes the whole left pane", f);
        check(written, line);
        std::snprintf(line, sizeof line, "%s: a frame composed over a stale frame equals it composed fresh", f);
        check(pure, line);
        std::snprintf(line, sizeof line, "%s: the legs at t_l 0 and D, rest branch bypassed, are the rest layouts", f);
        check(legs, line);
        std::snprintf(line, sizeof line, "%s: frame 0 and the last frame are the rest layouts", f);
        check(ends && lands, line);
        std::snprintf(line, sizeof line, "%s: Q-area lower side, all-cover cases, worst dip %d px, bar min(ends) - 8k",
                      f, dip);
        check(lower, line);
        std::snprintf(line, sizeof line, "%s: Q-area upper side, all-cover cases, worst rise %d px, bar max(ends) + 8k",
                      f, rise);
        check(upper, line);
        std::snprintf(line, sizeof line, "%s: no two moving blocks overlap", f);
        check(apart, line);
        std::snprintf(line, sizeof line, "%s: a game in both grids of a swap moves as two blocks", f);
        check(twin, line);
        std::snprintf(line, sizeof line, "%s: kept blocks never move, a swap keeps nothing but the column 2 hold", f);
        check(kept, line);
        std::snprintf(line, sizeof line, "%s: an artless block draws only its focus border", f);
        check(artless, line);
    }
    {
        BlockCase c;
        c.recent = 5;
        c.sys = 0;
        c.redeal = true;
        GlowDrive d(c);
        const char* tmp = std::getenv("TMPDIR");
        std::string tmpl = std::string(tmp && *tmp ? tmp : "/tmp") + "/blockie-dumpz-XXXXXX";
        std::vector<char> buf(tmpl.begin(), tmpl.end());
        buf.push_back('\0');
        std::string dir = mkdtemp(buf.data()) ? std::string(buf.data()) : std::string();
        bool same = !dir.empty(), sized = true, glowing = false, stale = false;
        int dumps = 0;
        for (int n = 0; n <= 24 && same; n++) {
            d.frame(d.t0 + frame_ms(n));
            const std::vector<uint8_t>& age = d.glow().age;
            std::string path = dir + "/g.z";
            std::vector<uint8_t> back(age.size(), 0x5A);
            same = !age.empty() && dump_z(path, age.data(), age.size()) &&
                   load_z(path, back.data(), back.size()) && back == age;
            sized = sized && !load_z(path, back.data(), back.size() - 1);
            for (uint8_t v : back) {
                glowing = glowing || (v & 0x80);
                stale = stale || (v & 0x7F) >= kGlowAgeMs;
            }
            std::remove(path.c_str());
            dumps++;
        }
        if (!dir.empty())
            rmdir(dir.c_str());
        check(same && sized && glowing && stale && dumps == 25,
              "dump: the glow age map round-trips through dump_z and load_z byte for byte, glowing bit and stale ages "
              "included; a read of the wrong size fails");
    }
    for (bool redeal : {false, true})
        for (int kind : {0, 1, 2}) {
            BlockCase c;
            c.recent = 12;
            c.sys = 16;
            c.redeal = redeal;
            c.collapse = kind == 1;
            LangDrive d(c);
            if (kind == 2)
                d.back_at = 150;
            Buffer small;
            collapsed_rest(small.surface(), d);
            const Surface& start = kind == 1 ? d.rb.surface() : small.surface();
            const Surface& end = kind == 0 ? d.rb.surface() : small.surface();
            bool first = false;
            for (int n = 0; n <= 60; n++) {
                d.frame(frame_ms(n));
                if (n == 0)
                    first = region_is(d.rd.surface(), start, 0, kPortraitW);
            }
            bool last = region_is(d.rd.surface(), end, 0, kPortraitW);
            const char* how = kind == 0 ? "expand" : kind == 1 ? "collapse" : "reversal at 150 ms";
            std::string what = std::string("lang ") + "step" + (redeal ? " re-deal " : " swap ") + how +
                               ": frame 0 is the start render and the last frame the end render, every column (a "
                               "re-deal's collapsed side at 0.6 with no ring)";
            check(first && last, what.c_str());
        }
    for (int64_t back_at : {222, 300}) {
        BlockCase c;
        c.recent = 9;
        c.redeal = true;
        LangDrive d(c);
        d.back_at = back_at;
        const LeftPlan& p = d.sp.moves;
        std::vector<size_t> at;
        std::vector<int> kept, moving, cells;
        for (size_t w = 0; w < p.blocks.size(); w++)
            for (size_t k = 0; k < p.blocks.size(); k++) {
                const Block& bw = p.blocks[w];
                const Block& bk = p.blocks[k];
                if (!bw.wide || bk.wide || bk.game != bw.game)
                    continue;
                if (bk.cell.x0 == bw.cell.x0 && bk.cell.y0 == bw.cell.y0) {
                    at.push_back(glow_at(p, (bw.cell.x0 + bw.cell.x1) / 2, (bw.cell.y0 + bw.cell.y1) / 2));
                    kept.push_back(bw.game);
                } else {
                    moving.push_back((int)w);
                    cells.push_back((int)k);
                }
            }
        std::vector<int> was(at.size(), -1), t_was(moving.size(), -1);
        std::vector<uint8_t> landed(moving.size(), 0);
        bool falls = true, held = false, dimmed = false, hold = true, ramp = true, above = false;
        char why[240] = "";
        auto first = [&](bool ok, const std::string& line) {
            if (!ok && !why[0])
                std::snprintf(why, sizeof why, "%s", line.c_str());
            return ok;
        };
        int64_t prev = 0, flip = -1;
        for (int n = 0; n <= 60; n++) {
            int64_t s = frame_ms(n);
            d.frame(s);
            if (flip < 0 && d.sp.moves.reversed)
                flip = s;
            const Glow& gl = p.glow;
            int step = (int)((int64_t)kGlowDimStep * 256 * 3 * (s - prev) / 50);
            bool after = flip >= 0 && s > flip;
            std::string at_n = " at frame " + std::to_string(n) + " (" + std::to_string(s) + " ms)";
            for (size_t j = 0; j < at.size(); j++) {
                int e = gl.e[at[j]];
                std::string tile = "kept tile game " + std::to_string(kept[j]) + at_n;
                if (after) {
                    falls = first(e <= was[j] && was[j] - e <= step + 1,
                                  tile + ": e " + std::to_string(e) + " after " + std::to_string(was[j]) +
                                      ", at most one dim step " + std::to_string(step) + " down") &&
                            falls;
                    bool still = was[j] == kGlowFull && e == kGlowFull;
                    first(!still, tile + ": e still at 1 after Back");
                    held = held || still;
                    dimmed = dimmed || e < was[j];
                }
                was[j] = e;
            }
            for (size_t j = 0; flip >= 0 && j < moving.size(); j++) {
                size_t w = (size_t)moving[j];
                const Rect& home = p.blocks[(size_t)cells[j]].cell;
                for (const BlockPose& q : gl.poses)
                    if ((q.block == (int)w || q.block == cells[j]) && q.rect.x0 == home.x0 &&
                        q.rect.y0 == home.y0)
                        landed[j] = 1;
                int t = gl.shades[w].t, b = gl.back_t[w];
                std::string tile = "moving tile game " + std::to_string(p.blocks[w].game) + at_n;
                if (!landed[j]) {
                    hold = first(t == b, tile + ": T " + std::to_string(t) + " before its landing, want back_t " +
                                             std::to_string(b)) &&
                           hold;
                    above = above || b > kGlowDim;
                } else if (t_was[j] >= 0) {
                    ramp = first(t <= t_was[j] && t_was[j] - t <= step + 1,
                                 tile + ": T " + std::to_string(t) + " after " + std::to_string(t_was[j]) +
                                     ", at most one dim step " + std::to_string(step) + " down") &&
                           ramp;
                }
                t_was[j] = t;
            }
            prev = s;
        }
        bool rest = first(!at.empty() && !moving.empty(), "no kept or no moving tile");
        for (size_t j = 0; j < at.size(); j++)
            rest = first(was[j] == kGlowDim, "kept tile game " + std::to_string(kept[j]) + " ends at e " +
                                                 std::to_string(was[j]) + ", want 0.6") &&
                   rest;
        for (size_t j = 0; j < moving.size(); j++)
            rest = first(landed[j] && t_was[j] == kGlowDim,
                         "moving tile game " + std::to_string(p.blocks[(size_t)moving[j]].game) + ": landed " +
                             std::to_string(landed[j]) + ", T " + std::to_string(t_was[j]) + " at the end") &&
                   rest;
        bool late = back_at > block_ms(p);
        first(dimmed, "no kept tile dimmed after Back");
        first(above || !late, "Back after D, yet no moving tile was above 0.6 at Back");
        Buffer end;
        collapsed_rest(end.surface(), d);
        Surface got = d.rd.surface(), want = end.surface();
        int dx = -1, dy = -1;
        for (int y = 0; y < got.h && dx < 0; y++)
            for (int x = 0; x < got.w && dx < 0; x++)
                if (got.at(x, y) != want.at(x, y)) {
                    dx = x;
                    dy = y;
                }
        bool lands = first(dx < 0, "landing differs from the rest at (" + std::to_string(dx) + ", " +
                                       std::to_string(dy) + ")");
        std::string what = std::string("lang ") + "step" + " re-deal n 9, Back at " +
                           std::to_string(back_at) +
                           " ms, the flip snap fixed: kept tiles dim from Back at the dim step, no snap and no "
                           "hold at 1, to 0.6; moving tiles hold their T at Back until they land on their "
                           "collapsed cell, then dim at the dim step; the landing equals the rest";
        bool ok = falls && !held && dimmed && hold && ramp && (above || !late) && rest && lands;
        if (!ok)
            std::printf("  %s\n", why);
        check(ok, what.c_str());
    }
    {
        BlockCase c;
        c.recent = 12;
        c.sys = 16;
        c.collapse = true;
        c.focus = 5;
        LangDrive d(c);
        Rect cell = grid_tile_rect(c.focus, kGridColsWide);
        Rect pr = d.sp.pane->rect();
        size_t pw = (size_t)(pr.x1 - pr.x0);
        bool rows = true, part = false, gone = false;
        for (int n = 0; n <= 60; n++) {
            int64_t s = frame_ms(n);
            d.frame(s);
            if (s >= d.sp.moves.fold)
                continue;
            const std::vector<uint8_t>& cov = d.sp.pane->cover();
            int shown = 0, hidden = 0;
            for (int y = cell.y0 - kTileFocusBorder; y < cell.y1 + kTileFocusBorder; y++) {
                int by = std::clamp(y, cell.y0, cell.y1 - 1);
                bool covered = cov[(size_t)(by - pr.y0) * pw + (size_t)(kPaneLeftEdge - pr.x0)];
                bool bar = d.rd.surface().at(kPaneSplit + 1, y) != kGround;
                rows = rows && bar == !covered;
                (bar ? shown : hidden)++;
            }
            part = part || (shown && hidden);
            gone = shown == 0;
        }
        bool lands = region_is(d.rd.surface(), d.ra.surface(), 0, kPortraitW);
        std::string what = std::string("lang ") + "step" +
                           " collapse, focus in column 2: no ring segment beside a covered block, the bar at x 432 to "
                           "436 shortens from the top as the plane comes down and is gone before the fold, and the "
                           "landing equals the rest";
        check(rows && part && gone && lands, what.c_str());
    }
    {
        int revealed = 0, plain = 0;
        bool ground = true, back = true;
        for (int kind : {0, 1, 2}) {
            BlockCase c;
            c.recent = 12;
            c.sys = kind == 2 ? 0 : 16;
            c.redeal = kind == 2;
            c.focus = 5;
            LangDrive d(c);
            d.back_at = kind == 1 ? 200 : kind == 2 ? 160 : -1;
            const Glow& gl = d.sp.moves.glow;
            for (int n = 0; n <= 60; n++) {
                std::vector<uint8_t> hid = gl.hid, was = gl.ids;
                d.frame(frame_ms(n));
                if (n == 0 || was.size() != gl.ids.size())
                    continue;
                int bound = (int)((int64_t)kGlowStepFull * 256 * 3 * (frame_ms(n) - frame_ms(n - 1)) / 50);
                for (size_t i = 0; i < gl.ids.size(); i++) {
                    int id = gl.ids[i];
                    if (!id)
                        continue;
                    const GlowShade& h = gl.shades[(size_t)(id & 0x7F) - 1];
                    if (i < hid.size() && hid[i]) {
                        revealed++;
                        ground = ground && gl.e[i] <= bound && gl.e[i] < kGlowFull;
                    } else if (gl.flip && !was[i] && h.dep) {
                        plain++;
                        back = back && gl.e[i] == std::min<int>(gl.back[(size_t)(id & 0x7F) - 1], h.t);
                    }
                }
            }
        }
        check(revealed > 0 && ground,
              "lang: a pixel hidden by a cover, directly or through the ring rule, takes the from-ground step when it "
              "shows again, on the lift's expand with and without a Back mid-lift (swap and re-deal), never at T and "
              "never by the 100 ms resume");
        check(plain > 0 && back, "lang: on a Back mid-lift, plain ground a departing block newly covers takes the "
                                 "block's lowest e at Back");
    }
    {
        BlockCase c;
        c.recent = 8;
        c.sys = 12;
        c.focus = 1;
        GlowDrive d(c);
        Rect r{kGridX + kGridRowPitch - 8, kBandTop, kGridX + kGridRowPitch + 80, kBandTop + 170};
        std::vector<uint8_t> mask((size_t)(r.x1 - r.x0) * (size_t)(r.y1 - r.y0), 1);
        d.sp.moves.cover = &mask;
        d.sp.moves.cover_rect = r;
        const Glow& gl = d.glow();
        int shown = 0, rings = 0;
        bool ground = true;
        for (int n = 0; n < 40; n++) {
            int64_t now = d.t0 + frame_ms(n);
            if (n == 12)
                d.back(now);
            if (n == 14)
                std::fill(mask.begin(), mask.end(), 0);
            std::vector<uint8_t> hid = gl.hid;
            d.frame(now);
            if (n == 0 || !gl.flip)
                continue;
            int bound = (int)((int64_t)kGlowStepFull * 256 * 3 * (frame_ms(n) - frame_ms(n - 1)) / 50);
            for (size_t i = 0; i < gl.ids.size() && i < hid.size(); i++) {
                int id = gl.ids[i];
                if (!id || !hid[i] || !gl.shades[(size_t)(id & 0x7F) - 1].dep)
                    continue;
                shown++;
                rings += (id & kGlowRing) ? 1 : 0;
                ground = ground && gl.e[i] <= bound && gl.e[i] < kGlowFull && gl.back[(size_t)(id & 0x7F) - 1] > bound;
            }
        }
        d.sp.moves.cover = nullptr;
        check(shown > 0 && rings > 0 && ground,
              "C7 (g) with a cover: after Back a departing block's pixels and ring that a cover hid start from ground "
              "at one step when it lifts, never at the block's lowest e at Back, which is above one step");
    }
    {
        bool fresh = true;
        int skipped = 0;
        {
            BlockCase c;
            c.recent = 12;
            c.sys = 16;
            LangDrive d(c);
            d.sp.pane->set_segments(true);
            d.sp.pane->trust_dst(true);
            int n = 0;
            for (; n < 120; n++)
                d.frame(frame_ms(n));
            int composed = d.sp.pane->trusted_frames();
            for (int k = 0; k < 10; k++, n++)
                d.frame(frame_ms(n));
            skipped += d.sp.pane->trusted_frames() == composed;
            int before = d.sp.pane->trusted_frames();
            d.prev = -1;
            d.frame(frame_ms(n));
            fresh = fresh && composed >= 3 && d.sp.pane->trusted_frames() == before;
        }
        check(fresh && skipped == 1,
              "lang: a pane skipped once landed is invalidated, so a later compose of it is not trusted");
    }
    {
        const uint32_t mark = 0x00A5C3E1u;
        Rect r{kGridX + kGridRowPitch - 8, 0, kPaneSplitWide, kBandTop + 240};
        int cw = r.x1 - r.x0;
        std::vector<uint8_t> mask((size_t)cw * (size_t)(r.y1 - r.y0));
        for (int y = r.y0; y < r.y1; y++)
            for (int x = r.x0; x < r.x1; x++)
                mask[(size_t)(y - r.y0) * (size_t)cw + (size_t)(x - r.x0)] = ((x / 7 + y / 5) & 1) != 0;
        bool kept = true, same = true;
        int under = 0;
        for (int kind = 0; kind < 3; kind++) {
            BlockCase c;
            c.recent = 12;
            c.sys = 16;
            c.focus = 1;
            c.collapse = kind == 1;
            GlowDrive a(c), b(c);
            for (GlowDrive* d : {&a, &b}) {
                d->sp.moves.glow_on = kind != 2;
                d->sp.moves.cover = &mask;
                d->sp.moves.cover_rect = r;
            }
            Surface sa = a.rd.surface(), sb = b.rd.surface();
            for (int n = 0; n <= 60 && kept && same; n++) {
                int64_t now = a.t0 + frame_ms(n);
                if (n == 9) {
                    a.back(now);
                    b.back(now);
                }
                int end = a.x_end(a.ft_at(now));
                auto shut = [&](int x, int y) {
                    return x < end && mask[(size_t)(y - r.y0) * (size_t)cw + (size_t)(x - r.x0)];
                };
                for (int y = r.y0; y < r.y1; y++)
                    for (int x = r.x0; x < r.x1; x++)
                        if (shut(x, y))
                            sa.row(y)[x] = mark;
                a.frame(now);
                b.frame(now);
                for (int y = r.y0; y < r.y1; y++)
                    for (int x = r.x0; x < r.x1; x++)
                        if (shut(x, y)) {
                            kept = kept && sa.row(y)[x] == mark;
                            under++;
                            sa.row(y)[x] = sb.row(y)[x];
                        }
                same = same && a.rd.pix == b.rd.pix && glow_eq(a.glow(), b.glow());
            }
            for (GlowDrive* d : {&a, &b})
                d->sp.moves.cover = nullptr;
        }
        check(kept && same && under > 0,
              "slide: the grid writes no pixel under the pane's cover left of x_end (the raster's ground and tiles, "
              "the copy frames, the header and the glow, with the glow on and off), and "
              "everywhere else draws and keeps the same state whatever the covered pixels hold, for Stepper expands "
              "and collapses with Back at frame 9");
    }
    {
        std::vector<uint8_t> row(24, 5);
        auto at = [&](int k, int x, int end) {
            std::vector<uint8_t> v = row;
            if (k >= 0)
                v[(size_t)k] = 6;
            return run_end(v.data(), x, end);
        };
        bool runs = at(-1, 0, 24) == 24 && at(1, 0, 24) == 1 && at(8, 0, 24) == 8 && at(9, 0, 24) == 9 &&
                    at(16, 0, 24) == 16 && at(20, 0, 24) == 20 && at(23, 0, 24) == 23 && at(-1, 3, 5) == 5 &&
                    at(-1, 23, 24) == 24 && at(12, 10, 24) == 12 && at(20, 10, 18) == 18;
        const int p = kGridRowPitch, last = kGridX + (kGridColsWide - 1) * p;
        bool edges = cell_edge_after(0) == kGridX && cell_edge_after(kGridX - 1) == kGridX &&
                     cell_edge_after(kGridX) == kGridX + kTileSide &&
                     cell_edge_after(kGridX + 5) == kGridX + kTileSide &&
                     cell_edge_after(kGridX + kTileSide - 1) == kGridX + kTileSide &&
                     cell_edge_after(kGridX + kTileSide) == kGridX + p &&
                     cell_edge_after(kGridX + p - 1) == kGridX + p &&
                     cell_edge_after(kGridX + p) == kGridX + p + kTileSide &&
                     cell_edge_after(last + kTileSide - 1) == last + kTileSide &&
                     cell_edge_after(last + kTileSide) == last + p &&
                     cell_edge_after(last + p) == INT_MAX && cell_edge_after(kPortraitW) == INT_MAX;
        check(runs && edges,
              "slide: run_end returns the first byte unlike the start, a mismatch at byte 0 and byte 7 of a word, "
              "in the tail and in the first word, and the end on a run to it; cell_edge_after returns the next tile "
              "edge before the grid, inside a tile, on each edge, in a gutter and in the last column, and none past it");
    }
    {
        bool same = true;
        int extra = 0;
        {
            BlockCase c;
            c.recent = 12;
            c.sys = 16;
            c.focus = 5;
            LangDrive a(c), b(c);
            for (int n = 0; n <= 40; n++) {
                a.frame(frame_ms(n));
                b.frame(frame_ms(n));
                const Glow& ga = a.sp.moves.glow;
                const Glow& gb = b.sp.moves.glow;
                same = same && ga.e == gb.e && ga.age == gb.age && ga.ids == gb.ids && ga.hid == gb.hid &&
                       region_is(a.rd.surface(), b.rd.surface(), 0, kPortraitW);
                if (n % 2 == 1) {
                    slide_compose(b.ra.surface(), b.rb.surface(), b.rd.surface(), b.sp, b.prev, b.prev, kGround,
                                  SlideText{});
                    extra++;
                }
            }
        }
        check(same && extra > 0, "lang: a compose that does not advance, between two that do, changes no glow state, "
                                 "the hid flags included, and leaves the next frame's reveal unchanged");
    }
    {
        bool same = true, stops = true, back = true;
        int compared = 0, done = 0;
        {
            BlockCase c;
            c.recent = 12;
            c.sys = 16;
            c.focus = 5;
            c.collapse = true;
            LangDrive b(c), a(c);
            a.sp.pane_stop = a.sp.moves.fold;
            int64_t dr = b.sp.pane_stop, fold = b.sp.moves.fold, end = fold + kBlockTailMs;
            stops = stops && dr == 417 && fold > dr;
            for (int n = 0; frame_ms(n) <= end; n++) {
                int64_t s = frame_ms(n);
                a.frame(s);
                b.frame(s);
                bool b_done = b.drawn.x1 < kPortraitW, a_done = a.drawn.x1 < kPortraitW;
                if (s < dr)
                    stops = stops && !b_done;
                if (s >= dr + 100 && s < fold) {
                    stops = stops && b_done && !a_done;
                    done++;
                }
                if (s >= dr) {
                    same = same && region_is(a.rd.surface(), b.rd.surface(), 0, kPortraitW);
                    compared++;
                }
            }
            c.collapse = false;
            LangDrive be(c), ae(c);
            ae.sp.pane_stop = ae.sp.moves.fold;
            int64_t at = be.sp.pane_stop + 100;
            be.back_at = ae.back_at = at;
            bool held = false;
            back = back && at < be.sp.moves.fold;
            for (int n = 0; frame_ms(n) <= at + end; n++) {
                be.frame(frame_ms(n));
                ae.frame(frame_ms(n));
                if (frame_ms(n) < at)
                    held = be.drawn.x1 < kPortraitW && ae.drawn.x1 == kPortraitW;
                back = back && region_is(ae.rd.surface(), be.rd.surface(), 0, kPortraitW);
            }
            back = back && held;
        }
        check(stops && done > 0 && same && compared > 0,
              "lang: a collapse stops composing the pane at its end (417 ms; its returned rect leaves the "
              "pane out on every frame from the end plus the text rise to the fold, where the twin composing to the "
              "fold still draws it) and, handing the grid the pane's cover, is pixel equal to that twin frame by frame "
              "through the grid's landing");
        check(back, "lang: an expand reversed by Back 100 ms past the pane's end (517 ms), with the pane "
                    "already stopped on the frame before it, is pixel equal on every frame through its landing to the "
                    "same expand composing the pane to the fold");
    }
    {
        struct Low {
            int n = -1;
            int64_t ms = -1;
            int e = eng::kFull;
            int id = 0;
            bool ink = false;
        };
        Low held_all, held_ink, last_all, last_ink;
        int last_rows = 0, last_n = -1;
        for (bool collapse : {false, true}) {
            BlockCase c;
            c.recent = 12;
            c.sys = 16;
            c.collapse = collapse;
            LangDrive d(c);
            for (int n = 0; n <= 60; n++) {
                int64_t s = frame_ms(n);
                d.frame(s);
                if (s >= d.sp.moves.fold)
                    continue;
                const std::vector<uint16_t>& ids = d.sp.pane->ids();
                const std::vector<uint16_t>& e = d.sp.pane->e();
                Rect pr = d.sp.pane->rect();
                size_t gw = (size_t)(pr.x1 - pr.x0);
                Low all, ink;
                int rows = 0;
                for (size_t i = 0; i < ids.size(); i++) {
                    int k = ids[i] & eng::kIdMask;
                    if (k < 2 || k > 9)
                        continue;
                    rows++;
                    bool lit = d.rd.surface().at(pr.x0 + (int)(i % gw), pr.y0 + (int)(i / gw)) != d.ground;
                    if (e[i] < all.e)
                        all = {n, s, e[i], k, lit};
                    if (lit && e[i] < ink.e)
                        ink = {n, s, e[i], k, true};
                }
                if (!collapse) {
                    if (held_all.n < 0 && all.e < eng::kFull)
                        held_all = all;
                    if (held_ink.n < 0 && ink.e < eng::kFull)
                        held_ink = ink;
                } else {
                    last_all = all;
                    last_ink = ink;
                    last_rows = rows;
                    last_n = n;
                }
            }
        }
        auto said = [](const char* what, const Low& v) {
            return v.n < 0 ? std::string(what) + " none"
                           : std::string(what) + " frame " + std::to_string(v.n) + " (" + std::to_string(v.ms) +
                                 " ms) e " + std::to_string(v.e) + " id " + std::to_string(v.id) +
                                 (v.ink ? " ink" : " ground");
        };
        bool held = held_all.n < 0, arrived = last_rows > 0 && last_ink.e == eng::kFull;
        std::string what = std::string("lang ") + "step" +
                           ": the departing menu rows (ids 2 to 9) hold kFull on every expand frame, and the arriving "
                           "rows' ink on a collapse is at kFull by the last frame before the fold (expand " +
                           (held ? "held" : "NOT held") + ", first below kFull: " + said("all px", held_all) + "; " +
                           said("ink px", held_ink) + "; collapse " + (arrived ? "arrived" : "NOT arrived") +
                           " at frame " + std::to_string(last_n) + ", " + std::to_string(last_rows) +
                           " row px, lowest: " + said("all px", last_all) + "; " + said("ink px", last_ink) + ")";
        check(held && arrived, what.c_str());
    }

    {
        auto glow_same = [](const Glow& a, const Glow& b) {
            return a.e == b.e && a.age == b.age && a.ids == b.ids && a.cur == b.cur && a.lo == b.lo && a.hi == b.hi &&
                   a.act_lo == b.act_lo && a.act_hi == b.act_hi && a.back == b.back && a.back_t == b.back_t &&
                   a.hid == b.hid && a.hid_now == b.hid_now && a.t0 == b.t0 &&
                   a.last == b.last && a.due == b.due && a.due_t0 == b.due_t0 && a.flip_s3 == b.flip_s3 &&
                   a.inited == b.inited && a.flip == b.flip && a.settled == b.settled;
        };
        auto pane_same = [](const eng::Instance& a, const eng::Instance& b) {
            return a.e() == b.e() && a.ids() == b.ids() && a.age() == b.age() && a.cover() == b.cover() &&
                   a.settled() == b.settled() && a.inited() == b.inited();
        };
        auto noise = [](Buffer& b) {
            Surface s = b.surface();
            for (int y = 0; y < s.h; y++)
                for (int x = 0; x < s.w; x++)
                    s.row(y)[x] = ((uint32_t)x * 2654435761u ^ (uint32_t)y * 40503u) & 0xFFFFFFu;
        };
        bool seed = true, dirty = true, runs = true;
        for (int kind = 0; kind < 4; kind++)
            for (int64_t lead : {1, 8, 16, -8}) {
                BlockCase c;
                c.recent = 12;
                c.sys = 16;
                c.redeal = kind & 1;
                c.collapse = kind >= 2;
                uint32_t ground = lead < 0 ? 0xEEEEEEu : kGround;
                int64_t ahead = lead < 0 ? -lead : lead;
                LangDrive a(c), b(c), d(c), r(c);
                a.ground = b.ground = d.ground = r.ground = ground;
                r.sp.moves.glow.seg = true;
                r.sp.moves.glow.verify = true;
                noise(d.rd);
                if (!c.collapse) {
                    b.frame(0);
                    std::fill(b.rd.pix.begin(), b.rd.pix.end(), 0u);
                    b.prev = -1;
                }
                bool ok = true, ok_d = true, ok_r = true;
                for (int n = 0; n < 4; n++) {
                    int64_t s = ahead + frame_ms(n);
                    a.frame(s);
                    d.frame(s);
                    r.frame(s);
                    ok_r = ok_r && a.rd.pix == r.rd.pix && glow_same(a.sp.moves.glow, r.sp.moves.glow);
                    ok_d = ok_d && a.rd.pix == d.rd.pix && glow_same(a.sp.moves.glow, d.sp.moves.glow) &&
                           pane_same(*a.sp.pane, *d.sp.pane);
                    if (!c.collapse) {
                        b.frame(s);
                        ok = ok && a.rd.pix == b.rd.pix && glow_same(a.sp.moves.glow, b.sp.moves.glow) &&
                             pane_same(*a.sp.pane, *b.sp.pane);
                    }
                }
                ok = ok && a.sp.moves.glow.seed_frames == 1 && d.sp.moves.glow.seed_frames == 1 &&
                     b.sp.moves.glow.seed_frames == 0 && a.sp.pane->seed_frames() == 1 &&
                     d.sp.pane->seed_frames() == 1;
                if (!ok || !ok_d)
                    std::printf("  %s %s %s lead %lld%s: %s\n", "step", c.redeal ? "re-deal" : "swap",
                                c.collapse ? "collapse" : "expand", (long long)ahead, lead < 0 ? " light" : "",
                                ok ? "differs over another target" : "differs from the start frame alone");
                const Glow& rg = r.sp.moves.glow;
                ok_r = ok_r && rg.seed_frames == 1 && rg.seg_frames == 5 && rg.runs_built == 0 &&
                       rg.runs_scans == 5 && rg.back_scans == 0 && rg.verify_fails == 0;
                seed = seed && ok;
                dirty = dirty && ok_d;
                runs = runs && ok_r;
            }
        check(seed, "lang: a swap or re-deal expand whose input leads its first frame by 1, 8 or 16 ms (8 in "
                    "light) draws and keeps the same pane and glow state as its start frame composed alone at t0 into "
                    "another target, the grid's seed and the pane's start composed as state only, expands and "
                    "collapses");
        check(dirty, "lang: a swap or re-deal expand or collapse whose input leads its first frame draws and "
                     "keeps the same pane and glow state over a different target");
        check(runs, "lang: with the grid's run pass on, a swap or re-deal expand or collapse whose input leads "
                    "its first frame composes its state only seed and every frame on runs, the runs never built from "
                    "the planes and agreeing with the verify scans, and draws and keeps the same glow state as the "
                    "pixel pass");
    }

    {
        bool same = true, built = true;
        int on = 0, off = 0, aged = 0, bscans = 0, rscans = 0, vfails = 0, pieces = 0, drives = 0;
        int hits[7] = {};
        for (int kind = 0; kind < 4; kind++)
            for (int64_t back_at : {-1, 150, 300}) {
                BlockCase c;
                c.recent = 12;
                c.sys = 16;
                c.redeal = kind & 1;
                c.collapse = kind >= 2;
                LangDrive a(c), b(c);
                b.sp.moves.glow.seg = true;
                b.sp.moves.glow.verify = true;
                a.back_at = b.back_at = back_at;
                for (int n = 0; n <= 60 && same; n++) {
                    a.frame(frame_ms(n));
                    b.frame(frame_ms(n));
                    same = same && a.rd.pix == b.rd.pix && glow_eq(a.sp.moves.glow, b.sp.moves.glow);
                }
                const Glow& g = b.sp.moves.glow;
                on += g.seg_frames;
                off += a.sp.moves.glow.seg_frames;
                aged += g.aging_frames;
                bscans += g.back_scans;
                rscans += g.runs_scans;
                vfails += g.verify_fails;
                built = built && g.runs_built == 0;
                drives++;
                pieces += g.run_pieces;
                for (int k = 0; k < 7; k++)
                    hits[k] += g.seg_hits[k];
            }
        bool branches = true;
        for (int k = 0; k < 7; k++)
            branches = branches && hits[k] > 0;
        char line[640];
        std::snprintf(line, sizeof line,
                      "lang: the grid's run pass draws and keeps the same glow state as its pixel pass over step swap "
                      "and re-deal expands and collapses, with and without Back at 150 ms, every aging frame and each "
                      "fold's first frame on runs, every rule branch taken on runs, every Back's kept lowest e and "
                      "every frame's row runs agreeing with the verify scans, the runs never built from the planes "
                      "(on %d, %d drives, hits %d %d %d %d %d %d %d)",
                      on, drives, hits[0], hits[1], hits[2], hits[3], hits[4], hits[5], hits[6]);
        check(same && off == 0 && on == aged + drives && on > 200 && branches && bscans > 0 && rscans > 0 &&
                  vfails == 0 && built && pieces > 0,
              line);
    }
    {
        bool same = true;
        int bscans = 0, rscans = 0, vfails = 0, folds = 0, built = 0;
        for (bool collapse : {false, true}) {
            BlockCase c;
            c.recent = 12;
            c.sys = 16;
            c.collapse = collapse;
            LangDrive a(c), b(c);
            b.sp.moves.glow.verify = true;
            a.back_at = b.back_at = 150;
            for (int n = 0; n <= 30 && same; n++) {
                b.sp.moves.glow.seg = n < 4 || frame_ms(n) >= 150;
                a.frame(frame_ms(n));
                b.frame(frame_ms(n));
                same = same && a.rd.pix == b.rd.pix && glow_eq(a.sp.moves.glow, b.sp.moves.glow);
            }
            bscans += b.sp.moves.glow.back_scans;
            rscans += b.sp.moves.glow.runs_scans;
            vfails += b.sp.moves.glow.verify_fails;
            folds += b.sp.moves.glow.flip;
            built += b.sp.moves.glow.runs_built;
        }
        check(same && folds == 2 && built == 2 && bscans == 0 && rscans > 0 && vfails == 0,
              "lang: a grid pass with seg turned off on the frames before Back keeps no lowest e from its last run "
              "frame: Back at 150 ms with seg on again rescans, builds the row runs from the planes once, and draws "
              "and keeps the same glow state as the pixel pass, expands and collapses under verify");
    }
    {
        bool same = true, unscanned = true;
        int on = 0, aged = 0, drives = 0;
        int hits[7] = {};
        for (bool reverse : {false, true}) {
            BlockCase c;
            c.recent = 12;
            c.sys = 16;
            GlowDrive a(c), b(c);
            b.sp.moves.glow.seg = true;
            for (int n = 0; n <= 60 && same; n++) {
                int64_t now = a.t0 + frame_ms(n);
                if (reverse && n == 9) {
                    a.back(now);
                    b.back(now);
                }
                a.frame(now);
                b.frame(now);
                same = same && a.rd.pix == b.rd.pix && glow_eq(a.glow(), b.glow());
            }
            const Glow& g = b.glow();
            on += g.seg_frames;
            aged += g.aging_frames;
            unscanned = unscanned && g.verify_scans == 0 && g.runs_built == 0;
            drives++;
            for (int k = 0; k < 7; k++)
                hits[k] += g.seg_hits[k];
        }
        bool branches = true;
        for (int k = 1; k < 7; k++)
            branches = branches && hits[k] > 0;
        char line[640];
        std::snprintf(line, sizeof line,
                      "slide: the grid's run pass draws and keeps the same glow state as its pixel pass over a Stepper "
                      "expand with and without Back at 150 ms, every aging frame and each fold's first frame on runs, "
                      "at least 53 run frames (35 to the hold at 567 ms, 18 to the reversed landing), every rule "
                      "branch but the hid one taken on runs, the runs never built from the planes, and with verify off "
                      "no reference scan at Back (on %d, aged %d, %d drives, hits %d %d %d %d %d %d %d)",
                      on, aged, drives, hits[0], hits[1], hits[2], hits[3], hits[4], hits[5], hits[6]);
        check(same && on == aged + drives && on >= 53 && branches && unscanned, line);
    }
    {
        int copies = 0, copy_runs = 0, built = 0, vfails = 0, drives = 0, aged = 0, deep = 0, deep_runs = 0;
        bool same = true;
        for (int wait : {1, 3})
            {
                BlockCase c;
                c.recent = 12;
                c.sys = 16;
                GlowDrive a(c), b(c);
                b.sp.moves.glow.seg = true;
                b.sp.moves.glow.verify = true;
                int copied = 0;
                bool backed = false;
                for (int n = 0; n <= 90 && same; n++) {
                    int64_t now = a.t0 + frame_ms(n);
                    if (copied >= wait && !backed) {
                        a.back(now);
                        b.back(now);
                        backed = true;
                        deep += wait == 3;
                    }
                    int seg0 = b.glow().seg_frames;
                    a.frame(now);
                    b.frame(now);
                    same = same && a.rd.pix == b.rd.pix && glow_eq(a.glow(), b.glow());
                    bool ran = backed && b.glow().seg_frames > seg0;
                    copies += b.glow().copied;
                    copy_runs += ran && wait == 1;
                    deep_runs += ran && wait == 3;
                    copied += !backed && b.glow().copied;
                }
                built += b.glow().runs_built;
                vfails += b.glow().verify_fails;
                aged += b.glow().runs_aged;
                drives++;
            }
        auto said = [&](const char* what) {
            char t[300];
            std::snprintf(t, sizeof t,
                          "slide: %s (copy frames %d, run frames after the copy %d, Backs after 3 copies %d with run "
                          "frames %d, aged pieces changed %d, plane builds %d, verify fails %d, %d drives)",
                          what, copies, copy_runs, deep, deep_runs, aged, built, vfails, drives);
            return std::string(t);
        };
        check(copies > 0, said("a Stepper expand reaches the grid's copy branch at rest").c_str());
        check(copy_runs > 0, said("a Back on the frame after the copy composes on runs again in the same gen").c_str());
        check(deep_runs > 0, said("a Back after 3 copy frames at rest composes on runs again in the same gen").c_str());
        check(aged > 0, said("glow_age's in place aging changes runs, so an identity map cannot pass").c_str());
        check(built == 0, said("the runs glow_age aged in place are never rebuilt from the planes").c_str());
        check(vfails == 0, said("the aged runs agree with the verify scans").c_str());
        check(same, said("the copy then Back drive keeps the pixel pass's pixels and glow state").c_str());
    }

    {
        struct Fold {
            bool collapse;
            int back_n;
            int cut_n;
        };
        const Fold seq[] = {{false, -1, -1}, {true, -1, -1}, {false, 9, -1}, {true, -1, 8}, {false, -1, -1}};
        auto glow = std::make_shared<Glow>();
        uint64_t gen = 0;
        bool same = true, kept = true;
        int folds = 0;
        for (const Fold& fd : seq) {
            BlockCase c;
            c.recent = 12;
            c.sys = 16;
            c.collapse = fd.collapse;
            GlowDrive k(c), f(c);
            k.sp.moves.shared = glow;
            k.sp.moves.gen = ++gen;
            size_t caps[] = {glow->e.capacity(), glow->age.capacity(), glow->ids.capacity(), glow->cur.capacity(),
                             glow->hid.capacity()};
            for (int n = 0; n <= 60 && n != fd.cut_n && same; n++) {
                int64_t now = k.t0 + frame_ms(n);
                if (n == fd.back_n) {
                    k.back(now);
                    f.back(now);
                }
                k.frame(now);
                f.frame(now);
                same = same && k.rd.pix == f.rd.pix && glow_all(glow_of(k.sp.moves), f.glow()) &&
                       glow->gen == k.sp.moves.gen;
            }
            size_t now_caps[] = {glow->e.capacity(), glow->age.capacity(), glow->ids.capacity(),
                                 glow->cur.capacity(), glow->hid.capacity()};
            for (int q = 0; q < 5; q++)
                kept = kept && (folds == 0 || now_caps[q] == caps[q]);
            folds++;
        }
        check(same && kept && folds == 5,
              "slide: one Glow kept across a run of folds (an expand, a collapse, a reversal, a fold an input cuts at "
              "frame 8, an expand) draws and keeps the same state as a fresh Glow per fold in every member, the e, age, "
              "ids, cur and hid planes' capacity kept");
    }
    {
        BlockCase c;
        c.recent = 12;
        c.sys = 16;
        auto glow = std::make_shared<Glow>();
        glow->seg = true;
        LangDrive d(c);
        d.sp.moves.shared = glow;
        d.sp.moves.gen = 1;
        for (int n = 0; n < 6; n++)
            d.frame(frame_ms(n));
        size_t caps[] = {glow->e.capacity(), glow->age.capacity(), glow->ids.capacity(), glow->cur.capacity(),
                         glow->hid.capacity()};
        bool used = caps[0] > 0 && caps[4] > 0 && glow->inited;
        glow_reset(*glow);
        Glow fresh;
        fresh.seg = true;
        size_t after[] = {glow->e.capacity(), glow->age.capacity(), glow->ids.capacity(), glow->cur.capacity(),
                          glow->hid.capacity()};
        bool held = true;
        for (int q = 0; q < 5; q++)
            held = held && after[q] == caps[q];
        check(used && held && glow_all(*glow, fresh) && glow->gen == 0,
              "slide: glow_reset leaves a used Glow equal to a fresh one in every member but seg and gen, its e, age, "
              "ids, cur and hid planes' capacity kept");
    }
    {
        auto drive = [](GlowDrive& g, int64_t now, int x_end) {
            int64_t ft = g.ft_at(now);
            block_advance(g.sp.moves, g.t0, now);
            int bx = std::clamp(slide_pose(g.sp, ft).boundary, kPaneSplit, kPortraitW);
            int end = x_end > 0 ? x_end : std::max(bx, kPaneLeftEdge);
            block_compose(g.ra.surface(), g.rb.surface(), g.rd.surface(), g.sp.moves, ft,
                          block_time(g.sp.moves, ft), bx, end, kGround);
            return end;
        };
        bool led = true, cut = true;
        int seeded = 0, drawn = 0;
        for (bool collapse : {false, true})
            for (int x_end : {0, 300}) {
                BlockCase c;
                c.recent = 12;
                c.sys = 16;
                c.collapse = collapse;
                GlowDrive a(c), b(c);
                drive(b, b.t0, x_end);
                if (!x_end)
                    std::fill(b.rd.pix.begin(), b.rd.pix.end(), 0u);
                for (int n = 0; n < 4; n++) {
                    int64_t now = a.t0 + 8 + frame_ms(n);
                    int end = drive(a, now, x_end);
                    drive(b, now, x_end);
                    bool same = glow_eq(a.glow(), b.glow()) &&
                                (x_end ? a.rd.pix == b.rd.pix : region_is(a.rd.surface(), b.rd.surface(), 0, end));
                    (x_end ? cut : led) = (x_end ? cut : led) && same;
                }
                (x_end ? drawn : seeded) += a.glow().seed_frames;
            }
        check(led && seeded > 0 && cut && drawn == 0,
              "slide: Stepper expands and collapses led by 8 ms draw and keep the same state as their s 0 "
              "frame composed alone, the seed state only when every seed pose's box ends at or before x_end, and with "
              "x_end 300 under the seed's tiles the seed draws (the frame's paint does not rewrite past x_end)");
    }
    {
        BlockCase c;
        c.recent = 12;
        c.sys = 16;
        auto glow = std::make_shared<Glow>();
        GlowDrive a(c), b(c), fresh(c);
        a.sp.moves.shared = glow;
        a.sp.moves.gen = 1;
        b.sp.moves.shared = glow;
        b.sp.moves.gen = 2;
        for (int n = 0; n < 5; n++)
            a.frame(a.t0 + frame_ms(n));
        bool view = glow_view(a.sp.moves).inited;
        for (int n = 0; n < 3; n++)
            b.frame(b.t0 + frame_ms(n));
        bool lent = !glow_view(a.sp.moves).inited && glow->gen == 2;
        int64_t now = a.t0 + frame_ms(5);
        a.frame(now);
        fresh.frame(now);
        bool drawn = region_is(a.rd.surface(), fresh.rd.surface(), 0, a.x_end(a.ft_at(now)));
        check(view && lent && glow->gen == 1 && drawn && glow_eq(glow_of(a.sp.moves), fresh.glow()),
              "slide: a plan composing after another plan took the kept Glow sees a fresh Glow until it composes, then "
              "re-seeds as a fresh fold would");
    }

    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
