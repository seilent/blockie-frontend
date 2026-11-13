#include "homeflow.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

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

std::vector<int64_t> HomeFlow::regame(int sys, const std::vector<HomeGame>& list)
{
    std::vector<int64_t> gone;
    if (!nav.system_view || nav.focus != sys || !nav.stack.empty())
        return gone;
    std::unordered_set<int64_t> ids;
    for (const HomeGame& g : list)
        ids.insert(g.id);
    for (const HomeGame& g : home.sysgames)
        if (!ids.count(g.id)) {
            gone.push_back(g.id);
            home.cache.erase(g.id);
        }
    if (list.empty()) {
        input(PadAction::Back, false, now_);
        return gone;
    }
    int old = nav.sys_focus;
    int64_t focus_id = old >= 0 && old < (int)home.sysgames.size() ? home.sysgames[old].id : -1;
    int focus = std::clamp(old, 0, (int)list.size() - 1);
    for (int i = 0; i < (int)list.size(); i++)
        if (list[i].id == focus_id)
            focus = i;
    home.sysgames = list;
    nav.sys_total = (int)list.size();
    nav.sys_focus = focus;
    int row = focus / std::max(1, nav.cols);
    nav.sys_scroll = std::clamp(nav.sys_scroll, std::max(0, row - Nav::kRows + 1), row);
    std::vector<HomeGame> want = home.window(model, nav.sys_scroll, nav.sys_focus, true);
    if (covers_wanted)
        covers_wanted(want, nav.sys_scroll);
    t9_.set(shown_text(home.sysgames, nav.sys_focus));
    redraw();
    return gone;
}

