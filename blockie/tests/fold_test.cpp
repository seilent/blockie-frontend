#include <algorithm>
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
    FoldPlan push = fold_page(1);
    FoldPlan again = fold_page(1);
    bool equal = push.cells.size() == again.cells.size();
    for (size_t i = 0; equal && i < push.cells.size(); i++)
        equal = push.cells[i].ring == again.cells[i].ring && push.cells[i].edge == again.cells[i].edge;
    check(equal, "two page plans for the same row have the same rings and edges");
    bool grid = push.cells.size() == 20 * 15;
    for (size_t i = 0; grid && i < push.cells.size(); i++)
        grid = push.cells[i].x == (int)(i / 15) * 48 && push.cells[i].y == (int)(i % 15) * 48;
    check(grid, "a page fold covers the screen with 20 x 15 cells of 48 px, column by column");
    FoldPlan row3 = fold_page(3);
    bool left_same = true;
    for (size_t i = 0; i < push.cells.size(); i++)
        if (push.cells[i].x < 432 &&
            (push.cells[i].ring != row3.cells[i].ring || push.cells[i].edge != row3.cells[i].edge))
            left_same = false;
    check(left_same, "left of 432 the page cells seed on the tiles, the same for every row");
    struct Pin {
        const FoldPlan* p;
        int x, y;
        FoldEdge edge;
        int lo, hi;
    };
    const Pin pins[] = {{&push, 480, 144, FoldEdge::Right, 0, 1}, {&push, 816, 432, FoldEdge::Down, 3, 6},
                        {&row3, 480, 144, FoldEdge::Up, 2, 5},    {&row3, 816, 432, FoldEdge::Down, 0, 2},
                        {&push, 192, 240, FoldEdge::Up, 0, 2},    {&row3, 48, 528, FoldEdge::Down, 4, 7}};
    bool pinned = true;
    for (const Pin& pn : pins) {
        const FoldCell* c = nullptr;
        for (const FoldCell& k : pn.p->cells)
            if (k.x == pn.x && k.y == pn.y)
                c = &k;
        if (!c || c->edge != pn.edge || c->ring < pn.lo || c->ring > pn.hi)
            pinned = false;
    }
    check(pinned, "page cells fold away from their nearest seed, the ring within the jitter of the seed distance");
    bool moved = false;
    for (size_t i = 0; i < push.cells.size(); i++)
        if (push.cells[i].x >= 432 && push.cells[i].ring != row3.cells[i].ring)
            moved = true;
    check(moved, "the page seeds follow the focused row");
    check(push.ring_unit == 0 && fold_duration(push) == ((push.maxring + 1) * 5000 + 59) / 60,
          "a page fold takes 83.33 ms a ring, rounded up");

    Buffer from, to, dst;
    paint(from, 0);
    paint(to, kSwap);
    Surface fs = from.surface(), ts = to.surface(), ds = dst.surface();
    int64_t total = fold_duration(push);
    fold_compose(fs, ts, ds, push, 0);
    check(same(ds, fs, 0, 0, 960, 720), "page push at 0 equals from");
    fold_compose(fs, ts, ds, push, total);
    check(same(ds, ts, 0, 0, 960, 720), "page push at T equals to");

    Buffer sf, st;
    solid(sf, kSolidFrom);
    solid(st, kSolidTo);
    Surface sfs = sf.surface(), sts = st.surface();
    const FoldCell* right = nullptr;
    const FoldCell* down = nullptr;
    for (const FoldCell& c : push.cells) {
        if (!right && c.edge == FoldEdge::Right)
            right = &c;
        if (!down && c.edge == FoldEdge::Down)
            down = &c;
    }
    check(right && down, "the page plan has cells folding right and down");
    if (right) {
        fold_compose(sfs, sts, ds, push, mid_fold(right->ring));
        check(folded_toward_right(ds, *right), "mid fold: a right cell keeps a strip of from at its right edge");
    }
    if (down) {
        fold_compose(sfs, sts, ds, push, mid_fold(down->ring));
        check(folded_toward_bottom(ds, *down), "mid fold: a down cell keeps a strip of from at its bottom edge");
    }
    const FoldCell* last = nullptr;
    for (const FoldCell& c : push.cells)
        if (c.ring == push.maxring)
            last = &c;
    fold_compose(sfs, sts, ds, push, total - 17);
    check(last && !same(ds, sts, last->x, last->y, last->x + 48, last->y + 48),
          "one frame before T the last ring is still folding");

    FoldPlan left5 = fold_left(5);
    FoldPlan left3 = fold_left(3);
    {
        bool shape = left5.cells.size() == 135;
        for (const FoldCell& c : left5.cells)
            if (c.x < 0 || c.x + 48 > 432 || c.y < 0 || c.y + 48 > 720)
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
    for (const FoldPlan* pl : {&push, &row3}) {
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
