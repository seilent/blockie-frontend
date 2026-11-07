#include "launch.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fd {

namespace {

bool starts_with(const std::string& s, const std::string& prefix)
{
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool ends_with(const std::string& s, const std::string& suffix)
{
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool allowed_key(const std::string& key, const std::string& value)
{
    static const char* const kKeys[] = {"aspect_ratio_index",  "video_scale_integer", "video_shader_enable",
                                         "video_shader",        "video_smooth",        "rewind_enable",
                                         "savestate_auto_save", "savestate_auto_load", "fastforward_ratio"};
    bool known = false;
    for (const char* k : kKeys) {
        if (key == k) {
            known = true;
            break;
        }
    }
    if (!known)
        return false;
    if (key == "video_shader")
        return starts_with(value, "/usr/share/libretro/shaders/") && (ends_with(value, ".glsl") || ends_with(value, ".glslp"));
    if (key == "video_smooth")
        return value == "true" || value == "false";
    if (key == "aspect_ratio_index") {
        char* end = nullptr;
        long v = std::strtol(value.c_str(), &end, 10);
        return end != value.c_str() && *end == '\0' && ((v >= 0 && v <= 22) || v == 24);
    }
    return true;
}

bool under_data_root(const std::string& data_root, const std::string& path)
{
    if (path.empty() || path[0] != '/')
        return false;
    if (!starts_with(path, data_root + "/"))
        return false;
    size_t i = 0;
    while (i < path.size()) {
        size_t j = path.find('/', i);
        size_t end = j == std::string::npos ? path.size() : j;
        if (path.compare(i, end - i, "..") == 0)
            return false;
        if (j == std::string::npos)
            break;
        i = j + 1;
    }
    return true;
}

}

Launcher::Launcher(std::string data_root, std::string libretro_dir, std::string wrapper)
    : data_root_(std::move(data_root)), libretro_dir_(std::move(libretro_dir)), wrapper_(std::move(wrapper))
{
    while (data_root_.size() > 1 && data_root_.back() == '/')
        data_root_.pop_back();
    while (libretro_dir_.size() > 1 && libretro_dir_.back() == '/')
        libretro_dir_.pop_back();
}

bool Launcher::core_installed(const std::string& core) const
{
    if (core.empty())
        return false;
    std::string path = libretro_dir_ + "/" + core + "_libretro.so";
    struct stat sb;
    return stat(path.c_str(), &sb) == 0 && S_ISREG(sb.st_mode);
}

std::string Launcher::emit_config(const std::vector<std::pair<std::string, std::string>>& overrides)
{
    std::string out;
    for (const auto& kv : overrides) {
        if (!allowed_key(kv.first, kv.second))
            continue;
        out += kv.first;
        out += " = \"";
        out += kv.second;
        out += "\"\n";
    }
    return out;
}

LaunchExit Launcher::classify(int status)
{
    if (WIFSIGNALED(status))
        return LaunchExit::Quit;
    if (!WIFEXITED(status))
        return LaunchExit::Crash;
    switch (WEXITSTATUS(status)) {
    case 0:
        return LaunchExit::Quit;
    case 75:
        return LaunchExit::Busy;
    case 64:
        return LaunchExit::ConfigError;
    case 69:
        return LaunchExit::CoreAbsent;
    case 71:
        return LaunchExit::AudioGate;
    case 129:
    case 130:
    case 143:
        return LaunchExit::Quit;
    default:
        return LaunchExit::Crash;
    }
}

Launcher::Child Launcher::spawn(const LaunchSpec& spec)
{
    if (!under_data_root(data_root_, spec.rompath_abs))
        return {};

    std::string cfg = emit_config(spec.overrides);
    std::string cfg_path;
    if (!cfg.empty()) {
        std::string cache = data_root_ + "/cache";
        mkdir(cache.c_str(), 0755);
        cfg_path = cache + "/launch-" + std::to_string((long)getpid()) + ".cfg";
        FILE* f = std::fopen(cfg_path.c_str(), "wb");
        if (!f)
            return {};
        bool ok = std::fwrite(cfg.data(), 1, cfg.size(), f) == cfg.size();
        std::fclose(f);
        if (!ok) {
            unlink(cfg_path.c_str());
            return {};
        }
    }

    std::vector<std::string> args;
    args.push_back(wrapper_);
    args.push_back(spec.system);
    args.push_back(spec.rompath_abs);
    if (!spec.core.empty()) {
        args.push_back("--core");
        args.push_back(spec.core);
    }
    if (!cfg_path.empty()) {
        args.push_back("--config");
        args.push_back(cfg_path);
    }
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (std::string& a : args)
        argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    pid_t pid = fork();
    if (pid == 0) {
        execv(wrapper_.c_str(), argv.data());
        _exit(127);
    }
    if (pid < 0) {
        if (!cfg_path.empty())
            unlink(cfg_path.c_str());
        return {};
    }
    return {pid, cfg_path};
}

LaunchExit Launcher::run(const LaunchSpec& spec, const std::function<void()>& on_handoff,
                         const std::function<void()>& on_return)
{
    if (!under_data_root(data_root_, spec.rompath_abs))
        return LaunchExit::ConfigError;

    std::string cfg = emit_config(spec.overrides);
    std::string cfg_path;
    if (!cfg.empty()) {
        std::string cache = data_root_ + "/cache";
        mkdir(cache.c_str(), 0755);
        cfg_path = cache + "/launch-" + std::to_string((long)getpid()) + ".cfg";
        FILE* f = std::fopen(cfg_path.c_str(), "wb");
        if (!f)
            return LaunchExit::ConfigError;
        bool ok = std::fwrite(cfg.data(), 1, cfg.size(), f) == cfg.size();
        std::fclose(f);
        if (!ok) {
            unlink(cfg_path.c_str());
            return LaunchExit::ConfigError;
        }
    }

    std::vector<std::string> args;
    args.push_back(wrapper_);
    args.push_back(spec.system);
    args.push_back(spec.rompath_abs);
    if (!spec.core.empty()) {
        args.push_back("--core");
        args.push_back(spec.core);
    }
    if (!cfg_path.empty()) {
        args.push_back("--config");
        args.push_back(cfg_path);
    }
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (std::string& a : args)
        argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    if (on_handoff)
        on_handoff();

    LaunchExit result = LaunchExit::Crash;
    pid_t pid = fork();
    if (pid == 0) {
        execv(wrapper_.c_str(), argv.data());
        _exit(127);
    } else if (pid > 0) {
        int status = 0;
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        }
        result = classify(status);
    }

    if (on_return)
        on_return();

    if (!cfg_path.empty())
        unlink(cfg_path.c_str());

    return result;
}

}
