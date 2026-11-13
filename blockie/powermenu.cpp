#include "powermenu.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace fd {

namespace {

constexpr const char* kPowerTitle = "power";
constexpr const char* kPowerOff = "power off";
constexpr const char* kReboot = "reboot";
constexpr const char* kAndroid = "android";
constexpr const char* kRecovery = "recovery";
constexpr const char* kConfirmTitle = "switch to android";
constexpr const char* kBootAndroid = "boot android";
constexpr const char* kCancel = "cancel";
constexpr const char* kRestarting = "restarting";
constexpr const char* kPauseTitle = "paused";
constexpr const char* kResume = "resume";
constexpr const char* kQuit = "quit";
constexpr const char* kFailedGate = "failed gate";
constexpr const char* kFailedBoot = "failed boot";
constexpr int kConfirmCancel = 1;

int ms(fx t) { return fx_round(t); }

std::string lower(std::string s)
{
    for (char& ch : s)
        ch = (char)std::tolower((unsigned char)ch);
    return s;
}

std::string failed_words(const std::string& t)
{
    if (t == kAndroid)
        return "boot android failed";
    return t + " failed";
}

enum class Gate { None, State, Android, Return, Recovery };

Gate refused(const BootGates& g, const std::string& t)
{
    if (!g.read || g.state != "default")
        return Gate::State;
    if (t == kAndroid && g.android != "bootable")
        return Gate::Android;
    if (t == kAndroid && g.ret != "keys")
        return Gate::Return;
    if (t == kRecovery && g.recovery != "yes")
        return Gate::Recovery;
    return t == kReboot || t == kAndroid || t == kRecovery ? Gate::None : Gate::State;
}

bool running(const std::string& s)
{
    return s == "activating" || s == "active" || s == "deactivating" || s == "reloading";
}

}

BootGates parse_gates(const std::string& text)
{
    BootGates g;
    g.read = true;
    std::istringstream in(text);
    std::string ln;
    while (std::getline(in, ln)) {
        size_t eq = ln.find('=');
        if (eq == std::string::npos)
            continue;
        std::string k = ln.substr(0, eq), v = ln.substr(eq + 1);
        while (!v.empty() && (v.back() == '\r' || v.back() == ' '))
            v.pop_back();
        if (k == "state")
            g.state = v;
        else if (k == "android")
            g.android = v;
        else if (k == "reason")
            g.reason = v;
        else if (k == "return")
            g.ret = v;
        else if (k == "recovery")
            g.recovery = v;
        else if (k == "fastboot")
            g.fastboot = v;
    }
    return g;
}

BootGates read_gates(const std::string& path)
{
    std::ifstream in(path);
    if (!in)
        return {};
    std::ostringstream s;
    s << in.rdbuf();
    return parse_gates(s.str());
}

UnitProbe parse_unit_show(const std::string& text)
{
    UnitProbe u;
    u.read = text.find_first_not_of(" \t\r\n") != std::string::npos;
    std::istringstream in(text);
    std::string ln;
    while (std::getline(in, ln)) {
        size_t eq = ln.find('=');
        if (eq == std::string::npos)
            continue;
        std::string k = ln.substr(0, eq), v = ln.substr(eq + 1);
        if (k == "ActiveState")
            u.active = v;
        else if (k == "Result")
            u.result = v;
        else if (k == "StateChangeTimestampMonotonic")
            u.changed_us = std::atoll(v.c_str());
        else if (k == "LoadState")
            u.load = v;
    }
    return u;
}

void PowerMenu::open(const BootGates& g, bool dark, int64_t now)
{
    gates_ = g;
    power_ = {kPowerOff};
    if (g.read && g.state == "default")
        power_.push_back(kReboot);
    sheet_ = {kReboot};
    if (g.android == "bootable" && g.ret == "keys")
        sheet_.push_back(kAndroid);
    if (g.recovery == "yes")
        sheet_.push_back(kRecovery);
    confirm_ = {kBootAndroid, kCancel};
    pause_items_ = {kResume, kQuit};
    idle_ms_ = dark ? kIdleDarkMs : kIdleAwakeMs;
    last_input_ = now;
    done_ = false;
    has_line_ = false;
    term_ = false;
    pause_ = false;
    note_.clear();
    cross_.set(0);
    open_dialog(PmSurface::Power, 0, now);
}

