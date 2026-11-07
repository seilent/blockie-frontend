#pragma once
#include <string>
#include <vector>

#include "../gfx/font.h"
#include "../gfx/pixel.h"

namespace fd {

enum class Pane { Selector, Details };

struct SelectorItem {
    std::string name;
    int games;
};

struct SelectorModel {
    std::vector<SelectorItem> systems;
    int focus;
};

void draw_selector(Canvas& c, Fonts& fonts, const SelectorModel& model, Pane focus_pane);

}
