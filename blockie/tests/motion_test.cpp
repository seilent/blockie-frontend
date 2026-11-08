#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "../fold.h"
#include "../glide.h"
#include "../home.h"
#include "../motion.h"
#include "../nav.h"
#include "../repeat.h"
#include "../slide.h"
#include "../t9.h"
#include "../theme.h"

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

constexpr uint32_t kBar = 0xFFFFFF;
constexpr uint32_t kLit = 0xF0F0F0;
constexpr uint32_t kDim = 0x909090;
constexpr uint32_t kBorder = 0xFEFEFE;
constexpr uint32_t kTitle = 0xE0E0E0;
constexpr Rect kHead{120, 0, 300, 76};

Art solid(uint32_t v)
{
    auto img = std::make_shared<Image>();
    img->w = 2;
    img->h = 2;
    img->mem.assign(4, v);
    return img;
}

void copy(const Surface& src, const Surface& dst)
{
    for (int y = 0; y < dst.h; y++)
        copy_px(dst.row(y), src.row(y), dst.w);
}

bool equal(const Surface& a, const Surface& b)
{
    for (int y = 0; y < a.h; y++)
        for (int x = 0; x < a.w; x++)
            if (a.at(x, y) != b.at(x, y))
                return false;
    return true;
}

bool has_bar(const Surface& s)
{
    for (int y = 0; y < s.h; y++)
        for (int x = 496; x < 500; x++)
            if (s.at(x, y) == kBar)
                return true;
    return false;
}

void border(Canvas& c, Rect r)
{
    Rect g = r.grow(kTileFocusBorder);
    c.fill({g.x0, g.y0, g.x1, r.y0}, kBorder);
    c.fill({g.x0, r.y1, g.x1, g.y1}, kBorder);
    c.fill({g.x0, r.y0, r.x0, r.y1}, kBorder);
    c.fill({r.x1, r.y0, g.x1, r.y1}, kBorder);
}

void title(Canvas& c, int shift, int a) { c.blend({700, 100 - shift, 760, 680 - shift}, kTitle, a); }

int64_t frame_ms(int n) { return (int64_t)n * 1000 / 60; }

enum class Want { None, Enter, Exit, In, Out, Push, Pop };

class Host : public NavHost {
public:
    Nav nav;
    HomeState home;
    SelectorModel model;
    MotionCtl ctl;
    Glide bar;
    Glide label;
    T9 t9;
    Repeat repeat;
    Buffer last;
    SelectorModel kept;
    Nav kept_nav;
    int64_t now = 0;
    std::vector<HomeGame> sys;
    std::unordered_map<int64_t, Art> art;
    std::vector<std::string> landed;
    bool launched = false;
    bool redraw = false;
    int unit = 5;
    Want want = Want::None;
    std::shared_ptr<SlidePlan> plan;

    Host(int recent_n, int games, bool settings)
    {
        nav.cols = 3;
        nav.partial_rows = true;
        home.cols = 3;
        home.recent_cap = 12;
        home.load = [this](const HomeGame& g) {
            auto it = art.find(g.id);
            return it == art.end() ? nullptr : it->second;
        };
        for (int i = 0; i < games; i++) {
            HomeGame g{100 + i, "Synth " + std::to_string(i), "Synth"};
            if (i != 1)
                art[g.id] = solid(0x100000u + (uint32_t)i * 4);
            sys.push_back(g);
        }
        std::vector<HomeGame> rec;
        for (int k = 0; k < recent_n; k++) {
            HomeGame g{500 + k, "Synth Recent " + std::to_string(k), "Synth"};
            art[g.id] = solid(0x200000u + (uint32_t)k * 16);
            rec.push_back(g);
        }
        model.systems = {{"Synth", games, ""}, {"Synth Two", games, ""}};
        if (settings)
            model.systems.push_back({"settings", 0, "display"});
        home.deal_recent(model, rec);
        ctl.render = [this](const Surface& s, Rect bound) { draw(s, model, nav, false, focus_for(nav), bound); };
        ctl.render_kept = [this](const Surface& s) {
            draw(s, kept, kept_focus(), true, focus_for(kept_nav), s.rect());
        };
        ctl.last = [this](const Surface& s) { copy(last.surface(), s); };
        ctl.keep = [this]() {
            kept = model;
            kept_nav = nav;
        };
        ctl.settle = [this]() { sync_targets(true); };
        ctl.redraw_pending = [this]() { return redraw; };
        ctl.landed = [this](const char* what, const char* why) { landed.push_back(std::string(what) + ":" + why); };
        ctl.add({"bar", [this](int64_t t) { return bar.running(t); }, [this]() { bar.land(); }});
        ctl.add({"labels", [this](int64_t t) { return label.running(t); }, [this]() { label.land(); }});
        ctl.add({"t9", [this](int64_t t) { return t9.running(t); }, [this]() { t9.land(); }});
        sync_targets(true);
        draw(last.surface(), model, nav, false, nav.pane, kScreen);
    }

