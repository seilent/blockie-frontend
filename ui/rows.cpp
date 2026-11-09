#include "rows.h"

#include <algorithm>
#include <cstring>

#include "../gfx/compose.h"
#include "../gfx/wrap.h"
#include "accent.h"
#include "size.h"

namespace fd {

namespace {

int to64(fx u) { return (to_px(u) + 512) >> 10; }
constexpr int std64(fx u) { return (std_to_px(u) + 512) >> 10; }

const Role& primary_role(const Row& r)
{
    return r.collection ? role::listCollection : r.bold ? sz().listPlaylist : sz().listPrimary;
}

const Role& secondary_role(const Row& r) { return r.count_line || r.bold ? sz().listCount : sz().listSecondary; }

const Role& count_role() { return sz().listCount; }

const Role& divider_role() { return role::letterGlyph; }

void ring(Canvas& c, Rect r, int w, int a)
{
    for (Rect q : {Rect{r.x0 - w, r.y0 - w, r.x1 + w, r.y0}, Rect{r.x0 - w, r.y1, r.x1 + w, r.y1 + w},
                   Rect{r.x0 - w, r.y0, r.x0, r.y1}, Rect{r.x1, r.y0, r.x1 + w, r.y1}})
        if (a >= 256)
            c.fill(q, kWhite);
        else
            c.blend(q, kWhite, a);
}

void inset_ring(Canvas& c, Rect t, fx center, fx width, int a)
{
    int in = px(center + width / 2);
    ring(c, t.grow(-in), std::max(1, in - px(center - width / 2)), a);
}

int number_column64(Fonts& f, size_t digits)
{
    static int digit64[2], gap64[2];
    int k = ppu() != tok::u;
    if (digit64[k] <= 0) {
        int d = 0, s = 0;
        for (char c = '0'; c <= '9'; c++) {
            const char t[2] = {c, 0};
            d = std::max(d, f.width64(sz().listCount.style(), t));
            s = std::max(s, f.width64(role::listCount.style(), t));
        }
        digit64[k] = d;
        gap64[k] = to64(tok::songIndent) - 4 * s;
    }
    return std::max(to64(sz().songIndent), (int)digits * digit64[k] + gap64[k]);
}

}

int row_title_x64(Fonts& f, const Row& r, bool ring)
{
    if (r.collection && ring)
        return to64(tok::rowCollectionTextX);
    if (ring)
        return to64(tok::hubEdge + sz().songIndent);
    if (!r.number.empty())
        return to64(tok::hubEdge) + number_column64(f, r.number.size());
    return to64(tok::hubEdge);
}

bool draw_clipped(Canvas& c, Fonts& f, const TextStyle& st, const std::string& s, int x64, int base, int a, Rect clip,
                  const Zoom& z, uint32_t color)
{
    Rect keep = c.clip();
    c.set_clip(z.rect(clip).intersect(keep));
    bool ok = c.clip().empty() || f.draw(c, st, s.c_str(), x64, base, color, a, z);
    c.set_clip(keep);
    return ok;
}

void Chrome::measure(Fonts& f, const char* word, std::vector<const char*> pivots)
{
    word_ = word;
    names_ = std::move(pivots);
    clear_parts();
    clear();
    header_w64_ = f.width64(role::pageHeader.std_style(), word);
    for (int k = 0; k < 2; k++) {
        w64_[k].clear();
        std64_[k].clear();
        ink_[k].clear();
        std_ink_[k].clear();
    }
    for (const char* n : names_) {
        w64_[0].push_back(f.width64(role::pivotInactive.style(), n));
        w64_[1].push_back(f.width64(role::pivotActive.style(), n));
        std64_[0].push_back(f.width64(role::pivotInactive.std_style(), n));
        std64_[1].push_back(f.width64(role::pivotActive.std_style(), n));
        for (int k = 0; k < 2; k++) {
            const Role& r = k ? role::pivotActive : role::pivotInactive;
            int l, rt;
            f.ink64(r.style(), n, l, rt);
            ink_[k].push_back({l, rt});
            f.ink64(r.std_style(), n, l, rt);
            std_ink_[k].push_back({l, rt});
        }
    }
    active_ = 0;
}

fx Chrome::gap(int k) const
{
    return k == active_ - 1 ? tok::pivotLeadGap : k == active_ ? tok::pivotActiveGap : tok::pivotGap;
}

int Chrome::pos64(int i) const
{
    int x = 0;
    for (int k = 0; k < i; k++)
        x += k == active_ - 1 ? w64_[0][k] + to64(gap(k))
                              : ink_[k == active_][k].second + to64(gap(k)) - ink_[k + 1 == active_][k + 1].first;
    return x;
}

int Chrome::rest_row64() const { return to64(tok::hubEdge) - pos64(active_); }

int Chrome::pivot_x64(int i) const { return rest_row64() + pos64(i); }

bool Chrome::moving(int64_t now) const { return now - at_ < (tok::motion_pivotStrip >> kFxBits); }

int Chrome::row64(int64_t now) const
{
    int dur = tok::motion_pivotStrip >> kFxBits;
    int64_t e = now - at_;
    if (e >= dur || e < 0)
        return rest_row64();
    fx p = kOne - curve(Curve::CubicOut, (fx)(e * kOne / dur));
    return rest_row64() + dir_ * (int)(((int64_t)to64(tok::motion_pivotStripShift) * p) >> kFxBits);
}

int Chrome::std_pos64(int i) const
{
    int x = 0;
    for (int k = 0; k < i; k++)
        x += k == active_ - 1
                 ? std64_[0][k] + std64(gap(k))
                 : std_ink_[k == active_][k].second + std64(gap(k)) - std_ink_[k + 1 == active_][k + 1].first;
    return x;
}

int Chrome::std_row64(int64_t now) const
{
    int dur = tok::motion_pivotStrip >> kFxBits, rest = std64(tok::hubEdge) - std_pos64(active_);
    int64_t e = now - at_;
    if (e >= dur || e < 0)
        return rest;
    fx p = kOne - curve(Curve::CubicOut, (fx)(e * kOne / dur));
    return rest + dir_ * (int)(((int64_t)std64(tok::motion_pivotStripShift) * p) >> kFxBits);
}

int Chrome::header_x64(int std_row) const
{
    int edge = std64(tok::hubEdge);
    return std64(header_x_) + (int)(((int64_t)(std_row - edge) * tok::motion_pivotHeaderParallax) >> kFxBits);
}

void Chrome::switch_to(int i, int64_t now, int dir)
{
    if (i == active_)
        return;
    from_active_ = active_;
    active_ = i;
    dir_ = dir;
    at_ = now;
}

bool Chrome::draw(Canvas& c, Fonts& f, int64_t now) const
{
    bool ok = true;
    Rect keep = c.clip();
    int row = row64(now);
    if (!header_part_.hidden) {
        int hy = header_part_.dy;
        c.set_clip(Rect{0, hy, kPortraitW, hy + std_px(tok::pivotTop)}.intersect(keep));
        fx ha = lit(kHeader, now) ? tok::alpha_full : header_alpha_;
        ok = f.draw(c, role::pageHeader.std_style(), word_.c_str(), header_x64(std_row64(now)) + header_part_.dx64,
                    std_px(tok::page_headerBaseline) + hy, tok::color_pageHeader,
                    alpha256(fx_mul(ha, header_part_.alpha))) &&
             ok;
        c.set_clip(keep);
    }
    int label = tok::motion_pivotLabel >> kFxBits, strip = tok::motion_pivotStrip >> kFxBits;
    int64_t e = now - at_;
    fx lp = e >= label || e < 0 ? kOne : (fx)(e * kOne / label);
    fx sa = e >= strip || e < 0 ? kOne : fx_lerp(tok::motion_pivotStripAlpha, kOne, (fx)(e * kOne / strip));
    for (int i = 0; i < pivots(); i++) {
        const ChromePart& part = pivot_parts_[(size_t)i];
        if (part.hidden)
            continue;
        const Role& r = i == active_ ? role::pivotActive : role::pivotInactive;
        fx on = i == active_ ? (i != from_active_ ? lp : kOne) : i == from_active_ ? kOne - lp : 0;
        fx la = fx_mul(lit(i, now) ? tok::alpha_full
                                   : fx_mul(fx_lerp(role::pivotInactive.alpha, role::pivotActive.alpha, on), sa),
                       part.alpha);
        uint32_t rgb = blend_px(tok::color_pivotInactive, kWhite, alpha256(on));
        ok = f.draw(c, r.style(), names_[i], row + pos64(i) + part.dx64, px(tok::page_pivotBaseline) + part.dy, rgb,
                    alpha256(la)) &&
             ok;
    }
    return ok;
}

void Chrome::clear_parts()
{
    header_part_ = {};
    pivot_parts_.assign(names_.size(), {});
}

int Chrome::pivot_hit(fx x, fx y) const
{
    if (y < tok::pivotTop || y >= tok::pivotBottom)
        return -1;
    int64_t x64 = (int64_t)to_px(x) >> 10;
    for (int i = 0; i < pivots(); i++) {
        int x0 = pivot_x64(i), x1 = x0 + w64_[i == active_][i];
        if (x64 >= x0 && x64 < x1)
            return i;
    }
    return -1;
}

int Chrome::header_x64() const { return header_x64(std64(tok::hubEdge) - std_pos64(active_)); }

bool Chrome::header_hit(fx x, fx y) const
{
    int64_t x64 = (int64_t)to_px(x) >> 10;
    int hx = header_x64();
    return y < kept_u(tok::headerBand) && x64 >= hx && x64 < hx + header_w64_;
}

void PivotMotion::begin(int64_t now, int dir)
{
    at_ = now;
    dir_ = dir;
    in_ready_ = false;
}

bool PivotMotion::active(int64_t now) const
{
    int total = ((tok::motion_pivotOut + std::max(tok::motion_pivotRows, tok::motion_pivotRowFade)) >> kFxBits) +
                (tok::motion_pivotRowCap >> kFxBits) * (tok::motion_pivotRowStagger >> kFxBits);
    return at_ >= 0 && now - at_ < total;
}

bool PivotMotion::entering(int64_t now) const { return active(now) && now - at_ >= (tok::motion_pivotOut >> kFxBits); }

int64_t PivotMotion::in_ms(int64_t now, int row) const
{
    int stagger = tok::motion_pivotRowStagger >> kFxBits, cap = tok::motion_pivotRowCap >> kFxBits;
    return now - at_ - (tok::motion_pivotOut >> kFxBits) - (int64_t)std::min(row, cap) * stagger;
}

fx PivotMotion::alpha(int64_t now, int row) const
{
    int out = tok::motion_pivotOut >> kFxBits, fade = tok::motion_pivotRowFade >> kFxBits;
    int64_t e = now - at_;
    if (e < out)
        return kOne - curve(Curve::Strip, (fx)(e * kOne / out));
    int64_t k = in_ms(now, row);
    return k <= 0 ? 0 : k >= fade ? kOne : (fx)(k * kOne / fade);
}

Thumb PivotMotion::thumb(const Thumb& out, const Thumb& in, int64_t now) const
{
    if (!active(now))
        return in;
    return (entering(now) ? in : out).faded(alpha(now, 0));
}

void PivotMotion::compose(const Surface& layer, Canvas& c, Rect area, pos64 top, fx pitch, pos64 off, int first, int last,
                          int64_t now) const
{
    Rect clip = area.intersect(c.clip());
    for (int r = first; r <= last; r++)
        blend_row(layer, c, clip, top + (pos64)r * pitch - off, pitch, r - first, now);
}

void PivotMotion::compose(const Surface& layer, Canvas& c, Rect area, pos64 top, const std::vector<fx>& tops,
                          const std::vector<fx>& heights, pos64 off, int first, int last, int64_t now) const
{
    Rect clip = area.intersect(c.clip());
    for (int r = first; r <= last; r++)
        blend_row(layer, c, clip, top + (pos64)tops[(size_t)r] - off, heights[(size_t)r], r - first, now);
}

void PivotMotion::blend_row(const Surface& layer, Canvas& c, Rect clip, pos64 y, fx h, int row, int64_t now) const
{
    int y0 = (int)((((y * ppu()) >> kFxBits) + kHalf) >> kFxBits);
    int y1 = (int)(((((y + h) * ppu()) >> kFxBits) + kHalf) >> kFxBits);
    fx p = alpha(now, row);
    int dx;
    if (now - at_ < (tok::motion_pivotOut >> kFxBits)) {
        dx = -dir_ * fx_round(to_px(fx_mul(tok::motion_pivotShift, kOne - p)));
    } else {
        int rows = tok::motion_pivotRows >> kFxBits;
        int64_t k = std::clamp<int64_t>(in_ms(now, row), 0, rows);
        fx s = kOne - curve(Curve::CubicOut, (fx)(k * kOne / rows));
        dx = dir_ * fx_round(to_px(fx_mul(tok::motion_pivotRowInShift, s)));
    }
    blend_band(layer, c.surface(), Rect{0, y0, kPortraitW, y1}.intersect(clip), dx, alpha256(p));
}

int collection_base(int top, const TextStyle& name)
{
    return top + px(tok::rowTextInset) + fx_round(fx_mul(tok::capHeight, fx_int(name.px)));
}

int list_row_base(Fonts& f, const Row& r, int top, const TextStyle& name)
{
    int h = px(r.collection ? tok::rowCollection : sz().rowHeight);
    if (r.collection)
        return collection_base(top, name);
    if (!r.secondary.empty())
        return top + px(sz().rowTextInset) + f.ascent(name.px);
    return top + (h - f.ascent(name.px) - f.descent(name.px)) / 2 + f.ascent(name.px);
}

int row_thumbs_top(const Row& r, int top) { return collection_base(top, primary_role(r).style()) + px(tok::rowThumbDrop); }

bool draw_list_row(Canvas& c, Fonts& f, GlyphCache* g, const Row& r, int top, fx alpha, bool focused, fx press,
                   PressGlow* glow, int64_t now)
{
    bool ok = true;
    int h = px(r.collection ? tok::rowCollection : sz().rowHeight);
    int right = px(canvas_w() - tok::hubEdge);
    Rect band{0, top, kPortraitW, top + h};
    if (r.kind == Row::Divider) {
        int d = px(r.collection ? tok::dividerTileCollection : tok::dividerTile), x = px(tok::hubEdge);
        Rect t{x, top + (h - d) / 2, x + d, top + (h - d) / 2 + d};
        draw_letter_tile(c, t, px(tok::letterTileBorder), alpha256(tok::letterTileBorderAlpha), {});
        TextStyle st = divider_role().style();
        int w = f.width64(st, r.primary.c_str());
        return draw_clipped(c, f, st, r.primary, (t.x1 - px(tok::letterInsetX)) * 64 - w,
                            t.y1 - px(tok::letterInsetBaseline), alpha256(divider_role().alpha), band);
    }
    Zoom z{press, kPortraitW * 32, top * 64 + h * 32};
    TextStyle ps = primary_role(r).style();
    const Role& sr = secondary_role(r);
    TextStyle ss = sr.style();
    int base = list_row_base(f, r, top, ps);
    int x64 = row_title_x64(f, r, r.ring && g);
    if (r.ring && g) {
        int d = px(r.collection ? tok::rowPlayRing : sz().rowRing);
        int cx = r.collection ? px(tok::rowPlayRingX) : px(tok::hubEdge) + d / 2, cy = top + h / 2;
        bool lit = r.ring_toggle ? r.ring_on : focused;
        int a = alpha256(lit ? tok::alpha_full : tok::alpha_mid);
        int stroke = px(focused ? tok::ringStrokeFocused : tok::ringStrokeIdle);
        Rect keep = c.clip();
        c.set_clip(z.rect(band).intersect(keep));
        if (z.on()) {
            cx = z.px_x(cx);
            cy = z.px_y(cy);
        }
        if (r.ring_scale != kOne) {
            Coverage rc, gc;
            int sd = fx_round(d * r.ring_scale);
            if (!r.ring_bare) {
                scale_coverage(g->ring(d, stroke, press), sd, rc);
                draw_centered(c, rc, cx, cy, kWhite, a);
            }
            if (!strcmp(r.glyph, "wifi")) {
                draw_wifi(c, *g, sd / 2, cx, cy, r.level, a, press);
            } else {
                scale_coverage(g->glyph(r.glyph, d / 2, press), sd / 2, gc);
                draw_centered(c, gc, cx, cy, kWhite, a);
            }
        } else {
            if (!r.ring_bare)
                draw_centered(c, g->ring(d, stroke, press), cx, cy, kWhite, a);
            if (!strcmp(r.glyph, "wifi"))
                draw_wifi(c, *g, d / 2, cx, cy, r.level, a, press);
            else
                draw_centered(c, g->glyph(r.glyph, d / 2, press), cx, cy, kWhite, a);
        }
        if (r.arc > 0) {
            const Coverage& arc = r.arc_cv ? *r.arc_cv : g->arc(d, stroke, tok::ringArcStart >> kFxBits, r.arc);
            if (r.ring_scale != kOne) {
                Coverage ac;
                scale_coverage(arc, fx_round(d * r.ring_scale), ac);
                draw_centered(c, ac, cx, cy, accent(), 256);
            } else {
                draw_centered(c, arc, cx, cy, accent(), 256);
            }
        }
        c.set_clip(keep);
    }
    if (!r.number.empty())
        ok = draw_clipped(c, f, count_role().style(), r.number, to64(tok::hubEdge), base, alpha256(count_role().alpha),
                          band, z) && ok;
    int primary_right = right;
    if (!r.trailing.empty()) {
        TextStyle ts = count_role().style();
        int tx64 = right * 64 - f.width64(ts, r.trailing.c_str());
        ok = draw_clipped(c, f, ts, r.trailing, tx64, base, alpha256(count_role().alpha), band, z) && ok;
        primary_right = (tx64 >> 6) - px(tok::gutter);
    }
    Rect clip{x64 >> 6, top, primary_right, top + h};
    Rect keep = c.clip();
    TextStyle ts = ps;
    if (r.accent)
        accent_bold(ts);
    if (glow && !r.hide && (!r.accent || accent_off()))
        ok = glow->draw(c, f, {{ts, r.primary, x64, base}}, clip, z, now) && ok;
    c.set_clip(z.rect(clip).intersect(keep));
    if (!c.clip().empty() && !r.hide)
        ok = f.draw(c, ts, r.primary.c_str(), x64, base, r.accent ? accent() : kWhite,
                    r.accent ? 256 : alpha256(alpha), z) && ok;
    c.set_clip(keep);
    if (!r.secondary.empty()) {
        int sb = top + h - px(sz().rowTextInset) - f.descent(ss.px);
        fx sa = r.lib_sub ? role::listLibSecondary.alpha : sr.alpha;
        ok = draw_clipped(c, f, ss, r.secondary, x64, sb, alpha256(sa), Rect{x64 >> 6, top, right, top + h}, z) && ok;
    }
    return ok;
}

bool list_row_word(Fonts& f, ListView& v, int row, std::string& text, TextStyle& st, fx& x, fx& base, fx& look)
{
    if (!v.model() || row < 0 || row >= v.model()->count() || !v.cell_x.empty())
        return false;
    pos64 y = (pos64)v.top() + (pos64)row * v.pitch() - v.offset();
    if (y < (pos64)v.top() || y + v.pitch() > (pos64)v.bottom())
        return false;
    const Row& r = v.slot(row);
    if (r.kind != Row::Item || r.accent || r.primary.empty())
        return false;
    int top = (int)((((y * ppu()) >> kFxBits) + kHalf) >> kFxBits);
    st = primary_role(r).style();
    int base_px = list_row_base(f, r, top, st);
    int x64 = row_title_x64(f, r, r.ring), right = px(canvas_w() - tok::hubEdge);
    if (!r.trailing.empty())
        right = ((right * 64 - f.width64(count_role().style(), r.trailing.c_str())) >> 6) - px(tok::gutter);
    if (x64 + f.width64(st, r.primary.c_str()) > right * 64)
        return false;
    text = r.primary;
    x = (fx)((int64_t)x64 * 1024);
    base = fx_int(base_px);
    look = primary_role(r).alpha;
    return true;
}

void warm_list_row(const Row& r, std::vector<WarmText>& out)
{
    if (r.kind == Row::Divider) {
        warm_add(out, divider_role().style(), r.primary);
        return;
    }
    warm_add(out, count_role().style(), r.number);
    warm_add(out, count_role().style(), r.trailing);
    TextStyle ps = primary_role(r).style();
    warm_add(out, ps, r.primary);
    if (r.accent && accent_bold(ps))
        warm_add(out, ps, r.primary);
    warm_add(out, secondary_role(r).style(), r.secondary);
}

const char* const kWifiParts[] = {"wifiDot", "wifiArc1", "wifiArc2"};
const char* const kMicParts[] = {"micHead", "micBase"};

void draw_wifi(Canvas& c, GlyphCache& g, int size, int cx, int cy, int level, int a, fx press)
{
    for (int i = 0; i < 3; i++)
        draw_centered(c, g.glyph(kWifiParts[i], size, press), cx, cy, kWhite,
                      level < 0 || i < level ? a : a * alpha256(tok::alpha_low) >> 8);
}

void wifi_icons(std::vector<Icon>& out, int size)
{
    for (const char* n : kWifiParts)
        out.push_back({n, size});
}

void draw_mic(Canvas& c, GlyphCache& g, int size, int cx, int cy, int a, fx scale)
{
    for (const char* n : kMicParts)
        draw_centered(c, g.glyph(n, size, scale), cx, cy, kWhite, a);
}

void mic_icons(std::vector<Icon>& out, int size)
{
    for (const char* n : kMicParts)
        out.push_back({n, size});
}

void draw_focus_bar(Canvas& c, int y, fx pitch)
{
    int x = px(tok::hubEdge - tok::focus_barOffset);
    c.fill({x, y, x + px(tok::focus_barWidth), y + px(pitch)}, kWhite);
}

std::vector<std::string> settings_value_lines(Fonts& f, const Role& role, const std::string& title,
                                              const std::string& value)
{
    TextStyle ts = role.style(), vs = sz().settingsValue.style();
    accent_bold(vs);
    int right64 = to64(canvas_w() - tok::hubEdge), end64 = to64(tok::hubEdge) + f.width64(ts, title.c_str());
    auto fits = [&](int w64) { return end64 + (px(tok::gutter) << 6) + w64 <= right64; };
    if (fits(f.width64(vs, value.c_str())))
        return {value};
    std::vector<std::string> best{value};
    int narrow = 0;
    for (size_t sp = value.find(' '); sp != std::string::npos; sp = value.find(' ', sp + 1)) {
        std::string l0 = value.substr(0, sp), l1 = value.substr(sp + 1);
        if (l0.empty() || l1.empty() || l0.back() == ' ' || l1[0] == ' ')
            continue;
        int w = std::max(f.width64(vs, l0.c_str()), f.width64(vs, l1.c_str()));
        if (fits(w) && (best.size() < 2 || w < narrow)) {
            narrow = w;
            best = {l0, l1};
        }
    }
    return best;
}

std::vector<std::string> settings_title_lines(Fonts& f, const Role& role, const std::string& title,
                                              const std::vector<std::string>& values)
{
    TextStyle ts = role.style(), vs = sz().settingsValue.style();
    accent_bold(vs);
    int room64 = to64(canvas_w() - tok::hubEdge) - to64(tok::hubEdge) - (px(tok::gutter) << 6);
    auto fits = [&](int t64, const std::string& v) { return t64 + f.width64(vs, v.c_str()) <= room64; };
    int whole = f.width64(ts, title.c_str());
    bool need = false;
    for (const std::string& v : values)
        need = need || (!fits(whole, v) && settings_value_lines(f, role, title, v).size() < 2);
    if (!need)
        return {title};
    std::vector<std::string> best{title};
    int narrow = 0;
    for (size_t sp = title.find(' '); sp != std::string::npos; sp = title.find(' ', sp + 1)) {
        std::string l0 = title.substr(0, sp), l1 = title.substr(sp + 1);
        if (l0.empty() || l1.empty() || l0.back() == ' ' || l1[0] == ' ')
            continue;
        int w = std::max(f.width64(ts, l0.c_str()), f.width64(ts, l1.c_str()));
        if (best.size() < 2 || w < narrow) {
            narrow = w;
            best = {l0, l1};
        }
    }
    if (best.size() < 2)
        return best;
    const std::string& wide = settings_wide_line(f, role, best);
    for (const std::string& v : values)
        if (!fits(narrow, v) && settings_value_lines(f, role, wide, v).size() < 2)
            return {title};
    return best;
}

const std::string& settings_wide_line(Fonts& f, const Role& role, const std::vector<std::string>& lines)
{
    TextStyle ts = role.style();
    return lines.size() > 1 && f.width64(ts, lines[1].c_str()) > f.width64(ts, lines[0].c_str()) ? lines[1] : lines[0];
}

bool draw_settings_value(Canvas& c, Fonts& f, const Role& role, const std::string& title, const std::string& value,
                         int top, uint32_t rgb, int a256, int& clip_x, bool split, int h, bool edit)
{
    if (h < 0)
        h = px(sz().settingsPitch);
    TextStyle ts = role.style(), vs = sz().settingsValue.style();
    if (edit)
        accent_bold(vs);
    int right64 = to64(canvas_w() - tok::hubEdge);
    std::vector<std::string> lines =
        split ? settings_value_lines(f, role, title, value) : std::vector<std::string>{value};
    int vasc = f.ascent(vs.px), vdesc = f.descent(vs.px);
    int pitch = lines.size() < 2 ? 0 : vasc + vdesc;
    int asc = lines.size() < 2 ? f.ascent(ts.px) : vasc, desc = lines.size() < 2 ? f.descent(ts.px) : vdesc;
    int base = top + (h - asc - pitch - desc) / 2 + asc;
    int left64 = right64;
    bool ok = true;
    for (size_t l = 0; l < lines.size(); l++) {
        int vx64 = right64 - f.width64(vs, lines[l].c_str());
        left64 = std::min(left64, vx64);
        ok = f.draw(c, vs, lines[l].c_str(), vx64, base + (int)l * pitch, rgb, a256) && ok;
    }
    clip_x = (left64 >> 6) - px(tok::gutter);
    return ok;
}

bool draw_settings_title(Canvas& c, Fonts& f, const Role& role, const std::vector<std::string>& lines, int top, int a256,
                         int clip_x, int h, PressGlow* glow, int64_t now)
{
    if (h < 0)
        h = px(sz().settingsPitch);
    TextStyle ts = role.style();
    int asc = f.ascent(ts.px), desc = f.descent(ts.px);
    int pitch = lines.size() < 2 ? 0 : px(sz().settingsTitlePitch);
    int base = top + (h - asc - pitch - desc) / 2 + asc;
    bool ok = true;
    if (glow) {
        std::vector<GlowText> gt;
        for (size_t l = 0; l < lines.size(); l++)
            gt.push_back({ts, lines[l], to64(tok::hubEdge), base + (int)l * pitch});
        ok = glow->draw(c, f, gt, {0, top, clip_x, top + h}, {}, now);
    }
    for (size_t l = 0; l < lines.size(); l++)
        ok = draw_clipped(c, f, ts, lines[l], to64(tok::hubEdge), base + (int)l * pitch, a256, {0, top, clip_x, top + h}) &&
             ok;
    return ok;
}

bool draw_settings_row(Canvas& c, Fonts& f, const Role& role, const std::string& title, const std::string& value, int top,
                       fx focus, const std::vector<std::string>& words, bool lit, PressGlow* glow, int64_t now)
{
    fx ta = lit ? tok::alpha_full : fx_lerp(tok::type_settingsItem_alphaDim, tok::type_settingsItem_alpha, focus);
    fx va = fx_lerp(tok::settingsValueDim, tok::settingsValueFocused, focus);
    std::vector<std::string> cands = words;
    cands.push_back(value);
    std::vector<std::string> lines = settings_title_lines(f, role, title, cands);
    int clip_x = 0;
    bool ok =
        draw_settings_value(c, f, role, settings_wide_line(f, role, lines), value, top, kWhite, alpha256(va), clip_x);
    return draw_settings_title(c, f, role, lines, top, alpha256(ta), clip_x, -1, glow, now) && ok;
}

Zoom tile_zoom(Rect t, fx press) { return {press, (t.x0 + t.x1) * 32, (t.y0 + t.y1) * 32}; }

void draw_art_tile(Canvas& c, const Image* img, Rect t, int b, const Zoom& z)
{
    bool art = img && img->w == t.x1 - t.x0;
    if (!z.on()) {
        if (art)
            c.blit(*img, t.x0, t.y0);
        else
            c.fill(t, tok::color_tile);
        if (b)
            ring(c, t, b, 256);
        return;
    }
    Rect64 r = Rect64::of(t, z);
    if (b)
        c.fill_aa(Rect64::of(t.grow(b), z), kWhite, 256);
    if (art)
        c.blit_bilinear(*img, r, 256, z.s == tok::motion_pressScale);
    else
        c.fill_aa(r, tok::color_tile, 256);
}

bool draw_art_tile(Canvas& c, Fonts& f, const Image* img, bool box, const std::string& name, Rect t, int b, bool frame,
                   const Role& role, int lines)
{
    bool ok = true;
    if (img && img->w == t.x1 - t.x0)
        c.blit(*img, t.x0, t.y0);
    else if (box)
        ok = draw_box_tile(c, f, role, name, t, lines);
    else
        c.fill(t, tok::color_tile);
    if (b)
        ring(c, t, b, 256);
    if (frame)
        draw_press_frame(c, t);
    return ok;
}

bool draw_box_tile(Canvas& c, Fonts& f, const Role& role, const std::string& name, Rect t, int lines)
{
    inset_ring(c, t, tok::boxInset, tok::boxLine, alpha256(tok::boxLineAlpha));
    if (name.empty() || lines <= 0)
        return true;
    Rect in = t.grow(-px(tok::boxInset + tok::boxLine / 2));
    TextStyle ts = role.style();
    std::shared_ptr<const TextLayout> l = f.layout(ts, name, Dir::Auto);
    bool rtl = l->para.base & 1;
    int inset64 = to64(tok::boxInset + tok::boxCaptionX);
    int x64 = rtl ? t.x1 * 64 - inset64 : t.x0 * 64 + inset64;
    TextPrefix pre;
    f.prefix64(ts, *l, pre);
    auto width = [&](size_t b, size_t e) {
        int w = 0;
        return pre.span64(*l, b, e, w) ? w : f.span64(ts, *l, b, e);
    };
    auto step = [&](size_t p, size_t end) { return cluster_next(*l, p, end); };
    std::vector<std::pair<size_t, size_t>> rows =
        wrap_ranges(l->text, rtl ? x64 - in.x0 * 64 : in.x1 * 64 - x64, (size_t)lines, width, step);
    Rect keep = c.clip();
    c.set_clip(in.intersect(keep));
    bool ok = l->complete;
    fx pitch = (fx)((int64_t)tok::boxCaptionPitch * role.size / tok::type_boxCaptionLib_size);
    for (size_t i = 0; i < rows.size(); i++) {
        GlyphRun r = f.run(ts, *l, rows[i].first, i + 1 == rows.size() ? l->text.size() : rows[i].second);
        int base = t.y1 - px(tok::boxInset + tok::boxCaptionBase + (fx)(rows.size() - 1 - i) * pitch);
        ok = f.draw(c, r, rtl ? x64 - r.width64 : x64, base, kWhite, alpha256(role.alpha)) && ok;
    }
    c.set_clip(keep);
    return ok;
}

void draw_press_frame(Canvas& c, Rect t) { inset_ring(c, t, tok::boxInset, tok::pressFrame, 256); }

void draw_letter_tile(Canvas& c, Rect t, int bw, int ba, const Zoom& z, bool frame)
{
    if (!z.on()) {
        c.fill(t, tok::color_bg);
        c.blend({t.x0, t.y0, t.x1, t.y0 + bw}, kWhite, ba);
        c.blend({t.x0, t.y1 - bw, t.x1, t.y1}, kWhite, ba);
        c.blend({t.x0, t.y0 + bw, t.x0 + bw, t.y1 - bw}, kWhite, ba);
        c.blend({t.x1 - bw, t.y0 + bw, t.x1, t.y1 - bw}, kWhite, ba);
        if (frame)
            draw_press_frame(c, t);
        return;
    }
    Rect64 r = Rect64::of(t, z);
    c.fill_aa(r, tok::color_bg, 256);
    c.frame_aa(r, Rect64::of(t.grow(-bw), z), kWhite, ba);
}

Rect status_rect(const StatusBox& box)
{
    return {0, px(box.top), kPortraitW, px(box.top + box.height)};
}

bool draw_status(Canvas& c, Fonts& f, GlyphCache& g, int battery, PlayState st, int a, bool bluetooth,
                 const std::string& clock, bool percent, int wifi, int volume, const StatusBox& box)
{
    int size = px(box.height), top = px(box.top), gap = px(box.gap);
    int right = px(canvas_w() - box.right);
    Role tr = role::time;
    tr.size = box.text;
    TextStyle ts = tr.style();
    int base = top + (size - f.ascent(ts.px) - f.descent(ts.px)) / 2 + f.ascent(ts.px);
    int ta = alpha256(role::time.alpha) * a >> 8;
    bool ok = true;
    if (battery >= 0) {
        bool low = battery < (tok::batteryLowPercent >> kFxBits);
        uint32_t rgb = low ? accent() : kWhite;
        if (percent || (low && accent_off())) {
            std::string n = std::to_string(std::min(battery, 100)) + "%";
            int w64 = f.width64(ts, n.c_str());
            ok = f.draw(c, ts, n.c_str(), right * 64 - w64, base, rgb, ta) && ok;
            right -= (w64 + 63) / 64 + gap;
        } else if (box.battery_w > 0) {
            int bw = px(box.battery_w), bh = px(box.battery_h);
            int sk = std::max(1, px(box.battery_h * 2 / 9)), nub = std::max(1, px(box.battery_h / 9));
            int nh = std::max(1, px(box.battery_h / 3));
            int x0 = right - bw, x1 = right - nub, y0 = top + size / 2 - bh / 2, y1 = y0 + bh;
            c.blend({x0, y0, x1, y0 + sk}, rgb, a);
            c.blend({x0, y1 - sk, x1, y1}, rgb, a);
            c.blend({x0, y0 + sk, x0 + sk, y1 - sk}, rgb, a);
            c.blend({x1 - sk, y0 + sk, x1, y1 - sk}, rgb, a);
            c.blend({x1, y0 + (bh - nh) / 2, right, y0 + (bh - nh) / 2 + nh}, rgb, a);
            int iw = x1 - x0 - 2 * sk, ih = bh - 2 * sk;
            int ix = std::max(1, (iw + 9) / 18), iy = std::max(1, (ih + 5) / 10);
            int lx = x0 + sk + ix, ly = y0 + sk + iy, fw = iw - 2 * ix;
            c.blend({lx, ly, lx + fw * std::min(battery, 100) / 100, y1 - sk - iy}, rgb, a);
            right = x0 - gap;
        } else {
            int x0 = right - size;
            draw_centered(c, g.glyph("battery", size), x0 + size / 2, top + size / 2, rgb, a);
            int box = fx_round(tok::glyphBox);
            int bx = fx_round(tok::statusBatteryX), by = fx_round(tok::statusBatteryY);
            int bw = fx_round(tok::statusBatteryW), bh = fx_round(tok::statusBatteryH);
            int lx = x0 + bx * size / box, lw = bw * size * std::min(battery, 100) / (box * 100);
            c.blend({lx, top + by * size / box, lx + lw, top + (by + bh) * size / box}, rgb, a);
            right = x0 - gap;
        }
    }
    if (bluetooth) {
        draw_centered(c, g.glyph("bluetooth", size), right - size / 2, top + size / 2, kWhite, a);
        right -= size + gap;
    }
    if (st != PlayState::None) {
        draw_centered(c, g.glyph(st == PlayState::Playing ? "play" : "pause", size), right - size / 2, top + size / 2,
                      kWhite, a);
        right -= size + gap;
    }
    if (wifi >= 0) {
        draw_wifi(c, g, size, right - size / 2, top + size / 2, wifi, a);
        right -= size + gap;
    }
    if (!clock.empty()) {
        int w64 = f.width64(ts, clock.c_str());
        ok = f.draw(c, ts, clock.c_str(), right * 64 - w64, base, kWhite, ta) && ok;
        right -= (w64 + 63) / 64 + gap;
    }
    int left = std_px(box.left) + gap;
    std::string n = volume >= 0 ? std::to_string(volume) : "";
    int w64 = n.empty() ? 0 : f.width64(ts, n.c_str());
    if (!n.empty() && right - (w64 + 63) / 64 - gap - size >= left) {
        ok = f.draw(c, ts, n.c_str(), right * 64 - w64, base, kWhite, ta) && ok;
        right -= (w64 + 63) / 64 + gap;
        draw_centered(c, g.glyph("speaker", size), right - size / 2, top + size / 2, kWhite, a);
        draw_centered(c, g.glyph("speakerWaves", size), right - size / 2, top + size / 2, kWhite, a);
    }
    return ok;
}

void status_icons(std::vector<Icon>& out, const StatusBox& box)
{
    int size = px(box.height);
    for (const char* n : {"battery", "bluetooth", "play", "pause", "speaker", "speakerWaves"})
        out.push_back({n, size});
    wifi_icons(out, size);
}

}
