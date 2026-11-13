#include "powermenu.h"

#include <algorithm>

#include "../ui/roles.h"

namespace fd {

namespace {

int index_px(fx at, int pitch) { return (int)(((int64_t)at * pitch + kHalf) >> kFxBits); }

bool draw_body(Canvas& k, Fonts& f, const PmBody& b, Rect sr, Zoom z)
{
    int edge = px(tok::edge), pitch = px(tok::settingsPitch), top = px(tok::contentTop);
    Rect keep = k.clip();
    TextStyle ts = role::dialogTitle.style(), is = role::dialogItem.style();
    k.set_clip(z.rect(Rect{sr.x0, sr.y0, sr.x1 - edge, top}).intersect(keep));
    bool ok = f.draw(k, ts, b.title, edge * 64, px(tok::pivotBaseline), kWhite, alpha256(role::dialogTitle.alpha), z);
    int asc = f.ascent(is.px), desc = f.descent(is.px);
    for (size_t i = 0; i < b.items.size(); i++) {
        int rt = top + (int)i * pitch;
        k.set_clip(z.rect(Rect{sr.x0, rt, sr.x1 - edge, rt + pitch}).intersect(keep));
        ok = f.draw(k, is, b.items[i], edge * 64, rt + (pitch - asc - desc) / 2 + asc, kWhite, alpha256(b.alpha[i]),
                    z) && ok;
    }
    k.set_clip(keep);
    if (!b.items.empty()) {
        int bx = edge - px(tok::focus_barOffset), by = top + index_px(b.bar, pitch);
        k.fill_aa(Rect64::of({bx, by, bx + px(tok::focus_barWidth), by + pitch}, z), kWhite, 256);
    }
    return ok;
}

InkLayer& cross_ink()
{
    static InkLayer layer;
    return layer;
}

bool draw_dialog(Canvas& c, Fonts& f, InkLayer& ink, const PmView& v, const Theme& th,
                 std::vector<AlphaRegion>& regions)
{
    Rect sr = c.surface().rect();
    c.fill(sr, th.scrim_dialog);
    regions.push_back({sr, th.scrim_dialog_a256 * alpha256(v.o) >> 8});
    Zoom z{v.s, sr.x1 * 32, sr.y1 * 32};
    bool ok = draw_body(ink.begin(sr), f, PmBody{v.title, v.items, v.alpha, v.bar}, sr, z);
    if (!v.under && !v.other.items.empty()) {
        InkLayer& o = cross_ink();
        ok = draw_body(o.begin(sr), f, v.other, sr, z) && ok;
        int m = alpha256(v.mix);
        ink_add(ink.surface(), o.surface(), 256 - m, m, sr);
    }
    ink.compose(c.surface(), th.ink);
    return ok;
}

void draw_sheet(Canvas& c, Fonts& f, const PmView& v, const Theme& th, std::vector<AlphaRegion>& regions)
{
    Rect sr = c.surface().rect();
    c.fill(sr, th.scrim_sheet);
    draw_value_sheet(c, f, v.sheet, th);
    SheetGeom g = sheet_geom(f, v.sheet, sr);
    int a = alpha256(v.sheet.progress);
    regions.push_back({sr, th.scrim_sheet_a256 * a >> 8});
    regions.push_back({{sr.x0, g.top, sr.x1, g.rows_top + g.shown * g.pitch + px(tok::edge)}, a});
}

bool draw_pm_toast(Canvas& c, Fonts& f, InkLayer& ink, const PmView& v, const Theme& th,
                   std::vector<AlphaRegion>& regions)
{
    if (v.e <= 0)
        return true;
    Rect strip{};
    bool ok = draw_toast(c, f, ink, ToastView{v.e, v.text, v.line1, v.line2, v.strip_h}, th, &strip);
    regions.push_back({strip, alpha256(fx_mul(v.e, tok::scrim_overlay_alpha))});
    return ok;
}

bool draw_top(Canvas& c, Fonts& f, InkLayer& ink, const PmView& v, const Theme& th,
              std::vector<AlphaRegion>& regions)
{
    switch (v.surface) {
    case PmSurface::Power:
    case PmSurface::Confirm:
    case PmSurface::Pause:
        return draw_dialog(c, f, ink, v, th, regions);
    case PmSurface::Sheet:
        draw_sheet(c, f, v, th, regions);
        return true;
    case PmSurface::Toast:
        return draw_pm_toast(c, f, ink, v, th, regions);
    case PmSurface::None:
        break;
    }
    return true;
}

bool draw_under(Canvas& c, Fonts& f, InkLayer& ink, const PmView& v, const Theme& th,
                std::vector<AlphaRegion>& regions)
{
    static Buffer under, top;
    PmView u;
    u.surface = PmSurface::Pause;
    u.title = v.other.title;
    u.items = v.other.items;
    u.alpha = v.other.alpha;
    u.bar = v.other.bar;
    u.o = kOne;
    Canvas uc(under.surface());
    bool ok = draw_dialog(uc, f, ink, u, th, regions);
    int p = regions.back().a256;
    Canvas tc(top.surface());
    tc.fill(tc.surface().rect(), 0);
    std::vector<AlphaRegion> tr;
    ok = draw_top(tc, f, ink, v, th, tr) && ok;
    Surface us = under.surface(), ts = top.surface(), cs = c.surface();
    Rect sr = cs.rect().intersect(ts.rect()).intersect(us.rect());
    for (int y = sr.y0; y < sr.y1; y++)
        for (int x = sr.x0; x < sr.x1; x++)
            cs.at(x, y) = us.at(x, y);
    for (const AlphaRegion& g : tr) {
        int a = std::clamp(g.a256, 0, 256);
        int all = 256 - ((256 - p) * (256 - a) >> 8);
        regions.push_back({g.r, all});
        Rect r = g.r.intersect(sr);
        if (a == 0) {
            for (int y = r.y0; y < r.y1; y++)
                for (int x = r.x0; x < r.x1; x++)
                    cs.at(x, y) = us.at(x, y);
            continue;
        }
        if (a == 256) {
            for (int y = r.y0; y < r.y1; y++)
                for (int x = r.x0; x < r.x1; x++)
                    cs.at(x, y) = ts.at(x, y);
            continue;
        }
        uint32_t ku = (uint32_t)((int64_t)p * (256 - a) * 256 / all), kt = (uint32_t)((int64_t)a * 65536 / all);
        for (int y = r.y0; y < r.y1; y++)
            for (int x = r.x0; x < r.x1; x++) {
                uint32_t pu = LiveF::unpack(us.at(x, y)), po = LiveF::unpack(ts.at(x, y)), out = 0;
                for (int sh = 0; sh <= 16; sh += 8) {
                    uint32_t ch = (((pu >> sh) & 0xFF) * ku + ((po >> sh) & 0xFF) * kt + 32768) >> 16;
                    out |= std::min<uint32_t>(ch, 255) << sh;
                }
                cs.at(x, y) = LiveF::pack(out);
            }
    }
    return ok;
}

}

bool draw_powermenu(Canvas& c, Fonts& f, InkLayer& ink, const PmView& v, const Theme& th,
                    std::vector<AlphaRegion>& regions)
{
    regions.clear();
    c.fill(c.surface().rect(), 0);
    if (v.under)
        return draw_under(c, f, ink, v, th, regions);
    return draw_top(c, f, ink, v, th, regions);
}

}
