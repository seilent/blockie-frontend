#include "home.h"

#include <algorithm>

namespace fd {

std::vector<HomeGame> played_first(const std::vector<HomeGame>& recent, const HomeGame& g, int cap)
{
    std::vector<HomeGame> out;
    out.push_back(g);
    for (const HomeGame& r : recent)
        if (r.id != g.id && (int)out.size() < cap)
            out.push_back(r);
    if ((int)out.size() > cap)
        out.resize((size_t)std::max(0, cap));
    return out;
}

Art HomeState::cover(const HomeGame& g)
{
    auto it = cache.find(g.id);
    if (it != cache.end())
        return it->second;
    Art img = load ? load(g) : nullptr;
    if (img && img->w > 0)
        cache[g.id] = img;
    return img;
}

std::vector<HomeGame> HomeState::deal_recent(SelectorModel& m, const std::vector<HomeGame>& list)
{
    size_t n = std::min(list.size(), (size_t)std::max(0, recent_cap));
    recent.assign(list.begin(), list.begin() + (long)n);
    std::vector<Art>& grid = system_view ? recent_hold : m.tiles;
    grid.clear();
    std::vector<HomeGame> want;
    for (const HomeGame& g : recent) {
        Art img = cover(g);
        if (!img || img->w <= 0)
            want.push_back(g);
        grid.push_back(std::move(img));
    }
    return want;
}

std::vector<HomeGame> HomeState::game_ran(SelectorModel& m, const std::vector<HomeGame>& list, Nav& nav)
{
    std::vector<HomeGame> want = deal_recent(m, list);
    if (!system_view && nav.pane == Pane::Details)
        nav.tile_focus = 0;
    return want;
}

std::vector<HomeGame> HomeState::enter(SelectorModel& m, const std::string& name, const std::vector<HomeGame>& games)
{
    if (games.empty())
        return {};
    sysgames = games;
    sys_name = name;
    system_view = true;
    epoch++;
    m.tiles.swap(recent_hold);
    return window(m, 0, 0, true);
}

void HomeState::leave(SelectorModel& m)
{
    system_view = false;
    m.tiles.swap(recent_hold);
    recent_hold.clear();
    cache.clear();
    m.section_label.clear();
    m.game_system.clear();
    m.game_title.clear();
}

std::unordered_set<int64_t> HomeState::keep_set(int scroll) const
{
    int n = (int)sysgames.size();
    int lo = std::max(0, (scroll - 1) * cols);
    int hi = std::min(n, (scroll + rows + 1) * cols);
    std::unordered_set<int64_t> keep;
    for (int i = lo; i < hi; i++)
        keep.insert(sysgames[i].id);
    return keep;
}

bool HomeState::keeps_job(int64_t id, int64_t job_epoch) const
{
    if (job_epoch == 0)
        return true;
    for (const HomeGame& g : recent)
        if (g.id == id)
            return true;
    return false;
}

std::vector<HomeGame> HomeState::window(SelectorModel& m, int scroll, int focus, bool rebuild)
{
    std::vector<HomeGame> want;
    int n = (int)sysgames.size();
    int start = scroll * cols;
    if (rebuild) {
        first = std::max(0, scroll - pad_rows) * cols;
        int end = std::min(n, (scroll + rows + pad_rows) * cols);
        m.tiles.clear();
        for (int i = first; i < end; i++)
            m.tiles.push_back(cover(sysgames[i]));
        m.section_label = sys_name;
        std::unordered_set<int64_t> keep = keep_set(scroll);
        for (auto it = cache.begin(); it != cache.end();) {
            if (keep.count(it->first))
                ++it;
            else
                it = cache.erase(it);
        }
        int shown = std::min(n, start + rows * cols);
        int hi = std::min(n, (scroll + rows + 1) * cols);
        for (int i = start; i < hi; i++) {
            const HomeGame& g = sysgames[i];
            if (cache.count(g.id))
                continue;
            if (i >= shown) {
                Art art = cover(g);
                if (art && art->w > 0)
                    continue;
            }
            want.push_back(g);
        }
    }
    m.game_system = sys_name;
    m.game_title = focus >= 0 && focus < n ? sysgames[focus].name : std::string();
    m.tile_focus = focus - first;
    return want;
}

bool HomeState::drop(SelectorModel& m, int64_t id, int64_t drop_epoch, const Art& img)
{
    std::vector<Art>& grid = system_view ? recent_hold : m.tiles;
    int rpos = -1;
    for (int k = 0; k < (int)recent.size() && k < (int)grid.size(); k++)
        if (recent[k].id == id) {
            rpos = k;
            break;
        }
    int spos = -1;
    if (system_view) {
        int end = std::min(first + (int)m.tiles.size(), (int)sysgames.size());
        for (int i = first; i < end; i++)
            if (sysgames[i].id == id) {
                spos = i - first;
                break;
            }
    }
    bool keep = system_view && (spos >= 0 || drop_epoch == epoch);
    if (rpos < 0 && !keep)
        return false;
    bool redraw = false;
    if (rpos >= 0) {
        grid[rpos] = img;
        if (!system_view)
            redraw = true;
    }
    if (keep)
        cache[id] = img;
    if (spos >= 0 && spos < (int)m.tiles.size()) {
        m.tiles[spos] = img;
        redraw = true;
    }
    return redraw;
}

}
