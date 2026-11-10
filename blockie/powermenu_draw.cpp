#include "powermenu.h"

#include "../ui/roles.h"

namespace fd {

namespace {

int index_px(fx at, int pitch) { return (int)(((int64_t)at * pitch + kHalf) >> kFxBits); }

bool draw_dialog(Canvas& c, Fonts& f, InkLayer& ink, const PmView& v, const Theme& th,
                 std::vector<AlphaRegion>& regions)
{
    Rect sr = c.surface().rect();
    c.fill(sr, th.scrim_dialog);
    regions.push_back({sr, th.scrim_dialog_a256 * alpha256(v.o) >> 8});
    Zoom z{v.s, sr.x1 * 32, sr.y1 * 32};
    int edge = px(tok::edge), pitch = px(tok::settingsPitch), top = px(tok::contentTop);
    Canvas& k = ink.begin(sr);
    Rect keep = k.clip();
    TextStyle ts = role::dialogTitle.style(), is = role::dialogItem.style();
    k.set_clip(z.rect(Rect{sr.x0, sr.y0, sr.x1 - edge, top}).intersect(keep));
    bool ok = f.draw(k, ts, v.title, edge * 64, px(tok::pivotBaseline), kWhite, alpha256(role::dialogTitle.alpha), z);
    int asc = f.ascent(is.px), desc = f.descent(is.px);
    for (size_t i = 0; i < v.items.size(); i++) {
        int rt = top + (int)i * pitch;
        k.set_clip(z.rect(Rect{sr.x0, rt, sr.x1 - edge, rt + pitch}).intersect(keep));
        ok = f.draw(k, is, v.items[i], edge * 64, rt + (pitch - asc - desc) / 2 + asc, kWhite, alpha256(v.alpha[i]),
                    z) && ok;
    }
    k.set_clip(keep);
    if (!v.items.empty()) {
        int bx = edge - px(tok::focus_barOffset), by = top + index_px(v.bar, pitch);
        k.fill_aa(Rect64::of({bx, by, bx + px(tok::focus_barWidth), by + pitch}, z), kWhite, 256);
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

bool draw_toast(Canvas& c, Fonts& f, InkLayer& ink, const PmView& v, const Theme& th,
                std::vector<AlphaRegion>& regions)
{
    if (v.e <= 0)
        return true;
    Rect sr = c.surface().rect();
    int y = fx_round(to_px(fx_mul(-tok::motion_toastTravel, kOne - v.e)));
    int h = v.strip_h > 0 ? v.strip_h : px(tok::toastHeight);
    Rect strip = Rect{sr.x0, y, sr.x1, y + h}.intersect(sr);
    c.fill(strip, th.light ? th.bg : tok::color_black);
    regions.push_back({strip, alpha256(fx_mul(v.e, tok::scrim_overlay_alpha))});
    int edge = px(tok::edge);
    Canvas& k = ink.begin(strip);
    k.set_clip(Rect{edge, y, sr.x1 - edge, y + h}.intersect(k.clip()));
    bool ok = f.draw(k, role::toastArtist.style(), v.line1.c_str(), edge * 64, y + px(tok::toastArtistBaseline), kWhite,
                     alpha256(fx_mul(v.text, role::toastArtist.alpha)));
    if (!v.line2.empty())
        ok = f.draw(k, role::toastTitle.style(), v.line2.c_str(), edge * 64, y + px(tok::toastTitleBaseline), kWhite,
                    alpha256(fx_mul(v.text, role::toastTitle.alpha))) && ok;
    ink.compose(c.surface(), th.ink);
    return ok;
}

}

bool draw_powermenu(Canvas& c, Fonts& f, InkLayer& ink, const PmView& v, const Theme& th,
                    std::vector<AlphaRegion>& regions)
{
    regions.clear();
    c.fill(c.surface().rect(), 0);
    switch (v.surface) {
    case PmSurface::Power:
    case PmSurface::Confirm:
        return draw_dialog(c, f, ink, v, th, regions);
    case PmSurface::Sheet:
        draw_sheet(c, f, v, th, regions);
        return true;
    case PmSurface::Toast:
        return draw_toast(c, f, ink, v, th, regions);
    case PmSurface::None:
        break;
    }
    return true;
}

}
