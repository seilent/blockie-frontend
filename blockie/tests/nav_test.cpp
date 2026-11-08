#include <cstdio>
#include <vector>

#include "../nav.h"

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

class FakeHost : public NavHost {
public:
    int n_systems = 2;
    bool pages = true;
    int n_recent = 4;
    int games = 9;
    int entered = -1;
    int exits = 0;
    int moves = 0;
    int scrolls = 0;
    int launched_recent = -1;
    int launched_game = -1;
    std::vector<Screen> pushed;
    std::vector<Screen> popped;
    bool pop_on_back = true;

    int menu_count() const override { return n_systems + (pages ? 2 : 0); }
    MenuKind menu_kind(int i) const override
    {
        return i < n_systems ? MenuKind::System : i == n_systems ? MenuKind::Bluetooth : MenuKind::Settings;
    }
    int recent_count() const override { return n_recent; }
    int enter_system(int sys) override
    {
        entered = sys;
        return games;
    }
    void exit_system() override { exits++; }
    void system_moved(bool scrolled) override
    {
        moves++;
        if (scrolled)
            scrolls++;
    }
    void launch_recent(int i) override { launched_recent = i; }
    void launch_system_game(int i) override { launched_game = i; }
    void push(Screen s) override { pushed.push_back(s); }
    void pop(Screen s) override { popped.push_back(s); }
    bool page_action(Screen, PadAction a, bool repeat) override
    {
        return pop_on_back && a == PadAction::Back && !repeat;
    }
};

}

int main()
{
    {
        FakeHost h;
        h.n_recent = 0;
        Nav n;
        check(!nav_apply(n, h, PadAction::Left, false) && n.pane == Pane::Selector,
              "left from the menu does nothing while recent is empty");
        check(!nav_apply(n, h, PadAction::Back, false) && n.pane == Pane::Selector,
              "back on the menu does nothing while recent is empty");
    }
    {
        FakeHost h;
        Nav n;
        check(h.menu_count() == 4 && h.menu_kind(2) == MenuKind::Bluetooth && h.menu_kind(3) == MenuKind::Settings,
              "the menu is the systems, then bluetooth, then settings");

        nav_apply(n, h, PadAction::Left, false);
        check(n.pane == Pane::Details && !n.system_view, "left from the menu goes to the recent grid");
        for (int i = 0; i < 10; i++)
            nav_apply(n, h, PadAction::Left, true);
        check(n.pane == Pane::Details, "a held left stays in the recent grid");
        nav_apply(n, h, PadAction::Left, false);
        check(n.pane == Pane::Selector, "a fresh left at column 0 returns to the menu");
        for (int i = 0; i < 10; i++)
            nav_apply(n, h, PadAction::Left, true);
        check(n.pane == Pane::Selector, "a held left on the menu does not bounce to the grid");

        nav_apply(n, h, PadAction::Back, false);
        check(n.pane == Pane::Details, "back on the menu goes to the recent grid");
        nav_apply(n, h, PadAction::Back, false);
        check(n.pane == Pane::Selector, "back on the recent grid returns to the menu");
        nav_apply(n, h, PadAction::Accept, true);
        check(h.entered == -1, "a repeat never accepts");
    }
    {
        FakeHost h;
        Nav n;
        nav_apply(n, h, PadAction::Accept, false);
        check(h.entered == 0 && n.system_view && n.pane == Pane::Details, "accept on a system opens its games");
        nav_apply(n, h, PadAction::Right, true);
        check(n.sys_focus == 1, "a repeat moves inside the system grid");
        nav_apply(n, h, PadAction::Left, true);
        check(n.sys_focus == 0 && n.system_view, "a repeat moves back to column 0 and stays in the system");
        for (int i = 0; i < 10; i++)
            nav_apply(n, h, PadAction::Left, true);
        check(n.system_view && h.exits == 0, "a held left at column 0 never leaves the system view");
        for (int i = 0; i < 4; i++)
            nav_apply(n, h, PadAction::Down, true);
        check(n.sys_focus == 8 && n.sys_scroll == 1 && h.scrolls == 1, "held down scrolls the system grid");
        nav_apply(n, h, PadAction::Accept, false);
        check(h.launched_game == 8, "accept launches the focused system game");
        nav_apply(n, h, PadAction::Left, false);
        check(!n.system_view && n.pane == Pane::Selector && h.exits == 1, "a fresh left at column 0 leaves the system");
    }
    {
        FakeHost h;
        h.games = 0;
        Nav n;
        nav_apply(n, h, PadAction::Accept, false);
        check(!n.system_view && n.pane == Pane::Selector, "a system with no games stays on the menu");
    }
    {
        FakeHost h;
        h.n_systems = 9;
        h.pages = false;
        Nav n;
        for (int i = 0; i < 6; i++)
            nav_apply(n, h, PadAction::Down, true);
        check(n.focus == 6 && n.menu_top == 1, "the menu scrolls so the focused row stays in the six visible");
        for (int i = 0; i < 20; i++)
            nav_apply(n, h, PadAction::Down, true);
        check(n.focus == 8 && n.menu_top == 3, "the menu stops at the last item");
        for (int i = 0; i < 20; i++)
            nav_apply(n, h, PadAction::Up, true);
        check(n.focus == 0 && n.menu_top == 0, "the menu scrolls back to the top");
    }
    {
        FakeHost h;
        Nav n;
        n.focus = 3;
        nav_apply(n, h, PadAction::Accept, false);
        check(n.top() == Screen::Settings && h.pushed.size() == 1 && h.pushed[0] == Screen::Settings,
              "accept on settings pushes the settings page");
        nav_apply(n, h, PadAction::Down, true);
        check(n.top() == Screen::Settings && n.focus == 3, "page keys go to the page, not the menu");
        nav_apply(n, h, PadAction::Back, true);
        check(n.top() == Screen::Settings, "a repeated back does not pop the page");
        nav_apply(n, h, PadAction::Back, false);
        check(n.top() == Screen::Home && h.popped.size() == 1 && n.focus == 3,
              "back pops the page and the menu focus stays on the item that opened it");
        n.focus = 2;
        nav_apply(n, h, PadAction::Accept, false);
        check(n.top() == Screen::Bluetooth, "accept on bluetooth pushes the bluetooth page");
    }

    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
