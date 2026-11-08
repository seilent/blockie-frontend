#pragma once
#include <cstdint>

namespace fd {

struct Theme {
    bool light = false;
    uint32_t bg = 0;
    uint32_t bg_elevated = 0;
    uint32_t tile = 0;
    uint32_t tile_pressed = 0;
    uint32_t border = 0;
    uint32_t ink = 0;
    uint32_t focus = 0;
    uint32_t scrim_sheet = 0;
    int scrim_sheet_a256 = 0;
    uint32_t scrim_dialog = 0;
    int scrim_dialog_a256 = 0;
};

const Theme& theme_dark();
const Theme& theme_light();
const Theme& theme();
void set_theme_light(bool light);

}
