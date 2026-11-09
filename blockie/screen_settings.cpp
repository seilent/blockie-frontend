#include "screen_settings.h"

#include <algorithm>
#include <iterator>
#include <utility>

#include "../core/curves.h"
#include "../ui/rows.h"

namespace fd {

namespace {

constexpr const char* kWord = "settings";
constexpr const char* kPivot = "display";
constexpr const char* kThemeTitle = "theme";
constexpr const char* kThemeValues[2] = {"dark", "light"};
constexpr const char* kZoneTitle = "time zone";

constexpr fx kHeaderBaseline = 66 * kOne;
constexpr fx kHeaderBand = 70 * kOne;
constexpr fx kPivotBaseline = (58 + 21) * kOne;
constexpr fx kRowsTop = 98 * kOne;

int sheet_ms() { return fx_round(tok::motion_sheet); }

int u64(fx u) { return (to_px(u) + 512) >> 10; }

fx glide(fx from, int to, int64_t at, int64_t now)
{
    int d = fx_round(tok::motion_focusCursor);
    int64_t e = now - at;
    if (e >= d || e < 0)
        return fx_int(to);
    fx k = curve(Curve::Sine, (fx)(e * kOne / d));
    return from + fx_mul(fx_int(to) - from, k);
}

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

int settings_row_y(int row) { return px(kRowsTop) + row * px(tok::settingsPitch); }

int value_sheet_rows(Fonts& f)
{
    TextStyle ts = role::dialogTitle.style();
    int title_h = f.ascent(ts.px) + f.descent(ts.px);
    return std::max(1, (kPortraitH - 2 * px(tok::edge) - title_h) / std::max(1, px(tok::settingsPitch)));
}

int sheet_rows(Fonts& f, int want) { return std::min(want, value_sheet_rows(f)); }

SheetGeom sheet_geom(Fonts& f, const SheetView& s, Rect sr)
{
    SheetGeom g;
    int edge = px(tok::edge);
    g.pitch = px(tok::settingsPitch);
    TextStyle ts = role::dialogTitle.style();
    int title_h = f.ascent(ts.px) + f.descent(ts.px);
    int fit = (sr.y1 - 2 * edge - title_h) / std::max(1, g.pitch);
    g.shown = std::max(1, std::min({s.count, fit, s.rows > 0 ? s.rows : fit}));
    int panel_h = edge + title_h + g.shown * g.pitch + edge;
    int travel = panel_h - fx_round(fx_mul(fx_int(panel_h), s.progress));
    g.top = sr.y1 - panel_h + travel;
    g.rows_top = g.top + edge + title_h;
    return g;
}

void draw_value_sheet(Canvas& c, Fonts& f, const SheetView& s, const Theme& th)
{
    if (s.progress <= 0)
        return;
    Rect sr = c.surface().rect();
    Rect keep = c.clip();
    int a = s.opaque ? 256 : alpha256(s.progress);
    c.blend(sr, th.scrim_sheet, th.scrim_sheet_a256 * a >> 8);

    int edge = px(tok::edge);
    SheetGeom g = sheet_geom(f, s, sr);
    int pitch = g.pitch;
    int shown = g.shown;
    int top = g.top;
    TextStyle ts = role::dialogTitle.style();
    Rect panel{sr.x0, top, sr.x1, g.rows_top + shown * pitch + edge};
    c.blend(panel, th.bg_elevated, a);

    f.draw(c, ts, s.title, u64(tok::edge), top + edge + f.ascent(ts.px), th.ink,
           alpha256(role::dialogTitle.alpha) * a >> 8);

    int rows_top = g.rows_top;
    TextStyle is = role::dialogItem.style();
    int asc = f.ascent(is.px);
    int desc = f.descent(is.px);
    for (int i = 0; i < shown && s.first + i < s.count; i++) {
        int k = s.first + i;
        int rt = rows_top + i * pitch;
        int base = rt + (pitch - asc - desc) / 2 + asc;
        fx al = k == s.focus || k == s.mark ? role::dialogItem.alpha : tok::type_dialogItem_alphaDim;
        c.set_clip(Rect{sr.x0, rt, sr.x1 - edge, rt + pitch}.intersect(keep));
        f.draw(c, is, s.items[k], u64(tok::edge), base, th.ink, alpha256(al) * a >> 8);
    }
    c.set_clip(keep);
    if (s.focus >= s.first && s.focus < s.first + shown) {
        int x = px(tok::edge - tok::focus_barOffset);
        fx row = (s.bar >= 0 ? s.bar : fx_int(s.focus)) - fx_int(s.first);
        int y = rows_top + (int)(((int64_t)row * pitch + kHalf) >> kFxBits);
        c.set_clip(Rect{sr.x0, rows_top, sr.x1, rows_top + shown * pitch}.intersect(keep));
        c.blend({x, y, x + px(tok::focus_barWidth), y + pitch}, th.focus, a);
        c.set_clip(keep);
    }
}

void draw_settings(Canvas& c, Fonts& f, InkLayer& ink, const SettingsView& v, const Theme& th)
{
    draw_page_ground(c, th);
    draw_page_chrome(c, f, kWord, kPivot, th);

    int top = px(kRowsTop);
    int pitch = px(tok::settingsPitch);
    Rect sr = c.surface().rect();
    Rect area{sr.x0, top, sr.x1, top + (v.zone_row ? 2 : 1) * pitch};
    Canvas& lc = ink.begin(area);
    draw_settings_row(lc, f, role::settingsItem, kThemeTitle, kThemeValues[v.light ? 1 : 0], top,
                      v.focus == 0 ? kOne : 0);
    if (v.zone_row)
        draw_settings_row(lc, f, role::settingsItem, kZoneTitle, v.zone, top + pitch, v.focus == 1 ? kOne : 0);
    fx bar = v.bar >= 0 ? v.bar : fx_int(v.focus);
    draw_focus_bar(lc, top + (int)(((int64_t)bar * pitch + kHalf) >> kFxBits), tok::settingsPitch);
    ink.compose(c.surface(), th.ink);

    draw_value_sheet(c, f, v.sheet, th);
}

void SettingsPage::reset(bool light)
{
    focus_ = 0;
    light_ = light;
    sheet_ = false;
    kind_ = Kind::Theme;
    items();
    sheet_focus_ = light ? 1 : 0;
    first_ = 0;
    land();
}

void SettingsPage::set_zones(Zones z)
{
    zones_ = std::move(z);
    if (kind_ != Kind::Theme) {
        sheet_ = false;
        sheet_at_ = -kFar;
        kind_ = Kind::Theme;
        sheet_focus_ = light_ ? 1 : 0;
        first_ = 0;
    }
    items();
    zone_changed();
}

void SettingsPage::set_zone(const std::string& id)
{
    zone_ = id;
    zone_row_ = true;
    zone_changed();
}

void SettingsPage::set_zone_missing(int64_t now)
{
    zone_row_ = false;
    if (focus_ == 1) {
        focus_ = 0;
        page_bar_at_ = -kFar;
    }
    if (sheet_ && kind_ != Kind::Theme) {
        land();
        sheet_ = false;
        sheet_at_ = now;
    }
}

void SettingsPage::zone_changed()
{
    zone_words_ = zone_words(zones_, zone_);
    spot_ = locate_zone(zones_, zone_);
}

void SettingsPage::items()
{
    items_.clear();
    if (kind_ == Kind::Theme) {
        items_.assign(std::begin(kThemeValues), std::end(kThemeValues));
    } else if (kind_ == Kind::Region) {
        for (const ZoneRegion& r : zones_.regions)
            items_.push_back(r.words.c_str());
        items_.push_back(kUtcWords);
    } else {
        for (const ZoneEntry& e : zones_.regions[(size_t)region_].zones)
            items_.push_back(e.words.c_str());
    }
}

int SettingsPage::mark() const
{
    if (kind_ == Kind::Theme)
        return light_ ? 1 : 0;
    if (kind_ == Kind::Region)
        return spot_.region;
    return spot_.region == region_ ? spot_.zone : -1;
}

void SettingsPage::fit()
{
    int n = count();
    int shown = std::min(n, rows_);
    first_ = std::clamp(first_, std::max(0, sheet_focus_ - shown + 1), std::max(0, std::min(sheet_focus_, n - shown)));
}

void SettingsPage::open(Kind k, int focus, int64_t now)
{
    kind_ = k;
    items();
    sheet_focus_ = std::clamp(focus, 0, std::max(0, count() - 1));
    first_ = 0;
    fit();
    sheet_ = true;
    sheet_at_ = now;
    bar_at_ = -kFar;
}

void SettingsPage::move(int to, int64_t now, bool wrap)
{
    int old = sheet_focus_, old_first = first_;
    bar_from_ = bar_at(now);
    sheet_focus_ = to;
    bar_at_ = now;
    fit();
    if (wrap)
        bar_from_ = fx_int(old - old_first + first_);
}

void SettingsPage::pick(const std::string& id, int64_t now)
{
    sheet_ = false;
    sheet_at_ = now;
    if (id == zone_ || (zone_is_utc(id) && zone_is_utc(zone_)))
        return;
    zone_ = id;
    zone_changed();
    if (on_zone)
        on_zone(id);
}

void SettingsPage::accept(int64_t now)
{
    if (kind_ == Kind::Theme) {
        bool light = sheet_focus_ == 1;
        sheet_ = false;
        sheet_at_ = now;
        if (light != light_) {
            light_ = light;
            if (on_theme)
                on_theme(light);
        }
        return;
    }
    if (kind_ == Kind::Region) {
        if (sheet_focus_ < (int)zones_.regions.size()) {
            back_focus_ = sheet_focus_;
            back_first_ = first_;
            region_ = sheet_focus_;
            open(Kind::Zone, spot_.region == region_ ? spot_.zone : 0, now);
            return;
        }
        pick("UTC", now);
        return;
    }
    pick(zones_.regions[(size_t)region_].zones[(size_t)sheet_focus_].id, now);
}

bool SettingsPage::animating(int64_t now) const
{
    int cursor = fx_round(tok::motion_focusCursor);
    return now - sheet_at_ < sheet_ms() || now - bar_at_ < cursor || now - page_bar_at_ < cursor;
}

fx SettingsPage::bar_at(int64_t now) const { return glide(bar_from_, sheet_focus_, bar_at_, now); }

fx SettingsPage::page_bar(int64_t now) const { return glide(page_bar_from_, focus_, page_bar_at_, now); }

bool SettingsPage::action(PadAction a, bool repeat, int64_t now)
{
    land();
    if (sheet_) {
        if (a == PadAction::Up || a == PadAction::Down) {
            int n = count(), f = sheet_focus_;
            bool down = a == PadAction::Down;
            bool wrap = !repeat && (down ? f + 1 >= n : f == 0);
            if (down)
                f = f + 1 < n ? f + 1 : repeat ? f : 0;
            else
                f = f > 0 ? f - 1 : repeat ? f : n - 1;
            if (f != sheet_focus_)
                move(f, now, wrap);
            return false;
        }
        if (repeat)
            return false;
        if (a == PadAction::Accept) {
            accept(now);
            return false;
        }
        if (a == PadAction::Back) {
            if (kind_ == Kind::Zone) {
                kind_ = Kind::Region;
                items();
                sheet_focus_ = back_focus_;
                first_ = back_first_;
                fit();
                sheet_at_ = now;
            } else {
                sheet_ = false;
                sheet_at_ = now;
            }
        }
        return false;
    }
    if (a == PadAction::Up || a == PadAction::Down) {
        int last = zone_row_ ? 1 : 0;
        int f = a == PadAction::Down ? std::min(focus_ + 1, last) : std::max(focus_ - 1, 0);
        if (f != focus_) {
            page_bar_from_ = fx_int(focus_);
            focus_ = f;
            page_bar_at_ = now;
        }
        return false;
    }
    if (repeat)
        return false;
    if (a == PadAction::Accept) {
        if (focus_ == 0)
            open(Kind::Theme, light_ ? 1 : 0, now);
        else
            open(Kind::Region, spot_.region >= 0 ? spot_.region : 0, now);
        return false;
    }
    return a == PadAction::Back;
}

SettingsView SettingsPage::view(int64_t now) const
{
    SettingsView v;
    v.focus = focus_;
    v.bar = page_bar(now);
    v.light = light_;
    v.zone_row = zone_row_;
    v.zone = zone_words_;
    SheetView& s = v.sheet;
    s.title = kind_ == Kind::Theme ? kThemeTitle : kZoneTitle;
    s.items = items_.data();
    s.count = count();
    s.focus = sheet_focus_;
    s.bar = bar_at(now);
    s.first = first_;
    s.mark = mark();
    s.rows = rows_;
    s.open = sheet_;
    s.level = kind_ == Kind::Theme ? 0 : kind_ == Kind::Region ? 1 : 2;
    int64_t e = now - sheet_at_;
    int d = sheet_ms();
    fx t = e >= d || e < 0 ? kOne : (fx)(e * kOne / d);
    fx p = curve(Curve::Decel, t);
    s.progress = sheet_ ? p : kOne - p;
    return v;
}

}
