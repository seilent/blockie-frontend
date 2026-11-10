#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "nav.h"
#include "screen_selector.h"

namespace fd {

struct HomeGame {
    int64_t id = 0;
    std::string name;
    std::string system;
};

std::vector<HomeGame> played_first(const std::vector<HomeGame>& recent, const HomeGame& g, int cap);
void add_menu_items(SelectorModel& m, std::vector<MenuKind>& kinds);

class HomeState {
public:
    int cols = Nav::kCols;
    int rows = Nav::kRows;
    int pad_rows = 0;
    int recent_cap = kGridTiles;
    std::function<Art(const HomeGame&)> load;

    std::vector<HomeGame> recent;
    std::vector<HomeGame> sysgames;
    std::vector<Art> recent_hold;
    std::unordered_map<int64_t, Art> cache;
    std::string sys_name;
    bool system_view = false;
    int64_t epoch = 0;
    int first = 0;

    Art cover(const HomeGame& g);
    std::vector<HomeGame> deal_recent(SelectorModel& m, const std::vector<HomeGame>& list);
    std::vector<HomeGame> game_ran(SelectorModel& m, const std::vector<HomeGame>& list, Nav& nav);
    std::vector<HomeGame> enter(SelectorModel& m, const std::string& name, const std::vector<HomeGame>& games);
    void leave(SelectorModel& m);
    std::vector<HomeGame> window(SelectorModel& m, int scroll, int focus, bool rebuild);
    std::unordered_set<int64_t> keep_set(int scroll) const;
    bool keeps_job(int64_t id, int64_t job_epoch) const;
    Art recent_art(const SelectorModel& m, int k, int side) const;
    bool drop(SelectorModel& m, int64_t id, int side, int64_t drop_epoch, const Art& img);
};

}
