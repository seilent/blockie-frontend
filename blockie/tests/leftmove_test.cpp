#include <cmath>
#include <cstdio>
#include <vector>

#include "../../core/curves.h"
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

}

int main()
{
    uint32_t g = LiveF::pack(kGround);

    check(fx_round(tok::motion_swapOut) == 50 && std_px(tok::motion_swapShift) == 68 &&
              fx_round(tok::motion_swapRows) == 150 && std_px(tok::motion_swapRowInShift) == 50 &&
              fx_round(tok::motion_swapRowFade) == 200 && fx_round(tok::motion_leave) == 133,
          "D's tokens: out 50 ms, shift 68 px, rows 150 ms, row in shift 50 px, row fade 200 ms, header 133 ms");
    check(swap_ms() == 250, "D ends at 250 ms");
    check(fx_round(tok::motion_pivotOut) == 300 && fx_round(tok::motion_pivotRows) == 380 &&
              fx_round(tok::motion_pivotRowStagger) == 22 && swap_ms() != 300 + 380,
          "D reads no motion.pivot* token: the carved 300, 380 and 22 would not end at 250");

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

    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
