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
    check(fold_duration(push) == ((push.maxring + 1) * 5000 + 59) / 60,
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
