#include <cstdio>
#include <vector>

#include <fcntl.h>
#include <linux/input.h>
#include <unistd.h>

#include "../input_pad.h"

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

void push(int fd, uint16_t type, uint16_t code, int32_t value)
{
    input_event ev = {};
    ev.type = type;
    ev.code = code;
    ev.value = value;
    if (write(fd, &ev, sizeof ev) != (ssize_t)sizeof ev)
        std::printf("FAIL could not write event\n");
}

bool eq(const std::vector<PadAction>& a, const std::vector<PadAction>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); i++)
        if (a[i] != b[i])
            return false;
    return true;
}

bool eq_edges(const std::vector<PadEdge>& a, const std::vector<PadEdge>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); i++)
        if (a[i].action != b[i].action || a[i].down != b[i].down)
            return false;
    return true;
}

}

int main()
{
    check(pad_matches({0x0019, 0x4001, 0x0428, "ayaneo-gamepad/input0", "AYANEO Controller"}),
          "matches the ayaneo id");
    check(pad_matches({0, 0x4001, 0x0428, "", ""}), "matches vendor+product alone");
    check(pad_matches({0, 0, 0, "", "AYANEO Controller"}), "matches by name alone");
    check(pad_matches({0, 0, 0, "ayaneo-gamepad/input0", ""}), "matches by phys prefix alone");
    check(!pad_matches({0, 0x4001, 0x0001, "", ""}), "rejects same vendor wrong product");
    check(!pad_matches({0x0003, 0x045e, 0x028e, "usb-0000:00:14.0-1/input0", "Xbox 360 Controller"}),
          "rejects an unrelated gamepad");

    int p[2];
    if (pipe(p) != 0) {
        std::printf("FAIL pipe\n");
        return 1;
    }
    fcntl(p[0], F_SETFL, O_NONBLOCK);

    PadReader pad;
    pad.adopt_fd(p[0]);
    check(pad.is_open(), "reader is open after adopt_fd");
    check(eq(pad.poll(), {}), "no pending events yields no actions");

    push(p[1], EV_ABS, ABS_HAT0Y, 1);
    push(p[1], EV_SYN, SYN_REPORT, 0);
    push(p[1], EV_ABS, ABS_HAT0Y, 0);
    push(p[1], EV_SYN, SYN_REPORT, 0);
    push(p[1], EV_ABS, ABS_HAT0Y, -1);
    push(p[1], EV_SYN, SYN_REPORT, 0);
    check(eq(pad.poll(), {PadAction::Down, PadAction::Up}), "hat0y down then release then up");

    push(p[1], EV_ABS, ABS_HAT0X, 1);
    push(p[1], EV_ABS, ABS_HAT0X, 0);
    push(p[1], EV_ABS, ABS_HAT0X, -1);
    check(eq(pad.poll(), {PadAction::Right, PadAction::Left}), "hat0x right then release then left");

    push(p[1], EV_KEY, BTN_EAST, 1);
    push(p[1], EV_KEY, BTN_EAST, 0);
    push(p[1], EV_KEY, BTN_SOUTH, 1);
    check(eq(pad.poll(), {PadAction::Accept, PadAction::Back}),
          "east accept, release ignored, south back");

    push(p[1], EV_KEY, BTN_DPAD_UP, 1);
    push(p[1], EV_KEY, BTN_DPAD_DOWN, 1);
    push(p[1], EV_KEY, BTN_DPAD_LEFT, 1);
    push(p[1], EV_KEY, BTN_DPAD_RIGHT, 1);
    check(eq(pad.poll(), {PadAction::Up, PadAction::Down, PadAction::Left, PadAction::Right}),
          "dpad keys map to directions");

    push(p[1], EV_KEY, BTN_SOUTH, 2);
    check(eq(pad.poll(), {}), "autorepeat value 2 is ignored");

    push(p[1], EV_KEY, BTN_MODE, 1);
    push(p[1], EV_MSC, MSC_SCAN, 42);
    check(eq(pad.poll(), {}), "unmapped button and misc events yield nothing");

    push(p[1], EV_KEY, BTN_DPAD_UP, 0);
    push(p[1], EV_KEY, BTN_DPAD_DOWN, 0);
    push(p[1], EV_KEY, BTN_DPAD_LEFT, 0);
    push(p[1], EV_KEY, BTN_DPAD_RIGHT, 0);
    push(p[1], EV_ABS, ABS_HAT0X, 0);
    push(p[1], EV_ABS, ABS_HAT0Y, 0);
    check(eq(pad.poll(), {}), "releasing everything yields no presses");

    push(p[1], EV_KEY, BTN_DPAD_UP, 1);
    push(p[1], EV_KEY, BTN_DPAD_UP, 0);
    check(eq_edges(pad.poll_edges(), {{PadAction::Up, true}, {PadAction::Up, false}}),
          "dpad press and release are both edges");

    push(p[1], EV_ABS, ABS_HAT0X, -1);
    push(p[1], EV_ABS, ABS_HAT0X, 1);
    push(p[1], EV_ABS, ABS_HAT0X, 0);
    check(eq_edges(pad.poll_edges(), {{PadAction::Left, true},
                                      {PadAction::Left, false},
                                      {PadAction::Right, true},
                                      {PadAction::Right, false}}),
          "hat flip releases the old direction before pressing the new one");

    push(p[1], EV_KEY, BTN_WEST, 1);
    push(p[1], EV_KEY, BTN_WEST, 0);
    check(eq_edges(pad.poll_edges(), {{PadAction::Context, true}}), "west is context, its release is not an edge");

    push(p[1], EV_KEY, BTN_EAST, 0);
    push(p[1], EV_KEY, BTN_SOUTH, 0);
    check(eq_edges(pad.poll_edges(), {}), "accept and back releases are not edges");

    check(pad.held(PadAction::Up) == Held::Unknown, "held state is unknown without evdev ioctls");

    push(p[1], EV_KEY, BTN_DPAD_LEFT, 1);
    push(p[1], EV_SYN, SYN_DROPPED, 0);
    push(p[1], EV_KEY, BTN_DPAD_LEFT, 0);
    push(p[1], EV_KEY, BTN_DPAD_RIGHT, 1);
    push(p[1], EV_SYN, SYN_REPORT, 0);
    check(eq_edges(pad.poll_edges(), {{PadAction::Left, true}, {PadAction::Left, false}}),
          "syn dropped skips to the report and releases what is held when the state is unknown");

    push(p[1], EV_ABS, ABS_HAT0Y, 1);
    push(p[1], EV_SYN, SYN_DROPPED, 0);
    push(p[1], EV_SYN, SYN_REPORT, 0);
    push(p[1], EV_ABS, ABS_HAT0Y, 1);
    check(eq_edges(pad.poll_edges(), {{PadAction::Down, true}, {PadAction::Down, false}, {PadAction::Down, true}}),
          "a hat held through a drop is released, then pressed again by the next event");

    check(pad.is_open(), "reader open before writer close");
    close(p[1]);
    check(eq(pad.poll(), {}), "poll after writer eof yields no actions");
    check(!pad.is_open(), "reader closes its fd on eof instead of spinning");

    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
