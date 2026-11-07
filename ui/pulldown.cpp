#include "pulldown.h"

#include <algorithm>

#include "../core/curves.h"
#include "../board/board.h"
#include "../gfx/compose.h"
#include "rows.h"
#include "size.h"

namespace fd {

namespace {

constexpr int ms(fx t) { return t >> kFxBits; }
int to64(fx u) { return (to_px(u) + 512) >> 10; }
constexpr int kCols = 4;

fx progress(int64_t e, int dur) { return (fx)std::clamp<int64_t>(dur > 0 ? e * kOne / dur : kOne, 0, kOne); }

int mul_a(int a, fx b) { return a * alpha256(b) >> 8; }

const Role& track_title() { return role::toastTitle; }

const Role& track_artist() { return role::toastArtist; }

fx col_x(int col) { return tok::edge + col * (tok::kb_keyW + tok::qpGutter); }

Rect box(fx x0, fx y0, fx x1, fx y1) { return {px(x0), px(y0), px(x1), px(y1)}; }

void centered(Canvas& c, Fonts& f, const char* t, Rect r, int a, const Zoom& z)
{
    TextStyle s = role::listPrimary.style();
    s.tcase = Case::Upper;
    int base = r.y0 + (r.y1 - r.y0 - f.ascent(s.px) - f.descent(s.px)) / 2 + f.ascent(s.px);
    f.draw(c, s, t, (r.x0 + r.x1) * 32 - f.width64(s, t) / 2, base, kWhite, a, z);
}

void corner(Canvas& c, Fonts& f, const char* t, Rect r, int a, const Zoom& z)
{
    TextStyle s = role::sectionLabel.style();
    Rect keep = c.clip();
    c.set_clip(z.rect(r).intersect(keep));
    f.draw(c, s, t, (r.x1 - px(tok::letterInsetX)) * 64 - f.width64(s, t), r.y1 - px(tok::letterInsetBaseline), kWhite,
           mul_a(a, role::sectionLabel.alpha), z);
    c.set_clip(keep);
}

}

int PullDown::fade_ms() { return ms(tok::motion_volumeFade); }

void PullDown::open(int64_t now)
{
    in_from_ = visible(now) ? opacity(now) : 0;
    in_at_ = now;
    fade_at_ = -1;
    shown_ = true;
    drawn_a_ = -1;
    press_.reset();
}

void PullDown::close(int64_t now)
{
    if (shown_ && fade_at_ < 0)
        fade_at_ = now;
}

fx PullDown::opacity(int64_t now) const
{
    if (!shown_)
        return 0;
    if (fade_at_ >= 0)
        return fx_mul(fx_lerp(in_from_, kOne, progress(fade_at_ - in_at_, ms(tok::motion_volumeShow))),
                      kOne - curve(Curve::Decel, progress(now - fade_at_, fade_ms())));
    return fx_lerp(in_from_, kOne, progress(now - in_at_, ms(tok::motion_volumeShow)));
}

bool PullDown::moving(int64_t now) const
{
    if (!shown_)
        return false;
    return now < in_at_ + ms(tok::motion_volumeShow) || (fade_at_ >= 0 && now < fade_at_ + fade_ms()) ||
           press_.stale(now) || alpha256(opacity(now)) != drawn_a_;
}

Rect PullDown::damage(int64_t now) const
{
    Scope scope(Fit::Kept, "pull");
    if (!shown_)
        return {};
    if (drawn_a_ < 0 || now < in_at_ + ms(tok::motion_volumeShow) || fade_at_ >= 0 ||
        alpha256(opacity(now)) != drawn_a_ || drawn_.gain.empty() != info_.gain.empty() || drawn_.np != info_.np ||
        drawn_.artist != info_.artist || drawn_.title != info_.title)
        return kScreen;
    Rect d;
    for (int i = 0; i < kItems; i++)
        if (has(i) && (press_step(press_.scale(i, now)) != drawn_step_[i] || value_changed(i)))
            d = d.unite(box_of(i));
    if (drawn_.brightness != info_.brightness)
        d = d.unite(value_box());
    return d;
}

int64_t PullDown::next_change(int64_t now) const
{
    int64_t w = -1;
    for (const PressSlots::Slot* s : {&press_.on, &press_.back}) {
        int64_t t = s->item >= 0 ? s->p.next_step(now) : -1;
        if (t > now && (w < 0 || t < w))
            w = t;
    }
    return w;
}

bool PullDown::value_changed(int item) const
{
    switch (item) {
    case Bluetooth:
        return drawn_.bt != info_.bt;
    case Wifi:
        return drawn_.wifi != info_.wifi;
    case Gain:
        return drawn_.gain != info_.gain;
    case Sleep:
        return drawn_.sleep != info_.sleep;
    case Dimmer:
        return drawn_.dimmer != info_.dimmer;
    case Brighter:
        return drawn_.brighter != info_.brighter;
    case Play:
        return drawn_.playing != info_.playing;
    default:
        return false;
    }
}

bool PullDown::has(int item) const
{
    switch (item) {
    case Wifi:
        return board::kHasWifi;
    case Gain:
        return !info_.gain.empty();
    case Track:
        return info_.np;
    default:
        return item >= 0 && item < kItems;
    }
}

int PullDown::slot(int item) const
{
    int s = 0;
    for (int i = Bluetooth; i < item; i++)
        s += has(i);
    return s;
}

fx PullDown::grid_top() const { return tok::qpFirstBaseline + tok::qpContentDrop; }

fx PullDown::ring_y() const { return canvas_h() - tok::overlayBottomMargin - tok::overlayRing / 2; }

Rect PullDown::cell(int item) const
{
    fx top = grid_top(), w = tok::kb_keyW;
    fx row2 = top + w + tok::qpGutter;
    switch (item) {
    case Bluetooth:
    case Wifi:
    case Gain:
    case Sleep: {
        fx x = col_x(slot(item));
        return box(x, top, x + w, top + w);
    }
    case Dimmer:
        return box(col_x(0), row2, col_x(0) + w, row2 + w);
    case Brighter:
        return box(col_x(kCols - 1), row2, col_x(kCols - 1) + w, row2 + w);
    case Track: {
        fx y1 = ring_y() - tok::overlayRing / 2 - tok::gutter;
        return box(tok::edge, y1 - tok::overlayTextPitch - role::toastTitle.size, canvas_w() - tok::edge, y1);
    }
    default: {
        fx cx = canvas_w() / 2 + (item - Play) * tok::overlayGlyphOffset, r = tok::overlayRing / 2;
        return box(cx - r, ring_y() - r, cx + r, ring_y() + r);
    }
    }
}

Rect PullDown::box_of(int item) const
{
    switch (item) {
    case Prev:
    case Play:
    case Next:
        return cell(item).grow(1);
    case Track:
        return box(tok::edge, grid_top() + 2 * tok::kb_keyW + tok::qpGutter, canvas_w() - tok::edge,
                   ring_y() - tok::overlayRing / 2);
    default:
        return cell(item);
    }
}

bool PullDown::clip(Canvas& c, Rect b, Rect d) const
{
    c.set_clip((inset_ < 0 ? kScreen : b.grow(-inset_)).intersect(d));
    return !c.clip().empty();
}

Rect PullDown::value_box() const
{
    fx row2 = grid_top() + tok::kb_keyW + tok::qpGutter;
    return box(col_x(1), row2, col_x(kCols - 1) - tok::qpGutter, row2 + tok::kb_keyW);
}

int PullDown::hit(fx x, fx y) const
{
    int px_x = fx_round(to_px(x)), px_y = fx_round(to_px(y));
    Scope scope(Fit::Kept, "pull");
    for (int i = 0; i < kItems; i++) {
        if (!has(i))
            continue;
        Rect r = cell(i);
        if (px_x >= r.x0 && px_x < r.x1 && px_y >= r.y0 && px_y < r.y1)
            return i;
    }
    return -1;
}

bool PullDown::center(int item, fx& x, fx& y) const
{
    if (!has(item))
        return false;
    Rect r;
    {
        Scope scope(Fit::Kept, "pull");
        r = cell(item);
    }
    x = fx_div(fx_int(r.x0 + r.x1) / 2, ppu());
    y = fx_div(fx_int(r.y0 + r.y1) / 2, ppu());
    return true;
}

void PullDown::draw_tile(Canvas& c, Fonts& f, GlyphCache& g, int item, int a, int64_t now)
{
    Rect t = cell(item);
    fx press = press_.draw(item, now);
    Zoom z = tile_zoom(t, press);
    c.fill_aa(Rect64::of(t, z), tok::color_tile, a);
    int gpx = px(tok::overlayGlyph), cx = (t.x0 + t.x1) / 2, cy = (t.y0 + t.y1) / 2;
    switch (item) {
    case Bluetooth:
        draw_centered(c, g.glyph("bluetooth", gpx, press), cx, cy, kWhite,
                      mul_a(a, info_.bt ? tok::alpha_full : tok::alpha_mid));
        break;
    case Wifi:
        draw_wifi(c, g, gpx, cx, cy, info_.wifi, mul_a(a, info_.wifi >= 0 ? tok::alpha_full : tok::alpha_mid), press);
        break;
    case Gain:
        centered(c, f, info_.gain.c_str(), t, mul_a(a, role::listPrimary.alpha), z);
        corner(c, f, "gain", t, a, z);
        break;
    case Sleep:
        centered(c, f, info_.sleep.c_str(), t, mul_a(a, role::listPrimary.alpha), z);
        corner(c, f, "sleep", t, a, z);
        break;
    case Dimmer:
    case Brighter: {
        bool on = item == Dimmer ? info_.dimmer : info_.brighter;
        draw_centered(c, g.glyph(item == Dimmer ? "minus" : "plus", gpx, press), cx, cy, kWhite,
                      mul_a(a, on ? tok::alpha_full : tok::alpha_low));
        break;
    }
    default:
        break;
    }
}

void PullDown::draw_player(Canvas& c, Fonts& f, GlyphCache& g, int a, int64_t now, Rect d)
{
    int cx = kPortraitW / 2, ry = px(ring_y()), gpx = px(tok::overlayGlyph);
    for (int i : {Prev, Next}) {
        if (!clip(c, box_of(i), d))
            continue;
        fx press = press_.draw(i, now);
        draw_centered(c, g.glyph(i == Prev ? "previous" : "next", gpx, press),
                      cx + (i - Play) * px(tok::overlayGlyphOffset), ry, kWhite, a);
    }
    if (clip(c, box_of(Play), d)) {
        fx press = press_.draw(Play, now);
        draw_centered(c, g.ring(px(tok::overlayRing), px(tok::ringStrokeFocused), press), cx, ry, kWhite, a);
        draw_centered(c, g.glyph(info_.playing ? "pause" : "play", px(tok::overlayRing) / 2, press), cx, ry, kWhite, a);
    }
    if (!clip(c, box_of(Track), d))
        return;
    TextStyle ts = track_title().style(), as = track_artist().style();
    int tb = px(ring_y() - tok::overlayRing / 2 - tok::gutter) - f.descent(ts.px);
    Zoom z = tile_zoom(cell(Track), press_.draw(Track, now));
    Rect keep = c.clip();
    c.set_clip(z.rect(Rect{px(tok::edge), 0, px(canvas_w() - tok::edge), kPortraitH}).intersect(keep));
    f.draw(c, ts, info_.title.c_str(), to64(tok::edge), tb, kWhite, mul_a(a, track_title().alpha), z);
    f.draw(c, as, info_.artist.c_str(), to64(tok::edge), tb - px(tok::overlayTextPitch), kWhite,
           mul_a(a, track_artist().alpha), z);
    c.set_clip(keep);
}

void PullDown::warm_track(const std::string& artist, const std::string& title, bool buckets, std::vector<WarmText>& out)
{
    Scope scope(Fit::Kept, "pull");
    const std::pair<TextStyle, const std::string*> lines[] = {{track_title().style(), &title},
                                                              {track_artist().style(), &artist}};
    for (const auto& l : lines) {
        int n = buckets ? Fonts::press_buckets(l.first.px) : 0;
        for (int b = 0; b <= n; b++)
            warm_add(out, l.first, *l.second, b);
    }
}

void PullDown::icons(std::vector<Icon>& out)
{
    Scope scope(Fit::Kept, "pull");
    int gpx = px(tok::overlayGlyph), ring = px(tok::overlayRing);
    for (const char* n : {"bluetooth", "minus", "plus", "previous", "next"})
        out.push_back({n, gpx});
    wifi_icons(out, gpx);
    out.push_back({nullptr, ring, px(tok::ringStrokeFocused)});
    for (const char* n : {"play", "pause"})
        out.push_back({n, ring / 2});
}

void PullDown::compose(Fonts& f, GlyphCache& g, const Surface& page, const Surface& dst, int64_t now, Rect d)
{
    Scope scope(Fit::Kept, "pull");
    d = d.intersect(kScreen);
    fx o = opacity(now);
    scrim(page, dst, d, tok::color_black, alpha256(fx_mul(o, tok::scrim_overlay_alpha)));
    Canvas c(dst);
    int a = alpha256(o);
    drawn_a_ = a;
    drawn_ = info_;
    for (int i = 0; i < kItems; i++)
        drawn_step_[i] = press_step(press_.scale(i, now));
    TextStyle ls = role::overlayLabel.style();
    int ex = to64(tok::edge), eb = px(tok::edge) + f.ascent(ls.px);
    Rect label{ex >> 6, eb - f.ascent(ls.px), (ex + f.width64(ls, "exit") + 63) >> 6, eb + f.descent(ls.px)};
    if (clip(c, label.grow(2), d))
        f.draw(c, ls, "exit", ex, eb, kWhite, mul_a(a, tok::overlayDim));
    for (int i = Bluetooth; i <= Brighter; i++)
        if (has(i) && clip(c, box_of(i), d))
            draw_tile(c, f, g, i, a, now);
    Rect v = value_box();
    if (clip(c, v, d)) {
        c.fill_aa(Rect64::of(v, Zoom{}), tok::color_tile, a);
        std::string pct = std::to_string(info_.brightness) + "%";
        centered(c, f, pct.c_str(), v, mul_a(a, role::listPrimary.alpha), Zoom{});
        corner(c, f, "brightness", v, a, Zoom{});
    }
    draw_player(c, f, g, a, now, d);
}

}
