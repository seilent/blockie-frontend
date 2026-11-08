#include <cstdio>

#include "../repeat.h"

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

int count_due(Repeat& r, int64_t now, PadAction want)
{
    int n = 0;
    PadAction a = PadAction::Up;
    while (r.due(now, a)) {
        if (a != want)
            return -1;
        n++;
        if (n > 100)
            break;
    }
    return n;
}

}

int main()
{
    Repeat def;
    def.press(PadAction::Down, kFromPad, 0);
    check(def.deadline() == 300, "the default first repeat comes 300 ms after the press");

    Repeat r(300, 80);
    PadAction a = PadAction::Up;
    check(!r.due(0, a) && r.deadline() == -1, "nothing held means nothing due and no deadline");

    r.press(PadAction::Down, kFromPad, 1000);
    check(!r.due(1299, a), "no repeat before 300 ms");
    check(r.due(1300, a) && a == PadAction::Down, "first repeat at 300 ms");
    check(!r.due(1379, a), "no second repeat before 380 ms");
    check(r.due(1380, a) && a == PadAction::Down, "second repeat at 380 ms");
    check(r.due(1460, a) && r.deadline() == 1540, "then every 80 ms");

    r.release(PadAction::Down, kFromPad, 1500);
    check(!r.due(2000, a) && r.deadline() == -1, "release stops the repeat");

    r.press(PadAction::Up, kFromPad, 0);
    r.press(PadAction::Left, kFromPad, 100);
    check(r.deadline() == 400, "a new direction restarts the delay");
    check(r.due(400, a) && a == PadAction::Left, "the newest held direction repeats");
    r.release(PadAction::Left, kFromPad, 450);
    check(r.deadline() == 750, "releasing the repeating one falls back to the still held one after the delay");
    check(r.due(750, a) && a == PadAction::Up, "the fallback repeats the still held direction");
    r.release(PadAction::Up, kFromPad, 800);
    check(!r.active(), "all released");

    r.press(PadAction::Right, kFromPad, 0);
    check(count_due(r, 1300, PadAction::Right) == 1, "a one second stall emits once, no burst");
    check(r.deadline() == 1380, "after a stall the next repeat is one interval later");
    check(count_due(r, 1301, PadAction::Right) == 0, "the next wake right after a stall emits nothing");

    r.rearm(1310);
    check(r.deadline() == 1610, "rearm restarts the delay for a held direction");

    r.press(PadAction::Accept, kFromPad, 1400);
    check(r.sources(PadAction::Accept) == 0 && r.due(1610, a) && a == PadAction::Right, "accept never repeats");

    r.release_all();
    check(!r.active() && r.deadline() == -1, "release all clears every held direction");

    r.release(PadAction::Down, kFromPad, 0);
    check(!r.active(), "releasing a direction that is not held is harmless");

    r.press(PadAction::Down, kFromPad, 0);
    r.press(PadAction::Down, kFromKeys, 10);
    r.release(PadAction::Down, kFromKeys, 20);
    check(r.active() && r.sources(PadAction::Down) == kFromPad, "a key release keeps a direction the pad still holds");
    r.release(PadAction::Down, kFromPad, 30);
    check(!r.active(), "the direction ends when its last source releases it");

    r.press(PadAction::Up, kFromKeys, 0);
    r.press(PadAction::Left, kFromPad, 10);
    r.release_source(kFromKeys, 20);
    check(r.active() && r.sources(PadAction::Up) == 0 && r.sources(PadAction::Left) == kFromPad,
          "releasing the keyboard source drops only keyboard held directions");
    r.release_source(kFromPad, 30);
    check(!r.active() && r.deadline() == -1, "releasing the pad source drops the rest");

    r.press(PadAction::Right, kFromKeys, 0);
    r.press(PadAction::Down, kFromPad, 50);
    r.release_source(kFromPad, 100);
    check(r.deadline() == 400 && r.due(400, a) && a == PadAction::Right,
          "dropping the repeating source falls back to the keyboard held direction after the delay");

    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
