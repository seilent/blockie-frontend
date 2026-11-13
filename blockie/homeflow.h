#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../gfx/canvas.h"
#include "../gfx/font.h"
#include "engine.h"
#include "glide.h"
#include "home.h"
#include "launchflow.h"
#include "motion.h"
#include "nav.h"
#include "pageform.h"
#include "pagemove.h"
#include "screen_selector.h"
#include "slide.h"
#include "t9.h"

namespace fd {

struct FlowOpts {
    bool big = false;
    SlotMap slots = SlotMap::RowMajor;
    bool t9_edge = false;
    bool bar_slot = false;
    bool grid_band = true;
    bool menu_glide = true;
    bool lang = false;
    bool fast = false;
    bool verify = false;
    bool dry = false;
};

Screen page_under(const Nav& n, bool push);

class HomeFlow : public NavHost {
public:
    HomeFlow(Fonts& fonts, const FlowOpts& o) : fonts_(fonts), o_(o) {}

    Nav nav;
    HomeState home;
    SelectorModel model;
    MotionCtl ctl;
    std::vector<MenuKind> kinds;

    std::function<std::vector<HomeGame>(int sys)> games;
    std::function<LaunchStart(const HomeGame&)> launch_game;
    std::function<void(Canvas&, Screen, int64_t now)> draw_page;
    std::function<bool(Screen, PadAction, bool)> page_input;
    std::function<void(Screen, int64_t now)> page_open;
    std::function<int(Screen)> page_seed_y;
    std::function<std::vector<PageRow>(Screen)> page_rows;
    std::function<PageHead(Screen)> page_head;
    std::function<void(Canvas&, const PageHead&)> draw_word;
    std::function<void(const std::vector<HomeGame>& want, int scroll)> covers_wanted;
    std::function<void()> system_left;
    std::function<void(int64_t now)> rearm;
    std::function<void(const Surface&)> last_frame;
    std::function<bool(int sys)> store_entry;
    std::function<bool(int sys)> store_empty;
    std::function<bool(int i)> port_context;
    std::function<void(const HomeGame&)> open_port;
    std::function<void()> store_moved;
    std::function<void()> store_closed;

    void setup();
    void startup(int64_t now);
    void come_back(int64_t now);
    bool launch_back(int64_t now);
    bool input(PadAction a, bool rep, int64_t now);
    bool frame(int64_t now, const Surface& dst);
    bool art(int64_t id, int side, int64_t epoch, const Art& img);
    std::vector<HomeGame> game_ran(const std::vector<HomeGame>& list);
    std::vector<int64_t> regame(int sys, const std::vector<HomeGame>& list);
    std::vector<HomeGame> recent_changed(const std::vector<HomeGame>& list);
    void redraw();
    void theme_changed();
    void enter_store(const SelectorModel& m);
    void swap_tiles(int64_t now, const std::function<void()>& change);
    void page_change(int64_t now, const std::function<void()>& change);
    void reset_menu(std::vector<SelectorItem> systems, int focus);
    void launch_page(const HomeGame& g) { launch(g); }
    SelectorModel& home_model() { return store_ ? store_->model : model; }
    void want_mode(TileMode m) { mode_wanted_ = m; }
    TileMode wanted_mode() const { return mode_wanted_; }
    bool animating(int64_t now) const { return ctl.animating(now); }
    void land_all(int64_t now, const char* why) { ctl.land_all(now, why); }
    void at(int64_t now) { now_ = now; }
    int64_t now() const { return now_; }
    bool launched() const { return launched_; }
    int page_seed() const { return page_seed_; }
    const T9& t9() const { return t9_; }
    const SlidePlan* plan() const { return plan_.get(); }
    const eng::Instance* pane() const { return pane_.pane.get(); }
    const eng::Instance* page_engine() const { return page_eng_.get(); }
    const Glide& scroll_glide() const { return scroll_; }
    const Glide& bar_glide() const { return bar_; }
    bool glow_owed() const;
    bool page_owed() const
    {
        return page_eng_ && !nav.store && nav.top() != Screen::Port && !page_eng_->prepared() &&
               !page_eng_->start_busy();
    }
    bool start_owed();
    bool prepare_owed() { return glow_owed() || page_owed() || start_owed(); }
    void prepare_pass() { seed_fresh_ = false; }
    char prepare(int rows);
    Rect band() const;
    void set_clock(std::function<int64_t()> c);
    void set_probe(bool on);
    const Glow& glow() const { return *glow_; }
    const PageRun* page_run() const;

