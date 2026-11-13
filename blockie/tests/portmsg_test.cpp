#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <ftw.h>
#include <linux/input.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../../gfx/wayland.h"
#include "../../ui/busydots.h"
#include "../portmsg.h"
#include "../ring_spin.h"

using namespace fd;

namespace {

int g_fail = 0;
int g_total = 0;
std::string g_dir, g_log, g_active;
std::string g_pad;
int g_pad_w = -1;
Fonts* g_fonts = nullptr;

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

void note(const std::string& line)
{
    int fd = open(g_log.c_str(), O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0)
        return;
    std::string l = line + "\n";
    ssize_t w = write(fd, l.data(), l.size());
    (void)w;
    close(fd);
}

std::string slurp(const std::string& path)
{
    std::string out;
    FILE* f = std::fopen(path.c_str(), "rb");
    char buf[4096];
    size_t n;
    while (f && (n = std::fread(buf, 1, sizeof buf, f)) > 0)
        out.append(buf, n);
    if (f)
        std::fclose(f);
    return out;
}

std::vector<pid_t> servers()
{
    std::vector<pid_t> out;
    std::string log = slurp(g_log);
    size_t i = 0;
    while ((i = log.find("open ", i)) != std::string::npos) {
        out.push_back((pid_t)std::atoi(log.c_str() + i + 5));
        i += 5;
    }
    return out;
}

int count(const std::string& what)
{
    std::string log = slurp(g_log);
    int n = 0;
    for (size_t i = 0; (i = log.find(what, i)) != std::string::npos; i += what.size())
        n++;
    return n;
}

class FakeWindow : public MsgWindow {
public:
    ~FakeWindow() override { note("gone " + std::to_string(getpid())); }
    bool open() override
    {
        note("open " + std::to_string(getpid()));
        return true;
    }
    Surface frame() override { return buf_.surface(); }
    bool ready() override { return true; }
    void present(const std::vector<AlphaRegion>& regions) override
    {
        if (!shown_ && regions_visible(regions))
            note("shown " + std::to_string(getpid()));
        shown_ = shown_ || regions_visible(regions);
        uint64_t h = 1469598103934665603ull;
        for (auto v : buf_.pix)
            h = (h ^ (uint64_t)v) * 1099511628211ull;
        if (h != hash_)
            note("frame " + std::to_string(h));
        hash_ = h;
    }
    int fd() const override { return -1; }
    int event_fd() const override { return -1; }
    bool pump() override { return true; }
    bool activated() const override { return access(g_active.c_str(), F_OK) == 0; }

private:
    Buffer buf_;
    bool shown_ = false;
    uint64_t hash_ = 0;
};

void serve(int lfd)
{
    FakeWindow w;
    MsgServerOpts o;
    o.open_pad = [](PadReader& p) {
        int fd = open(g_pad.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            return false;
        p.adopt_fd(fd);
        return true;
    };
    serve_portmsg(lfd, w, *g_fonts, o);
}

bool blocked(pid_t pid)
{
    std::string locks = slurp("/proc/locks");
    std::string id = " " + std::to_string(pid) + " ";
    size_t i = 0;
    while (i < locks.size()) {
        size_t e = locks.find('\n', i);
        std::string line = locks.substr(i, e == std::string::npos ? std::string::npos : e - i);
        if (line.find("-> FLOCK") != std::string::npos && line.find(id) != std::string::npos)
            return true;
        if (e == std::string::npos)
            break;
        i = e + 1;
    }
    return false;
}

bool released() { return access((g_dir + "/release").c_str(), F_OK) == 0; }

void gate()
{
    note("gate " + std::to_string(getpid()));
    for (int t = 0; t < 5000 && !released(); t += 10)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

pid_t call(const std::vector<std::string>& args, const std::string& in, const std::string& out, bool gated = false)
{
    std::string in_path = g_dir + "/in-" + std::to_string(std::rand());
    FILE* f = std::fopen(in_path.c_str(), "wb");
    if (f) {
        std::fwrite(in.data(), 1, in.size(), f);
        std::fclose(f);
    }
    pid_t pid = fork();
    if (pid == 0) {
        int ifd = open(in_path.c_str(), O_RDONLY);
        int ofd = open(out.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        MsgCall c;
        if (ifd < 0 || ofd < 0 || !parse_call(args, c))
            _exit(kMsgUsage);
        _exit(run_portmsg_client(c, g_dir, ifd, ofd, serve, gated ? gate : std::function<void()>()));
    }
    return pid;
}

int wait_rc(pid_t pid, int ms)
{
    for (int t = 0; t < ms; t += 10) {
        int status = 0;
        if (waitpid(pid, &status, WNOHANG) == pid)
            return WIFEXITED(status) ? WEXITSTATUS(status) : -2;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return -1;
}

void reap(pid_t pid)
{
    kill(pid, SIGKILL);
    int status = 0;
    waitpid(pid, &status, 0);
}

bool wait_for(const std::function<bool()>& f, int ms)
{
    for (int t = 0; t < ms; t += 10) {
        if (f())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return f();
}

void pad(uint16_t code, int32_t value)
{
    input_event ev = {};
    ev.type = EV_KEY;
    ev.code = code;
    ev.value = value;
    ssize_t w = write(g_pad_w, &ev, sizeof ev);
    (void)w;
}

void kill_servers()
{
    for (pid_t p : servers())
        kill(p, SIGKILL);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

void fresh(const std::string& root, const std::string& name)
{
    kill_servers();
    g_dir = root + "/" + name;
    mkdir(g_dir.c_str(), 0700);
    g_log = g_dir + "/log";
}

void set_file(const std::string& path)
{
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_CLOEXEC, 0600);
    if (fd >= 0)
        close(fd);
}

void set_active(bool on)
{
    if (on) {
        set_file(g_active);
    } else {
        unlink(g_active.c_str());
    }
}

std::string opts() { return "blockie-test-a\tAlpha\nblockie-test-b\tBeta\n"; }

int rm_cb(const char* path, const struct stat*, int, struct FTW*)
{
    remove(path);
    return 0;
}

}

int main(int argc, char** argv)
{
    std::string fdir = argc > 1 ? argv[1] : "fonts";
    Fonts fonts;
    if (!fonts.open(fdir, {})) {
        std::printf("FAIL fonts not found in %s\n", fdir.c_str());
        return 1;
    }
    g_fonts = &fonts;
    std::signal(SIGPIPE, SIG_IGN);

    {
        MsgFrame t;
        uint32_t len = 0;
        std::string at_cap = msg_frame(MsgFrame::Body, std::string(kMsgFrameCap, 'x'));
        check(msg_head(at_cap, t, len) == MsgHead::Ok && len == kMsgFrameCap, "a frame at the 64 KiB cap is taken");
        uint32_t h[2] = {(uint32_t)MsgFrame::Body, kMsgFrameCap + 1};
        std::string over((const char*)h, sizeof h);
        check(msg_head(over, t, len) == MsgHead::Refused, "a frame declaring more than the cap is refused");
        check(msg_head(over.substr(0, 5), t, len) == MsgHead::More, "a short header waits for more");
        check(msg_cap("ab\xc3\xa9", 3) == "ab", "a cap never cuts inside a code point");
    }
    {
        MsgCall c;
        check(parse_call({"--text", "blockie test", "--title", "T", "--timeout", "5", "--wait"}, c) &&
                  c.kind == CallKind::Text && c.wait && c.timeout_s == 5 && c.title == "T",
              "text with title, timeout and wait parses");
        check(parse_call({"--progress-set", "10", "-"}, c) && c.kind == CallKind::Set && c.done == 10 && c.total == -1,
              "progress-set with an unknown total parses");
        check(parse_call({"--choose", "--title", "Q"}, c) && c.kind == CallKind::Choose, "choose with a title parses");
        check(!parse_call({"--choose"}, c), "choose without a question is a usage error");
        check(!parse_call({"--progress", "--wait"}, c), "wait outside text is a usage error");
        check(!parse_call({"--text", "a", "--close"}, c), "two modes are a usage error");
        check(!parse_call({"--blockie-test"}, c), "an unknown flag is a usage error");
        check(!parse_call({"--progress-set", "x", "1"}, c), "a progress-set count that is not a number is refused");
    }
    {
        check(msg_band(-1, -1, 4) == 1 && msg_band(-1, -1, 3) == 1,
              "a log row stepping up past the top rides band 1, not the picked band");
        check(msg_band(2, -1, 4) == 3 && msg_band(7, -1, 4) == 4, "rows take their band up to the cap");
        check(msg_band(0, 0, 3) == 4 && msg_band(5, 5, 3) == 4, "the picked row leaves one band after the cap");
        MsgView v;
        std::vector<AlphaRegion> regions;
        Buffer b;
        Canvas c(b.surface());
        GlyphCache g;
        InkLayer ink;
        MsgContent p;
        p.mode = MsgMode::Progress;
        bool fits = true;
        for (int k = 0; k < 3; k++) {
            int64_t t = k * 120;
            v.show(p, t);
            for (int i = 0; i < 9; i++)
                v.line("blockie-test fast step " + std::to_string(i), t + i * 10);
            for (int64_t at = t; at < t + 120; at += 16) {
                v.draw(c, *g_fonts, g, ink, theme(), at, regions);
                fits = fits && regions.size() <= (size_t)kWlAlphaMax;
            }
        }
        check(fits, "a fast log step across three layers stays within the region limit");
    }
    {
        MsgCore core;
        std::vector<std::pair<int64_t, int>> got;
        core.answer = [&got](int64_t id, int code, const std::string&) { got.push_back({id, code}); };
        MsgCall pc;
        pc.kind = CallKind::Progress;
        pc.title = "blockie test steps";
        core.call(1, pc, {}, 0);
        core.line(1, "blockie test step", 10);
        MsgCall tc;
        tc.text = "blockie test next";
        core.call(2, tc, {}, 20);
        core.done(1);
        check(got.size() == 2 && got[0] == std::make_pair((int64_t)2, 0) && got[1] == std::make_pair((int64_t)1, 3),
              "a replaced progress answers 3 at its EOF");
        core.call(3, pc, {}, 30);
        core.done(3);
        check(got.size() == 3 && got[2] == std::make_pair((int64_t)3, 0), "a progress that keeps the window answers 0");
        MsgCall cc;
        cc.kind = CallKind::Choose;
        cc.title = "blockie test question";
        core.call(4, cc, {"a", "b"}, 40);
        core.key(PadAction::Back, 800);
        check(got.size() == 3 && core.live(), "Back in a choose sends no answer and keeps the window live");
    }
    {
        MsgView v;
        std::vector<AlphaRegion> regions;
        Buffer b;
        Canvas c(b.surface());
        GlyphCache g;
        InkLayer ink;
        MsgContent ch;
        ch.mode = MsgMode::Choose;
        ch.title = "blockie test question";
        for (int i = 0; i < 8; i++) {
            ch.lines.push_back("blockie test option " + std::to_string(i));
            ch.values.push_back(std::to_string(i));
        }
        v.show(ch, 0);
        for (int i = 0; i < 3; i++)
            v.move(true, 800 + i);
        v.close(v.focus(), 900);
        v.show(ch, 950);
        v.show(ch, 1000);
        bool fits = true, drawn = true;
        for (int64_t at = 900; at < 1900; at += 16) {
            v.draw(c, *g_fonts, g, ink, theme(), at, regions);
            fits = fits && regions.size() <= (size_t)kWlAlphaMax;
            drawn = drawn && regions.size() >= 2;
        }
        check(fits && drawn,
              "a choose swap with a picked row and two leaving layers stays within 16 regions and keeps its blocks");
    }
    {
        GlyphCache g;
        InkLayer ink;
        std::vector<AlphaRegion> regions;
        auto shoot = [&](MsgView& v, Buffer& b, int64_t at) {
            Canvas c(b.surface());
            v.draw(c, *g_fonts, g, ink, theme(), at, regions);
        };
        auto same_rows = [](Buffer& a, Buffer& b, int y0, int y1) {
            for (int y = y0; y < y1; y++)
                for (int x = 0; x < kPortraitW; x++)
                    if (a.surface().at(x, y) != b.surface().at(x, y))
                        return false;
            return true;
        };
        MsgContent one;
        one.mode = MsgMode::Text;
        one.title = "blockie test";
        one.lines = {"blockie test one\nblockie test same"};
        MsgContent two = one;
        two.lines = {"blockie test two\nblockie test same"};
        MsgView v;
        Buffer before, mid;
        v.show(one, 0);
        shoot(v, before, 900);
        v.show(two, 1000);
        shoot(v, mid, 1125);
        check(same_rows(before, mid, 0, 196), "a swap keeps an unchanged title in place");
        check(same_rows(before, mid, 252, 288), "a swap keeps an unchanged row in place");
        {
            MsgContent dots_msg;
            dots_msg.mode = MsgMode::Progress;
            dots_msg.title = "blockie test";
            dots_msg.done = 3;
            dots_msg.total = -1;
            MsgView dots_view;
            Buffer dots_buf;
            dots_view.show(dots_msg, 0);
            shoot(dots_view, dots_buf, 1500);
            uint32_t dots_ground = dots_buf.surface().at(kPortraitW - 2, kPortraitH - 2);
            int dots_inked = 0;
            for (int y = px(kDotsY); y < px(kDotsY) + px(kDotSide); y++)
                for (int x = px(kDotsX0); x < px(kDotsX1); x++)
                    dots_inked += dots_buf.surface().at(x, y) != dots_ground ? 1 : 0;
            check(dots_inked > 0, "a progress with no total inks the busy dots band");
        }

        MsgContent renamed = one;
        renamed.title = "blockie test renamed";
        MsgView t;
        Buffer t0, t1, t2;
        t.show(one, 0);
        shoot(t, t0, 900);
        t.show(renamed, 1000);
        shoot(t, t1, 1125);
        shoot(t, t2, 1900);
        check(!same_rows(t0, t1, 0, 196) && !same_rows(t1, t2, 0, 196) && same_rows(t0, t1, 200, 288),
              "a changed title moves as one row while the unchanged rows stay");

        MsgContent ask;
        ask.mode = MsgMode::Choose;
        ask.title = "blockie test";
        ask.lines = {"blockie test a", "blockie test b"};
        ask.values = {"a", "b"};
        MsgView m;
        Buffer m0;
        m.show(one, 0);
        shoot(m, m0, 900);
        m.show(ask, 1000);
        shoot(m, m0, 1050);
        int in_x = -1, out_x = -1;
        for (size_t i = 1; i < regions.size(); i++) {
            const Rect& r = regions[i].r;
            if (r.y0 == px(tok::contentTop) && r.x0 > 0 && r.x0 < kPortraitW)
                in_x = r.x0;
            if (r.y0 == px(tok::contentTop) && r.x0 == 0 && r.x1 < kPortraitW)
                out_x = r.x1;
        }
        check(in_x > 0 && in_x == out_x, "a swap's new rows arrive as the old leave, both from s 0");

        MsgView a;
        Buffer a0;
        a.show(one, 0);
        bool ground = true;
        for (int64_t at = 16; at < 400; at += 16) {
            shoot(a, a0, at);
            for (const AlphaRegion& r : regions)
                ground = ground && r.a256 == regions[0].a256;
        }
        check(ground, "a band's ground matches the scrim on every frame of the appear");

        MsgView fall;
        Buffer f0;
        fall.show(one, 0);
        shoot(fall, f0, 900);
        int full = regions[0].a256;
        fall.close(-1, 1000);
        shoot(fall, f0, 1240);
        int held = regions[0].a256;
        shoot(fall, f0, 1400);
        int falling = regions[0].a256;
        bool same_ground = true;
        for (const AlphaRegion& r : regions)
            same_ground = same_ground && r.a256 == falling;
        shoot(fall, f0, 1600);
        int gone = regions[0].a256;
        check(held == full && falling > 0 && falling < full && same_ground && gone == 0,
              "close A holds the scrim to 250 ms, then falls to 0 by 583 with the bands on the same ground");

        MsgView early;
        Buffer e0;
        early.show(ask, 0);
        shoot(early, e0, 100);
        int rising = regions[0].a256;
        early.land(100);
        early.close(0, 100);
        shoot(early, e0, 116);
        int landed = regions[0].a256;
        shoot(early, e0, 340);
        int still = regions[0].a256;
        shoot(early, e0, 570);
        int late = regions[0].a256;
        check(rising > 0 && rising < full && landed == rising && still == rising && late == rising,
              "Accept during the appear keeps the scrim at its strength");

        MsgContent abc;
        abc.mode = MsgMode::Choose;
        abc.title = "blockie test";
        abc.lines = {"blockie test a", "blockie test b", "blockie test c"};
        abc.values = {"a", "b", "c"};
        MsgView bv;
        Buffer b0, b1, b2;
        bv.show(abc, 0);
        bv.move(true, 500);
        shoot(bv, b0, 900);
        bv.show(abc, 1000);
        shoot(bv, b1, 1125);
        shoot(bv, b2, 1900);
        check(same_rows(b0, b1, 372, 450) && !same_rows(b1, b2, 200, 278),
              "a choose swap moves the rows whose focus bar changed and keeps the rest");

        MsgContent steps;
        steps.mode = MsgMode::Progress;
        steps.title = "blockie test steps";
        MsgView lv;
        Buffer l1, l2;
        lv.show(steps, 0);
        for (int i = 0; i < 8; i++) {
            steps.lines.push_back("blockie test step " + std::to_string(i));
            lv.line(steps.lines.back(), 10 + i * 10);
        }
        shoot(lv, l1, 900);
        lv.show(steps, 1000);
        shoot(lv, l1, 1125);
        shoot(lv, l2, 1900);
        check(!same_rows(l1, l2, 200, 708), "a swap from a scrolled log moves its rows even where the text matches");

        auto fall_from = [&](const MsgContent& m, int64_t at, bool accept, int64_t held_to) {
            MsgView w;
            Buffer b;
            w.show(m, 0);
            shoot(w, b, at);
            int s0 = regions[0].a256;
            if (accept)
                w.land(at);
            w.close(accept ? 0 : -1, at);
            shoot(w, b, at + held_to);
            int hold = regions[0].a256;
            shoot(w, b, at + 560);
            int tail = regions[0].a256;
            shoot(w, b, at + 590);
            return s0 > 0 && hold == s0 && tail > 0 && tail < s0 && regions[0].a256 == 0;
        };
        check(fall_from(ask, 83, true, 480), "Accept at a low scrim holds it, then the fall ends at 583");
        check(fall_from(ask, 266, true, 300), "Accept at a high scrim holds it, then the fall ends at 583");
        check(fall_from(one, 150, false, 400), "a close during the appear holds the scrim, then the fall ends at 583");

        auto inked = [](Buffer& b, int x0, int y0, int x1, int y1) {
            for (int y = y0; y < y1; y++)
                for (int x = x0; x < x1; x++)
                    if (b.surface().at(x, y) != b.surface().at(0, 0))
                        return true;
            return false;
        };
        MsgContent bare;
        bare.mode = MsgMode::Progress;
        MsgView dv;
        Buffer d0;
        dv.show(bare, 0);
        bool slot_clear = true;
        for (int64_t at = 700; at < 700 + kBusyLoopMs; at += 16) {
            shoot(dv, d0, at);
            slot_clear = slot_clear && !inked(d0, 888, 123, 928, 163);
        }
        check(slot_clear, "a progress with no total draws no ink in the ring slot");

        MsgView rv;
        Buffer r0;
        rv.show(bare, 0);
        rv.progress(10, -1, 1000);
        rv.progress(50, 100, 1100);
        shoot(rv, r0, 1150);
        check(inked(r0, 888, 123, 928, 163), "the first ring frame after the dots draws at once");
    }
    {
        MsgView v;
        MsgContent p;
        p.mode = MsgMode::Progress;
        v.show(p, 0);
        for (int i = 0; i < 250; i++)
            v.line("blockie-test line " + std::to_string(i), i);
        check(v.content().lines.size() == kMsgLogLines && v.content().lines.front() == "blockie-test line 50",
              "the progress log keeps the last 200 lines");
        MsgContent ch;
        ch.mode = MsgMode::Choose;
        for (int i = 0; i < 30; i++) {
            ch.lines.push_back("option " + std::to_string(i));
            ch.values.push_back(std::to_string(i));
        }
        v.show(ch, 1000);
        check(!v.move(false, 1000) && v.focus() == 0, "up at the first option stops");
        for (int i = 0; i < 7; i++)
            v.move(true, 1000 + i);
        check(v.focus() == 7 && v.top() == 2, "the window past 6 options moves the least to keep the focus shown");
    }

    char tmpl[] = "/tmp/blockie-test-portmsg.XXXXXX";
    char* root_c = mkdtemp(tmpl);
    if (!root_c) {
        std::printf("FAIL mkdtemp\n");
        return 1;
    }
    std::string root = root_c;
    g_active = root + "/active";
    g_pad = root + "/pad";
    if (mkfifo(g_pad.c_str(), 0600) != 0 || (g_pad_w = open(g_pad.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC)) < 0) {
        std::printf("FAIL pad fifo\n");
        return 1;
    }
    set_active(true);

    const char* const modes[][2] = {{"--text", "blockie test message"}, {"--progress", ""}, {"--choose", ""}};
    for (int m = 0; m < 3; m++) {
        fresh(root, "mode" + std::to_string(m));
        std::vector<std::string> args = {modes[m][0]};
        if (*modes[m][1])
            args.push_back(modes[m][1]);
        if (m > 0) {
            args.push_back("--title");
            args.push_back("blockie test title");
        }
        pid_t c = call(args, m == 1 ? "blockie test step\n" : opts(), g_dir + "/out");
        bool shown = wait_for([] { return count("shown ") == 1; }, 3000);
        check(shown && count("open ") == 1, m == 0   ? "a text call opens one window"
                                            : m == 1 ? "a progress call opens one window"
                                                     : "a choose call opens one window");
        reap(c);
    }

    fresh(root, "two");
    pid_t a = call({"--text", "blockie test one"}, "", g_dir + "/out-a");
    pid_t b = call({"--text", "blockie test two"}, "", g_dir + "/out-b");
    int ra = wait_rc(a, 3000), rb = wait_rc(b, 3000);
    check(ra == 0 && rb == 0 && count("open ") == 1, "two calls at once give one window");

    fresh(root, "race");
    pid_t ga = call({"--text", "blockie test first"}, "", g_dir + "/out-a", true);
    bool in_gate = wait_for([] { return count("gate ") == 1; }, 3000);
    pid_t gb = call({"--text", "blockie test second"}, "", g_dir + "/out-b", true);
    bool raced = wait_for([gb] { return count("gate ") == 2 || blocked(gb); }, 5000);
    set_file(g_dir + "/release");
    int rga = wait_rc(ga, 5000), rgb = wait_rc(gb, 5000);
    check(in_gate && raced && rga == 0 && rgb == 0 && count("gate ") == 1 && count("open ") == 1,
          "a second call made while the first starts the server waits on the lock and joins its window");

    std::vector<pid_t> first = servers();
    for (pid_t p : first)
        kill(p, SIGKILL);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    int rc = wait_rc(call({"--text", "blockie test after"}, "", g_dir + "/out-c"), 3000);
    std::vector<pid_t> now = servers();
    check(rc == 0 && first.size() == 1 && now.size() == 2 && now[1] != first[0],
          "a call after a killed server starts a fresh one");

    fresh(root, "replace");
    pid_t ch = call({"--choose", "--title", "blockie test question"}, opts(), g_dir + "/out-ch");
    wait_for([] { return count("shown ") == 1; }, 3000);
    int rt = wait_rc(call({"--text", "blockie test next"}, "", g_dir + "/out-t"), 3000);
    check(rt == 0 && wait_rc(ch, 3000) == kMsgCancel, "a choose replaced by a text call exits 3");

    pid_t w = call({"--text", "blockie test wait", "--wait"}, "", g_dir + "/out-w");
    bool waiting = wait_rc(w, 300) == -1;
    wait_rc(call({"--text", "blockie test after wait"}, "", g_dir + "/out-x"), 3000);
    check(waiting && wait_rc(w, 3000) == kMsgCancel, "a waiting message replaced by the next exits 3");

    fresh(root, "killed");
    ch = call({"--choose", "--title", "blockie test question"}, opts(), g_dir + "/out-ch");
    wait_for([] { return count("shown ") == 1; }, 3000);
    kill_servers();
    check(wait_rc(ch, 3000) == kMsgCancel, "a choose whose server is killed exits 3");

    fresh(root, "inactive");
    set_active(false);
    ch = call({"--choose", "--title", "blockie test question"}, opts(), g_dir + "/out-ch");
    wait_for([] { return count("shown ") == 1; }, 3000);
    pad(BTN_DPAD_DOWN, 1);
    pad(BTN_DPAD_DOWN, 0);
    pad(BTN_EAST, 1);
    pad(BTN_EAST, 0);
    bool ignored = wait_rc(ch, 300) == -1;
    set_active(true);
    pad(BTN_EAST, 1);
    pad(BTN_EAST, 0);
    rc = wait_rc(ch, 3000);
    check(ignored && rc == 0 && slurp(g_dir + "/out-ch") == "blockie-test-a\n",
          "choose ignores the pad while not activated and answers a press once activated");
    ch = call({"--choose", "--title", "blockie test second"}, opts(), g_dir + "/out-ch2");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    pad(BTN_DPAD_DOWN, 1);
    pad(BTN_DPAD_DOWN, 0);
    pad(BTN_EAST, 1);
    pad(BTN_EAST, 0);
    rc = wait_rc(ch, 3000);
    check(rc == 0 && slurp(g_dir + "/out-ch2") == "blockie-test-b\n", "choose answers the option the pad moves to");

    fresh(root, "held");
    pad(BTN_EAST, 1);
    ch = call({"--choose", "--title", "blockie test question"}, opts(), g_dir + "/out-ch");
    wait_for([] { return count("shown ") == 1; }, 3000);
    bool held = wait_rc(ch, 300) == -1;
    pad(BTN_EAST, 0);
    bool released = wait_rc(ch, 200) == -1;
    pad(BTN_EAST, 1);
    pad(BTN_EAST, 0);
    rc = wait_rc(ch, 3000);
    check(held && released && rc == 0 && slurp(g_dir + "/out-ch") == "blockie-test-a\n",
          "a choose opened with a key held answers only a fresh press");

    fresh(root, "quiet");
    int rq = wait_rc(call({"--close"}, "", g_dir + "/out-q"), 3000);
    int rs = wait_rc(call({"--progress-set", "1", "2"}, "", g_dir + "/out-s"), 3000);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    bool no_sock = access((g_dir + kMsgSock).c_str(), F_OK) != 0;
    check(rq == 0 && count("open ") == 0 && no_sock, "a close with no server opens no window and exits 0");
    check(rs == 0 && count("open ") == 0, "a progress-set with no server opens no window and exits 0");

    fresh(root, "ends");
    rc = wait_rc(call({"--text", "blockie test message"}, "", g_dir + "/out"), 3000);
    bool up = wait_for([] { return count("shown ") == 1; }, 3000);
    int rclose = wait_rc(call({"--close"}, "", g_dir + "/out-c"), 3000);
    auto gone = [](int n) {
        return [n] { return count("gone ") == n && access((g_dir + kMsgSock).c_str(), F_OK) != 0; };
    };
    bool ended = wait_for(gone(1), 10000);
    int rnext = wait_rc(call({"--text", "blockie test again"}, "", g_dir + "/out-n"), 3000);
    std::vector<pid_t> two = servers();
    check(rc == 0 && up && rclose == 0 && ended,
          "after a close the server plays its close, drops its window and ends");
    check(rnext == 0 && two.size() == 2 && two[0] != two[1], "the next text after a close opens a new window");
    rc = wait_rc(call({"--text", "blockie test timeout", "--timeout", "1"}, "", g_dir + "/out-t"), 3000);
    check(rc == 0 && wait_for(gone(2), 10000), "after a timeout close the server ends as well");

    fresh(root, "empty");
    rc = wait_rc(call({"--choose", "--title", "blockie test question"}, "", g_dir + "/out"), 3000);
    check(rc == kMsgUsage && count("open ") == 0, "a choose with no options is a usage error and opens no window");

    fresh(root, "waited");
    auto t0 = std::chrono::steady_clock::now();
    rc = wait_rc(call({"--text", "blockie test", "--wait", "--timeout", "1"}, "", g_dir + "/out"), 4000);
    auto took = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    check(rc == 0 && took >= 900 && took < 4000, "a waiting text with a timeout returns 0 when the time runs out");
    check(wait_for(gone(1), 10000), "the server ends after the waiting text times out");

    fresh(root, "early");
    t0 = std::chrono::steady_clock::now();
    rc = wait_rc(call({"--text", "blockie test", "--timeout", "5"}, "", g_dir + "/out"), 3000);
    took = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    check(rc == 0 && took < 3000, "a text with a timeout and no wait returns at once");
    pid_t wc = call({"--text", "blockie test wait", "--wait", "--timeout", "30"}, "", g_dir + "/out-w");
    bool still = wait_rc(wc, 300) == -1;
    wait_rc(call({"--close"}, "", g_dir + "/out-c"), 3000);
    check(still && wait_rc(wc, 3000) == kMsgCancel, "a waiting text closed before its timeout exits 3");

    fresh(root, "dots");
    pid_t sp = call({"--progress", "--title", "blockie test steps"}, "blockie test step\n", g_dir + "/out");
    wait_for([] { return count("shown ") == 1; }, 3000);
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    int f1 = count("frame ");
    std::this_thread::sleep_for(std::chrono::milliseconds(3200));
    int f2 = count("frame ");
    check(f2 - f1 >= 10, "the busy dots with no total keep moving after the window lands");
    reap(sp);

    fresh(root, "frame");
    rc = wait_rc(call({"--text", "blockie test"}, "", g_dir + "/out"), 3000);
    int s = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    std::string sock = g_dir + kMsgSock;
    std::memcpy(addr.sun_path, sock.c_str(), sock.size() + 1);
    bool closed = false;
    if (s >= 0 && connect(s, (const sockaddr*)&addr, sizeof addr) == 0) {
        uint32_t h[2] = {(uint32_t)MsgFrame::Open, kMsgFrameCap + 1};
        ssize_t sent = send(s, h, sizeof h, MSG_NOSIGNAL);
        timeval tv = {3, 0};
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        char byte;
        closed = sent == (ssize_t)sizeof h && recv(s, &byte, 1, 0) == 0;
    }
    if (s >= 0)
        close(s);
    int after = wait_rc(call({"--text", "blockie test after"}, "", g_dir + "/out-2"), 3000);
    check(rc == 0 && closed && after == 0 && count("open ") == 1,
          "an oversized frame is refused unread and the server keeps serving");

    kill_servers();
    close(g_pad_w);
    nftw(root.c_str(), rm_cb, 16, FTW_DEPTH | FTW_PHYS);
    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
