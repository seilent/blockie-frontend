#include "screen_settings.h"

#include <algorithm>

#include "../core/curves.h"
#include "../ui/rows.h"

namespace fd {

namespace {

constexpr const char* kWord = "settings";
constexpr const char* kPivot = "display";
constexpr const char* kThemeTitle = "theme";
constexpr const char* kThemeValues[2] = {"dark", "light"};

constexpr fx kHeaderBaseline = 66 * kOne;
constexpr fx kHeaderBand = 70 * kOne;
constexpr fx kPivotBaseline = (58 + 21) * kOne;
constexpr fx kRowsTop = 98 * kOne;

int sheet_ms() { return fx_round(tok::motion_sheet); }

int u64(fx u) { return (to_px(u) + 512) >> 10; }

}

void draw_page_ground(Canvas& c, const Theme& th) { c.fill(c.surface().rect(), th.bg); }

void draw_page_chrome(Canvas& c, Fonts& f, const char* word, const char* pivot, const Theme& th)
{
    Rect keep = c.clip();
    c.set_clip(Rect{0, 0, kPortraitW, px(kHeaderBand)}.intersect(keep));
    f.draw(c, role::hubHeader.style(), word, u64(tok::hubEdge), px(kHeaderBaseline), th.ink,
           alpha256(role::hubHeader.alpha));
    c.set_clip(keep);
    if (pivot && *pivot)
        f.draw(c, role::pivotActive.style(), pivot, u64(tok::hubEdge), px(kPivotBaseline), th.ink,
               alpha256(role::pivotActive.alpha));
}

void draw_value_sheet(Canvas& c, Fonts& f, const SheetView& s, const Theme& th)
{
    if (s.progress <= 0)
        return;
    Rect sr = c.surface().rect();
    int a = alpha256(s.progress);
    c.blend(sr, th.scrim_sheet, th.scrim_sheet_a256 * a >> 8);

    int edge = px(tok::edge);
    int pitch = px(tok::settingsPitch);
    TextStyle ts = role::dialogTitle.style();
    int title_h = f.ascent(ts.px) + f.descent(ts.px);
    int shown = std::max(1, std::min(s.count, (sr.y1 - 2 * edge - title_h) / std::max(1, pitch)));
    int panel_h = edge + title_h + shown * pitch + edge;
    int travel = panel_h - fx_round(fx_mul(fx_int(panel_h), s.progress));
    int top = sr.y1 - panel_h + travel;
    Rect panel{sr.x0, top, sr.x1, top + panel_h};
    c.blend(panel, th.bg_elevated, a);

    f.draw(c, ts, s.title, u64(tok::edge), top + edge + f.ascent(ts.px), th.ink,
           alpha256(role::dialogTitle.alpha) * a >> 8);

    int rows_top = top + edge + title_h;
    TextStyle is = role::dialogItem.style();
    int asc = f.ascent(is.px);
    int desc = f.descent(is.px);
    for (int i = 0; i < shown; i++) {
        int rt = rows_top + i * pitch;
        int base = rt + (pitch - asc - desc) / 2 + asc;
        fx al = i == s.focus ? role::dialogItem.alpha : tok::type_dialogItem_alphaDim;
        f.draw(c, is, s.items[i], u64(tok::edge), base, th.ink, alpha256(al) * a >> 8);
    }
    if (s.focus >= 0 && s.focus < shown) {
        int x = px(tok::edge - tok::focus_barOffset);
        fx row = s.bar >= 0 ? s.bar : fx_int(s.focus);
        int y = rows_top + fx_round(row * pitch);
        c.blend({x, y, x + px(tok::focus_barWidth), y + pitch}, th.focus, a);
    }
}

void draw_settings(Canvas& c, Fonts& f, InkLayer& ink, const SettingsView& v, const Theme& th)
{
    draw_page_ground(c, th);
    draw_page_chrome(c, f, kWord, kPivot, th);

    int top = px(kRowsTop);
    int pitch = px(tok::settingsPitch);
    Rect sr = c.surface().rect();
    Rect area{sr.x0, top, sr.x1, top + pitch};
    Canvas& lc = ink.begin(area);
    draw_settings_row(lc, f, role::settingsItem, kThemeTitle, kThemeValues[v.light ? 1 : 0], top,
                      v.focus == 0 ? kOne : 0);
    draw_focus_bar(lc, top + v.focus * pitch, tok::settingsPitch);
    ink.compose(c.surface(), th.ink);

    SheetView s;
    s.title = kThemeTitle;
    s.items = kThemeValues;
    s.count = 2;
    s.focus = v.sheet_focus;
    s.bar = v.sheet_bar;
    s.progress = v.sheet;
    draw_value_sheet(c, f, s, th);
}

void SettingsPage::reset(bool light)
{
    focus_ = 0;
    light_ = light;
    sheet_ = false;
    sheet_focus_ = light ? 1 : 0;
    sheet_at_ = -kFar;
}

bool SettingsPage::animating(int64_t now) const
{
    return now - sheet_at_ < sheet_ms() || now - bar_at_ < fx_round(tok::motion_focusCursor);
}

fx SettingsPage::bar_at(int64_t now) const
{
    int d = fx_round(tok::motion_focusCursor);
    int64_t e = now - bar_at_;
    fx to = fx_int(sheet_focus_);
    if (e >= d || e < 0)
        return to;
    fx k = curve(Curve::Sine, (fx)(e * kOne / d));
    return bar_from_ + fx_mul(to - bar_from_, k);
}

bool SettingsPage::action(PadAction a, bool repeat, int64_t now)
{
    land();
    if (sheet_) {
        if (a == PadAction::Up || a == PadAction::Down) {
            bar_from_ = bar_at(now);
            sheet_focus_ = (sheet_focus_ + 1) % 2;
            bar_at_ = now;
            return false;
        }
        if (repeat)
            return false;
        if (a == PadAction::Accept) {
            bool light = sheet_focus_ == 1;
            sheet_ = false;
            sheet_at_ = now;
            if (light != light_) {
                light_ = light;
                if (on_theme)
                    on_theme(light);
            }
            return false;
        }
        if (a == PadAction::Back) {
            sheet_ = false;
            sheet_at_ = now;
        }
        return false;
    }
    if (repeat)
        return false;
    if (a == PadAction::Accept && focus_ == 0) {
        sheet_ = true;
        sheet_focus_ = light_ ? 1 : 0;
        sheet_at_ = now;
        return false;
    }
    return a == PadAction::Back;
}

SettingsView SettingsPage::view(int64_t now) const
{
    SettingsView v;
    v.focus = focus_;
    v.light = light_;
    v.sheet_focus = sheet_focus_;
    v.sheet_bar = bar_at(now);
    int64_t e = now - sheet_at_;
    int d = sheet_ms();
    fx t = e >= d || e < 0 ? kOne : (fx)(e * kOne / d);
    fx p = curve(Curve::Decel, t);
    v.sheet = sheet_ ? p : kOne - p;
    return v;
}

}
