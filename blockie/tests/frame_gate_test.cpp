#include <cstdio>
#include <string>
#include <vector>

#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include "../frame_gate.h"

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

bool same(const char* a, const char* b) { return a && b && std::string(a) == b; }

GateIn idle_in(int64_t now)
{
    GateIn in;
    in.now = now;
    in.ready = true;
    in.claim = true;
    in.activated = true;
    return in;
}

GateIn out_in(int64_t now)
{
    GateIn in = idle_in(now);
    in.ready = false;
    in.claim = false;
    return in;
}

struct Touch {
    int id;
};

struct FakePump {
    std::vector<Touch> queue;
    int pumps = 0;
    bool pump()
    {
        pumps++;
        queue.push_back({pumps});
        queue.push_back({pumps + 100});
        return true;
    }
    bool take_touch(std::vector<Touch>& out)
    {
        out.clear();
        out.swap(queue);
        return !out.empty();
    }
};

struct FakeFill {
    std::string calls;
    int waits = 0;
    int ready_after = -1;
    bool lost_now = false;
    bool posted = false;
    bool ready() const { return ready_after >= 0 && waits >= ready_after; }
    bool lost() const { return lost_now; }
    void flip()
    {
        calls += 'F';
        posted = ready();
    }
    void flush() { calls += 's'; }
};

}

