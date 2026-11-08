#include "theme.h"

#include "../ui/roles.h"

namespace fd {

namespace {

Theme make_dark()
{
    Theme t;
    t.light = false;
    t.bg = tok::color_bg;
    t.bg_elevated = tok::color_bgElevated;
    t.tile = tok::color_tile;
    t.tile_pressed = tok::color_tilePressed;
    t.border = tok::color_border;
    t.ink = kWhite;
    t.focus = kWhite;
    t.scrim_sheet = tok::color_bg;
    t.scrim_sheet_a256 = alpha256(tok::scrim_sheet_alpha);
    t.scrim_dialog = tok::color_bg;
    t.scrim_dialog_a256 = alpha256(tok::motion_dialogScrim);
    return t;
}

Theme make_light()
{
    Theme t;
    t.light = true;
    t.bg = tok::color_light_bg;
    t.bg_elevated = tok::color_light_bgElevated;
    t.tile = tok::color_light_tile;
    t.tile_pressed = tok::color_light_tilePressed;
    t.border = tok::color_light_border;
    t.ink = tok::color_light_ink;
    t.focus = tok::color_light_ink;
    t.scrim_sheet = tok::color_light_bg;
    t.scrim_sheet_a256 = alpha256(tok::scrim_sheet_alpha);
    t.scrim_dialog = tok::color_light_bg;
    t.scrim_dialog_a256 = alpha256(tok::motion_dialogScrim);
    return t;
}

const Theme kDark = make_dark();
const Theme kLight = make_light();
bool g_light = false;

}

const Theme& theme_dark() { return kDark; }
const Theme& theme_light() { return kLight; }
const Theme& theme() { return g_light ? kLight : kDark; }
void set_theme_light(bool light) { g_light = light; }

}
