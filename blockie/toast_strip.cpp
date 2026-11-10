#include "toast_strip.h"

#include <algorithm>

#include "layer_mem.h"
#include "powermenu.h"
#include "theme.h"
#include "tile_grid.h"

namespace fd {

namespace {

int strip_h() { return std::min(kPortraitH, std_px(tok::toastHeight)); }

void copy_rows(const Surface& src, const Surface& dst, int h)
{
    for (int y = 0; y < h; y++)
        for (int x = 0; x < dst.w; x++)
            dst.at(x, y) = src.at(x, y);
}

}

ToastStrip::ToastStrip()
    : layer_(kPortraitW, strip_h()), clean_(kPortraitW, strip_h()), argb_((size_t)kPortraitW * strip_h())
{
}

void ToastStrip::show(int64_t at, const std::string& line1, const std::string& line2, bool home)
{
    int p = clock_.phase(at);
    if (p != 1 && p != 2)
        h_ = home ? kBandTop : 0;
    clock_.show(at, line1, line2, false);
}

void ToastStrip::hide()
{
    clock_.hide();
    drawn_ = false;
}

bool ToastStrip::animating(int64_t now) const { return clock_.phase(now) == 0 ? drawn_ : clock_.animating(now); }

void ToastStrip::under(const Surface& s)
{
    if (drawn_)
        copy_rows(clean_.surface(), s, std::min(s.h, clean_.h));
}

void ToastStrip::draw(Fonts& f, InkLayer& ink, const Surface& s, int64_t now)
{
    drawn_ = false;
    fx e = eased(now);
    if (e <= 0)
        return;
    Surface top = s;
    top.h = std::min(s.h, layer_.h);
    copy_rows(s, clean_.surface(), top.h);
    PmView v;
    v.surface = PmSurface::Toast;
    v.line1 = line1(now);
    v.line2 = line2(now);
    v.e = e;
    v.text = text(now);
    v.strip_h = h_;
    Canvas c(layer_.surface());
    draw_powermenu(c, f, ink, v, theme(), regions_);
    write_argb(layer_.surface(), regions_, argb_.data(), layer_.w);
    composite_argb(argb_.data(), layer_.w, top, top);
    drawn_ = true;
}

}