void PowerMenu::open_pause(const BootGates& g, int64_t now)
{
    open(g, false, now);
    pause_ = true;
    pause_focus_ = 0;
    open_dialog(PmSurface::Pause, 0, now);
}

const std::vector<const char*>& PowerMenu::items() const
{
    return surf_ == PmSurface::Sheet     ? sheet_
           : surf_ == PmSurface::Confirm ? confirm_
           : surf_ == PmSurface::Pause   ? pause_items_
                                         : power_;
}

void PowerMenu::say(const std::string& s, bool closing, int64_t at)
{
    line_ = s;
    line_at_ = at;
    line_close_ = closing;
    has_line_ = !s.empty();
}

void PowerMenu::finish(const std::string& s, int64_t at)
{
    say(s, true, at);
    surf_ = PmSurface::None;
    run_ = Run::Still;
    probing_ = false;
    done_ = true;
}

bool PowerMenu::peek_line(std::string& out, int64_t& at, bool& closing) const
{
    if (!has_line_)
        return false;
    out = line_;
    at = line_at_;
    closing = line_close_;
    return true;
}

bool PowerMenu::take_line(std::string& out)
{
    if (!has_line_)
        return false;
    out = line_;
    has_line_ = false;
    return true;
}

bool PowerMenu::take_note(std::string& out)
{
    if (note_.empty())
        return false;
    out = note_;
    note_.clear();
    return true;
}

void PowerMenu::abort(const std::string& line, int64_t now)
{
    if (!done_)
        finish(line, now);
}

void PowerMenu::term(int64_t now)
{
    if (done_ || term_)
        return;
    term_ = true;
    probing_ = false;
    if (toast_.leave(now)) {
        fail_line_.clear();
        return;
    }
    finish("", now);
}

void PowerMenu::open_dialog(PmSurface s, int focus, int64_t at)
{
    surf_ = s;
    focus_ = focus;
    prev_ = -1;
    bar_.set(fx_int(focus));
    alpha_in_.set(kOne);
    alpha_out_.set(0);
    fade_.run(at, 0, kOne, ms(tok::motion_dialog), Curve::Pop);
    scale_.run(at, tok::motion_dialogScale, kOne, ms(tok::motion_dialog), Curve::BezierPop);
    run_ = Run::Open;
    end_at_ = at + ms(tok::motion_dialog);
}

void PowerMenu::open_sheet(int64_t at)
{
    surf_ = PmSurface::Sheet;
    focus_ = 0;
    prev_ = -1;
    bar_.set(0);
    prog_.run(at, 0, kOne, ms(tok::motion_sheet), Curve::Decel);
    run_ = Run::Open;
    end_at_ = at + ms(tok::motion_sheet);
}

void PowerMenu::settle(int64_t at)
{
    Run r = run_;
    run_ = Run::Still;
    fade_.set(fade_.at(end_at_));
    scale_.set(scale_.at(end_at_));
    prog_.set(prog_.at(end_at_));
    cross_.set(cross_.at(end_at_));
    if (r == Run::Reverse) {
        cancel(at);
        return;
    }
    if (r != Run::Close)
        return;
    if (next_ == Next::Cancel)
        cancel(at);
    else if (next_ == Next::Confirm)
        open_dialog(PmSurface::Confirm, kConfirmCancel, at);
    else
        start_pick(at);
}

void PowerMenu::cancel(int64_t at)
{
    if (!pause_)
        finish(kCancel, at);
    else if (surf_ == PmSurface::Pause)
        finish(kResume, at);
    else
        to_pause(at);
}