    Pane focus_for(const Nav& n) const { return ctl.details() ? Pane::Details : n.pane; }

    Nav kept_focus() const
    {
        Nav k = kept_nav;
        if (ctl.details())
            k.tile_focus = nav.tile_focus;
        return k;
    }

    int bar_target() const { return kMenuFirstBaseline + nav.focus * kMenuPitchPx; }

    int label_target() const { return nav.pane == Pane::Selector ? 256 : 0; }

    void sync_targets(bool instant)
    {
        if (instant) {
            bar.set(bar_target());
            label.set(label_target());
            return;
        }
        bar.to(bar_target(), now, 100, Curve::Sine);
        label.to(label_target(), now, 200, Curve::Linear);
    }

    void draw(const Surface& s, const SelectorModel& m, const Nav& n, bool at_target, Pane focus, Rect bound)
    {
        Canvas c(s);
        c.set_bound(bound);
        if (n.top() != Screen::Home) {
            c.fill(s.rect(), 0x203040);
            c.fill({300, 300, 660, 420}, 0xF0F0F0);
            return;
        }
        c.fill(s.rect(), theme().bg);
        bool on = focus == Pane::Selector;
        if (n.pane == Pane::Selector) {
            c.fill({124, 20, 296, 70}, 0x606060);
            for (int k = 0; k < kGridTiles && k < (int)m.tiles.size(); k++) {
                const Image* img = m.tiles[k].get();
                if (!img || img->w <= 0)
                    continue;
                c.fill(grid_tile_rect(k, kGridCols), on ? (img->mem[0] >> 1 & 0x7F7F7F) : img->mem[0]);
            }
            if (!on && n.tile_focus >= 0 && n.tile_focus < kGridTiles && n.tile_focus < (int)m.tiles.size())
                border(c, grid_tile_rect(n.tile_focus, kGridCols));
            for (int i = 0; i < (int)m.systems.size() && i < kMenuVisibleRows; i++) {
                int base = kMenuFirstBaseline + i * kMenuPitchPx;
                c.fill({512, base - 40, 692, base}, kDim);
                if (on && i == n.focus)
                    c.blend({512, base - 40, 692, base}, kLit, at_target ? label.target() : label.at(now));
            }
            if (on) {
                int y = at_target ? bar.target() : bar.at(now);
                c.fill({496, y - 56, 500, y + 27}, kBar);
            }
            return;
        }
        for (int k = 0; k < (int)m.tiles.size(); k++) {
            const Image* img = m.tiles[k].get();
            int sl = slot_of(k, 3, n.system_view ? SlotMap::RowMajor : m.slots);
            if (!img || img->w <= 0 || sl < 0)
                continue;
            c.fill(grid_tile_rect(sl, 3), img->mem[0]);
        }
        int f = n.system_view ? n.sys_focus - home.first : n.tile_focus;
        if (f >= 0 && f < (int)m.tiles.size())
            border(c, grid_tile_rect(f, 3));
        title(c, 0, 256);
    }

    Buffer render_now(bool at_target, Pane focus)
    {
        Buffer b;
        draw(b.surface(), model, nav, at_target, focus, kScreen);
        return b;
    }

    Buffer render_kept_as(Pane focus)
    {
        Buffer b;
        draw(b.surface(), kept, kept_nav, true, focus, kScreen);
        return b;
    }

