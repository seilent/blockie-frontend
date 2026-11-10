#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "../gfx/canvas.h"
#include "../gfx/font.h"
#include "glide.h"
#include "home.h"
#include "launchflow.h"
#include "motion.h"
#include "nav.h"
#include "pagemove.h"
#include "screen_selector.h"
#include "slide.h"
#include "t9.h"

namespace fd {

struct FlowOpts {
    bool big = false;
    SlotMap slots = SlotMap::RowMajor;
    LeftMove left = LeftMove::T5;
    bool t9_edge = false;
    bool bar_slot = false;
    bool grid_band = true;
    bool menu_glide = true;
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
    std::function<void(const std::vector<HomeGame>& want, int scroll)> covers_wanted;
    std::function<void()> system_left;
    std::function<void(int64_t now)> rearm;
    std::function<void(const Surface&)> last_frame;

    void setup();
    void startup(int64_t now);
    void come_back(int64_t now);
    bool launch_back(int64_t now);
    bool input(PadAction a, bool rep, int64_t now);
    bool frame(int64_t now, const Surface& dst);
    bool art(int64_t id, int side, int64_t epoch, const Art& img);
    std::vector<HomeGame> game_ran(const std::vector<HomeGame>& list);
    void redraw();
    void theme_changed();
    void want_mode(TileMode m) { mode_wanted_ = m; }
    TileMode wanted_mode() const { return mode_wanted_; }
    bool animating(int64_t now) const { return ctl.animating(now); }
    void land_all(int64_t now, const char* why) { ctl.land_all(now, why); }
    void at(int64_t now) { now_ = now; }
    int64_t now() const { return now_; }
    bool launched() const { return launched_; }
    int page_seed() const { return page_seed_; }
    const T9& t9() const { return t9_; }

    Nav kept_focus() const;
    const SelectorModel& kept() const { return kept_; }
    const Nav& kept_nav() const { return kept_nav_; }
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

private:
    enum class Want { None, Enter, Exit, RecentIn, RecentOut, Push, Pop, Leave };

    Pane fold_focus(const Nav& n) const { return ctl.details() ? Pane::Details : n.pane; }
    SelectorModel posed(const SelectorModel& base, const Nav& n, bool at_target, Pane focus) const;
    SelectorModel turned(const SelectorModel& base, const Nav& n) const;
    T9Text shown_text(const std::vector<HomeGame>& list, int k) const;
    GridSpec spec() const;
    int grid_y_px(int first, int gy) const;
    void show_recent(int k);
    FoldPlay expand_play(const SelectorModel& em, const SelectorModel& collapsed, bool details, bool open);
    FoldPlay page_play(Screen base);
    FoldPlay launch_play();
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
    int64_t now_ = 0;
    bool launched_ = false;
    int page_seed_ = -1;
    bool redraw_ = false;
    Rect art_rect_;
    bool step_in_ = false;
    Want want_ = Want::None;
    TileMode mode_wanted_ = TileMode::Standard;
    int drawn_gy_ = 0;
    int drawn_first_ = 0;
    int drawn_focus_ = -1;
    std::shared_ptr<SlidePlan> plan_;
};

}
