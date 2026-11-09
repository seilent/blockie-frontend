#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

#include <ftw.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../children.h"
#include "../volume.h"

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

int64_t now_ms()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

void write_file(const std::string& path, const std::string& body, mode_t mode = 0644)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f) {
        std::fwrite(body.data(), 1, body.size(), f);
        std::fclose(f);
        chmod(path.c_str(), mode);
    }
}

std::string read_file(const std::string& path)
{
    std::string out;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f)
        return out;
    char buf[512];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
        out.append(buf, n);
    std::fclose(f);
    return out;
}

int rm_cb(const char* path, const struct stat*, int, struct FTW*)
{
    remove(path);
    return 0;
}

std::string make_root()
{
    const char* dir = std::getenv("TMPDIR");
    std::string tmpl = std::string(dir && *dir ? dir : "/tmp") + "/blockie-volume-XXXXXX";
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    return mkdtemp(buf.data()) ? std::string(buf.data()) : std::string();
}

struct Rig {
    std::string root;
    Children kids;
    Volume vol;
    std::vector<VolumeLevel> values;
    std::vector<bool> sets;
    int fails = 0;

    explicit Rig(const std::string& r) : root(r), vol(kids, r + "/tool")
    {
        vol.on_value = [this](const VolumeLevel& v) { values.push_back(v); };
        vol.on_fail = [this]() { fails++; };
        vol.on_set = [this](bool ok) { sets.push_back(ok); };
    }

    void step()
    {
        int st = 0;
        pid_t p;
        while ((p = waitpid(-1, &st, WNOHANG)) > 0)
            kids.reaped(p, st);
        if (vol.fd() >= 0) {
            pollfd pf{vol.fd(), POLLIN, 0};
            if (poll(&pf, 1, 10) > 0)
                vol.readable();
        } else {
            usleep(5000);
        }
    }

    bool until(const std::function<bool()>& cond, int ms = 5000)
    {
        int64_t end = now_ms() + ms;
        while (now_ms() < end) {
            step();
            if (cond())
                return true;
        }
        return cond();
    }

    bool idle() const { return !vol.getting() && !vol.setting() && kids.running() == 0; }

    std::vector<std::string> calls() const
    {
        std::vector<std::string> v;
        std::string log = read_file(root + "/log");
        size_t at = 0;
        while (at < log.size()) {
            size_t e = log.find('\n', at);
            if (e == std::string::npos)
                e = log.size();
            v.push_back(log.substr(at, e - at));
            at = e + 1;
        }
        return v;
    }
};

}

