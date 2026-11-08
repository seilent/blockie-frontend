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

bool pose_eq(ItemPose a, ItemPose b) { return a.a256 == b.a256 && a.dx == b.dx && a.dy == b.dy; }

bool at_rest(ItemPose p) { return p.a256 == 256 && p.dx == 0 && p.dy == 0; }

}

int main()
{
    check(turnstile_d_px() == 2208, "turnstile perspective is 2.3 x 960 = 2208 px");

    PageMove m;
    check(m.phase(0) == PagePhase::Idle && at_rest(m.item(0, 0)), "a fresh move is idle with items at rest");

    m.push(1000, 0, 8);
    int64_t t3 = 1000 + 250 + 28 * 7;
    check(m.phase(1000) == PagePhase::HomeExit, "push starts with the home exit");
    check(m.phase(t3 - 1) == PagePhase::HomeExit && m.phase(t3) == PagePhase::PageEnter,
          "push focus 0 of 8: home exit lasts 250 + 28 x 7 ms");
    check(m.end() == t3 + 350, "push focus 0 of 8: the page enter lasts 350 ms");
    check(m.page_angle(t3) == fx_int(-80), "T3 starts at -80 deg");
    check(m.page_angle(t3 + 349) > fx_int(-1) && m.page_angle(t3 + 349) <= 0, "T3 is near 0 deg at 349 ms");
    check(m.page_angle(m.end()) == 0 && m.phase(m.end()) == PagePhase::Idle, "T3 ends at 0 deg");
    check(m.page_a256(t3) == 0 && m.page_a256(t3 + 5) == 128 && m.page_a256(t3 + 10) == 256,
          "T3 fades in over 10 ms");
    check(m.page_a256(t3 - 1) == 0, "the page is hidden during the home exit");

    bool order = true;
    for (int i = 0; i < 8; i++) {
        int64_t go = 1000 + 28 * i;
        if (!at_rest(m.item(go, i)) || m.item(go + 10, i).dy >= 0 || m.item(go + 10, i).a256 >= 256)
            order = false;
    }
    check(order, "T2 focus 0: item i leaves 28 x i ms after the start");
    check(m.item(1250, 0).dy == -180 && m.item(1250, 0).a256 == 0 && m.item(1250, 0).dx == 0,
          "T2: the focused item ends 180 px up at alpha 0");
    check(m.item(1100, 0).dy < 0 && m.item(1100, 0).dy > -180, "T2: an item midway is between 0 and -180 px");
    check(m.grid_a256(1000) == 256 && m.grid_a256(1250) == 0, "T2: the grid fades out over 250 ms");
    check(m.bar_a256(1000) == 256 && m.bar_a256(1250) == 0, "T2: the focus bar fades with the focused item");
    check(m.item(t3 + 100, 3).a256 == 0 && m.grid_a256(t3 + 100) == 0, "the home is hidden while the page enters");
    check(m.header_dy(t3 + 100) == 0 && m.header_a256(t3 + 100) == 256, "a push never flies the header");

    PageMove mid;
    mid.push(0, 3, 8);
    check(mid.phase(361) == PagePhase::HomeExit && mid.phase(362) == PagePhase::PageEnter,
          "push focus 3 of 8: home exit lasts 250 + 28 x 4 ms");
    check(mid.item(20, 3).dy < 0 && at_rest(mid.item(20, 2)) && at_rest(mid.item(20, 4)),
          "T2 focus 3: the focused item leaves first");
    check(pose_eq(mid.item(100, 2), mid.item(100, 4)) && pose_eq(mid.item(150, 1), mid.item(150, 5)),
          "T2 focus 3: items the same distance away move together");
    check(at_rest(mid.item(112, 7)) && mid.item(120, 7).dy < 0, "T2 focus 3: item 7 waits 4 x 28 ms");
    check(mid.item(100, 2).dy < mid.item(100, 1).dy, "T2 focus 3: nearer items lead");

    PageMove back;
    back.pop(5000, 8);
    check(back.phase(5000) == PagePhase::PageExit && back.phase(5249) == PagePhase::PageExit,
          "pop starts with the page exit, 250 ms");
    check(back.page_angle(5000) == 0, "T4 starts at 0 deg");
    check(back.page_angle(5249) < fx_int(-75) && back.page_angle(5249) > fx_int(-80), "T4 nears -80 deg at 249 ms");
    check(back.page_a256(5239) == 256, "T4 alpha is 1 at 239 ms");
    check(back.page_a256(5241) == 0, "T4 alpha is 0 at 241 ms");
    check(back.header_dy(5000) == 0 && back.header_a256(5000) == 256, "T4 header starts in place");
    check(back.header_dy(5250) == -180, "T4 header fly at 250 ms is -180 px");
    check(back.header_a256(5250) == 0, "T4 header alpha at 250 ms is 0");
    check(back.header_dy(5100) < 0 && back.header_dy(5100) > -180, "T4 header midway is between 0 and -180 px");
    check(back.item(5100, 0).a256 == 0 && back.grid_a256(5100) == 0, "the home is hidden while the page leaves");

    check(back.phase(5250) == PagePhase::HomeEnter, "T1 follows T4");
    ItemPose first = back.item(5250, 0);
    check(first.a256 == 0 && first.dx == 48 && first.dy == 0, "T1 item 0 starts at alpha 0 and dx 48 px");
    check(at_rest(back.item(5250 + 220, 0)), "T1 item 0 is at rest after 220 ms");
    check(pose_eq(back.item(5300, 0), back.item(5300, 7)), "T1 items enter together, as replayEntrance");
    check(back.grid_a256(5250) == 0 && back.grid_a256(5250 + 210) > 0 && back.grid_a256(5250 + 210) < 256,
          "T1 grid fades in over 420 ms");
    check(back.end() == 5250 + 420 && back.phase(5669) == PagePhase::HomeEnter &&
              back.phase(5670) == PagePhase::Idle,
          "T1 ends with the grid fade at 420 ms");
    check(back.bar_a256(5250) == 0 && back.bar_a256(5470) == 256, "T1 focus bar enters with item 0");

    back.land();
    check(back.phase(5300) == PagePhase::Idle && at_rest(back.item(5300, 3)) && back.grid_a256(5300) == 256 &&
              back.page_angle(5300) == 0 && back.page_a256(5300) == 256 && back.header_dy(5300) == 0,
          "land cuts to the end state");

    PageMove entrance;
    entrance.enter(0, 8, 20);
    check(entrance.phase(0) == PagePhase::HomeEnter, "enter starts with the home enter");
    ItemPose item1 = entrance.item(20, 1);
    check(item1.a256 == 0 && item1.dx == 48 && entrance.item(30, 1).a256 > 0, "enter with stagger 20: item 1 waits 20 ms");
    check(entrance.item(100, 0).a256 > entrance.item(100, 1).a256 && entrance.item(100, 1).a256 > entrance.item(100, 2).a256,
          "enter with stagger 20: items enter in index order");
    check(entrance.end() == 420 && at_rest(entrance.item(360, 7)), "enter with stagger 20: item 7 rests at 360 ms, end 420");
    check(entrance.header_dy(100) == 0 && entrance.header_a256(100) == 256, "enter alone never flies a header");

    Buffer page, dst;
    Surface ps = page.surface(), ds = dst.surface();
    for (int y = 0; y < ps.h; y++)
        for (int x = 0; x < ps.w; x++)
            ps.at(x, y) = (uint32_t)((x * 5 + y) & 0xFF) << 16 | (uint32_t)(y & 0xFF) << 8 | (uint32_t)(x & 0xFF);
    draw_turnstile(ps, ds, 0, 256, 0x111111);
    bool ident = true;
    for (int y = 0; y < ps.h && ident; y++)
        for (int x = 0; x < ps.w; x++)
            if (ds.at(x, y) != ps.at(x, y)) {
                ident = false;
                break;
            }
    check(ident, "turnstile at 0 deg copies the page");
    draw_turnstile(ps, ds, fx_int(-80), 256, 0x111111);
    check(ds.at(0, 0) == ps.at(0, 0) && ds.at(0, 400) == ps.at(0, 400), "turnstile hinges at x 0");
    check(ds.at(959, 0) == 0x111111 && ds.at(959, 719) == 0x111111, "turnstile at -80 deg leaves the far side bg");
    draw_turnstile(ps, ds, fx_int(-40), 0, 0x111111);
    check(ds.at(0, 0) == 0x111111 && ds.at(480, 360) == 0x111111, "turnstile at alpha 0 is all bg");

    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
