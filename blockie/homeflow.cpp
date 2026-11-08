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
    home.pad_rows = o_.grid_band ? 1 : 0;
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
    redraw_ = false;
    return more;
}

bool HomeFlow::art(int64_t id, int64_t epoch, const Art& img)
{
    bool expanded = nav.pane == Pane::Details;
    std::vector<Art> before = model.tiles;
    if (!home.drop(model, id, epoch, img))
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
    if (ctl.active() && plan_)
        return slide_pose(*plan_, ctl.forward_time(t)).boundary;
    if (nav.pane != Pane::Details || nav.top() != Screen::Home)
        return kPaneSplit;
    return nav.system_view ? kPaneSplitWide : recent_edge();
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
        m.grid_y_px = home.first / std::max(1, home.cols) * kGridRowPitch - gy;
        if (!at_target) {
            m.game_title = t9_.shown(t);
            T9Pose tp = t9_.pose(t);
            m.title_shift = tp.shift;
            m.title_a256 = tp.a256;
        }
    } else {
        m.tile_focus = focus == Pane::Details ? n.tile_focus : -1;
        m.grid_y_px = 0;
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

int HomeFlow::recent_edge() const
{
    return o_.big ? big_tile_rect(kBigCols - 1).x1 : grid_tile_rect(kGridColsWide - 1, kGridColsWide).x1;
}

FoldPlay HomeFlow::expand_play(const SelectorModel& em, const SelectorModel& collapsed, SlideMode mode)
{
    int target = mode == SlideMode::System ? kPaneSplitWide : recent_edge();
    Rect head = header_rect(fonts_, collapsed);
    auto plan = std::make_shared<SlidePlan>(slide_plan(mode, target, o_.left, o_.unit, head));
    plan_ = plan;
    FoldPlay p;
    p.details = mode == SlideMode::Recent;
    p.to_bound = Rect{0, 0, kPaneSplitWide, kPortraitH};
    p.duration = slide_duration(*plan);
    SlideText text;
    if (mode == SlideMode::System) {
        auto tm = std::make_shared<const SelectorModel>(em);
        Fonts* f = &fonts_;
        text = [tm, f](Canvas& c, int shift, int a) {
            SelectorModel m = *tm;
            m.turn_shift = shift;
            m.turn_a256 = a;
            draw_turned(c, *f, m, true, theme());
        };
    }
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

void HomeFlow::start_wanted()
{
    Want w = want_;
    want_ = Want::None;
    switch (w) {
    case Want::Enter:
        ctl.start(FoldKind::Expand, expand_play(turned(model, nav), kept_, SlideMode::System), now_);
        break;
    case Want::Exit:
        ctl.start(FoldKind::Collapse, expand_play(turned(kept_, kept_nav_), model, SlideMode::System), now_);
        break;
    case Want::RecentIn:
        ctl.start(FoldKind::Expand, expand_play(turned(model, nav), kept_, SlideMode::Recent), now_);
        step_in_ = ctl.details() && ctl.kind() == FoldKind::Expand && !ctl.reversed();
        break;
    case Want::RecentOut:
        ctl.start(FoldKind::Collapse, expand_play(turned(kept_, kept_nav_), model, SlideMode::Recent), now_);
        break;
    case Want::Push:
        ctl.start(FoldKind::Push, page_play(), now_);
        break;
    case Want::Pop:
        ctl.start(FoldKind::Pop, page_play(), now_);
        break;
    case Want::None:
        break;
    }
}

void HomeFlow::launch(const HomeGame& g)
{
    if (launch_game && launch_game(g))
        launched_ = true;
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
    int gy = nav.system_view ? nav.sys_scroll * kGridRowPitch : 0;
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
    t9_.set(model.game_title);
    if (rearm)
        rearm(now_);
    want_ = Want::Enter;
    return (int)list.size();
}

void HomeFlow::exit_system()
{
    home.leave(model);
    t9_.set(std::string());
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
    t9_.change(model.game_title, now_);
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
    if (rearm)
        rearm(now_);
    want_ = p == Pane::Details ? Want::RecentIn : Want::RecentOut;
}

}