    FoldPlay p2(bool details)
    {
        auto pl = std::make_shared<SlidePlan>(slide_plan(kPaneSplitWide, LeftMove::Cells, unit, kHead));
        plan = pl;
        FoldPlay p;
        p.details = details;
        p.to_bound = Rect{0, 0, kPaneSplitWide, kPortraitH};
        p.duration = slide_duration(*pl);
        SlideText text = title;
        p.prepare = [pl](const Surface& a, const Surface& b) {
            slide_prepare(*pl, a, b);
            return slide_duration(*pl);
        };
        p.forward = [pl, text](const Surface& a, const Surface& b, const Surface& dst, int64_t t, int64_t prev) {
            return slide_compose(a, b, dst, *pl, t, prev, theme().bg, text);
        };
        return p;
    }

    FoldPlay page()
    {
        FoldPlan pp = fold_page(nav.focus - nav.menu_top);
        FoldPlay p;
        p.duration = fold_duration(pp);
        p.forward = [pp](const Surface& a, const Surface& b, const Surface& dst, int64_t t, int64_t) {
            fold_compose(a, b, dst, pp, t);
            return dst.rect();
        };
        return p;
    }

    void start_wanted()
    {
        Want w = want;
        want = Want::None;
        switch (w) {
        case Want::Enter: ctl.start(FoldKind::Expand, p2(false), now); break;
        case Want::Exit: ctl.start(FoldKind::Collapse, p2(false), now); break;
        case Want::In: ctl.start(FoldKind::Expand, p2(true), now); break;
        case Want::Out: ctl.start(FoldKind::Collapse, p2(true), now); break;
        case Want::Push: ctl.start(FoldKind::Push, page(), now); break;
        case Want::Pop: ctl.start(FoldKind::Pop, page(), now); break;
        case Want::None: break;
        }
    }

    void input(PadAction a, bool rep, int64_t t)
    {
        now = t;
        launched = false;
        want = Want::None;
        ctl.input(t, [&]() {
            InputResult r;
            r.changed = nav_apply(nav, *this, a, rep);
            r.launched = launched;
            start_wanted();
            return r;
        });
        sync_targets(ctl.started());
    }

    void press(PadAction a, int64_t t)
    {
        input(a, false, t);
        if (is_direction(a))
            repeat.press(a, kFromKeys, t);
    }

    void ticks(int64_t t)
    {
        while (repeat.active() && repeat.deadline() <= t) {
            int64_t d = repeat.deadline();
            PadAction a = PadAction::Up;
            if (repeat.due(d, a))
                input(a, true, d);
        }
    }

    void frame(int64_t t, Buffer& out)
    {
        now = t;
        ctl.frame(t, out.surface());
        copy(out.surface(), last.surface());
    }

    int64_t land_frames(int64_t t, Buffer& out, Buffer& before, bool& bar_seen)
    {
        bar_seen = false;
        for (int n = 0; n < 200; n++) {
            copy(out.surface(), before.surface());
            bool was = ctl.active();
            frame(t, out);
            if (was && !ctl.active())
                return t;
            if (has_bar(out.surface()))
                bar_seen = true;
            t += 1000 / 60;
        }
        return -1;
    }

    bool saw(const std::string& s) const { return std::find(landed.begin(), landed.end(), s) != landed.end(); }

    T9Text recent_text(int k) const
    {
        if (k < 0 || k >= (int)home.recent.size())
            return {};
        const HomeGame& g = home.recent[k];
        return {g.id, g.name, g.system};
    }

    int menu_count() const override { return (int)model.systems.size(); }
    MenuKind menu_kind(int i) const override { return i < 2 ? MenuKind::System : MenuKind::Settings; }
    int recent_count() const override { return (int)home.recent.size(); }
    int enter_system(int) override
    {
        if (sys.empty())
            return 0;
        home.enter(model, "Synth", sys);
        want = Want::Enter;
        return (int)sys.size();
    }
    void exit_system() override
    {
        home.leave(model);
        want = Want::Exit;
    }
    void system_moved(bool scrolled) override { home.window(model, nav.sys_scroll, nav.sys_focus, scrolled); }
    void launch_recent(int) override
    {
        launched = true;
        ctl.land_all(now, "child");
    }
    void launch_system_game(int) override
    {
        launched = true;
        ctl.land_all(now, "child");
    }
    void push(Screen) override { want = Want::Push; }
    void pop(Screen) override { want = Want::Pop; }
    bool page_action(Screen, PadAction a, bool rep) override { return a == PadAction::Back && !rep; }
    void pane_moved(Pane p) override
    {
        t9.set(p == Pane::Details ? recent_text(nav.tile_focus) : T9Text{});
        want = p == Pane::Details ? Want::In : Want::Out;
    }
    void recent_moved() override { t9.change(recent_text(nav.tile_focus), now); }
};

}

