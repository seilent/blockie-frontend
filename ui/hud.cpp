#include "hud.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

#include "../core/curves.h"
#include "../core/gesture.h"
#include "../gfx/compose.h"
#include "../gfx/xform.h"
#include "navbar.h"
#include "rows.h"
#include "size.h"
#include "tilt.h"

namespace fd {

namespace {

constexpr int ms(fx t) { return t >> kFxBits; }

constexpr bool kTilt = tok::motion_volumeTiltAngle > 0;

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

int cap_mid(int size) { return fx_round(fx_mul(fx_int(size), tok::capHeight / 2)); }

int mul_a(fx a, fx b) { return alpha256(fx_mul(a, b)); }

const Role& toast_artist() { return sz().toastArtist; }

const Role& toast_title() { return sz().toastTitle; }

const Role& hud_artist() { return role::toastArtist; }

const Role& hud_title() { return role::toastTitle; }

bool text(Canvas& c, Fonts& f, const Role& r, const std::string& s, int x64, int base, int a, Rect clip,
          const Zoom& z = {})
{
    Rect keep = c.clip();
    c.set_clip(z.rect(clip).intersect(keep));
    bool ok = c.clip().empty() || f.draw(c, r.style(), s.c_str(), x64, base, kWhite, a, z);
    c.set_clip(keep);
    return ok;
}

std::string upper(std::string s)
{
    for (char& ch : s)
        ch = (char)std::toupper((unsigned char)ch);
    return s;
}

Rect number_into(Canvas& lc, Fonts& f, const TextStyle& ns, const std::string& num, int x64, int base, Rect r, Rect old)
{
    if (!old.empty())
        lc.ground(old, tok::color_black);
    r = r.intersect(kScreen);
    lc.ground(r, tok::color_black);
    f.draw(lc, ns, num.c_str(), x64, base, kWhite, 256);
    return r;
}

Rect ascent_box(Fonts& f, const TextStyle& ns, const std::string& num, int x64, int base)
{
    int w = (f.width64(ns, num.c_str()) + 63) >> 6, x0 = x64 >> 6;
    return {x0, base - f.ascent(ns.px), x0 + w + 1, base + f.descent(ns.px)};
}

Rect ink_box(Fonts& f, const TextStyle& st, const std::string& s, int x64, int base)
{
    int l, r, t, b;
    f.ink64(st, s.c_str(), l, r);
    f.ink_rows(st, s.c_str(), t, b);
    return {((x64 + l) >> 6) - 1, base + t, ((x64 + r + 63) >> 6) + 1, base + b};
}

Rect cover_box(const Coverage& cv, int cx, int cy)
{
    Rect r{cv.w, cv.h, 0, 0};
    for (int y = 0; y < cv.h; y++)
        for (int x = 0; x < cv.w; x++)
            if (cv.rot[cv.index(x, y)]) {
                r.x0 = std::min(r.x0, x);
                r.y0 = std::min(r.y0, y);
                r.x1 = std::max(r.x1, x + 1);
                r.y1 = std::max(r.y1, y + 1);
            }
    if (r.empty())
        return {};
    int x0 = cx - cv.w / 2, y0 = cy - cv.h / 2;
    return {x0 + r.x0, y0 + r.y0, x0 + r.x1, y0 + r.y1};
}

void exit_place(Fonts& f, const TextStyle& es, int& x64, int& base)
{
    int l, r;
    f.ink64(es, "exit", l, r);
    x64 = to64(tok::vol_exitX) - (l + r) / 2;
    base = px(tok::vol_exitY) + fx_round(fx_mul(fx_int(es.px), tok::capHeight / 2));
}

void part_blit(const Surface& layer, uint8_t* mask, const Surface& dst, Rect r, fx s, int cx, int cy, int a)
{
    if constexpr (board::kMipPan)
        coverage_scaled(layer, (const uint8_t*)mask, dst, r, s, cx, cy, kWhite, a);
    else
        coverage_scaled(layer, dst, r, s, cx, cy, kWhite, a);
}

void number_blit(const Surface& layer, uint8_t* mask, const Surface& dst, Rect r, fx s, int a)
{
    int cx = (r.x0 + r.x1) / 2, cy = (r.y0 + r.y1) / 2;
    if constexpr (board::kMipPan)
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

void VolumeHud::show(int64_t now, int percent, bool playing, const std::string& artist, const std::string& title,
                     bool step)
{
    Scope scope(Fit::Kept, "vol");
    bool fresh = !visible(now);
    if (fresh) {
        in_at_ = now;
        chg_at_ = close_at_ = back_at_ = cross_at_ = press_up_ = held_at_ = -1;
        press_ = None;
        for (Coverage& cv : glow_)
            cv = Coverage{};
        num_shift_ = 0;
        num_from_ = kOne;
        at_rest_ = false;
        tilt_ = false;
        up_at_ = -1;
        artist_ = artist;
        title_ = title;
    } else {
        if (closing(now) || (fade_at_ >= 0 && now >= fade_at_))
            revive(now);
        if (percent != percent_) {
            num_from_ = number_k(now);
            chg_at_ = now;
            num_shift_ = 0;
            if (held_)
                held_at_ = now;
        }
        set_track(playing, artist, title, now);
    }
    exit_lit_ = exit_lit_ && own_;
    if (fresh ? step : percent != percent_)
        pop_at_ = now;
    num_dirty_ = num_dirty_ || percent != percent_ || !shown_;
    shown_ = true;
    percent_ = percent;
    playing_ = playing;
    rest_from(now);
}

bool VolumeHud::set_track(bool playing, const std::string& artist, const std::string& title, int64_t now)
{
    bool flip = playing != playing_;
    playing_ = playing;
    const std::string& a = cross_at_ >= 0 ? cross_artist_ : artist_;
    const std::string& t = cross_at_ >= 0 ? cross_title_ : title_;
    if (artist == a && title == t)
        return flip;
    if (now < 0 || !shown_) {
        artist_ = artist;
        title_ = title;
        cross_at_ = -1;
        return true;
    }
    cross_artist_ = artist;
    cross_title_ = title;
    int out = ms(tok::motion_volumeCrossOut), in = ms(tok::motion_volumeCrossIn);
    if (cross_at_ < 0 || now >= cross_at_ + out + in)
        cross_at_ = now;
    else if (now >= cross_at_ + out)
        cross_at_ = now - (((int64_t)(kOne - progress(now - cross_at_ - out, in)) * out) >> kFxBits);
    return true;
}

void VolumeHud::hide(int64_t now)
{
    if (close_at_ < 0 && shown_) {
        Look l = look(now);
        close_from_ = rest(now);
        close_scrim_ = scrim_k(now);
        close_k_ = l.k;
        close_size_ = l.size;
        close_at_ = now;
    }
    tilt_up(now);
    hold_end(now);
    drop_touch();
}

void VolumeHud::revive(int64_t now)
{
    back_from_ = rest(now);
    scrim_from_ = scrim_k(now);
    back_at_ = now;
    close_at_ = -1;
    at_rest_ = true;
}

fx VolumeHud::rest(int64_t now) const
{
    if (!shown_)
        return 0;
    if (close_at_ >= 0)
        return fx_mul(close_from_, kOne - progress(now - close_at_, ms(tok::motion_volumeTouchClose)));
    int back_ms = ms(tok::motion_volumeNumberBack);
    fx back = back_at_ >= 0 ? std::min<fx>(kOne, back_from_ + progress(now - back_at_, back_ms)) : kOne;
    if (fade_at_ < 0 || now < fade_at_)
        return back;
    int slow = ms(tok::motion_volumeRestSlow);
    if (now < fade_at_ + slow)
        return fx_lerp(back, tok::motion_volumeRestLevel, progress(now - fade_at_, slow));
    return fx_lerp(tok::motion_volumeRestLevel, 0, progress(now - fade_at_ - slow, ms(tok::motion_volumeRestFast)));
}

fx VolumeHud::scrim_k(int64_t now) const
{
    if (!shown_)
        return 0;
    if (close_at_ >= 0)
        return fx_mul(close_scrim_, kOne - progress(now - close_at_, ms(tok::motion_volumeTouchClose)));
    fx in = back_at_ >= 0 ? std::min<fx>(kOne, scrim_from_ + progress(now - back_at_, ms(tok::motion_volumeNumberBack)))
                          : progress(now - in_at_, ms(tok::motion_volumeOpen));
    if (fade_at_ < 0)
        return in;
    int64_t out = fade_at_ + ms(tok::motion_volumeRestSlow);
    return now < out ? in : fx_mul(in, kOne - progress(now - out, ms(tok::motion_volumeScrimOut)));
}

fx VolumeHud::number_k(int64_t now) const
{
    if (!shown_)
        return 0;
    int64_t t = held_ && held_at_ >= 0 ? std::min(now, held_at_) : now, h = number_at();
    fx own = t < h ? kOne : kOne - curve(Curve::EaseIn, progress(t - h, ms(tok::motion_volumeNumberFade)));
    if (chg_at_ >= 0 && num_from_ < kOne)
        own = std::min<fx>(own, num_from_ + progress(now - chg_at_, ms(tok::motion_volumeNumberBack)));
    return own;
}

VolumeHud::Look VolumeHud::look(int64_t now) const
{
    Look l;
    if (!shown_) {
        l.group = l.number = l.exit = l.text = l.scrim = l.queue = 0;
        return l;
    }
    fx r = rest(now);
    int64_t e = now - in_at_, lead = ms(tok::motion_volumeGroupLead);
    l.k = at_rest_ ? kOne : curve(Curve::PowOut17, progress(e + lead, ms(tok::motion_volumeGroup)));
    l.size = tok::motion_volumeGroupScale + fx_mul(kOne - tok::motion_volumeGroupScale, l.k);
    int slow = ms(tok::motion_volumeRestSlow);
    if (close_at_ >= 0) {
        fx p = progress(now - close_at_, ms(tok::motion_volumeTouchClose));
        l.k = fx_lerp(close_k_, tok::motion_volumeCloseK, p);
        l.size = fx_lerp(close_size_, tok::motion_volumeCloseSize, p);
    } else if (fade_at_ >= 0 && now >= fade_at_ && now < fade_at_ + slow) {
        l.k = fx_lerp(kOne, tok::motion_volumeCloseSlowK, progress(now - fade_at_, slow));
        l.size = kOne;
    } else if (fade_at_ >= 0 && now >= fade_at_ + slow) {
        fx p = progress(now - fade_at_ - slow, ms(tok::motion_volumeRestFast));
        l.k = fx_lerp(tok::motion_volumeCloseSlowK, tok::motion_volumeCloseK, p);
        l.size = fx_lerp(kOne, tok::motion_volumeCloseSize, p);
    }
    l.group = fx_mul(curve(Curve::CubicOut, progress(e + lead, ms(tok::motion_volumeGroupFade))), r);
    fx num = curve(Curve::CubicOut, progress(e - ms(tok::motion_volumeNumberAt), ms(tok::motion_volumeNumberIn)));
    l.number = fx_mul(fx_mul(num, number_k(now)), r);
    fx exit = curve(Curve::CubicOut, progress(e - ms(tok::motion_volumeExitAt), ms(tok::motion_volumeExitIn)));
    l.exit = fx_mul(exit, r);
    fx text = curve(Curve::EaseOut, progress(e - ms(tok::motion_volumeTextFadeAt), ms(tok::motion_volumeTextFadeIn)));
    l.queue = fx_mul(text, r);
    if (cross_at_ >= 0) {
        int out = ms(tok::motion_volumeCrossOut);
        text = fx_mul(text, now < cross_at_ + out ? kOne - progress(now - cross_at_, out)
                                                  : progress(now - cross_at_ - out, ms(tok::motion_volumeCrossIn)));
    }
    l.text = fx_mul(text, r);
    fx slid = curve(Curve::EaseOut, progress(e - ms(tok::motion_volumeTextAt), ms(tok::motion_volumeTextSlide)));
    l.text_dx = fx_mul(tok::motion_volumeTextShift, kOne - slid);
    l.scrim = scrim_k(now);
    return l;
}

void VolumeHud::drop_touch()
{
    if (tilt_ && up_at_ < 0)
        tilt_ = false;
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
    tilt_up(now);
    hold_end(now);
    drop_touch();
    exit_lit_ = false;
    rest_from(now);
}

void VolumeHud::anchor(int percent)
{
    if (drag_ != Drag::Volume)
        return;
    from_ = to_ = keep_ = percent;
    ay_ = ly_;
}

int VolumeHud::hit(fx x, fx y, fx k, fx size) const
{
    fx r = fx_mul(tok::vol_hit / 2, size), cx = tok::canvasW / 2, ry = tok::vol_ringY;
    auto in = [&](fx gx, fx gy, fx h) { return x >= gx - h && x < gx + h && y >= gy - h && y < gy + h; };
    if (in(tok::vol_exitX, tok::vol_exitY, tok::adjustExit / 2))
        return Exit;
    if (queue_ && queue_y_ >= 0 && in(tok::vol_queueX, queue_y_, tok::vol_hit / 2))
        return Queue;
    struct Part {
        int hit;
        fx x, y, h;
    };
    const Part parts[] = {{Plus, cx, ry + fx_mul(tok::vol_plusY - ry, k), r},
                          {Minus, cx, ry + fx_mul(tok::vol_minusY - ry, k), r},
                          {Prev, cx - fx_mul(tok::vol_skipX, k), ry, r},
                          {Ring, cx, ry, fx_mul(tok::vol_ring / 2, size)},
                          {Next, cx + fx_mul(tok::vol_skipX, k), ry, r}};
    int best = None;
    int64_t near = 0;
    for (const Part& p : parts)
        if (in(p.x, p.y, p.h)) {
            int64_t d = ((int64_t)x - p.x) * ((int64_t)x - p.x) + ((int64_t)y - p.y) * ((int64_t)y - p.y);
            if (best == None || d < near) {
                best = p.hit;
                near = d;
            }
        }
    return best;
}

VolumeHud::Act VolumeHud::event(const Event& in, int64_t now, int percent, const Pos& at)
{
    Scope scope(Fit::Kept, "vol");
    Event e = scoped(in);
    if (e.kind == Ev::TouchDown) {
        if (!visible(now) || closing(now) || e.y >= canvas_h() - std_u(tok::gesture_bottom))
            return Act::Pass;
        Look lk = look(now);
        if (fade_at_ >= 0 && now >= fade_at_)
            revive(now);
        own_ = held_ = true;
        held_at_ = now;
        drop_ = seeking_ = false;
        fade_at_ = -1;
        drag_ = Drag::Tap;
        hit_ = at_rest_ ? hit(e.x, e.y) : hit(e.x, e.y, lk.k, lk.size);
        at_rest_ = true;
        press_ = hit_ == Prev || hit_ == Next || hit_ == Ring ? hit_ : None;
        press_up_ = -1;
        exit_lit_ = hit_ == Exit;
        row_ = e.y >= tok::vol_ringY - tok::vol_hit / 2 && e.y < tok::vol_ringY + tok::vol_hit / 2;
        x0_ = e.x;
        y0_ = ly_ = e.y;
        if (kTilt) {
            built_ = built_ && tilt_;
            tilt_ = true;
            up_at_ = -1;
            tilt_at(e.x, e.y);
        }
        return Act::Draw;
    }
    if (!own_)
        return Act::None;
    if (e.kind == Ev::TouchMove) {
        Act a = slide(e, percent, at);
        if (tilt_)
            tilt_at(e.x, e.y);
        bool far = std::max(std::abs(e.x - drawn_tx_), std::abs(e.y - drawn_ty_)) >= fx_int(1);
        plate_moved_ = tilt_ && far && corners_moved();
        bool lit = exit_lit_, pressed = press_ != None && press_up_ < 0;
        exit_lit_ = lit && drag_ == Drag::Tap;
        if (pressed && drag_ != Drag::Tap)
            press_up_ = now;
        return a == Act::None && (plate_moved_ || lit != exit_lit_ || (pressed && press_up_ >= 0)) ? Act::Draw : a;
    }
    if (e.kind != Ev::TouchUp)
        return Act::None;
    tilt_up(now);
    hold_end(now);
    if (press_ != None && press_up_ < 0)
        press_up_ = now;
    own_ = held_ = false;
    rest_from(now);
    Drag d = drag_;
    drag_ = Drag::Tap;
    if (d == Drag::Seek) {
        seeking_ = false;
        return Act::Seek;
    }
    if (d != Drag::Tap || !touch_tap(x0_, y0_, e.x, e.y)) {
        bool lit = exit_lit_;
        exit_lit_ = false;
        return lit ? Act::Draw : Act::None;
    }
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
    case Queue:
        return Act::Queue;
    default:
        hide(now);
        return Act::None;
    }
}

VolumeHud::Act VolumeHud::slide(const Event& e, int percent, const Pos& at)
{
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
        int64_t stride = tok::vol_slideStride, d = ((int64_t)ay_ - e.y) * step_;
        int n = std::clamp(from_ + (int)((d + (d < 0 ? -stride : stride) / 2) / stride), 0, 100);
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

void VolumeHud::tilt_at(fx x, fx y)
{
    TiltPose p = tilt_pose((double)x / canvas_w(), (double)y / canvas_h(), tok::motion_volumeTiltAngle / 65536.0,
                           to_px(tok::motion_volumeTiltDepth) / 65536.0);
    rx_ = p.rx;
    ry_ = p.ry;
    depth_ = p.depth;
    tx_ = x;
    ty_ = y;
}

fx VolumeHud::tilt_k(int64_t now) const
{
    if (!tilt_)
        return 0;
    if (up_at_ < 0)
        return kOne;
    return kOne - progress(now - up_at_ - ms(tok::motion_volumeTiltHold), ms(tok::motion_volumeTiltReturn));
}

void VolumeHud::tilt(int64_t now, double& rx, double& ry, double& depth) const
{
    double q = tilt_k(now) / 65536.0;
    rx = rx_ * q;
    ry = ry_ * q;
    depth = depth_ * q;
}

void VolumeHud::plate(fx k, double* h) const
{
    Scope scope(Fit::Kept, "vol");
    tilt_matrix({rx_, ry_, depth_}, k / 65536.0, kPortraitW / 2.0, kPortraitH / 2.0,
                to_px(tok::motion_perspective) / 65536.0, h);
}

void VolumeHud::corners(const double* h, int* out) const
{
    for (int i = 0; i < kBoxes; i++) {
        const Rect& r = box_[i];
        int j = i * 8;
        for (int y : {r.y0, r.y1})
            for (int x : {r.x0, r.x1}) {
                double w = h[6] * x + h[7] * y + h[8];
                out[j++] = (int)std::lround((h[0] * x + h[1] * y + h[2]) / w);
                out[j++] = (int)std::lround((h[3] * x + h[4] * y + h[5]) / w);
            }
    }
}

bool VolumeHud::corners_moved() const
{
    double h[9];
    plate(kOne, h);
    int c[kBoxes * 8];
    corners(h, c);
    return !std::equal(c, c + kBoxes * 8, corner_);
}

int64_t VolumeHud::wake_at(int64_t now) const
{
    int64_t hold = ms(tok::motion_volumeTiltHold), w = -1;
    int64_t r = tilt_ && up_at_ >= 0 ? up_at_ + hold : -1;
    int64_t still = board::kTiltNearMove && tilt_ && near_drawn_ && up_at_ < 0 ? moved_at_ + hold : -1;
    int64_t fast = fade_at_ >= 0 ? fade_at_ + ms(tok::motion_volumeRestSlow) : -1;
    int64_t gone = fade_at_ >= 0 ? fast + ms(tok::motion_volumeScrimOut) : -1;
    for (int64_t c : {r, still, end_at(), shown_ && !held_ ? number_at() : -1, fade_at_, fast, gone})
        if (c > now && (w <= now || c < w))
            w = c;
    return w;
}

#ifndef FD_ALSA
void VolumeHud::shot_seek(int64_t to, int64_t dur)
{
    seeking_ = true;
    seek_to_ = to;
    readout_ = seek_text(to, dur);
}

void VolumeHud::shot_tilt(double rx, double ry, double depth)
{
    built_ = built_ && tilt_;
    tilt_ = true;
    up_at_ = -1;
    rx_ = rx;
    ry_ = ry;
    depth_ = depth;
}
#endif

fx VolumeHud::opacity(int64_t now) const
{
    Scope scope(Fit::Kept, "vol");
    return look(now).group;
}

bool VolumeHud::moving(int64_t now) const
{
    Scope scope(Fit::Kept, "vol");
    if (!shown_)
        return false;
    Look l = look(now);
    const int drawn[8] = {alpha256(l.group), alpha256(l.number), alpha256(l.exit), alpha256(l.text),
                          alpha256(l.scrim), (int)(l.k >> 6), (int)(l.size >> 6), fx_round(to_px(l.text_dx))};
    return now < end_at() || now < pop_at_ + ms(tok::motion_volumePop) || !std::equal(drawn, drawn + 8, drawn_look_) ||
           (press_ != None && press_up_ >= 0 && now < press_up_ + ms(tok::motion_volumePressOff)) ||
           (cross_at_ >= 0 && now < cross_at_ + ms(tok::motion_volumeCrossOut) + ms(tok::motion_volumeCrossIn)) ||
           pop(now) != drawn_pop_ || tilt_k(now) != drawn_k_ ||
           (tilt_ && up_at_ >= 0 && now >= up_at_ + ms(tok::motion_volumeTiltHold) && drawn_k_ != 0) ||
           (board::kTiltNearMove && tilt_ && near_drawn_ &&
            (up_at_ >= 0 || now >= moved_at_ + ms(tok::motion_volumeTiltHold)));
}

fx VolumeHud::pop(int64_t now) const
{
    return fx_lerp(tok::motion_toggleScale, kOne,
                   curve(Curve::BezierPop, progress(now - pop_at_, ms(tok::motion_volumePop))));
}

void VolumeHud::draw_box(int i, Fonts& f, GlyphCache& g, Canvas& lc, bool fresh)
{
    int cx = kPortraitW / 2, ry = px(tok::vol_ringY);
    auto begin = [&](Rect r) {
        if (!fresh && !box_[i].empty())
            lc.ground(box_[i], tok::color_black);
        box_[i] = r.intersect(kScreen);
        lc.ground(box_[i], tok::color_black);
        lc.set_clip(box_[i]);
    };
    switch (i) {
    case BExit: {
        TextStyle es = role::volExit.style();
        int x64, base;
        exit_place(f, es, x64, base);
        begin(ink_box(f, es, "exit", x64, base));
        f.draw(lc, es, "exit", x64, base, kWhite, 256);
        break;
    }
    case BPlus:
    case BMinus: {
        const char* name = i == BPlus ? "plus" : "minus";
        int gpx = px(i == BPlus ? tok::vol_plus : tok::vol_minus);
        int y = px(i == BPlus ? tok::vol_plusY : tok::vol_minusY);
        begin(cover_box(g.glyph(name, gpx), cx, y));
        draw_centered(lc, g.glyph(name, gpx), cx, y, kWhite, 256);
        break;
    }
    case BRow: {
        int spx = px(tok::vol_skip), gx = px(tok::vol_skipX), rpx = px(tok::vol_ring), stroke = px(tok::vol_ringStroke);
        const char* mid = playing_ ? "pauseWide" : "play";
        if (fresh) {
            part_[0] = cover_box(g.glyph("skipPrevious", spx), cx - gx, ry);
            part_[2] = cover_box(g.glyph("skipNext", spx), cx + gx, ry);
            part_[1] = cover_box(g.ring(rpx, stroke), cx, ry)
                           .unite(cover_box(g.glyph("pauseWide", pause_px()), cx, ry))
                           .unite(cover_box(g.glyph("play", pause_px()), cx, ry));
            Rect sides = part_[0].unite(part_[2]).unite(cover_box(g.ring(rpx, stroke), cx, ry));
            row_ink_[0] = sides.unite(cover_box(g.glyph("pauseWide", pause_px()), cx, ry));
            row_ink_[1] = sides.unite(cover_box(g.glyph("play", pause_px()), cx, ry));
        }
        TextStyle rs = role::overlayNumber.style();
        int rb = ry + fx_round(fx_mul(fx_int(rs.px), tok::adjustNumberDrop));
        int rx64 = cx * 64 - f.width64(rs, readout_.c_str()) / 2;
        read_ = seeking_ ? ink_box(f, rs, readout_, rx64, rb).intersect(kScreen) : Rect{};
        Rect ink = row_ink_[playing_ ? 0 : 1].unite(read_);
        int held = press_up_ < 0 ? press_ : None;
        auto glow_box = [&](const Coverage& cv, int x) {
            int x0 = x - cv.w / 2, y0 = ry - cv.h / 2;
            return cv.w ? Rect{x0, y0, x0 + cv.w, y0 + cv.h} : Rect{};
        };
        if (held == Prev)
            ink = ink.unite(glow_box(glow_[0], cx - gx));
        else if (held == Next)
            ink = ink.unite(glow_box(glow_[2], cx + gx));
        else if (held == Ring)
            ink = ink.unite(glow_box(glow_[1], cx));
        begin(ink);
        if (held == Prev)
            draw_centered(lc, glow_[0], cx - gx, ry, kWhite, 256);
        else if (held == Next)
            draw_centered(lc, glow_[2], cx + gx, ry, kWhite, 256);
        else if (held == Ring) {
            draw_centered(lc, glow_[1], cx, ry, kWhite, 256);
            int d = rpx - 2 * stroke;
            draw_centered(lc, g.ring(d, d / 2), cx, ry, kWhite, alpha256(tok::vol_discAlpha));
        }
        draw_centered(lc, g.glyph("skipPrevious", spx), cx - gx, ry, kWhite, 256);
        draw_centered(lc, g.glyph("skipNext", spx), cx + gx, ry, kWhite, 256);
        draw_centered(lc, g.ring(rpx, stroke), cx, ry, kWhite, 256);
        draw_centered(lc, g.glyph(mid, pause_px()), cx, ry, kWhite, 256);
        if (!read_.empty()) {
            Canvas pc(lc.surface());
            pc.set_clip(read_);
            pc.fill(read_, tok::color_black);
            f.draw(pc, rs, readout_.c_str(), rx64, rb, kWhite, 256);
        }
        row_playing_ = playing_;
        row_seeking_ = seeking_;
        row_readout_ = readout_;
        row_press_ = held;
        break;
    }
    case BLabel: {
        int top;
        TextStyle ls = label_style(top);
        label_px_ = ls.px;
        int x64 = cx * 64 - f.width64(ls, "volume") / 2, base = top + f.ascent(ls.px);
        begin(ink_box(f, ls, "volume", x64, base));
        f.draw(lc, ls, "volume", x64, base, kWhite, 256);
        break;
    }
    case BQueue: {
        int qx = px(tok::vol_queueX), qy = text_top(f) + px(tok::vol_queueDrop), rpx = px(tok::vol_queueRing);
        queue_y_ = fx_div(fx_int(qy), ppu());
        const Coverage& ring = g.ring(rpx, px(tok::vol_queueStroke));
        begin(queue_ ? cover_box(ring, qx, qy).unite(cover_box(g.glyph("queue", rpx), qx, qy)) : Rect{});
        if (!box_[i].empty()) {
            draw_centered(lc, ring, qx, qy, kWhite, 256);
            draw_centered(lc, g.glyph("queue", rpx), qx, qy, kWhite, 256);
        }
        break;
    }
    default: {
        bool artist = i == BArtist;
        TextStyle st = (artist ? hud_artist() : hud_title()).style();
        const std::string& line = artist ? artist_ : title_;
        int base = text_top(f) + cap_mid(hud_artist().style().px);
        if (!artist)
            base += px(tok::overlayTextPitch);
        Rect clip{px(tok::vol_textX), 0, text_right(), kPortraitH};
        begin(line.empty() ? Rect{} : ink_box(f, st, line, to64(tok::vol_textX), base).intersect(clip));
        if (!box_[i].empty())
            f.draw(lc, st, line.c_str(), to64(tok::vol_textX), base, kWhite, 256);
        (artist ? box_artist_ : box_title_) = line;
        break;
    }
    }
}

void VolumeHud::build_glows(GlyphCache& g)
{
    if (glow_[1].w)
        return;
    int spx = px(tok::vol_skip), rpx = px(tok::vol_ring), skip = px(tok::vol_glowSkip);
    glow_coverage(g.glyph("skipPrevious", spx), skip, glow_[0]);
    glow_coverage(g.ring(rpx, px(tok::vol_ringStroke)), px(tok::vol_glowRing), glow_[1], rpx / 2);
    glow_coverage(g.glyph("skipNext", spx), skip, glow_[2]);
}

fx VolumeHud::press_k(int64_t now) const
{
    if (press_ == None)
        return 0;
    if (press_up_ < 0)
        return kOne;
    return press_ == Ring ? 0 : kOne - progress(now - press_up_, ms(tok::motion_volumePressOff));
}

void VolumeHud::build_boxes(Fonts& f, GlyphCache& g, const Surface& layer, uint8_t* mask)
{
    Canvas lc(layer, mask);
    bool fresh = !built_;
    if (press_ != None)
        build_glows(g);
    bool row = row_playing_ != playing_ || row_seeking_ != seeking_ || (seeking_ && row_readout_ != readout_) ||
               row_press_ != (press_up_ < 0 ? press_ : None);
    for (int i = 0; i < kBoxes; i++) {
        bool dirty = fresh || (i == BRow && row) || (i == BArtist && box_artist_ != artist_) ||
                     (i == BTitle && box_title_ != title_);
        if (i == BNumber || !dirty)
            continue;
        lc.set_clip(kScreen);
        draw_box(i, f, g, lc, fresh);
    }
    built_ = true;
    box_[BNumber] = num_rect_;
}

void VolumeHud::draw_plate(Fonts& f, GlyphCache& g, const Surface& layer, uint8_t* mask, const Surface& dst,
                           const Look& l, fx k, fx s, int64_t now)
{
    build_boxes(f, g, layer, mask);
    double h[9];
    plate(k, h);
    int c[kBoxes * 8], was[kBoxes * 8];
    corners(h, c);
    if (plated_) {
        std::copy(corner_, corner_ + kBoxes * 8, was);
    } else {
        const double flat[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
        corners(flat, was);
    }
    bool moved = !std::equal(c, c + kBoxes * 8, was);
    int64_t hold = ms(tok::motion_volumeTiltHold);
    bool settle = near_drawn_ && (up_at_ >= 0 ? now < up_at_ + hold : now >= moved_at_ + hold);
    bool near = board::kTiltNearMove && moved && !settle, motion = board::kTiltNearMove ? near : moved;
    fx line = fx_mul(l.text, tok::overlayLit);
    int lit = mul_a(l.group, tok::overlayLit), read_a = mul_a(l.group, tok::overlayDim);
    const int alpha[kBoxes] = {mul_a(l.exit, exit_lit_ ? tok::overlayLit : tok::overlayDim),
                               mul_a(l.number, tok::overlayLit),
                               lit,
                               lit,
                               lit,
                               lit,
                               mul_a(line, hud_artist().alpha),
                               mul_a(line, hud_title().alpha),
                               mul_a(l.queue, tok::overlayLit)};
    for (int i = 0; i < kBoxes; i++) {
        if (box_[i].empty())
            continue;
        double m[9];
        const double* q = h;
        if (i == BNumber && s != kOne) {
            double z = s / 65536.0, bx = (box_[i].x0 + box_[i].x1) / 2.0, by = (box_[i].y0 + box_[i].y1) / 2.0;
            const double pop[9] = {z, 0, bx * (1 - z), 0, z, by * (1 - z), 0, 0, 1};
            mat3_mul(h, pop, m);
            q = m;
        }
        if constexpr (!std::is_same_v<LiveL, Rot>) {
            if (i == BRow && !read_.empty())
                coverage_homography(layer, nullptr, dst, read_, q, kWhite, read_a, near, motion);
            coverage_homography(layer, mask, dst, box_[i], q, kWhite, alpha[i], near, motion);
        }
    }
    std::copy(c, c + kBoxes * 8, corner_);
    plated_ = true;
    near_drawn_ = near;
    if (near)
        moved_at_ = now;
    drawn_tx_ = tx_;
    drawn_ty_ = ty_;
}

void VolumeHud::compose(Fonts& f, GlyphCache& g, const Surface& page, const Surface& layer, uint8_t* mask,
                        const Surface& dst, int64_t now)
{
    Scope scope(Fit::Kept, "vol");
    int out = ms(tok::motion_volumeCrossOut);
    if (cross_at_ >= 0 && now >= cross_at_ + out) {
        artist_ = cross_artist_;
        title_ = cross_title_;
        if (now >= cross_at_ + out + ms(tok::motion_volumeCrossIn))
            cross_at_ = -1;
    }
    Look l = look(now);
    fx k = tilt_k(now);
    if (!k) {
        tilt_ = false;
        up_at_ = -1;
    }
    int cx = kPortraitW / 2, ry = px(tok::vol_ringY);
    bool scaled = l.size < kOne;
    if (num_dirty_ || ((k || scaled) && !built_)) {
        std::string num = (percent_ < 10 ? "0" : "") + std::to_string(percent_);
        TextStyle ns = number_style(f, num);
        int nx64, base;
        number_place(f, ns, num, nx64, base);
        Canvas lc(layer, mask);
        lc.set_clip(kScreen);
        num_rect_ = number_into(lc, f, ns, num, nx64, base, ink_box(f, ns, num, nx64, base), num_rect_);
        num_dirty_ = false;
    }
    scrim(page, dst, tok::color_black, mul_a(l.scrim, tok::scrim_overlay_alpha));
    fx s = pop(now);
    const int drawn[8] = {alpha256(l.group), alpha256(l.number), alpha256(l.exit), alpha256(l.text),
                          alpha256(l.scrim), (int)(l.k >> 6), (int)(l.size >> 6), fx_round(to_px(l.text_dx))};
    std::copy(drawn, drawn + 8, drawn_look_);
    drawn_pop_ = s;
    drawn_k_ = k;
    if constexpr (!std::is_same_v<LiveL, Rot>) {
        if (k && mask) {
            draw_plate(f, g, layer, mask, dst, l, k, s, now);
            return;
        }
    }
    plated_ = near_drawn_ = false;
    Canvas c(dst);
    c.set_clip(kScreen);
    int lit = mul_a(l.group, tok::overlayLit), dim = mul_a(l.group, tok::overlayDim);
    auto place = [&](int rest, int r0) { return r0 + (int)(((int64_t)(rest - r0) * l.k + kOne / 2) >> kFxBits); };
    if (seeking_) {
        TextStyle rs = role::overlayNumber.style();
        int rb = ry + fx_round(fx_mul(fx_int(rs.px), tok::adjustNumberDrop));
        text(c, f, role::overlayNumber, readout_, cx * 64 - f.width64(rs, readout_.c_str()) / 2, rb, dim, kScreen);
    }
    if (scaled && lit > 0) {
        build_boxes(f, g, layer, mask);
        auto part = [&](Rect r, int rest_x, int rest_y) {
            if (r.empty())
                return;
            int64_t q = kOne - l.size, w = (int64_t)l.k - l.size;
            int px0 = cx + (int)((int64_t)(rest_x - cx) * w / q), py0 = ry + (int)((int64_t)(rest_y - ry) * w / q);
            part_blit(layer, mask, dst, r, l.size, px0, py0, lit);
        };
        int sx = px(tok::vol_skipX);
        part(part_[0], cx - sx, ry);
        part(part_[1], cx, ry);
        part(part_[2], cx + sx, ry);
        part(box_[BPlus], cx, px(tok::vol_plusY));
        part(box_[BMinus], cx, px(tok::vol_minusY));
        Rect lb = box_[BLabel];
        part(lb, (lb.x0 + lb.x1) / 2, (lb.y0 + lb.y1) / 2);
    } else if (!scaled) {
        draw_centered(c, g.glyph("plus", px(tok::vol_plus)), cx, place(px(tok::vol_plusY), ry), kWhite, lit);
        draw_centered(c, g.glyph("minus", px(tok::vol_minus)), cx, place(px(tok::vol_minusY), ry), kWhite, lit);
        int spx = px(tok::vol_skip), sx = px(tok::vol_skipX);
        fx pk = press_k(now);
        if (pk > 0) {
            build_glows(g);
            int ga = mul_a(fx_mul(l.group, pk), tok::overlayLit);
            if (press_ == Prev)
                draw_centered(c, glow_[0], place(cx - sx, cx), ry, kWhite, ga);
            else if (press_ == Next)
                draw_centered(c, glow_[2], place(cx + sx, cx), ry, kWhite, ga);
            else if (press_ == Ring) {
                int rp = px(tok::vol_ring), d = rp - 2 * px(tok::vol_ringStroke);
                draw_centered(c, glow_[1], cx, ry, kWhite, ga);
                draw_centered(c, g.ring(d, d / 2), cx, ry, kWhite, mul_a(fx_mul(l.group, pk), tok::vol_discAlpha));
            }
        }
        draw_centered(c, g.glyph("skipPrevious", spx), place(cx - sx, cx), ry, kWhite, lit);
        draw_centered(c, g.glyph("skipNext", spx), place(cx + sx, cx), ry, kWhite, lit);
        draw_centered(c, g.ring(px(tok::vol_ring), px(tok::vol_ringStroke)), cx, ry, kWhite, lit);
        draw_centered(c, g.glyph(playing_ ? "pauseWide" : "play", pause_px()), cx, ry, kWhite, lit);
        int vtop;
        TextStyle ls = label_style(vtop);
        label_px_ = ls.px;
        int vy = vtop + f.ascent(ls.px), vc = vtop + f.ascent(ls.px) / 2;
        f.draw(c, ls, "volume", cx * 64 - f.width64(ls, "volume") / 2, vy + place(vc, ry) - vc, kWhite, lit);
    }
    TextStyle es = role::volExit.style();
    int ex64, ebase;
    exit_place(f, es, ex64, ebase);
    text(c, f, role::volExit, "exit", ex64, ebase, mul_a(l.exit, exit_lit_ ? tok::overlayLit : tok::overlayDim),
         kScreen);
    int top = text_top(f), ab = top + cap_mid(hud_artist().style().px);
    Rect clip{px(tok::vol_textX), 0, text_right(), kPortraitH};
    fx line = fx_mul(l.text, tok::overlayLit);
    text(c, f, hud_title(), title_, to64(tok::vol_textX + l.text_dx), ab + px(tok::overlayTextPitch),
         mul_a(line, hud_title().alpha), clip);
    text(c, f, hud_artist(), artist_, to64(tok::vol_textX + l.text_dx), ab, mul_a(line, hud_artist().alpha), clip);
    int qy = top + px(tok::vol_queueDrop);
    queue_y_ = fx_div(fx_int(qy), ppu());
    if (queue_) {
        int qx = px(tok::vol_queueX + l.text_dx), rpx = px(tok::vol_queueRing), qa = mul_a(l.queue, tok::overlayLit);
        draw_centered(c, g.ring(rpx, px(tok::vol_queueStroke)), qx, qy, kWhite, qa);
        draw_centered(c, g.glyph("queue", rpx), qx, qy, kWhite, qa);
    }
    number_blit(layer, mask, dst, num_rect_, s, mul_a(l.number, tok::overlayLit));
}

int VolumeHud::pause_px()
{
    int p = px(tok::vol_pause);
    if ((p - px(tok::vol_ring)) & 1)
        p += to_px(tok::vol_pause) < fx_int(p) ? -1 : 1;
    return p;
}

int VolumeHud::number_right64() { return to64(tok::canvasW / 2 - tok::vol_plus * 3 / 8 - tok::vol_numberGap); }

TextStyle VolumeHud::number_style(Fonts& f, const std::string& num)
{
    TextStyle ns = role::volNumber.style();
    int room = number_right64() - to64(tok::edge);
    auto ink = [&] {
        int l, r;
        f.ink64(ns, num.c_str(), l, r);
        return r - l;
    };
    int w = ink();
    if (w > room)
        ns.px = ns.px * room / w;
    while (ns.px > 1 && ink() > room)
        ns.px--;
    return ns;
}

void VolumeHud::number_place(Fonts& f, const TextStyle& ns, const std::string& num, int& x64, int& base)
{
    int l, r;
    f.ink64(ns, num.c_str(), l, r);
    x64 = ns.px < role::volNumber.style().px ? to64(tok::edge) - l : number_right64() - r;
    base = px(tok::vol_numberY) + fx_round(fx_mul(fx_int(ns.px), tok::capHeight / 2));
}

TextStyle VolumeHud::label_style(int& top)
{
    top = px(tok::vol_minusY + tok::overlayGlyph / 2);
    return role::volLabel.style();
}

int VolumeHud::text_top(Fonts& f)
{
    Scope scope(Fit::Kept, "vol");
    int top;
    TextStyle ls = label_style(top);
    int label = top + f.ascent(ls.px) - cap_mid(ls.px);
    return std::max(px(tok::vol_textY), label + px(tok::vol_textGap));
}

int VolumeHud::text_right() const
{
    return px(queue_ ? tok::vol_queueX - tok::vol_queueRing / 2 - tok::vol_numberGap : tok::canvasW - tok::edge);
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
    out.push_back({"pauseWide", pause_px()});
    out.push_back({nullptr, px(tok::vol_queueRing), px(tok::vol_queueStroke)});
    out.push_back({"queue", px(tok::vol_queueRing)});
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
            num_rect_[i] = number_into(lc, f, ns, num, x64, base, ascent_box(f, ns, num, x64, base), num_rect_[i]);
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
    placed_ = p == Out;
    hold_at_ = now + in_ms() * (int64_t)(kOne - in_from_) / kOne;
    swap_at_ = -1;
    artist_ = artist;
    title_ = title;
    track_ = track;
    reset();
    phase_ = In;
}

void Toast::set_place(Place p, int64_t now)
{
    if (placed_ || phase(now) == Hidden)
        return;
    place_ = p;
    placed_ = true;
    in_at_ = now;
    hold_at_ = now + in_ms() * (int64_t)(kOne - in_from_) / kOne;
}

void Toast::to_top(int64_t now)
{
    if (!bottom())
        return;
    if (phase(now) == In) {
        in_from_ = std::min<fx>(kOne, in_from_ + progress(now - in_at_, in_ms()));
        in_at_ = now;
        place_ = Top;
        hold_at_ = now + in_ms() * (int64_t)(kOne - in_from_) / kOne;
        return;
    }
    place_ = Top;
}

int Toast::in_ms() const { return ms(place_ == Bottom ? tok::motion_toastRise : tok::motion_toastIn); }

fx Toast::surface() const { return nav_ ? NavBar::top() : canvas_h(); }

int Toast::surface_px() const { return nav_ ? NavBar::band().y0 : kPortraitH; }

int Toast::strip_y(fx e) const
{
    if (bottom())
        return surface_px() - px(sz().toastHeight) + fx_round(to_px(fx_mul(sz().toastTravel, kOne - e)));
    return fx_round(to_px(fx_mul(-sz().toastTravel, kOne - e)));
}

Toast::Phase Toast::phase(int64_t now) const
{
    if (phase_ == Hidden)
        return Hidden;
    if (!placed_) {
        int64_t life = ms(tok::motion_toastIn) + ms(tok::motion_toastHold) + ms(tok::motion_toastOut);
        return now < in_at_ + life ? In : Hidden;
    }
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
        if (!placed_)
            return 0;
        return curve(Curve::ExpOut6, std::min<fx>(kOne, in_from_ + progress(now - in_at_, in_ms())));
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
    if (bottom()) {
        fx s = surface(), top = s - sz().toastHeight + fx_mul(sz().toastTravel, kOne - e);
        return x >= 0 && x < canvas_w() && y >= top && y < s;
    }
    fx top = fx_mul(-sz().toastTravel, kOne - e);
    return x >= 0 && x < canvas_w() && y >= top && y < top + sz().toastHeight;
}

Rect Toast::rect() const
{
    Scope scope(Fit::Page, "toast");
    if (bottom()) {
        int b = surface_px();
        return {0, b - px(sz().toastHeight), kPortraitW, b};
    }
    return {0, 0, kPortraitW, px(sz().toastHeight)};
}

bool Toast::look(int64_t now, Look& l)
{
    Scope scope(Fit::Page, "toast");
    set_place(place_, now);
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
        return false;
    l.y = strip_y(e);
    l.h = px(sz().toastHeight);
    l.strip = Rect{0, l.y, kPortraitW, l.y + l.h};
    if (bottom())
        l.strip.y1 = std::min(l.strip.y1, surface_px());
    l.scrim = mul_a(e, tok::scrim_overlay_alpha);
    l.clip = Rect{px(tok::edge), l.y, px(canvas_w() - tok::edge), l.strip.y1};
    l.a = fx_mul(e, ta);
    l.zoom = press_.draw(now);
    return true;
}

bool Toast::text_at(Canvas& c, Fonts& f, int y, Rect clip, fx a, fx zoom) const
{
    Scope scope(Fit::Page, "toast");
    Zoom z = tile_zoom(clip, zoom);
    bool ok = text(c, f, toast_artist(), artist_, to64(tok::edge), y + px(sz().toastArtistBaseline),
                   mul_a(a, toast_artist().alpha), clip, z);
    return text(c, f, toast_title(), title_, to64(tok::edge), y + px(sz().toastTitleBaseline),
                mul_a(a, toast_title().alpha), clip, z) &&
           ok;
}

int Toast::height_max()
{
    Scope scope(Fit::Std, "toast");
    return std::max(px(tok::toastHeight), fx_round(fx_mul(tok::toastHeight, large_ppu())));
}

void Toast::draw(Canvas& c, Fonts& f, int64_t now)
{
    Scope scope(Fit::Page, "toast");
    Look l;
    if (!look(now, l))
        return;
    Rect keep = c.clip();
    c.blend(l.strip, tok::color_black, l.scrim);
    text_at(c, f, l.y, l.clip, l.a, l.zoom);
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
