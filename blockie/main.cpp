#include <chrono>
#include <cstdio>
#include <wayland-client.h>

#include "present_wl.h"

namespace fd {

static int64_t now_ms()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

static int run()
{
    std::fprintf(stderr, "blockie-frontend: started\n");
    WaylandPresenter p;
    if (!p.open("", true)) {
        std::fprintf(stderr, "blockie-frontend: no wayland display, exiting\n");
        return 1;
    }

    int64_t t = now_ms();
    p.begin_frame(t, p.show_at(t));
    p.begin_full();
    WaylandPresenter::S s = p.draw_target();
    for (int y = 0; y < kPortraitH; y++)
        fill_px(s.row(y), 0x101418u, kPortraitW);
    p.present(kScreen);
    p.flip();

    wl_display* d = p.display();
    while (p.ready()) {
        p.flush();
        if (wl_display_dispatch(d) < 0)
            break;
    }
    p.stop();
    std::fprintf(stderr, "blockie-frontend: exiting\n");
    return 0;
}

}

int main() { return fd::run(); }