int main()
{
    {
        FrameGate g;
        g.posted(true, true, 0);
        GateStep a = g.step(out_in(16));
        GateStep b = g.step(idle_in(17));
        GateStep c = g.step(idle_in(18));
        check(!a.report_done && b.report_done && !c.report_done,
              "G1 a posted motion frame arms and the next ready() reports done once");
    }
    {
        FrameGate g;
        g.posted(true, true, 0);
        GateIn in = out_in(16);
        in.ready = true;
        in.animating = true;
        GateStep a = g.step(in);
        GateStep b = g.step(in);
        check(a.report_done && !a.draw && !b.report_done,
              "G1 ready() with claim() false (no buffer free) still reports the armed frame done once, and draws "
              "nothing");
    }
    {
        FrameGate g;
        g.posted(false, true, 0);
        GateStep a = g.step(out_in(200));
        check(!a.report_done && !a.land_reason && g.timeout(out_in(200)) == -1,
              "G2 a flip that did not post arms nothing and starts no watchdog");
    }
    {
        FrameGate g;
        g.posted(true, true, 0);
        GateIn in = out_in(50);
        in.animating = true;
        int t50 = g.timeout(in);
        in.now = 100;
        GateStep at100 = g.step(in);
        in.now = 101;
        GateStep at101 = g.step(in);
        in.now = 300;
        in.need_redraw = true;
        GateStep later = g.step(in);
        check(t50 == 51 && !at100.land_reason && same(at101.land_reason, "watchdog") && at101.redraw && !at101.draw &&
                  !later.land_reason && !later.draw,
              "G3 no ready() for more than 100 ms: one watchdog landing, a redraw asked, no frame while claim() is "
              "false");
    }
    {
        FrameGate g;
        g.posted(true, true, 0);
        GateIn in = out_in(10);
        in.animating = true;
        in.child = true;
        GateStep start = g.step(in);
        in.animating = false;
        in.need_redraw = true;
        int lands = start.land_reason ? 1 : 0, draws = 0;
        for (int64_t t = 20; t < 1000; t += 10) {
            in.now = t;
            GateStep s = g.step(in);
            lands += s.land_reason ? 1 : 0;
            draws += s.draw ? 1 : 0;
        }
        in.child = false;
        in.now = 1010;
        GateStep exit_out = g.step(in);
        in.now = 1020;
        in.ready = true;
        in.claim = true;
        GateStep back = g.step(in);
        g.posted(true, false, 1020);
        in.need_redraw = false;
        in.ready = false;
        in.claim = false;
        in.now = 1030;
        GateStep after = g.step(in);
        in.animating = true;
        in.ready = true;
        in.claim = true;
        int done = 0;
        for (int k = 0; k < 5; k++) {
            in.now = 1040 + k * 16;
            GateStep s = g.step(in);
            done += s.report_done ? 1 : 0;
            if (s.draw)
                g.posted(true, true, in.now);
        }
        check(same(start.land_reason, "child") && lands == 1 && draws == 0 && !exit_out.draw && back.draw &&
                  !after.draw && done == 4,
              "G4 a child while armed: one child landing, no frame until it exits and ready() comes, then one frame, "
              "and a fold counts its frames");
    }
    {
        FrameGate g;
        g.posted(true, true, 0);
        GateIn in = out_in(10);
        in.animating = true;
        in.activated = false;
        GateStep hide = g.step(in);
        in.animating = false;
        in.activated = true;
        in.now = 210;
        GateStep later = g.step(in);
        check(same(hide.land_reason, "hidden") && !hide.draw && !later.land_reason && !later.redraw &&
                  g.timeout(in) == -1,
              "G4 hidden while armed: one hidden landing, then no watchdog at +200");
    }
    {
        check(gate_runs(false, false) && !gate_runs(true, false) && !gate_runs(false, true),
              "G5 the loop runs while ready() is false and ends on lost() or closed() only");
    }
    {
        FakeFill p;
        p.ready_after = 2;
        int64_t pclock = 0;
        fill_when_ready(
            p, [&]() { return pclock; },
            [&](int ms) {
                p.calls += 'w';
                p.waits++;
                pclock += ms < 10 ? ms : 10;
                return true;
            },
            [&]() { p.calls += 'f'; });
        FakeFill q;
        int64_t qclock = 0;
        fill_when_ready(
            q, [&]() { return qclock; },
            [&](int ms) {
                q.calls += 'w';
                q.waits++;
                qclock += ms < 30 ? ms : 30;
                return true;
            },
            [&]() { q.calls += 'f'; });
        FakeFill r;
        r.lost_now = true;
        int64_t rclock = 0;
        fill_when_ready(
            r, [&]() { return rclock; },
            [&](int) {
                r.calls += 'w';
                return true;
            },
            [&]() { r.calls += 'f'; });
        check(p.calls == "wwfFs" && p.posted, "G6 the fill waits for ready(), then flips, posts and flushes");
        check(q.calls == "wwwwfFs" && !q.posted && qclock == kFillWaitMs,
              "G6 with no ready() the fill waits 100 ms, then flips with the fill owed and flushes");
        check(r.calls == "fFs", "G6 a lost connection fills, flips and flushes without waiting");
    }
    {
        FakePump p;
        std::vector<Touch> scratch;
        bool empty = true, ok = true;
        for (int k = 0; k < 5; k++) {
            ok = pump_presenter(p, scratch) && ok;
            empty = empty && p.queue.empty();
        }
        check(ok && empty && p.pumps == 5, "G7 every pump takes and discards the touch queue");
    }
    {
        FrameGate g;
        g.posted(true, false, 0);
        GateIn in = out_in(5);
        in.need_redraw = true;
        GateStep a = g.step(in);
        int ta = g.timeout(in);
        in.timeout = 30;
        int tb = g.timeout(in);
        in.ready = true;
        in.claim = true;
        GateStep b = g.step(in);
        int tc = g.timeout(in);
        check(!a.draw && ta == -1 && tb == 30 && b.draw && tc == 0,
              "G8 a callback out with a redraw owed: no draw, the timeout from the deadlines and never 0; after "
              "ready() a draw and timeout 0");
    }
    {
        FrameGate g;
        g.posted(true, false, 0);
        GateIn in = out_in(5);
        in.need_redraw = true;
        int draws = 0;
        for (int64_t t = 5; t < 50; t += 5) {
            in.now = t;
            draws += g.step(in).draw ? 1 : 0;
        }
        in.now = 50;
        in.ready = true;
        in.claim = true;
        GateStep s = g.step(in);
        draws += s.draw ? 1 : 0;
        if (s.draw)
            g.posted(true, false, 50);
        in.need_redraw = false;
        in.ready = false;
        in.claim = false;
        for (int64_t t = 55; t < 100; t += 5) {
            in.now = t;
            draws += g.step(in).draw ? 1 : 0;
        }
        check(draws == 1, "G9 a key edge while a callback is out draws exactly once on the next ready()");
    }
    {
        int efd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        uint64_t one = 1;
        bool wrote = efd >= 0;
        for (int k = 0; k < 3 && wrote; k++)
            wrote = write(efd, &one, sizeof one) == (ssize_t)sizeof one;
        bool drained = wrote && drain_event(efd);
        struct pollfd pf;
        pf.fd = efd;
        pf.events = POLLIN;
        pf.revents = 0;
        bool quiet = poll(&pf, 1, 0) == 0;
        bool again = drain_event(efd);
        if (efd >= 0)
            close(efd);
        check(drained && quiet && !again, "G10 an eventfd signalled three times is drained by one read");
    }
    {
        GateFds a = gate_fds(7, 9, false);
        GateFds b = gate_fds(7, 9, true);
        check(a.display.fd == 7 && a.display.events == POLLIN && b.display.events == (POLLIN | POLLOUT) &&
                  a.event.fd == 9 && a.event.events == POLLIN && b.event.events == POLLIN,
              "G11 the display entry asks POLLOUT only when blocked, the event entry POLLIN");
        check(gate_pump(POLLIN) && gate_pump(POLLHUP) && gate_pump(POLLERR) && !gate_pump(POLLOUT) && !gate_pump(0),
              "G11 pump on POLLIN, POLLHUP or POLLERR");
    }
    {
        FrameGate g;
        g.posted(true, false, 0);
        GateStep s = g.step(out_in(500));
        check(!s.land_reason && !s.redraw && g.timeout(out_in(500)) == -1, "G12 an idle post arms no watchdog");
    }

    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
