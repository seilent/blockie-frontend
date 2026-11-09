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
constexpr const char* kScreenOff = "screen off";
constexpr const char* kPowerOff = "power off";
constexpr const char* kReboot = "reboot";
constexpr const char* kAndroid = "android";
constexpr const char* kRecovery = "recovery";
constexpr const char* kConfirmTitle = "android becomes default";
constexpr const char* kBootAndroid = "boot android";
constexpr const char* kCancel = "cancel";
constexpr const char* kHostLine = "back to blockie needs the computer";
constexpr const char* kRestarting = "restarting";
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

bool gate_holds(const BootGates& g, const std::string& t)
{
    if (!g.read || g.state != "default")
        return false;
    if (t == kAndroid)
        return g.android == "bootable";
    if (t == kRecovery)
        return g.recovery == "yes";
    return t == kReboot;
}

fx progress(int64_t e, int d) { return e >= d || d <= 0 ? kOne : e <= 0 ? 0 : (fx)(e * kOne / d); }

fx exp_out6_inverse(fx e)
{
    fx lo = 0, hi = kOne;
    for (int i = 0; i < 17; i++) {
        fx mid = (lo + hi) / 2;
        if (curve(Curve::ExpOut6, mid) < e)
            lo = mid;
        else
            hi = mid;
    }
    return hi;
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
    power_ = {kScreenOff, kPowerOff};
    if (g.read && g.state == "default")
        power_.push_back(kReboot);
    sheet_ = {kReboot};
    if (g.android == "bootable")
        sheet_.push_back(kAndroid);
    if (g.recovery == "yes")
        sheet_.push_back(kRecovery);
    confirm_ = {kBootAndroid, kCancel};
    idle_ms_ = dark ? kIdleDarkMs : kIdleAwakeMs;
    last_input_ = now;
    done_ = false;
    has_line_ = false;
    term_ = false;
    open_dialog(PmSurface::Power, 0, now);
}

const std::vector<const char*>& PowerMenu::items() const
{
    return surf_ == PmSurface::Sheet ? sheet_ : surf_ == PmSurface::Confirm ? confirm_ : power_;
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
    if (toast_on_ && !toast_forever_ && toast_phase(now) != 0) {
        fx p = exp_out6_inverse(kOne - toast_eased(now));
        toast_hold_ = now - ms(tok::motion_toastHold) - (int64_t)out_ms_ * p / kOne;
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
    if (r == Run::Reverse) {
        finish(kCancel, at);
        return;
    }
    if (r != Run::Close)
        return;
    if (next_ == Next::Cancel)
        finish(kCancel, at);
    else if (next_ == Next::Confirm)
        open_dialog(PmSurface::Confirm, kConfirmCancel, at);
    else
        start_pick(at);
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
    pick_ok_ = gate_holds(g, t);
    pick_reason_ = lower(g.reason);
}

void PowerMenu::start_pick(int64_t at)
{
    surf_ = PmSurface::Toast;
    if (pick_ok_) {
        toast(at, kRestarting, "", true);
        say("boot " + pick_, false, pick_at_);
        boot_at_ = at;
        probing_ = true;
        return;
    }
    toast(at, failed_words(pick_), pick_reason_, false);
    fail_line_ = "failed gate";
}

void PowerMenu::accept(int64_t now)
{
    const char* it = items()[(size_t)focus_];
    if (surf_ == PmSurface::Power) {
        if (it == kScreenOff)
            finish("screen-off", now);
        else if (it == kPowerOff)
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
        finish(kCancel, now);
        return;
    }
    pick(kAndroid, now);
    start_pick(now);
}

void PowerMenu::action(PadAction a, bool repeat, int64_t now)
{
    if (done_ || !menu())
        return;
    last_input_ = now;
    if (a == PadAction::Left || a == PadAction::Right || a == PadAction::Context)
        return;
    if (run_ == Run::Open && a == PadAction::Back && !repeat) {
        reverse(now);
        return;
    }
    land(now);
    if (done_ || !menu())
        return;
    if (a == PadAction::Up || a == PadAction::Down) {
        move(a == PadAction::Down, repeat, now);
        return;
    }
    if (repeat)
        return;
    if (a == PadAction::Accept)
        accept(now);
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
    toast(now, failed_words(pick_), lower(reason), false);
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
    if (toast_on_ && !toast_forever_ && now >= toast_end())
        finish(fail_line_, now);
}

void PowerMenu::toast(int64_t at, const std::string& l1, const std::string& l2, bool forever)
{
    int p = toast_phase(at);
    toast_forever_ = forever;
    if (toast_on_ && (p == 1 || p == 2)) {
        next_l1_ = l1;
        next_l2_ = l2;
        toast_swap_ = at;
        if (p == 2)
            toast_hold_ = at;
        return;
    }
    toast_on_ = true;
    toast_in_ = at;
    toast_hold_ = at + in_ms_;
    toast_swap_ = -1;
    l1_ = l1;
    l2_ = l2;
}

int PowerMenu::toast_phase(int64_t now) const
{
    if (!toast_on_)
        return 0;
    if (now < toast_hold_)
        return 1;
    if (toast_forever_ || now < toast_hold_ + ms(tok::motion_toastHold))
        return 2;
    if (now < toast_end())
        return 3;
    return 0;
}

int64_t PowerMenu::toast_end() const { return toast_hold_ + ms(tok::motion_toastHold) + out_ms_; }

fx PowerMenu::toast_eased(int64_t now) const
{
    switch (toast_phase(now)) {
    case 1:
        return curve(Curve::ExpOut6, progress(now - toast_in_, in_ms_));
    case 2:
        return kOne;
    case 3:
        return kOne - curve(Curve::ExpOut6, progress(now - toast_hold_ - ms(tok::motion_toastHold), out_ms_));
    default:
        return 0;
    }
}

fx PowerMenu::toast_text(int64_t now) const
{
    if (toast_swap_ < 0)
        return kOne;
    int64_t e = now - toast_swap_;
    int d = ms(tok::motion_valueChange);
    if (e < d)
        return fx_lerp(kOne, tok::motion_valueDip, progress(e, d));
    return fx_lerp(tok::motion_valueDip, kOne, progress(e - d, d));
}

bool PowerMenu::animating(int64_t now) const
{
    if (done_)
        return false;
    if (run_ != Run::Still || !bar_.done(now) || !alpha_in_.done(now) || !alpha_out_.done(now))
        return true;
    int p = toast_phase(now);
    return p == 1 || p == 3 || (toast_swap_ >= 0 && now < toast_swap_ + 2 * ms(tok::motion_valueChange));
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
    if (toast_on_ && !toast_forever_) {
        take(toast_hold_ + ms(tok::motion_toastHold));
        take(toast_end());
    }
    return d;
}

PmView PowerMenu::view(int64_t now) const
{
    PmView v;
    v.surface = done_ ? PmSurface::None : surf_;
    if (v.surface == PmSurface::Toast) {
        bool next = toast_swap_ >= 0 && now >= toast_swap_ + ms(tok::motion_valueChange);
        v.line1 = next ? next_l1_ : l1_;
        v.line2 = next ? next_l2_ : l2_;
        v.e = toast_eased(now);
        v.text = toast_text(now);
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
    v.title = v.surface == PmSurface::Confirm ? kConfirmTitle : kPowerTitle;
    v.items = items();
    v.lead = v.surface == PmSurface::Confirm && gates_.ret == "host" ? kHostLine : nullptr;
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
