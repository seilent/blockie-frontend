#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "../gfx/font.h"
#include "../gfx/pixel.h"
#include "../gfx/png.h"
#include "screen_selector.h"

using namespace fd;

int main(int argc, char** argv)
{
    std::string out;
    std::string font_dir;
    Pane focus = Pane::Selector;
    if (const char* env = getenv("BLOCKIE_FONTS"))
        font_dir = env;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--focus" && i + 1 < argc)
            focus = std::string(argv[++i]) == "details" ? Pane::Details : Pane::Selector;
        else if (a == "--fonts" && i + 1 < argc)
            font_dir = argv[++i];
        else if (out.empty())
            out = a;
        else {
            fprintf(stderr, "blockie-shot: unexpected argument %s\n", a.c_str());
            return 2;
        }
    }
    if (out.empty()) {
        fprintf(stderr, "usage: blockie-shot OUT.png [--focus selector|details] [--fonts DIR]\n");
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

    Buffer buf;
    Canvas c(buf.surface());
    draw_selector(c, fonts, focus);

    if (!write_png(out, buf.surface())) {
        fprintf(stderr, "blockie-shot: cannot write %s\n", out.c_str());
        return 1;
    }
    return 0;
}
