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
    int pane_moves = 0;
    Pane last_pane = Pane::Selector;

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
    void pane_moved(Pane p) override
    {
        pane_moves++;
        last_pane = p;
    }
};

Nav grid_nav(int cols, bool partial)
{
    Nav n;
    n.cols = cols;
    n.partial_rows = partial;
    return n;
}

int system_move(int total, int cols, bool partial, int start, PadAction a)
{
    FakeHost h;
    h.games = total;
    Nav n = grid_nav(cols, partial);
    nav_apply(n, h, PadAction::Accept, false);
    n.sys_focus = start;
    n.sys_scroll = start / cols >= Nav::kRows ? start / cols - Nav::kRows + 1 : 0;
    return nav_apply(n, h, a, false) ? n.sys_focus : -1;
}

int recent_move(int total, SlotMap map, int start, PadAction a)
{
    FakeHost h;
    h.n_recent = total;
    Nav n = grid_nav(3, true);
    n.recent_map = map;
    nav_apply(n, h, PadAction::Left, false);
    n.tile_focus = start;
    return nav_apply(n, h, a, true) && n.pane == Pane::Details ? n.tile_focus : -1;
}

struct Move {
    int total;
    int from;
    PadAction a;
    int to;
};

constexpr PadAction kD = PadAction::Down;
constexpr PadAction kU = PadAction::Up;
constexpr PadAction kR = PadAction::Right;
constexpr PadAction kL = PadAction::Left;

const Move kDown3[] = {{1, 0, kD, -1},   {2, 1, kD, -1},   {3, 2, kD, -1},   {4, 0, kD, 3},    {4, 1, kD, 3},
                       {4, 2, kD, 3},    {5, 0, kD, 3},    {5, 1, kD, 4},    {5, 2, kD, 4},    {5, 4, kD, -1},
                       {6, 2, kD, 5},    {6, 5, kD, -1},   {7, 3, kD, 6},    {7, 4, kD, 6},    {7, 5, kD, 6},
                       {7, 6, kD, -1},   {8, 4, kD, 7},    {8, 5, kD, 7},    {9, 5, kD, 8},    {9, 8, kD, -1},
                       {10, 6, kD, 9},   {10, 7, kD, 9},   {10, 8, kD, 9},   {11, 7, kD, 10},  {11, 8, kD, 10},
                       {12, 8, kD, 11},  {12, 11, kD, -1}, {13, 9, kD, 12},  {13, 10, kD, 12}, {13, 11, kD, 12},
                       {40, 36, kD, 39}, {40, 37, kD, 39}, {40, 38, kD, 39}, {40, 39, kD, -1}};

const Move kDown2[] = {{1, 0, kD, -1}, {3, 0, kD, 2},  {3, 1, kD, 2},  {4, 1, kD, 3},  {5, 3, kD, 4},
                       {7, 5, kD, 6},  {9, 7, kD, 8},  {11, 9, kD, 10}, {12, 9, kD, 11}, {12, 10, kD, -1}};

const Move kDown2Strict[] = {{3, 0, kD, 2}, {3, 1, kD, -1}, {5, 3, kD, -1}, {7, 5, kD, -1}, {12, 9, kD, 11}};

const Move kRecentC[] = {{1, 0, kR, -1}, {1, 0, kD, -1},  {1, 0, kU, -1},  {3, 0, kR, 1},   {3, 1, kR, -1},
                         {3, 0, kD, 2},  {3, 1, kD, -1},  {3, 2, kU, 0},   {3, 2, kR, -1},  {8, 1, kR, -1},
                         {8, 5, kD, 7},  {8, 6, kD, -1},  {8, 7, kU, 5},   {8, 7, kR, -1},  {9, 1, kR, 8},
                         {9, 8, kD, -1}, {9, 8, kL, 1},   {9, 8, kU, -1},  {10, 3, kR, 9},  {10, 8, kD, 9},
                         {10, 9, kU, 8}, {10, 9, kL, 3},  {10, 9, kD, -1}, {10, 5, kR, -1}, {11, 9, kD, 10},
                         {11, 5, kR, 10}, {11, 10, kL, 5}, {11, 10, kD, -1}, {12, 7, kR, 11}, {12, 10, kD, 11},
                         {12, 11, kD, -1}, {12, 11, kL, 7}, {12, 0, kL, -1}};

