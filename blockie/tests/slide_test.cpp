#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include "../motion.h"
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
constexpr uint32_t kLabel = 0x909090;
constexpr uint32_t kTitle = 0xE04040;
constexpr uint32_t kSentinel = 0x00FF00;
constexpr Rect kHead{120, 0, 300, 76};
constexpr int kMenuW = 180;

const uint32_t kFrom[8] = {0x200000, 0x210000, 0x220000, 0x230000, 0x240000, 0x250000, 0x260000, 0x270000};
const uint32_t kTo[12] = {0x200000, 0x210000, 0x002200, 0x002300, 0x002400, 0x002500,
                          0x002600, 0x002700, 0x002800, 0x002900, 0x002A00, 0x002B00};

int64_t frame_ms(int n) { return (int64_t)n * 1000 / 60; }

bool same(const Surface& a, const Surface& b, Rect r)
{
    for (int y = r.y0; y < r.y1; y++)
        for (int x = r.x0; x < r.x1; x++)
            if (a.at(x, y) != b.at(x, y))
                return false;
    return true;
}

void copy(const Surface& src, const Surface& dst)
{
    for (int y = 0; y < dst.h; y++)
        copy_px(dst.row(y), src.row(y), dst.w);
}

void menu(Canvas& c, int x)
{
    for (int i = 0; i < 6; i++)
        c.fill({x, 56 + i * 96, x + kMenuW, 96 + i * 96}, kLabel);
}

void draw_collapsed(const Surface& s, Rect bound, bool every)
{
    Canvas c(s);
    c.set_bound(bound);
    c.fill(s.rect(), kGround);
    c.fill({124, 20, 296, 70}, 0x808080);
    for (int k = 0; k < 8; k++)
        c.fill(grid_tile_rect(k, kGridCols), every ? kFrom[k] ^ 0x0F0F0F : kFrom[k]);
    if (every)
        c.fill({0, 0, 432, 720}, 0x334455);
    menu(c, 512);
}

void draw_expanded(const Surface& s, Rect bound, bool every)
{
    Canvas c(s);
    c.set_bound(bound);
    c.fill(s.rect(), kGround);
    for (int k = 0; k < 12; k++)
        c.fill(grid_tile_rect(k, 3), kTo[k]);
    if (every)
        c.fill({0, 0, 432, 720}, 0x556677);
}

void title(Canvas& c, int shift, int a) { c.blend({700, 100 - shift, 760, 680 - shift}, kTitle, a); }

struct Host {
    int unit = 5;
    bool every = false;
    bool details = false;
    bool expanded = false;
    MotionCtl ctl;
    Buffer last;
    std::shared_ptr<SlidePlan> plan;
    int text_calls = 0;

    Host(int u, bool all, bool det) : unit(u), every(all), details(det)
    {
        ctl.render = [this](const Surface& s, Rect bound) {
            if (expanded)
                draw_expanded(s, bound, every);
            else
                draw_collapsed(s, bound, every);
        };
        ctl.render_kept = [this](const Surface& s) { draw_collapsed(s, s.rect(), every); };
        ctl.last = [this](const Surface& s) { copy(last.surface(), s); };
        draw_collapsed(last.surface(), kScreen, every);
    }

    FoldPlay play()
    {
        plan = std::make_shared<SlidePlan>(slide_plan(kPaneSplitWide, LeftMove::Cells, unit, kHead));
        if (every)
            plan->cells.slots = {Rect{0, 0, 432, 720}};
        auto pl = plan;
        FoldPlay p;
        p.details = details;
        p.to_bound = Rect{0, 0, kPaneSplitWide, kPortraitH};
        p.duration = slide_duration(*pl);
        SlideText text = [this](Canvas& c, int shift, int a) {
            text_calls++;
            title(c, shift, a);
        };
        p.prepare = [pl](const Surface& a, const Surface& b) {
            slide_prepare(*pl, a, b);
            return slide_duration(*pl);
        };
        p.forward = [pl, text](const Surface& a, const Surface& b, const Surface& dst, int64_t t, int64_t prev) {
            return slide_compose(a, b, dst, *pl, t, prev, kGround, text);
        };
        return p;
    }

    void frame(int64_t t)
    {
        ctl.frame(t, last.surface());
    }
};

bool mid_fold(const SlidePlan& p, int64_t t)
{
    if (p.left != LeftMove::Cells)
        return false;
    int64_t unit = p.cells.plan.ring_unit;
    int64_t tt = t * 60;
    for (size_t i = 0; i < p.cells.plan.cells.size(); i++) {
        if (!p.cells.folds[i])
            continue;
        int64_t local = tt - p.cells.plan.cells[i].ring * unit;
        if (local <= 0 || local >= unit)
            continue;
        for (int k = 0; k < kLeftLine; k++)
            if (p.cells.lines[i * kLeftLine + k])
                return true;
    }
    return false;
}

