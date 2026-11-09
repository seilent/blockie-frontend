#include "pm_run.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <utility>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/signalfd.h>
#include <sys/wait.h>
#include <unistd.h>

#include "ink.h"
#include "powermenu.h"
#include "repeat.h"
#include "theme.h"

namespace fd {

namespace {

constexpr int kFrameMs = 16;

int64_t now_ms()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

bool say(int fd, const std::string& line)
{
    std::string s = line + "\n";
    size_t off = 0;
    while (off < s.size()) {
        ssize_t w = write(fd, s.data() + off, s.size() - off);
        if (w < 0 && errno == EINTR)
            continue;
        if (w <= 0) {
            std::fprintf(stderr, "blockie-powermenu: cannot write %s: %s\n", line.c_str(), std::strerror(errno));
            return false;
        }
        off += (size_t)w;
    }
    std::fprintf(stderr, "blockie-powermenu: %s\n", line.c_str());
    return true;
}

struct Probe {
    Spawned s;
    std::string buf;
    bool ok = false;
    UnitVerdict verdict;
};

void stop_probe(Probe& p)
{
    if (p.s.fd >= 0)
        close(p.s.fd);
    if (p.s.pid > 0) {
        kill(p.s.pid, SIGTERM);
        int status = 0;
        reap(p.s.pid, status);
    }
    p = Probe{};
}

bool drain_probe(Probe& p)
{
    char buf[512];
    for (;;) {
        ssize_t r = read(p.s.fd, buf, sizeof buf);
        if (r > 0) {
            p.buf.append(buf, (size_t)r);
            continue;
        }
        if (r < 0 && errno == EINTR)
            continue;
        if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return false;
        break;
    }
    close(p.s.fd);
    p.s.fd = -1;
    int status = 0;
    p.ok = reap(p.s.pid, status) && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    p.s.pid = -1;
    return true;
}

std::vector<std::string> with(std::vector<std::string> argv, std::initializer_list<std::string> more)
{
    argv.insert(argv.end(), more.begin(), more.end());
    return argv;
}

}

bool reap(pid_t pid, int& status)
{
    for (int i = 0; i < 20; i++) {
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid)
            return true;
        if (r < 0 && errno == ECHILD)
            return false;
        usleep(5000);
    }
    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
    return false;
}

Spawned spawn_capture(const std::vector<std::string>& argv)
{
    if (argv.empty())
        return {};
    int pp[2];
    if (pipe2(pp, O_CLOEXEC) != 0)
        return {};
    pid_t pid = fork();
    if (pid == 0) {
        int nul = open("/dev/null", O_RDONLY);
        if (nul >= 0) {
            dup2(nul, 0);
            if (nul != 0)
                close(nul);
        }
        dup2(pp[1], 1);
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, nullptr);
        signal(SIGPIPE, SIG_DFL);
        std::vector<char*> a;
        for (const std::string& s : argv)
            a.push_back(const_cast<char*>(s.c_str()));
        a.push_back(nullptr);
        execvp(a[0], a.data());
        _exit(127);
    }
    close(pp[1]);
    if (pid < 0) {
        close(pp[0]);
        return {};
    }
    fcntl(pp[0], F_SETFL, O_NONBLOCK);
    return {pid, pp[0]};
}

