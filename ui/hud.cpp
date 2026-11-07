#include "hud.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "../core/curves.h"
#include "../core/gesture.h"
#include "../gfx/compose.h"
#include "rows.h"
#include "size.h"

namespace fd {

namespace {

constexpr int ms(fx t) { return t >> kFxBits; }

std::string seek_text(int64_t t, int64_t dur)
{
    int64_t s = std::max<int64_t>(0, t / 1000);
    char buf[32];
    if (dur >= 3600000)
        snprintf(buf, sizeof buf, "%lld:%02d:%02d", (long long)(s / 3600), (int)(s / 60 % 60), (int)(s % 60));
    else
        snprintf(buf, sizeof buf, "%lld:%02d", (long long)(s / 60), (int)(s % 60));
    return buf;
}

int to64(fx u) { return (to_px(u) + 512) >> 10; }

fx progress(int64_t e, int dur) { return (fx)std::clamp<int64_t>(dur > 0 ? e * kOne / dur : kOne, 0, kOne); }

int mul_a(fx a, fx b) { return alpha256(fx_mul(a, b)); }

const Role& toast_artist() { return sz().toastArtist; }

const Role& toast_title() { return sz().toastTitle; }

const Role& hud_artist() { return role::toastArtist; }

const Role& hud_title() { return role::toastTitle; }

void text(Canvas& c, Fonts& f, const Role& r, const std::string& s, int x64, int base, int a, Rect clip,
          const Zoom& z = {})
{
    Rect keep = c.clip();
    c.set_clip(z.rect(clip).intersect(keep));
    if (!c.clip().empty())
        f.draw(c, r.style(), s.c_str(), x64, base, kWhite, a, z);
    c.set_clip(keep);
}

std::string upper(std::string s)
{
    for (char& ch : s)
        ch = (char)std::toupper((unsigned char)ch);
    return s;
}

Rect number_into(Canvas& lc, Fonts& f, const TextStyle& ns, const std::string& num, int x64, int base, Rect old)
{
    if (!old.empty())
        lc.ground(old, tok::color_black);
    int w = (f.width64(ns, num.c_str()) + 63) >> 6, x0 = x64 >> 6;
    Rect r = Rect{x0, base - f.ascent(ns.px), x0 + w + 1, base + f.descent(ns.px)}.intersect(kScreen);
    lc.ground(r, tok::color_black);
    f.draw(lc, ns, num.c_str(), x64, base, kWhite, 256);
    return r;
}

void number_blit(const Surface& layer, uint8_t* mask, const Surface& dst, Rect r, fx s, int a)
{
    int cx = (r.x0 + r.x1) / 2, cy = (r.y0 + r.y1) / 2;
    if constexpr (kPix16)
        coverage_scaled(layer, (const uint8_t*)mask, dst, r, s, cx, cy, kWhite, a);
    else
        coverage_scaled(layer, dst, r, s, cx, cy, kWhite, a);
}

fx exp_out6_inverse(fx e)
{
    fx lo = 0, hi = kOne;
    for (int i = 0; i < 17; i++) {
        fx mid = (lo + hi) / 2;
        if (curve(Curve::ExpOut6, mid) < e)
            lo = mid;
        else
            hi = mid;
    }
    return hi;
}

}

int VolumeHud::fade_ms() { return ms(tok::motion_volumeFade); }

void VolumeHud::show(int64_t now, int percent, bool playing, const std::string& artist, const std::string& title)
{
    Scope scope(Fit::Kept, "vol");
    if (!visible(now)) {
        in_from_ = 0;
        in_at_ = now;
    } else if (fade_at_ >= 0 && now >= fade_at_) {
        in_from_ = opacity(now);
        in_at_ = now;
    }
    num_dirty_ = num_dirty_ || percent != percent_ || !shown_;
    shown_ = true;
    percent_ = percent;
    playing_ = playing;
    artist_ = artist;
    title_ = title;
    pop_at_ = now;
    fade_at_ = held_ ? -1 : now + ms(tok::motion_volumeHide);
}

void VolumeHud::drop_touch()
{
    held_ = seeking_ = false;
    drag_ = Drag::Tap;
    if (own_) {
        own_ = false;
        drop_ = true;
    }
}

void VolumeHud::cancel(int64_t now)
{
    if (!own_)
        return;
    drop_touch();
    fade_at_ = now + ms(tok::motion_volumeHide);
}

void VolumeHud::anchor(int percent)
{
    if (drag_ != Drag::Volume)
        return;
    from_ = to_ = keep_ = percent;
    ay_ = ly_;
}

int VolumeHud::hit(fx x, fx y) const
{
    fx r = tok::vol_hit / 2, cx = tok::canvasW / 2;
    if (x < tok::adjustExit && y < tok::adjustExit)
        return Exit;
    auto in = [&](fx gx, fx gy, fx h) { return x >= gx - h && x < gx + h && y >= gy - h && y < gy + h; };
    if (in(cx, tok::overlayPlusY, r))
        return Plus;
    if (in(cx, tok::overlayMinusY, r))
        return Minus;
    if (in(cx - tok::overlayGlyphOffset, tok::overlayRingY, r))
        return Prev;
    if (in(cx, tok::overlayRingY, tok::vol_ring / 2))
        return Ring;
    if (in(cx + tok::overlayGlyphOffset, tok::overlayRingY, r))
        return Next;
    return None;
}

VolumeHud::Act VolumeHud::event(const Event& in, int64_t now, int percent, const Pos& at)
{
    Scope scope(Fit::Kept, "vol");
    Event e = scoped(in);
    if (e.kind == Ev::TouchDown) {
        if (!visible(now) || closing(now) || e.y >= canvas_h() - std_u(tok::gesture_bottom))
            return Act::Pass;
        own_ = held_ = true;
        drop_ = seeking_ = false;
        fade_at_ = -1;
        drag_ = Drag::Tap;
        hit_ = hit(e.x, e.y);
        row_ = e.y >= tok::overlayRingY - tok::vol_hit / 2 && e.y < tok::overlayRingY + tok::vol_hit / 2;
        x0_ = e.x;
        y0_ = ly_ = e.y;
        return Act::None;
    }
    if (!own_)
        return Act::None;
    if (e.kind == Ev::TouchMove) {
        fx was = ly_;
        ly_ = e.y;
        if (drag_ == Drag::Tap) {
            if (touch_tap(x0_, y0_, e.x, e.y))
                return Act::None;
            ax_ = e.x;
            ay_ = e.y;
            from_ = to_ = keep_ = percent;
            if (axis_of((int64_t)e.x - x0_, (int64_t)e.y - y0_) == Axis::Vertical) {
                drag_ = Drag::Volume;
                return Act::None;
            }
            drag_ = row_ && at.on ? Drag::Seek : Drag::Off;
            if (drag_ == Drag::Off)
                return Act::None;
            at_ = at;
            at_.ms = std::clamp<int64_t>(at.ms, 0, at.dur);
            seek_to_ = at_.ms;
            seeking_ = true;
            readout_ = seek_text(seek_to_, at_.dur);
            return Act::Draw;
        }
        if (drag_ == Drag::Volume) {
            if (percent != keep_) {
                from_ = to_ = keep_ = percent;
                ay_ = was;
                return Act::None;
            }
            int64_t span = tok::overlayMinusY - tok::overlayPlusY, d = ((int64_t)ay_ - e.y) * 100;
            int n = std::clamp(from_ + (int)((d + (d < 0 ? -span : span) / 2) / span), 0, 100);
            if (n == to_)
                return Act::None;
            to_ = n;
            return Act::Volume;
        }
        if (drag_ != Drag::Seek)
            return Act::None;
        int64_t w = 2 * (int64_t)tok::overlayGlyphOffset;
        seek_to_ = std::clamp<int64_t>(at_.ms + ((int64_t)e.x - ax_) * at_.dur / w, 0, at_.dur);
        std::string s = seek_text(seek_to_, at_.dur);
        if (s == readout_)
            return Act::None;
        readout_ = s;
        return Act::Draw;
    }
    if (e.kind != Ev::TouchUp)
        return Act::None;
    own_ = held_ = false;
    fade_at_ = now + ms(tok::motion_volumeHide);
    Drag d = drag_;
    drag_ = Drag::Tap;
    if (d == Drag::Seek) {
        seeking_ = false;
        return Act::Seek;
    }
    if (d != Drag::Tap || !touch_tap(x0_, y0_, e.x, e.y))
        return Act::None;
    switch (hit_) {
    case Plus:
        return Act::Plus;
    case Minus:
        return Act::Minus;
    case Prev:
        return Act::Prev;
    case Ring:
        return Act::Ring;
    case Next:
        return Act::Next;
    default:
        hide(now);
        return Act::None;
    }
}

#ifndef FD_ALSA
void VolumeHud::shot_seek(int64_t to, int64_t dur)
{
    seeking_ = true;
    seek_to_ = to;
    readout_ = seek_text(to, dur);
}
#endif

fx VolumeHud::opacity(int64_t now) const
{
    Scope scope(Fit::Kept, "vol");
    if (!shown_)
        return 0;
    if (fade_at_ >= 0 && now >= fade_at_)
        return kOne - curve(Curve::Decel, progress(now - fade_at_, fade_ms()));
    return fx_lerp(in_from_, kOne, progress(now - in_at_, ms(tok::motion_volumeShow)));
}

bool VolumeHud::moving(int64_t now) const
{
    Scope scope(Fit::Kept, "vol");
    if (!shown_)
        return false;
    return now < in_at_ + ms(tok::motion_volumeShow) || now < pop_at_ + ms(tok::motion_volumePop) ||
           (fade_at_ >= 0 && now >= fade_at_ && now < fade_at_ + fade_ms()) ||
           alpha256(opacity(now)) != drawn_a_ || pop(now) != drawn_pop_;
}

fx VolumeHud::pop(int64_t now) const
{
    return fx_lerp(tok::motion_toggleScale, kOne,
                   curve(Curve::BezierPop, progress(now - pop_at_, ms(tok::motion_volumePop))));
}

void VolumeHud::compose(Fonts& f, GlyphCache& g, const Surface& page, const Surface& layer, uint8_t* mask,
                        const Surface& dst, int64_t now)
{
    Scope scope(Fit::Kept, "vol");
    fx o = opacity(now);
    std::string num = (percent_ < 10 ? "0" : "") + std::to_string(percent_);
    int base = px(tok::overlayNumberBaseline);
    int cx = kPortraitW / 2;
    if (num_dirty_) {
        Canvas lc(layer, mask);
        lc.set_clip(kScreen);
        num_rect_ = number_into(lc, f, number_style(f, num), num, to64(tok::edge), base, num_rect_);
        num_dirty_ = false;
    }
    scrim(page, dst, tok::color_black, mul_a(o, tok::scrim_overlay_alpha));
    Canvas c(dst);
    c.set_clip(kScreen);
    int lit = mul_a(o, tok::overlayLit), dim = mul_a(o, tok::overlayDim);
    draw_centered(c, g.glyph("plus", px(tok::vol_plus)), cx, px(tok::overlayPlusY), kWhite, lit);
    draw_centered(c, g.glyph("minus", px(tok::vol_minus)), cx, px(tok::overlayMinusY), kWhite, lit);
    int ry = px(tok::overlayRingY);
    if (seeking_) {
        TextStyle rs = role::overlayNumber.style();
        int rb = ry + fx_round(fx_mul(fx_int(rs.px), tok::adjustNumberDrop));
        text(c, f, role::overlayNumber, readout_, cx * 64 - f.width64(rs, readout_.c_str()) / 2, rb, dim, kScreen);
    }
    int spx = px(tok::vol_skip);
    draw_centered(c, g.glyph("skipPrevious", spx), cx - px(tok::overlayGlyphOffset), ry, kWhite, dim);
    draw_centered(c, g.glyph("skipNext", spx), cx + px(tok::overlayGlyphOffset), ry, kWhite, dim);
    draw_centered(c, g.ring(px(tok::vol_ring), px(tok::vol_ringStroke)), cx, ry, kWhite, dim);
    draw_centered(c, g.glyph(playing_ ? "pause" : "play", pause_px()), cx, ry, kWhite, dim);
    TextStyle es = role::volExit.style(), ls = role::volLabel.style(), ts = hud_title().style();
    text(c, f, role::volExit, "exit", to64(tok::edge), px(tok::edge) + f.ascent(es.px), dim, kScreen);
    int tb = px(tok::overlayTitleBottom) - f.descent(ts.px);
    int cap = tb - px(tok::overlayTextPitch) - fx_round(fx_mul(fx_int(hud_artist().style().px), tok::capHeight));
    int vtop = px(tok::overlayMinusY + tok::overlayGlyph / 2);
    while (ls.px > fx_round(tok::minTextPx) && vtop + f.ascent(ls.px) >= cap - 1)
        ls.px--;
    label_px_ = ls.px;
    f.draw(c, ls, "volume", cx * 64 - f.width64(ls, "volume") / 2, vtop + f.ascent(ls.px), kWhite, dim);
    Rect clip{px(tok::edge), 0, px(tok::canvasW - tok::edge), kPortraitH};
    text(c, f, hud_title(), title_, to64(tok::edge), tb, mul_a(fx_mul(o, tok::overlayDim), hud_title().alpha), clip);
    text(c, f, hud_artist(), artist_, to64(tok::edge), tb - px(tok::overlayTextPitch),
         mul_a(fx_mul(o, tok::overlayDim), hud_artist().alpha), clip);
    fx s = pop(now);
    drawn_a_ = alpha256(o);
    drawn_pop_ = s;
    number_blit(layer, mask, dst, num_rect_, s, lit);
}

int VolumeHud::pause_px()
{
    int p = px(tok::vol_pause);
    if ((p - px(tok::vol_ring)) & 1)
        p += to_px(tok::vol_pause) < fx_int(p) ? -1 : 1;
    return p;
}

TextStyle VolumeHud::number_style(Fonts& f, const std::string& num)
{
    TextStyle ns = role::volNumber.style();
    int room = (kPortraitW / 2 - px(tok::vol_plus) * 3 / 8) * 64 - to64(tok::edge), w = f.width64(ns, num.c_str());
    if (w > room)
        ns.px = ns.px * room / w;
    while (ns.px > 1 && f.width64(ns, num.c_str()) > room)
        ns.px--;
    return ns;
}

void VolumeHud::icons(std::vector<Icon>& out)
{
    Scope scope(Fit::Kept, "vol");
    out.push_back({"plus", px(tok::vol_plus)});
    out.push_back({"minus", px(tok::vol_minus)});
    out.push_back({"skipPrevious", px(tok::vol_skip)});
    out.push_back({"skipNext", px(tok::vol_skip)});
    out.push_back({nullptr, px(tok::vol_ring), px(tok::vol_ringStroke)});
    out.push_back({"play", pause_px()});
    out.push_back({"pause", pause_px()});
}

void VolumeHud::warm(const std::string& artist, const std::string& title, std::vector<WarmText>& out)
{
    Scope scope(Fit::Kept, "vol");
    warm_add(out, hud_title().style(), title);
    warm_add(out, hud_artist().style(), artist);
}

int AdjustHud::fade_ms() { return ms(tok::motion_volumeFade); }

void AdjustHud::open(int64_t now, std::vector<Col> cols, const std::string& line1, const std::string& line2)
{
    shown_ = true;
    dirty_ = true;
    hide_at_ = -1;
    in_at_ = now;
    pop_at_[0] = pop_at_[1] = -1;
    num_rect_[0] = num_rect_[1] = Rect{};
    cols_ = std::move(cols);
    line1_ = line1;
    line2_ = upper(line2);
}

void AdjustHud::set(int64_t now, std::vector<Col> cols, const std::string& line2)
{
    for (size_t i = 0; i < cols.size() && i < cols_.size() && i < 2; i++)
        if (cols[i].num != cols_[i].num)
            pop_at_[i] = now;
    dirty_ = dirty_ || cols != cols_;
    cols_ = std::move(cols);
    line2_ = upper(line2);
}

fx AdjustHud::opacity(int64_t now) const
{
    if (!shown_)
        return 0;
    if (hide_at_ >= 0)
        return kOne - curve(Curve::Decel, progress(now - hide_at_, fade_ms()));
    return progress(now - in_at_, ms(tok::motion_volumeShow));
}

bool AdjustHud::moving(int64_t now) const
{
    if (!shown_)
        return false;
    if (hide_at_ >= 0)
        return now < hide_at_ + fade_ms();
    return now < in_at_ + ms(tok::motion_volumeShow) || (pop_at_[0] >= 0 && now < pop_at_[0] + ms(tok::motion_volumePop)) ||
           (pop_at_[1] >= 0 && now < pop_at_[1] + ms(tok::motion_volumePop));
}

int AdjustHud::col_x(int i) const
{
    if (cols_.size() < 2)
        return kPortraitW / 2;
    return i == 0 ? px(tok::edge + tok::adjustColumn) : px(tok::canvasW - tok::edge - tok::adjustColumn);
}

int AdjustHud::hit(fx x, fx y) const
{
    int hx = px(x), hy = px(y);
    Scope scope(Fit::Kept, "adjust");
    int e = px(tok::adjustExit), r = px(tok::overlayRing) / 2;
    if (hx < e && hy < e)
        return Exit;
    for (int i = 0; i < (int)cols_.size() && i < 2; i++) {
        int cx = col_x(i);
        if (hx < cx - r || hx >= cx + r)
            continue;
        int py = px(tok::overlayPlusY), my = px(tok::overlayMinusY);
        if (hy >= py - r && hy < py + r)
            return cols_[(size_t)i].up ? Up0 + 2 * i : None;
        if (hy >= my - r && hy < my + r)
            return cols_[(size_t)i].down ? Down0 + 2 * i : None;
    }
    return None;
}

void AdjustHud::center(int h, fx& x, fx& y) const
{
    {
        Scope scope(Fit::Kept, "adjust");
        int i = (h - Up0) / 2;
        if (h == Exit) {
            x = y = tok::adjustExit / 2;
        } else {
            x = cols_.size() < 2 ? tok::canvasW / 2 : i == 0 ? tok::edge + tok::adjustColumn : tok::canvasW - tok::edge - tok::adjustColumn;
            y = (h - Up0) % 2 ? tok::overlayMinusY : tok::overlayPlusY;
        }
    }
    x = u_from(x, tok::u);
    y = u_from(y, tok::u);
}

int AdjustHud::number_at(fx x, fx y) const
{
    int hx = px(x), hy = px(y);
    for (int i = 0; i < (int)cols_.size() && i < 2; i++) {
        const Rect& r = num_rect_[i];
        if (hx >= r.x0 && hx < r.x1 && hy >= r.y0 && hy < r.y1)
            return i;
    }
    return -1;
}

void AdjustHud::compose(Fonts& f, GlyphCache& g, const Surface& page, const Surface& layer, uint8_t* mask,
                        const Surface& dst, int64_t now)
{
    Scope scope(Fit::Kept, "adjust");
    fx o = opacity(now);
    TextStyle ns = role::overlayNumber.style(), ls = role::overlayLabel.style();
    int base = px(tok::overlayRingY) + fx_round(fx_mul(fx_int(ns.px), tok::adjustNumberDrop));
    int n = std::min<int>((int)cols_.size(), 2);
    if (dirty_) {
        Canvas lc(layer, mask);
        lc.set_clip(kScreen);
        for (int i = 0; i < n; i++) {
            const std::string& num = cols_[(size_t)i].num;
            int w64 = f.width64(ns, num.c_str());
            int x64 = n < 2 ? to64(tok::edge) : col_x(i) * 64 - w64 / 2;
            num_rect_[i] = number_into(lc, f, ns, num, x64, base, num_rect_[i]);
        }
        dirty_ = false;
    }
    scrim(page, dst, tok::color_black, mul_a(o, tok::scrim_overlay_alpha));
    Canvas c(dst);
    c.set_clip(kScreen);
    int lit = mul_a(o, tok::overlayLit), dim = mul_a(o, tok::overlayDim);
    int gpx = px(tok::overlayGlyph);
    text(c, f, role::overlayLabel, "exit", to64(tok::edge), px(tok::edge) + f.ascent(ls.px), lit, kScreen);
    int ltop = px(tok::overlayMinusY + tok::overlayGlyph / 2);
    for (int i = 0; i < n; i++) {
        const Col& col = cols_[(size_t)i];
        int cx = col_x(i);
        draw_centered(c, g.glyph("plus", gpx), cx, px(tok::overlayPlusY), kWhite, col.up ? lit : dim);
        draw_centered(c, g.glyph("minus", gpx), cx, px(tok::overlayMinusY), kWhite, col.down ? lit : dim);
        int lw = f.width64(ls, col.label.c_str());
        text(c, f, role::overlayLabel, col.label, cx * 64 - lw / 2, ltop + f.ascent(ls.px), dim, kScreen);
    }
    TextStyle ts = role::toastTitle.style();
    int tb = px(tok::overlayTitleBottom) - f.descent(ts.px);
    Rect clip{px(tok::edge), 0, px(tok::canvasW - tok::edge), kPortraitH};
    text(c, f, role::toastTitle, line2_, to64(tok::edge), tb, mul_a(fx_mul(o, tok::overlayDim), role::toastTitle.alpha), clip);
    text(c, f, role::toastArtist, line1_, to64(tok::edge), tb - px(tok::overlayTextPitch),
         mul_a(fx_mul(o, tok::overlayDim), role::toastArtist.alpha), clip);
    for (int i = 0; i < n; i++) {
        fx s = pop_at_[i] < 0 ? kOne
                              : fx_lerp(tok::motion_toggleScale, kOne,
                                        curve(Curve::BezierPop, progress(now - pop_at_[i], ms(tok::motion_volumePop))));
        number_blit(layer, mask, dst, num_rect_[i], s, lit);
    }
}

void Toast::show(int64_t now, const std::string& artist, const std::string& title, bool track)
{
    Scope scope(Fit::Page, "toast");
    shows_++;
    Phase p = phase(now);
    if (p == In || p == Hold) {
        next_artist_ = artist;
        next_title_ = title;
        next_track_ = track;
        if (!track) {
            press_.drop();
            target_ = false;
        }
        swap_at_ = now;
        if (p == Hold)
            hold_at_ = now;
        return;
    }
    in_from_ = p == Out ? exp_out6_inverse(eased(now)) : 0;
    in_at_ = now;
    hold_at_ = now + ms(tok::motion_toastIn) * (int64_t)(kOne - in_from_) / kOne;
    swap_at_ = -1;
    artist_ = artist;
    title_ = title;
    track_ = track;
    reset();
    phase_ = In;
}

Toast::Phase Toast::phase(int64_t now) const
{
    if (phase_ == Hidden)
        return Hidden;
    int64_t hold_end = hold_at_ + ms(tok::motion_toastHold);
    if (now < hold_at_)
        return In;
    if (now < hold_end)
        return Hold;
    if (now < hold_end + ms(tok::motion_toastOut))
        return Out;
    return Hidden;
}

fx Toast::eased(int64_t now) const
{
    switch (phase(now)) {
    case In:
        return curve(Curve::ExpOut6, std::min<fx>(kOne, in_from_ + progress(now - in_at_, ms(tok::motion_toastIn))));
    case Hold:
        return kOne;
    case Out:
        return kOne - curve(Curve::ExpOut6,
                            progress(now - hold_at_ - ms(tok::motion_toastHold), ms(tok::motion_toastOut)));
    default:
        return 0;
    }
}

fx Toast::text_alpha(int64_t now) const
{
    if (swap_at_ < 0)
        return kOne;
    int64_t e = now - swap_at_;
    int d = ms(tok::motion_valueChange);
    if (e < d)
        return fx_lerp(kOne, tok::motion_valueDip, progress(e, d));
    return fx_lerp(tok::motion_valueDip, kOne, progress(e - d, d));
}

bool Toast::moving(int64_t now) const
{
    Scope scope(Fit::Page, "toast");
    Phase p = phase(now);
    return p == In || p == Out || (swap_at_ >= 0 && now < swap_at_ + 2 * ms(tok::motion_valueChange)) ||
           (p != Hidden && (press_.stale(now) || eased(now) != drawn_e_ || text_alpha(now) != drawn_ta_));
}

int64_t Toast::wake_at(int64_t now) const
{
    Scope scope(Fit::Page, "toast");
    Phase p = phase(now);
    int64_t w = p == Hold ? hold_at_ + ms(tok::motion_toastHold) : -1, d = press_.due(now);
    return w < 0 ? d : d < 0 ? w : std::min(w, d);
}

bool Toast::hit(int64_t now, fx x, fx y) const
{
    Scope scope(Fit::Page, "toast");
    fx e = eased(now);
    if (e <= 0 || !track())
        return false;
    fx top = fx_mul(-sz().toastTravel, kOne - e);
    return x >= 0 && x < canvas_w() && y >= top && y < top + sz().toastHeight;
}

Rect Toast::rect() const
{
    Scope scope(Fit::Page, "toast");
    return {0, 0, kPortraitW, px(sz().toastHeight)};
}

void Toast::draw(Canvas& c, Fonts& f, int64_t now)
{
    Scope scope(Fit::Page, "toast");
    if (swap_at_ >= 0 && now >= swap_at_ + ms(tok::motion_valueChange)) {
        artist_ = next_artist_;
        title_ = next_title_;
        track_ = next_track_;
    }
    fx ta = text_alpha(now);
    if (swap_at_ >= 0 && now >= swap_at_ + 2 * ms(tok::motion_valueChange))
        swap_at_ = -1;
    fx e = eased(now);
    drawn_e_ = e;
    drawn_ta_ = ta;
    if (e <= 0)
        return;
    int y = fx_round(to_px(fx_mul(-sz().toastTravel, kOne - e)));
    Rect strip{0, y, kPortraitW, y + px(sz().toastHeight)};
    Rect keep = c.clip();
    c.blend(strip, tok::color_black, mul_a(e, tok::scrim_overlay_alpha));
    Rect clip = Rect{px(tok::edge), y, px(canvas_w() - tok::edge), strip.y1};
    fx a = fx_mul(e, ta);
    Zoom z = tile_zoom(clip, press_.draw(now));
    text(c, f, toast_artist(), artist_, to64(tok::edge), y + px(sz().toastArtistBaseline), mul_a(a, toast_artist().alpha), clip, z);
    text(c, f, toast_title(), title_, to64(tok::edge), y + px(sz().toastTitleBaseline), mul_a(a, toast_title().alpha), clip, z);
    c.set_clip(keep);
}

void Toast::warm(const std::string& artist, const std::string& title, std::vector<WarmText>& out)
{
    Scope scope(Fit::Page, "toast");
    warm_add(out, toast_artist().style(), artist);
    warm_add(out, toast_title().style(), title);
}

void NavStrip::show(int64_t now)
{
    Phase p = phase(now);
    if (p == Hold) {
        hold_at_ = now;
        return;
    }
    if (p == In)
        return;
    in_from_ = p == Out ? exp_out6_inverse(eased(now)) : 0;
    in_at_ = now;
    hold_at_ = now + ms(tok::motion_toastIn) * (int64_t)(kOne - in_from_) / kOne;
    out_at_ = -1;
    phase_ = In;
}

void NavStrip::hide(int64_t now)
{
    Phase p = phase(now);
    if (p != In && p != Hold)
        return;
    out_from_ = eased(now);
    out_at_ = now;
}

NavStrip::Phase NavStrip::phase(int64_t now) const
{
    if (phase_ == Hidden)
        return Hidden;
    int64_t out = out_at_ >= 0 ? out_at_ : hold_at_ + ms(tok::motion_toastHold);
    if (now < out)
        return now < hold_at_ ? In : Hold;
    if (now < out + ms(tok::motion_toastOut))
        return Out;
    return Hidden;
}

fx NavStrip::eased(int64_t now) const
{
    switch (phase(now)) {
    case In:
        return curve(Curve::ExpOut6, std::min<fx>(kOne, in_from_ + progress(now - in_at_, ms(tok::motion_toastIn))));
    case Hold:
        return kOne;
    case Out: {
        int64_t out = out_at_ >= 0 ? out_at_ : hold_at_ + ms(tok::motion_toastHold);
        fx from = out_at_ >= 0 ? out_from_ : kOne;
        return fx_mul(from, kOne - curve(Curve::ExpOut6, progress(now - out, ms(tok::motion_toastOut))));
    }
    default:
        return 0;
    }
}

bool NavStrip::moving(int64_t now) const
{
    Phase p = phase(now);
    return p == In || p == Out || (p != Hidden && eased(now) != drawn_);
}

int64_t NavStrip::wake_at(int64_t now) const
{
    Phase p = phase(now);
    if (p == In)
        return hold_at_;
    if (p == Hold)
        return out_at_ >= 0 ? out_at_ : hold_at_ + ms(tok::motion_toastHold);
    return -1;
}

}
