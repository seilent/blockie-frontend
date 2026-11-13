#include "screen_settings.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <utility>

#include "../core/curves.h"
#include "../gfx/compose.h"
#include "../ui/rows.h"
#include "leftmove.h"
#include "setd_key.h"

namespace fd {

const char* const kSettingsPivotWords[kSettingsPivots] = {"display", "sound", "controls", "games",
                                                         "network", "power", "system"};

namespace {

constexpr const char* kWord = "settings";
constexpr const char* kThemeTitle = "theme";
constexpr const char* kThemeValues[2] = {"dark", "light"};
constexpr const char* kZoneTitle = "time zone";
constexpr const char* kLevels[] = {"100", "95", "90", "85", "80", "75", "70", "65", "60", "55", "50",
                                   "45",  "40", "35", "30", "25", "20", "15", "10", "5",  "0"};

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

fx t12(int64_t at, bool open, int64_t now)
{
    int64_t e = now - at;
    int d = sheet_ms();
    fx t = e >= d || e < 0 ? kOne : (fx)(e * kOne / d);
    fx p = curve(Curve::Decel, t);
    return open ? p : kOne - p;
}

bool is_row(const SettingsRowSpec& s, const char* id) { return std::strcmp(s.id, id) == 0; }

void draw_rows(Canvas& c, Fonts& f, InkLayer& ink, const std::vector<SettingsRowView>& rows, int focus, fx bar,
               const Theme& th)
{
    if (rows.empty())
        return;
    int top = px(kRowsTop);
    int pitch = px(tok::settingsPitch);
    Rect sr = c.surface().rect();
    Rect area{sr.x0, top, sr.x1, top + (int)rows.size() * pitch};
    Canvas& lc = ink.begin(area);
    for (size_t i = 0; i < rows.size(); i++) {
        const SettingsRowView& r = rows[i];
        draw_settings_row(lc, f, r.kind == RowKind::Info ? role::settingsValue : role::settingsItem, r.title, r.value,
                          top + (int)i * pitch, (int)i == focus ? kOne : 0);
    }
    fx b = bar >= 0 ? bar : fx_int(focus);
    draw_focus_bar(lc, top + (int)(((int64_t)b * pitch + kHalf) >> kFxBits), tok::settingsPitch);
    ink.compose(c.surface(), th.ink);
}

}

void draw_page_pivots(Canvas& c, Fonts& f, InkLayer& ink, const char* word, const SettingsView& v, const Theme& th)
{
    if (v.pivots.empty())
        return;
    Chrome ch;
    ch.measure(f, word, v.pivots);
    ch.set_header_part(ChromePart{0, 0, kOne, true});
    int dy = px(tok::pivotBaseline) - px(tok::page_pivotBaseline);
    for (int i = 0; i < ch.pivots(); i++)
        ch.set_pivot_part(i, ChromePart{0, dy, kOne, false});
    ch.set_active(v.from_pivot >= 0 ? v.from_pivot : v.pivot);
    if (v.from_pivot >= 0)
        ch.switch_to(v.pivot, v.switch_at, v.dir);
    Rect sr = c.surface().rect();
    Canvas& k = ink.begin(Rect{sr.x0, px(tok::pivotTop), sr.x1, px(tok::pivotBottom)});
    ch.draw(k, f, v.now);
    ink.compose(c.surface(), th.ink);
}

void draw_page_ground(Canvas& c, const Theme& th) { c.fill(c.surface().rect(), th.bg); }

void draw_page_header(Canvas& c, Fonts& f, const char* word, fx band, const Theme& th)
{
    Rect keep = c.clip();
    c.set_clip(Rect{0, 0, kPortraitW, px(band)}.intersect(keep));
    f.draw(c, role::hubHeader.style(), word, u64(tok::hubEdge), px(kHeaderBaseline), th.ink,
           alpha256(role::hubHeader.alpha));
    c.set_clip(keep);
}

void draw_page_chrome(Canvas& c, Fonts& f, const char* word, const char* pivot, const Theme& th)
{
    draw_page_header(c, f, word, kHeaderBand, th);
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
    int lead_h = s.lead && *s.lead ? g.pitch : 0;
    int fit = (sr.y1 - 2 * edge - title_h - lead_h) / std::max(1, g.pitch);
    g.shown = std::max(1, std::min({s.count, fit, s.rows > 0 ? s.rows : fit}));
    int panel_h = edge + title_h + lead_h + g.shown * g.pitch + edge;
    int travel = panel_h - fx_round(fx_mul(fx_int(panel_h), s.progress));
    g.top = sr.y1 - panel_h + travel;
    g.rows_top = g.top + edge + title_h + lead_h;
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
    if (s.mixed)
        ts.tcase = Case::Mixed;
    Rect panel{sr.x0, top, sr.x1, g.rows_top + shown * pitch + edge};
    c.blend(panel, th.bg_elevated, a);

    f.draw(c, ts, s.title, u64(tok::edge), top + edge + f.ascent(ts.px), th.ink,
           alpha256(role::dialogTitle.alpha) * a >> 8);

    int rows_top = g.rows_top;
    if (s.lead && *s.lead) {
        TextStyle ls = role::settingsValue.style();
        int lt = rows_top - pitch;
        c.set_clip(Rect{sr.x0, lt, sr.x1 - edge, rows_top}.intersect(keep));
        f.draw(c, ls, s.lead, u64(tok::edge), lt + (pitch - f.ascent(ls.px) - f.descent(ls.px)) / 2 + f.ascent(ls.px),
               th.ink, alpha256(role::settingsValue.alpha) * a >> 8);
        c.set_clip(keep);
    }
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

std::vector<std::string> row_sheet_wrap(Fonts& f, const TextStyle& st, const std::string& s, int first64, int next64)
{
    if (f.width64(st, s.c_str()) <= first64)
        return {s};
    auto fits = [&](size_t n) { return f.width64(st, s.substr(0, n).c_str()) <= first64; };
    size_t space = std::string::npos;
    for (size_t i = s.find(' '); i != std::string::npos && fits(i); i = s.find(' ', i + 1))
        space = i;
    if (space != std::string::npos && f.width64(st, s.c_str() + space + 1) <= next64)
        return {s.substr(0, space), s.substr(space + 1)};
    if (space == std::string::npos && f.width64(st, s.c_str()) <= next64)
        return {std::string(), s};
    size_t end = 0;
    for (size_t i = 1; i <= s.size(); i++) {
        if (i < s.size() && ((unsigned char)s[i] & 0xC0) == 0x80)
            continue;
        if (!fits(i))
            break;
        end = i;
    }
    size_t rest = s.find_first_not_of(' ', end);
    return {s.substr(0, end), rest == std::string::npos ? std::string() : s.substr(rest)};
}

void draw_row_sheet(Canvas& c, Fonts& f, const RowSheetView& s, const Theme& th)
{
    if (s.progress <= 0 || s.lines.empty())
        return;
    Rect sr = c.surface().rect();
    Rect keep = c.clip();
    int a = alpha256(s.progress);
    c.blend(sr, th.scrim_sheet, th.scrim_sheet_a256 * a >> 8);

    int edge = px(tok::edge);
    int pitch = px(tok::settingsPitch);
    int gutter = px(tok::gutter);
    TextStyle ts = role::dialogTitle.style();
    TextStyle is = role::dialogItem.style();
    TextStyle vs = role::settingsValue.style();
    if (s.mixed)
        vs.tcase = Case::Mixed;
    int asc = f.ascent(is.px);
    int box = asc + f.descent(is.px);
    int title_h = f.ascent(ts.px) + f.descent(ts.px);
    struct Laid {
        std::vector<std::string> title;
        int vx64, right, h;
    };
    std::vector<Laid> laid;
    int body = 0;
    for (const InfoLine& l : s.lines) {
        Laid d;
        d.vx64 = std::max(edge * 64, (sr.x1 - edge) * 64 - f.width64(vs, l.value.c_str()));
        d.right = l.value.empty() ? sr.x1 - edge : (d.vx64 >> 6) - gutter;
        d.title = row_sheet_wrap(f, is, l.title, (d.right - edge) * 64, (sr.x1 - 2 * edge) * 64);
        d.h = pitch + (d.title.size() > 1 ? box : 0);
        if (edge + title_h + body + d.h + edge > sr.y1 - sr.y0)
            break;
        body += d.h;
        laid.push_back(std::move(d));
    }
    int panel_h = edge + title_h + body + edge;
    int top = sr.y1 - fx_round(fx_mul(fx_int(panel_h), s.progress));
    c.blend(Rect{sr.x0, top, sr.x1, top + panel_h}, th.bg_elevated, a);
    f.draw(c, ts, s.title, u64(tok::edge), top + edge + f.ascent(ts.px), th.ink,
           alpha256(role::dialogTitle.alpha) * a >> 8);

    int rt = top + edge + title_h;
    for (size_t i = 0; i < laid.size(); i++) {
        const Laid& d = laid[i];
        int base = rt + (pitch - box) / 2 + asc;
        c.set_clip(Rect{sr.x0, rt, sr.x1 - edge, rt + d.h}.intersect(keep));
        f.draw(c, vs, s.lines[i].value.c_str(), d.vx64, base, th.ink, alpha256(tok::settingsValueDim) * a >> 8);
        for (size_t k = 0; k < d.title.size(); k++) {
            c.set_clip(Rect{sr.x0, rt, k == 0 ? d.right : sr.x1 - edge, rt + d.h}.intersect(keep));
            f.draw(c, is, d.title[k].c_str(), u64(tok::edge), base + (int)k * box, th.ink,
                   alpha256(tok::type_dialogItem_alphaDim) * a >> 8);
        }
        rt += d.h;
    }
    c.set_clip(keep);
}

bool draw_toast(Canvas& c, Fonts& f, InkLayer& ink, const ToastView& v, const Theme& th, Rect* layer_strip)
{
    if (v.e <= 0)
        return true;
    Rect sr = c.surface().rect();
    int y = fx_round(to_px(fx_mul(-tok::motion_toastTravel, kOne - v.e)));
    int h = v.strip_h > 0 ? v.strip_h : px(tok::toastHeight);
    Rect strip = Rect{sr.x0, y, sr.x1, y + h}.intersect(sr);
    uint32_t ground = th.light ? th.bg : tok::color_black;
    int a = 256;
    if (layer_strip) {
        *layer_strip = strip;
        c.fill(strip, ground);
    } else {
        a = alpha256(fx_mul(v.e, tok::scrim_overlay_alpha));
        c.blend(strip, ground, a);
    }
    int edge = px(tok::edge);
    Canvas& k = ink.begin(strip);
    k.set_clip(Rect{edge, y, sr.x1 - edge, y + h}.intersect(k.clip()));
    bool ok = f.draw(k, role::toastArtist.style(), v.line1.c_str(), edge * 64, y + px(tok::toastArtistBaseline), kWhite,
                     alpha256(fx_mul(v.text, role::toastArtist.alpha)) * a >> 8);
    if (!v.line2.empty())
        ok = f.draw(k, role::toastTitle.style(), v.line2.c_str(), edge * 64, y + px(tok::toastTitleBaseline), kWhite,
                    alpha256(fx_mul(v.text, role::toastTitle.alpha)) * a >> 8) &&
             ok;
    ink.compose(c.surface(), th.ink);
    return ok;
}

std::string setd_fail_text(const std::string& error, const std::string& text)
{
    size_t n = std::strlen(SetdKey::kErr);
    std::string kind = error.compare(0, n, SetdKey::kErr) == 0 ? error.substr(n) : std::string();
    if (kind == "Invalid")
        return text.empty() ? "failed" : text;
    if (kind == "Denied" || kind == "ReadOnly")
        return "denied";
    if (kind == "Busy")
        return "busy";
    if (kind == "Timeout" || error == "org.freedesktop.DBus.Error.NoReply")
        return "timed out";
    if (kind == "Failed")
        return "failed";
    return "unavailable";
}

std::vector<SettingsRowSpec> settings_rows()
{
    std::vector<SettingsRowSpec> t;
    auto row = [&](const char* pivot, const char* title, RowKind kind, const char* id) -> SettingsRowSpec& {
        SettingsRowSpec r;
        r.pivot = pivot;
        r.title = title;
        r.kind = kind;
        r.id = id;
        t.push_back(r);
        return t.back();
    };
    auto key = [&](const char* pivot, const char* title, const char* id, SetdType type,
                   std::vector<const char*> words, std::vector<const char*> values) -> SettingsRowSpec& {
        SettingsRowSpec& r = row(pivot, title, RowKind::Sheet, id);
        r.words = std::move(words);
        r.values = std::move(values);
        r.setd = true;
        r.type = type;
        return r;
    };
    SettingsRowSpec& theme = row("display", kThemeTitle, RowKind::Sheet, kThemeRow);
    theme.words = {kThemeValues[0], kThemeValues[1]};
    theme.values = {kThemeValues[0], kThemeValues[1]};
    SettingsRowSpec& bright = key("display", "brightness", kBrightnessRow, SetdType::U,
                                  std::vector<const char*>(std::begin(kLevels), std::end(kLevels) - 2),
                                  std::vector<const char*>(std::begin(kLevels), std::end(kLevels) - 2));
    bright.kind = RowKind::Live;
    bright.bounded = true;
    SettingsRowSpec& vol = row("sound", "volume", RowKind::Live, kVolumeRow);
    vol.words.assign(std::begin(kLevels), std::end(kLevels));
    vol.values = vol.words;
    vol.bounded = true;
    key("sound", "volume step", "sound.volume_step", SetdType::U, {"10", "5", "2"}, {"10", "5", "2"});
    key("controls", "vibration", "controls.rumble", SetdType::U, {"100", "75", "50", "25", "off"},
        {"100", "75", "50", "25", "0"});
    key("games", "shaders", "games.shader", SetdType::B, {"on", "off"}, {"true", "false"});
    key("games", "integer scale", "games.integer_scale", SetdType::B, {"on", "off"}, {"true", "false"});
    key("games", "resume", "games.resume", SetdType::B, {"on", "off"}, {"true", "false"});
    key("games", "rewind", "games.rewind", SetdType::B, {"on", "off"}, {"true", "false"});
    SettingsRowSpec& radio = row("network", "wi-fi", RowKind::Sheet, kRadioRow);
    radio.words = {"on", "off"};
    radio.values = {"on", "off"};
    row("network", "bluetooth", RowKind::Push, kBluetoothRow);
    row("power", "battery", RowKind::Info, kBatteryRow);
    key("power", "screen off after", "power.screen_off", SetdType::U,
        {"30 min", "10 min", "5 min", "2 min", "1 min", "never"}, {"1800", "600", "300", "120", "60", "0"});
    row("system", kZoneTitle, RowKind::Sheet, kZoneRow);
    row("system", "storage", RowKind::RowSheet, kStorageRow);
    key("system", "android gpu", "android.gpu", SetdType::S, {"mali", "soft"}, {"mali", "soft"});
    key("system", "android performance", "android.perf", SetdType::S, {"on", "off"}, {"policy", "off"});
    row("system", "about", RowKind::RowSheet, kAboutRow).mixed = true;
    return t;
}

SettingsRowState volume_row(bool ok, int pct, bool muted)
{
    SettingsRowState st;
    st.shown = ok;
    if (ok)
        st.value = std::to_string(pct);
    if (ok && muted)
        st.text = "muted";
    return st;
}

SettingsRowState radio_row(bool up, bool on)
{
    SettingsRowState st;
    st.shown = up;
    if (up)
        st.value = on ? "on" : "off";
    return st;
}

SettingsRowState info_row(const std::string& text, std::vector<InfoLine> lines)
{
    SettingsRowState st;
    st.shown = true;
    st.text = text;
    st.lines = std::move(lines);
    return st;
}

bool sync_bt_row(SettingsPage& p, std::string& last, const std::string& sub, int64_t now)
{
    if (sub == last)
        return false;
    last = sub;
    p.set_row(kBluetoothRow, info_row(sub), now);
    return true;
}

int64_t settings_switch_ms() { return std::max<int64_t>(swap_ms(), fx_round(tok::motion_pivotStrip)); }

void settings_rows_switch(const Surface& from, const Surface& to, const Surface& dst, Rect area, int64_t t, int dir,
                          uint32_t ground)
{
    Rect r = area.intersect(dst.rect());
    Canvas dc(dst);
    dc.fill(r, ground);
    bool leaving = t < fx_round(tok::motion_swapOut);
    SwapLeg leg = leaving ? swap_leave(t, dir) : swap_arrive(t, dir);
    blend_band(leaving ? from : to, dst, r, leg.dx, leg.a256);
}

void draw_settings(Canvas& c, Fonts& f, InkLayer& ink, const SettingsView& v, const Theme& th)
{
    draw_page_ground(c, th);
    draw_page_chrome(c, f, kWord, nullptr, th);
    draw_page_pivots(c, f, ink, kWord, v, th);
    int64_t t = v.now - v.switch_at;
    if (v.from_pivot >= 0 && t >= 0 && t < settings_switch_ms()) {
        Rect sr = c.surface().rect();
        Rect area{sr.x0, px(kRowsTop), sr.x1, sr.y1};
        Buffer from;
        Buffer to;
        Canvas fc(from.surface());
        Canvas tc(to.surface());
        fc.fill(area, th.bg);
        tc.fill(area, th.bg);
        draw_rows(fc, f, ink, v.from_rows, v.from_focus, -kOne, th);
        draw_rows(tc, f, ink, v.rows, v.focus, v.bar, th);
        settings_rows_switch(from.surface(), to.surface(), c.surface(), area, t, v.dir, th.bg);
    } else {
        draw_rows(c, f, ink, v.rows, v.focus, v.bar, th);
    }
    draw_value_sheet(c, f, v.sheet, th);
    draw_row_sheet(c, f, v.rowsheet, th);
    draw_toast(c, f, ink, v.toast, th);
}

void SettingsPage::size_states() { states_.assign(table_.size(), SettingsRowState{}); }

void SettingsPage::set_table(std::vector<SettingsRowSpec> t)
{
    table_ = std::move(t);
    size_states();
    sheet_ = false;
    rowsheet_ = false;
    sheet_row_ = -1;
    rowsheet_row_ = -1;
    kind_ = Kind::Theme;
    items();
    std::fill(std::begin(pfocus_), std::end(pfocus_), 0);
    keep_pivot();
}

int SettingsPage::find_row(const std::string& id) const
{
    for (size_t i = 0; i < table_.size(); i++)
        if (id == table_[i].id)
            return (int)i;
    return -1;
}

bool SettingsPage::shown(int row) const
{
    const SettingsRowSpec& s = table_[(size_t)row];
    if (is_row(s, kThemeRow))
        return true;
    if (is_row(s, kZoneRow))
        return zone_row_;
    return states_[(size_t)row].shown;
}

int SettingsPage::pivot_of(int row) const
{
    for (int p = 0; p < kSettingsPivots; p++)
        if (std::strcmp(table_[(size_t)row].pivot, kSettingsPivotWords[p]) == 0)
            return p;
    return -1;
}

std::vector<int> SettingsPage::rows_of(int pivot) const
{
    std::vector<int> v;
    for (int i = 0; i < (int)table_.size(); i++)
        if (pivot_of(i) == pivot && shown(i))
            v.push_back(i);
    return v;
}

std::vector<int> SettingsPage::pivots_shown() const
{
    std::vector<int> v;
    for (int p = 0; p < kSettingsPivots; p++)
        if (!rows_of(p).empty())
            v.push_back(p);
    return v;
}

int SettingsPage::row_at(int pivot, int focus) const
{
    std::vector<int> r = rows_of(pivot);
    return focus >= 0 && focus < (int)r.size() ? r[(size_t)focus] : -1;
}

std::string SettingsPage::row_value(int row) const
{
    const SettingsRowSpec& s = table_[(size_t)row];
    if (is_row(s, kThemeRow))
        return kThemeValues[light_ ? 1 : 0];
    if (is_row(s, kZoneRow))
        return zone_words_;
    const SettingsRowState& st = states_[(size_t)row];
    if (!st.text.empty())
        return st.text;
    for (size_t k = 0; k < s.values.size() && k < s.words.size(); k++)
        if (st.value == s.values[k])
            return s.words[k];
    return st.value;
}

std::vector<SettingsRowView> SettingsPage::row_views(int pivot) const
{
    std::vector<SettingsRowView> v;
    for (int r : rows_of(pivot))
        v.push_back({table_[(size_t)r].title, row_value(r), table_[(size_t)r].kind});
    return v;
}

void SettingsPage::keep_pivot()
{
    for (int p = 0; p < kSettingsPivots; p++)
        pfocus_[p] = std::clamp(pfocus_[p], 0, std::max(0, (int)rows_of(p).size() - 1));
    std::vector<int> ps = pivots_shown();
    if (ps.empty() || std::find(ps.begin(), ps.end(), pivot_) != ps.end())
        return;
    int to = ps.front();
    for (int p : ps)
        if (p < pivot_)
            to = p;
    pivot_ = to;
    switch_at_ = -kFar;
    page_bar_at_ = -kFar;
}

void SettingsPage::shut_sheets(int row, int64_t now)
{
    if (sheet_ && sheet_row_ == row) {
        land();
        sheet_ = false;
        sheet_at_ = now;
        if (kind_ == Kind::Words && on_close)
            on_close(table_[(size_t)row].id);
    }
    if (rowsheet_ && rowsheet_row_ == row) {
        land();
        rowsheet_ = false;
        rowsheet_at_ = now;
    }
}

void SettingsPage::row_going(int row, int64_t now)
{
    int p = pivot_of(row);
    if (p >= 0) {
        std::vector<int> r = rows_of(p);
        int i = (int)(std::find(r.begin(), r.end(), row) - r.begin());
        if (i < pfocus_[p] || (i == pfocus_[p] && i > 0))
            pfocus_[p]--;
        if (p == pivot_)
            page_bar_at_ = -kFar;
    }
    shut_sheets(row, now);
}

void SettingsPage::row_coming(int row, bool had)
{
    int p = pivot_of(row);
    if (p < 0)
        return;
    std::vector<int> r = rows_of(p);
    int i = (int)(std::find(r.begin(), r.end(), row) - r.begin());
    if (!had)
        pfocus_[p] = 0;
    else if (i <= pfocus_[p])
        pfocus_[p]++;
}

void SettingsPage::set_row(const std::string& id, const SettingsRowState& st, int64_t now)
{
    int row = find_row(id);
    if (row < 0 || id == kThemeRow || id == kZoneRow)
        return;
    SettingsRowState& s = states_[(size_t)row];
    if (s.shown && !st.shown)
        row_going(row, now);
    bool coming = !s.shown && st.shown;
    int p = pivot_of(row);
    bool had = p >= 0 && !rows_of(p).empty();
    s = st;
    if (coming)
        row_coming(row, had);
    keep_pivot();
}

void SettingsPage::close_row(const std::string& id, int64_t now)
{
    int row = find_row(id);
    if (row >= 0)
        shut_sheets(row, now);
}

void SettingsPage::reset(bool light)
{
    std::fill(std::begin(pfocus_), std::end(pfocus_), 0);
    pivot_ = 0;
    light_ = light;
    sheet_ = false;
    rowsheet_ = false;
    sheet_row_ = -1;
    rowsheet_row_ = -1;
    kind_ = Kind::Theme;
    items();
    sheet_focus_ = light ? 1 : 0;
    first_ = 0;
    land();
    keep_pivot();
}

void SettingsPage::set_zones(Zones z)
{
    zones_ = std::move(z);
    if (kind_ == Kind::Region || kind_ == Kind::Zone) {
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
    if (!zone_row_) {
        int row = find_row(kZoneRow);
        int p = row >= 0 ? pivot_of(row) : -1;
        bool had = p >= 0 && !rows_of(p).empty();
        zone_row_ = true;
        if (row >= 0)
            row_coming(row, had);
    }
    zone_changed();
    keep_pivot();
}

void SettingsPage::set_zone_missing(int64_t now)
{
    int row = find_row(kZoneRow);
    if (zone_row_ && row >= 0)
        row_going(row, now);
    zone_row_ = false;
    keep_pivot();
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
    } else if (kind_ == Kind::Zone) {
        for (const ZoneEntry& e : zones_.regions[(size_t)region_].zones)
            items_.push_back(e.words.c_str());
    } else if (sheet_row_ >= 0) {
        const SettingsRowSpec& s = table_[(size_t)sheet_row_];
        items_.assign(s.words.begin(), s.words.end());
    }
}

int SettingsPage::mark() const
{
    if (kind_ == Kind::Theme)
        return light_ ? 1 : 0;
    if (kind_ == Kind::Region)
        return spot_.region;
    if (kind_ == Kind::Zone)
        return spot_.region == region_ ? spot_.zone : -1;
    if (sheet_row_ < 0)
        return -1;
    const SettingsRowSpec& s = table_[(size_t)sheet_row_];
    const std::string& value = states_[(size_t)sheet_row_].value;
    for (size_t k = 0; k < s.values.size(); k++)
        if (value == s.values[k])
            return (int)k;
    if (s.kind != RowKind::Live || value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
        return -1;
    long v = std::strtol(value.c_str(), nullptr, 10);
    int best = -1;
    long gap = 0;
    for (size_t k = 0; k < s.values.size(); k++) {
        long d = std::labs(std::strtol(s.values[k], nullptr, 10) - v);
        if (best < 0 || d < gap || (d == gap && std::strtol(s.values[k], nullptr, 10) > v)) {
            best = (int)k;
            gap = d;
        }
    }
    return best;
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

void SettingsPage::open_words(int row, int64_t now)
{
    sheet_row_ = row;
    kind_ = Kind::Words;
    int m = mark();
    open(Kind::Words, m >= 0 ? m : 0, now);
    moved_ = false;
    if (table_[(size_t)row].kind == RowKind::Live && on_open)
        on_open(table_[(size_t)row].id);
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

void SettingsPage::close_sheet(bool restore, int64_t now)
{
    bool words = kind_ == Kind::Words && sheet_row_ >= 0;
    bool live = words && table_[(size_t)sheet_row_].kind == RowKind::Live;
    sheet_ = false;
    sheet_at_ = now;
    if (words && on_close)
        on_close(table_[(size_t)sheet_row_].id);
    if (restore && live && on_restore)
        on_restore(table_[(size_t)sheet_row_].id);
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
    if (kind_ == Kind::Zone) {
        pick(zones_.regions[(size_t)region_].zones[(size_t)sheet_focus_].id, now);
        return;
    }
    const SettingsRowSpec& s = table_[(size_t)sheet_row_];
    bool same = s.kind == RowKind::Live ? !moved_ : sheet_focus_ == mark();
    close_sheet(false, now);
    if (!same && on_pick && sheet_focus_ < (int)s.values.size())
        on_pick(s.id, s.values[(size_t)sheet_focus_]);
}

void SettingsPage::switch_pivot(int dir, int64_t now)
{
    std::vector<int> ps = pivots_shown();
    auto it = std::find(ps.begin(), ps.end(), pivot_);
    if (it == ps.end())
        return;
    int i = (int)(it - ps.begin()) + dir;
    if (i < 0 || i >= (int)ps.size())
        return;
    from_pivot_ = pivot_;
    pivot_ = ps[(size_t)i];
    dir_ = dir;
    switch_at_ = now;
    page_bar_at_ = -kFar;
}

int SettingsPage::focus_y() const { return settings_row_y(pfocus_[pivot_]) + px(tok::settingsPitch) / 2; }

std::string SettingsPage::title(const std::string& id) const
{
    int row = find_row(id);
    return row >= 0 ? table_[(size_t)row].title : id;
}

void SettingsPage::refused(const std::string& id, const std::string& error, const std::string& text, int64_t now)
{
    toast(now, title(id), setd_fail_text(error, text));
}

bool SettingsPage::animating(int64_t now) const
{
    int cursor = fx_round(tok::motion_focusCursor);
    if (now - sheet_at_ < sheet_ms() || now - bar_at_ < cursor || now - page_bar_at_ < cursor ||
        now - switch_at_ < settings_switch_ms() || now - rowsheet_at_ < sheet_ms())
        return true;
    return toast_.animating(now);
}

int64_t SettingsPage::deadline(int64_t now) const { return toast_.deadline(now); }

fx SettingsPage::bar_at(int64_t now) const { return glide(bar_from_, sheet_focus_, bar_at_, now); }

fx SettingsPage::page_bar(int64_t now) const { return glide(page_bar_from_, pfocus_[pivot_], page_bar_at_, now); }

bool SettingsPage::action(PadAction a, bool repeat, int64_t now)
{
    land();
    if (rowsheet_) {
        if (a == PadAction::Back && !repeat) {
            rowsheet_ = false;
            rowsheet_at_ = now;
        }
        return false;
    }
    if (sheet_) {
        if (a == PadAction::Up || a == PadAction::Down) {
            bool bounded = kind_ == Kind::Words && table_[(size_t)sheet_row_].bounded;
            int n = count(), f = sheet_focus_;
            bool down = a == PadAction::Down;
            bool stop = repeat || bounded;
            bool wrap = !stop && (down ? f + 1 >= n : f == 0);
            if (down)
                f = f + 1 < n ? f + 1 : stop ? f : 0;
            else
                f = f > 0 ? f - 1 : stop ? f : n - 1;
            if (f != sheet_focus_) {
                moved_ = true;
                move(f, now, wrap);
                const SettingsRowSpec* s = kind_ == Kind::Words ? &table_[(size_t)sheet_row_] : nullptr;
                if (s && s->kind == RowKind::Live && on_preview && f < (int)s->values.size())
                    on_preview(s->id, s->values[(size_t)f]);
            }
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
                close_sheet(true, now);
            }
        }
        return false;
    }
    if (a == PadAction::Prev || a == PadAction::Next) {
        if (!repeat)
            switch_pivot(a == PadAction::Next ? 1 : -1, now);
        return false;
    }
    if (a == PadAction::Up || a == PadAction::Down) {
        int last = std::max(0, (int)rows_of(pivot_).size() - 1);
        int& focus = pfocus_[pivot_];
        int f = a == PadAction::Down ? std::min(focus + 1, last) : std::max(focus - 1, 0);
        if (f != focus) {
            page_bar_from_ = fx_int(focus);
            focus = f;
            page_bar_at_ = now;
        }
        return false;
    }
    if (repeat)
        return false;
    if (a == PadAction::Accept) {
        int row = row_at(pivot_, pfocus_[pivot_]);
        if (row < 0)
            return false;
        const SettingsRowSpec& s = table_[(size_t)row];
        if (is_row(s, kThemeRow)) {
            sheet_row_ = row;
            open(Kind::Theme, light_ ? 1 : 0, now);
            return false;
        }
        if (is_row(s, kZoneRow)) {
            sheet_row_ = row;
            open(Kind::Region, spot_.region >= 0 ? spot_.region : 0, now);
            return false;
        }
        switch (s.kind) {
        case RowKind::Sheet:
        case RowKind::Live:
            open_words(row, now);
            break;
        case RowKind::Push:
            if (on_push)
                on_push(s.id);
            break;
        case RowKind::Action:
            if (on_action)
                on_action(s.id);
            break;
        case RowKind::RowSheet:
            rowsheet_ = true;
            rowsheet_row_ = row;
            rowsheet_at_ = now;
            break;
        case RowKind::Info:
            break;
        }
        return false;
    }
    return a == PadAction::Back;
}

SettingsView SettingsPage::view(int64_t now) const
{
    SettingsView v;
    std::vector<int> ps = pivots_shown();
    for (int p : ps)
        v.pivots.push_back(kSettingsPivotWords[p]);
    auto index = [&](int p) { return (int)(std::find(ps.begin(), ps.end(), p) - ps.begin()); };
    int pi = index(pivot_);
    v.pivot = pi < (int)ps.size() ? pi : 0;
    v.rows = row_views(pivot_);
    v.focus = pfocus_[pivot_];
    v.bar = page_bar(now);
    v.light = light_;
    v.zone_row = zone_row_;
    v.zone = zone_words_;
    v.now = now;
    v.dir = dir_;
    v.switch_at = switch_at_;
    int64_t e = now - switch_at_;
    int fi = index(from_pivot_);
    if (e >= 0 && e < settings_switch_ms() && fi < (int)ps.size() && from_pivot_ != pivot_) {
        v.from_pivot = fi;
        v.from_rows = row_views(from_pivot_);
        v.from_focus = pfocus_[from_pivot_];
    }
    SheetView& s = v.sheet;
    s.title = kind_ == Kind::Theme ? kThemeTitle
              : kind_ == Kind::Words && sheet_row_ >= 0 ? table_[(size_t)sheet_row_].title
                                                        : kZoneTitle;
    if (kind_ == Kind::Words && sheet_row_ >= 0)
        s.lead = table_[(size_t)sheet_row_].lead;
    s.items = items_.data();
    s.count = count();
    s.focus = sheet_focus_;
    s.bar = bar_at(now);
    s.first = first_;
    s.mark = mark();
    s.rows = rows_;
    s.open = sheet_;
    s.level = kind_ == Kind::Region ? 1 : kind_ == Kind::Zone ? 2 : 0;
    s.progress = t12(sheet_at_, sheet_, now);
    if (rowsheet_row_ >= 0) {
        RowSheetView& r = v.rowsheet;
        r.title = table_[(size_t)rowsheet_row_].title;
        r.lines = states_[(size_t)rowsheet_row_].lines;
        r.mixed = table_[(size_t)rowsheet_row_].mixed;
        r.open = rowsheet_;
        r.progress = t12(rowsheet_at_, rowsheet_, now);
    }
    if (toast_.phase(now) != 0) {
        ToastView& t = v.toast;
        t.line1 = toast_.line1(now);
        t.line2 = toast_.line2(now);
        t.e = toast_.eased(now);
        t.text = toast_.text(now);
    }
    return v;
}

}
