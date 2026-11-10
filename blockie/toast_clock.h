#pragma once
#include <cstdint>
#include <string>

#include "../core/fx.h"

namespace fd {

class ToastClock {
public:
    void set_ms(int in, int out)
    {
        in_ms_ = in;
        out_ms_ = out;
    }
    void show(int64_t at, const std::string& line1, const std::string& line2, bool forever);
    void hide()
    {
        on_ = false;
        swap_ = -1;
    }
    bool leave(int64_t now);
    bool on() const { return on_; }
    bool forever() const { return forever_; }
    int phase(int64_t now) const;
    int64_t end() const;
    fx eased(int64_t now) const;
    fx text(int64_t now) const;
    bool animating(int64_t now) const;
    int64_t deadline(int64_t now) const;
    const std::string& line1(int64_t now) const { return swapped(now) ? next_l1_ : l1_; }
    const std::string& line2(int64_t now) const { return swapped(now) ? next_l2_ : l2_; }

private:
    bool swapped(int64_t now) const;

    int in_ms_ = fx_round(tok::motion_toastIn), out_ms_ = fx_round(tok::motion_toastOut);
    bool on_ = false, forever_ = false;
    int64_t in_ = 0, hold_ = 0, swap_ = -1;
    std::string l1_, l2_, next_l1_, next_l2_;
};

}
