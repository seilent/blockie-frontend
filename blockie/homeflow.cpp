#include "homeflow.h"

#include <algorithm>
#include <cassert>
#include <unordered_set>
#include <utility>

#include "../core/curves.h"
#include "../ui/roles.h"
#include "fold.h"
#include "theme.h"
#include "tile_grid.h"

namespace fd {

namespace {

bool has_art(const Art& a) { return a && a->w > 0 && a->h > 0; }

bool same_rect(Rect a, Rect b) { return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1; }

int bar_rest(const Nav& n, bool slot)
{
    return kMenuFirstBaseline + (slot ? n.focus - n.menu_top : n.focus) * kMenuPitchPx;
}

int label_rest(const Nav& n, int i)
{
    bool lit = n.pane == Pane::Selector && i == n.focus;
    return alpha256(lit ? role::homeItem.alpha : tok::type_homeItem_alphaDim);
}

}

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
    glow_->seg = o_.fast;
    glow_->verify = o_.verify;
    glow_->dry = o_.dry;
    ctl.render = [this](const Surface& s, Rect bound) {
        if (ctl.active() && pane_.pane)
            pane_.pane->invalidate();
        if (ctl.active() && page_eng_)
            page_eng_->invalidate();
        draw(s, model, nav, false, fold_focus(nav), bound);
    };
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
        kept_t9_ = t9_.shown(now_);
    };
    ctl.begun = [this]() {
        bool l5 = ctl.from_target() && !ctl.reversed();
        bool page = ctl.kind() == FoldKind::Push || ctl.kind() == FoldKind::Pop;
        int src = ctl.kind() == FoldKind::Expand || ctl.kind() == FoldKind::Push ? 0 : 1;
        if (pane_.pane) {
            pane_.pane->adopt_source(src);
            pane_.pane->adopt_start(l5 && !page);
        }
        if (page_eng_) {
            page_eng_->adopt_source(src);
            page_eng_->adopt_start(l5 && page && page_run_);
        }
    };
    ctl.settle = [this]() {
        if (!ctl.reversed() && next_plan_)
            plan_ = next_plan_;
        if (!ctl.reversed())
            page_run_ = next_run_;
        next_plan_.reset();
        next_run_.reset();
        if (plan_)
            plan_->moves.reversed = ctl.reversed();
        sync(true);
    };
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
    if (o_.lang && !page_eng_) {
        page_eng_ = std::make_unique<eng::Instance>(kScreen);
        page_eng_->clock_us = clock_;
        page_eng_->set_segments(o_.fast);
        page_eng_->trust_dst(o_.fast);
        page_eng_->set_verify(o_.verify);
        page_eng_->keep_cover(false);
    }
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
    plan_.reset();
    next_plan_.reset();
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
    seed_fresh_ = false;
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
    seed_fresh_ = false;
    now_ = now;
    bool more = ctl.frame(now, dst);
    if (!ctl.active() || ctl.kind() == FoldKind::Startup || ctl.kind() == FoldKind::Return) {
        drawn_gy_ = grid_.at(now);
        drawn_first_ = home.first;
        drawn_focus_ = nav.system_view ? nav.sys_focus - home.first : nav.tile_focus;
    }
    redraw_ = false;
    last_dst_ = dst;
    return more;
}

