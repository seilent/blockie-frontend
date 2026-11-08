#pragma once
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "../core/fx.h"
#include "../gfx/canvas.h"
#include "../gfx/font.h"
#include "../gfx/pixel.h"
#include "theme.h"
#include "tile_grid.h"

namespace fd {

enum class Pane { Selector, Details };

using Art = std::shared_ptr<const Image>;

struct SelectorItem {
    std::string name;
    int games = 0;
    std::string subtitle;
};

constexpr int kBarAtFocus = -1000000;

struct MenuPose {
    int dx = 0;
    int a256 = 256;
};

struct SelectorModel {
    std::vector<SelectorItem> systems;
    int focus = 0;
    int menu_top_px = 0;
    std::vector<MenuPose> poses;
    int grid_a256 = 256;
    int bar_a256 = 256;
    std::vector<Art> tiles;
    std::string section_label;
    bool header_dim = false;
    int tile_focus = -1;
    std::string game_system;
    std::string game_title;
    bool big = false;
    bool t9_edge = false;
    SlotMap slots = SlotMap::RowMajor;
    int grid_y_px = 0;
    bool grid_band = false;
    int bar_baseline = kBarAtFocus;
    std::vector<int> label_a256;
    int title_shift = 0;
    int title_a256 = 256;
    int turn_shift = 0;
    int turn_a256 = 256;
};

constexpr int kMenuVisibleRows = 6;
constexpr int kMenuPitchPx = 96;
constexpr int kMenuFirstBaseline = 96;

void draw_selector(Canvas& c, Fonts& fonts, const SelectorModel& model, Pane focus_pane, bool scatter = false,
                   bool detail = false, const Theme& th = theme(), bool expanded = false);
void draw_turned(Canvas& c, Fonts& fonts, const SelectorModel& model, bool detail, const Theme& th = theme());
void warm_turned(Fonts& fonts, const SelectorModel& model);
size_t turned_rasters();
Rect header_rect(Fonts& fonts, const SelectorModel& model);

inline Rect tile_slot_rect(const SelectorModel& model, int k, bool expanded)
{
    if (k < 0)
        return {};
    if (!expanded)
        return k < kGridTiles ? grid_tile_rect(k, kGridCols).grow(kTileFocusBorder) : Rect{};
    int cols = model.big ? kBigCols : kGridColsWide;
    int s = slot_of(k, cols, model.slots);
    if (s < 0)
        return {};
    Rect r = model.big ? big_tile_rect(s) : grid_tile_rect(s, cols);
    r.y0 += model.grid_y_px;
    r.y1 += model.grid_y_px;
    return r.grow(kTileFocusBorder);
}

}