void PowerMenu::to_pause(int64_t at)
{
    surf_ = PmSurface::Pause;
    focus_ = pause_focus_;
    prev_ = -1;
    bar_.set(fx_int(focus_));
    alpha_in_.set(kOne);
    alpha_out_.set(0);
    fade_.set(kOne);
    scale_.set(kOne);
    cross_.set(0);
    run_ = Run::Still;
    end_at_ = at;
    note_ = kCancel;
}

void PowerMenu::uncross(int64_t now)
{
    fx from = cross_.at(now);
    power_focus_ = focus_;
    to_pause(now);
    int d = (int)((int64_t)ms(tok::motion_focusAlpha) * from / kOne);
    cross_.run(now, from, 0, d, Curve::Linear);
    run_ = Run::Cross;
    end_at_ = now + d;
}

void PowerMenu::power(int64_t now)
{
    if (!pause_ || done_ || surf_ != PmSurface::Pause)
        return;
    last_input_ = now;
    land(now);
    if (done_ || surf_ != PmSurface::Pause)
        return;
    pause_focus_ = focus_;
    surf_ = PmSurface::Power;
    focus_ = 0;
    prev_ = -1;
    bar_.set(0);
    alpha_in_.set(kOne);
    alpha_out_.set(0);
    cross_.run(now, 0, kOne, ms(tok::motion_focusAlpha), Curve::Linear);
    run_ = Run::Cross;
    end_at_ = now + ms(tok::motion_focusAlpha);
}

void PowerMenu::land(int64_t now)
{
    while (run_ != Run::Still && menu())
        settle(now);
    bar_.set(bar_.to);
}

void PowerMenu::reverse(int64_t now)
{
    if (dialog()) {
        int hide = ms(tok::motion_dialogHide);
        fx back = fade_.at(now);
        scale_.set(scale_.at(now));
        int back_ms = back >= kOne ? hide
                      : back <= 0  ? 0
                                   : hide - (int)std::min<int64_t>(
                                                hide, std::lround(-hide / 10.0 * std::log2((double)back / kOne)));
        fade_.run(now, back, 0, hide, Curve::Pop);
        end_at_ = now + back_ms;
    } else {
        fx back = prog_.at(now);
        int back_ms = (int)((int64_t)ms(tok::motion_sheet) * back / kOne);
        prog_.run(now, back, 0, back_ms, Curve::Decel);
        end_at_ = now + back_ms;
    }
    run_ = Run::Reverse;
}

void PowerMenu::close(Next n, int64_t now)
{
    next_ = n;
    run_ = Run::Close;
    if (dialog()) {
        int hide = ms(tok::motion_dialogHide);
        fade_.run(now, fade_.at(now), 0, hide, Curve::Pop);
        end_at_ = now + hide;
    } else {
        prog_.run(now, prog_.at(now), 0, ms(tok::motion_sheet), Curve::Decel);
        end_at_ = now + ms(tok::motion_sheet);
    }
}

void PowerMenu::move(bool down, bool repeat, int64_t now)
{
    int n = (int)items().size(), f = focus_;
    if (n == 0)
        return;
    if (surf_ == PmSurface::Sheet) {
        if (down)
            f = f + 1 < n ? f + 1 : repeat ? f : 0;
        else
            f = f > 0 ? f - 1 : repeat ? f : n - 1;
    } else {
        f = std::clamp(f + (down ? 1 : -1), 0, n - 1);
    }
    if (f == focus_)
        return;
    bar_.run(now, bar_.at(now), fx_int(f), ms(tok::motion_focusCursor), Curve::Sine);
    if (dialog()) {
        fx was = alpha_in_.at(now), back = f == prev_ ? alpha_out_.at(now) : 0;
        alpha_out_.run(now, was, 0, ms(tok::motion_focusAlpha), Curve::Linear);
        alpha_in_.run(now, back, kOne, ms(tok::motion_focusAlpha), Curve::Linear);
        prev_ = focus_;
    }
    focus_ = f;
}

