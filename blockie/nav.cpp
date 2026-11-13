#include "nav.h"

#include <algorithm>

namespace fd {

namespace {

void keep_visible(Nav& n)
{
    if (n.focus < n.menu_top)
        n.menu_top = n.focus;
    if (n.focus > n.menu_top + kMenuVisibleRows - 1)
        n.menu_top = n.focus - (kMenuVisibleRows - 1);
    n.menu_top = std::max(0, n.menu_top);
}

bool leave_system(Nav& n, NavHost& h)
{
    n.system_view = false;
    n.pane = Pane::Selector;
    h.exit_system();
    return true;
}

bool apply_menu(Nav& n, NavHost& h, PadAction a, bool repeat)
{
    int count = h.menu_count();
    switch (a) {
    case PadAction::Up:
        if (n.focus <= 0)
            return false;
        n.focus--;
        keep_visible(n);
        h.menu_moved();
        return true;
    case PadAction::Down:
        if (n.focus >= count - 1)
            return false;
        n.focus++;
        keep_visible(n);
        h.menu_moved();
        return true;
    case PadAction::Context:
        return !repeat && !n.store && n.focus >= 0 && n.focus < count && h.menu_context(n.focus);
    case PadAction::Back:
        if (n.store)
            return !repeat && h.close_store();
        [[fallthrough]];
    case PadAction::Left:
        if (n.store)
            return apply_menu(n, h, PadAction::Accept, repeat);
        if (repeat || h.recent_count() <= 0)
            return false;
        n.pane = Pane::Details;
        n.tile_focus = 0;
        h.pane_moved(Pane::Details);
        return true;
    case PadAction::Accept:
        if (repeat)
            return false;
        if (n.focus < 0 || n.focus >= count)
            return false;
        if (h.menu_kind(n.focus) == MenuKind::System) {
            int games = h.enter_system(n.focus);
            if (games <= 0)
                return !n.store && h.menu_empty(n.focus);
            n.system_view = true;
            n.sys_focus = 0;
            n.sys_scroll = 0;
            n.sys_total = games;
            n.pane = Pane::Details;
            return true;
        }
        n.stack.push_back(Screen::Settings);
        h.push(Screen::Settings);
        return true;
    default:
        return false;
    }
}

int row_down(const Nav& n, int focus, int total)
{
    if (focus + n.cols < total)
        return focus + n.cols;
    if (n.partial_rows && (focus / n.cols + 1) * n.cols < total)
        return total - 1;
    return -1;
}

bool system_to(Nav& n, NavHost& h, int to)
{
    if (to < 0 || to >= n.sys_total)
        return false;
    n.sys_focus = to;
    bool up = n.sys_focus < n.sys_scroll * n.cols;
    bool down = n.sys_focus >= (n.sys_scroll + Nav::kRows) * n.cols;
    if (up)
        n.sys_scroll--;
    if (down)
        n.sys_scroll++;
    h.system_moved(up || down);
    return true;
}

bool apply_system(Nav& n, NavHost& h, PadAction a, bool repeat)
{
    int total = n.sys_total;
    switch (a) {
    case PadAction::Up:
        return system_to(n, h, n.sys_focus - n.cols);
    case PadAction::Down:
        return system_to(n, h, row_down(n, n.sys_focus, total));
    case PadAction::Right:
        return system_to(n, h, n.sys_focus + 1);
    case PadAction::Left:
        return system_to(n, h, n.sys_focus - 1);
    case PadAction::Back:
        if (repeat)
            return false;
        return leave_system(n, h);
    case PadAction::Accept:
        if (repeat || n.sys_focus < 0 || n.sys_focus >= total)
            return false;
        h.launch_system_game(n.sys_focus);
        return false;
    case PadAction::Context:
        return !repeat && !n.store && n.sys_focus >= 0 && n.sys_focus < total && h.game_context(n.sys_focus);
    default:
        return false;
    }
}

int slot_target(const Nav& n, int tiles, int dcol, int drow)
{
    int s = slot_of(n.tile_focus, n.cols, n.recent_map);
    if (s < 0)
        return -1;
    int col = s % n.cols + dcol, row = s / n.cols + drow;
    if (col < 0 || col >= n.cols || row < 0)
        return -1;
    return game_at(row * n.cols + col, n.cols, n.recent_map, tiles);
}

bool recent_to(Nav& n, NavHost& h, int to, int tiles)
{
    if (to < 0 || to >= tiles)
        return false;
    n.tile_focus = to;
    h.recent_moved();
    return true;
}

bool leave_recent(Nav& n, NavHost& h)
{
    n.pane = Pane::Selector;
    h.pane_moved(Pane::Selector);
    return true;
}

bool apply_recent(Nav& n, NavHost& h, PadAction a, bool repeat)
{
    int tiles = h.recent_count();
    bool slots = n.recent_map != SlotMap::RowMajor;
    switch (a) {
    case PadAction::Up:
        return recent_to(n, h, slots ? slot_target(n, tiles, 0, -1) : n.tile_focus - n.cols, tiles);
    case PadAction::Down:
        return recent_to(n, h, slots ? slot_target(n, tiles, 0, 1) : row_down(n, n.tile_focus, tiles), tiles);
    case PadAction::Right:
        return recent_to(n, h, slots ? slot_target(n, tiles, 1, 0) : n.tile_focus + 1, tiles);
    case PadAction::Left:
        return recent_to(n, h, slots ? slot_target(n, tiles, -1, 0) : n.tile_focus - 1, tiles);
    case PadAction::Back:
        if (repeat)
            return false;
        return leave_recent(n, h);
    case PadAction::Accept:
        if (!repeat && n.tile_focus >= 0 && n.tile_focus < tiles)
            h.launch_recent(n.tile_focus);
        return false;
    default:
        return false;
    }
}

}

bool nav_apply(Nav& n, NavHost& h, PadAction a, bool repeat)
{
    if (!n.stack.empty()) {
        Screen s = n.stack.back();
        if (!h.page_action(s, a, repeat))
            return true;
        n.stack.pop_back();
        h.pop(s);
        return true;
    }
    if (n.pane == Pane::Selector)
        return apply_menu(n, h, a, repeat);
    if (n.system_view)
        return apply_system(n, h, a, repeat);
    return apply_recent(n, h, a, repeat);
}

}
