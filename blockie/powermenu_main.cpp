#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include <poll.h>

#include "bus.h"
#include "layer_present.h"
#include "pm_input.h"
#include "pm_run.h"
#include "theme.h"
#include "theme_setting.h"

namespace fd {

namespace {

constexpr int kThemeWaitMs = 300;

int64_t now_ms()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

std::string env_or(const char* name, const char* fallback)
{
    const char* v = std::getenv(name);
    return v && *v ? v : fallback;
}

void read_theme()
{
    Bus bus;
    ThemeSetting theme_set;
    theme_set.on_value = [](bool light) { set_theme_light(light); };
    theme_set.attach(bus);
    int64_t t = now_ms();
    if (!bus.open(t))
        return;
    int64_t until = t + kThemeWaitMs;
    while (!theme_set.answered() && now_ms() < until && bus.connected()) {
        struct pollfd bf;
        bf.fd = bus.fd();
        bf.events = POLLIN | (bus.wants_write() ? POLLOUT : 0);
        bf.revents = 0;
        if (!bus.data_remains())
            poll(&bf, 1, (int)std::max<int64_t>(0, until - now_ms()));
        bus.pump();
    }
}

}

}

int main()
{
    using namespace fd;
    MenuGrab grab(real_evdev());
    grab.start(now_ms());
    read_theme();
    std::fprintf(stderr, "blockie-powermenu: theme %s\n", theme().light ? "light" : "dark");
    std::string font_dir = env_or("BLOCKIE_FONTS", "/usr/share/blockie/fonts");
    Fonts fonts;
    if (!fonts.open(font_dir, {}))
        std::fprintf(stderr, "blockie-powermenu: fonts not found in %s\n", font_dir.c_str());
    PmRunOpts o;
    o.gates = env_or("BLOCKIE_BOOT_STATE", "/run/blockie/boot/state");
    o.fifo = env_or("BLOCKIE_POWERMENU_INPUT", "");
    o.dark = env_or("BLOCKIE_POWERMENU_FROM", "awake") == "dark";
    std::unique_ptr<LayerPresenter> p = make_layer_presenter();
    return run_powermenu(*p, fonts, grab, o);
}