int main()
{
    Buffer out, before;

    {
        Host h(5, 12, false);
        Buffer pre = h.render_now(true, Pane::Selector);
        h.press(PadAction::Left, 0);
        Buffer step = h.render_kept_as(Pane::Details);
        h.frame(0, out);
        check(h.ctl.details() && equal(out.surface(), step.surface()) && !equal(out.surface(), pre.surface()),
              "Recent in: frame 0 is the Details focus render of the state before the input, not the frame before it");
        bool bar = false;
        int64_t tl = h.land_frames(16, out, before, bar);
        Buffer live = h.render_now(false, Pane::Details);
        check(tl > 0 && !bar && h.saw("expand:end") && equal(out.surface(), live.surface()) &&
                  equal(before.surface(), out.surface()),
              "Recent in: no frame shows the bar, the composite at T equals the landed render");

        h.press(PadAction::Back, tl + 100);
        check(h.ctl.kind() == FoldKind::Collapse && h.ctl.details() && !h.ctl.reversed(),
              "back in Recent starts the Recent collapse");
        int64_t tc = h.land_frames(tl + 100, out, before, bar);
        Buffer dest_details = h.render_now(false, Pane::Details);
        Buffer dest = h.render_now(false, Pane::Selector);
        check(tc > 0 && !bar, "Recent out: no frame before the landing shows the bar");
        check(equal(before.surface(), dest_details.surface()),
              "Recent out: the last composed frame equals a fresh render of the destination with Details focus");
        check(equal(out.surface(), dest.surface()) && has_bar(out.surface()),
              "Recent out: the landing frame is the step");
    }

    {
        Host h(5, 12, false);
        Buffer pre = h.render_now(true, Pane::Selector);
        h.press(PadAction::Left, 0);
        bool same0 = false, fades = true;
        uint32_t prev_px = pre.surface().at(200, 40);
        for (int n = 0; frame_ms(n) <= 420; n++) {
            h.frame(frame_ms(n), out);
            uint32_t v = out.surface().at(200, 40);
            if (n == 0) {
                same0 = true;
                for (int y = 20; y < 70; y++)
                    for (int x = 124; x < 296; x++)
                        if (out.surface().at(x, y) != pre.surface().at(x, y))
                            same0 = false;
            }
            if ((v & 0xFF) > (prev_px & 0xFF))
                fades = false;
            prev_px = v;
        }
        check(same0 && fades && prev_px == LiveF::pack(theme().bg),
              "Recent in: the header starts at its value before the input, no step, and only fades out");
    }

    for (int focus : {1, 3}) {
        Host h(5, 12, false);
        h.press(PadAction::Left, 0);
        bool bar = false;
        int64_t tl = h.land_frames(0, out, before, bar);
        h.press(focus == 1 ? PadAction::Right : PadAction::Down, tl + 50);
        h.frame(tl + 50, out);
        h.press(PadAction::Back, tl + 100);
        Rect keep = grid_tile_rect(slot_of(focus, 3, SlotMap::RowMajor), 3).grow(kTileFocusBorder);
        bool clean = h.nav.tile_focus == focus && h.ctl.kind() == FoldKind::Collapse;
        for (int64_t t = tl + 100; h.ctl.active() && t < tl + 2000; t += 16) {
            int bnd = slide_pose(*h.plan, h.ctl.forward_time(t)).boundary;
            h.frame(t, out);
            if (!h.ctl.active())
                break;
            for (int y = 0; y < out.surface().h; y++)
                for (int x = std::max(436, bnd); x < out.surface().w; x++) {
                    bool in = x >= keep.x0 && x < keep.x1 && y >= keep.y0 && y < keep.y1;
                    if (out.surface().at(x, y) == kBorder && !in)
                        clean = false;
                }
        }
        check(clean, focus == 1 ? "Recent out from game 1: no border strip rides right of the boundary"
                                : "Recent out from game 3: no border strip rides right of the boundary");
    }

    {
        Host h(5, 12, false);
        h.press(PadAction::Left, 0);
        bool bar = false;
        int64_t tl = h.land_frames(0, out, before, bar);
        h.press(PadAction::Down, tl + 50);
        h.frame(tl + 50, out);
        h.press(PadAction::Back, tl + 100);
        int64_t tc = h.land_frames(tl + 100, out, before, bar);
        h.press(PadAction::Left, tc + 50);
        h.frame(tc + 50, out);
        Nav want_nav = h.kept_nav;
        want_nav.tile_focus = 0;
        Buffer want;
        h.draw(want.surface(), h.kept, want_nav, true, Pane::Details, kScreen);
        Rect stale = grid_tile_rect(3, kGridCols).grow(kTileFocusBorder);
        check(h.kept_nav.tile_focus == 3 && h.nav.tile_focus == 0 && equal(out.surface(), want.surface()) &&
                  out.surface().at(stale.x1 - 1, stale.y0 + 40) != kBorder,
              "a second Recent in draws its first frame with the border on game 0, not the old focus");
    }

    for (int r : {0, 8}) {
        Host h(r, 12, false);
        h.press(PadAction::Accept, 0);
        bool bar = false;
        int64_t te = h.land_frames(0, out, before, bar);
        h.press(PadAction::Back, te + 50);
        int64_t tc = h.land_frames(te + 50, out, before, bar);
        check(tc > 0 && equal(before.surface(), out.surface()) &&
                  equal(out.surface(), h.render_now(false, Pane::Selector).surface()),
              r ? "a system exit at Recent 8: the last composed frame equals the landed frame, no label jump"
                : "a system exit at Recent 0: the last composed frame equals the landed frame, no label jump");
    }

    {
        Host h(8, 12, false);
        Buffer a0 = h.render_now(true, Pane::Selector);
        h.press(PadAction::Accept, 0);
        check(h.ctl.kind() == FoldKind::Expand && !h.ctl.details() && h.ctl.run_length() == 800,
              "a system enter at Recent 8 runs 800 ms");
        Buffer b0;
        copy(h.ctl.to(), b0.surface());
        SlidePlan ref = *h.plan;
        for (int n = 0; frame_ms(n) < 320; n++)
            h.frame(frame_ms(n), out);
        h.frame(320, out);
        Buffer want;
        slide_compose(a0.surface(), b0.surface(), want.surface(), ref, 320, -1, theme().bg, title);
        bool at_e = equal(out.surface(), want.surface());
        h.press(PadAction::Back, 320);
        check(h.ctl.reversed() && h.ctl.kind() == FoldKind::Collapse && h.ctl.run_length() == 320,
              "back at 40 % of a system enter reverses it in place, e = 320 of 800");
        check(equal(h.ctl.to(), a0.surface()), "the reversal re-renders the origin, the menu as before the enter");
        bool mirror = true;
        for (int64_t s : {(int64_t)0, (int64_t)16, (int64_t)100, (int64_t)200, (int64_t)319}) {
            h.frame(320 + s, out);
            slide_compose(a0.surface(), b0.surface(), want.surface(), ref, 320 - s, -1, theme().bg, title);
            if (!equal(out.surface(), want.surface()))
                mirror = false;
            if (s == 0 && !equal(out.surface(), want.surface()))
                at_e = false;
        }
        check(at_e, "the frame at s = 0 equals the forward frame at e");
        check(mirror, "every leg then plays forward time e - s");
        h.frame(640, out);
        copy(out.surface(), before.surface());
        h.frame(657, out);
        check(!h.ctl.active() && h.saw("collapse:end") && equal(out.surface(), a0.surface()),
              "the reversal ends on a fresh render of the menu");
        check(equal(before.surface(), out.surface()),
              "a reversed system enter: the last composed frame equals the landed frame, no label jump");
    }

    {
        Host h(2, 12, false);
        h.press(PadAction::Left, 0);
        check(h.ctl.run_length() == 800 && h.ctl.run_length() == std::max<int64_t>(800, slide_left_ms(*h.plan)),
              "a Recent in at 2 played re-deals nothing and runs the system slide's 800 ms");
        Buffer a0 = h.render_kept_as(Pane::Details);
        Buffer b0;
        copy(h.ctl.to(), b0.surface());
        SlidePlan ref = *h.plan;
        for (int n = 0; frame_ms(n) < 160; n++)
            h.frame(frame_ms(n), out);
        h.press(PadAction::Back, 160);
        check(h.ctl.reversed() && h.ctl.details() && h.ctl.run_length() == 160,
              "back at e = 160 of a Recent in reverses it in place");
        check(equal(h.ctl.to(), a0.surface()), "the reversal re-renders the origin with Details focus");
        bool mirror = true, bar = false;
        for (int64_t s : {(int64_t)0, (int64_t)16, (int64_t)80, (int64_t)159}) {
            h.frame(160 + s, out);
            Buffer want;
            slide_compose(a0.surface(), b0.surface(), want.surface(), ref, 160 - s, -1, theme().bg, title);
            if (!equal(out.surface(), want.surface()))
                mirror = false;
            if (has_bar(out.surface()))
                bar = true;
        }
        check(mirror && !bar, "the reversed Recent in plays forward time e - s with no bar");
        h.frame(320, out);
        bool last_ok = equal(out.surface(), a0.surface());
        h.frame(337, out);
        check(last_ok && !h.ctl.active() && has_bar(out.surface()) &&
                  equal(out.surface(), h.render_now(false, Pane::Selector).surface()),
              "a reversed Recent in: the last composed frame is the Details origin, the landing is the step");
    }

    {
        Host h(5, 12, false);
        h.press(PadAction::Left, 0);
        bool bar = false;
        int64_t tl = h.land_frames(0, out, before, bar);
        h.press(PadAction::Back, tl + 50);
        int64_t T = h.ctl.run_length();
        int64_t tc = tl + 50 + T * 3 / 10;
        for (int64_t t = tl + 50; t < tc; t += 16)
            h.frame(t, out);
        h.press(PadAction::Left, tc);
        check(h.saw("collapse:input") && h.ctl.kind() == FoldKind::Expand && !h.ctl.reversed(),
              "left during a collapse lands it and starts a new expand");
        h.frame(tc, out);
        Buffer step = h.render_kept_as(Pane::Details);
        check(!has_bar(out.surface()) && equal(out.surface(), step.surface()),
              "the next frame shows no bar: the landed state with Details focus");
    }

    {
        Host h(0, 12, false);
        h.press(PadAction::Accept, 0);
        h.frame(0, out);
        Buffer snap;
        copy(h.ctl.to(), snap.surface());
        Art drop = solid(0xABCDEF);
        h.art[101] = drop;
        Rect r = tile_slot_rect(h.model, 1, true);
        if (h.home.drop(h.model, 101, h.home.epoch, drop))
            h.ctl.dirty(r);
        h.frame(16, out);
        bool inside = false, outside = true;
        Surface ts = h.ctl.to();
        for (int y = 0; y < ts.h; y++)
            for (int x = 0; x < ts.w; x++) {
                bool in = x >= r.x0 && x < r.x1 && y >= r.y0 && y < r.y1;
                if (ts.at(x, y) != snap.surface().at(x, y)) {
                    if (in)
                        inside = true;
                    else
                        outside = false;
                }
            }
        check(inside && outside, "an art drop changes only its tile rect in to");
    }

    {
        Host h(5, 12, false);
        h.press(PadAction::Left, 0);
        int64_t T = h.ctl.run_length();
        for (int n = 0; frame_ms(n) < T; n++) {
            h.ticks(frame_ms(n));
            h.frame(frame_ms(n), out);
        }
        h.ticks(T - 1);
        check(h.ctl.active() && !h.saw("expand:input"),
              "a held left through a Recent in: the no-op ticks never land it");
        bool bar = false;
        h.land_frames(T, out, before, bar);
        check(h.saw("expand:end") && equal(out.surface(), h.render_now(false, Pane::Details).surface()),
              "the held expand runs to T and ends on the expanded frame");
    }

    {
        Host h(5, 12, false);
        h.press(PadAction::Accept, 0);
        for (int n = 0; frame_ms(n) < 320; n++)
            h.frame(frame_ms(n), out);
        h.press(PadAction::Left, 320);
        check(h.ctl.active() && h.ctl.kind() == FoldKind::Expand && !h.ctl.reversed() && !h.saw("expand:input") &&
                  h.nav.system_view && h.nav.sys_focus == 0,
              "a fresh left at game 0 during a system enter lands nothing and reverses nothing");
    }

    {
        Host h(5, 12, false);
        h.home.recent[1].system = "Synth Two";
        h.press(PadAction::Left, 0);
        bool bar = false;
        int64_t tl = h.land_frames(0, out, before, bar);
        T9Text g0 = h.recent_text(0), g1 = h.recent_text(1);
        check(tl > 0 && h.saw("expand:end") && !h.t9.running(tl) && h.t9.shown(tl) == g0,
              "Recent in lands on game 0's title and system line with no T9");
        int64_t t0 = tl + 50;
        h.press(PadAction::Right, t0);
        check(h.nav.tile_focus == 1 && h.t9.running(t0) && h.t9.end() == t0 + 900,
              "Right in the Recent browse runs T9 to game 1");
        check(h.t9.shown(t0 + 499) == g0 && h.t9.shown(t0 + 500) == g1,
              "the system line stays on game 0 until the out ends, then swaps with the title");
        h.press(PadAction::Left, t0 + 100);
        check(h.nav.tile_focus == 0 && h.t9.shown(t0 + 500) == g0 && h.t9.end() == t0 + 900,
              "a move back during the out replaces the pending title and system, coalesced");
    }

    {
        Host h(5, 12, false);
        h.home.recent[1].name = h.home.recent[0].name;
        h.home.recent[1].system = "Synth Two";
        h.press(PadAction::Left, 0);
        bool bar = false;
        int64_t tl = h.land_frames(0, out, before, bar);
        int64_t t0 = tl + 50;
        h.press(PadAction::Right, t0);
        const T9Text& in = h.t9.shown(t0 + 500);
        check(h.t9.running(t0) && in.title == h.home.recent[0].name && in.system == "Synth Two" &&
                  in.id == h.home.recent[1].id,
              "two games with one name on different systems run T9, keyed by the game");
    }

    {
        Host h(5, 12, false);
        h.press(PadAction::Down, 0);
        h.press(PadAction::Back, 100);
        check(h.ctl.active() && h.ctl.kind() == FoldKind::Expand, "back on the menu starts the Recent in");
        h.ticks(299);
        check(h.ctl.active() && !h.saw("expand:input"), "no tick before the first repeat");
        h.ticks(300);
        check(!h.ctl.active() && h.saw("expand:input") && h.nav.tile_focus == 3,
              "a held down whose tick moves the Recent focus lands the expand");
    }

    {
        Host h(0, 12, false);
        h.press(PadAction::Accept, 0);
        bool bar = false;
        int64_t te = h.land_frames(0, out, before, bar);
        h.press(PadAction::Back, te + 50);
        check(h.ctl.kind() == FoldKind::Collapse && h.ctl.active(), "back in a system starts the exit collapse");
        int64_t t2 = te + 50 + 240;
        h.press(PadAction::Back, t2);
        check(h.ctl.active() && !h.saw("collapse:input"),
              "a refused back at Recent 0 during the exit collapse does not land it");
        int64_t tc = h.land_frames(t2, out, before, bar);
        check(tc > 0 && equal(before.surface(), out.surface()) &&
                  equal(out.surface(), h.render_now(false, Pane::Selector).surface()),
              "a system exit: the last composed frame equals the landed menu, no step");
    }

    {
        Host h(5, 12, false);
        h.press(PadAction::Left, 0);
        h.frame(0, out);
        h.now = 100;
        set_theme_light(!theme().light);
        h.ctl.land("theme");
        h.frame(100, out);
        Buffer live = h.render_now(false, h.nav.pane);
        bool lit = theme().light;
        set_theme_light(false);
        check(lit && !h.ctl.active() && h.saw("expand:theme") && equal(out.surface(), live.surface()) &&
                  out.surface().at(5, 5) == theme_light().bg,
              "a theme change lands the running fold and the next frame is the landed state in the new theme");
    }

    {
        Host h(0, 12, false);
        h.press(PadAction::Down, 0);
        h.now = 30;
        Buffer live = h.render_now(false, Pane::Selector);
        Buffer target = h.render_now(true, Pane::Selector);
        check(!equal(live.surface(), target.surface()), "30 ms after down the focus bar is mid glide");
        h.input(PadAction::Accept, false, 30);
        check(h.ctl.kind() == FoldKind::Expand && h.saw("bar:fold") && !h.ctl.copied(),
              "accept starts the expand and lands the bar glide");
        check(equal(h.ctl.from(), target.surface()), "accept 30 ms after down starts from a fresh landed render");
    }

    {
        Host h(0, 12, false);
        h.press(PadAction::Accept, 0);
        check(h.ctl.copied() && equal(h.ctl.from(), h.last.surface()),
              "nothing moving, no step, nothing pending: from is the last shown frame, copied");
        Host p(0, 12, false);
        p.redraw = true;
        p.press(PadAction::Accept, 0);
        check(!p.ctl.copied(), "a pending redraw renders from instead of copying");
        Host q(5, 12, false);
        q.press(PadAction::Left, 0);
        check(!q.ctl.copied(), "a focus step renders from instead of copying");
    }

    {
        Host h(0, 12, true);
        h.nav.focus = 2;
        h.bar.set(h.bar_target());
        Buffer m = h.render_now(true, Pane::Selector);
        copy(m.surface(), h.last.surface());
        h.press(PadAction::Accept, 0);
        check(h.ctl.kind() == FoldKind::Push && h.nav.top() == Screen::Settings, "accept on settings pushes the page");
        Buffer p = h.render_now(true, Pane::Selector);
        FoldPlan pp = fold_page(2);
        int64_t pd = fold_duration(pp);
        int64_t tb = pd * 4 / 10;
        bool same_push = true;
        for (int64_t t : {(int64_t)0, (int64_t)16, (int64_t)33, tb - 1}) {
            h.frame(t, out);
            Buffer want;
            fold_compose(m.surface(), p.surface(), want.surface(), pp, t);
            if (!equal(out.surface(), want.surface()))
                same_push = false;
        }
        check(same_push, "the page push frames are the page push fold unchanged");
        h.press(PadAction::Back, tb);
        check(h.ctl.reversed() && h.ctl.kind() == FoldKind::Pop && h.nav.top() == Screen::Home,
              "back during a page push reverses it in place");
        bool back = true;
        for (int64_t s : {(int64_t)0, (int64_t)16, tb - 1}) {
            h.frame(tb + s, out);
            Buffer want;
            fold_compose(m.surface(), p.surface(), want.surface(), pp, tb - s);
            if (!equal(out.surface(), want.surface()))
                back = false;
        }
        check(back, "the reversed push runs the push frames backward from where it was");
        h.frame(tb + tb, out);
        copy(out.surface(), before.surface());
        h.frame(tb + tb + 17, out);
        check(!h.ctl.active() && equal(out.surface(), m.surface()) && equal(before.surface(), out.surface()),
              "the reversed push ends on the menu, the last composed frame equals the landed frame");
    }

    {
        Host h(5, 12, false);
        h.ctl.can_animate = []() { return false; };
        h.press(PadAction::Accept, 0);
        h.frame(0, out);
        check(h.saw("expand:hidden") && !h.ctl.active() && h.nav.system_view &&
                  equal(out.surface(), h.render_now(false, h.nav.pane).surface()),
              "a fold started while hidden lands at once and the frame is the system view");
    }

    {
        Host h(0, 12, true);
        h.nav.focus = 2;
        h.sync_targets(true);
        Buffer m = h.render_now(true, Pane::Selector);
        copy(m.surface(), h.last.surface());
        h.press(PadAction::Accept, 0);
        bool bar = false;
        int64_t tl = h.land_frames(0, out, before, bar);
        Buffer pg = h.render_now(true, Pane::Selector);
        h.press(PadAction::Back, tl + 50);
        check(tl > 0 && h.ctl.kind() == FoldKind::Pop && !h.ctl.reversed() && h.nav.top() == Screen::Home,
              "back on a landed page starts a fresh page pop");
        FoldPlan pp = fold_page(2);
        int64_t pd = fold_duration(pp);
        bool backward = true;
        for (int64_t s : {(int64_t)0, (int64_t)16, pd / 2, pd - 17}) {
            h.frame(tl + 50 + s, out);
            Buffer want;
            fold_compose(m.surface(), pg.surface(), want.surface(), pp, pd - s);
            if (!equal(out.surface(), want.surface()))
                backward = false;
        }
        check(backward, "a fresh page pop at s is the page push at T - s");
    }

    {
        Host h(5, 12, false);
        h.press(PadAction::Accept, 0);
        h.frame(16, out);
        h.ctl.land_all(200, "hidden");
        h.frame(200, out);
        Buffer live = h.render_now(false, h.nav.pane);
        check(h.saw("expand:hidden") && !h.ctl.animating(200) && equal(out.surface(), live.surface()),
              "hidden lands with one frame of the landed state");
    }

    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
