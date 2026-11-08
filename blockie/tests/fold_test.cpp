#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "../fold.h"
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

constexpr uint32_t kSwap = 0x808080;
constexpr uint32_t kSolidFrom = 0x204060;
constexpr uint32_t kSolidTo = 0xE0C0A0;

uint32_t pattern(int x, int y)
{
    return (uint32_t)((x * 7 + y * 13) & 0xFF) << 16 | (uint32_t)((x * 3) & 0xFF) << 8 | (uint32_t)(y & 0xFF);
}

void paint(Buffer& b, uint32_t mask)
{
    Surface s = b.surface();
    for (int y = 0; y < s.h; y++)
        for (int x = 0; x < s.w; x++)
            s.at(x, y) = pattern(x, y) ^ mask;
}

void solid(Buffer& b, uint32_t v)
{
    Surface s = b.surface();
    for (int y = 0; y < s.h; y++)
        for (int x = 0; x < s.w; x++)
            s.at(x, y) = v;
}

void fill_rect(Surface s, Rect r, uint32_t v)
{
    for (int y = r.y0; y < r.y1; y++)
        for (int x = r.x0; x < r.x1; x++)
            s.at(x, y) = v;
}

bool same(const Surface& a, const Surface& b, int x0, int y0, int x1, int y1)
{
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            if (a.at(x, y) != b.at(x, y))
                return false;
    return true;
}

const FoldCell* cell_at(const FoldPlan& p, int x, int y)
{
    for (const FoldCell& c : p.cells)
        if (c.x == x && c.y == y)
            return &c;
    return nullptr;
}

bool ring_in(const FoldCell* c, int lo, int hi) { return c && c->ring >= lo && c->ring <= hi; }

bool inside_matches(const Surface& d, const Surface& want, const FoldPlan& p)
{
    for (const FoldCell& c : p.cells)
        if (c.inside && !same(d, want, c.x, c.y, c.x + 48, c.y + 48))
            return false;
    return true;
}

int64_t mid_fold(int ring) { return (int64_t)ring * 5000 / 60 + 42; }

bool folded_toward_right(const Surface& d, const FoldCell& c)
{
    for (int y = c.y; y < c.y + 48; y++) {
        int n = 0;
        while (n < 48 && d.at(c.x + 47 - n, y) == kSolidFrom)
            n++;
        if (n == 0 || n == 48)
            return false;
        for (int x = c.x; x < c.x + 48 - n; x++)
            if (d.at(x, y) != kSolidTo)
                return false;
    }
    return true;
}

bool folded_toward_bottom(const Surface& d, const FoldCell& c)
{
    for (int x = c.x; x < c.x + 48; x++) {
        int n = 0;
        while (n < 48 && d.at(x, c.y + 47 - n) == kSolidFrom)
            n++;
        if (n == 0 || n == 48)
            return false;
        for (int y = c.y; y < c.y + 48 - n; y++)
            if (d.at(x, y) != kSolidTo)
                return false;
    }
    return true;
}

}

