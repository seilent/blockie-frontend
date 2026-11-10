#include <algorithm>
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
    int recent_moves = 0;
    bool last_scrolled = false;
    Nav* row_push = nullptr;

    int menu_count() const override { return n_systems + (pages ? 1 : 0); }
    MenuKind menu_kind(int i) const override { return i < n_systems ? MenuKind::System : MenuKind::Settings; }
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
        last_scrolled = scrolled;
        if (scrolled)
            scrolls++;
    }
    void launch_recent(int i) override { launched_recent = i; }
    void launch_system_game(int i) override { launched_game = i; }
    void push(Screen s) override { pushed.push_back(s); }
    void pop(Screen s) override { popped.push_back(s); }
    bool page_action(Screen s, PadAction a, bool repeat) override
    {
        if (row_push && s == Screen::Settings && a == PadAction::Accept && !repeat) {
            row_push->stack.push_back(Screen::Bluetooth);
            push(Screen::Bluetooth);
            return false;
        }
        return pop_on_back && a == PadAction::Back && !repeat;
    }
    void pane_moved(Pane p) override
    {
        pane_moves++;
        last_pane = p;
    }
    void recent_moved() override { recent_moves++; }
};

Nav grid_nav(int cols, bool partial)
{
    Nav n;
    n.cols = cols;
    n.partial_rows = partial;
    return n;
}

int system_move(int total, int cols, bool partial, int start, PadAction a, bool rep = false)
{
    FakeHost h;
    h.games = total;
    Nav n = grid_nav(cols, partial);
    nav_apply(n, h, PadAction::Accept, false);
    n.sys_focus = start;
    n.sys_scroll = start / cols >= Nav::kRows ? start / cols - Nav::kRows + 1 : 0;
    bool moved = nav_apply(n, h, a, rep);
    return moved && n.system_view && h.exits == 0 ? n.sys_focus : -1;
}

