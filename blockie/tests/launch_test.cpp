#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <ftw.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../launch.h"
#include "../ports.h"

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

void write_file(const std::string& path, const std::string& body, mode_t mode)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f) {
        std::fwrite(body.data(), 1, body.size(), f);
        std::fclose(f);
        chmod(path.c_str(), mode);
    }
}

bool file_exists(const std::string& path)
{
    struct stat sb;
    return stat(path.c_str(), &sb) == 0;
}

int rm_cb(const char* path, const struct stat*, int, struct FTW*)
{
    remove(path);
    return 0;
}

void rm_rf(const std::string& p) { nftw(p.c_str(), rm_cb, 16, FTW_DEPTH | FTW_PHYS); }

bool has_line(const std::string& cfg, const std::string& line)
{
    std::string hay = "\n" + cfg;
    return hay.find("\n" + line + "\n") != std::string::npos;
}

bool has_key(const std::string& cfg, const std::string& key) { return cfg.find(key) != std::string::npos; }

std::vector<std::string> lines_of(const std::string& path)
{
    std::vector<std::string> lines;
    FILE* f = std::fopen(path.c_str(), "rb");
    char buf[512];
    while (f && std::fgets(buf, sizeof buf, f)) {
        std::string l = buf;
        if (!l.empty() && l.back() == '\n')
            l.pop_back();
        lines.push_back(l);
    }
    if (f)
        std::fclose(f);
    return lines;
}

using Ov = std::vector<std::pair<std::string, std::string>>;

}

std::string fd_list()
{
    std::vector<int> v;
    DIR* d = opendir("/proc/self/fd");
    if (!d)
        return "none";
    while (dirent* e = readdir(d)) {
        if (e->d_name[0] == '.')
            continue;
        int n = std::atoi(e->d_name);
        if (n != dirfd(d))
            v.push_back(n);
    }
    closedir(d);
    std::sort(v.begin(), v.end());
    std::string out;
    for (int n : v)
        out += (out.empty() ? "" : " ") + std::to_string(n);
    return out;
}

