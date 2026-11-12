#pragma once
#include <cstdint>
#include <functional>
#include <string>

#include "../gfx/canvas.h"
#include "frame_gate.h"
#include "home.h"
#include "input_pad.h"
#include "ports.h"
#include "repeat.h"
#include "screen_selector.h"

namespace fd {

enum class LaunchPhase { Idle, Leaving, Waiting, Covered, Returning };
enum class LaunchStart { Refused, Cut, Leave };
enum class LaunchMotion { None, Reverse, Return };
enum class LaunchDraw { None, Black, Home };
enum class LaunchInput { Drop, Keep, Act };

constexpr int kCoverWaitMs = 300;

struct LaunchOutcome {
    LaunchMotion motion = LaunchMotion::None;
    bool played = false;
    std::string line1;
    std::string line2;
};

struct LaunchHooks {
    std::function<void(int64_t now, const char* why)> land;
    std::function<bool(int64_t now)> reverse;
    std::function<int64_t()> fold_end;
    std::function<void(int64_t now)> finish;
    std::function<void(int64_t now)> come_back;
    std::function<bool()> covers_pending;
    std::function<void()> played;
    std::function<void(bool light)> theme;
    std::function<void()> drops;
    std::function<void(const std::string& line1, const std::string& line2)> toast;
    std::function<void(PadAction a, int64_t now)> act;
};

const char* launch_phase_name(LaunchPhase p);
unsigned long long frame_hash(const Surface& s);
LaunchOutcome launch_refused(bool core_absent, const std::string& core);
LaunchOutcome launch_outcome(int status, LaunchPhase at, bool quit_stamp, const std::string& core);
LaunchOutcome port_outcome(int status, LaunchPhase at, bool quit_stamp);
LaunchOutcome launch_helper_busy(HelperOp op);
int64_t launch_leave_ms(bool light);
int64_t return_ms();
int leave_a256(int64_t t, int64_t dur = launch_leave_ms(false));
int return_a256(int64_t t);
int return_dx(int64_t t);
uint32_t launch_ground();
void leave_compose(const Surface& a, const Surface& dst, int a256);
void launch_compose(const Surface& a, const Surface& dst, int64_t t, int64_t dur = launch_leave_ms(false));
void return_compose(const Surface& b, const Surface& dst, int64_t t);
bool covers_pending(const HomeState& h, const SelectorModel& m, const std::function<bool(int64_t)>& pending);

class LaunchFlow;
GateIn launch_gate(const LaunchFlow& l, GateIn in, bool child);

class LaunchFlow {
public:
    LaunchHooks hooks;

    LaunchPhase phase() const { return phase_; }
    bool holds() const
    {
        return phase_ == LaunchPhase::Leaving || phase_ == LaunchPhase::Waiting || phase_ == LaunchPhase::Covered ||
               (phase_ == LaunchPhase::Returning && back_);
    }
    bool animates() const { return phase_ == LaunchPhase::Leaving || phase_ == LaunchPhase::Returning; }
    bool can_animate(bool active, bool child) const { return animates() || (active && !child); }
    bool draws() const
    {
        return phase_ == LaunchPhase::Idle || phase_ == LaunchPhase::Leaving ||
               (phase_ == LaunchPhase::Returning && back_);
    }
    bool waiting_covers() const { return phase_ == LaunchPhase::Returning && !back_; }
    int64_t cover_deadline() const { return waiting_covers() ? cover_until_ : -1; }
    int64_t cover_until() const { return cover_until_; }
    bool black_owed() const { return black_; }
    bool kept() const { return kept_; }
    void black_shown() { black_ = false; }
    bool cut() const { return cut_; }
    LaunchDraw draw(bool want) const;
    int64_t deadline() const;

    void start(bool leave, bool active, bool port = false);
    LaunchInput press(PadAction a);
    void edges(bool active, bool child, int64_t now);
    LaunchOutcome reap(int status, bool quit_stamp, const std::string& core, int64_t now, int64_t cover_until);
    void step(int64_t now);
    void theme(bool light);
    void landed(const char* what);
    bool activation(bool active, bool child);
    LaunchOutcome reaped(int status, bool quit_stamp, const std::string& core, int64_t cover_until);
    void back();
    bool take_toast(std::string& line1, std::string& line2);

private:
    void come_back(int64_t now);
    void settle();
    void flush();

    LaunchPhase phase_ = LaunchPhase::Idle;
    bool cut_ = false;
    bool port_ = false;
    bool reversing_ = false;
    bool returning_ = false;
    bool back_ = false;
    bool black_ = false;
    bool active_ = true;
    bool toast_ = false;
    bool theme_held_ = false;
    bool held_light_ = false;
    bool kept_ = false;
    PadAction key_ = PadAction::Up;
    int64_t cover_until_ = -1;
    std::string line1_;
    std::string line2_;
};

class LaunchKeys {
public:
    LaunchKeys(LaunchFlow& launch, Repeat& repeat) : launch_(launch), repeat_(repeat) {}

    std::function<void(PadAction a, bool rep, int64_t now)> apply;

    void press(PadAction a, uint8_t src, bool repeats, int64_t now);
    void repeat(PadAction a, int64_t now);
    void act(PadAction a, int64_t now);

private:
    LaunchFlow& launch_;
    Repeat& repeat_;
};

}
