#pragma once
#include "../gfx/font.h"
#include "../gfx/pixel.h"

namespace fd {

enum class Pane { Selector, Details };

void draw_selector(Canvas& c, Fonts& fonts, Pane focus);

}
