#pragma once
#include <string>
#include <vector>

#include "../core/fx.h"
#include "../gfx/canvas.h"
#include "../gfx/font.h"
#include "../gfx/pixel.h"
#include "theme.h"

namespace fd {

enum class Pane { Selector, Details };
enum class ArtFit { Contain, Cover };

struct SelectorItem {
    std::string name;
    int games = 0;
    std::string subtitle;
};

struct MenuPose {
    int dx = 0;
    int dy = 0;
    int a256 = 256;
};

struct SelectorModel {
    std::vector<SelectorItem> systems;
    int focus = 0;
    int menu_top_px = 0;
    std::vector<MenuPose> poses;
    int grid_a256 = 256;
    int bar_a256 = 256;
    std::vector<Image> tiles;
    std::string section_label;
    int tile_focus = -1;
    std::string game_system;
    std::string game_title;
};

constexpr int kMenuVisibleRows = 6;
constexpr int kMenuPitchPx = 96;

int detail_title_bottom(Fonts& fonts, const SelectorModel& model);

void draw_selector(Canvas& c, Fonts& fonts, const SelectorModel& model, Pane focus_pane,
                   ArtFit fit = ArtFit::Cover, bool scatter = false, bool detail = false,
                   const Theme& th = theme());

}
