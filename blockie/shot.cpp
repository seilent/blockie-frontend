#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <dirent.h>

#include "../gfx/canvas.h"
#include "../gfx/font.h"
#include "../gfx/pixel.h"
#include "../gfx/png.h"
#include "image.h"
#include "screen_selector.h"

using namespace fd;

namespace {

bool load_tile(const std::string& path, Image& out)
{
    DecodedImage d;
    if (!load_png(path, d) || !d.ok())
        return false;
    out = Image::from_portrait(d.rgb.data(), d.w, d.h);
    return true;
}

std::vector<std::string> pngs_in(const std::string& dir)
{
    std::vector<std::string> names;
    DIR* dp = opendir(dir.c_str());
    if (!dp)
        return names;
    for (struct dirent* e = readdir(dp); e; e = readdir(dp)) {
        std::string n = e->d_name;
        if (n.size() > 4 && n.compare(n.size() - 4, 4, ".png") == 0)
            names.push_back(n);
    }
    closedir(dp);
    std::sort(names.begin(), names.end());
    for (std::string& n : names)
        n = dir + "/" + n;
    return names;
}

}

int main(int argc, char** argv)
{
    std::string out;
    std::string font_dir;
    std::string tiles_dir;
    Pane focus = Pane::Selector;
    ArtFit fit = ArtFit::Cover;
    if (const char* env = getenv("BLOCKIE_FONTS"))
        font_dir = env;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--focus" && i + 1 < argc)
            focus = std::string(argv[++i]) == "details" ? Pane::Details : Pane::Selector;
        else if (a == "--fonts" && i + 1 < argc)
            font_dir = argv[++i];
        else if (a == "--tiles" && i + 1 < argc)
            tiles_dir = argv[++i];
        else if (a == "--fit" && i + 1 < argc)
            fit = std::string(argv[++i]) == "cover" ? ArtFit::Cover : ArtFit::Contain;
        else if (out.empty())
            out = a;
        else {
            fprintf(stderr, "blockie-shot: unexpected argument %s\n", a.c_str());
            return 2;
        }
    }
    if (out.empty()) {
        fprintf(stderr,
                "usage: blockie-shot OUT.png [--focus selector|details] [--fonts DIR] [--tiles DIR] [--fit contain|cover]\n");
        return 2;
    }
    if (font_dir.empty())
        font_dir = "fonts";

    Fonts fonts;
    if (!fonts.open(font_dir, {})) {
        fprintf(stderr, "blockie-shot: fonts not found in %s\n", font_dir.c_str());
        return 1;
    }
    fonts.begin_frame(-1);

    SelectorModel model;
    model.systems = {
        {"Game Boy", 28}, {"Game Boy Color", 19}, {"NES", 63},
        {"SNES", 47},     {"Genesis", 35},        {"Game Boy Advance", 52},
    };
    model.focus = 0;

    if (!tiles_dir.empty()) {
        for (const std::string& p : pngs_in(tiles_dir)) {
            if (model.tiles.size() >= 8)
                break;
            Image img;
            if (load_tile(p, img))
                model.tiles.push_back(std::move(img));
            else
                fprintf(stderr, "blockie-shot: skip %s\n", p.c_str());
        }
    }

    Buffer buf;
    Canvas c(buf.surface());
    draw_selector(c, fonts, model, focus, fit);

    if (!write_png(out, buf.surface())) {
        fprintf(stderr, "blockie-shot: cannot write %s\n", out.c_str());
        return 1;
    }
    return 0;
}
