#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <poll.h>
#include <wayland-client.h>

#ifdef BLOCKIE_HAVE_XKB
#include <xkbcommon/xkbcommon-keysyms.h>
#else
#include <linux/input-event-codes.h>
#endif

#include "../gfx/pixel.h"
#include "input_pad.h"
#include "library.h"
#include "present_wl.h"
#include "screen_selector.h"

namespace fd {

namespace {

volatile sig_atomic_t g_quit = 0;

void on_signal(int) { g_quit = 1; }

int64_t now_ms()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

const char* action_name(PadAction a)
{
    switch (a) {
    case PadAction::Up: return "up";
    case PadAction::Down: return "down";
    case PadAction::Left: return "left";
    case PadAction::Right: return "right";
    case PadAction::Accept: return "accept";
    case PadAction::Back: return "back";
    }
    return "?";
}

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

    std::string font_dir;
    if (const char* env = std::getenv("BLOCKIE_FONTS"))
        font_dir = env;
    if (font_dir.empty())
        font_dir = "/usr/share/blockie/fonts";
    Fonts fonts;
    if (!fonts.open(font_dir, {}))
        std::fprintf(stderr, "blockie-frontend: fonts not found in %s\n", font_dir.c_str());

    Library lib;
    SelectorModel model;
    model.focus = 0;
    if (lib.open("")) {
        int n = lib.scan();
        std::fprintf(stderr, "blockie-frontend: %d games\n", n);
        for (const auto& sw : lib.systems_with_games())
            model.systems.push_back({sw.first.name, sw.second});
    }

    bool need_redraw = false;
    auto redraw = [&]() {
        int64_t tt = now_ms();
        p.begin_frame(tt, p.show_at(tt));
        p.begin_full();
        Canvas c(p.draw_target());
        fonts.begin_frame(-1);
        draw_selector(c, fonts, model, Pane::Selector);
        p.present(kScreen);
        p.flip();
    };
    redraw();

    auto apply = [&](PadAction a) {
        int cnt = (int)model.systems.size();
        if (a == PadAction::Up && model.focus > 0) {
            model.focus--;
            need_redraw = true;
        } else if (a == PadAction::Down && cnt > 0 && model.focus < cnt - 1) {
            model.focus++;
            need_redraw = true;
        }
    };

    p.set_key_handler([&](uint32_t sym, bool pressed) {
        if (!pressed)
            return;
        bool have = true;
        PadAction a = PadAction::Up;
#ifdef BLOCKIE_HAVE_XKB
        if (sym == XKB_KEY_Up) a = PadAction::Up;
        else if (sym == XKB_KEY_Down) a = PadAction::Down;
        else if (sym == XKB_KEY_Left) a = PadAction::Left;
        else if (sym == XKB_KEY_Right) a = PadAction::Right;
        else if (sym == XKB_KEY_Return) a = PadAction::Accept;
        else if (sym == XKB_KEY_Escape) a = PadAction::Back;
        else have = false;
#else
        if (sym == KEY_UP) a = PadAction::Up;
        else if (sym == KEY_DOWN) a = PadAction::Down;
        else if (sym == KEY_LEFT) a = PadAction::Left;
        else if (sym == KEY_RIGHT) a = PadAction::Right;
        else if (sym == KEY_ENTER) a = PadAction::Accept;
        else if (sym == KEY_ESC) a = PadAction::Back;
        else have = false;
#endif
        if (!have)
            return;
        std::fprintf(stderr, "blockie-frontend: key %s\n", action_name(a));
        apply(a);
    });

    PadReader pad;
    bool pad_ok = pad.open();
    std::fprintf(stderr, pad_ok ? "blockie-frontend: pad fd %d\n" : "blockie-frontend: no pad\n", pad.fd());

    std::signal(SIGTERM, on_signal);
    std::signal(SIGINT, on_signal);

    wl_display* d = p.display();
    int wl_fd = p.event_fd();
    while (p.ready() && !g_quit) {
        while (wl_display_prepare_read(d) != 0)
            wl_display_dispatch_pending(d);
        wl_display_flush(d);

        struct pollfd fds[2];
        int nfds = 1;
        fds[0].fd = wl_fd;
        fds[0].events = POLLIN;
        fds[0].revents = 0;
        if (pad_ok) {
            fds[1].fd = pad.fd();
            fds[1].events = POLLIN;
            fds[1].revents = 0;
            nfds = 2;
        }
        int pr = poll(fds, nfds, -1);
        if (pr < 0) {
            wl_display_cancel_read(d);
            if (errno == EINTR)
                continue;
            break;
        }
        if (fds[0].revents & (POLLIN | POLLHUP | POLLERR)) {
            if (wl_display_read_events(d) < 0)
                break;
        } else {
            wl_display_cancel_read(d);
        }
        if (wl_display_dispatch_pending(d) < 0)
            break;

        if (pad_ok && (fds[1].revents & (POLLIN | POLLHUP | POLLERR))) {
            for (PadAction a : pad.poll()) {
                std::fprintf(stderr, "blockie-frontend: pad %s\n", action_name(a));
                if (p.activated())
                    apply(a);
            }
            if (!pad.is_open()) {
                pad_ok = false;
                std::fprintf(stderr, "blockie-frontend: pad disconnected\n");
            }
        }

        if (need_redraw) {
            redraw();
            need_redraw = false;
        }
    }
    p.stop();
    std::fprintf(stderr, "blockie-frontend: exiting\n");
    return 0;
}

}

int main() { return fd::run(); }
