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
        return true;
    case PadAction::Down:
        if (n.focus >= count - 1)
            return false;
        n.focus++;
        keep_visible(n);
        return true;
    case PadAction::Left:
    case PadAction::Back:
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
                return false;
            n.system_view = true;
            n.sys_focus = 0;
            n.sys_scroll = 0;
            n.sys_total = games;
            n.pane = Pane::Details;
            return true;
        }
        {
            Screen s = h.menu_kind(n.focus) == MenuKind::Bluetooth ? Screen::Bluetooth : Screen::Settings;
            n.stack.push_back(s);
            h.push(s);
            return true;
        }
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

bool apply_system(Nav& n, NavHost& h, PadAction a, bool repeat)
{
    int total = n.sys_total;
    int cols = n.cols;
    switch (a) {
    case PadAction::Up: {
        if (n.sys_focus - cols < 0)
            return false;
        n.sys_focus -= cols;
        bool scrolled = n.sys_focus < n.sys_scroll * cols;
        if (scrolled)
            n.sys_scroll--;
        h.system_moved(scrolled);
        return true;
    }
    case PadAction::Down: {
        int to = row_down(n, n.sys_focus, total);
        if (to < 0)
            return false;
        n.sys_focus = to;
        bool scrolled = n.sys_focus >= (n.sys_scroll + Nav::kRows) * cols;
        if (scrolled)
            n.sys_scroll++;
        h.system_moved(scrolled);
        return true;
    }
    case PadAction::Right:
        if (n.sys_focus % cols >= cols - 1 || n.sys_focus + 1 >= total)
            return false;
        n.sys_focus++;
        h.system_moved(false);
        return true;
    case PadAction::Left:
        if (n.sys_focus % cols > 0) {
            n.sys_focus--;
            h.system_moved(false);
            return true;
        }
        if (repeat)
            return false;
        return leave_system(n, h);
    case PadAction::Back:
        if (repeat)
            return false;
        return leave_system(n, h);
    case PadAction::Accept:
        if (repeat || n.sys_focus < 0 || n.sys_focus >= total)
            return false;
        h.launch_system_game(n.sys_focus);
        return false;
    default:
        return false;
    }
}

bool slot_move(Nav& n, int tiles, int dcol, int drow)
{
    int s = slot_of(n.tile_focus, n.cols, n.recent_map);
    if (s < 0)
        return false;
    int col = s % n.cols + dcol, row = s / n.cols + drow;
    if (col < 0 || col >= n.cols || row < 0)
        return false;
    int g = game_at(row * n.cols + col, n.cols, n.recent_map, tiles);
    if (g < 0)
        return false;
    n.tile_focus = g;
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
    int cols = n.cols;
    bool slots = n.recent_map != SlotMap::RowMajor;
    switch (a) {
    case PadAction::Up:
        if (slots)
            return slot_move(n, tiles, 0, -1);
        if (n.tile_focus - cols < 0)
            return false;
        n.tile_focus -= cols;
        return true;
    case PadAction::Down: {
        if (slots)
            return slot_move(n, tiles, 0, 1);
        int to = row_down(n, n.tile_focus, tiles);
        if (to < 0)
            return false;
        n.tile_focus = to;
        return true;
    }
    case PadAction::Right:
        if (slots)
            return slot_move(n, tiles, 1, 0);
        if (n.tile_focus % cols >= cols - 1 || n.tile_focus + 1 >= tiles)
            return false;
        n.tile_focus++;
        return true;
    case PadAction::Left:
        if (slots ? slot_of(n.tile_focus, cols, n.recent_map) % cols > 0 : n.tile_focus % cols > 0) {
            if (slots)
                return slot_move(n, tiles, -1, 0);
            n.tile_focus--;
            return true;
        }
        if (repeat)
            return false;
        return leave_recent(n, h);
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