bool leg_mid(const SlidePlan& p, int64_t t)
{
    SlidePose ps = slide_pose(p, t);
    bool slide = ps.boundary > kPaneSplit + 8 && ps.boundary < p.target - 8;
    bool text = ps.text_a256 >= 8 && ps.text_a256 <= 248;
    return slide || text || mid_fold(p, t);
}

void fill_all(const Surface& s, uint32_t v)
{
    Canvas c(s);
    c.fill(s.rect(), v);
}

struct RevResult {
    bool run_e = true;
    bool mirror = true;
    bool no_repeat = true;
    bool left_moves = true;
    bool title_first = true;
    bool ends = true;
};

RevResult reverse_at(int unit, bool every, int64_t e, bool details = false)
{
    RevResult r;
    Host h(unit, every, details);
    Buffer a0;
    draw_collapsed(a0.surface(), kScreen, every);
    h.expanded = true;
    h.ctl.start(FoldKind::Expand, h.play(), 0);
    std::shared_ptr<SlidePlan> fwd = h.plan;
    for (int n = 0; frame_ms(n) < e; n++)
        h.frame(frame_ms(n));
    h.expanded = false;
    h.ctl.start(FoldKind::Collapse, h.play(), e);
    r.run_e = h.ctl.reversed() && h.ctl.details() == details && h.ctl.run_length() == e;
    Buffer b0;
    draw_expanded(b0.surface(), kScreen, every);
    SlidePlan ref = *fwd;
    SlideText text(title);
    Buffer prev, want;
    bool have = false;
    for (int64_t t = e;; t += 1000 / 60) {
        int64_t s = t - e;
        if (s > e)
            s = e;
        h.frame(e + s);
        int64_t ft = e - s;
        slide_compose(a0.surface(), b0.surface(), want.surface(), ref, ft, -1, kGround, text);
        if (!same(h.last.surface(), want.surface(), kScreen))
            r.mirror = false;
        bool titled = false;
        for (int y = 0; y < 720 && !titled; y++)
            for (int x = 700; x < 760; x++) {
                uint32_t v = h.last.surface().at(x, y);
                if ((v >> 16 & 0xFF) != (v >> 8 & 0xFF)) {
                    titled = true;
                    break;
                }
            }
        if (ft <= 400 && titled)
            r.title_first = false;
        if (have && ft > 400 && !same(h.last.surface(), prev.surface(), {436, 0, 648, 720}))
            r.title_first = false;
        if (have) {
            if (leg_mid(ref, ft) && same(h.last.surface(), prev.surface(), kScreen))
                r.no_repeat = false;
            if (mid_fold(ref, ft) && same(h.last.surface(), prev.surface(), {0, 0, kPaneSplit, kPortraitH}))
                r.left_moves = false;
        }
        copy(h.last.surface(), prev.surface());
        have = true;
        if (s >= e)
            break;
    }
    r.ends = same(h.last.surface(), a0.surface(), kScreen);
    h.frame(e + e + 17);
    r.ends = r.ends && !h.ctl.active() && same(h.last.surface(), a0.surface(), kScreen);
    return r;
}

void report(const RevResult& r, const char* name)
{
    char buf[200];
    snprintf(buf, sizeof buf, "%s: run_length == e", name);
    check(r.run_e, buf);
    snprintf(buf, sizeof buf, "%s: every reversed frame is the forward frame at e - s", name);
    check(r.mirror, buf);
    snprintf(buf, sizeof buf, "%s: no repeated frame while any leg is mid way", name);
    check(r.no_repeat, buf);
    snprintf(buf, sizeof buf, "%s: while cells fold, x < 432 changes every frame", name);
    check(r.left_moves, buf);
    snprintf(buf, sizeof buf, "%s: the title's alpha is 0 before the boundary leaves 648", name);
    check(r.title_first, buf);
    snprintf(buf, sizeof buf, "%s: the run ends on the origin frame at s = e", name);
    check(r.ends, buf);
}

}