void PowerMenu::pick(const std::string& t, int64_t now)
{
    pick_ = t;
    pick_at_ = now;
    BootGates g = reread ? reread() : gates_;
    Gate why = refused(g, t);
    pick_ok_ = why == Gate::None;
    pick_reason_ = why == Gate::State || why == Gate::Android ? lower(g.reason) : "";
}

void PowerMenu::start_pick(int64_t at)
{
    surf_ = PmSurface::Toast;
    if (pick_ok_) {
        toast_.show(at, kRestarting, "", true);
        say("boot " + pick_, false, pick_at_);
        boot_at_ = at;
        probing_ = true;
        return;
    }
    toast_.show(at, failed_words(pick_), pick_reason_, false);
    fail_line_ = kFailedGate;
}

void PowerMenu::accept(int64_t now)
{
    const char* it = items()[(size_t)focus_];
    if (surf_ == PmSurface::Pause) {
        finish(it == kResume ? kResume : kQuit, now);
        return;
    }
    if (surf_ == PmSurface::Power) {
        if (it == kPowerOff)
            finish("poweroff", now);
        else
            open_sheet(now);
        return;
    }
    if (surf_ == PmSurface::Sheet) {
        if (it == kAndroid) {
            close(Next::Confirm, now);
            return;
        }
        pick(it, now);
        close(Next::Pick, now);
        return;
    }
    if (focus_ == kConfirmCancel) {
        cancel(now);
        return;
    }
    pick(kAndroid, now);
    start_pick(now);
}

void PowerMenu::action(PadAction a, bool repeat, int64_t now)
{
    if (done_ || !menu())
        return;
    if (a == PadAction::Prev || a == PadAction::Next || a == PadAction::Start)
        return;
    last_input_ = now;
    if (a == PadAction::Left || a == PadAction::Right || a == PadAction::Context)
        return;
    if (run_ == Run::Cross && surf_ == PmSurface::Power && a == PadAction::Back && !repeat) {
        uncross(now);
        return;
    }
    if (run_ == Run::Open && a == PadAction::Back && !repeat) {
        reverse(now);
        return;
    }
    bool closing = (run_ == Run::Close && next_ == Next::Cancel) || (pause_ && run_ == Run::Reverse);
    land(now);
    if (done_ || !menu() || closing)
        return;
    if (a == PadAction::Up || a == PadAction::Down) {
        move(a == PadAction::Down, repeat, now);
        return;
    }
    if (repeat)
        return;
    if (a == PadAction::Accept)
        accept(now);
    else if (a == PadAction::Back && pause_ && surf_ == PmSurface::Power)
        uncross(now);
    else if (a == PadAction::Back)
        close(Next::Cancel, now);
}

UnitVerdict PowerMenu::check(const UnitProbe& u) const
{
    UnitVerdict v;
    if (!probing_ || done_ || !u.read)
        return v;
    if (u.load != "loaded") {
        v.failed = true;
        v.why = v.reason = u.load.empty() ? "unknown" : u.load;
        return v;
    }
    if (u.changed_us < boot_at_ * 1000 || running(u.active))
        return v;
    if (u.active == "inactive" && u.result == "success")
        return v;
    std::string state = u.active.empty() ? "unknown" : u.active;
    v.failed = true;
    v.why = state + " " + u.result;
    v.reason = u.result.empty() ? state : u.result;
    return v;
}

void PowerMenu::fail(const std::string& reason, int64_t now)
{
    if (!probing_ || done_ || reason.empty())
        return;
    probing_ = false;
    toast_.show(now, failed_words(pick_), lower(reason), false);
    if (pause_)
        fail_line_ = kFailedBoot;
    else
        fail_line_.clear();
}