int recent_move(int total, SlotMap map, int start, PadAction a, bool rep = true)
{
    FakeHost h;
    h.n_recent = total;
    Nav n = grid_nav(3, true);
    n.recent_map = map;
    nav_apply(n, h, PadAction::Left, false);
    n.tile_focus = start;
    return nav_apply(n, h, a, rep) && n.pane == Pane::Details && h.pane_moves == 1 ? n.tile_focus : -1;
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

const Move kSide3[] = {{1, 0, kR, -1},   {1, 0, kL, -1},   {2, 0, kR, 1},    {2, 1, kR, -1},   {2, 1, kL, 0},
                       {3, 1, kR, 2},    {3, 2, kR, -1},   {4, 2, kR, 3},    {4, 3, kR, -1},   {4, 3, kL, 2},
                       {5, 2, kR, 3},    {5, 4, kR, -1},   {5, 3, kL, 2},    {6, 5, kR, -1},   {7, 5, kR, 6},
                       {7, 6, kR, -1},   {7, 6, kL, 5},    {8, 7, kR, -1},   {9, 8, kR, -1},   {9, 6, kL, 5},
                       {10, 8, kR, 9},   {10, 9, kR, -1},  {10, 9, kL, 8},   {11, 10, kR, -1}, {12, 8, kR, 9},
                       {12, 11, kR, -1}, {12, 9, kL, 8},   {12, 0, kL, -1},  {13, 11, kR, 12}, {13, 12, kR, -1},
                       {13, 12, kL, 11}, {40, 11, kR, 12}, {40, 35, kR, 36}, {40, 38, kR, 39}, {40, 39, kR, -1},
                       {40, 36, kL, 35}, {40, 39, kL, 38}, {40, 12, kL, 11}, {40, 0, kL, -1}};

const Move kSide2[] = {{1, 0, kR, -1},  {1, 0, kL, -1},   {2, 1, kR, -1},   {2, 1, kL, 0},    {3, 1, kR, 2},
                       {3, 2, kR, -1},  {3, 2, kL, 1},    {5, 3, kR, 4},    {5, 4, kL, 3},    {7, 5, kR, 6},
                       {9, 7, kR, 8},   {9, 8, kL, 7},    {12, 9, kR, 10},  {12, 11, kR, -1}, {13, 11, kR, 12},
                       {13, 12, kR, -1}, {13, 12, kL, 11}, {40, 7, kR, 8},  {40, 8, kL, 7},   {40, 39, kR, -1},
                       {40, 0, kL, -1},  {4, 1, kR, 2},    {4, 2, kL, 1},    {6, 3, kR, 4},    {6, 4, kL, 3},
                       {8, 5, kR, 6},    {8, 6, kL, 5},    {10, 7, kR, 8},   {10, 8, kL, 7},   {11, 9, kR, 10},
                       {11, 10, kL, 9}};

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
        check(h.menu_count() == 3 && h.menu_kind(1) == MenuKind::System && h.menu_kind(2) == MenuKind::Settings,
              "the menu is the systems, then settings");

        nav_apply(n, h, PadAction::Left, false);
        check(n.pane == Pane::Details && !n.system_view, "left from the menu goes to the recent grid");
        for (int i = 0; i < 10; i++)
            nav_apply(n, h, PadAction::Left, true);
        check(n.pane == Pane::Details, "a held left stays in the recent grid");
        check(!nav_apply(n, h, PadAction::Left, false) && n.pane == Pane::Details,
              "a fresh left at game 0 stays in the recent grid");
        nav_apply(n, h, PadAction::Back, false);
        check(n.pane == Pane::Selector, "back from game 0 returns to the menu");
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
        check(nav_apply(n, h, PadAction::Left, false) && n.sys_focus == 7 && n.system_view && h.exits == 0,
              "a fresh left at column 0 goes to the last tile of the row above and stays in the system");
        nav_apply(n, h, PadAction::Back, false);
        check(!n.system_view && n.pane == Pane::Selector && h.exits == 1, "back leaves the system");
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
        n.focus = 2;
        nav_apply(n, h, PadAction::Accept, false);
        check(n.top() == Screen::Settings && h.pushed.size() == 1 && h.pushed[0] == Screen::Settings,
              "accept on settings pushes the settings page");
        nav_apply(n, h, PadAction::Down, true);
        check(n.top() == Screen::Settings && n.focus == 2, "page keys go to the page, not the menu");
        nav_apply(n, h, PadAction::Back, true);
        check(n.top() == Screen::Settings, "a repeated back does not pop the page");
        nav_apply(n, h, PadAction::Back, false);
        check(n.top() == Screen::Home && h.popped.size() == 1 && n.focus == 2,
              "back pops the page and the menu focus stays on the item that opened it");
    }
    {
        FakeHost h;
        Nav n;
        h.row_push = &n;
        n.focus = 2;
        nav_apply(n, h, PadAction::Accept, false);
        nav_apply(n, h, PadAction::Accept, false);
        bool pushed = n.top() == Screen::Bluetooth && n.stack.size() == 2 && h.pushed.size() == 2 &&
                      h.pushed[1] == Screen::Bluetooth;
        nav_apply(n, h, PadAction::Back, false);
        bool back = n.top() == Screen::Settings && h.popped.size() == 1 && h.popped[0] == Screen::Bluetooth;
        nav_apply(n, h, PadAction::Back, false);
        bool home = n.top() == Screen::Home && h.popped.size() == 2 && n.focus == 2;
        check(pushed && back && home,
              "the settings bluetooth row pushes the bluetooth page over settings, back returns to settings, then home");
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
        check(!nav_apply(n, h, PadAction::Left, false) && h.pane_moves == 1 && n.pane == Pane::Details,
              "a fresh left at game 0 reports no pane move");
        nav_apply(n, h, PadAction::Back, false);
        check(h.pane_moves == 2 && h.last_pane == Pane::Selector, "pane_moved once when back returns");
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
        n.sys_scroll = 0;
        check(nav_apply(n, h, PadAction::Right, true) && n.sys_focus == 3,
              "3 columns: right at the last column goes on to the first tile of the next row");
        n.sys_focus = 1;
        nav_apply(n, h, PadAction::Right, true);
        check(n.sys_focus == 2, "3 columns: right moves inside the row");
        n.sys_focus = 3;
        check(nav_apply(n, h, PadAction::Left, true) && n.sys_focus == 2 && n.system_view,
              "3 columns: a held left at column 0 goes to the last tile of the row above");
        n.sys_focus = 3;
        check(nav_apply(n, h, PadAction::Left, false) && n.sys_focus == 2 && n.system_view && h.exits == 0,
              "3 columns: a fresh left at column 0 stays in the system");
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
        check(nav_apply(n, h, PadAction::Right, true) && n.tile_focus == 3,
              "recent (a): right at column 2 goes on to the first tile of the next row");
    }
    {
        check(table_ok(kSide3, [](const Move& m) { return system_move(m.total, 3, true, m.from, m.a); }),
              "3 columns: right and left move in reading order across rows and stop at the ends, n 1 to 13 and 40");
        check(table_ok(kSide3, [](const Move& m) { return system_move(m.total, 3, true, m.from, m.a, true); }),
              "3 columns: held right and left move the same way");
        check(table_ok(kSide2, [](const Move& m) { return system_move(m.total, 2, true, m.from, m.a); }),
              "2 columns: right and left move in reading order across rows and stop at the ends");
        check(table_ok(kSide2, [](const Move& m) { return system_move(m.total, 2, true, m.from, m.a, true); }),
              "2 columns: held right and left move the same way");
        check(table_ok(kSide3,
                       [](const Move& m) {
                           return m.total > 12 ? m.to : recent_move(m.total, SlotMap::RowMajor, m.from, m.a, false);
                       }),
              "recent (a): right and left move in reading order across rows and stop at the ends, n 1 to 12");
        check(table_ok(kSide3,
                       [](const Move& m) {
                           return m.total > 12 ? m.to : recent_move(m.total, SlotMap::RowMajor, m.from, m.a, true);
                       }),
              "recent (a): held right and left move the same way");
    }
    {
        FakeHost h;
        h.games = 40;
        Nav n = grid_nav(3, true);
        nav_apply(n, h, PadAction::Accept, false);
        n.sys_focus = 11;
        n.sys_scroll = 0;
        check(nav_apply(n, h, PadAction::Right, false) && n.sys_focus == 12 && n.sys_scroll == 1 && h.last_scrolled,
              "40 games: right from 11 at scroll 0 lands on 12 and scrolls one row");
        n.sys_focus = 2;
        n.sys_scroll = 0;
        check(nav_apply(n, h, PadAction::Right, false) && n.sys_focus == 3 && n.sys_scroll == 0 && !h.last_scrolled,
              "40 games: right from 2 lands on 3 with no scroll");
        n.sys_focus = 12;
        n.sys_scroll = 1;
        check(nav_apply(n, h, PadAction::Left, false) && n.sys_focus == 11 && n.sys_scroll == 1 && !h.last_scrolled,
              "40 games: left from 12 at scroll 1 lands on 11 with no scroll");
        n.sys_focus = 12;
        n.sys_scroll = 4;
        check(nav_apply(n, h, PadAction::Left, false) && n.sys_focus == 11 && n.sys_scroll == 3 && h.last_scrolled,
              "40 games: left from 12 at scroll 4 lands on 11 and scrolls up one row");
        n.sys_focus = 0;
        n.sys_scroll = 0;
        int scrolls = h.scrolls;
        nav_apply(n, h, PadAction::Right, false);
        for (int i = 0; i < 60; i++)
            nav_apply(n, h, PadAction::Right, true);
        check(n.sys_focus == 39 && n.sys_scroll == 10 && h.scrolls - scrolls == 10,
              "40 games: right held from 0 ends at 39 with scroll 10");
        int moves = h.moves;
        check(!nav_apply(n, h, PadAction::Right, true) && !nav_apply(n, h, PadAction::Right, false) &&
                  n.sys_focus == 39 && n.sys_scroll == 10 && h.moves == moves,
              "40 games: right at the last game is refused, held and fresh, with no move reported");
        nav_apply(n, h, PadAction::Left, false);
        for (int i = 0; i < 60; i++)
            nav_apply(n, h, PadAction::Left, true);
        check(n.sys_focus == 0 && n.sys_scroll == 0 && n.system_view && h.exits == 0,
              "40 games: left held from 39 ends at 0 with scroll 0, still in the system");
        moves = h.moves;
        check(!nav_apply(n, h, PadAction::Left, true) && !nav_apply(n, h, PadAction::Left, false) &&
                  n.sys_focus == 0 && n.system_view && h.exits == 0 && h.pane_moves == 0 && h.moves == moves,
              "40 games: left at game 0 is refused, held and fresh, never leaves and reports no move");
        bool rows = true;
        for (int r = 1; r < 14; r++) {
            n.sys_focus = 3 * r;
            n.sys_scroll = std::max(0, r - Nav::kRows + 1);
            if (!nav_apply(n, h, PadAction::Left, false) || n.sys_focus != 3 * r - 1 || !n.system_view)
                rows = false;
        }
        check(rows && h.exits == 0 && h.pane_moves == 0,
              "system: a fresh left at column 0 of each row above 0 lands on 3r - 1, no exit, no pane move");
    }
    for (int total : {1, 5, 12}) {
        FakeHost h;
        h.n_recent = total;
        Nav n = grid_nav(3, true);
        nav_apply(n, h, PadAction::Left, false);
        bool rows = true;
        for (int r = 1; 3 * r < total; r++) {
            n.tile_focus = 3 * r;
            if (!nav_apply(n, h, PadAction::Left, false) || n.tile_focus != 3 * r - 1 || n.pane != Pane::Details)
                rows = false;
        }
        n.tile_focus = 0;
        int moves = h.recent_moves;
        bool refused = !nav_apply(n, h, PadAction::Left, false) && !nav_apply(n, h, PadAction::Left, true) &&
                       n.tile_focus == 0 && n.pane == Pane::Details && h.recent_moves == moves;
        char name[160];
        snprintf(name, sizeof name,
                 "recent at %d: a fresh left at column 0 of each row above 0 lands on 3r - 1, at game 0 it is "
                 "refused, no pane move after the opening",
                 total);
        check(rows && refused && h.pane_moves == 1, name);
    }
    {
        FakeHost h;
        h.n_recent = 5;
        Nav n = grid_nav(3, true);
        nav_apply(n, h, PadAction::Left, false);
        int before = h.recent_moves;
        nav_apply(n, h, PadAction::Right, false);
        nav_apply(n, h, PadAction::Down, true);
        nav_apply(n, h, PadAction::Left, true);
        nav_apply(n, h, PadAction::Up, false);
        int moved = h.recent_moves - before;
        nav_apply(n, h, PadAction::Up, false);
        check(moved == 4 && h.recent_moves - before == 4 && n.tile_focus == 0,
              "recent_moved once per recent move, up, down, left and right, none on a refused one");
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
        n8.tile_focus = 2;
        check(!nav_apply(n8, h8, PadAction::Left, false) && n8.tile_focus == 2 && n8.pane == Pane::Details &&
                  h8.pane_moves == 1,
              "recent (c) at 8: a fresh left at slot column 0 is refused and stays in recent");
    }

    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
