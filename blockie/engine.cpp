#include "engine.h"

#include <algorithm>
#include <cassert>
#include <numeric>

#include "../core/curves.h"
#include "../gfx/pixel.h"
#include "blend_row.h"
#include "tile_grid.h"

namespace fd {
namespace eng {

namespace {

constexpr int kIds = kIdMask + 1;

int step_by(int step, int64_t dt3) { return (int)((int64_t)step * 256 * dt3 / 50); }

uint32_t shade(uint32_t src, uint32_t ground, int e, int sigma, const uint16_t* lut)
{
    int a = (int)(((int64_t)e * 256 + kFull / 2) / kFull);
    a = (unsigned)a <= 256 ? lut[a] : std::min(256, (a * 256 + sigma / 2) / sigma);
    return a >= 256 ? src : a <= 0 ? ground : blend_px(ground, src, a);
}

uint32_t shade_argb(uint32_t src, uint32_t ground, int e, int sigma, const uint16_t* lut)
{
    int a = (int)(((int64_t)e * 256 + kFull / 2) / kFull);
    a = (unsigned)a <= 256 ? lut[a] : std::min(256, (a * 256 + sigma / 2) / sigma);
    if (a >= 256)
        return src;
    uint32_t out = src & 0xFF000000u;
    if (a <= 0)
        return out | (ground & 0xFFFFFFu);
    for (int sh = 0; sh < 24; sh += 8) {
        int g = (int)((ground >> sh) & 0xFF), v = (int)((src >> sh) & 0xFF);
        out |= (uint32_t)(g + (((v - g) * a + 128) >> 8)) << sh;
    }
    return out;
}

struct RuleIn {
    bool ring;
    bool hold;
    bool dep;
    bool carried;
    bool stale;
    bool glowing;
    bool edge;
    bool tiles;
    int t;
    int prev;
    int step;
    int eprev;
    int back;
    uint16_t was;
    uint16_t old;
};

inline int rule_e(const RuleIn& r, bool rev, int64_t dt3, int dim_inc, int ring_inc, bool& lit)
{
    int t = r.t, e = t, base = r.stale ? 0 : r.prev;
    lit = false;
    if (r.ring && r.hold) {
        if (r.carried) {
            e = std::min(r.prev, t);
        } else if (r.old) {
            e = std::min(t, ring_inc);
            lit = e < t;
        } else {
            e = std::min(r.back, t);
        }
    } else if (r.ring) {
        e = std::min(kFull, base + ring_inc);
        lit = (r.stale || r.glowing) && e < kFull;
    } else if (r.dep) {
        if (rev && r.carried && !(r.was & kRingBit)) {
            e = std::min(r.prev, t);
        } else if (rev && !r.carried && r.old) {
            e = std::min(t, step_by(r.step, dt3));
            lit = e < t;
        } else if (rev) {
            e = std::min(r.back, t);
        }
    } else if (!r.carried && r.old) {
        if (r.tiles) {
            e = t > r.eprev ? std::min(t, r.eprev + dim_inc) : t;
        } else {
            e = std::min(t, step_by(r.step, dt3));
            lit = e < t;
        }
    } else if (r.edge) {
        e = t;
    } else if (r.stale || r.glowing) {
        e = std::min(t, base + step_by(r.step, dt3));
        lit = e < t;
    } else {
        e = t > base ? std::min(t, base + dim_inc) : t;
    }
    return e;
}

Rect pose_span(const Pose& q, const Block& b, Rect lim, int src_w)
{
    Rect out = q.rect.intersect(lim).intersect(
        {q.rect.x0, q.rect.y0, q.rect.x0 + (b.src.x1 - b.src.x0), q.rect.y0 + (b.src.y1 - b.src.y0)});
    int sx0 = q.rect.x0 - b.src.x0;
    out.x0 = std::max(out.x0, sx0);
    out.x1 = std::min(out.x1, sx0 + src_w);
    return out;
}

int64_t sig_of(const Pose& q)
{
    int64_t v = 0;
    for (int64_t f : {(int64_t)q.rect.x0, (int64_t)q.rect.y0, (int64_t)q.rect.x1, (int64_t)q.rect.y1})
        v = v * 1000003 + f + 1;
    return v;
}

}

void blend_row(uint32_t* p, int n, uint32_t ground, int a) { fd::blend_row(p, n, ground, a); }

void Instance::s1_emit_row(int ry)
{
    const Seg gap{0, 0, 0, (uint8_t)kAgeMs};
    int gw = s1_gw_, mx0 = s1_mx0_;
    segs_.row(ry);
    int y = s1_my0_ + ry;
    std::vector<S1Span>& sp = s1_[(size_t)ry];
    if (y < s1_y0_ || y >= s1_y1_) {
        segs_.emit(0, gw, gap);
        return;
    }
    std::sort(sp.begin(), sp.end(), [](const S1Span& a, const S1Span& b) { return a.u < b.u; });
    uint16_t* ep = e_.data() + (size_t)ry * gw;
    uint8_t* ap = age_.data() + (size_t)ry * gw;
    int act0 = gw, act1 = 0, x = 0;
    for (const S1Span& sv : sp) {
        int u = sv.u - mx0, v = sv.v - mx0;
        segs_.emit(x, u, gap);
        int sl = slot_[(size_t)(sv.id & kIdMask)];
        const Slot* h = sl >= 0 && (size_t)sl < tab_.size() && tab_[(size_t)sl].on ? &tab_[(size_t)sl] : nullptr;
        if (h) {
            uint16_t e = (uint16_t)((sv.id & kRingBit) ? kFull : h->t);
            uint16_t k = (uint16_t)(sv.id & kIdMask);
            if (k && !owns_[k]) {
                owns_[k] = 1;
                owned_.push_back(k);
            }
            std::fill(ep + u, ep + v, e);
            std::fill(ap + u, ap + v, (uint8_t)0);
            act0 = std::min(act0, u);
            act1 = v;
            segs_.emit(u, v, {sv.id, e, 0, 0});
        } else {
            segs_.emit(u, v, {sv.id, 0, 0, (uint8_t)kAgeMs});
        }
        x = v;
    }
    segs_.emit(x, gw, gap);
    act_lo_[(size_t)ry] = (int16_t)act0;
    act_hi_[(size_t)ry] = (int16_t)act1;
}

void Instance::s1_finish()
{
    segs_.end(s1_gh_);
    seg_ok_ = true;
    if (verify_) {
        RowRuns<Seg> built;
        built.swap(segs_);
        seg_build();
        bool same = built.at == segs_.at && built.runs.size() == segs_.runs.size();
        for (size_t k = 0; same && k < built.runs.size(); k++)
            same = built.runs[k].x0 == segs_.runs[k].x0 && built.runs[k].x1 == segs_.runs[k].x1 &&
                   built.runs[k].p == segs_.runs[k].p;
        verify_scans_++;
        verify_fails_ += !same;
        segs_.swap(built);
    }
}

bool Instance::prepare_start(const Surface& dst, const Frame& start, const std::vector<Surface>& sources,
                             uint32_t ground, int clip_x, int rows)
{
    if (start_ready_)
        return true;
    if (s1_row_ < 0) {
        if (!seg_ || !prepared())
            return true;
        begin(0, false);
        due_ = 0;
        s1_defer_ = true;
        pass(dst, start, sources, ground, clip_x, false);
        s1_defer_ = false;
        due_ = -1;
        inited_ = false;
        settled_ = false;
        if (s1_row_ < 0)
            return true;
        seed_ = start;
        seed_src_.resize(sources.size());
        for (size_t k = 0; k < sources.size(); k++)
            seed_src_[k] = sources[k].row(0);
        seed_ground_ = ground;
        seed_clip_ = clip_x;
        seed_w_ = dst.w;
        seed_h_ = dst.h;
        return false;
    }
    int r1 = std::min(s1_gh_, s1_row_ + std::max(1, rows));
    for (int ry = s1_row_; ry < r1; ry++)
        s1_emit_row(ry);
    s1_row_ = r1;
    if (r1 < s1_gh_)
        return false;
    s1_finish();
    s1_row_ = -1;
    start_ready_ = true;
    return true;
}

char Instance::start_key(const Surface& dst, const Frame& start, const std::vector<Surface>& sources, uint32_t ground,
                         int clip_x) const
{
    auto re = [](const Rect& a, const Rect& b) { return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1; };
    bool same = seed_.blocks.size() == start.blocks.size() && seed_.poses.size() == start.poses.size() &&
                seed_.shades.size() == start.shades.size();
    for (size_t k = 0; same && k < start.blocks.size(); k++) {
        const Block &a = seed_.blocks[k], &b = start.blocks[k];
        same = a.id == b.id && a.ink == b.ink && a.z == b.z && a.source == b.source && re(a.src, b.src) &&
               a.tile == b.tile;
    }
    for (size_t k = 0; same && k < start.poses.size(); k++) {
        const Pose &a = seed_.poses[k], &b = start.poses[k];
        same = a.block == b.block && re(a.rect, b.rect) && re(a.clip, b.clip);
    }
    for (size_t k = 0; same && k < start.shades.size(); k++) {
        const Shade &a = seed_.shades[k], &b = start.shades[k];
        same = a.t == b.t && a.end == b.end && a.step == b.step && a.sigma == b.sigma && a.dep == b.dep;
    }
    if (!same || !re(seed_.ground, start.ground))
        return 'f';
    if (seed_ground_ != ground)
        return 'g';
    bool srcs = seed_src_.size() == sources.size();
    for (size_t k = 0; srcs && k < sources.size(); k++)
        srcs = seed_src_[k] == (const void*)sources[k].row(0);
    if (!srcs)
        return 's';
    if (seed_clip_ != clip_x || seed_w_ != dst.w || seed_h_ != dst.h)
        return 'c';
    return 0;
}

void Instance::seg_build()
{
    int gw = rect_.x1 - rect_.x0, gh = rect_.y1 - rect_.y0;
    segs_.begin(gh);
    for (int r = 0; r < gh; r++) {
        segs_.row(r);
        size_t row = (size_t)r * gw;
        const uint16_t* id = ids_.data() + row;
        const uint16_t* e = e_.data() + row;
        const uint8_t* a = age_.data() + row;
        const uint16_t* hd = held_.data() + row;
        for (int x = 0; x < gw;) {
            uint16_t held = id[x] ? 0 : hd[x];
            int b = x + 1;
            while (b < gw && id[b] == id[x] && e[b] == e[x] && a[b] == a[x] && (id[b] ? 0 : hd[b]) == held)
                b++;
            segs_.emit(x, b, {id[x], e[x], held, a[x]});
            x = b;
        }
    }
    segs_.end(gh);
    seg_ok_ = true;
}

fx step_progress(int64_t t3, int steps)
{
    if (t3 <= 0 || steps <= 0)
        return 0;
    if (t3 >= (int64_t)steps * kStepThirds)
        return fx_int(steps);
    int j = (int)(t3 / kStepThirds);
    fx tau = (fx)((t3 - (int64_t)j * kStepThirds) * kOne / kStepThirds);
    return fx_int(j) + curve(Curve::EaseIn, tau);
}

Instance::Instance(Rect motion, bool argb) : rect_(motion), argb_(argb)
{
    size_t n = (size_t)std::max(0, motion.x1 - motion.x0) * (size_t)std::max(0, motion.y1 - motion.y0);
    size_t h = (size_t)std::max(0, motion.y1 - motion.y0);
    e_.assign(n, 0);
    ids_.assign(n, 0);
    next_.assign(n, 0);
    age_.assign(n, (uint8_t)kAgeMs);
    held_.assign(n, 0);
    lo_.assign(h, 0);
    hi_.assign(h, 0);
    act_lo_.assign(h, 0);
    act_hi_.assign(h, 0);
    slot_.assign((size_t)kIds, -1);
    cover_.assign(n, 0);
    back_.assign((size_t)kIds, (uint16_t)kFull);
    kind_.assign((size_t)kIds, 0);
    e_prev_.assign(n, 0);
    age_prev_.assign(n, (uint8_t)kAgeMs);
    ox_.assign((size_t)kIds, 0);
    oy_.assign((size_t)kIds, 0);
    has_o_.assign((size_t)kIds, 0);
    tsx_.assign((size_t)kIds, 0);
    tsy_.assign((size_t)kIds, 0);
    tsi_.assign((size_t)kIds, -1);
    tsg_.assign((size_t)kIds, 0);
}

void Instance::begin(int64_t t0, bool reversed)
{
    t0_ = t0;
    reversed_ = reversed;
    back_due_ = reversed;
    if (reversed || start_ready_)
        return;
    ps_ = s1_row_ >= 0 ? 'p' : '-';
    s1_row_ = -1;
    int64_t c0 = clock_us ? clock_us() : 0;
    inited_ = false;
    settled_ = false;
    ready_ = false;
    start_fill_ = false;
    xr_ = -1;
    sig_.clear();
    assert(prep_row_ == 0 || prep_size_ == e_.size());
    size_t a = (size_t)prep_row_ * (size_t)std::max(0, rect_.x1 - rect_.x0);
    std::fill(e_.data() + a, e_.data() + e_.size(), (uint16_t)0);
    std::fill(ids_.data() + a, ids_.data() + ids_.size(), (uint16_t)0);
    std::fill(age_.data() + a, age_.data() + age_.size(), (uint8_t)kAgeMs);
    std::fill(held_.data() + a, held_.data() + held_.size(), (uint16_t)0);
    std::fill(act_lo_.data() + prep_row_, act_lo_.data() + act_lo_.size(), (int16_t)0);
    std::fill(act_hi_.data() + prep_row_, act_hi_.data() + act_hi_.size(), (int16_t)0);
    if (keep_cover_)
        std::fill(cover_.data() + a, cover_.data() + cover_.size(), (uint8_t)0);
    std::fill(has_o_.begin(), has_o_.end(), (uint8_t)0);
    prep_row_ = 0;
    fresh_ = true;
    seg_ok_ = false;
    if (clock_us)
        times_.begin += clock_us() - c0;
}

void Instance::prepare(int rows)
{
    int gw = std::max(0, rect_.x1 - rect_.x0), gh = std::max(0, rect_.y1 - rect_.y0);
    assert(!start_busy());
    if (prep_row_ == 0)
        prep_size_ = e_.size();
    assert(prep_size_ == e_.size());
    int r1 = std::min(gh, prep_row_ + std::max(0, rows));
    size_t a = (size_t)prep_row_ * (size_t)gw, b = (size_t)r1 * (size_t)gw;
    std::fill(e_.data() + a, e_.data() + b, (uint16_t)0);
    std::fill(ids_.data() + a, ids_.data() + b, (uint16_t)0);
    std::fill(age_.data() + a, age_.data() + b, (uint8_t)kAgeMs);
    std::fill(held_.data() + a, held_.data() + b, (uint16_t)0);
    std::fill(act_lo_.data() + prep_row_, act_lo_.data() + r1, (int16_t)0);
    std::fill(act_hi_.data() + prep_row_, act_hi_.data() + r1, (int16_t)0);
    if (keep_cover_)
        std::fill(cover_.data() + a, cover_.data() + b, (uint8_t)0);
    prep_row_ = r1;
}

void Instance::advance(int64_t t0, int64_t now)
{
    if (t0 != t0_)
        t0_ = t0;
    due_ = now;
}

void Instance::skip() { due_ = -1; }

void Instance::compose(const Surface& dst, const Frame& f, const std::vector<Surface>& sources, uint32_t ground,
                       int clip_x, const Frame* start)
{
    if (due_ >= 0)
        ready_ = inited_ && settled_;
    bool advancing = due_ >= 0;
    bool armed = adopt_;
    adopt_ = false;
    bool probe = false;
    if (due_ >= 0 && !inited_ && start && t0_ >= 0 && due_ != t0_) {
        Rect r = rect_.intersect(dst.rect());
        auto fill_x = [&](const Frame& g) {
            int x = g.poses.empty() ? r.x0 : r.x1;
            for (const Pose& q : g.poses)
                x = std::min(x, std::max(r.x0, q.clip.x0));
            return x;
        };
        bool marks = !f.poses.empty() && fill_x(f) <= fill_x(*start);
        int64_t due = due_;
        due_ = t0_;
        bool adopt = armed && marks;
        adopt_drawn_ += armed && !marks;
        armed = false;
        int64_t c0 = clock_us ? clock_us() : 0;
        bool seeded = false;
        if (start_ready_) {
            char k = start_key(dst, *start, sources, ground, clip_x);
            seeded = !k && marks;
            start_ready_ = false;
            if (!seeded)
                begin(t0_, false);
            ps_ = k ? k : seeded ? 'a' : 'm';
        }
        if (seeded) {
            inited_ = true;
            last_ = t0_;
            phase_[0] = phase_[1] = phase_[2] = 0;
        } else {
            pass(dst, *start, sources, ground, clip_x, !marks);
        }
        if (marks) {
            sfill_ = tfill_;
            start_fill_ = true;
        }
        if (clock_us) {
            times_.start += clock_us() - c0;
            times_.starts++;
            times_.start_why = (f.poses.empty() ? 1u << ('e' - 'a') : 0u) |
                               (!f.poses.empty() && fill_x(f) > fill_x(*start) ? 1u << ('x' - 'a') : 0u);
            times_.start_marks += phase_[0];
            times_.start_fills += phase_[1];
            times_.start_build += phase_[2];
        }
        if (marks)
            seed_frames_++;
        probe = probe_ && clock_us && marks && !seeded;
        due_ = due;
        if (adopt && adopt_rule_) {
            bool ok = last_s1_;
            for (const Pose& q : f.poses) {
                const Block& b = f.blocks[(size_t)q.block];
                size_t k = (size_t)(b.id & kIdMask);
                bool kept = has_o_[k] && q.rect.x0 == ox_[k] && q.rect.y0 == oy_[k] &&
                            b.src.x0 == tsx_[k] && b.src.y0 == tsy_[k] && b.source == tsi_[k] &&
                            (size_t)q.block < f.shades.size() && f.shades[(size_t)q.block].sigma == tsg_[k];
                int sx = slot_[k];
                bool on = sx >= 0 && (size_t)sx < tab_.size() && tab_[(size_t)sx].on;
                bool full = (b.id & kRingBit) || (on && tab_[(size_t)sx].t == kFull);
                bool ident = q.rect.x0 == b.src.x0 && q.rect.y0 == b.src.y0;
                if (kept && !owns_.empty() && owns_[k] && (b.source != adopt_src_ || !ident || !full)) {
                    if (ok && adopt_ref_.id < 0)
                        adopt_ref_ = {b.id, b.source, ident, on ? tab_[(size_t)sx].t : -1, last_s1_};
                    ok = false;
                }
            }
            if (!last_s1_ && adopt_ref_.id < 0)
                adopt_ref_.s1 = 0;
            adopt = ok;
            adopt_struct_ += !adopt;
        }
        if (adopt) {
            assert(tw_ == dst.w && th_ == dst.h);
            tdst_ = (const void*)dst.row(0);
            if (adopt_gap_)
                tfill_ = {tend_, tend_, 0, 0};
        }
        if (adopt && verify_) {
            Buffer pa(dst.w, dst.h), pb(dst.w, dst.h);
            Surface sa = pa.surface(), sb = pb.surface();
            for (int y = 0; y < dst.h; y++) {
                std::copy(dst.row(y), dst.row(y) + dst.w, sa.row(y));
                std::copy(dst.row(y), dst.row(y) + dst.w, sb.row(y));
            }
            Instance ta(*this), tb(*this);
            for (Instance* c : {&ta, &tb}) {
                c->verify_ = false;
                c->clock_us = nullptr;
            }
            ta.tdst_ = (const void*)sa.row(0);
            tb.tdst_ = nullptr;
            ta.pass(sa, f, sources, ground, clip_x);
            tb.pass(sb, f, sources, ground, clip_x);
            for (int y = 0; adopt && y < dst.h; y++) {
                auto ar = sa.row(y), br = sb.row(y);
                auto at = std::mismatch(ar, ar + dst.w, br).first;
                adopt = at == ar + dst.w;
                if (!adopt && adopt_fail_x_ < 0) {
                    adopt_fail_x_ = (int)(at - ar);
                    adopt_fail_y_ = y;
                }
            }
            adopt_fails_ += !adopt;
            if (!adopt)
                tdst_ = nullptr;
        }
        adopts_ += adopt;
    }
    adopt_nostart_ += armed;
    int64_t c1 = clock_us ? clock_us() : 0;
    pass(dst, f, sources, ground, clip_x);
    if (clock_us) {
        times_.pass += clock_us() - c1;
        times_.passes++;
        times_.trust_why = pass_why_;
    }
    if (advancing)
        invalidated_ = false;
    if (probe) {
        Instance w(*this);
        w.clock_us = nullptr;
        w.verify_ = false;
        w.probe_ = false;
        w.begin(t0_, reversed_);
        w.due_ = t0_;
        int64_t q0 = clock_us();
        w.pass(dst, *start, sources, ground, clip_x, false);
        times_.start_warm += clock_us() - q0;
        times_.start_warms++;
    }
}

void Instance::pass(const Surface& dst, const Frame& f, const std::vector<Surface>& sources, uint32_t ground,
                    int clip_x, bool write)
{
    last_s1_ = false;
    bool ext = start_fill_;
    start_fill_ = false;
    if (!s1_defer_ && start_busy()) {
        bool rev = reversed_;
        start_ready_ = false;
        s1_row_ = -1;
        begin(t0_, false);
        reversed_ = rev;
        back_due_ = rev;
        ps_ = 'n';
    }
    prep_row_ = 0;
    bool fresh = fresh_;
    fresh_ = false;
    if (clock_us)
        phase_[0] = phase_[1] = phase_[2] = 0;
    Rect r = rect_.intersect(dst.rect());
    int gw = rect_.x1 - rect_.x0, gh = rect_.y1 - rect_.y0;
    if (r.empty() || gw <= 0 || gh <= 0)
        return;
    int64_t ph = clock_us ? clock_us() : 0;
    int w = std::min(clip_x, r.x1);
    bool update = due_ >= 0;
    std::fill(next_.begin(), next_.end(), (uint16_t)0);
    std::fill(lo_.begin(), lo_.end(), (int16_t)gw);
    std::fill(hi_.begin(), hi_.end(), (int16_t)0);
    if (keep_cover_)
        std::fill(cover_.begin(), cover_.end(), (uint8_t)0);
    int fill0 = f.poses.empty() ? r.x0 : r.x1;
    for (const Pose& q : f.poses)
        fill0 = std::min(fill0, std::max(r.x0, q.clip.x0));
    Rect gr = f.ground.intersect(r);
    assert(gr.empty() || gr.x1 >= fill0);
    Fill own{fill0, fill0, 0, 0};
    if (!gr.empty())
        own = {fill0, std::min(fill0, gr.x0), gr.y0, gr.y1};
    auto fill_at = [&](int y) { return ext ? std::min(own.at(y), sfill_.at(y)) : own.at(y); };
    for (int y = gr.y0; keep_cover_ && !gr.empty() && gr.x0 < w && y < gr.y1; y++) {
        uint8_t* cv = cover_.data() + (size_t)(y - rect_.y0) * gw + (size_t)(gr.x0 - rect_.x0);
        std::fill(cv, cv + (std::min(gr.x1, w) - gr.x0), (uint8_t)1);
    }
    bool top = seg_;
    bool s1 = fresh && top && !write && update && !inited_;
    last_s1_ = s1;
    if (s1) {
        if (owns_.empty())
            owns_.assign((size_t)kIds, 0);
        for (uint16_t k : owned_)
            owns_[k] = 0;
        owned_.clear();
    }
    if (s1) {
        s1_.resize((size_t)gh);
        for (std::vector<S1Span>& v : s1_)
            v.clear();
    }
    bool trusted = top && update && inited_ && seg_ok_ && trust_ && tdst_ == (const void*)dst.row(0) &&
                   tw_ == dst.w && th_ == dst.h && tground_ == ground && tsrc_.size() == sources.size();
    for (size_t k = 0; trusted && k < sources.size(); k++)
        trusted = tsrc_[k] == (const void*)sources[k].row(0);
    if (trusted)
        trust_frames_++;
    if (clock_us && !trusted) {
        auto bit = [](char c) { return 1u << (c - 'a'); };
        bool srcs = tsrc_.size() == sources.size();
        for (size_t k = 0; srcs && k < sources.size(); k++)
            srcs = tsrc_[k] == (const void*)sources[k].row(0);
        bool dst_same = tdst_ == (const void*)dst.row(0) && tw_ == dst.w && th_ == dst.h;
        pass_why_ = (top ? 0 : bit('s')) | (update ? 0 : bit('u')) | (inited_ ? 0 : bit('i')) |
                    (seg_ok_ ? 0 : bit('k')) | (trust_ ? 0 : bit('o')) |
                    (dst_same ? 0 : tdst_ == nullptr && invalidated_ ? bit('v') : bit('d')) |
                    (tground_ == ground ? 0 : bit('g')) | (srcs ? 0 : bit('r'));
    } else if (clock_us) {
        pass_why_ = 0;
    }
    auto keeps = [&](const Pose& q, const Block& b) {
        size_t k = (size_t)(b.id & kIdMask);
        return trusted && has_o_[k] && q.rect.x0 == ox_[k] && q.rect.y0 == oy_[k] &&
               b.src.x0 == tsx_[k] && b.src.y0 == tsy_[k] && b.source == tsi_[k] &&
               (size_t)q.block < f.shades.size() && f.shades[(size_t)q.block].sigma == tsg_[k];
    };
    for (int y = r.y0; y < r.y1 && !top && write; y++) {
        int x0 = fill_at(y);
        std::fill(dst.row(y) + x0, dst.row(y) + std::max(x0, w), ground);
    }
    for (const Block& b : f.blocks)
        slot_[(size_t)(b.id & kIdMask)] = -1;
    for (size_t i = 0; i < f.blocks.size(); i++) {
        assert(slot_[(size_t)(f.blocks[i].id & kIdMask)] < 0);
        slot_[(size_t)(f.blocks[i].id & kIdMask)] = (int)i;
        kind_[(size_t)(f.blocks[i].id & kIdMask)] = f.blocks[i].tile ? 1 : 2;
    }
    order_.resize(f.poses.size());
    std::iota(order_.begin(), order_.end(), (size_t)0);
    std::sort(order_.begin(), order_.end(), [&](size_t a, size_t b) {
        int za = f.blocks[(size_t)f.poses[a].block].z, zb = f.blocks[(size_t)f.poses[b].block].z;
        return za != zb ? za < zb : a < b;
    });
    if (top) {
        covs_.resize((size_t)gh);
        for (auto& cv : covs_)
            cv.clear();
        for (auto it = order_.rbegin(); it != order_.rend(); ++it) {
            const Pose& q = f.poses[*it];
            const Block& b = f.blocks[(size_t)q.block];
            if ((size_t)b.source >= sources.size())
                continue;
            const Surface& src = sources[(size_t)b.source];
            Rect out = pose_span(q, b, q.clip.intersect(r).intersect({r.x0, r.y0, w, r.y1}), src.w);
            int sx0 = q.rect.x0 - b.src.x0;
            bool keep = keeps(q, b);
            for (int y = out.y0; y < out.y1 && out.x0 < out.x1; y++) {
                int sy = b.src.y0 + (y - q.rect.y0);
                if (sy < 0 || sy >= src.h)
                    continue;
                size_t ry = (size_t)(y - rect_.y0);
                std::vector<std::pair<int, int>>& cv = covs_[ry];
                auto draw = [&](int u, int v) {
                    if (u >= v)
                        return;
                    if (!keep) {
                        if (write)
                            copy_px(dst.row(y) + u, src.row(sy) + (u - sx0), v - u);
                    } else {
                        segs_.walk((int)ry, u, v, rect_.x0, [&](int p0, int p1, const Run& g) {
                            if (g.p.id != b.id && !keep_any_)
                                copy_px(dst.row(y) + p0, src.row(sy) + (p0 - sx0), p1 - p0);
                        });
                    }
                    size_t at = ry * gw + (size_t)(u - rect_.x0);
                    std::fill_n(next_.begin() + (ptrdiff_t)at, v - u, b.id);
                    if (keep_cover_)
                        std::fill_n(cover_.begin() + (ptrdiff_t)at, v - u, (uint8_t)1);
                    lo_[ry] = (int16_t)std::min<int>(lo_[ry], u - rect_.x0);
                    hi_[ry] = (int16_t)std::max<int>(hi_[ry], v - rect_.x0);
                    if (s1)
                        s1_[ry].push_back({u, v, b.id});
                };
                int x = out.x0;
                size_t i = 0;
                for (; i < cv.size() && x < out.x1; i++) {
                    if (cv[i].second <= x)
                        continue;
                    if (cv[i].first >= out.x1)
                        break;
                    draw(x, std::min(cv[i].first, out.x1));
                    x = std::max(x, cv[i].second);
                }
                draw(x, out.x1);
                size_t a = 0;
                while (a < cv.size() && cv[a].second < out.x0)
                    a++;
                size_t e = a;
                int c0 = out.x0, c1 = out.x1;
                while (e < cv.size() && cv[e].first <= out.x1) {
                    c0 = std::min(c0, cv[e].first);
                    c1 = std::max(c1, cv[e].second);
                    e++;
                }
                cv.erase(cv.begin() + (ptrdiff_t)a, cv.begin() + (ptrdiff_t)e);
                cv.insert(cv.begin() + (ptrdiff_t)a, {c0, c1});
            }
        }
        auto gap = [&](int y, int u, int v) {
            if (u >= v || !write)
                return;
            if (!trusted) {
                std::fill(dst.row(y) + u, dst.row(y) + v, ground);
                return;
            }
            segs_.walk(y - rect_.y0, u, v, rect_.x0, [&](int p0, int p1, const Run& g) {
                int k0 = std::max(p0, tfill_.at(y)), k1 = std::min(p1, tend_);
                if (g.p.id || k0 >= k1) {
                    std::fill(dst.row(y) + p0, dst.row(y) + p1, ground);
                    return;
                }
                std::fill(dst.row(y) + p0, dst.row(y) + k0, ground);
                std::fill(dst.row(y) + k1, dst.row(y) + p1, ground);
            });
        };
        for (int y = r.y0; y < r.y1; y++) {
            const std::vector<std::pair<int, int>>& cv = covs_[(size_t)(y - rect_.y0)];
            int x = fill_at(y);
            for (const auto& c : cv) {
                if (c.second <= x)
                    continue;
                if (c.first >= w)
                    break;
                gap(y, x, std::min(c.first, w));
                x = std::max(x, c.second);
            }
            gap(y, x, w);
        }
    }
    for (size_t k : order_) {
        if (top)
            break;
        const Pose& q = f.poses[k];
        const Block& b = f.blocks[(size_t)q.block];
        if ((size_t)b.source >= sources.size())
            continue;
        const Surface& src = sources[(size_t)b.source];
        Rect lim = q.clip.intersect(r).intersect({r.x0, r.y0, w, r.y1});
        Rect out = pose_span(q, b, lim, src.w);
        int sx0 = q.rect.x0 - b.src.x0, xa = out.x0, xb = out.x1;
        for (int y = out.y0; y < out.y1 && xa < xb; y++) {
            int sy = b.src.y0 + (y - q.rect.y0);
            if (sy < 0 || sy >= src.h)
                continue;
            if (write)
                copy_px(dst.row(y) + xa, src.row(sy) + (xa - sx0), xb - xa);
            size_t ry = (size_t)(y - rect_.y0);
            size_t at = ry * gw + (size_t)(xa - rect_.x0);
            std::fill_n(next_.begin() + (ptrdiff_t)at, xb - xa, b.id);
            if (keep_cover_)
                std::fill_n(cover_.begin() + (ptrdiff_t)at, xb - xa, (uint8_t)1);
            lo_[ry] = (int16_t)std::min<int>(lo_[ry], xa - rect_.x0);
            hi_[ry] = (int16_t)std::max<int>(hi_[ry], xb - rect_.x0);
        }
    }
    int xr = std::min(r.x1, w);
    bool first = update && !inited_;
    bool aging = update && inited_;
    bool edge = aging && !reversed_ && xr_ >= 0 && xr > xr_;
    int a0 = xr_;
    sig_now_.assign(f.blocks.size(), 0);
    for (const Pose& q : f.poses)
        sig_now_[(size_t)q.block] = sig_now_[(size_t)q.block] * 1000003 + sig_of(q);
    still_.assign(f.blocks.size(), 0);
    if (edge)
        for (size_t b = 0; b < f.blocks.size() && b < sig_.size(); b++)
            still_[b] = sig_[b] != 0 && sig_[b] == sig_now_[b];
    bdx_.assign(f.blocks.size(), 0);
    bdy_.assign(f.blocks.size(), 0);
    posed_.assign(f.blocks.size(), 0);
    pose_at_.assign(f.blocks.size(), -1);
    for (const Pose& q : f.poses) {
        size_t k = (size_t)(f.blocks[(size_t)q.block].id & kIdMask);
        assert(!posed_[(size_t)q.block]);
        posed_[(size_t)q.block] = 1;
        pose_at_[(size_t)q.block] = (int)(&q - f.poses.data());
        if (has_o_[k]) {
            bdx_[(size_t)q.block] = q.rect.x0 - ox_[k];
            bdy_[(size_t)q.block] = q.rect.y0 - oy_[k];
        }
    }
    bool segs = aging && seg_ && seg_ok_;
    if (aging && !segs)
        for (int ry = 0; ry < gh; ry++) {
            int c0 = std::min<int>(lo_[(size_t)ry], act_lo_[(size_t)ry]);
            int c1 = std::max<int>(hi_[(size_t)ry], act_hi_[(size_t)ry]);
            if (c0 >= c1)
                continue;
            size_t at = (size_t)ry * gw;
            std::copy(e_.begin() + (ptrdiff_t)(at + c0), e_.begin() + (ptrdiff_t)(at + c1),
                      e_prev_.begin() + (ptrdiff_t)(at + c0));
            std::copy(age_.begin() + (ptrdiff_t)(at + c0), age_.begin() + (ptrdiff_t)(at + c1),
                      age_prev_.begin() + (ptrdiff_t)(at + c0));
        }
    if (aging && back_due_) {
        auto scan = [&]() {
            std::vector<uint16_t> v((size_t)kIds, (uint16_t)kFull);
            for (size_t i = 0; i < ids_.size(); i++)
                if (ids_[i] && !(ids_[i] & kRingBit))
                    v[ids_[i] & kIdMask] = std::min(v[ids_[i] & kIdMask], e_[i]);
            return v;
        };
        bool lists = seg_ && seg_ok_;
        if (lists) {
            std::fill(back_.begin(), back_.end(), (uint16_t)kFull);
            for (const Run& g : segs_.runs)
                if (g.p.id && !(g.p.id & kRingBit))
                    back_[g.p.id & kIdMask] = std::min(back_[g.p.id & kIdMask], g.p.e);
        } else {
            back_ = scan();
        }
        if (verify_ && lists) {
            verify_scans_++;
            verify_fails_ += back_ != scan();
        }
        back_due_ = false;
    }
    int64_t dt3 = aging && last_ >= 0 ? 3 * (due_ - last_) : 0;
    int dim_inc = step_by(kDimStep, dt3), ring_inc = step_by(kRingStep, dt3);
    int64_t dt = dt3 / 3;
    bool settled = true;
    tab_.resize(f.blocks.size());
    lut_sigma_.clear();
    lut_.clear();
    for (size_t b = 0; b < f.blocks.size(); b++) {
        bool on = b < f.shades.size();
        Shade h = on ? f.shades[b] : Shade{};
        uint8_t kind = kind_[(size_t)(f.blocks[b].id & kIdMask)];
        size_t k = 0;
        while (k < lut_sigma_.size() && lut_sigma_[k] != h.sigma)
            k++;
        if (k == lut_sigma_.size()) {
            assert(h.sigma > 0);
            lut_sigma_.push_back(h.sigma);
            for (int a = 0; a <= 256; a++)
                lut_.push_back((uint16_t)std::min(256, (a * 256 + h.sigma / 2) / h.sigma));
        }
        int pi = pose_at_[b];
        bool keep = pi >= 0 && (size_t)f.blocks[b].source < sources.size() && keeps(f.poses[(size_t)pi], f.blocks[b]);
        tab_[b] = {h.t, h.end, h.step, h.sigma, bdx_[b], bdy_[b], (int)k * 257, kind, still_[b], h.dep, on, pi, keep};
    }
    const uint16_t* lut = lut_.data();
    const int* sl = slot_.data();
    const Slot* tb = tab_.data();
    size_t nt = tab_.size();
    const uint8_t* kd = kind_.data();
    const uint16_t* bk = back_.data();
    const uint16_t* pid = ids_.data();
    const uint16_t* pe = e_prev_.data();
    const uint8_t* pa = age_prev_.data();
    auto slot_of = [&](uint16_t id) -> const Slot* {
        int s = sl[id & kIdMask];
        return s >= 0 && (size_t)s < nt && tb[s].on ? tb + s : nullptr;
    };
    const int mx0 = rect_.x0, mx1 = rect_.x1, my0 = rect_.y0, my1 = rect_.y1;
    const bool rev = reversed_, init = inited_, argb = argb_;
    int nbx = a0 - 1 - mx0;
    bool fired = false;
    auto emit = [&](int u, int v, uint16_t id, uint16_t e, uint8_t age, uint16_t held) {
        nsegs_.emit(u, v, {id, e, held, age});
    };
    auto keep = [&](int r, int u, int v) {
        segs_.walk(r, u, v, 0, [&](int p0, int p1, const Run& g) { emit(p0, p1, 0, g.p.e, g.p.age, g.p.held); });
    };
    auto out = [&](uint32_t* d, uint16_t* ep, uint8_t* ap, int u, int v, uint16_t id, int e, bool lit, const Slot* h,
                   int end, const uint32_t* kept, int ksx, int pe, int pa) {
        uint8_t age = (uint8_t)(lit ? 0x80 : 0);
        emit(u, v, id, (uint16_t)e, age, 0);
        if (kept && e == pe && age == pa) {
            for (int k = u; settled && e != end && k < v; k++)
                settled = kept[mx0 + k - ksx] == ground;
            return;
        }
        if (kept)
            copy_px(d + mx0 + u, kept + (mx0 + u - ksx), v - u);
        std::fill(ep + u, ep + v, (uint16_t)e);
        std::fill(ap + u, ap + v, age);
        for (int k = u; settled && e != end && k < v; k++)
            settled = d[mx0 + k] == ground;
        const uint16_t* lt = lut + h->lut;
        int a = (int)(((int64_t)e * 256 + kFull / 2) / kFull);
        a = (unsigned)a <= 256 ? lt[a] : std::min(256, (a * 256 + h->sigma / 2) / h->sigma);
        if (a >= 256)
            return;
        if (argb)
            for (int k = u; k < v; k++)
                d[mx0 + k] = shade_argb(d[mx0 + k], ground, e, h->sigma, lt);
        else if (a <= 0)
            std::fill(d + mx0 + u, d + mx0 + v, ground);
        else
            blend_row(d + mx0 + u, v - u, ground, a);
    };
    auto seg_pass = [&](int ry, int y, int x0, int x1, const uint16_t* nx, uint16_t* ep, uint8_t* ap, uint16_t* hp,
                        uint32_t* d, int& act0, int& act1, uint16_t nb, int nb_t) {
        for (int a = x0; a < x1;) {
            uint16_t id = mx0 + a < w ? nx[a] : 0;
            int b = a + 1;
            while (b < x1 && (mx0 + b < w ? nx[b] : 0) == id)
                b++;
            const Slot* h = id ? slot_of(id) : nullptr;
            if (!h) {
                segs_.walk(ry, a, b, 0, [&](int u, int v, const Run& run) {
                    const Seg& g = run.p;
                    int age = (int)std::min<int64_t>(kAgeMs, (g.age & 0x7F) + dt);
                    uint8_t aged = (uint8_t)(age | ((g.age & 0x80) && age < kAgeMs ? 0x80 : 0));
                    uint16_t held = id ? 0 : g.id ? g.id : g.held;
                    if (age >= kAgeMs)
                        std::fill(ep + u, ep + v, (uint16_t)0);
                    std::fill(ap + u, ap + v, aged);
                    if (!id)
                        std::fill(hp + u, hp + v, held);
                    emit(u, v, id, age >= kAgeMs ? 0 : g.e, aged, held);
                    if (age < kAgeMs) {
                        act0 = std::min(act0, u);
                        act1 = v;
                    }
                });
                a = b;
                continue;
            }
            act0 = std::min(act0, a);
            act1 = b;
            bool ring = id & kRingBit, hold = rev && h->dep;
            int t = ring && !hold ? kFull : h->t, end = ring && !hold ? kFull : h->end;
            const uint32_t* kept = nullptr;
            int ksx = 0;
            if (h->keep) {
                const Pose& q = f.poses[(size_t)h->pose];
                const Block& kb = f.blocks[(size_t)q.block];
                int sy = kb.src.y0 + (y - q.rect.y0);
                const Surface& src = sources[(size_t)kb.source];
                if (sy >= 0 && sy < src.h) {
                    kept = src.row(sy);
                    ksx = q.rect.x0 - kb.src.x0;
                }
            }
            auto piece = [&](int u, int v, RuleIn in, int age, const uint32_t* skip, int pe, int pa) {
                bool held = in.stale && age < kAgeMs, stale = in.stale || age >= kAgeMs;
                in.stale = stale;
                const int cuts[] = {a0 - mx0, xr - mx0};
                cut_at(u, v, cuts, [&](int m, int n) {
                    int x = mx0 + m;
                    in.edge = edge && stale && x >= a0 && x < xr && h->still && (id != nb || ep[nbx] + 256 > nb_t);
                    fired = fired || (held && (ring ? !hold : !h->dep && !in.edge));
                    bool lit;
                    int e = rule_e(in, rev, dt3, dim_inc, ring_inc, lit);
                    out(d, ep, ap, m, n, id, e, lit, h, end, skip, ksx, pe, pa);
                });
            };
            auto screen = [&](int u, int v) {
                segs_.walk(ry, u, v, 0, [&](int p0, int p1, const Run& run) {
                    const Seg& g = run.p;
                    uint16_t old = g.id;
                    RuleIn in{ring, hold, h->dep, false, false, (g.age & 0x80) != 0, false,
                              old && kd[old & kIdMask] == 1 && h->kind == 1, t, g.e, h->step, g.e, bk[id & kIdMask],
                              g.id, old};
                    in.stale = !old && h->kind != 1 && (g.held & kIdMask) != (id & kIdMask);
                    piece(p0, p1, in, g.age & 0x7F, nullptr, 0, 0);
                });
            };
            int sr = y - h->bdy - my0, lo_in = h->bdx, hi_in = gw + h->bdx;
            bool rows_in = sr >= 0 && sr < gh;
            for (int u = a; u < b;) {
                bool inside = rows_in && u >= lo_in && u < hi_in;
                int v = inside ? std::min(b, hi_in) : rows_in && u < lo_in ? std::min(b, lo_in) : b;
                if (!inside) {
                    screen(u, v);
                    u = v;
                    continue;
                }
                segs_.walk(sr, u, v, h->bdx, [&](int p, int q, const Run& run) {
                    const Seg& g = run.p;
                    if (!(g.id && (g.id & kIdMask) == (id & kIdMask))) {
                        screen(p, q);
                        return;
                    }
                    RuleIn in{ring, hold, h->dep, true, false, (g.age & 0x80) != 0, false, false, t, g.e, h->step, 0,
                              bk[id & kIdMask], g.id, 0};
                    piece(p, q, in, g.age & 0x7F, kept && (g.id == id || keep_any_) ? kept : nullptr, g.e, g.age);
                });
                u = v;
            }
            a = b;
        }
    };
    if (clock_us) {
        int64_t c = clock_us();
        phase_[0] = c - ph;
        ph = c;
    }
    if (segs) {
        seg_frames_++;
        nsegs_.begin(gh);
    }
    for (int ry = 0; ry < gh && !s1; ry++) {
        int y = my0 + ry;
        if (segs)
            nsegs_.row(ry);
        if (y < r.y0 || y >= r.y1) {
            if (segs)
                keep(ry, 0, gw);
            continue;
        }
        int x0 = lo_[(size_t)ry], x1 = hi_[(size_t)ry];
        if (aging) {
            x0 = std::min<int>(x0, act_lo_[(size_t)ry]);
            x1 = std::max<int>(x1, act_hi_[(size_t)ry]);
        }
        assert(x0 >= x1 || (x0 >= 0 && x1 <= gw));
        size_t row = (size_t)ry * gw;
        const uint16_t* nx = next_.data() + row;
        const uint16_t* under = pid + row;
        uint16_t* ep = e_.data() + row;
        uint8_t* ap = age_.data() + row;
        uint16_t* hp = held_.data() + row;
        int act0 = gw, act1 = 0;
        uint32_t* d = dst.row(y);
        uint16_t nb = 0;
        int nb_t = 0;
        if (edge && a0 - 1 >= mx0 && a0 - 1 < w) {
            nb = nx[nbx];
            if (nb & kRingBit)
                nb = 0;
            const Slot* hn = nb ? slot_of(nb) : nullptr;
            nb_t = hn ? hn->t : 0;
            if (!hn)
                nb = 0;
        }
        for (int xi = x0; first && xi < x1 && mx0 + xi < w;) {
            uint16_t id = nx[xi];
            int xe = xi + 1;
            while (xe < x1 && mx0 + xe < w && nx[xe] == id)
                xe++;
            const Slot* h = id ? slot_of(id) : nullptr;
            if (h) {
                act0 = std::min(act0, xi);
                act1 = xe;
                bool ring = id & kRingBit;
                bool hold = rev && h->dep;
                int t = ring && !hold ? kFull : h->t;
                std::fill(ep + xi, ep + xe, (uint16_t)t);
                std::fill(ap + xi, ap + xe, (uint8_t)0);
                const uint16_t* lt = lut + h->lut;
                int a = (int)(((int64_t)t * 256 + kFull / 2) / kFull);
                a = (unsigned)a <= 256 ? lt[a] : std::min(256, (a * 256 + h->sigma / 2) / h->sigma);
                bool rest = t == (ring && !hold ? kFull : h->end);
                for (int k = xi; !rest && settled && k < xe; k++)
                    settled = d[mx0 + k] == ground;
                if (write && a < 256) {
                    if (argb)
                        for (int k = xi; k < xe; k++)
                            d[mx0 + k] = shade_argb(d[mx0 + k], ground, t, h->sigma, lt);
                    else if (a <= 0)
                        std::fill(d + mx0 + xi, d + mx0 + xe, ground);
                    else
                        blend_row(d + mx0 + xi, xe - xi, ground, a);
                }
            }
            xi = xe;
        }
        if (segs && x0 < x1) {
            keep(ry, 0, x0);
            seg_pass(ry, y, x0, x1, nx, ep, ap, hp, d, act0, act1, nb, nb_t);
            keep(ry, x1, gw);
        } else if (segs) {
            keep(ry, 0, gw);
        }
        for (int xi = x0; !first && !segs && xi < x1; xi++) {
            int x = mx0 + xi;
            uint16_t id = x < w ? nx[xi] : 0;
            const Slot* h = id ? slot_of(id) : nullptr;
            if (!h) {
                if (aging) {
                    int age = (int)std::min<int64_t>(kAgeMs, (ap[xi] & 0x7F) + dt);
                    if (age >= kAgeMs)
                        ep[xi] = 0;
                    ap[xi] = (uint8_t)(age | ((ap[xi] & 0x80) && age < kAgeMs ? 0x80 : 0));
                    if (!id && under[xi])
                        hp[xi] = under[xi];
                    if (age < kAgeMs) {
                        act0 = std::min(act0, xi);
                        act1 = xi + 1;
                    }
                }
                continue;
            }
            act0 = std::min(act0, xi);
            act1 = xi + 1;
            bool ring = id & kRingBit;
            bool hold = rev && h->dep;
            int t = ring && !hold ? kFull : h->t;
            int e = t;
            if (aging) {
                size_t i = row + (size_t)xi;
                int px = x - h->bdx, py = y - h->bdy;
                bool inside = px >= mx0 && px < mx1 && py >= my0 && py < my1;
                size_t ip = inside ? (size_t)(py - my0) * gw + (size_t)(px - mx0) : i;
                bool carried = inside && pid[ip] && (pid[ip] & kIdMask) == (id & kIdMask);
                size_t j = carried ? ip : i;
                int age = pa[j] & 0x7F;
                uint16_t old = under[xi];
                bool fresh = !carried && !old && h->kind != 1 && (hp[xi] & kIdMask) != (id & kIdMask);
                bool stale = age >= kAgeMs || fresh, lit = false;
                bool edged = edge && stale && x >= a0 && x < xr && h->still && (id != nb || ep[nbx] + 256 > nb_t);
                fired = fired || (fresh && age < kAgeMs && (ring ? !hold : !h->dep && !edged));
                RuleIn in{ring,
                          hold,
                          h->dep,
                          carried,
                          stale,
                          (pa[j] & 0x80) != 0,
                          edged,
                          old && kd[old & kIdMask] == 1 && h->kind == 1,
                          t,
                          pe[j],
                          h->step,
                          pe[i],
                          bk[id & kIdMask],
                          pid[j],
                          old};
                e = rule_e(in, rev, dt3, dim_inc, ring_inc, lit);
                ep[xi] = (uint16_t)e;
                ap[xi] = lit ? 0x80 : 0;
            } else if (init) {
                e = ep[xi];
            }
            if (d[x] != ground && e != (ring && !hold ? kFull : h->end))
                settled = false;
            const uint16_t* lt = lut + h->lut;
            d[x] = argb ? shade_argb(d[x], ground, e, h->sigma, lt) : shade(d[x], ground, e, h->sigma, lt);
        }
        if (update) {
            act_lo_[(size_t)ry] = (int16_t)act0;
            act_hi_[(size_t)ry] = (int16_t)act1;
        }
    }
    if (clock_us)
        phase_[1] = clock_us() - ph;
    if (!update) {
        tdst_ = nullptr;
        return;
    }
    if (fired)
        held_frames_++;
    ids_.swap(next_);
    for (const Pose& q : f.poses) {
        size_t k = (size_t)(f.blocks[(size_t)q.block].id & kIdMask);
        has_o_[k] = true;
        ox_[k] = q.rect.x0;
        oy_[k] = q.rect.y0;
        const Block& b = f.blocks[(size_t)q.block];
        tsx_[k] = b.src.x0;
        tsy_[k] = b.src.y0;
        tsi_[k] = b.source;
        tsg_[k] = (size_t)q.block < f.shades.size() ? f.shades[(size_t)q.block].sigma : 0;
    }
    tdst_ = write ? (const void*)dst.row(0) : nullptr;
    tw_ = dst.w;
    th_ = dst.h;
    tfill_ = own;
    tend_ = w;
    tground_ = ground;
    tsrc_.resize(sources.size());
    for (size_t k = 0; k < sources.size(); k++)
        tsrc_[k] = sources[k].row(0);
    sig_.swap(sig_now_);
    if (segs) {
        nsegs_.end(gh);
        segs_.swap(nsegs_);
    } else if (s1) {
        int64_t c = clock_us ? clock_us() : 0;
        segs_.begin(gh);
        s1_gh_ = gh;
        s1_gw_ = gw;
        s1_mx0_ = mx0;
        s1_my0_ = my0;
        s1_y0_ = r.y0;
        s1_y1_ = r.y1;
        if (s1_defer_) {
            s1_row_ = 0;
        } else {
            for (int ry = 0; ry < gh; ry++)
                s1_emit_row(ry);
            s1_finish();
        }
        if (clock_us) {
            phase_[2] = clock_us() - c;
            times_.s1_starts++;
        }
    } else if (seg_) {
        int64_t c = clock_us ? clock_us() : 0;
        seg_build();
        if (clock_us)
            phase_[2] = clock_us() - c;
    }
    xr_ = xr;
    last_ = due_;
    due_ = -1;
    inited_ = true;
    settled_ = settled;
}

}
}