int main()
{
    check(slide_ms() == 400, "the slide is 400 ms");
    check(slide_ease(0) == 0 && slide_ease(1) == 1 && slide_ease(0.5) > 0.5 && slide_ease(0.25) < slide_ease(0.5),
          "the bezier eases out and runs from 0 to 1");

    SlidePlan sys5 = slide_plan(648, LeftMove::Cells, 5, kHead);
    SlidePlan sys0 = slide_plan(648, LeftMove::Fade, 5, kHead);
    {
        Buffer a, b;
        draw_collapsed(a.surface(), kScreen, false);
        draw_collapsed(b.surface(), kScreen, false);
        slide_prepare(sys5, a.surface(), b.surface());
    }
    check(slide_duration(sys5) == 800 && slide_duration(sys0) == 800,
          "nothing folds: a system enter or a Recent toggle is 800 ms; L0 the same");

    SlidePose p0 = slide_pose(sys5, 0), p400 = slide_pose(sys5, 400), p800 = slide_pose(sys5, 800);
    check(p0.boundary == 432 && p0.pane_a256 == 256 && p0.text_a256 == 0 && p0.text_shift == -40 && p0.left_a256 == 0,
          "system at 0: the boundary at 432, the old pane opaque, the text hidden");
    check(p400.boundary == 648 && p400.pane_a256 == 0 && p400.text_a256 == 0 && p400.left_a256 == 256,
          "system at 400: the boundary at 648, the old pane gone, the text about to enter");
    check(p800.boundary == 648 && p800.text_a256 == 256 && p800.text_shift == 0, "system at 800: the text at rest");
    SlidePose t600 = slide_pose(sys5, 600);
    check(t600.text_shift == -30 && t600.text_a256 == 64, "the text enters by T9 in over 400 to 800");
    bool mono = true;
    for (int t = 1; t <= 400; t++)
        if (slide_pose(sys5, t).boundary < slide_pose(sys5, t - 1).boundary)
            mono = false;
    check(mono, "the boundary only moves right");

    Buffer fa, fb, dst, left;
    draw_collapsed(fa.surface(), kScreen, false);
    draw_expanded(fb.surface(), kScreen, false);
    Surface as = fa.surface(), bs = fb.surface(), ds = dst.surface();
    SlidePlan sp = slide_plan(648, LeftMove::Cells, 5, kHead);
    slide_prepare(sp, as, bs);
    int64_t L = slide_left_ms(sp);
    check(L == 667 && slide_duration(sp) == 800, "a system enter with re-dealt slots: L is 667 ms, the enter 800 ms");
    {
        bool full = true, left_eq = true, mid = true, right = true, strip = true, literal = true;
        int calls = 0;
        SlideText count = [&](Canvas& c, int shift, int a) {
            calls++;
            title(c, shift, a);
        };
        struct At {
            int64_t t;
            int b, pane;
        };
        const At poses[] = {{0, 432, 256}, {100, 530, 140}, {200, 599, 59},
                            {300, 634, 16}, {400, 648, 0}, {600, 648, 0}};
        for (const At& at : poses) {
            fill_all(ds, kSentinel);
            slide_compose(as, bs, ds, sp, at.t, -1, kGround, count);
            SlidePose ps = slide_pose(sp, at.t);
            if (ps.boundary != at.b || ps.pane_a256 != at.pane)
                literal = false;
            left_compose(as, bs, left.surface(), sp.cells, at.t, ps.left_a256);
            int shift = at.b - 432;
            for (int y = 0; y < ds.h; y++)
                for (int x = 0; x < ds.w; x++) {
                    uint32_t v = ds.at(x, y);
                    if (v == kSentinel)
                        full = false;
                    if (x < 436) {
                        if (v != left.surface().at(x, y))
                            left_eq = false;
                    } else if (x < at.b) {
                        if (v != bs.at(x, y))
                            mid = false;
                    } else if (x < at.b + 4) {
                        if (v != kGround)
                            strip = false;
                    } else if (ps.text_a256 == 0 || x < 700 || x >= 760) {
                        uint32_t o = as.at(x - shift, y);
                        uint32_t want = at.pane <= 0 || o == kGround ? kGround
                                        : at.pane >= 256           ? o
                                                                   : blend_px(kGround, o, at.pane);
                        if (v != want)
                            right = false;
                    }
                }
        }
        check(full, "a sentinel full write: every pixel is written");
        check(literal, "the boundary runs 432, 530, 599, 634, 648 at 0 to 400 ms, the old pane fades 256 to 0");
        check(left_eq, "x < 436 equals the left composite");
        check(mid, "436 to b equals the expanded frame");
        check(strip, "the 4 px after b are ground, the old pane's border overhang never rides along");
        check(right, "right of b + 4 is the old pane shifted by b - 432, faded, ground unblended");
        check(calls > 0, "the text callback draws the turned title once it shows");
    }
    {
        Host h(5, false, true);
        h.expanded = true;
        h.ctl.start(FoldKind::Expand, h.play(), 0);
        SlidePlan rp = *h.plan;
        int64_t T = h.ctl.run_length();
        check(h.ctl.details() && T == std::max<int64_t>(800, slide_left_ms(rp)) && T == slide_duration(rp) &&
                  slide_pose(rp, 400).boundary == 648,
              "a Recent in runs the system slide: the boundary at 648 by 400 ms, T = max(800, L)");
        Buffer ra, fresh;
        draw_collapsed(ra.surface(), kScreen, false);
        draw_expanded(fresh.surface(), kScreen, false);
        {
            Canvas fc(fresh.surface());
            fc.fill({648, 0, 960, 720}, kGround);
            title(fc, 0, 256);
        }
        h.frame(0);
        bool first = same(h.last.surface(), ra.surface(), kScreen);
        bool faded = true;
        for (int64_t t : {(int64_t)100, (int64_t)200, (int64_t)300}) {
            h.frame(t);
            SlidePose ps = slide_pose(rp, t);
            uint32_t want = blend_px(kGround, kLabel, ps.pane_a256);
            if (ps.pane_a256 <= 0 || ps.pane_a256 >= 256 || h.last.surface().at(ps.boundary + 80, 60) != want)
                faded = false;
        }
        h.frame(400);
        bool gone = h.last.surface().at(730, 400) == kGround;
        h.frame(600);
        uint32_t mid = h.last.surface().at(730, 400);
        bool tail = gone && mid != kGround && mid != kTitle;
        h.frame(T);
        bool landed = same(h.last.surface(), fresh.surface(), kScreen);
        check(first, "Recent in: frame 0 is the state before the input");
        check(faded, "Recent in: the old pane rides at b + 80 and fades as the boundary moves");
        check(tail, "Recent in: the title is hidden at 400 and enters by T9 in after it");
        check(landed, "Recent in: the landed frame equals a fresh render of the expanded state with its title");
    }
    {
        Buffer fresh;
        copy(bs, fresh.surface());
        Canvas fc(fresh.surface());
        fc.fill({648, 0, 960, 720}, kGround);
        title(fc, 0, 256);
        slide_compose(as, bs, ds, sp, slide_duration(sp), -1, kGround, SlideText(title));
        check(same(ds, fresh.surface(), kScreen), "system: the landed expand equals a fresh render of the system view");
        slide_compose(as, bs, ds, sp, 0, -1, kGround, SlideText(title));
        check(same(ds, as, kScreen), "system: the landed collapse equals a fresh render of the menu");
    }
    {
        fill_all(ds, kSentinel);
        Rect d = slide_compose(as, bs, ds, sp, 790, 780, kGround, SlideText(title));
        bool kept = true;
        for (int y = 0; y < 720; y++)
            for (int x = 0; x < 648; x++)
                if (ds.at(x, y) != kSentinel)
                    kept = false;
        check(d.x0 == 648 && d.x1 == 960 && d.y0 == 0 && d.y1 == 720 && kept && L <= 780,
              "once the left motion has landed the title phase composes and presents only the pane");
        fill_all(ds, kSentinel);
        d = slide_compose(as, bs, ds, sp, 500, 483, kGround, SlideText(title));
        bool mid_kept = true, left_drawn = true;
        for (int y = 0; y < 720; y++) {
            for (int x = 436; x < 648; x++)
                if (ds.at(x, y) != kSentinel)
                    mid_kept = false;
            for (int x = 0; x < 436; x++)
                if (ds.at(x, y) == kSentinel)
                    left_drawn = false;
        }
        check(d.x0 == 0 && d.x1 == 960 && mid_kept && left_drawn,
              "before the left motion lands the full screen is presented, the landed strip is not recomposed");
    }

    report(reverse_at(5, false, 380), "system e = 380 at 5 frames");
    report(reverse_at(3, false, 380), "system e = 380 at 3 frames");
    report(reverse_at(3, false, 500), "system e = 500 at 3 frames");
    report(reverse_at(5, false, 600), "system e = 600 at 5 frames");
    report(reverse_at(5, false, 160, true), "Recent e = 160 at 5 frames");
    report(reverse_at(5, false, 600, true), "Recent e = 600 at 5 frames");
    {
        Host h(5, true, false);
        h.expanded = true;
        h.ctl.start(FoldKind::Expand, h.play(), 0);
        int top = h.plan->cells.plan.maxring;
        int64_t span = ((top + 1) * 5000 + 59) / 60;
        check(h.plan->cells.top_ring == top && slide_left_ms(*h.plan) == span && slide_duration(*h.plan) == span &&
                  span > 820,
              "the every cell fixture: the left plan's top ring folds, L passes 800 at 5 frames");
        Buffer a0, b0, at;
        draw_collapsed(a0.surface(), kScreen, true);
        draw_expanded(b0.surface(), kScreen, true);
        slide_compose(a0.surface(), b0.surface(), at.surface(), *h.plan, 820, -1, kGround, SlideText(title));
        check(at.surface().at(730, 400) == kTitle, "at e = 820 the title is at rest");
        Rect lp{0, 0, 432, 720};
        check(!same(at.surface(), a0.surface(), lp) && !same(at.surface(), b0.surface(), lp),
              "at e = 820 the cells are still folding");
    }
    report(reverse_at(5, true, 820), "system e = 820 at 5 frames, every cell fixture");

    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