void PowerMenu::tick(int64_t now)
{
    if (done_)
        return;
    while (!done_ && menu() && run_ != Run::Still && now >= end_at_)
        settle(end_at_);
    if (done_)
        return;
    if (menu() && run_ != Run::Reverse && now - last_input_ >= idle_ms_) {
        finish("screen-off", now);
        return;
    }
    if (surf_ != PmSurface::Toast || !toast_.on() || toast_.forever() || now < toast_.end())
        return;
    if (pause_ && !fail_line_.empty()) {
        to_pause(now);
        note_ = fail_line_;
    } else {
        finish(fail_line_, now);
    }
}

bool PowerMenu::animating(int64_t now) const
{
    if (done_)
        return false;
    if (run_ != Run::Still || !bar_.done(now) || !alpha_in_.done(now) || !alpha_out_.done(now))
        return true;
    return toast_.animating(now);
}

int64_t PowerMenu::deadline(int64_t now) const
{
    if (done_)
        return -1;
    int64_t d = -1;
    auto take = [&](int64_t t) {
        if (t > now && (d < 0 || t < d))
            d = t;
    };
    if (run_ != Run::Still)
        take(end_at_);
    if (menu())
        take(last_input_ + idle_ms_);
    take(toast_.deadline(now));
    return d;
}

bool PowerMenu::power_drawn(int64_t now) const
{
    if (done_ || !pause_)
        return false;
    if (run_ == Run::Cross)
        return surf_ == PmSurface::Power && cross_.at(now) > 0;
    return surf_ == PmSurface::Power || surf_ == PmSurface::Sheet || surf_ == PmSurface::Confirm;
}

PmBody PowerMenu::body(const std::vector<const char*>& items, int focus, const char* title) const
{
    PmBody b;
    b.title = title;
    b.items = items;
    b.bar = fx_int(focus);
    for (int i = 0; i < (int)items.size(); i++)
        b.alpha.push_back(i == focus ? tok::type_dialogItem_alpha : tok::type_dialogItem_alphaDim);
    return b;
}

PmView PowerMenu::view(int64_t now) const
{
    PmView v;
    v.surface = done_ ? PmSurface::None : surf_;
    if (pause_ && (v.surface == PmSurface::Sheet || v.surface == PmSurface::Confirm || v.surface == PmSurface::Toast)) {
        v.under = true;
        v.other = body(pause_items_, pause_focus_, kPauseTitle);
    }
    if (v.surface == PmSurface::Toast) {
        v.line1 = toast_.line1(now);
        v.line2 = toast_.line2(now);
        v.e = toast_.eased(now);
        v.text = toast_.text(now);
        return v;
    }
    if (v.surface == PmSurface::Sheet) {
        SheetView& s = v.sheet;
        s.title = kReboot;
        s.items = sheet_.data();
        s.count = (int)sheet_.size();
        s.focus = focus_;
        s.bar = bar_.at(now);
        s.rows = s.count;
        s.progress = prog_.at(now);
        s.opaque = true;
        return v;
    }
    if (v.surface == PmSurface::None)
        return v;
    v.title = v.surface == PmSurface::Confirm ? kConfirmTitle
              : v.surface == PmSurface::Pause ? kPauseTitle
                                              : kPowerTitle;
    v.items = items();
    if (run_ == Run::Cross && v.surface == PmSurface::Power) {
        v.other = body(pause_items_, pause_focus_, kPauseTitle);
        v.mix = kOne - cross_.at(now);
    } else if (run_ == Run::Cross) {
        v.other = body(power_, power_focus_, kPowerTitle);
        v.mix = cross_.at(now);
    }
    v.focus = focus_;
    v.bar = bar_.at(now);
    v.o = fade_.at(now);
    v.s = scale_.at(now);
    for (int i = 0; i < (int)v.items.size(); i++) {
        fx t = i == focus_ ? alpha_in_.at(now) : i == prev_ ? alpha_out_.at(now) : 0;
        v.alpha.push_back(fx_lerp(tok::type_dialogItem_alphaDim, tok::type_dialogItem_alpha, t));
    }
    return v;
}

}