    Nav kept_focus() const;
    bool opening() const { return opening_; }
    const SelectorModel& kept() const { return kept_; }
    const Nav& kept_nav() const { return kept_nav_; }
    const T9Text& kept_text() const { return kept_t9_; }
    int boundary(int64_t t) const;
    bool take_step_in();
    Rect take_art_rect();
    void draw(const Surface& dst, const SelectorModel& base, const Nav& n, bool at_target, Pane focus, Rect bound);

    int menu_count() const override { return (int)model.systems.size(); }
    MenuKind menu_kind(int i) const override
    {
        return i >= 0 && i < (int)kinds.size() ? kinds[i] : MenuKind::System;
    }
    int recent_count() const override { return (int)home.recent.size(); }
    int enter_system(int sys) override;
    void exit_system() override;
    void system_moved(bool scrolled) override;
    void launch_recent(int i) override;
    void launch_system_game(int i) override;
    void push(Screen s) override;
    void pop(Screen s) override;
    bool page_action(Screen s, PadAction a, bool repeat) override;
    void pane_moved(Pane p) override;
    void recent_moved() override;
    void menu_moved() override;
    bool menu_context(int i) override;
    bool menu_empty(int i) override;
    bool game_context(int i) override;
    bool close_store() override;

private:
    enum class Want { None, Enter, Exit, RecentIn, RecentOut, Push, Pop, Leave, StoreIn, StoreOut, Swap };

    struct HomeKept {
        SelectorModel model;
        Nav nav;
        HomeState home;
        std::vector<MenuKind> kinds;
    };

    Pane fold_focus(const Nav& n) const { return ctl.details() ? Pane::Details : n.pane; }
    SelectorModel posed(const SelectorModel& base, const Nav& n, bool at_target, Pane focus) const;
    SelectorModel turned(const SelectorModel& base, const Nav& n) const;
    T9Text shown_text(const std::vector<HomeGame>& list, int k) const;
    GridSpec spec() const;
    int grid_y_px(int first, int gy) const;
    void show_recent(int k);
    FoldPlay expand_play(const SelectorModel& em, const SelectorModel& collapsed, bool details, bool open);
    PageForm page_form(Screen base, Screen over, bool push) const;
    FoldPlay page_play(Screen base, bool push);
    void seed_in();
    FoldPlay launch_play();
    FoldPlay swap_play();
    void start_wanted();
    void launch(const HomeGame& g);
    void sync(bool instant);

    Fonts& fonts_;
    FlowOpts o_;
    Glide scroll_;
    Glide bar_;
    Glide grid_;
    std::vector<Glide> labels_;
    T9 t9_;
    PageMove pm_;
    SelectorModel kept_;
    Nav kept_nav_;
    T9Text kept_t9_;
    int64_t now_ = 0;
    bool launched_ = false;
    int page_seed_ = -1;
    Screen popped_ = Screen::Home;
    bool redraw_ = false;
    Rect art_rect_;
    bool step_in_ = false;
    Want want_ = Want::None;
    TileMode mode_wanted_ = TileMode::Standard;
    int drawn_gy_ = 0;
    int drawn_first_ = 0;
    int drawn_focus_ = -1;
    std::shared_ptr<Glow> glow_ = std::make_shared<Glow>();
    uint64_t glow_gen_ = 0;
    std::shared_ptr<SlidePlan> plan_;
    std::unique_ptr<HomeKept> store_;
    int store_slot_ = 0;
    std::shared_ptr<SlidePlan> next_plan_;
    PaneSurface pane_;
    std::unique_ptr<eng::Instance> page_eng_;
    std::function<int64_t()> clock_;
    std::shared_ptr<PageRun> page_run_;
    std::shared_ptr<PageRun> next_run_;
    Surface last_dst_;
    eng::Frame seed_start_;
    std::vector<Surface> seed_src_;
    bool opening_ = false;
    struct WordBand {
        std::string word;
        bool light = false;
        Buffer band{kPortraitW, std_px(tok::headerBand)};
    };
    std::vector<std::unique_ptr<WordBand>> words_;
    Surface word_band(const PageHead& h);
    uint32_t seed_ground_ = 0;
    bool seed_fresh_ = false;
};

}
