#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "../gfx/canvas.h"
#include "../gfx/font.h"
#include "ink.h"
#include "layer_present.h"
#include "toast_clock.h"

namespace fd {

class ToastStrip {
public:
    ToastStrip();

    void show(int64_t at, const std::string& line1, const std::string& line2, bool home);
    void hide();
    bool visible(int64_t now) const { return clock_.phase(now) != 0; }
    bool animating(int64_t now) const;
    int64_t deadline(int64_t now) const { return clock_.deadline(now); }
    fx eased(int64_t now) const { return clock_.eased(now); }
    fx text(int64_t now) const { return clock_.text(now); }
    const std::string& line1(int64_t now) const { return clock_.line1(now); }
    const std::string& line2(int64_t now) const { return clock_.line2(now); }
    void under(const Surface& s);
    void draw(Fonts& f, InkLayer& ink, const Surface& s, int64_t now);

private:
    ToastClock clock_;
    int h_ = 0;
    bool drawn_ = false;
    Buffer layer_;
    Buffer clean_;
    std::vector<uint32_t> argb_;
    std::vector<AlphaRegion> regions_;
};

}
