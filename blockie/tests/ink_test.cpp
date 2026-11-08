#include <cstdio>
#include <cstdlib>

#include "../../ui/roles.h"
#include "../ink.h"
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

int channel_diff(uint32_t a, uint32_t b)
{
    int m = 0;
    for (int sh = 0; sh <= 16; sh += 8) {
        int d = std::abs((int)((a >> sh) & 0xFF) - (int)((b >> sh) & 0xFF));
        if (d > m)
            m = d;
    }
    return m;
}

int max_diff(const Theme& th)
{
    Buffer direct;
    Buffer layered;
    Canvas dc(direct.surface());
    Canvas lc0(layered.surface());
    Rect all = direct.surface().rect();
    dc.fill(all, th.bg);
    lc0.fill(all, th.bg);
    InkLayer ink;
    Rect area{0, 0, 600, 40};
    Canvas& lc = ink.begin(area);
    const int alphas[] = {0, 20, 64, 128, 154, 200, 218, 256};
    for (int i = 0; i < 8; i++) {
        Rect r{i * 60, 0, i * 60 + 50, 20};
        dc.blend(r, th.ink, alphas[i]);
        lc.blend(r, kWhite, alphas[i]);
        Rect r2{i * 60, 20, i * 60 + 50, 40};
        dc.blend(r2, th.ink, alphas[i]);
        dc.blend(r2, th.ink, alphas[i] / 2);
        lc.blend(r2, kWhite, alphas[i]);
        lc.blend(r2, kWhite, alphas[i] / 2);
    }
    ink.compose(layered.surface(), th.ink);
    int m = 0;
    Surface ds = direct.surface();
    Surface ls = layered.surface();
    for (int y = 0; y < 40; y++)
        for (int x = 0; x < 600; x++) {
            int d = channel_diff(LiveF::unpack(ds.at(x, y)), LiveF::unpack(ls.at(x, y)));
            if (d > m)
                m = d;
        }
    return m;
}

}

int main()
{
    int dark = max_diff(theme_dark());
    int lite = max_diff(theme_light());
    std::printf("ink layer max channel difference: dark %d, light %d\n", dark, lite);
    check(dark <= 2, "dark: the ink layer matches a direct ink draw within 2 LSB");
    check(lite <= 2, "light: the ink layer matches a direct ink draw within 2 LSB");

    check(theme_light().bg == 0xFFFFFF && theme_light().ink == 0x111111, "light theme is white paper with near black ink");
    check(theme_dark().bg == 0x111111 && theme_dark().ink == 0xFFFFFF, "dark theme is the dark ground with white ink");
    check(theme_light().tile == 0xF0F0F0 && theme_light().bg_elevated == 0xF8F8F8, "light surfaces mirror the dark deltas");

    set_theme_light(true);
    check(theme().light, "the current theme follows set_theme_light");
    set_theme_light(false);
    check(!theme().light, "and switches back");

    Buffer b;
    Canvas c(b.surface());
    c.fill(b.surface().rect(), 0x123456);
    InkLayer ink;
    ink.begin({0, 0, 10, 10});
    ink.compose(b.surface(), 0xFFFFFF);
    check(LiveF::unpack(b.surface().at(5, 5)) == 0x123456, "an empty layer leaves the target untouched");

    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
