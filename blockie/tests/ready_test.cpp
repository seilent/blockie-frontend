#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

#include <fcntl.h>
#include <ftw.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../children.h"
#include "../ready.h"

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

int64_t real_ms()
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
    std::string tmpl = std::string(dir && *dir ? dir : "/tmp") + "/blockie-ready-XXXXXX";
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    return mkdtemp(buf.data()) ? std::string(buf.data()) : std::string();
}

size_t count(const std::string& text, const std::string& line)
{
    size_t n = 0;
    for (size_t at = text.find(line); at != std::string::npos; at = text.find(line, at + line.size()))
        n++;
    return n;
}

const std::string kLine = "blockie-frontend: ready report\n";
const std::string kOk = "blockie-frontend: ready report ok\n";
const std::string kFailed = "blockie-frontend: ready report failed";
const std::string kGivenUp = "blockie-frontend: ready report given up\n";

struct Rig {
    std::string root;
    Children kids;
    int64_t clock = 1000;
    ReadyReport ready;

    Rig(const std::string& r, const std::string& tool) : root(r), ready(kids, [this]() { return clock; }, tool)
    {
        write_file(root + "/log", "");
        write_file(root + "/err", "");
    }

    void reap()
    {
        int st = 0;
        pid_t p;
        while ((p = waitpid(-1, &st, WNOHANG)) > 0)
            kids.reaped(p, st);
    }

    bool settle(int ms = 5000)
    {
        int64_t end = real_ms() + ms;
        while (kids.running() > 0 && real_ms() < end) {
            reap();
            if (kids.running() > 0)
                usleep(5000);
        }
        return kids.running() == 0;
    }

