#pragma once
#include <vector>

#include "input_pad.h"
#include "screen_selector.h"

namespace fd {

enum class Screen { Home, Settings, Bluetooth };
enum class MenuKind { System, Bluetooth, Settings };

class NavHost {
public:
    virtual ~NavHost() = default;
    virtual int menu_count() const = 0;
    virtual MenuKind menu_kind(int i) const = 0;
    virtual int recent_count() const = 0;
    virtual int enter_system(int sys) = 0;
    virtual void exit_system() = 0;
    virtual void system_moved(bool scrolled) = 0;
    virtual void launch_recent(int i) = 0;
    virtual void launch_system_game(int i) = 0;
    virtual void push(Screen s) = 0;
    virtual void pop(Screen s) = 0;
    virtual bool page_action(Screen s, PadAction a, bool repeat) = 0;
};

struct Nav {
    static constexpr int kCols = 2;
    static constexpr int kRows = 4;

    Pane pane = Pane::Selector;
    int focus = 0;
    int menu_top = 0;
    int tile_focus = 0;
    bool system_view = false;
    int sys_focus = 0;
    int sys_scroll = 0;
    int sys_total = 0;
    std::vector<Screen> stack;

    Screen top() const { return stack.empty() ? Screen::Home : stack.back(); }
};

bool nav_apply(Nav& n, NavHost& h, PadAction a, bool repeat);

}
