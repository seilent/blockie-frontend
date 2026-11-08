#include <cstdio>

#include "../pagemove.h"

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

bool at_rest(ItemPose p) { return p.a256 == 256 && p.dx == 0; }

}

int main()
{
    PageMove m;
    check(!m.running(0) && at_rest(m.item(0, 0)), "a fresh move is idle with items at rest");

    PageMove entrance;
    entrance.enter(0, 8, 20);
    check(entrance.running(0), "enter starts running at once");
    ItemPose item1 = entrance.item(20, 1);
    check(item1.a256 == 0 && item1.dx == 48 && entrance.item(30, 1).a256 > 0, "enter with stagger 20: item 1 waits 20 ms");
    check(entrance.item(100, 0).a256 > entrance.item(100, 1).a256 && entrance.item(100, 1).a256 > entrance.item(100, 2).a256,
          "enter with stagger 20: items enter in index order");
    check(entrance.end() == 420 && at_rest(entrance.item(360, 7)), "enter with stagger 20: item 7 rests at 360 ms, end 420");

    ItemPose first = entrance.item(0, 0);
    check(first.a256 == 0 && first.dx == 48, "T1 item 0 starts at alpha 0 and dx 48 px");
    check(at_rest(entrance.item(220, 0)), "T1 item 0 is at rest after 220 ms");
    check(entrance.grid_a256(0) == 0 && entrance.grid_a256(210) > 0 && entrance.grid_a256(210) < 256,
          "T1 grid fades in over 420 ms");
    check(entrance.bar_a256(0) == 0 && entrance.bar_a256(220) == 256, "T1 focus bar enters with item 0");
    check(entrance.running(419) && !entrance.running(420), "T1 ends at 420 ms");

    entrance.land();
    check(!entrance.running(100) && at_rest(entrance.item(100, 3)) && entrance.grid_a256(100) == 256,
          "land cuts to the end state");

    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
