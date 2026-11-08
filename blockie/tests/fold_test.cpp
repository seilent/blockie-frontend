#include <algorithm>
#include <cstdio>

#include "../fold.h"

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
