#include "portmsg.h"

#include <algorithm>

#include "../gfx/wrap.h"
#include "../ui/busydots.h"
#include "../ui/roles.h"
#include "ring_spin.h"

namespace fd {

namespace {

constexpr Case kTitleCase = Case::Mixed;
constexpr size_t kTextRows = 10;
constexpr const char* kTextMore = "...";
constexpr fx kRingRight = fx_int(464);
constexpr fx kRingCy = (fx)(71.3 * kOne);
constexpr fx kRingTitleClip = fx_int(432);
constexpr int kChooseRows = 6;
constexpr int64_t kFallEndMs = 583;
constexpr int64_t kFallSpanMs = 333;

constexpr int kRows = 6;
constexpr fx kBaselineEm = (fx)(0.9897 * kOne);
constexpr int64_t kStep3 = 250;
constexpr int64_t kLeg3 = 3 * kStep3;
constexpr int kAppearCap = 4;
constexpr int kPartCap = 4;
constexpr int kPickCap = 3;
constexpr int64_t kDimNum = 73728;
constexpr int64_t kDimDen = 100000;
constexpr int64_t kGlowDen3 = 200;
constexpr int64_t kGlowMs = 67;
constexpr int64_t kRingStepMs = 250;
constexpr size_t kGoneMax = 2;

fx covered(int64_t tau3)
{
    if (tau3 <= 0)
        return 0;
    if (tau3 >= kLeg3)
        return kOne;
    int64_t k = tau3 / kStep3;
    fx f = (fx)((tau3 % kStep3) * kOne / kStep3);
    return (fx)((k * kOne + fx_mul(f, f)) / 3);
}

int a256_of(fx alpha, fx glow) { return alpha256(fx_mul(alpha, glow)); }

void push_block(std::vector<AlphaRegion>& regions, Rect r, int a256)
{
    r = r.intersect(kScreen);
    if (r.empty())
        return;
    if (regions.size() > 1) {
        AlphaRegion& b = regions.back();
        if (b.a256 == a256 && b.r.x0 == r.x0 && b.r.x1 == r.x1 && b.r.y1 == r.y0) {
            b.r.y1 = r.y1;
            return;
        }
    }
    regions.push_back({r, a256});
}

}

bool MsgView::ring(const MsgContent& c) const { return c.mode == MsgMode::Progress && c.total > 0; }

int msg_band(int row, int picked, int cap)
{
    return picked >= 0 && row == picked ? cap + 1 : std::min(std::max(row, 0) + 1, cap);
}

int MsgView::band_of(const Layer& l, int row) const { return msg_band(row, l.picked, l.cap); }

int MsgView::bands(const Layer& l) const { return l.cap + (l.picked >= 0 ? 1 : 0); }

int64_t MsgView::end_of(const Layer& l) const
{
    return l.t0 + ((l.swap ? 0 : bands(l)) * kStep3 + kLeg3) / 3 + kGlowMs;
}

int MsgView::dx_of(const Layer& l, int band, int64_t now) const
{
    if (l.move == Move::Still)
        return 0;
    fx cov = covered((now - l.t0) * 3 - (l.swap ? 0 : band) * kStep3);
    int w = fx_round(fx_mul(fx_int(kPortraitW), cov));
    if (l.move == Move::In)
        return kPortraitW - w;
    return l.move == Move::Out ? -w : w;
}

fx MsgView::glow_of(const Layer& l, int band, int64_t now) const
{
    if (l.move != Move::In)
        return kOne;
    int64_t tau3 = (now - l.t0) * 3 - (l.swap ? 0 : band) * kStep3;
    return (fx)std::clamp<int64_t>(tau3 * kOne / kGlowDen3, 0, kOne);
}

int MsgView::scrim_at(int64_t now) const
{
    int64_t d = std::max<int64_t>(0, now - scrim_t0_) * kDimNum / kDimDen;
    if (scrim_target_ >= scrim_from_)
        return (int)std::min<int64_t>(scrim_target_, scrim_from_ + d);
    return (int)std::max<int64_t>(scrim_target_, scrim_from_ - d);
}

void MsgView::scrim_to(int a256, int64_t now, int64_t hold)
{
    scrim_from_ = scrim_at(now);
    scrim_target_ = a256;
    scrim_t0_ = now + hold;
}

void MsgView::show(const MsgContent& c, int64_t now)
{
    Layer n;
    n.c = c;
    n.move = Move::In;
    n.t0 = now;
    n.cap = kAppearCap;
    if (shown()) {
        Layer old = cur_;
        old.move = Move::Out;
        old.t0 = now;
        old.cap = kAppearCap;
        old.focus = focus_;
        old.top = top_;
        old.log_off = log_.at(now);
        old.swap = n.swap = true;
        old.link = n.link = ++links_;
        old.matched = false;
        old.same.clear();
        old.cover.clear();
        if (gone_.size() >= kGoneMax)
            gone_.erase(gone_.begin());
        gone_.push_back(std::move(old));
    }
    cur_ = std::move(n);
    scrim_to(theme().scrim_dialog_a256, now);
    focus_ = 0;
    top_ = 0;
    bar_.set(0);
    alpha_.assign(c.mode == MsgMode::Choose ? c.lines.size() : 0, Glide());
    for (size_t i = 0; i < alpha_.size(); i++)
        alpha_[i].set(alpha256(i == 0 ? role::dialogItem.alpha : tok::type_dialogItem_alphaDim));
    log_.set(0);
    line_at_ = now - kGlowMs;
    ring_done_ = c.done;
    ring_total_ = c.total;
    ring_at_ = -1;
    spin0_ = now;
}

void MsgView::line(const std::string& l, int64_t now)
{
    if (cur_.c.mode != MsgMode::Progress)
        return;
    int pitch = px(tok::settingsPitch);
    int from = log_.at(now);
    std::vector<std::string>& lines = cur_.c.lines;
    lines.push_back(l);
    if (lines.size() > kMsgLogLines) {
        lines.erase(lines.begin());
        from -= pitch;
    }
    int target = std::max(0, (int)lines.size() - kRows) * pitch;
    log_.set(from);
    log_.to(target, now, fx_round(tok::motion_scroll), Curve::EaseIn);
    line_at_ = now;
}

void MsgView::progress(int64_t done, int64_t total, int64_t now)
{
    cur_.c.done = done;
    cur_.c.total = total;
    if (ring_at_ >= 0 && now - ring_at_ < kRingStepMs && ring_total_ > 0)
        return;
    ring_done_ = done;
    ring_total_ = total;
    ring_at_ = now;
}

bool MsgView::move(bool down, int64_t now)
{
    int n = cur_.c.mode == MsgMode::Choose ? (int)cur_.c.lines.size() : 0;
    int to = focus_ + (down ? 1 : -1);
    if (to < 0 || to >= n)
        return false;
    int was = focus_;
    focus_ = to;
    if (focus_ < top_)
        top_ = focus_;
    if (focus_ >= top_ + kChooseRows)
        top_ = focus_ - kChooseRows + 1;
    bar_.to(focus_ * px(tok::settingsPitch), now, fx_round(tok::motion_focusCursor), Curve::Sine);
    int ms = fx_round(tok::motion_focusAlpha);
    alpha_[(size_t)was].to(alpha256(tok::type_dialogItem_alphaDim), now, ms, Curve::Linear);
    alpha_[(size_t)focus_].to(alpha256(role::dialogItem.alpha), now, ms, Curve::Linear);
    return true;
}

void MsgView::close(int picked, int64_t now)
{
    if (shown()) {
        Layer old = cur_;
        old.move = Move::Part;
        old.t0 = now;
        old.cap = picked >= 0 ? kPickCap : kPartCap;
        old.picked = picked >= 0 ? picked - top_ : -1;
        old.focus = focus_;
        old.top = top_;
        old.log_off = log_.at(now);
        old.swap = false;
        if (gone_.size() >= kGoneMax)
            gone_.erase(gone_.begin());
        gone_.push_back(std::move(old));
    }
    cur_ = Layer{};
    int s = scrim_at(now);
    int full = std::max(1, theme().scrim_dialog_a256);
    scrim_to(0, now, std::max<int64_t>(0, kFallEndMs - kFallSpanMs * std::min(s, full) / full));
}

void MsgView::land(int64_t now)
{
    if (cur_.move == Move::In)
        cur_.move = Move::Still;
    gone_.clear();
    scrim_from_ = scrim_at(now);
    scrim_target_ = scrim_from_;
    scrim_t0_ = now;
    bar_.land();
    for (Glide& g : alpha_)
        g.land();
    log_.land();
}

bool MsgView::animating(int64_t now) const
{
    if (scrim_at(now) != scrim_target_ || bar_.running(now) || log_.running(now) || now - line_at_ < kGlowMs)
        return true;
    for (const Glide& g : alpha_)
        if (g.running(now))
            return true;
    for (const Layer& l : gone_)
        if (now < end_of(l))
            return true;
    return cur_.move == Move::In && now < end_of(cur_);
}

int64_t MsgView::deadline(int64_t now) const
{
    if (animating(now))
        return now + kMsgFrameMs;
    if (cur_.c.mode != MsgMode::Progress)
        return -1;
    if (cur_.c.total > 0 && (ring_done_ != cur_.c.done || ring_total_ != cur_.c.total))
        return ring_at_ + kRingStepMs;
    if (cur_.c.total > 0)
        return -1;
    return now + kBusyFrameMs;
}

void MsgView::lay(Layer& l, Fonts& f) const
{
    if (l.laid)
        return;
    l.laid = true;
    l.rows.clear();
    if (l.c.mode != MsgMode::Text || l.c.lines.empty())
        return;
    TextStyle ms = role::npAlbum.style();
    ms.tcase = Case::Mixed;
    int width64 = (kPortraitW - 2 * px(tok::edge)) * 64;
    const std::string& t = l.c.lines[0];
    size_t i = 0;
    while (l.rows.size() <= kTextRows) {
        size_t nl = t.find('\n', i);
        std::string piece = t.substr(i, nl == std::string::npos ? std::string::npos : nl - i);
        if (piece.empty())
            l.rows.push_back(std::string());
        for (std::string& r : wrap_lines(f, ms, piece, width64, kTextRows + 1 - l.rows.size()))
            l.rows.push_back(std::move(r));
        if (nl == std::string::npos)
            break;
        i = nl + 1;
    }
    if (l.rows.size() <= kTextRows)
        return;
    l.rows.resize(kTextRows);
    std::string& last = l.rows.back();
    while (!last.empty() && f.width64(ms, (last + kTextMore).c_str()) > width64) {
        while (!last.empty() && ((unsigned char)last.back() & 0xC0) == 0x80)
            last.pop_back();
        if (!last.empty())
            last.pop_back();
        while (!last.empty() && last.back() == ' ')
            last.pop_back();
    }
    last += kTextMore;
}

void MsgView::match(Fonts& f)
{
    if (!cur_.swap || cur_.matched)
        return;
    cur_.matched = true;
    Layer* old = nullptr;
    for (Layer& l : gone_)
        if (l.swap && l.link == cur_.link)
            old = &l;
    if (!old)
        return;
    lay(cur_, f);
    lay(*old, f);
    old->matched = true;
    const std::vector<std::string>& a = old->c.mode == MsgMode::Text ? old->rows : old->c.lines;
    const std::vector<std::string>& b = cur_.c.mode == MsgMode::Text ? cur_.rows : cur_.c.lines;
    bool geom = old->c.mode == cur_.c.mode && old->log_off == 0 && old->top == 0;
    bool title = old->c.title == cur_.c.title && ring(old->c) == ring(cur_.c) &&
                 (old->c.mode == MsgMode::Progress) == (cur_.c.mode == MsgMode::Progress);
    auto fill = [&](Layer& l, size_t n, size_t other) {
        l.same.assign(n + 1, 0);
        l.cover.assign(n + 1, 0);
        l.same[0] = title;
        l.cover[0] = 1;
        for (size_t r = 0; r < n; r++) {
            bool cover = geom && r < other;
            bool bar = old->c.mode == MsgMode::Choose && ((int)r == old->focus) != (r == 0);
            l.cover[r + 1] = cover;
            l.same[r + 1] = cover && a[r] == b[r] && !bar;
        }
    };
    fill(*old, a.size(), b.size());
    fill(cur_, b.size(), a.size());
}

void MsgView::draw_layer(Layer& l, Canvas& k, Fonts& f, GlyphCache& g, const Theme& th, int64_t now,
                         std::vector<AlphaRegion>& regions)
{
    lay(l, f);
    const bool cur = &l == &cur_;
    const int edge = px(tok::edge), right = kPortraitW - edge, top = px(tok::contentTop);
    const int pitch = px(tok::settingsPitch), bottom = top + kRows * pitch;
    const int block = scrim_at(now);
    Rect keep = k.clip();
    struct Slot {
        bool on;
        int dx;
        fx glow;
    };
    auto band = [&](int b, int e, int y0, int y1) {
        bool same = l.swap && e < (int)l.same.size() && l.same[(size_t)e];
        bool cover = l.swap && e < (int)l.cover.size() && l.cover[(size_t)e];
        if (same && l.move == Move::Out)
            return Slot{false, 0, 0};
        int dx = same ? 0 : dx_of(l, b, now);
        if (dx >= kPortraitW || dx <= -kPortraitW)
            return Slot{false, 0, 0};
        Rect r{dx, y0, dx + kPortraitW, y1};
        if (!cover || l.move != Move::Out)
            push_block(regions, cover ? Rect{0, y0, kPortraitW, y1} : r, block);
        if (l.move == Move::In)
            ink_clear(k.surface(), r.intersect(keep));
        return Slot{true, dx, same ? kOne : glow_of(l, b, now)};
    };
    auto clip = [&](int dx, int x1, int y0, int y1) { k.set_clip(Rect{dx, y0, dx + x1, y1}.intersect(keep)); };

    Slot t0 = band(0, 0, 0, top);
    if (t0.on) {
        int dx = t0.dx;
        fx glow = t0.glow;
        bool rg = ring(l.c);
        if (!l.c.title.empty()) {
            TextStyle ts = role::dialogTitle.style();
            ts.tcase = kTitleCase;
            clip(dx, rg ? px(kRingTitleClip) : right, 0, top);
            f.draw(k, ts, l.c.title.c_str(), (edge + dx) * 64, px(tok::pivotBaseline), kWhite,
                   a256_of(role::dialogTitle.alpha, glow));
        }
        k.set_clip(keep);
        if (rg) {
            int d = px(tok::rowRing), stroke = px(tok::ringStrokeIdle);
            int cx = dx + px(kRingRight) - d / 2, cy = px(kRingCy);
            int64_t done = cur ? ring_done_ : l.c.done, total = cur ? ring_total_ : l.c.total;
            RingArc ra = ring_arc(done, total, tok::ringArcStart >> kFxBits);
            if (ra.sweep > 0)
                draw_centered(k, g.arc(d, stroke, ra.start, ra.sweep), cx, cy, kWhite, alpha256(glow));
        } else if (l.c.mode == MsgMode::Progress) {
            int y = px(kDotsY);
            busy_dots(k, Rect{dx + px(kDotsX0), y, dx + px(kDotsX1), y + px(kDotSide)}, kWhite, now - spin0_);
            k.set_clip(keep);
        }
    }

    if (l.c.mode == MsgMode::Text) {
        TextStyle ms = role::npAlbum.style();
        ms.tcase = Case::Mixed;
        int tp = px(tok::npAlbumBaseline - tok::npTextTop);
        int base = px(tok::contentTop + fx_mul(tok::type_npAlbum_size, kBaselineEm));
        for (int r = 0; r < (int)l.rows.size(); r++) {
            int b = band_of(l, r);
            Slot on = band(b, r + 1, top + r * tp, top + (r + 1) * tp);
            if (!on.on)
                continue;
            clip(on.dx, right, top, bottom);
            f.draw(k, ms, l.rows[(size_t)r].c_str(), (edge + on.dx) * 64, base + r * tp, kWhite,
                   a256_of(role::npAlbum.alpha, on.glow));
        }
    } else if (l.c.mode == MsgMode::Progress) {
        TextStyle vs = role::settingsValue.style();
        vs.tcase = Case::Mixed;
        int asc = f.ascent(vs.px), desc = f.descent(vs.px);
        int off = cur ? log_.at(now) : l.log_off;
        int n = (int)l.c.lines.size();
        for (int i = 0; i < n; i++) {
            int y = top + i * pitch - off;
            if (y + pitch <= top || y >= bottom)
                continue;
            int row = y >= top ? (y - top) / pitch : -1;
            int b = band_of(l, row);
            Slot on = band(b, i + 1, y, y + pitch);
            if (!on.on)
                continue;
            bool newest = i == n - 1;
            fx glow = on.glow;
            if (newest && cur)
                glow = fx_mul(glow, (fx)std::clamp<int64_t>((now - line_at_) * 3 * kOne / kGlowDen3, 0, kOne));
            clip(on.dx, right, top, bottom);
            f.draw(k, vs, l.c.lines[(size_t)i].c_str(), (edge + on.dx) * 64, y + (pitch - asc - desc) / 2 + asc,
                   kWhite, a256_of(newest ? kOne : tok::type_settingsValue_alpha, glow));
        }
    } else if (l.c.mode == MsgMode::Choose) {
        TextStyle is = role::dialogItem.style();
        int asc = f.ascent(is.px), desc = f.descent(is.px);
        int wtop = cur ? top_ : l.top, focus = cur ? focus_ : l.focus;
        int n = (int)l.c.lines.size();
        for (int r = 0; r < kChooseRows && wtop + r < n; r++) {
            int i = wtop + r, y = top + r * pitch;
            int b = band_of(l, r);
            Slot on = band(b, i + 1, y, y + pitch);
            if (!on.on)
                continue;
            fx a = cur ? (fx)((int64_t)alpha_[(size_t)i].at(now) * kOne / 256)
                       : (i == focus ? role::dialogItem.alpha : tok::type_dialogItem_alphaDim);
            clip(on.dx, right, top, bottom);
            f.draw(k, is, l.c.lines[(size_t)i].c_str(), (edge + on.dx) * 64, y + (pitch - asc - desc) / 2 + asc,
                   kWhite, a256_of(a, on.glow));
            if (i != focus)
                continue;
            int by = top + (cur ? bar_.at(now) - wtop * pitch : r * pitch);
            int bx = on.dx + edge - px(tok::focus_barOffset);
            clip(0, kPortraitW, top, bottom);
            k.blend(Rect{bx, by, bx + px(tok::focus_barWidth), by + pitch}, kWhite, alpha256(on.glow));
        }
    }
    k.set_clip(keep);
}

void MsgView::draw(Canvas& c, Fonts& f, GlyphCache& g, InkLayer& ink, const Theme& th, int64_t now,
                   std::vector<AlphaRegion>& regions)
{
    regions.clear();
    gone_.erase(std::remove_if(gone_.begin(), gone_.end(), [&](const Layer& l) { return now >= end_of(l); }),
                gone_.end());
    if (cur_.move == Move::In && now >= end_of(cur_))
        cur_.move = Move::Still;
    bool owed = ring_done_ != cur_.c.done || ring_total_ != cur_.c.total;
    if (cur_.c.total > 0 && now - ring_at_ >= kRingStepMs && owed) {
        ring_done_ = cur_.c.done;
        ring_total_ = cur_.c.total;
        ring_at_ = now;
    }
    match(f);
    Rect sr = c.surface().rect();
    for (;;) {
        regions.clear();
        c.fill(sr, th.scrim_dialog);
        regions.push_back({sr, scrim_at(now)});
        Canvas& k = ink.begin(sr);
        for (Layer& l : gone_)
            draw_layer(l, k, f, g, th, now, regions);
        if (shown())
            draw_layer(cur_, k, f, g, th, now, regions);
        if (regions.size() <= (size_t)kWlAlphaMax || gone_.empty())
            break;
        gone_.erase(gone_.begin());
    }
    if (regions.size() > (size_t)kWlAlphaMax)
        regions.resize(1);
    ink.compose(c.surface(), th.ink);
}

}
