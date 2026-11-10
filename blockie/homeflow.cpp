#include "homeflow.h"

#include <algorithm>

#include "../core/curves.h"
#include "../ui/roles.h"
#include "fold.h"
#include "theme.h"
#include "tile_grid.h"

namespace fd {

void HomeFlow::setup()
{
    nav.cols = o_.big ? kBigCols : kGridColsWide;
    nav.recent_map = o_.slots;
    nav.partial_rows = true;
    nav.menu_top = std::max(0, nav.focus - (kMenuVisibleRows - 1));
    home.cols = nav.cols;
    home.rows = o_.big ? kBigRows : Nav::kRows;
    home.pad_rows = o_.grid_band ? 2 : 0;
    home.recent_cap = kGridColsWide * Nav::kRows;
    ctl.render = [this](const Surface& s, Rect bound) { draw(s, model, nav, false, fold_focus(nav), bound); };
    ctl.render_kept = [this](const Surface& s) {
        draw(s, kept_, kept_focus(), true, fold_focus(kept_nav_), s.rect());
    };
    ctl.last = [this](const Surface& s) {
        if (last_frame)
            last_frame(s);
    };
    ctl.keep = [this]() {
        kept_ = model;
        kept_nav_ = nav;
    };
    ctl.settle = [this]() { sync(true); };
    ctl.redraw_pending = [this]() { return redraw_; };
    ctl.add({"scroll", [this](int64_t t) { return scroll_.running(t); }, [this]() { scroll_.land(); }});
    ctl.add({"bar", [this](int64_t t) { return bar_.running(t); }, [this]() { bar_.land(); }});
    ctl.add({"grid", [this](int64_t t) { return grid_.running(t); }, [this]() { grid_.land(); }});
    ctl.add({"labels",
             [this](int64_t t) {
                 for (const Glide& g : labels_)
                     if (g.running(t))
                         return true;
                 return false;
             },
             [this]() {
                 for (Glide& g : labels_)
                     g.land();
             }});
    ctl.add({"t9", [this](int64_t t) { return t9_.running(t); }, [this]() { t9_.land(); }});
    sync(true);
}

void HomeFlow::startup(int64_t now)
{
    now_ = now;
    int rows = std::min(kMenuVisibleRows, std::max(0, (int)model.systems.size() - nav.menu_top));
    pm_.enter(now, rows, fx_round(tok::motion_homeEnterStagger));
    FoldPlay p;
    p.duration = pm_.end() - now;
    ctl.start(FoldKind::Startup, p, now);
}

void HomeFlow::come_back(int64_t now)
{
    now_ = now;
    FoldPlay p;
    p.duration = return_ms();
    p.forward = [](const Surface&, const Surface& b, const Surface& dst, int64_t t, int64_t) {
        return_compose(b, dst, t);
        return dst.rect();
    };
    ctl.start(FoldKind::Return, p, now);
}

bool HomeFlow::launch_back(int64_t now)
{
    now_ = now;
    if (!ctl.reverse(now))
        return false;
    ctl.render(ctl.to(), kScreen);
    return true;
}

bool HomeFlow::input(PadAction a, bool rep, int64_t now)
{
    now_ = now;
    launched_ = false;
    want_ = Want::None;
    bool changed = false;
    ctl.input(now, [&]() {
        InputResult r;
        r.changed = nav_apply(nav, *this, a, rep);
        r.launched = launched_;
        changed = r.changed;
        start_wanted();
        return r;
    });
    sync(ctl.started());
    return changed;
}

bool HomeFlow::frame(int64_t now, const Surface& dst)
{
    now_ = now;
    bool more = ctl.frame(now, dst);
    if (!ctl.active() || ctl.kind() == FoldKind::Startup || ctl.kind() == FoldKind::Return) {
        drawn_gy_ = grid_.at(now);
        drawn_first_ = home.first;
        drawn_focus_ = nav.system_view ? nav.sys_focus - home.first : nav.tile_focus;
    }
    redraw_ = false;
    return more;
}

bool HomeFlow::art(int64_t id, int side, int64_t epoch, const Art& img)
{
    bool expanded = nav.pane == Pane::Details;
    std::vector<Art> before = model.tiles;
    if (!home.drop(model, id, side, epoch, img))
        return false;
    redraw_ = true;
    for (int k = 0; k < (int)model.tiles.size(); k++)
        if (k >= (int)before.size() || model.tiles[k] != before[k]) {
            Rect r = tile_slot_rect(posed(model, nav, false, fold_focus(nav)), k, expanded);
            ctl.dirty(r);
            art_rect_ = art_rect_.unite(r);
        }
    return true;
}

std::vector<HomeGame> HomeFlow::game_ran(const std::vector<HomeGame>& list)
{
    std::vector<HomeGame> want = home.game_ran(model, list, nav);
    if (!nav.system_view && nav.pane == Pane::Details) {
        show_recent(nav.tile_focus);
        t9_.set(shown_text(home.recent, nav.tile_focus));
        warm_turned(fonts_, model);
    }
    ctl.dirty();
    redraw_ = true;
    return want;
}

void HomeFlow::redraw()
{
    redraw_ = true;
    ctl.dirty();
}

void HomeFlow::theme_changed() { ctl.land("theme"); }

Nav HomeFlow::kept_focus() const
{
    Nav k = kept_nav_;
    if (ctl.details())
        k.tile_focus = nav.tile_focus;
    return k;
}

int HomeFlow::boundary(int64_t t) const
{
    if (ctl.active() && plan_ && (ctl.kind() == FoldKind::Expand || ctl.kind() == FoldKind::Collapse))
        return slide_pose(*plan_, ctl.forward_time(t)).boundary;
    if (nav.pane != Pane::Details || nav.top() != Screen::Home)
        return kPaneSplit;
    return kPaneSplitWide;
}

bool HomeFlow::take_step_in()
{
    bool s = step_in_;
    step_in_ = false;
    return s;
}

Rect HomeFlow::take_art_rect()
{
    Rect r = art_rect_;
    art_rect_ = Rect{};
    return r;
}

SelectorModel HomeFlow::posed(const SelectorModel& base, const Nav& n, bool at_target, Pane focus) const
{
    SelectorModel m = base;
    int64_t t = now_;
    m.focus = n.focus;
    m.big = o_.big;
    m.t9_edge = o_.t9_edge;
    m.slots = n.system_view ? SlotMap::RowMajor : o_.slots;
    m.grid_band = o_.grid_band;
    m.menu_top_px = at_target ? scroll_.target() : scroll_.at(t);
    int bar = at_target ? bar_.target() : bar_.at(t);
    m.bar_baseline = o_.bar_slot ? bar : bar - m.menu_top_px;
    m.label_a256.clear();
    if (focus == Pane::Selector) {
        m.label_a256.assign(m.systems.size(), 256);
        for (size_t i = 0; i < m.systems.size() && i < labels_.size(); i++)
            m.label_a256[i] = at_target ? labels_[i].target() : labels_[i].at(t);
    }
    if (n.system_view) {
        m.tile_focus = n.sys_focus - home.first;
        int gy = at_target ? grid_.target() : grid_.at(t);
        m.grid_y_px = grid_y_px(home.first, gy);
    } else {
        m.tile_focus = focus == Pane::Details ? n.tile_focus : -1;
        m.grid_y_px = 0;
    }
    if (n.pane == Pane::Details && !at_target) {
        const T9Text& shown = t9_.shown(t);
        m.game_title = shown.title;
        m.game_system = shown.system;
        T9Pose tp = t9_.pose(t);
        m.title_shift = tp.shift;
        m.title_a256 = tp.a256;
    }
    if (ctl.folding(t) && ctl.kind() == FoldKind::Startup) {
        m.poses.resize(m.systems.size());
        for (int i = 0; i < (int)m.systems.size(); i++) {
            ItemPose ip = pm_.item(t, i - n.menu_top);
            m.poses[i] = {ip.dx, ip.a256};
        }
        m.grid_a256 = pm_.grid_a256(t);
        m.bar_a256 = pm_.bar_a256(t);
    }
    return m;
}

SelectorModel HomeFlow::turned(const SelectorModel& base, const Nav& n) const
{
    SelectorModel m = posed(base, n, true, n.pane);
    m.tiles.clear();
    return m;
}

void HomeFlow::draw(const Surface& dst, const SelectorModel& base, const Nav& n, bool at_target, Pane focus,
                    Rect bound)
{
    Canvas c(dst);
    c.set_bound(bound);
    if (n.top() != Screen::Home) {
        if (draw_page)
            draw_page(c, n.top(), now_);
        return;
    }
    SelectorModel m = posed(base, n, at_target, focus);
    bool expanded = n.pane == Pane::Details;
    m.header_dim = !expanded && focus == Pane::Details;
    draw_selector(c, fonts_, m, focus, false, n.system_view, theme(), expanded);
}

GridSpec HomeFlow::spec() const { return grid_spec(o_.big ? TileMode::Large : TileMode::Standard); }

int HomeFlow::grid_y_px(int first, int gy) const { return first / std::max(1, home.cols) * spec().pitch - gy; }

T9Text HomeFlow::shown_text(const std::vector<HomeGame>& list, int k) const
{
    if (k < 0 || k >= (int)list.size())
        return {};
    const HomeGame& g = list[k];
    return {g.id, g.name, home.system_view ? home.sys_name : g.system};
}

void HomeFlow::show_recent(int k)
{
    bool in = k >= 0 && k < (int)home.recent.size();
    model.game_title = in ? home.recent[k].name : std::string();
    model.game_system = in ? home.recent[k].system : std::string();
}

FoldPlay HomeFlow::expand_play(const SelectorModel& em, const SelectorModel& collapsed, bool details, bool open)
{
    Rect head = header_rect(fonts_, collapsed);
    auto plan = std::make_shared<SlidePlan>(slide_plan(kPaneSplitWide, o_.left, head));
    const Nav& wn = open ? nav : kept_nav_;
    SelectorModel wide = posed(open ? model : kept_, wn, true, Pane::Details);
    if (!open)
        wide.tile_focus = drawn_focus_;
    if (wn.system_view) {
        int first = open ? home.first : drawn_first_;
        int gy = open ? wn.sys_scroll * spec().pitch : drawn_gy_;
        wide.grid_y_px = grid_y_px(first, gy);
    }
    LeftPlan& lp = plan->moves;
    lp.grid = spec();
    lp.grid_y = wide.grid_y_px;
    lp.band = o_.grid_band ? Rect{0, kBandTop, kPaneSplitWide, kBandBottom} : kScreen;
    lp.focus_from = details ? tile_slot_rect(collapsed, nav.tile_focus, false) : Rect{};
    lp.focus_to = tile_slot_rect(wide, wide.tile_focus, true);
    lp.collapse = !open;
    plan_ = plan;
    FoldPlay p;
    p.details = details;
    p.to_bound = Rect{0, 0, kPaneSplitWide, kPortraitH};
    p.duration = slide_duration(*plan);
    auto tm = std::make_shared<const SelectorModel>(em);
    Fonts* f = &fonts_;
    SlideText text = [tm, f](Canvas& c, int shift, int a) {
        SelectorModel m = *tm;
        m.turn_shift = shift;
        m.turn_a256 = a;
        draw_turned(c, *f, m, theme());
    };
    p.prepare = [plan](const Surface& a, const Surface& b) {
        slide_prepare(*plan, a, b);
        return slide_duration(*plan);
    };
    p.forward = [plan, text](const Surface& a, const Surface& b, const Surface& dst, int64_t t, int64_t prev) {
        return slide_compose(a, b, dst, *plan, t, prev, theme().bg, text);
    };
    return p;
}

FoldPlay HomeFlow::page_play()
{
    plan_.reset();
    FoldPlan plan = fold_page(nav.focus - nav.menu_top);
    FoldPlay p;
    p.duration = fold_duration(plan);
    auto shared = std::make_shared<const FoldPlan>(plan);
    p.forward = [shared](const Surface& a, const Surface& b, const Surface& dst, int64_t t, int64_t) {
        fold_compose(a, b, dst, *shared, t);
        return dst.rect();
    };
    return p;
}

FoldPlay HomeFlow::launch_play()
{
    int64_t dur = launch_leave_ms(theme().light);
    FoldPlay p;
    p.duration = dur;
    p.to_bound = Rect{};
    p.forward = [dur](const Surface& a, const Surface&, const Surface& dst, int64_t t, int64_t) {
        launch_compose(a, dst, t, dur);
        return dst.rect();
    };
    return p;
}

void HomeFlow::start_wanted()
{
    Want w = want_;
    want_ = Want::None;
    switch (w) {
    case Want::Enter:
        ctl.start(FoldKind::Expand, expand_play(turned(model, nav), kept_, false, true), now_);
        break;
    case Want::Exit:
        ctl.start(FoldKind::Collapse, expand_play(turned(kept_, kept_nav_), model, false, false), now_);
        break;
    case Want::RecentIn:
        ctl.start(FoldKind::Expand, expand_play(turned(model, nav), kept_, true, true), now_);
        step_in_ = ctl.details() && ctl.kind() == FoldKind::Expand && !ctl.reversed();
        break;
    case Want::RecentOut:
        ctl.start(FoldKind::Collapse, expand_play(turned(kept_, kept_nav_), model, true, false), now_);
        break;
    case Want::Push:
        ctl.start(FoldKind::Push, page_play(), now_);
        break;
    case Want::Pop:
        ctl.start(FoldKind::Pop, page_play(), now_);
        break;
    case Want::Leave:
        ctl.start(FoldKind::Launch, launch_play(), now_);
        break;
    case Want::None:
        break;
    }
}

void HomeFlow::launch(const HomeGame& g)
{
    LaunchStart s = launch_game ? launch_game(g) : LaunchStart::Refused;
    if (s == LaunchStart::Refused)
        return;
    launched_ = true;
    if (s == LaunchStart::Leave)
        want_ = Want::Leave;
}

void HomeFlow::sync(bool instant)
{
    int64_t t = now_;
    auto go = [&](Glide& g, int target, int dur, Curve c, bool glide) {
        if (instant || !glide)
            g.set(target);
        else
            g.to(target, t, dur, c);
    };
    go(scroll_, nav.menu_top * kMenuPitchPx, fx_round(tok::motion_scroll), Curve::EaseIn, true);
    int slot = o_.bar_slot ? nav.focus - nav.menu_top : nav.focus;
    go(bar_, kMenuFirstBaseline + slot * kMenuPitchPx, fx_round(tok::motion_focusCursor), Curve::Sine,
       o_.menu_glide);
    labels_.resize(model.systems.size());
    for (size_t i = 0; i < labels_.size(); i++) {
        bool lit = nav.pane == Pane::Selector && (int)i == nav.focus;
        go(labels_[i], alpha256(lit ? role::homeItem.alpha : tok::type_homeItem_alphaDim),
           fx_round(tok::motion_focusAlpha), Curve::Linear, o_.menu_glide);
    }
    int gy = nav.system_view ? nav.sys_scroll * spec().pitch : 0;
    if (o_.grid_band && !instant)
        grid_.to(gy, t, fx_round(tok::motion_scroll), Curve::EaseIn);
    else
        grid_.set(gy);
}

int HomeFlow::enter_system(int sys)
{
    if (sys < 0 || sys >= (int)model.systems.size())
        return 0;
    std::vector<HomeGame> list = games ? games(sys) : std::vector<HomeGame>();
    if (list.empty())
        return 0;
    std::vector<HomeGame> want = home.enter(model, model.systems[sys].name, list);
    if (covers_wanted)
        covers_wanted(want, 0);
    t9_.set(shown_text(home.sysgames, 0));
    if (rearm)
        rearm(now_);
    want_ = Want::Enter;
    return (int)list.size();
}

void HomeFlow::exit_system()
{
    home.leave(model);
    t9_.set(T9Text{});
    if (system_left)
        system_left();
    if (rearm)
        rearm(now_);
    want_ = Want::Exit;
}

void HomeFlow::system_moved(bool scrolled)
{
    std::vector<HomeGame> want = home.window(model, nav.sys_scroll, nav.sys_focus, scrolled);
    if (scrolled && covers_wanted)
        covers_wanted(want, nav.sys_scroll);
    t9_.change(shown_text(home.sysgames, nav.sys_focus), now_);
    warm_turned(fonts_, model);
}

void HomeFlow::launch_recent(int i)
{
    if (i >= 0 && i < (int)home.recent.size())
        launch(home.recent[i]);
}

void HomeFlow::launch_system_game(int i)
{
    if (i >= 0 && i < (int)home.sysgames.size())
        launch(home.sysgames[i]);
}

void HomeFlow::push(Screen s)
{
    if (page_open)
        page_open(s, now_);
    if (rearm)
        rearm(now_);
    want_ = Want::Push;
}

void HomeFlow::pop(Screen)
{
    if (rearm)
        rearm(now_);
    want_ = Want::Pop;
}

bool HomeFlow::page_action(Screen s, PadAction a, bool repeat)
{
    if (page_input)
        return page_input(s, a, repeat);
    return a == PadAction::Back && !repeat;
}

void HomeFlow::pane_moved(Pane p)
{
    bool in = p == Pane::Details;
    show_recent(in ? nav.tile_focus : -1);
    t9_.set(in ? shown_text(home.recent, nav.tile_focus) : T9Text{});
    if (rearm)
        rearm(now_);
    want_ = in ? Want::RecentIn : Want::RecentOut;
}

void HomeFlow::recent_moved()
{
    show_recent(nav.tile_focus);
    t9_.change(shown_text(home.recent, nav.tile_focus), now_);
    warm_turned(fonts_, model);
}

}
