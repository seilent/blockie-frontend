#pragma once
#include <cstdint>
#include <vector>

namespace fd {

constexpr int kFrameWatchdogMs = 100;
constexpr int kFillWaitMs = 100;

struct GateIn {
    int64_t now = 0;
    bool ready = false;
    bool claim = false;
    bool activated = false;
    bool child = false;
    bool need_redraw = false;
    bool animating = false;
    int timeout = -1;
};

struct GateStep {
    bool report_done = false;
    const char* land_reason = nullptr;
    bool redraw = false;
    bool draw = false;
    bool tail = false;
};

class FrameGate {
public:
    GateStep step(const GateIn& in);
    void posted(bool posted, bool motion, int64_t now);
    int timeout(const GateIn& in) const;
    bool armed() const { return armed_; }
    int tails() const { return tails_; }

private:
    bool want(const GateIn& in) const;

    bool armed_ = false;
    int64_t at_ = 0;
    bool tail_ = false;
    int tails_ = 0;
};

struct GateFd {
    int fd;
    short events;
};

struct GateFds {
    GateFd display;
    GateFd event;
};

GateFds gate_fds(int display_fd, int event_fd, bool blocked);
bool gate_pump(short revents);
bool gate_runs(bool lost, bool closed);
bool drain_event(int fd);

template <class P, class T> bool pump_presenter(P& p, std::vector<T>& touch)
{
    bool ok = p.pump();
    p.take_touch(touch);
    return ok;
}

template <class P, class Now, class Wait, class Fill> void fill_when_ready(P& p, Now now, Wait wait, Fill fill)
{
    int64_t until = now() + kFillWaitMs;
    for (;;) {
        int64_t left = until - now();
        if (p.ready() || p.lost() || left <= 0 || !wait((int)left))
            break;
    }
    fill();
    p.flip();
    p.flush();
}

}
