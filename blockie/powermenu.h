#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "../core/tween.h"
#include "../gfx/font.h"
#include "ink.h"
#include "input_pad.h"
#include "layer_present.h"
#include "screen_settings.h"
#include "theme.h"
#include "toast_clock.h"

namespace fd {

struct BootGates {
    bool read = false;
    std::string state, android, reason, ret, recovery, fastboot;
};

BootGates parse_gates(const std::string& text);
BootGates read_gates(const std::string& path);

struct UnitProbe {
    bool read = false;
    std::string load, active, result;
    int64_t changed_us = 0;
};

UnitProbe parse_unit_show(const std::string& text);

struct UnitVerdict {
    bool failed = false;
    std::string why, reason;
};

enum class PmSurface { None, Power, Sheet, Confirm, Toast };

struct PmView {
    PmSurface surface = PmSurface::None;
    const char* title = "";
    std::vector<const char*> items;
    int focus = 0;
    fx bar = 0;
    std::vector<fx> alpha;
    fx o = 0;
    fx s = kOne;
    SheetView sheet;
    std::string line1, line2;
    fx e = 0;
    fx text = kOne;
    int strip_h = 0;
};

class PowerMenu {
public:
    static constexpr int kIdleAwakeMs = 30000;
    static constexpr int kIdleDarkMs = 10000;
    static constexpr int kProbeMs = 1000;
    static constexpr int kCloseMs = 50;
    static constexpr int kBootMs = fx_round(tok::motion_sheet) + 100;

    std::function<BootGates()> reread;

    void open(const BootGates& g, bool dark, int64_t now);
    void action(PadAction a, bool repeat, int64_t now);
    UnitVerdict check(const UnitProbe& u) const;
    void fail(const std::string& reason, int64_t now);
    void tick(int64_t now);
    void abort(const std::string& line, int64_t now);
    void term(int64_t now);
    void set_idle_ms(int ms) { idle_ms_ = ms; }
    void set_toast_ms(int in, int out) { toast_.set_ms(in, out); }
    bool peek_line(std::string& out, int64_t& at, bool& closing) const;
    bool take_line(std::string& out);
    bool done() const { return done_; }
    bool animating(int64_t now) const;
    int64_t deadline(int64_t now) const;
    bool probing() const { return probing_; }
    const std::string& target() const { return pick_; }
    int64_t boot_at() const { return boot_at_; }
    PmSurface surface() const { return surf_; }
    PmView view(int64_t now) const;

private:
    enum class Run { Still, Open, Close, Reverse };
    enum class Next { Cancel, Confirm, Pick };

    bool menu() const { return surf_ == PmSurface::Power || surf_ == PmSurface::Sheet || surf_ == PmSurface::Confirm; }
    bool dialog() const { return surf_ == PmSurface::Power || surf_ == PmSurface::Confirm; }
    const std::vector<const char*>& items() const;
    void say(const std::string& s, bool closing, int64_t at);
    void finish(const std::string& s, int64_t at);
    void open_dialog(PmSurface s, int focus, int64_t at);
    void open_sheet(int64_t at);
    void settle(int64_t at);
    void land(int64_t now);
    void reverse(int64_t now);
    void close(Next n, int64_t now);
    void move(bool down, bool repeat, int64_t now);
    void accept(int64_t now);
    void pick(const std::string& t, int64_t now);
    void start_pick(int64_t at);

    BootGates gates_;
    std::vector<const char*> power_, sheet_, confirm_;
    PmSurface surf_ = PmSurface::None;
    Run run_ = Run::Still;
    Next next_ = Next::Cancel;
    int64_t end_at_ = 0;
    int focus_ = 0, prev_ = -1;
    Tween fade_, scale_, prog_, bar_, alpha_in_, alpha_out_;
    int idle_ms_ = kIdleAwakeMs;
    int64_t last_input_ = 0;
    std::string pick_, pick_reason_;
    bool pick_ok_ = false;
    int64_t pick_at_ = 0;
    bool probing_ = false;
    int64_t boot_at_ = -1;
    ToastClock toast_;
    std::string fail_line_;
    std::string line_;
    int64_t line_at_ = 0;
    bool has_line_ = false, line_close_ = false, term_ = false;
    bool done_ = false;
};

bool draw_powermenu(Canvas& c, Fonts& f, InkLayer& ink, const PmView& v, const Theme& th,
                    std::vector<AlphaRegion>& regions);

}
