#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "../glide.h"
#include "../home.h"

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

Art solid(uint32_t v)
{
    auto img = std::make_shared<Image>();
    img->w = 2;
    img->h = 2;
    img->mem.assign(4, v);
    return img;
}

uint32_t px(const Art& img) { return img && img->w > 0 ? img->mem[0] : 0; }

std::vector<HomeGame> synth(int64_t base, int n, const char* system)
{
    std::vector<HomeGame> out;
    for (int i = 0; i < n; i++) {
        HomeGame g;
        g.id = base + i;
        g.name = "Synth " + std::to_string(base + i);
        g.system = system;
        out.push_back(g);
    }
    return out;
}

bool ids(const std::vector<HomeGame>& v, std::initializer_list<int64_t> want)
{
    if (v.size() != want.size())
        return false;
    size_t i = 0;
    for (int64_t id : want)
        if (v[i++].id != id)
            return false;
    return true;
}

}

int main()
{
    auto with_even_art = [](const HomeGame& g) -> Art { return g.id % 2 == 0 ? solid((uint32_t)g.id) : nullptr; };

    {
        HomeState h;
        h.cols = 3;
        h.rows = 4;
        h.load = with_even_art;
        SelectorModel m;
        std::vector<HomeGame> recent = synth(500, 5, "Synth Recent");
        std::vector<HomeGame> want = h.deal_recent(m, recent);
        check(m.tiles.size() == 5 && px(m.tiles[0]) == 500 && !m.tiles[1],
              "deal_recent fills the grid in played order with the covers that exist");
        check(ids(want, {501, 503}), "deal_recent asks for the covers it lacks");

        std::vector<HomeGame> games = synth(100, 40, "Synth");
        std::vector<HomeGame> w = h.enter(m, "Synth", games);
        check(h.system_view && h.epoch == 1 && h.recent_hold.size() == 5 && px(h.recent_hold[0]) == 500,
              "enter keeps the recent tiles aside");
        check(m.tiles.size() == 12 && px(m.tiles[0]) == 100 && !m.tiles[1] && px(m.tiles[2]) == 102,
              "enter shows the first 12 games of the system in 3 columns");
        check(m.section_label == "Synth" && m.game_system == "Synth" && m.game_title == "Synth 100" && m.tile_focus == 0,
              "enter sets the system and the focused title");
        bool odd = !w.empty();
        for (const HomeGame& g : w)
            if (g.id % 2 == 0 || g.id < 100 || g.id >= 115)
                odd = false;
        check(odd && w.size() == 7, "the window asks for art it lacks on screen and the next row only");

        std::unordered_set<int64_t> keep = h.keep_set(2);
        check(keep.size() == 18 && keep.count(103) && keep.count(120) && !keep.count(102) && !keep.count(121),
              "the keep set is rows scroll - 1 to scroll + 4");

        w = h.window(m, 2, 7, true);
        check(h.first == 6 && m.tiles.size() == 12 && px(m.tiles[0]) == 106 && m.tile_focus == 1,
              "a scrolled window starts at its first row");
        check(m.game_title == "Synth 107", "the focused title follows the focus");
        bool pruned = true;
        for (const auto& e : h.cache)
            if (!keep.count(e.first))
                pruned = false;
        check(pruned && h.cache.count(106) && !h.cache.count(100), "the cover cache keeps only the keep set");

        h.window(m, 2, 8, false);
        check(m.tiles.size() == 12 && m.tile_focus == 2 && m.game_title == "Synth 108", "a move without scroll keeps the tiles");

        h.pad_rows = 1;
        h.window(m, 2, 8, true);
        check(h.first == 3 && m.tiles.size() == 18 && px(m.tiles[1]) == 104 && m.tile_focus == 5,
              "with a pad row the tiles hold rows scroll - 1 to scroll + 4");
        h.pad_rows = 0;
        h.window(m, 2, 8, true);

        check(h.drop(m, 109, kTileSide, h.epoch, solid(0xA9)) && px(m.tiles[3]) == 0xA9 && px(h.cache[109]) == 0xA9,
              "a drop for a game on screen lands on its tile and in the cache");
        check(!h.drop(m, 131, kTileSide, h.epoch, solid(0xB1)) && h.cache.count(131),
              "a drop for the current system off screen is cached without a redraw");
        bool stale = h.drop(m, 131, kTileSide, h.epoch - 1, solid(0xB2));
        check(!stale && px(h.cache[131]) == 0xB1, "a drop from an older epoch off screen is ignored");
        check(!h.drop(m, 9999, kTileSide, 0, solid(0xC1)) && !h.cache.count(9999),
              "a drop for an unknown game is dropped");
        bool held = h.drop(m, 501, kTileSide, 0, solid(0x51));
        check(!held && px(h.recent_hold[1]) == 0x51 && !m.tiles[1] && px(m.tiles[3]) == 0xA9,
              "a recent drop in a system view lands in the held recent tiles, not on screen");
        check(px(h.recent_art(m, 1, kTileSide)) == 0x51 && !h.recent_art(m, 1, kBigTileSide) &&
                  !h.recent_art(m, -1, kTileSide) && !h.recent_art(m, (int)h.recent.size(), kTileSide),
              "recent art by game and side reads the held 154 tiles in a system view and nothing at other sides");
        bool big = h.drop(m, 502, kBigTileSide, 0, solid(0x52)) || h.drop(m, 109, kBigTileSide, h.epoch, solid(0x53));
        check(!big && px(h.recent_hold[2]) == 502 && px(m.tiles[3]) == 0xA9 && px(h.cache[109]) == 0xA9,
              "a drop at a side the grid does not use changes nothing");
        check(h.keeps_job(9999, 0) && h.keeps_job(502, 7) && !h.keeps_job(9999, 7),
              "jobs of epoch 0 and of recent games survive a system exit");

        Nav nav;
        nav.pane = Pane::Details;
        nav.system_view = true;
        nav.tile_focus = 3;
        HomeGame played = games[5];
        std::vector<HomeGame> list = played_first(h.recent, played, 12);
        check(list.size() == 6 && list[0].id == 105 && list[1].id == 500, "the played game goes first");
        h.game_ran(m, list, nav);
        bool held_px = h.recent_hold.size() == 6 && !h.recent_hold[0] && px(h.recent_hold[1]) == 500 &&
                       !h.recent_hold[2] && px(h.recent_hold[3]) == 502 && !h.recent_hold[4] &&
                       px(h.recent_hold[5]) == 504;
        check(ids(h.recent, {105, 500, 501, 502, 503, 504}) && held_px && m.tiles.size() == 12 &&
                  px(m.tiles[3]) == 0xA9 && h.first == 6,
              "a game run from a system view re-deals into the held recent tiles and leaves the system tiles");
        check(nav.tile_focus == 3, "a system view launch leaves the focus alone");

        h.leave(m);
        check(!h.system_view && m.tiles.size() == 6 && h.recent_hold.empty() && h.cache.empty() &&
                  m.section_label.empty() && m.game_title.empty() && m.game_system.empty(),
              "leave brings the recent tiles back and clears the system state");
        check(px(h.recent_art(m, 1, kTileSide)) == 500 && h.recent_art(m, 3, kTileSide) == m.tiles[3] &&
                  !h.recent_art(m, 0, kTileSide) && !h.recent_art(m, 1, kBigTileSide),
              "collapsed, recent art by game at 154 reads the recent tiles on screen");

        nav.system_view = false;
        nav.pane = Pane::Details;
        nav.tile_focus = 4;
        h.game_ran(m, played_first(h.recent, h.recent[3], 12), nav);
        check(nav.tile_focus == 0 && ids(h.recent, {502, 105, 500, 501, 503, 504}) && px(m.tiles[0]) == 502 &&
                  !m.tiles[1] && px(m.tiles[2]) == 500 && !m.tiles[3] && !m.tiles[4] && px(m.tiles[5]) == 504,
              "a recent launch puts the game first and the focus on it");
        list = played_first(h.recent, games[30], 3);
        check(list.size() == 3 && list[0].id == 130, "played_first caps the list");
    }

    {
        HomeState h;
        h.cols = 3;
        h.rows = 4;
        h.pad_rows = 2;
        h.load = [](const HomeGame& g) -> Art { return solid((uint32_t)g.id); };
        SelectorModel m;
        std::vector<HomeGame> games = synth(100, 40, "Synth");
        h.enter(m, "Synth", games);
        h.window(m, 5, 15, true);
        std::unordered_set<int64_t> keep = h.keep_set(5);
        check(h.first == 9 && m.tiles.size() == 24 && px(m.tiles[0]) == 109 && px(m.tiles[23]) == 132 &&
                  keep.size() == 24 && keep.count(109) && keep.count(132) && !keep.count(108) && !keep.count(133),
              "with two pad rows the tiles and the keep set hold rows scroll - 2 to scroll + 5, 24 tiles");

        const int total = (int)games.size();
        const int cols = h.cols;
        const int band_top = kTileFirstTop - kTileFocusBorder;
        const int band_bottom = kTileFirstTop + (kGridRows - 1) * kGridRowPitch + kTileSide + kTileFocusBorder;
        int focus = 0, scroll = 0, checked = 0, missed = -1;
        h.window(m, 0, 0, true);
        Glide grid;
        grid.set(0);
        auto hold = [&](int dir, int64_t t0, int64_t t1) {
            int64_t next = t0, gap = 300;
            for (int64_t t = t0; t <= t1 + 200; t++) {
                if (t <= t1 && t == next) {
                    int to = focus + dir * cols;
                    if (dir > 0 && to >= total && (focus / cols + 1) * cols < total)
                        to = total - 1;
                    if (to >= 0 && to < total) {
                        focus = to;
                        int s = scroll;
                        if (focus < s * cols)
                            s--;
                        if (focus >= (s + h.rows) * cols)
                            s++;
                        if (s != scroll) {
                            scroll = s;
                            h.window(m, scroll, focus, true);
                            grid.to(scroll * kGridRowPitch, t, fx_round(tok::motion_scroll), Curve::EaseIn);
                        }
                    }
                    next += gap;
                    gap = 80;
                }
                int gy = grid.at(t);
                std::unordered_set<int64_t> kept = h.keep_set(scroll);
                int lo = h.first / cols, hi = lo + ((int)m.tiles.size() + cols - 1) / cols;
                for (int r = 0; r * cols < total; r++) {
                    int y0 = kTileFirstTop + r * kGridRowPitch - gy;
                    if (y0 + kTileSide <= band_top || y0 >= band_bottom)
                        continue;
                    checked++;
                    bool in = r >= lo && r < hi;
                    for (int k = r * cols; k < std::min(total, (r + 1) * cols); k++)
                        in = in && kept.count(games[k].id);
                    if (!in && missed < 0)
                        missed = (int)t;
                }
            }
        };
        hold(1, 0, 3000);
        bool bottom = focus == total - 1 && scroll == (total - 1) / cols - h.rows + 1;
        hold(-1, 4000, 7000);
        if (missed >= 0)
            std::printf("  first row outside the held tiles at %d ms\n", missed);
        check(missed < 0 && checked > 0 && bottom && focus == 0 && scroll == 0,
              "Down then Up held over 40 games: every row in the band at the glide offset is held");
    }

    {
        HomeState h;
        h.recent_cap = 8;
        SelectorModel m;
        h.deal_recent(m, synth(700, 12, "Synth"));
        check(h.recent.size() == 8 && m.tiles.size() == 8, "the recent cap limits the deal");
        check(h.enter(m, "Synth", {}).empty() && !h.system_view, "entering an empty system changes nothing");
    }

    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