template <size_t N> bool table_ok(const Move (&t)[N], int (*move)(const Move&))
{
    bool ok = true;
    for (const Move& m : t) {
        int got = move(m);
        if (got != m.to) {
            std::printf("  n %d from %d: got %d, want %d\n", m.total, m.from, got, m.to);
            ok = false;
        }
    }
    return ok;
}
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

    {
        FakeHost h;
        h.n_recent = 0;
        Nav n = grid_nav(3, true);
        nav_apply(n, h, PadAction::Left, false);
        nav_apply(n, h, PadAction::Back, false);
        check(n.pane == Pane::Selector && h.pane_moves == 0, "3 columns: back and left do nothing at recent 0");
    }
    {
        FakeHost h;
        h.n_recent = 5;
        Nav n = grid_nav(3, true);
        nav_apply(n, h, PadAction::Left, false);
        check(h.pane_moves == 1 && h.last_pane == Pane::Details, "pane_moved once when left opens recent");
        for (int i = 0; i < 10; i++)
            nav_apply(n, h, PadAction::Left, true);
        check(h.pane_moves == 1 && n.pane == Pane::Details, "a held left never reports a pane move");
        nav_apply(n, h, PadAction::Left, false);
        check(h.pane_moves == 2 && h.last_pane == Pane::Selector, "pane_moved once when left at column 0 returns");
        for (int i = 0; i < 10; i++)
            nav_apply(n, h, PadAction::Back, true);
        check(h.pane_moves == 2 && n.pane == Pane::Selector, "a held back on the menu never reports a pane move");
        nav_apply(n, h, PadAction::Back, false);
        nav_apply(n, h, PadAction::Back, false);
        check(h.pane_moves == 4 && h.last_pane == Pane::Selector, "back opens and closes recent, one report each");
        nav_apply(n, h, PadAction::Accept, false);
        check(h.pane_moves == 4 && n.system_view, "entering a system reports no pane move");
    }
    {
        check(table_ok(kDown2, [](const Move& m) { return system_move(m.total, 2, true, m.from, m.a); }),
              "2 columns: down onto a partial last row lands on its last tile");
        check(table_ok(kDown3, [](const Move& m) { return system_move(m.total, 3, true, m.from, m.a); }),
              "3 columns: down onto a partial last row lands on its last tile, n 1 to 13 and 40");
        check(table_ok(kDown2Strict, [](const Move& m) { return system_move(m.total, 2, false, m.from, m.a); }),
              "without partial_rows down onto a partial row is refused as before");
    }
    {
        FakeHost h;
        h.games = 13;
        Nav n = grid_nav(3, true);
        nav_apply(n, h, PadAction::Accept, false);
        const char* const names[] = {"13 games: down from row 3 column 0 lands on the 13th and scrolls one row",
                                     "13 games: down from row 3 column 1 lands on the 13th and scrolls one row",
                                     "13 games: down from row 3 column 2 lands on the 13th and scrolls one row"};
        for (int col = 0; col < 3; col++) {
            n.sys_focus = 9 + col;
            n.sys_scroll = 0;
            bool moved = nav_apply(n, h, PadAction::Down, false);
            check(moved && n.sys_focus == 12 && n.sys_scroll == 1, names[col]);
        }
        nav_apply(n, h, PadAction::Up, false);
        check(n.sys_focus == 9, "up from the 13th goes to column 0 of row 3");
        n.sys_focus = 2;
        check(!nav_apply(n, h, PadAction::Right, true) && n.sys_focus == 2, "3 columns: right stops at the last column");
        n.sys_focus = 1;
        nav_apply(n, h, PadAction::Right, true);
        check(n.sys_focus == 2, "3 columns: right moves inside the row");
        n.sys_focus = 3;
        check(!nav_apply(n, h, PadAction::Left, true) && n.system_view, "3 columns: a held left at column 0 stays");
        nav_apply(n, h, PadAction::Left, false);
        check(!n.system_view && h.exits == 1, "3 columns: a fresh left at column 0 leaves the system");
    }
    {
        check(table_ok(kDown3, [](const Move& m) {
                  return m.total > 12 ? m.to : recent_move(m.total, SlotMap::RowMajor, m.from, m.a);
              }),
              "recent (a): down onto a partial last row lands on its last tile, n 1 to 12");
        FakeHost h;
        h.n_recent = 12;
        Nav n = grid_nav(3, true);
        nav_apply(n, h, PadAction::Left, false);
        n.tile_focus = 2;
        check(!nav_apply(n, h, PadAction::Right, true) && n.tile_focus == 2, "recent (a): right stops at column 2");
    }
    {
        check(table_ok(kRecentC, [](const Move& m) { return recent_move(m.total, SlotMap::RecentC, m.from, m.a); }),
              "recent (c): moves go to the adjacent slot and refuse an empty one, n 1, 3, 8 to 12");
        FakeHost h;
        h.n_recent = 10;
        Nav n = grid_nav(3, true);
        n.recent_map = SlotMap::RecentC;
        nav_apply(n, h, PadAction::Left, false);
        n.tile_focus = 1;
        nav_apply(n, h, PadAction::Right, false);
        check(n.tile_focus == 8, "recent (c) at 10: right from game 1 reaches game 8");
        nav_apply(n, h, PadAction::Down, false);
        check(n.tile_focus == 9, "recent (c) at 10: down from game 8 reaches game 9");
        check(!nav_apply(n, h, PadAction::Down, false) && n.tile_focus == 9,
              "recent (c) at 10: down from game 9 into the empty slot is refused");
        nav_apply(n, h, PadAction::Left, false);
        check(n.tile_focus == 3, "recent (c) at 10: left from game 9 reaches game 3");
        n.tile_focus = 5;
        check(!nav_apply(n, h, PadAction::Right, false) && n.tile_focus == 5,
              "recent (c) at 10: right from game 5 into the empty slot is refused, no clamp");
        FakeHost h8;
        h8.n_recent = 8;
        Nav n8 = grid_nav(3, true);
        n8.recent_map = SlotMap::RecentC;
        nav_apply(n8, h8, PadAction::Left, false);
        n8.tile_focus = 1;
        check(!nav_apply(n8, h8, PadAction::Right, false) && n8.tile_focus == 1,
              "recent (c) at 8: right from game 1 is refused while column 2 is empty");
        n8.tile_focus = 6;
        check(!nav_apply(n8, h8, PadAction::Down, false) && n8.tile_focus == 6,
              "recent (c) at 8: down from the last row is refused");
    }

    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
