#pragma once
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace fd {

enum class LaunchExit { Quit, Crash, Busy, ConfigError, CoreAbsent, AudioGate };

struct LaunchSpec {
    std::string system;
    std::string rompath_abs;
    std::string core;
    std::vector<std::pair<std::string, std::string>> overrides;
};

class Launcher {
public:
    Launcher(std::string data_root, std::string libretro_dir = "/usr/lib64/libretro",
             std::string wrapper = "/usr/bin/blockie-game");

    bool core_installed(const std::string& core) const;
    static std::string emit_config(const std::vector<std::pair<std::string, std::string>>& overrides);
    LaunchExit run(const LaunchSpec& spec, const std::function<void()>& on_handoff = {},
                   const std::function<void()>& on_return = {});

private:
    std::string data_root_;
    std::string libretro_dir_;
    std::string wrapper_;
};

}
