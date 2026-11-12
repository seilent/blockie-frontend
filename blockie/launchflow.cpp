#include "launchflow.h"

#include <cstring>
#include <type_traits>

#include <sys/wait.h>

#include "../gfx/compose.h"
#include "launch.h"
#include "leftmove.h"

namespace fd {

namespace {

constexpr const char* kNotStarted = "game not started";

struct PortWords {
    const char* quit;
    const char* oom;
    const char* missing;
    const char* installing;
    const char* updating;
    const char* removing;
};

constexpr PortWords kPortWords = {
    "the port quit",        "out of memory",      "the port's files are missing",
    "a port is installing", "a port is updating", "a port is being removed",
};

int ms(fx v) { return fx_round(v); }

fx progress(int64_t e, int64_t dur)
{
    if (dur <= 0 || e >= dur)
        return kOne;
    if (e <= 0)
        return 0;
    return (fx)(e * kOne / dur);
}

int64_t dark_ms() { return ms(tok::motion_npDark); }

}

const char* launch_phase_name(LaunchPhase p)
{
    switch (p) {
    case LaunchPhase::Idle: return "idle";
    case LaunchPhase::Leaving: return "leaving";
    case LaunchPhase::Waiting: return "waiting";
    case LaunchPhase::Covered: return "covered";
    case LaunchPhase::Returning: return "returning";
    }
    return "?";
}

unsigned long long frame_hash(const Surface& s)
{
    unsigned long long h = 1469598103934665603ull;
    for (int y = 0; y < s.h; y++)
        for (int x = 0; x < s.w; x++) {
            h ^= s.at(x, y);
            h *= 1099511628211ull;
        }
    return h;
}

LaunchOutcome launch_refused(bool core_absent, const std::string& core)
{
    LaunchOutcome o;
    o.line1 = core_absent ? "core not installed" : kNotStarted;
    o.line2 = core_absent ? core : "could not start the launcher";
    return o;
}

LaunchOutcome launch_outcome(int status, LaunchPhase at, bool quit_stamp, const std::string& core)
{
    LaunchOutcome o;
    bool covered = at == LaunchPhase::Covered;
    o.motion = at == LaunchPhase::Leaving ? LaunchMotion::Reverse : LaunchMotion::Return;
    switch (Launcher::classify(status)) {
    case LaunchExit::Busy:
        o.line1 = kNotStarted;
        o.line2 = "another game is closing";
        break;
    case LaunchExit::ConfigError:
        o.line1 = kNotStarted;
        o.line2 = WIFEXITED(status) && WEXITSTATUS(status) == 127 ? "launcher missing" : "game settings refused";
        break;
    case LaunchExit::CoreAbsent:
        o.line1 = "core not installed";
        o.line2 = core;
        break;
    case LaunchExit::AudioGate:
        o.line1 = kNotStarted;
        o.line2 = "audio not ready";
        break;
    case LaunchExit::Crash:
        o.played = covered;
        o.line1 = covered ? "game closed" : kNotStarted;
        o.line2 = "the emulator stopped";
        break;
    case LaunchExit::Quit:
        o.played = covered;
        if (covered)
            break;
        if (quit_stamp) {
            o.motion = LaunchMotion::Return;
            break;
        }
        o.line1 = kNotStarted;
        o.line2 = "the core closed it";
        break;
    }
    return o;
}

LaunchOutcome port_outcome(int status, LaunchPhase at, bool quit_stamp)
{
    LaunchOutcome o = launch_outcome(status, at, quit_stamp, std::string());
    int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    switch (Launcher::classify(status)) {
    case LaunchExit::Crash:
        if (code == 1)
            o.line2 = kPortWords.oom;
        break;
    case LaunchExit::ConfigError:
        if (code == 64)
            o.line2 = kPortWords.missing;
        break;
    case LaunchExit::Quit:
        if (!o.line1.empty())
            o.line2 = kPortWords.quit;
        break;
    default:
        break;
    }
    return o;
}

LaunchOutcome launch_helper_busy(HelperOp op)
{
    LaunchOutcome o;
    o.line1 = kNotStarted;
    o.line2 = op == HelperOp::Update   ? kPortWords.updating
              : op == HelperOp::Remove ? kPortWords.removing
                                       : kPortWords.installing;
    return o;
}

int64_t launch_leave_ms(bool light) { return ms(light ? tok::motion_t7Fade : tok::motion_t7Leave); }

int64_t return_ms() { return dark_ms() + ms(tok::motion_t8Page); }

int leave_a256(int64_t t, int64_t dur) { return alpha256(kOne - progress(t, dur)); }

int return_a256(int64_t t)
{
    return alpha256(progress(t - dark_ms() - ms(tok::motion_t8PageFadeDelay), ms(tok::motion_t8PageFade)));
}

int return_dx(int64_t t)
{
    fx p = progress(t - dark_ms(), ms(tok::motion_t8Page));
    return fx_round(fx_mul(fx_int(std_px(tok::motion_t8PageX)), kOne - cubic_out(p)));
}

uint32_t launch_ground() { return tok::color_black; }

void leave_compose(const Surface& a, const Surface& dst, int a256)
{
    if constexpr (std::is_same_v<LiveF, X888> && std::is_same_v<LiveL, Rows>) {
        if (a256 > 0 && a256 < 256 && a.w == dst.w && a.h == dst.h && LiveF::pack(launch_ground()) == 0) {
            uint32_t m = (uint32_t)a256;
            for (int y = 0; y < dst.h; y++) {
                const auto* s = a.row(y);
                auto* d = dst.row(y);
                for (int x = 0; x < dst.w; x++) {
                    uint32_t p = s[x];
                    d[x] = (LiveF::T)(((p & 0xFF00FFu) * m >> 8 & 0xFF00FFu) | ((p & 0xFF00u) * m >> 8 & 0xFF00u));
                }
            }
            return;
        }
    }
    Canvas c(dst);
    c.fill(dst.rect(), launch_ground());
    blend_band(a, dst, dst.rect(), 0, a256);
}

void launch_compose(const Surface& a, const Surface& dst, int64_t t, int64_t dur)
{
    leave_compose(a, dst, leave_a256(t, dur));
}

void return_compose(const Surface& b, const Surface& dst, int64_t t)
{
    Canvas c(dst);
    c.fill(dst.rect(), launch_ground());
    blend_band(b, dst, dst.rect(), return_dx(t), return_a256(t));
}

bool covers_pending(const HomeState& h, const SelectorModel& m, const std::function<bool(int64_t)>& pending)
{
    const std::vector<HomeGame>& list = h.system_view ? h.sysgames : h.recent;
    int base = h.system_view ? h.first : 0;
    for (int k = 0; k < (int)m.tiles.size(); k++) {
        int i = base + k;
        if (!m.tiles[k] && i < (int)list.size() && pending(list[i].id))
            return true;
    }
    return false;
}

GateIn launch_gate(const LaunchFlow& l, GateIn in, bool child)
{
    in.activated = in.activated || l.animates();
    in.child = child && !l.animates() && !l.black_owed();
    in.need_redraw = in.need_redraw || l.black_owed();
    in.claim = in.claim && (l.black_owed() || l.draws());
    return in;
}

void LaunchFlow::start(bool leave, bool active, bool port)
{
    phase_ = leave ? LaunchPhase::Leaving : LaunchPhase::Waiting;
    cut_ = !leave;
    port_ = port;
    reversing_ = false;
    returning_ = false;
    back_ = false;
    black_ = false;
    toast_ = false;
    kept_ = false;
    active_ = active;
    cover_until_ = -1;
}

LaunchDraw LaunchFlow::draw(bool want) const
{
    if (!want)
        return LaunchDraw::None;
    return black_ ? LaunchDraw::Black : draws() ? LaunchDraw::Home : LaunchDraw::None;
}

int64_t LaunchFlow::deadline() const
{
    if ((theme_held_ && !holds()) || (kept_ && phase_ == LaunchPhase::Idle))
        return 0;
    if (waiting_covers())
        return cover_until_;
    if (animates() && hooks.fold_end)
        return hooks.fold_end();
    return -1;
}

LaunchInput LaunchFlow::press(PadAction a)
{
    if (phase_ == LaunchPhase::Idle)
        return LaunchInput::Act;
    if (phase_ != LaunchPhase::Returning)
        return LaunchInput::Drop;
    kept_ = true;
    key_ = a;
    return LaunchInput::Keep;
}

void LaunchFlow::edges(bool active, bool child, int64_t now)
{
    if (activation(active, child) && hooks.land)
        hooks.land(now, "covered");
}

LaunchOutcome LaunchFlow::reap(int status, bool quit_stamp, const std::string& core, int64_t now, int64_t cover_until)
{
    LaunchOutcome o = reaped(status, quit_stamp, core, cover_until);
    if (o.played && hooks.played)
        hooks.played();
    if (o.motion == LaunchMotion::Reverse && !(hooks.reverse && hooks.reverse(now)))
        landed("launch");
    bool folds = hooks.fold_end && hooks.fold_end() >= 0;
    if (o.motion == LaunchMotion::Return && phase_ == LaunchPhase::Leaving && !folds)
        landed("launch");
    settle();
    return o;
}

void LaunchFlow::step(int64_t now)
{
    if (!holds())
        flush();
    if (kept_ && phase_ == LaunchPhase::Idle) {
        kept_ = false;
        if (active_ && hooks.act)
            hooks.act(key_, now);
    }
    if (waiting_covers() && (now >= cover_until_ || !(hooks.covers_pending && hooks.covers_pending())))
        come_back(now);
    if (animates() && !waiting_covers() && hooks.finish)
        hooks.finish(now);
}

void LaunchFlow::flush()
{
    if (theme_held_) {
        theme_held_ = false;
        if (hooks.theme)
            hooks.theme(held_light_);
    }
    if (hooks.drops)
        hooks.drops();
}

void LaunchFlow::theme(bool light)
{
    if (holds()) {
        theme_held_ = true;
        held_light_ = light;
        return;
    }
    theme_held_ = false;
    if (hooks.theme)
        hooks.theme(light);
}

void LaunchFlow::come_back(int64_t now)
{
    back();
    if (hooks.come_back)
        hooks.come_back(now);
}

void LaunchFlow::settle()
{
    if (phase_ != LaunchPhase::Idle)
        return;
    std::string line1, line2;
    if (hooks.toast && take_toast(line1, line2))
        hooks.toast(line1, line2);
}

void LaunchFlow::landed(const char* what)
{
    if (phase_ == LaunchPhase::Leaving && std::strcmp(what, "launch") == 0) {
        phase_ = reversing_ ? LaunchPhase::Idle : returning_ ? LaunchPhase::Returning : LaunchPhase::Waiting;
        black_ = !reversing_;
        reversing_ = false;
        returning_ = false;
    } else if (phase_ == LaunchPhase::Returning && back_ && std::strcmp(what, "return") == 0) {
        phase_ = LaunchPhase::Idle;
        back_ = false;
    } else {
        return;
    }
    settle();
}

bool LaunchFlow::activation(bool active, bool child)
{
    bool fell = active_ && !active;
    active_ = active;
    if (!fell || !child || (phase_ != LaunchPhase::Leaving && phase_ != LaunchPhase::Waiting))
        return false;
    phase_ = LaunchPhase::Covered;
    black_ = !cut_;
    return true;
}

LaunchOutcome LaunchFlow::reaped(int status, bool quit_stamp, const std::string& core, int64_t cover_until)
{
    if (phase_ == LaunchPhase::Idle || phase_ == LaunchPhase::Returning || reversing_ || returning_)
        return {};
    LaunchOutcome o =
        port_ ? port_outcome(status, phase_, quit_stamp) : launch_outcome(status, phase_, quit_stamp, core);
    toast_ = !o.line1.empty();
    line1_ = o.line1;
    line2_ = o.line2;
    if (cut_) {
        o.motion = LaunchMotion::None;
        phase_ = LaunchPhase::Idle;
        return o;
    }
    if (o.motion == LaunchMotion::Reverse) {
        reversing_ = true;
        return o;
    }
    cover_until_ = cover_until;
    back_ = false;
    if (phase_ == LaunchPhase::Leaving) {
        returning_ = true;
        return o;
    }
    phase_ = LaunchPhase::Returning;
    return o;
}

void LaunchFlow::back()
{
    if (waiting_covers())
        back_ = true;
}

bool LaunchFlow::take_toast(std::string& line1, std::string& line2)
{
    if (phase_ != LaunchPhase::Idle || !toast_)
        return false;
    toast_ = false;
    line1 = line1_;
    line2 = line2_;
    return true;
}

void LaunchKeys::press(PadAction a, uint8_t src, bool repeats, int64_t now)
{
    LaunchInput in = launch_.press(a);
    if (in == LaunchInput::Act && apply)
        apply(a, false, now);
    if (in != LaunchInput::Drop && repeats)
        repeat_.press(a, src, now);
}

void LaunchKeys::repeat(PadAction a, int64_t now)
{
    if (launch_.phase() == LaunchPhase::Idle && !launch_.kept() && apply)
        apply(a, true, now);
}

void LaunchKeys::act(PadAction a, int64_t now)
{
    if (apply)
        apply(a, false, now);
    repeat_.rearm(now);
}

}