int main()
{
    if (const char* fdout = std::getenv("BLOCKIE_TEST_FDS")) {
        write_file(fdout, fd_list(), 0644);
        return 0;
    }
    char tmpl[] = "/tmp/blockie-launch-test.XXXXXX";
    char* root_c = mkdtemp(tmpl);
    if (!root_c) {
        std::fprintf(stderr, "mkdtemp failed\n");
        return 1;
    }
    std::string root = root_c;
    std::string data_root = root + "/data";
    std::string libretro = root + "/libretro";
    mkdir(data_root.c_str(), 0755);
    mkdir(libretro.c_str(), 0755);

    {
        Ov ov = {{"aspect_ratio_index", "1"},
                 {"video_scale_integer", "true"},
                 {"rewind_enable", "true"},
                 {"fastforward_ratio", "0"}};
        std::string cfg = fd::Launcher::emit_config(ov);
        check(has_line(cfg, "aspect_ratio_index = \"1\""), "emit_config keeps aspect_ratio_index");
        check(has_line(cfg, "video_scale_integer = \"true\""), "emit_config keeps video_scale_integer");
        check(has_line(cfg, "rewind_enable = \"true\""), "emit_config keeps rewind_enable");
        check(has_line(cfg, "fastforward_ratio = \"0\""), "emit_config keeps fastforward_ratio");
    }

    {
        Ov ov = {{"savefile_directory", "/var/lib/blockie/data/saves"},
                 {"video_driver", "vulkan"},
                 {"system_directory", "/var/lib/blockie/data/bios"},
                 {"savestate_directory", "/var/lib/blockie/data/states"}};
        std::string cfg = fd::Launcher::emit_config(ov);
        check(!has_key(cfg, "savefile_directory"), "emit_config drops savefile_directory");
        check(!has_key(cfg, "video_driver"), "emit_config drops video_driver");
        check(!has_key(cfg, "system_directory"), "emit_config drops system_directory");
        check(!has_key(cfg, "savestate_directory"), "emit_config drops savestate_directory");
        check(cfg.empty(), "emit_config of only-disallowed keys is empty");
    }

    {
        Ov ov = {{"video_shader", "/usr/share/libretro/shaders/crt.glslp"}};
        std::string cfg = fd::Launcher::emit_config(ov);
        check(has_line(cfg, "video_shader = \"/usr/share/libretro/shaders/crt.glslp\""),
              "emit_config keeps video_shader under the shaders dir");
    }
    {
        Ov ov = {{"video_shader", "/home/user/evil.glslp"}};
        std::string cfg = fd::Launcher::emit_config(ov);
        check(!has_key(cfg, "video_shader"), "emit_config drops video_shader outside the shaders dir");
    }
    {
        Ov ov = {{"video_shader", "/usr/share/libretro/shaders/crt.png"}};
        check(!has_key(fd::Launcher::emit_config(ov), "video_shader"),
              "emit_config drops video_shader with a non-glsl extension");
    }
    {
        check(has_line(fd::Launcher::emit_config({{"aspect_ratio_index", "22"}}), "aspect_ratio_index = \"22\""),
              "emit_config keeps aspect_ratio_index 22");
        check(has_line(fd::Launcher::emit_config({{"aspect_ratio_index", "24"}}), "aspect_ratio_index = \"24\""),
              "emit_config keeps aspect_ratio_index 24");
        check(!has_key(fd::Launcher::emit_config({{"aspect_ratio_index", "23"}}), "aspect_ratio_index"),
              "emit_config drops aspect_ratio_index 23 (custom viewport)");
        check(!has_key(fd::Launcher::emit_config({{"aspect_ratio_index", "25"}}), "aspect_ratio_index"),
              "emit_config drops aspect_ratio_index out of range");
        check(!has_key(fd::Launcher::emit_config({{"aspect_ratio_index", "x"}}), "aspect_ratio_index"),
              "emit_config drops aspect_ratio_index non-numeric");
    }

    {
        check(has_line(fd::Launcher::emit_config({{"video_smooth", "true"}}), "video_smooth = \"true\""),
              "emit_config keeps video_smooth true");
        check(has_line(fd::Launcher::emit_config({{"video_smooth", "false"}}), "video_smooth = \"false\""),
              "emit_config keeps video_smooth false");
        check(!has_key(fd::Launcher::emit_config({{"video_smooth", "yes"}}), "video_smooth"),
              "emit_config drops video_smooth with a non-boolean value");
    }

    write_file(libretro + "/mgba_libretro.so", "elf", 0644);
    {
        fd::Launcher l(data_root, libretro, "/usr/bin/blockie-game");
        check(l.core_installed("mgba"), "core_installed true for present core");
        check(!l.core_installed("snes9x"), "core_installed false for absent core");
        check(!l.core_installed(""), "core_installed false for empty core");
    }

    std::string flag = root + "/stub-ran";
    std::string stub = root + "/stub.sh";
    write_file(stub,
               "#!/bin/sh\n"
               "echo ran > \"" +
                   flag +
                   "\"\n"
                   "case \"$1\" in\n"
                   "  sig) kill -TERM $$ ;;\n"
                   "  *) exit \"$1\" ;;\n"
                   "esac\n",
               0755);

    std::string rom = data_root + "/roms/gba/Anguna.gba";

    struct Case {
        std::string sys;
        fd::LaunchExit want;
        const char* what;
    };
    Case cases[] = {
        {"0", fd::LaunchExit::Quit, "run exit 0 -> Quit"},
        {"75", fd::LaunchExit::Busy, "run exit 75 -> Busy"},
        {"64", fd::LaunchExit::ConfigError, "run exit 64 -> ConfigError"},
        {"69", fd::LaunchExit::CoreAbsent, "run exit 69 -> CoreAbsent"},
        {"71", fd::LaunchExit::AudioGate, "run exit 71 -> AudioGate"},
        {"70", fd::LaunchExit::Crash, "run exit 70 -> Crash"},
        {"127", fd::LaunchExit::ConfigError, "run exit 127 -> ConfigError"},
        {"1", fd::LaunchExit::Crash, "run exit 1 -> Crash"},
        {"143", fd::LaunchExit::Quit, "run exit 143 -> Quit"},
        {"sig", fd::LaunchExit::Quit, "run killed by signal -> Quit"},
    };

    fd::Launcher l(data_root, libretro, stub);
    for (const Case& c : cases) {
        fd::LaunchSpec spec;
        spec.system = c.sys;
        spec.rompath_abs = rom;
        spec.core = "mgba";
        bool handoff = false, ret = false;
        fd::LaunchExit got = l.run(spec, [&] { handoff = true; }, [&] { ret = true; });
        check(got == c.want, c.what);
        check(handoff && ret, "run calls on_handoff and on_return");
    }

    {
        fd::LaunchSpec spec;
        spec.system = "0";
        spec.rompath_abs = rom;
        spec.core = "mgba";
        fd::Launcher::Child ch = l.spawn(spec);
        check(ch.pid > 0, "spawn returns a live child");
        int status = 0;
        while (waitpid(ch.pid, &status, 0) < 0 && errno == EINTR) {
        }
        check(fd::Launcher::classify(status) == fd::LaunchExit::Quit, "spawn + classify exit 0 -> Quit");
        if (!ch.cfg_path.empty())
            unlink(ch.cfg_path.c_str());
    }
    {
        fd::LaunchSpec spec;
        spec.system = "75";
        spec.rompath_abs = rom;
        spec.core = "mgba";
        fd::Launcher::Child ch = l.spawn(spec);
        check(ch.pid > 0, "spawn returns a live child for the busy case");
        int status = 0;
        while (waitpid(ch.pid, &status, 0) < 0 && errno == EINTR) {
        }
        check(fd::Launcher::classify(status) == fd::LaunchExit::Busy, "spawn + classify exit 75 -> Busy");
        if (!ch.cfg_path.empty())
            unlink(ch.cfg_path.c_str());
    }
    {
        fd::LaunchSpec spec;
        spec.system = "0";
        spec.rompath_abs = "roms/gba/Anguna.gba";
        spec.core = "mgba";
        fd::Launcher::Child ch = l.spawn(spec);
        check(ch.pid < 0, "spawn rejects a relative rompath without forking");
    }

    {
        remove(flag.c_str());
        fd::LaunchSpec spec;
        spec.system = "0";
        spec.rompath_abs = "roms/gba/Anguna.gba";
        spec.core = "mgba";
        bool handoff = false, ret = false;
        fd::LaunchExit got = l.run(spec, [&] { handoff = true; }, [&] { ret = true; });
        check(got == fd::LaunchExit::ConfigError, "relative rompath -> ConfigError");
        check(!file_exists(flag) && !handoff && !ret, "relative rompath does not run the wrapper");
    }
    {
        remove(flag.c_str());
        fd::LaunchSpec spec;
        spec.system = "0";
        spec.rompath_abs = "/etc/passwd";
        spec.core = "mgba";
        fd::LaunchExit got = l.run(spec);
        check(got == fd::LaunchExit::ConfigError, "out-of-data_root rompath -> ConfigError");
        check(!file_exists(flag), "out-of-data_root rompath does not run the wrapper");
    }
    {
        remove(flag.c_str());
        fd::LaunchSpec spec;
        spec.system = "0";
        spec.rompath_abs = data_root + "/roms/../../etc/passwd";
        spec.core = "mgba";
        fd::LaunchExit got = l.run(spec);
        check(got == fd::LaunchExit::ConfigError, "dotdot traversal rompath -> ConfigError");
        check(!file_exists(flag), "dotdot traversal rompath does not run the wrapper");
    }

    {
        fd::LaunchSpec spec;
        spec.system = "0";
        spec.rompath_abs = rom;
        spec.core = "";
        fd::LaunchExit got = l.run(spec);
        check(got == fd::LaunchExit::Quit, "run with empty overrides and no core still launches (Quit)");
    }

    {
        std::string argf = root + "/stub-args";
        std::string astub = root + "/args.sh";
        std::string synth = data_root + "/roms/gba/synth.gba";
        write_file(astub, "#!/bin/sh\nprintf '%s\\n' \"$@\" > \"" + argf + "\"\nexit 0\n", 0755);
        fd::Launcher la(data_root, libretro, astub);
        auto args_of = [&](const std::string& theme) {
            remove(argf.c_str());
            fd::LaunchSpec spec;
            spec.system = "gba";
            spec.rompath_abs = synth;
            spec.core = "mgba";
            spec.theme = theme;
            fd::Launcher::Child ch = la.spawn(spec);
            int status = 0;
            while (ch.pid > 0 && waitpid(ch.pid, &status, 0) < 0 && errno == EINTR) {
            }
            std::vector<std::string> lines;
            FILE* f = std::fopen(argf.c_str(), "rb");
            char buf[512];
            while (f && std::fgets(buf, sizeof buf, f)) {
                std::string l = buf;
                if (!l.empty() && l.back() == '\n')
                    l.pop_back();
                lines.push_back(l);
            }
            if (f)
                std::fclose(f);
            return lines;
        };
        std::vector<std::string> light = args_of("light");
        std::vector<std::string> want = {"gba", synth, "--core", "mgba", "--theme", "light"};
        check(light == want, "spawn passes --theme light after the core");
        std::vector<std::string> dark = args_of("dark");
        check(dark.size() == 6 && dark[4] == "--theme" && dark[5] == "dark", "spawn passes --theme dark");
        std::vector<std::string> none = args_of("");
        check(none.size() == 4 && std::find(none.begin(), none.end(), "--theme") == none.end(),
              "spawn with no theme passes no --theme");
    }

    {
        fd::Launcher lc(data_root, libretro, "/usr/bin/blockie-game");
        check(lc.check("ports", "", fd::HelperOp::None) == fd::LaunchCheck::Go,
              "a port launches with no core installed");
        check(lc.check("gba", "mgba", fd::HelperOp::None) == fd::LaunchCheck::Go,
              "a game whose core is installed launches");
        check(lc.check("gba", "snes9x", fd::HelperOp::None) == fd::LaunchCheck::CoreAbsent,
              "a game whose core is absent is refused");
        check(lc.check("gba", "", fd::HelperOp::None) == fd::LaunchCheck::CoreAbsent,
              "the core gate is skipped only for ports");
        check(lc.check("ports", "", fd::HelperOp::Install) == fd::LaunchCheck::HelperBusy,
              "a port launch is refused while the helper installs");
        check(lc.check("gba", "mgba", fd::HelperOp::Remove) == fd::LaunchCheck::HelperBusy,
              "a game launch is refused while the helper removes a port");
    }

    {
        std::string argf = root + "/port-args";
        std::string pstub = root + "/port-args.sh";
        std::string script = data_root + "/roms/ports/blockie-test-port.sh";
        write_file(pstub, "#!/bin/sh\nprintf '%s\\n' \"$@\" > \"" + argf + "\"\nexit 0\n", 0755);
        fd::Launcher lp(data_root, libretro, pstub);
        fd::LaunchSpec spec;
        spec.system = "ports";
        spec.rompath_abs = script;
        spec.theme = "dark";
        fd::Launcher::Child ch = lp.spawn(spec);
        int status = 0;
        while (ch.pid > 0 && waitpid(ch.pid, &status, 0) < 0 && errno == EINTR) {
        }
        std::vector<std::string> want = {"ports", script, "--theme", "dark"};
        check(ch.pid > 0 && lines_of(argf) == want, "a port spawns blockie-game ports <script> with no --core");
    }

    {
        fd::PortsHelper h;
        std::vector<fd::HelperResult> done;
        h.on_result = [&](const fd::HelperResult& r) { done.push_back(r); };
        h.line("progress\tblockie-test-port\t1\t-");
        h.line("fail\tblockie-test-port\tnetwork");
        h.line("done");
        check(done.empty(), "progress, fail and a bare done report no result");
        h.line("done\tblockie-test-port");
        check(done.empty(), "a done line read before the helper is reaped reports no result");
    }

    {
        std::string fdf = root + "/game-fds";
        int leak = open("/dev/null", O_RDONLY);
        setenv("BLOCKIE_TEST_FDS", fdf.c_str(), 1);
        fd::Launcher lf(data_root, libretro, "/proc/self/exe");
        fd::LaunchSpec spec;
        spec.system = "ports";
        spec.rompath_abs = data_root + "/roms/ports/blockie-test-port.sh";
        fd::Launcher::Child ch = lf.spawn(spec);
        unsetenv("BLOCKIE_TEST_FDS");
        int status = 0;
        while (ch.pid > 0 && waitpid(ch.pid, &status, 0) < 0 && errno == EINTR) {
        }
        if (leak >= 0)
            close(leak);
        std::string got;
        if (FILE* f = std::fopen(fdf.c_str(), "rb")) {
            char b[256] = {};
            size_t n = std::fread(b, 1, sizeof b - 1, f);
            got.assign(b, n);
            std::fclose(f);
        }
        check(leak >= 3 && got == "0 1 2", "a launched game holds fds 0 to 2 only");
    }

    rm_rf(root);
    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