int main()
{
    FoldPlan ab = fold_plan(FoldDir::ToDetails, 1, 0);
    FoldPlan ab2 = fold_plan(FoldDir::ToDetails, 1, 0);
    FoldPlan ba = fold_plan(FoldDir::ToSelector, 1, 0);
    check(ab.cells.size() == 11 * 15, "the right pane grid is 11 x 15 cells of 48 px from x 432");
    check(ab.tiles.size() == 8 && ab.tile_steps == 7, "the left pane flips its 8 tiles in 7 steps");
    check(ab.tiles[0].step == 0 && ab.tiles[1].step == 1 && ab.tiles[2].step == 1 && ab.tiles[7].step == 6,
          "tiles flip outward from the top left tile, equal distances together");

    bool equal = ab.cells.size() == ab2.cells.size() && ab.maxring == ab2.maxring;
    for (size_t i = 0; equal && i < ab.cells.size(); i++)
        equal = ab.cells[i].ring == ab2.cells[i].ring && ab.cells[i].edge == ab2.cells[i].edge &&
                ab.cells[i].inside == ab2.cells[i].inside;
    check(equal, "two plans with the same inputs have the same rings and edges");

    bool differ = false;
    for (size_t i = 0; i < ab.cells.size() && i < ba.cells.size(); i++)
        if (ab.cells[i].ring != ba.cells[i].ring || ab.cells[i].edge != ba.cells[i].edge)
            differ = true;
    check(differ, "selector to details and back use different rings");

    const FoldCell* c1 = cell_at(ab, 432, 336);
    check(c1 && c1->edge == FoldEdge::Down, "to details v 1: cell 432,336 folds down");
    check(ring_in(c1, 2, 5), "to details v 1: cell 432,336 ring from d 179.8 plus jitter");
    const FoldCell* c2 = cell_at(ab, 912, 192);
    check(c2 && c2->edge == FoldEdge::Right, "to details v 1: cell 912,192 folds right");
    check(ring_in(c2, 1, 4), "to details v 1: cell 912,192 ring from d 124.1 plus jitter");
    const FoldCell* c3 = cell_at(ba, 432, 144);
    check(c3 && c3->edge == FoldEdge::Left, "to selector: cell 432,144 folds left");
    check(ring_in(c3, 0, 3), "to selector: cell 432,144 ring from d 75.9 plus jitter");
    const FoldCell* c4 = cell_at(ba, 528, 48);
    check(c4 && c4->edge == FoldEdge::Up, "to selector: cell 528,48 folds up");
    check(ring_in(c4, 0, 2), "to selector: cell 528,48 ring from d 48.3 plus jitter");

    FoldPlan ab3 = fold_plan(FoldDir::ToDetails, 3, 0);
    const FoldCell* c5 = cell_at(ab3, 432, 528);
    check(c5 && c5->edge == FoldEdge::Down && c5->inside, "to details v 3: seeds follow the focused row");

    int want_bottom[6] = {420, 420, 516, 612, 708, 720};
    bool bottoms = true;
    for (int v = 0; v < 6; v++)
        if (fold_plan(FoldDir::ToDetails, v, 0).scope.y1 != want_bottom[v] ||
            fold_plan(FoldDir::ToSelector, v, 0).scope.y1 != want_bottom[v])
            bottoms = false;
    check(bottoms, "scope bottom 420 + max(0, v - 1) x 96, capped at 720");
    check(fold_plan(FoldDir::ToDetails, 1, 600).scope.y1 == 600, "scope bottom grows to the title bottom");
    check(fold_plan(FoldDir::ToDetails, 1, 900).scope.y1 == 720, "scope bottom never passes the screen");
    check(ab.scope.x0 == 432 && ab.scope.x1 == 960 && ab.scope.y0 == 0, "scope spans x 432 to 960 from y 0");
    FoldPlan ab_v2 = fold_plan(FoldDir::ToDetails, 2, 0);
    const FoldCell* low1 = cell_at(ab, 432, 432);
    const FoldCell* low2 = cell_at(ab_v2, 432, 432);
    check(low1 && !low1->inside, "v 1: a cell centred at y 456 is outside the scope");
    check(low2 && low2->inside, "v 2: a cell centred at y 456 is inside the scope");

    bool durations = true, maxrings = true;
    for (int v = 0; v < 6; v++)
        for (FoldDir d : {FoldDir::ToDetails, FoldDir::ToSelector}) {
            FoldPlan p = fold_plan(d, v, 0);
            int m = 0;
            for (const FoldCell& c : p.cells)
                if (c.inside && c.ring > m)
                    m = c.ring;
            if (m != p.maxring)
                maxrings = false;
            if (fold_duration(p) != (std::max(p.maxring + 1, p.tile_steps + 1) * 250 + 2) / 3)
                durations = false;
        }
    check(maxrings, "maxring is the largest ring inside the scope");
    check(durations, "duration is the longer of the rings and the tile turns, in 83.33 ms units, rounded up");

    Buffer from, to, dst;
    paint(from, 0);
    paint(to, kSwap);
    Surface fs = from.surface(), ts = to.surface(), ds = dst.surface();
    int64_t total = fold_duration(ab);

    fold_compose(fs, ts, ds, ab, 0);
    check(inside_matches(ds, fs, ab), "t 0: every cell inside the scope equals from");
    check(same(ds, fs, 432, 0, 960, 720), "t 0: the whole fold area equals from");
    check(same(ds, fs, 0, 0, 432, 720), "t 0: the left pane equals from");

    fold_compose(fs, ts, ds, ab, total);
    check(same(ds, ts, 0, 0, 960, 720), "t T: the whole screen equals to");

    fold_compose(fs, ts, ds, ab, 40);
    Rect t0 = ab.tiles[0].r;
    int spine = (t0.x0 + t0.x1) / 2;
    check(same(ds, fs, t0.x0, t0.y0, spine, t0.y1) && same(ds, ts, t0.x1 - 8, t0.y0, t0.x1, t0.y1),
          "mid page turn: the old left half stays, the new right half shows at the edge");
    check(same(ds, fs, ab.tiles[7].r.x0, ab.tiles[7].r.y0, ab.tiles[7].r.x1, ab.tiles[7].r.y1),
          "the last tile has not started turning yet");
    fold_compose(fs, ts, ds, ba, fold_duration(ba));
    check(same(ds, ts, 0, 0, 960, 720), "to selector at T: the whole screen equals to");
    FoldPlan push = fold_plan(FoldDir::PagePush, 1, 0);
    FoldPlan pop = fold_plan(FoldDir::PagePop, 1, 0);
    bool all_inside = true;
    for (const FoldCell& c : push.cells)
        if (!c.inside)
            all_inside = false;
    for (const FoldCell& c : pop.cells)
        if (!c.inside)
            all_inside = false;
    check(all_inside && push.cells.size() == 20 * 15, "page push and pop fold every cell of the screen");
    fold_compose(fs, ts, ds, push, 0);
    check(same(ds, fs, 0, 0, 960, 720), "page push at 0 equals from");
    fold_compose(fs, ts, ds, push, fold_duration(push));
    check(same(ds, ts, 0, 0, 960, 720), "page push at T equals to");
    int64_t ptotal = fold_duration(pop);
    check(ptotal == fold_duration(push), "page pop lasts as long as page push");
    fold_compose(fs, ts, ds, pop, 0);
    check(same(ds, fs, 0, 0, 960, 720), "page pop at 0 equals from");
    fold_compose(fs, ts, ds, pop, ptotal);
    check(same(ds, ts, 0, 0, 960, 720), "page pop at T equals to");
    Buffer rev;
    Surface rs = rev.surface();
    bool mirrored = true;
    for (int64_t t : {(int64_t)100, ptotal / 2, ptotal - 100}) {
        fold_compose(fs, ts, ds, pop, t);
        fold_compose(ts, fs, rs, push, ptotal - t);
        if (!same(ds, rs, 0, 0, 960, 720))
            mirrored = false;
    }
    check(mirrored, "page pop at t is page push at T - t with the pages swapped");

    Buffer sf, st;
    solid(sf, kSolidFrom);
    solid(st, kSolidTo);
    Surface sfs = sf.surface(), sts = st.surface();
    if (c2) {
        fold_compose(sfs, sts, ds, ab, mid_fold(c2->ring));
        check(folded_toward_right(ds, *c2), "mid fold: a right cell keeps a strip of from at its right edge");
    }
    if (c1) {
        fold_compose(sfs, sts, ds, ab, mid_fold(c1->ring));
        check(folded_toward_bottom(ds, *c1), "mid fold: a down cell keeps a strip of from at its bottom edge");
    }

    const FoldCell* last = nullptr;
    for (const FoldCell& c : ab.cells)
        if (c.inside && c.ring == ab.maxring)
            last = &c;
    if (last) {
        fold_compose(sfs, sts, ds, ab, total - 17);
        Rect lt = ab.tiles.back().r;
        bool rings_last = ab.maxring + 1 >= ab.tile_steps + 1;
        check(rings_last ? !same(ds, sts, last->x, last->y, last->x + 48, last->y + 48)
                         : !same(ds, sts, lt.x0, lt.y0, lt.x1, lt.y1),
              "one frame before T: the last ring cell or the last tile is still turning");
    }

    const FoldCell* out = nullptr;
    for (const FoldCell& c : ab.cells)
        if (!c.inside)
            out = &c;
    if (out) {
        fold_compose(sfs, sts, ds, ab, total / 2);
        uint32_t v = ds.at(out->x, out->y);
        check(v != kSolidFrom && v != kSolidTo, "mid time: a cell outside the scope is a crossfade");
    }

    Rect t0r = ab.tiles[0].r;
    int sp = (t0r.x0 + t0r.x1) / 2, ty = (t0r.y0 + t0r.y1) / 2;
    fold_compose(sfs, sts, ds, ab, 42);
    check(ds.at(t0r.x0 + 2, ty) == kSolidFrom && ds.at(sp + 2, ty) == kSolidFrom && ds.at(t0r.x1 - 2, ty) == kSolidTo,
          "first half turn: the old right half squeezes toward the spine and the new right half shows behind it");
    fold_compose(sfs, sts, ds, ab, 125);
    check(ds.at(t0r.x0 + 2, ty) == kSolidFrom && ds.at(sp - 2, ty) == kSolidTo && ds.at(sp + 2, ty) == kSolidTo &&
              ds.at(t0r.x1 - 2, ty) == kSolidTo,
          "second half turn: the new left half opens out from the spine over the old one");
    bool together = true;
    for (int k : {1, 2}) {
        Rect r = ab.tiles[k].r;
        int s2 = (r.x0 + r.x1) / 2, y2 = (r.y0 + r.y1) / 2;
        if (ds.at(s2 + 2, y2) != kSolidFrom || ds.at(r.x1 - 2, y2) != kSolidTo)
            together = false;
    }
    Rect r3 = ab.tiles[3].r;
    check(together && same(ds, sfs, r3.x0, r3.y0, r3.x1, r3.y1),
          "tiles 1 and 2 turn together one unit after tile 0, tile 3 waits");

    check(push.ring_unit == 0 && pop.ring_unit == 0 && ab.ring_unit == 0,
          "the shipped plans keep the 83.33 ms ring unit");

    FoldPlan left5 = fold_left(5);
    FoldPlan left3 = fold_left(3);
    {
        bool shape = left5.cells.size() == 135;
        for (const FoldCell& c : left5.cells)
            if (c.x < 0 || c.x + 48 > 432 || c.y < 0 || c.y + 48 > 720 || !c.inside)
                shape = false;
        check(shape, "the left plan is 135 cells of 48 px, none crossing x 432");
        const char* rings = "778998988978978877787868778666877767777777556756655465455664553354353535534344452423435444"
                            "342224112341222332201020013201300011011101002";
        const char* edges = "LLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLL"
                            "ULLLLLLLLLLLLLLUULLLLLLLLLDLLLUUULDULDULDDULD";
        bool table = left5.cells.size() == 135;
        for (size_t i = 0; table && i < 135; i++) {
            const FoldCell& c = left5.cells[i];
            FoldEdge e = edges[i] == 'L' ? FoldEdge::Left : edges[i] == 'R' ? FoldEdge::Right
                         : edges[i] == 'U'                  ? FoldEdge::Up
                                                            : FoldEdge::Down;
            if (c.x != (int)(i / 15) * 48 || c.y != (int)(i % 15) * 48 || c.ring != rings[i] - '0' || c.edge != e)
                table = false;
        }
        check(table, "the left plan's cells, rings and edges match the table worked out from the seeds on x 432");
        check(left5.maxring == 9, "the left plan's highest ring is 9");
        bool same3 = left3.cells.size() == left5.cells.size() && left3.maxring == left5.maxring;
        for (size_t k = 0; same3 && k < left5.cells.size(); k++)
            same3 = left3.cells[k].ring == left5.cells[k].ring && left3.cells[k].edge == left5.cells[k].edge;
        check(same3 && left5.ring_unit == 5000 && left3.ring_unit == 3000,
              "3 frames per ring keeps the rings; the units are 5 and 3 frames");
    }

    const uint32_t ground = 0x111111;
    const Rect head{120, 0, 300, 76};
    auto scene = [&](Buffer& buf, bool header, const uint32_t* tiles) {
        Surface sf = buf.surface();
        fill_rect(sf, sf.rect(), ground);
        if (header)
            fill_rect(sf, {124, 20, 296, 70}, 0x808080);
        for (int k = 0; k < 8; k++)
            if (tiles[k])
                fill_rect(sf, grid_tile_rect(k, kGridCols), tiles[k]);
    };
    const uint32_t ta[8] = {0x200000, 0x210000, 0x220000, 0x230000, 0x240000, 0x250000, 0x260000, 0x270000};
    const uint32_t tb[8] = {0x200000, 0x210000, 0x002200, 0x002300, 0x002400, 0x002500, 0x002600, 0};
    Buffer la, lb, ld;
    scene(la, true, ta);
    scene(lb, false, tb);
    Surface las = la.surface(), lbs = lb.surface(), lds = ld.surface();
    auto in_rect = [](Rect r, int x, int y) { return x >= r.x0 && x < r.x1 && y >= r.y0 && y < r.y1; };
    for (int unit : {5, 3}) {
        LeftFold lf = left_fold(unit, head);
        left_prepare(lf, las, lbs);
        int64_t L = left_span(lf);
        check(lf.top_ring == 7 && L == (unit == 5 ? 667 : 400),
              unit == 5 ? "5 frames: the re-dealt slots fold up to ring 7, the span is 667 ms"
                        : "3 frames: the re-dealt slots fold up to ring 7, the span is 400 ms");
        bool slots = true, rows = true, sentinel = true, header = true, masks = true;
        for (int64_t t : {(int64_t)0, (int64_t)17, (int64_t)100, (int64_t)250, (int64_t)400, L / 2, L - 17, L}) {
            int hd = (int)std::min<int64_t>(256, t * 256 / 400);
            for (int y = 0; y < lds.h; y++)
                for (int x = 0; x < 436; x++)
                    lds.at(x, y) = 0xFF123456u;
            left_compose(las, lbs, lds, lf, t, hd);
            for (size_t ci = 0; ci < lf.plan.cells.size(); ci++) {
                if (!lf.folds[ci])
                    continue;
                const FoldCell& c = lf.plan.cells[ci];
                bool by_row = c.edge == FoldEdge::Right || c.edge == FoldEdge::Left;
                int cw = c.x + 48 >= 432 ? 52 : 48;
                for (int i = 0; i < (by_row ? 48 : cw); i++) {
                    if (lf.lines[ci * kLeftLine + i])
                        continue;
                    for (int k = 0; k < (by_row ? cw : 48); k++) {
                        int x = c.x + (by_row ? k : i), y = c.y + (by_row ? i : k);
                        if (!lf.own[(size_t)y * kPaneLeftEdge + x] && lds.at(x, y) != lbs.at(x, y))
                            masks = false;
                    }
                }
            }
            for (int y = 0; y < lds.h; y++)
                for (int x = 0; x < 436; x++) {
                    uint32_t v = lds.at(x, y);
                    if (v == 0xFF123456u)
                        sentinel = false;
                    for (int k = 0; k < 2; k++)
                        if (in_rect(lf.slots[k], x, y) && v != lbs.at(x, y))
                            slots = false;
                    if (y < 76 && !in_rect(head, x, y) && v != lbs.at(x, y))
                        rows = false;
                    if (in_rect(head, x, y) && v != blend_px(las.at(x, y), lbs.at(x, y), hd) &&
                        !(hd >= 256 && v == lbs.at(x, y)) && !(hd <= 0 && v == las.at(x, y)))
                        header = false;
                }
        }
        check(sentinel, "the left compose writes every pixel left of 436 at every t");
        check(slots, "unchanged slot pixels equal to at every t");
        check(rows, "with the header changing, the unchanged rows equal to at every t");
        check(header, "the header crossfades on its own and no cell writes it");
        check(masks, "inside a folding cell, rows (left, right) or columns (up, down) equal in from and to copy to");
        left_compose(las, lbs, lds, lf, 0, 0);
        check(same(lds, las, 0, 0, 436, 720), "at 0 the left pane is from");
        left_compose(las, lbs, lds, lf, L, 256);
        check(same(lds, lbs, 0, 0, 436, 720), "at the span the left pane is to");
    }
    {
        Buffer nb;
        scene(nb, false, ta);
        LeftFold lf = left_fold(5, head);
        left_prepare(lf, las, nb.surface());
        check(lf.top_ring < 0 && left_span(lf) == 0, "nothing folds when only the header changes");
    }
    {
        LeftFold lf = left_fold(5, head);
        left_prepare(lf, las, lbs);
        int top = lf.top_ring;
        std::vector<uint8_t> lines = lf.lines;
        Buffer nb;
        uint32_t tn[8] = {0x330000, 0x210000, 0x002200, 0x002300, 0x002400, 0x002500, 0x002600, 0};
        scene(nb, false, tn);
        Surface ns = nb.surface();
        fill_rect(ns, {98, 198, 104, 204}, 0x445566);
        left_prepare(lf, las, ns);
        check(lf.top_ring == top && lf.changed[0] == 0, "a later prepare keeps the span and the first changed set");
        check(lf.lines != lines, "the masks recompute after to changes");
        left_compose(las, ns, lds, lf, 0, 0);
        check(same(lds, ns, lf.slots[0].x0, lf.slots[0].y0, lf.slots[0].x1, lf.slots[0].y1),
              "a slot that turns changed after a re-render shows to at once");
    }
    {
        Buffer fa, fb, fd;
        paint(fa, 0);
        paint(fb, kSwap);
        left_fade(fa.surface(), fb.surface(), fd.surface(), 0);
        bool z = same(fd.surface(), fa.surface(), 0, 0, 436, 720);
        left_fade(fa.surface(), fb.surface(), fd.surface(), 256);
        bool o = same(fd.surface(), fb.surface(), 0, 0, 436, 720);
        left_fade(fa.surface(), fb.surface(), fd.surface(), 128);
        bool m = fd.surface().at(10, 10) == blend_px(fa.surface().at(10, 10), fb.surface().at(10, 10), 128) &&
                 fd.surface().at(500, 10) == 0;
        check(z && o && m, "L0 crossfades x < 436 per pixel and leaves the rest");
    }

    bool covered = true;
    for (const FoldPlan* pl : {&ab, &ba, &push, &pop}) {
        int64_t T = fold_duration(*pl);
        for (int64_t t : {(int64_t)0, (int64_t)40, (int64_t)125, (int64_t)300, T / 2, T - 17, T}) {
            for (int y = 0; y < ds.h; y++)
                for (int x = 0; x < ds.w; x++)
                    ds.at(x, y) = 0xFF123456u;
            fold_compose(fs, ts, ds, *pl, t);
            for (int y = 0; y < ds.h && covered; y++)
                for (int x = 0; x < ds.w; x++)
                    if (ds.at(x, y) == 0xFF123456u) {
                        covered = false;
                        break;
                    }
        }
    }
    check(covered, "every fold writes every pixel of the screen at every t");

    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