bool HomeFlow::art(int64_t id, int side, int64_t epoch, const Art& img)
{
    bool expanded = nav.pane == Pane::Details;
    std::vector<Art> before = model.tiles;
    if (!home.drop(model, id, side, epoch, img))
        return false;
    redraw_ = true;
    if (plan_ && ctl.active())
        for (Block& bk : plan_->moves.blocks)
            if (bk.id == id && bk.wide == expanded)
                bk.art = has_art(img);
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

void HomeFlow::set_clock(std::function<int64_t()> c)
{
    clock_ = std::move(c);
    glow_->clock_us = clock_;
    if (pane_.pane)
        pane_.pane->clock_us = clock_;
    if (page_eng_)
        page_eng_->clock_us = clock_;
}

void HomeFlow::set_probe(bool on)
{
    if (page_eng_)
        page_eng_->set_probe(on);
}

Rect HomeFlow::band() const { return o_.grid_band ? Rect{0, kBandTop, kPaneSplitWide, kBandBottom} : kScreen; }

bool HomeFlow::glow_owed() const
{
    Rect gr = glow_rect_of(band());
    return o_.lang && !o_.big && nav.top() == Screen::Home && !nav.store &&
           !(glow_->prep_fresh && glow_->gen == glow_gen_ + 1 && glow_->prep_w == gr.x1 - gr.x0 &&
             glow_->prep_h == gr.y1 - gr.y0);
}

char HomeFlow::prepare(int rows)
{
    assert(!ctl.active());
    if (glow_owed()) {
        glow_prepare(*glow_, glow_gen_ + 1, glow_rect_of(band()), rows);
        return 'g';
    }
    if (page_owed()) {
        page_eng_->prepare(rows);
        return 'p';
    }
    if (start_owed()) {
        if (page_eng_->start_ready())
            page_eng_->drop_start();
        else
            page_eng_->prepare_start(last_dst_, seed_start_, seed_src_, seed_ground_, last_dst_.w, rows);
        return 's';
    }
    return 0;
}

void HomeFlow::seed_in()
{
    if (seed_fresh_)
        return;
    opening_ = true;
    PageForm f = page_form(Screen::Home, Screen::Settings, true);
    opening_ = false;
    seed_start_ = page_frame(f, 0);
    Surface words[2] = {word_band(f.under_head), word_band(f.over_head)};
    seed_src_ = page_sources(ctl.from(), ctl.to(), words);
    seed_ground_ = LiveF::pack(theme().bg);
    seed_fresh_ = true;
}

bool HomeFlow::start_owed()
{
    if (!page_eng_ || !o_.fast || ctl.active() || nav.top() != Screen::Home || nav.pane != Pane::Selector ||
        menu_kind(nav.focus) != MenuKind::Settings || !last_dst_.px ||
        (ctl.target_current && ctl.target_current(now_)))
        return false;
    if (page_eng_->start_busy() && !page_eng_->start_ready())
        return true;
    if (!page_eng_->start_ready() && !page_eng_->prepared())
        return false;
    seed_in();
    if (page_eng_->start_ready())
        return page_eng_->start_key(last_dst_, seed_start_, seed_src_, seed_ground_, last_dst_.w) != 0;
    return true;
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

const PageRun* HomeFlow::page_run() const
{
    bool page = ctl.active() && (ctl.kind() == FoldKind::Push || ctl.kind() == FoldKind::Pop);
    return page ? page_run_.get() : nullptr;
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
    bool rest = at_target && !n.store;
    m.menu_top_px = rest ? n.menu_top * kMenuPitchPx : at_target ? scroll_.target() : scroll_.at(t);
    int bar = rest ? bar_rest(n, o_.bar_slot) : at_target ? bar_.target() : bar_.at(t);
    m.bar_baseline = o_.bar_slot ? bar : bar - m.menu_top_px;
    m.menu_lit = o_.lang && !o_.big && focus == Pane::Details && n.pane == Pane::Selector;
    m.label_a256.clear();
    if (focus == Pane::Selector || m.menu_lit) {
        m.label_a256.assign(m.systems.size(), 256);
        for (size_t i = 0; i < m.systems.size(); i++) {
            if (rest)
                m.label_a256[i] = label_rest(n, (int)i);
            else if (i < labels_.size())
                m.label_a256[i] = at_target ? labels_[i].target() : labels_[i].at(t);
        }
    }
    if (n.system_view) {
        m.tile_focus = n.sys_focus - home.first;
        int gy = rest ? n.sys_scroll * spec().pitch : at_target ? grid_.target() : grid_.at(t);
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
    if (&base == &kept_ && n.pane == Pane::Details) {
        m.game_title = kept_t9_.title;
        m.game_system = kept_t9_.system;
        m.game_next = kept_t9_.next;
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
    model.game_next = in ? home.recent[k].next : std::string();
}

FoldPlay HomeFlow::expand_play(const SelectorModel& em, const SelectorModel& collapsed, bool details, bool open)
{
    Rect head = header_rect(fonts_, collapsed);
    bool store = open ? nav.store : kept_nav_.store;
    bool lang = o_.lang && !o_.big && !store;
    auto plan = std::make_shared<SlidePlan>(slide_plan(kPaneSplitWide, lang ? LeftMove::Stepper : LeftMove::T5, head));
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
    lp.shared = glow_;
    lp.grid = spec();
    lp.grid_y = wide.grid_y_px;
    lp.band = band();
    lp.focus_from = details ? tile_slot_rect(collapsed, nav.tile_focus, false) : Rect{};
    lp.focus_to = tile_slot_rect(wide, wide.tile_focus, true);
    lp.collapse = !open;
    if (lang) {
        lp.gen = ++glow_gen_;
        lp.redeal = details;
        lp.focus_ink = theme().focus;
        int first = open ? home.first : drawn_first_;
        auto id_of = [&](bool wide, int k) -> int64_t {
            const std::vector<HomeGame>& list = wide && wn.system_view ? home.sysgames : home.recent;
            int i = wide && wn.system_view ? first + k : k;
            return i >= 0 && i < (int)list.size() ? list[i].id : 0;
        };
        for (int k = 0; k < (int)collapsed.tiles.size() && k < kGridTiles; k++) {
            Block bk;
            bk.id = id_of(false, k);
            bk.game = k;
            bk.col = k % kGridCols;
            bk.row = k / kGridCols;
            bk.cell = grid_tile_rect(k, kGridCols);
            bk.art = has_art(collapsed.tiles[k]);
            bk.focus = !lp.focus_from.empty() && same_rect(bk.cell.grow(kTileFocusBorder), lp.focus_from);
            lp.blocks.push_back(bk);
        }
        for (int k = 0; k < (int)wide.tiles.size(); k++) {
            int s = slot_of(k, kGridColsWide, wide.slots);
            if (s < 0)
                continue;
            Rect r = grid_tile_rect(s, kGridColsWide);
            r.y0 += wide.grid_y_px;
            r.y1 += wide.grid_y_px;
            if (r.grow(kTileFocusBorder).intersect(lp.band).empty())
                continue;
            Block bk;
            bk.id = id_of(true, k);
            bk.game = k;
            bk.wide = true;
            bk.col = s % kGridColsWide;
            bk.row = s / kGridColsWide;
            bk.cell = r;
            bk.art = has_art(wide.tiles[k]);
            bk.focus = !lp.focus_to.empty() && same_rect(r.grow(kTileFocusBorder), lp.focus_to);
            lp.blocks.push_back(bk);
        }
        block_rows(lp);
        lp.glow_on = true;
        if (!pane_.pane) {
            pane_ = pane_surface();
            pane_.pane->clock_us = clock_;
            pane_.pane->set_segments(o_.fast);
            pane_.pane->trust_dst(o_.fast);
            pane_.pane->set_verify(o_.verify);
        }
        slide_lang(*plan, pane_);
    }
    next_plan_ = plan;
    FoldPlay p;
    p.details = details;
    p.to_bound = lang ? kScreen : Rect{0, 0, kPaneSplitWide, kPortraitH};
    p.clamp_back = lang;
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
    if (lang) {
        p.tail = kBlockTailMs;
        p.advance = [plan](int64_t t0, int64_t now) { slide_advance(*plan, t0, now); };
    }
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

PageForm HomeFlow::page_form(Screen base, Screen over, bool push) const
{
    PageForm f;
    f.closing = !push;
    int split = nav.pane == Pane::Details ? kPaneSplitWide : kPaneSplit;
    f.under = page_inks(base == Screen::Home ? page_art(split, o_.grid_band) : Rect{});
    f.over = page_inks(Rect{});
    auto elements_of = [&](Screen s) {
        if (s == Screen::Home)
            return home_elements(split, o_.grid_band, nav.focus - nav.menu_top);
        std::vector<PageRow> rows = page_rows ? page_rows(s) : std::vector<PageRow>();
        if (s == Screen::Settings)
            return page_elements("settings", "chrome", rows);
        return page_elements(s == Screen::Bluetooth ? "bluetooth" : "page", "header", rows);
    };
    f.under_els = elements_of(base);
    f.over_els = elements_of(over);
    if (page_head) {
        bool under = f.closing;
        Screen s = under ? base : over;
        PageHead h = s == Screen::Home ? PageHead{} : page_head(s);
        if (!h.layer || draw_word) {
            PageHead& set = under ? f.under_head : f.over_head;
            set = h;
            set.rank = tab_ranks(f, set);
        }
    }
    return f;
}

Surface HomeFlow::word_band(const PageHead& h)
{
    if (!h.layer || h.letters.empty() || !draw_word)
        return Surface{};
    bool light = theme().light;
    for (const std::unique_ptr<WordBand>& w : words_)
        if (w->word == h.word && w->light == light)
            return w->band.surface();
    words_.push_back(std::make_unique<WordBand>());
    WordBand& w = *words_.back();
    w.word = h.word;
    w.light = light;
    Canvas c(w.band.surface());
    draw_word(c, h);
    return w.band.surface();
}

FoldPlay HomeFlow::page_play(Screen base, bool push)
{
    plan_.reset();
    next_plan_.reset();
    next_run_.reset();
    page_seed_ = (base != Screen::Home || nav.system_view) && page_seed_y ? page_seed_y(base) : -1;
    Screen over = push ? nav.top() : popped_;
    if (o_.lang && base != Screen::Port && over != Screen::Port) {
        auto run = std::make_shared<PageRun>();
        run->form = page_form(base, over, push);
        run->words[0] = word_band(run->form.under_head);
        run->words[1] = word_band(run->form.over_head);
        run->in = page_eng_.get();
        run->pair = base == Screen::Home ? 0 : 1;
        run->rows = run->form.under_els;
        run->rows.insert(run->rows.end(), run->form.over_els.begin(), run->form.over_els.end());
        bool under = false;
        if (head_side(run->form, under)) {
            const PageHead& h = form_head(run->form);
            run->rows.insert(run->rows.end(), h.letters.begin(), h.letters.end());
            run->rows.insert(run->rows.end(), h.tabs.begin(), h.tabs.end());
        }
        run->pop = !push;
        next_run_ = run;
        return page_fold(run);
    }
    FoldPlan plan = page_seed_ >= 0 ? fold_page_row(page_seed_) : fold_page(nav.focus - nav.menu_top);
    FoldPlay p;
    p.duration = fold_duration(plan);
    p.kept_from = !push && over == Screen::SteamLogin;
    auto shared = std::make_shared<const FoldPlan>(plan);
    p.forward = [shared](const Surface& a, const Surface& b, const Surface& dst, int64_t t, int64_t) {
        fold_compose(a, b, dst, *shared, t);
        return dst.rect();
    };
    return p;
}

FoldPlay HomeFlow::launch_play()
{
    plan_.reset();
    next_plan_.reset();
    next_run_.reset();
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
    next_plan_.reset();
    next_run_.reset();
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

void HomeFlow::page_change(int64_t now, const std::function<void()>& change)
{
    now_ = now;
    want_ = Want::None;
    ctl.input(now, [&]() {
        change();
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
        ctl.start(FoldKind::Push, page_play(page_under(nav, true), true), now_);
        break;
    case Want::Pop:
        ctl.start(FoldKind::Pop, page_play(page_under(nav, false), false), now_);
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
        next_plan_.reset();
        next_run_.reset();
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
    go(bar_, bar_rest(nav, o_.bar_slot), fx_round(tok::motion_focusCursor), Curve::Sine, o_.menu_glide);
    labels_.resize(model.systems.size());
    for (size_t i = 0; i < labels_.size(); i++)
        go(labels_[i], label_rest(nav, (int)i), fx_round(tok::motion_focusAlpha), Curve::Linear, o_.menu_glide);
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

void HomeFlow::pop(Screen s)
{
    popped_ = s;
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
