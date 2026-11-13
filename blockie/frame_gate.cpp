#include "frame_gate.h"

#include <algorithm>
#include <poll.h>
#include <unistd.h>

namespace fd {

static bool wants(const GateIn& in) { return !in.child && in.claim && (in.need_redraw || in.animating); }

bool FrameGate::want(const GateIn& in) const { return wants(in) || (tail_ && !in.child && in.claim); }

GateStep FrameGate::step(const GateIn& in)
{
    GateStep s;
    if (armed_ && in.ready) {
        armed_ = false;
        s.report_done = true;
    }
    if (!in.activated || in.child) {
        armed_ = false;
        if (in.animating)
            s.land_reason = in.child ? "child" : "hidden";
    } else if (armed_ && in.now - at_ > kFrameWatchdogMs) {
        armed_ = false;
        s.land_reason = "watchdog";
        s.redraw = true;
    }
    s.draw = want(in);
    s.tail = s.draw && !wants(in);
    tails_ += s.tail;
    return s;
}

void FrameGate::posted(bool posted, bool motion, int64_t now)
{
    if (posted)
        tail_ = motion;
    if (!posted || !motion)
        return;
    armed_ = true;
    at_ = now;
}

int FrameGate::timeout(const GateIn& in) const
{
    int t = in.timeout;
    if (armed_) {
        int ms = (int)std::max<int64_t>(0, at_ + kFrameWatchdogMs + 1 - in.now);
        t = t < 0 ? ms : std::min(t, ms);
    }
    if (want(in))
        t = 0;
    return t;
}

GateFds gate_fds(int display_fd, int event_fd, bool blocked)
{
    return {{display_fd, (short)(POLLIN | (blocked ? POLLOUT : 0))}, {event_fd, POLLIN}};
}

bool gate_pump(short revents) { return revents & (POLLIN | POLLHUP | POLLERR); }

bool gate_runs(bool lost, bool closed) { return !lost && !closed; }

bool drain_event(int fd)
{
    uint64_t n = 0;
    return read(fd, &n, sizeof n) == (ssize_t)sizeof n;
}

}
