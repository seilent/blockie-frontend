#pragma once
#include <string>
#include <vector>

#include "../gfx/canvas.h"
#include "../gfx/font.h"
#include "../gfx/pixel.h"

namespace fd {

enum class Pane { Selector, Details };
enum class ArtFit { Contain, Cover };

struct SelectorItem {
    std::string name;
    int games;
};

struct SelectorModel {
    std::vector<SelectorItem> systems;
    int focus = 0;
    std::vector<Image> tiles;
};

void draw_selector(Canvas& c, Fonts& fonts, const SelectorModel& model, Pane focus_pane,
                   ArtFit fit = ArtFit::Cover);

}
