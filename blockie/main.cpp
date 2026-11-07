#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <wayland-client.h>

#ifdef BLOCKIE_HAVE_XKB
#include <xkbcommon/xkbcommon-keysyms.h>
#else
#include <linux/input-event-codes.h>
#endif

#include "../gfx/pixel.h"
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

    bool need_redraw = false;
    p.set_key_handler([&](uint32_t sym, bool pressed) {
        if (!pressed)
            return;
        int cnt = (int)model.systems.size();
        if (cnt == 0)
            return;
#ifdef BLOCKIE_HAVE_XKB
        bool up = sym == XKB_KEY_Up;
        bool down = sym == XKB_KEY_Down;
#else
        bool up = sym == KEY_UP;
        bool down = sym == KEY_DOWN;
#endif
        if (up && model.focus > 0) {
            model.focus--;
            need_redraw = true;
        } else if (down && model.focus < cnt - 1) {
            model.focus++;
            need_redraw = true;
        }
    });

    std::signal(SIGTERM, on_signal);
    std::signal(SIGINT, on_signal);

    wl_display* d = p.display();
    while (p.ready() && !g_quit) {
        p.flush();
        if (wl_display_dispatch(d) < 0) {
            if (errno == EINTR)
                continue;
            break;
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
