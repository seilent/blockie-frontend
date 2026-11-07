#include <chrono>
#include <poll.h>
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
    WaylandPresenter p;
    if (!p.open("", true))
        return 1;

    int64_t t = now_ms();
    p.begin_frame(t, p.show_at(t));
    p.begin_full();
    WaylandPresenter::S s = p.draw_target();
    for (int y = 0; y < kPortraitH; y++)
        fill_px(s.row(y), 0x101418u, kPortraitW);
    p.present(kScreen);
    p.flip();

    wl_display* d = p.display();
    for (int i = 0; i < 60 && p.ready(); i++) {
        if (wl_display_dispatch_pending(d) < 0)
            break;
        p.flush();
        pollfd pfd{p.event_fd(), POLLIN, 0};
        if (poll(&pfd, 1, p.period_ms()) > 0 && wl_display_dispatch(d) < 0)
            break;
    }
    p.stop();
    return 0;
}

}

int main() { return fd::run(); }