int run_powermenu(LayerPresenter& p, Fonts& fonts, MenuGrab& grab, const PmRunOpts& o)
{
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    sigprocmask(SIG_BLOCK, &mask, nullptr);
    int sfd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_DFL);
    fcntl(o.out, F_SETFD, fcntl(o.out, F_GETFD) | FD_CLOEXEC);

    LineFifo fifo;
    if (!o.fifo.empty() && !fifo.open(o.fifo))
        std::fprintf(stderr, "blockie-powermenu: cannot open %s: %s\n", o.fifo.c_str(), std::strerror(errno));

    bool alive = true, spoke = false;
    int rc = 0;
    auto result = [&](const std::string& line) {
        if (spoke) {
            std::fprintf(stderr, "blockie-powermenu: %s after a result, not sent\n", line.c_str());
            return;
        }
        spoke = true;
        if (line.rfind("failed ", 0) == 0)
            rc = 1;
        alive = say(o.out, line) && alive;
    };

    PowerMenu m;
    m.reread = [&o]() { return read_gates(o.gates); };
    bool live = true;
    if (!p.open()) {
        result("failed wayland");
        alive = false;
        live = false;
    }
    int64_t now = now_ms();
    if (live) {
        m.open(read_gates(o.gates), o.dark, now);
        if (o.idle_ms > 0)
            m.set_idle_ms(o.idle_ms);
        if (o.toast_in_ms > 0 || o.toast_out_ms > 0)
            m.set_toast_ms(o.toast_in_ms > 0 ? o.toast_in_ms : fx_round(tok::motion_toastIn),
                           o.toast_out_ms > 0 ? o.toast_out_ms : fx_round(tok::motion_toastOut));
    }

    Repeat repeat;
    InkLayer ink;
    std::vector<AlphaRegion> regions;
    std::deque<std::pair<uint32_t, bool>> sent;
    Probe probe;
    int64_t probe_at = -1;
    bool shown = false, seen = false, dirty = true, committed = false, noted = false, framed = false;
    uint32_t close_seq = 0;
    bool fresh = true;
    std::vector<PadEdge> edges;
    std::vector<std::string> words;
    std::vector<PresentNote> notes;
    std::vector<pollfd> fds;

    auto act = [&](PadAction a, bool rep, int64_t t) {
        m.action(a, rep, t);
        dirty = true;
    };
    auto frame = [&](int64_t t) {
        Canvas c(p.frame());
        fonts.begin_frame(-1);
        bool ok = draw_powermenu(c, fonts, ink, m.view(t), theme(), regions);
        uint32_t seq = p.present(regions);
        bool visible = regions_visible(regions);
        sent.push_back({seq, visible});
        dirty = !ok;
        if (m.done() && !committed) {
            committed = true;
            close_seq = seq;
        }
        if (visible && m.surface() == PmSurface::Toast)
            framed = true;
    };
    auto line_due = [&](int64_t t) {
        std::string ln;
        int64_t at = 0;
        bool closing = false;
        if (!m.peek_line(ln, at, closing))
            return;
        if (closing ? !noted && t < at + PowerMenu::kCloseMs : !framed && t < at + PowerMenu::kBootMs)
            return;
        m.take_line(ln);
        result(ln);
    };
    auto closed = [&]() {
        std::string ln;
        int64_t at = 0;
        bool closing = false;
        return m.done() && !m.peek_line(ln, at, closing);
    };

    while (alive) {
        if (!fresh)
            now = now_ms();
        fresh = false;
        PadAction ra = PadAction::Up;
        if (repeat.due(now, ra))
            act(ra, true, now);
        m.tick(now);
        if (!grab.tick(now)) {
            if (!spoke)
                m.abort("failed grab", now);
            else
                std::fprintf(stderr, "blockie-powermenu: a pad stayed busy after a result\n");
        }
        line_due(now);
        if (!alive || closed())
            break;

        if (m.probing() && spoke && probe.s.pid < 0 && now >= probe_at) {
            probe = Probe{};
            probe.s = spawn_capture(with(o.systemctl, {"--user", "show", "-p", "LoadState", "-p", "ActiveState", "-p",
                                                       "Result", "-p", "StateChangeTimestampMonotonic",
                                                       "blockie-reboot@" + m.target() + ".service"}));
            probe_at = now + PowerMenu::kProbeMs;
        }

        if ((dirty || m.animating(now) || (m.done() && !committed)) && p.ready())
            frame(now);
        line_due(now);
        if (!alive || closed())
            break;

        int64_t wake = -1;
        auto take = [&](int64_t t) {
            if (t >= 0 && (wake < 0 || t < wake))
                wake = t;
        };
        take(m.deadline(now));
        take(grab.deadline());
        take(repeat.deadline());
        if (m.probing() && spoke && probe.s.pid < 0)
            take(probe_at);
        {
            std::string ln;
            int64_t at = 0;
            bool closing = false;
            if (m.peek_line(ln, at, closing))
                take(at + (closing ? PowerMenu::kCloseMs : PowerMenu::kBootMs));
        }
        if (m.animating(now) || dirty || (m.done() && !committed))
            take(now + kFrameMs);
        int timeout = wake < 0 ? -1 : (int)std::max<int64_t>(0, wake - now_ms());

        fds.clear();
        fds.push_back({sfd, POLLIN, 0});
        fds.push_back({p.fd(), POLLIN, 0});
        fds.push_back({fifo.fd(), POLLIN, 0});
        fds.push_back({probe.s.fd, POLLIN, 0});
        std::vector<int> gfds;
        grab.fds(gfds);
        for (int g : gfds)
            fds.push_back({g, POLLIN, 0});
        if (poll(fds.data(), fds.size(), timeout) < 0 && errno != EINTR)
            break;
        now = now_ms();

        if (fds[0].revents & POLLIN) {
            signalfd_siginfo si;
            if (read(sfd, &si, sizeof si) == (ssize_t)sizeof si) {
                std::fprintf(stderr, "blockie-powermenu: signal %u\n", si.ssi_signo);
                m.term(now);
                dirty = true;
            }
        }

        bool acts = seen;
        notes.clear();
        if (!p.pump(notes)) {
            live = false;
            if (!spoke)
                result("failed wayland");
            else
                std::fprintf(stderr, "blockie-powermenu: the surface went after a result\n");
            break;
        }
        for (const PresentNote& n : notes) {
            bool visible = false;
            while (!sent.empty() && sent.front().first <= n.seq) {
                if (sent.front().first == n.seq)
                    visible = sent.front().second;
                sent.pop_front();
            }
            if (committed && n.seq >= close_seq)
                noted = true;
            seen = seen || (n.presented && visible);
            if (n.presented && visible && !shown && !spoke) {
                shown = true;
                if (!say(o.out, "shown"))
                    alive = false;
            }
        }

        words.clear();
        fifo.read(words);
        for (const std::string& w : words) {
            PadAction a = PadAction::Up;
            if (fifo_action(w, a))
                act(a, false, now);
            else
                std::fprintf(stderr, "blockie-powermenu: input %s ignored\n", w.c_str());
        }

        edges.clear();
        if (grab.read(edges))
            repeat.release_all();
        if (!acts)
            edges.clear();
        for (const PadEdge& e : edges) {
            bool vertical = e.action == PadAction::Up || e.action == PadAction::Down;
            if (e.down)
                act(e.action, false, now);
            if (vertical && e.down)
                repeat.press(e.action, kFromPad, now);
            else if (vertical)
                repeat.release(e.action, kFromPad, now);
        }

        if (probe.s.fd >= 0 && drain_probe(probe)) {
            if (!probe.verdict.failed) {
                UnitProbe u = parse_unit_show(probe.buf);
                u.read = u.read && probe.ok;
                UnitVerdict v = m.check(u);
                probe = Probe{};
                if (v.failed) {
                    probe.verdict = v;
                    probe.s = spawn_capture(with(o.systemctl, {"is-system-running"}));
                    if (probe.s.pid < 0)
                        probe = Probe{};
                }
            } else {
                if (probe.buf.rfind("stopping", 0) == 0) {
                    std::fprintf(stderr, "blockie-powermenu: the unit is %s while stopping, ignored\n",
                                 probe.verdict.why.c_str());
                } else {
                    std::fprintf(stderr, "blockie-powermenu: the unit is %s\n", probe.verdict.why.c_str());
                    m.fail(probe.verdict.reason, now);
                }
                probe = Probe{};
            }
            dirty = true;
        }
    }

    stop_probe(probe);
    now = now_ms();
    int64_t until = now + MenuGrab::kReleaseMs;
    grab.begin_release(now);
    for (;;) {
        if (live && !committed && p.ready()) {
            regions.clear();
            p.present(regions);
            committed = true;
        }
        if (live) {
            notes.clear();
            live = p.pump(notes);
        }
        bool held = grab.releasing(now);
        if ((!held && (committed || !live)) || now >= until)
            break;
        std::vector<int> gfds;
        grab.fds(gfds);
        fds.clear();
        for (int g : gfds)
            fds.push_back({g, POLLIN, 0});
        if (live)
            fds.push_back({p.fd(), POLLIN, 0});
        int64_t left = until - now;
        if (live && !committed)
            left = std::min<int64_t>(left, kFrameMs);
        poll(fds.data(), fds.size(), (int)std::max<int64_t>(0, left));
        now = now_ms();
    }
    if (sfd >= 0)
        close(sfd);
    return rc;
}

}
