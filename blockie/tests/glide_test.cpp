#include <cstdio>

#include "../glide.h"

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

}

int main()
{
    Glide g;
    g.set(5);
    check(g.at(0) == 5 && g.at(1000000) == 5 && !g.running(0) && g.target() == 5, "a set glide rests at its value");

    g.to(105, 1000, 100, Curve::Linear);
    check(g.at(1000) == 5 && g.at(1050) == 55 && g.at(1100) == 105 && g.at(2000) == 105,
          "a linear glide runs from its value to the target over the duration");
    check(g.running(1000) && g.running(1099) && !g.running(1100) && !g.running(999), "it runs for exactly its duration");

    g.to(205, 1050, 100, Curve::Linear);
    check(g.at(1050) == 55 && g.at(1100) == 130 && g.at(1150) == 205,
          "a retarget starts from the current value, not from the old start");

    g.to(205, 1080, 100, Curve::Linear);
    check(g.at(1100) == 130 && g.running(1149), "the same target does not restart the glide");

    g.land();
    check(g.at(1060) == 205 && !g.running(1060), "land jumps to the target");

    Glide e;
    e.set(0);
    e.to(1000, 0, 100, Curve::EaseIn);
    int mid = e.at(50);
    check(mid > 0 && mid < 500 && e.at(100) == 1000, "an ease in glide is slow at the start and ends on the target");

    Glide s;
    s.set(0);
    s.to(-960, 0, 100, Curve::Sine);
    int smid = s.at(50);
    check(smid < -480 && smid > -960 && s.at(100) == -960, "a sine glide moves down as well as up");

    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