    void run(const std::function<void()>& body)
    {
        std::fflush(stderr);
        int saved = fcntl(STDERR_FILENO, F_DUPFD_CLOEXEC, 0);
        int f = open((root + "/err").c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
        if (f >= 0) {
            dup2(f, STDERR_FILENO);
            close(f);
        }
        body();
        std::fflush(stderr);
        if (saved >= 0) {
            dup2(saved, STDERR_FILENO);
            close(saved);
        }
    }

    std::string err() const { return read_file(root + "/err"); }

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
    unsetenv("INVOCATION_ID");
    std::string root = make_root();
    if (root.empty()) {
        std::printf("FAIL no temp dir\n");
        return 1;
    }
    std::string tool = root + "/tool";
    write_file(tool,
               "#!/bin/sh\n"
               "d='" + root + "'\n"
               "echo \"$*${INVOCATION_ID:+ $INVOCATION_ID}\" >> \"$d/log\"\n"
               "sleep \"$(cat \"$d/delay\")\"\n"
               "rc=\"$(cat \"$d/rc\")\"\n"
               "case \"$rc\" in\n"
               "kill)\n"
               "    kill -9 $$\n"
               "    ;;\n"
               "esac\n"
               "exit \"$rc\"\n",
               0755);
    write_file(root + "/delay", "0\n");
    write_file(root + "/rc", "0\n");

    {
        Rig r(root, tool);
        size_t a = 1, b = 1, c = 1;
        bool wants = false, armed_wants = true;
        r.run([&] {
            r.ready.shown(true, true);
            a = r.kids.running();
            r.ready.posted(false, true);
            wants = r.ready.wants_frame(true, true);
            r.ready.shown(true, true);
            b = r.kids.running();
            r.ready.posted(true, true);
            armed_wants = r.ready.wants_frame(true, true);
            r.ready.tick();
            c = r.kids.running();
        });
        check(a == 0, "1 a shown frame with nothing posted spawns nothing");
        check(b == 0 && wants, "1 a frame posted without the pad spawns nothing on show and asks for a frame");
        check(c == 0 && !armed_wants, "1 a posted frame with no show spawns nothing and asks for no frame");
        check(r.calls().empty() && r.err().empty() && r.ready.status() == -1 && r.ready.deadline() == -1,
              "1 no call, no line, status -1, no deadline");
    }

    {
        Rig r(root, tool);
        bool settled = false;
        r.run([&] {
            r.ready.posted(false, true);
            r.ready.posted(true, true);
            r.ready.shown(true, true);
            settled = r.settle();
        });
        std::vector<std::string> c = r.calls();
        std::string e = r.err();
        check(settled && c.size() == 1 && c[0] == "ready", "2 one call with argv ready");
        check(r.ready.status() == 0 && r.ready.deadline() == -1, "2 status 0, no deadline");
        check(count(e, kLine) == 1 && count(e, kOk) == 1 && count(e, kFailed) == 0,
              "2 one ready report line, ok, no failed");
        r.run([&] {
            for (int i = 0; i < 3; i++) {
                r.ready.posted(true, true);
                r.ready.shown(true, true);
                r.clock += 100000;
                r.ready.tick();
                r.settle();
            }
        });
        check(r.calls().size() == 1 && count(r.err(), kLine) == 1 && !r.ready.wants_frame(true, true),
              "2 three more rounds after ok: still one call");
    }

    {
        Rig r(root, tool);
        size_t n = 1;
        r.run([&] {
            r.ready.posted(true, true);
            r.ready.shown(false, true);
            n = r.kids.running();
        });
        check(n == 0 && r.calls().empty(), "3 a pad hangup before the callback spawns nothing");
        check(!r.ready.wants_frame(false, true), "3 no frame is asked while the pad is closed");
    }

    {
        Rig r(root, tool);
        size_t n = 1, m = 0;
        bool wants = false, settled = false;
        r.run([&] {
            r.ready.posted(true, true);
            r.ready.shown(true, false);
            n = r.kids.running();
            wants = r.ready.wants_frame(true, true);
            r.ready.posted(true, true);
            r.ready.shown(true, true);
            m = r.kids.running();
            settled = r.settle();
        });
        check(n == 0 && wants, "4 a deactivation before the callback spawns nothing and asks for a frame");
        check(m == 1 && settled && r.calls().size() == 1, "4 the next shown frame spawns one");
    }

    {
        Rig r(root, tool);
        bool before = false, armed = true, settled = false;
        size_t n = 0;
        r.run([&] {
            r.ready.posted(true, false);
            before = r.ready.wants_frame(true, true);
            r.ready.posted(true, true);
            armed = r.ready.wants_frame(true, true);
            r.ready.shown(true, true);
            n = r.kids.running();
            settled = r.settle();
        });
        check(before, "5 a frame posted before activation asks for a frame once activated");
        check(!armed, "5 no frame is asked while armed");
        check(n == 1 && settled && r.calls().size() == 1, "5 frame then activation spawns exactly one");
    }

    {
        write_file(root + "/rc", "1\n");
        Rig r(root, tool);
        bool settled = false;
        r.run([&] {
            r.ready.posted(true, true);
            r.ready.shown(true, true);
            settled = r.settle();
        });
        check(settled && r.calls().size() == 1, "6 the first attempt runs");
        const int64_t want[] = {2000, 5000, 10000, 20000};
        bool sched = true, early = true, due = true, cleared = true, again = true, between = true, ran = true;
        for (int i = 0; i < 4; i++) {
            int64_t dl = r.ready.deadline();
            sched = sched && dl == r.clock + want[i];
            r.run([&] {
                r.ready.posted(true, true);
                r.ready.shown(true, true);
                between = between && r.kids.running() == 0;
                r.clock = dl - 1;
                r.ready.tick();
                early = early && r.kids.running() == 0;
                r.clock = dl;
                r.ready.tick();
                due = due && r.kids.running() == 1;
                cleared = cleared && r.ready.deadline() == -1;
                r.ready.tick();
                again = again && r.kids.running() == 1;
                ran = r.settle() && ran;
            });
            ran = ran && r.calls().size() == (size_t)i + 2;
        }
        std::string e = r.err();
        check(sched, "6 deadlines 2000, 5000, 10000, 20000 ms after each failure");
        check(early, "6 a tick 1 ms early spawns nothing");
        check(due && ran, "6 a tick at the deadline spawns one");
        check(cleared && again, "6 no deadline after the due tick, a second tick spawns nothing");
        check(between, "6 frames between retries spawn nothing");
        check(count(e, "failed (exit 1)\n") == 5 && count(e, "retry in 2 s\n") == 1 &&
                  count(e, "retry in 5 s\n") == 1 && count(e, "retry in 10 s\n") == 1 &&
                  count(e, "retry in 20 s\n") == 1,
              "6 failed and retry lines");

        size_t n = 1;
        bool wants = true;
        r.run([&] {
            r.clock += 1000000;
            r.ready.tick();
            r.ready.posted(true, true);
            r.ready.shown(true, true);
            r.ready.posted(true, false);
            wants = r.ready.wants_frame(true, true);
            r.ready.tick();
            n = r.kids.running();
        });
        check(r.calls().size() == 5 && count(e, kLine) == 5, "8 five attempts");
        check(count(e, kGivenUp) == 1 && r.ready.deadline() == -1, "8 given up, no deadline");
        check(n == 0 && !wants && r.calls().size() == 5 && count(r.err(), kLine) == 5,
              "8 later ticks and frames spawn nothing");
        write_file(root + "/rc", "0\n");
    }

    {
        write_file(root + "/delay", "2\n");
        Rig r(root, tool);
        bool one = true;
        size_t first = 0;
        r.run([&] {
            r.ready.posted(true, true);
            r.ready.shown(true, true);
            first = r.kids.running();
            r.clock += 1000000;
            int64_t end = real_ms() + 5000;
            while (r.kids.running() > 0 && real_ms() < end) {
                r.ready.tick();
                one = one && r.kids.running() == 1;
                r.reap();
                usleep(5000);
            }
        });
        check(first == 1 && one && r.kids.running() == 0, "7 one child at a time while an attempt runs");
        check(r.calls().size() == 1, "7 no second spawn");
        write_file(root + "/delay", "0\n");
    }

    {
        write_file(root + "/rc", "1\n");
        Rig r(root, tool);
        r.run([&] {
            r.ready.posted(true, true);
            r.ready.shown(true, true);
            r.settle();
        });
        write_file(root + "/rc", "0\n");
        size_t later = 1;
        r.run([&] {
            r.clock = r.ready.deadline();
            r.ready.tick();
            r.settle();
            r.clock += 1000000;
            r.ready.tick();
            later = r.kids.running();
            r.settle();
        });
        std::string e = r.err();
        check(count(e, kOk) == 1 && r.ready.status() == 0 && r.ready.deadline() == -1, "9 the second attempt ends it");
        check(later == 0 && r.calls().size() == 2 && count(e, kLine) == 2, "9 no third spawn");
    }

    {
        Rig r(root, root + "/missing");
        r.run([&] {
            r.ready.posted(true, true);
            r.ready.shown(true, true);
            r.settle();
        });
        std::string e = r.err();
        int st = r.ready.status();
        check(WIFEXITED(st) && WEXITSTATUS(st) == 127 && count(e, "ready report failed (exit 127)\n") == 1,
              "10 a missing tool fails with exit 127");
        check(count(e, "ready report retry in 2 s\n") == 1 && r.ready.deadline() == r.clock + 2000,
              "10 a missing tool retries in 2 s");
    }

    {
        write_file(root + "/rc", "kill\n");
        Rig r(root, tool);
        r.run([&] {
            r.ready.posted(true, true);
            r.ready.shown(true, true);
            r.settle();
        });
        int st = r.ready.status();
        check(WIFSIGNALED(st) && WTERMSIG(st) == 9 && count(r.err(), "ready report failed (signal 9)\n") == 1,
              "10 a killed attempt fails with signal 9");
        write_file(root + "/rc", "0\n");
    }

    {
        setenv("INVOCATION_ID", "0123456789abcdef0123456789abcdef", 1);
        Rig r(root, tool);
        r.run([&] {
            r.ready.posted(true, true);
            r.ready.shown(true, true);
            r.settle();
        });
        unsetenv("INVOCATION_ID");
        std::vector<std::string> c = r.calls();
        check(c.size() == 1 && c[0] == "ready 0123456789abcdef0123456789abcdef", "11 INVOCATION_ID reaches the tool");
    }

    nftw(root.c_str(), rm_cb, 16, FTW_DEPTH | FTW_PHYS);
    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