std::vector<HomeGame> HomeFlow::recent_changed(const std::vector<HomeGame>& list)
{
    int old = nav.tile_focus;
    int64_t id = old >= 0 && old < (int)home.recent.size() ? home.recent[old].id : -1;
    std::vector<HomeGame> want = home.deal_recent(model, list);
    int n = (int)home.recent.size();
    int k = std::clamp(old, 0, std::max(0, n - 1));
    for (int i = 0; i < n; i++)
        if (home.recent[i].id == id)
            k = i;
    nav.tile_focus = k;
    bool in_recent = !nav.system_view && nav.pane == Pane::Details && nav.stack.empty();
    if (in_recent && n == 0)
        input(PadAction::Back, false, now_);
    else if (in_recent) {
        show_recent(k);
        t9_.set(shown_text(home.recent, k));
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
        m.game_next = shown.next;
        T9Pose tp = t9_.pose(t);
        m.title_shift = tp.shift;
        m.title_a256 = tp.a256;
    }
    if (n.pane == Pane::Details && at_target && n.store && n.system_view) {
        T9Text tt = shown_text(home.sysgames, n.sys_focus);
        m.game_title = tt.title;
        m.game_system = tt.system;
        m.game_next = tt.next;
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
    return {g.id, g.name, home.system_view && !nav.store ? home.sys_name : g.system, g.next};
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

Screen page_under(const Nav& n, bool push)
{
    if (!push)
        return n.top();
    return n.stack.size() >= 2 ? n.stack[n.stack.size() - 2] : Screen::Home;
}

FoldPlay HomeFlow::page_play(Screen base)
{
    plan_.reset();
    page_seed_ = (base != Screen::Home || nav.system_view) && page_seed_y ? page_seed_y(base) : -1;
    FoldPlan plan = page_seed_ >= 0 ? fold_page_row(page_seed_) : fold_page(nav.focus - nav.menu_top);
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

FoldPlay HomeFlow::swap_play()
{
    plan_.reset();
    bool wide = nav.system_view;
    int x_end = wide ? kPaneSplitWide : kPaneLeftEdge;
    auto plan = std::make_shared<LeftPlan>();
    plan->grid = wide ? spec() : grid_spec(TileMode::Standard);
    plan->band = Rect{0, kBandTop, x_end, kBandBottom};
    if (wide) {
        SelectorModel w = posed(model, nav, true, Pane::Details);
        plan->grid_y = w.grid_y_px;
        plan->focus_to = tile_slot_rect(w, w.tile_focus, true);
        SelectorModel k = posed(kept_, kept_nav_, true, Pane::Details);
        plan->from_set = true;
        plan->from_grid = spec();
        plan->from_y = k.grid_y_px;
        plan->from_focus = tile_slot_rect(k, k.tile_focus, true);
    }
    auto hm = std::make_shared<SelectorModel>(posed(model, nav, true, nav.pane));
    Rect ink = wide ? Rect{} : header_ink_rect(fonts_, *hm);
    ink.y0 = std::max(ink.y0, kBandTop);
    auto word = std::make_shared<Buffer>();
    Fonts* f = &fonts_;
    Pane focus = nav.pane;
    FoldPlay p;
    p.duration = swap_ms();
    p.prepare = [plan, hm, ink, word, f, focus](const Surface& a, const Surface& b) {
        swap_prepare(*plan, a, b);
        if (!ink.empty()) {
            Canvas wc(word->surface());
            wc.fill(ink, theme().bg);
            wc.set_clip(ink);
            draw_section_header(wc, *f, *hm, focus, theme());
        }
        return swap_ms();
    };
    p.forward = [plan, x_end, ink, word](const Surface& a, const Surface& b, const Surface& dst, int64_t t, int64_t) {
        uint32_t ground = LiveF::pack(theme().bg);
        swap_compose(a, b, dst, *plan, t, x_end, ground);
        for (int y = 0; y < dst.h; y++) {
            int x0 = y < kBandTop ? 0 : x_end;
            copy_px(dst.row(y) + x0, b.row(y) + x0, dst.w - x0);
        }
        Surface ws = word->surface();
        for (int y = ink.y0; y < ink.y1 && y < dst.h; y++)
            for (int x = ink.x0; x < ink.x1 && x < x_end; x++)
                if (ws.at(x, y) != ground && dst.at(x, y) == ground)
                    dst.at(x, y) = ws.at(x, y);
        return dst.rect();
    };
    return p;
}

void HomeFlow::swap_tiles(int64_t now, const std::function<void()>& change)
{
    now_ = now;
    want_ = Want::None;
    ctl.input(now, [&]() {
        change();
        kept_.section_label = model.section_label;
        redraw_ = true;
        want_ = Want::Swap;
        start_wanted();
        InputResult r;
        r.changed = true;
        return r;
    });
    sync(ctl.started());
}

void HomeFlow::reset_menu(std::vector<SelectorItem> systems, int focus)
{
    int was_focus = nav.focus, was_top = nav.menu_top;
    size_t was_rows = model.systems.size();
    model.systems = std::move(systems);
    kinds.assign(model.systems.size(), MenuKind::System);
    nav.focus = std::clamp(focus, 0, std::max(0, (int)model.systems.size() - 1));
    nav.menu_top = std::clamp(nav.menu_top, std::max(0, nav.focus - (kMenuVisibleRows - 1)), nav.focus);
    if (nav.focus != was_focus || nav.menu_top != was_top || model.systems.size() != was_rows)
        sync(true);
    redraw();
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
        ctl.start(FoldKind::Push, page_play(page_under(nav, true)), now_);
        break;
    case Want::Pop:
        ctl.start(FoldKind::Pop, page_play(page_under(nav, false)), now_);
        break;
    case Want::Leave:
        ctl.start(FoldKind::Launch, launch_play(), now_);
        break;
    case Want::Swap:
        ctl.start(FoldKind::Swap, swap_play(), now_);
        break;
    case Want::StoreIn:
    case Want::StoreOut: {
        plan_.reset();
        page_seed_ = -1;
        auto shared = std::make_shared<const FoldPlan>(fold_page(store_slot_));
        FoldPlay p;
        p.duration = fold_duration(*shared);
        p.forward = [shared](const Surface& a, const Surface& b, const Surface& dst, int64_t t, int64_t) {
            fold_compose(a, b, dst, *shared, t);
            return dst.rect();
        };
        ctl.start(w == Want::StoreIn ? FoldKind::Push : FoldKind::Pop, p, now_);
        break;
    }
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
    std::string genre = home.sys_name;
    home.leave(model);
    if (nav.store)
        model.section_label = genre;
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
    if (i < 0 || i >= (int)home.sysgames.size())
        return;
    if (!nav.store) {
        launch(home.sysgames[i]);
        return;
    }
    if (open_port)
        open_port(home.sysgames[i]);
}

void HomeFlow::enter_store(const SelectorModel& m)
{
    store_ = std::make_unique<HomeKept>(HomeKept{model, nav, home, kinds});
    store_slot_ = nav.focus - nav.menu_top;
    HomeState fresh;
    fresh.cols = home.cols;
    fresh.rows = home.rows;
    fresh.pad_rows = home.pad_rows;
    fresh.recent_cap = home.recent_cap;
    fresh.load = home.load;
    home = std::move(fresh);
    model = m;
    kinds.assign(model.systems.size(), MenuKind::System);
    Nav sn;
    sn.cols = nav.cols;
    sn.recent_map = nav.recent_map;
    sn.partial_rows = nav.partial_rows;
    sn.store = true;
    nav = sn;
    t9_.set(T9Text{});
    if (rearm)
        rearm(now_);
    want_ = Want::StoreIn;
}

bool HomeFlow::menu_context(int i) { return store_entry && store_entry(i); }

bool HomeFlow::menu_empty(int i) { return store_empty && store_empty(i); }

bool HomeFlow::game_context(int i)
{
    return !nav.store && i >= 0 && i < (int)home.sysgames.size() && port_context && port_context(i);
}

void HomeFlow::menu_moved()
{
    if (nav.store && store_moved)
        store_moved();
}

bool HomeFlow::close_store()
{
    if (!store_)
        return false;
    model = std::move(store_->model);
    nav = std::move(store_->nav);
    home = std::move(store_->home);
    kinds = std::move(store_->kinds);
    store_.reset();
    store_slot_ = nav.focus - nav.menu_top;
    t9_.set(T9Text{});
    if (system_left)
        system_left();
    if (rearm)
        rearm(now_);
    want_ = Want::StoreOut;
    if (store_closed)
        store_closed();
    return true;
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