int main()
{
    VolumeLevel v;
    check(parse_volume("40 unmuted\n", v) && v.pct == 40 && !v.muted, "40 unmuted");
    check(parse_volume("0 muted", v) && v.pct == 0 && v.muted, "0 muted with no newline");
    check(parse_volume("100 unmuted\n", v) && v.pct == 100, "100");
    bool bad = false;
    for (const char* s : {"", "40", "40 MUTED", "-1 muted", "1000 unmuted", "40 unmuted extra", " 40 muted",
                          "4a unmuted", "Volume: 0.40\n"})
        bad = bad || parse_volume(s, v);
    check(!bad, "anything else is refused");

    std::string root = make_root();
    if (root.empty()) {
        std::printf("FAIL no temp dir\n");
        return 1;
    }
    write_file(root + "/tool",
               "#!/bin/sh\n"
               "d='" + root + "'\n"
               "echo \"$*\" >> \"$d/log\"\n"
               "case \"$1\" in\n"
               "get)\n"
               "    sleep \"$(cat \"$d/delay\")\"\n"
               "    cat \"$d/out\"\n"
               "    exit \"$(cat \"$d/rc\")\"\n"
               "    ;;\n"
               "set)\n"
               "    sleep 0.2\n"
               "    exit \"$(cat \"$d/setrc\")\"\n"
               "    ;;\n"
               "esac\n",
               0755);
    write_file(root + "/delay", "0\n");
    write_file(root + "/out", "40 unmuted\n");
    write_file(root + "/rc", "0\n");
    write_file(root + "/setrc", "0\n");

    Rig rig(root);
    check(rig.vol.fd() < 0 && rig.idle(), "idle: no pipe in the poll set");
    rig.vol.get();
    check(rig.vol.fd() >= 0 && rig.vol.getting(), "a get polls its pipe while it runs");
    check(rig.until([&] { return rig.values.size() == 1; }) && rig.values[0].pct == 40 && !rig.values[0].muted,
          "get reads 40 unmuted");
    check(rig.until([&] { return rig.idle(); }) && rig.vol.fd() < 0, "the pipe leaves the poll set when the get ends");

    write_file(root + "/out", "0 muted\n");
    rig.vol.get();
    check(rig.until([&] { return rig.values.size() == 2; }) && rig.values[1].muted, "get reads muted");
    write_file(root + "/out", "Volume: 0.40\n");
    rig.vol.get();
    check(rig.until([&] { return rig.fails == 1; }) && rig.values.size() == 2, "output in another form fails the get");
    write_file(root + "/out", "40 unmuted\n");
    write_file(root + "/rc", "1\n");
    rig.vol.get();
    check(rig.until([&] { return rig.fails == 2; }), "a get that exits non zero fails");
    write_file(root + "/rc", "0\n");
    rig.until([&] { return rig.idle(); });

    check(!rig.vol.set(101) && !rig.vol.set(-1) && rig.idle(), "set refuses a level outside 0 to 100");
    write_file(root + "/log", "");
    check(rig.vol.set(10) && rig.vol.set(20) && rig.vol.set(30), "three quick sets are taken");
    check(rig.until([&] { return rig.idle() && rig.sets.size() == 1; }) && rig.sets[0], "one outcome for the run");
    {
        std::vector<std::string> c = rig.calls();
        check(c.size() == 2 && c[0] == "set 10" && c[1] == "set 30", "one set in flight, only the newest queued");
    }

    write_file(root + "/log", "");
    size_t seen = rig.values.size();
    rig.vol.set(50);
    rig.vol.get();
    check(!rig.vol.getting(), "a get waits while a set runs");
    check(rig.until([&] { return rig.idle() && rig.values.size() == seen + 1; }), "the get runs after the set");
    {
        std::vector<std::string> c = rig.calls();
        check(c.size() == 2 && c[0] == "set 50" && c[1] == "get", "set, then get");
    }

    write_file(root + "/log", "");
    write_file(root + "/delay", "0.3\n");
    seen = rig.values.size();
    rig.vol.get();
    rig.until([&] { return !rig.calls().empty(); });
    rig.vol.set(60);
    check(rig.until([&] { return rig.idle() && rig.values.size() > seen; }) && rig.values.size() == seen + 1,
          "a set during a get drops that get's value and reads again after the set");
    {
        std::vector<std::string> c = rig.calls();
        check(c.size() == 3 && c[0] == "get" && c[1] == "set 60" && c[2] == "get", "get, set, get");
    }
    write_file(root + "/delay", "0\n");

    write_file(root + "/setrc", "1\n");
    size_t outcomes = rig.sets.size();
    rig.vol.set(70);
    check(rig.until([&] { return rig.idle() && rig.sets.size() == outcomes + 1; }) && !rig.sets.back(),
          "a failed set reports false");
    write_file(root + "/setrc", "0\n");

    Children kids;
    check(!kids.reaped(1, 0), "a pid the children map does not hold is not taken");
    Volume none(kids, root + "/missing");
    int none_fails = 0;
    none.on_fail = [&]() { none_fails++; };
    none.get();
    {
        int st = 0;
        int64_t end = now_ms() + 3000;
        while (none_fails == 0 && now_ms() < end) {
            pid_t p;
            while ((p = waitpid(-1, &st, WNOHANG)) > 0)
                kids.reaped(p, st);
            usleep(5000);
        }
    }
    check(none_fails == 1 && !none.getting(), "a missing tool fails the get");

    nftw(root.c_str(), rm_cb, 16, FTW_DEPTH | FTW_PHYS);
    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
