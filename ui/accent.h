#pragma once
#include <cstdint>

#include "../gfx/canvas.h"
#include "../gfx/font.h"

namespace fd {

int hue_on_grid(int hue);
uint32_t accent_for(int hue);
uint32_t accent();
int accent_hue();
void set_accent_hue(int hue);
bool accent_off();
void set_accent_off(bool off);
void page_ground(Canvas& c, Rect r);
bool accent_bold(TextStyle& s);

}
