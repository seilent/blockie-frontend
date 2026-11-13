#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <sys/types.h>
#include <vector>

#include "../gfx/font.h"
#include "layer_present.h"
#include "pm_input.h"

namespace fd {

struct PmRunOpts {
    std::string gates = "/run/blockie/boot/state";
    std::string fifo;
    std::vector<std::string> systemctl = {"systemctl"};
    bool dark = false;
    bool pause = false;
    int in = -1;
    int idle_ms = 0;
    int toast_in_ms = 0;
    int toast_out_ms = 0;
    int out = 1;
    std::function<int64_t()> clock;
    std::function<void(int64_t now, int64_t wake, int64_t probe_at)> on_wake;
};

struct Spawned {
    pid_t pid = -1;
    int fd = -1;
};

Spawned spawn_capture(const std::vector<std::string>& argv);
bool reap(pid_t pid, int& status);
int run_powermenu(LayerPresenter& p, Fonts& fonts, MenuGrab& grab, const PmRunOpts& o);

}
