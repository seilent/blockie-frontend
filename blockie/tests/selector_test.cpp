#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "../screen_selector.h"
#include "../theme.h"

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
    img->w = kTileSide;
    img->h = kTileSide;
    img->mem.assign((size_t)kTileSide * kTileSide, v);
    return img;
}

bool same(Buffer& a, Buffer& b, Rect r)
{
    Surface sa = a.surface(), sb = b.surface();
    for (int y = r.y0; y < r.y1; y++)
        for (int x = r.x0; x < r.x1; x++)
            if (sa.at(x, y) != sb.at(x, y))
                return false;
    return true;
}

void draw(Buffer& b, Fonts& fonts, const SelectorModel& m, Pane focus, const Theme& th)
{
    Canvas c(b.surface());
    draw_selector(c, fonts, m, focus, false, false, th, false);
}

}

int main(int argc, char** argv)
{
    std::string dir = argc > 1 ? argv[1] : "fonts";
    Fonts fonts;
    if (!fonts.open(dir, {})) {
        std::printf("FAIL fonts not found in %s\n", dir.c_str());
        return 1;
    }
    fonts.begin_frame(-1);

    SelectorModel m;
    m.systems = {{"gba", 52, ""}, {"snes", 47, ""}};
    m.tiles = {solid(0x804020), solid(0x208040)};
    m.tile_focus = 0;
    SelectorModel held = m;
    held.header_dim = true;
    Rect head = header_rect(fonts, m);
    Rect tile = grid_tile_rect(1, kGridCols);
    for (const Theme* th : {&theme_dark(), &theme_light()}) {
        Buffer sel, det, hold;
        draw(sel, fonts, m, Pane::Selector, *th);
        draw(det, fonts, m, Pane::Details, *th);
        draw(hold, fonts, held, Pane::Details, *th);
        check(same(hold, sel, head) && !same(det, sel, head),
              th->light ? "light: a held header keeps its menu alpha under Details focus, an unheld one brightens"
                        : "dark: a held header keeps its menu alpha under Details focus, an unheld one brightens");
        check(same(hold, det, tile) && !same(hold, sel, tile),
              th->light ? "light: the tiles still take the Details alpha"
                        : "dark: the tiles still take the Details alpha");
    }

    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
