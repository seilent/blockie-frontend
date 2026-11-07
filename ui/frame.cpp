#include "frame.h"

#include <algorithm>
#include <cmath>
#include <type_traits>

#include "../core/curves.h"
#include "../gfx/compose.h"

namespace fd {

namespace {

bool same_rect(Rect a, Rect b) { return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1; }

Rect shift(Rect r, fx dx, fx dy)
{
    int x = fx_round(dx), y = fx_round(dy);
    return {r.x0 + x, r.y0 + y, r.x1 + x, r.y1 + y};
}

struct Key {
    uint32_t p[kFrameDepth + 1];
    int n = 0;

    bool operator==(const Key& o) const { return n == o.n && std::equal(p, p + n, o.p); }
};

struct Entry {
    int op = 0;
    int groups[kFrameDepth];
    int depth = 0;
    Key key;
    Rect b;
};

struct Walk {
    Entry leaves[Frame::kOps];
    Entry groups[Frame::kOps];
    int nl = 0, ng = 0;
};

uint32_t tag(const Op& o) { return (uint32_t)o.kind << 16 | o.id; }

Rect sampled(const Op& o, Rect r) { return xform_bounds(o.xf, o.sample == Sample::Linear ? r.grow(1) : r); }

Rect geom(const Op& o) { return (o.kind == OpKind::Quad ? sampled(o, o.src) : o.src).intersect(o.clip); }

Rect chain(const Frame& f, Rect b, const int* g, int depth)
{
    for (int k = depth - 1; k >= 0 && !b.empty(); k--) {
        const Op& go = f.ops[g[k]];
        b = sampled(go, b).intersect(go.clip);
    }
    return b;
}

const Entry* find(const Entry* v, int n, const Key& k)
{
    for (int i = 0; i < n; i++)
        if (v[i].key == k)
            return &v[i];
    return nullptr;
}

bool walk(const Frame& f, Walk& w)
{
    int g[kFrameDepth];
    int open[kFrameDepth];
    int depth = 0;
    for (int i = 0; i < f.n; i++) {
        const Op& o = f.ops[i];
        if (o.kind == OpKind::End) {
            if (depth == 0)
                return false;
            depth--;
            continue;
        }
        Entry e;
        e.op = i;
        e.depth = depth;
        std::copy(g, g + depth, e.groups);
        for (int k = 0; k < depth; k++)
            e.key.p[k] = tag(f.ops[g[k]]);
        e.key.p[depth] = tag(o);
        e.key.n = depth + 1;
        if (find(w.leaves, w.nl, e.key) || find(w.groups, w.ng, e.key))
            return false;
        if (o.kind == OpKind::Group) {
            if (!o.bounds.empty())
                e.b = o.bounds;
            else
                e.b = chain(f, o.src.empty() ? Rect{} : sampled(o, o.src).intersect(o.clip), g, depth);
            if (depth >= kFrameDepth)
                return false;
            open[depth] = w.ng;
            g[depth++] = i;
            w.groups[w.ng++] = e;
            continue;
        }
        e.b = o.bounds.empty() ? chain(f, geom(o), g, depth) : o.bounds;
        for (int k = 0; k < depth; k++) {
            Entry& ge = w.groups[open[k]];
            const Op& go = f.ops[ge.op];
            if (go.src.empty() && go.bounds.empty())
                ge.b = ge.b.unite(e.b);
        }
        w.leaves[w.nl++] = e;
    }
    return depth == 0;
}

bool chain_same(const Frame& a, const Entry& ea, const Frame& b, const Entry& eb)
{
    if (ea.depth != eb.depth)
        return false;
    for (int k = 0; k < ea.depth; k++)
        if (!a.ops[ea.groups[k]].same(b.ops[eb.groups[k]]))
            return false;
    return true;
}

struct Tgt {
    Surface s;
    uint8_t* m = nullptr;
    Rect clip;
};

Pix key_blend(Pix d, Pix v, int m, Pix k, int a)
{
    if constexpr (std::is_same_v<LiveF, R565>)
        return over_ground5(d, v, m, k, alpha5(a));
    else
        return over_ground256(d, v, m, k, a);
}

constexpr double kEdge = 1e-9;

void quad_px(const Op& o, const LayerRef& l, Tgt& t, Rect clip)
{
    Rect have{l.ox, l.oy, l.ox + l.s.w, l.oy + l.s.h};
    Rect src = o.src.intersect(have);
    if (src.empty() || o.a <= 0)
        return;
    Rect b = sampled(o, src).intersect(clip).intersect(t.clip).intersect(t.s.rect());
    if (b.empty())
        return;
    double m[9];
    o.xf.inverse(m);
    Pix key = LiveF::pack(l.key);
    uint32_t kc = LiveF::unpack(key);
    bool masked = l.fmt == LayerFmt::Masked && l.mask;
    auto cover = [&](int i, int j) { return masked ? (int)l.mask[l.s.index(i - l.ox, j - l.oy)] : 255; };
    auto texel = [&](int i, int j, uint32_t& c) -> int {
        if (i < src.x0 || i >= src.x1 || j < src.y0 || j >= src.y1) {
            c = kc;
            return 0;
        }
        c = LiveF::unpack(l.s.at(i - l.ox, j - l.oy));
        return cover(i, j);
    };
    for (int y = b.y0; y < b.y1; y++)
        for (int x = b.x0; x < b.x1; x++) {
            double X = x + 0.5, Y = y + 0.5, w = m[6] * X + m[7] * Y + m[8];
            if (!(w > 0))
                continue;
            double u = (m[0] * X + m[1] * Y + m[2]) / w, v = (m[3] * X + m[4] * Y + m[5]) / w;
            if (!(u > src.x0 - 2 && u < src.x1 + 2 && v > src.y0 - 2 && v < src.y1 + 2))
                continue;
            Pix px{};
            int cov = 0;
            if (o.sample == Sample::Nearest) {
                int i = (int)std::floor(u + kEdge), j = (int)std::floor(v + kEdge);
                if (i < src.x0 || i >= src.x1 || j < src.y0 || j >= src.y1)
                    continue;
                px = l.s.at(i - l.ox, j - l.oy);
                cov = cover(i, j);
            } else {
                u -= 0.5;
                v -= 0.5;
                int i = (int)std::floor(u), j = (int)std::floor(v);
                double fu = u - i, fv = v - j, acc[3] = {0, 0, 0}, ac = 0;
                const double wt[4] = {(1 - fu) * (1 - fv), fu * (1 - fv), (1 - fu) * fv, fu * fv};
                for (int k = 0; k < 4; k++) {
                    uint32_t c = 0;
                    int cv = texel(i + (k & 1), j + (k >> 1), c);
                    ac += wt[k] * cv;
                    for (int ch = 0; ch < 3; ch++)
                        acc[ch] += wt[k] * (c >> (8 * ch) & 255);
                }
                cov = (int)std::lround(ac);
                uint32_t rgb = 0;
                for (int ch = 0; ch < 3; ch++)
                    rgb |= (uint32_t)std::clamp((int)std::lround(acc[ch]), 0, 255) << (8 * ch);
                px = LiveF::pack(rgb);
            }
            if (cov <= 0)
                continue;
            Pix& d = t.s.at(x, y);
            d = key_blend(d, px, cov, key, o.a);
            if (t.m) {
                size_t mi = t.s.index(x, y);
                t.m[mi] = cover8(t.m[mi], (o.a * cov + 127) / 255);
            }
        }
}

int group_end(const Frame& f, int i, int i1)
{
    int depth = 1, e = i + 1;
    for (; e < i1 && depth > 0; e++)
        depth += f.ops[e].kind == OpKind::Group ? 1 : f.ops[e].kind == OpKind::End ? -1 : 0;
    return depth > 0 ? i1 : e - 1;
}

Rect range_bounds(const Frame& f, int i0, int i1)
{
    Rect b;
    for (int i = i0; i < i1; i++) {
        const Op& o = f.ops[i];
        if (o.kind == OpKind::End)
            continue;
        if (o.kind == OpKind::Group) {
            int end = group_end(f, i, i1);
            Rect inner = o.src.empty() ? range_bounds(f, i + 1, end) : o.src;
            b = b.unite(sampled(o, inner).intersect(o.clip));
            i = end;
            continue;
        }
        b = b.unite(geom(o));
    }
    return b;
}

Rect preimage(const Op& q, Rect r)
{
    const Rect cap{-2 * kMaxDim, -2 * kMaxDim, 3 * kMaxDim, 3 * kMaxDim};
    if (r.empty())
        return {};
    double m[9];
    q.xf.inverse(m);
    double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
    for (int px : {r.x0, r.x1})
        for (int py : {r.y0, r.y1}) {
            double w = m[6] * px + m[7] * py + m[8];
            if (!(w > 0))
                return cap;
            double u = (m[0] * px + m[1] * py + m[2]) / w, v = (m[3] * px + m[4] * py + m[5]) / w;
            x0 = std::min(x0, u);
            y0 = std::min(y0, v);
            x1 = std::max(x1, u);
            y1 = std::max(y1, v);
        }
    auto clamp = [](double v) { return std::clamp(v, -1e6, 1e6); };
    Rect b{(int)std::floor(clamp(x0)), (int)std::floor(clamp(y0)), (int)std::ceil(clamp(x1)),
           (int)std::ceil(clamp(y1))};
    return b.grow(2).intersect(cap);
}

constexpr bool kMipRows = std::is_same_v<LiveL, Rows> && std::is_same_v<MipT<LiveF>, LiveF>;

bool run_quad(const Frame& f, bool first, const Op& q, const LayerRef& l, Tgt& t, Rect clip, FrameScratch& sc)
{
    if (t.m)
        return false;
    const Xform& xf = q.xf;
    Rect c = clip.intersect(t.clip).intersect(t.s.rect());
    if (l.fmt == LayerFmt::Mip) {
        if constexpr (kMipRows) {
            if (!l.mip || xf.swivel || xf.eye || xf.d <= 0)
                return false;
            MipLayer ink = *l.mip;
            ink.l0.src = ink.l0.src.intersect(q.src);
            if (ink.l0.src.empty())
                return true;
            if (!sc.mip)
                sc.mip = std::make_unique<MipScratch>();
            mip_menu(ink, t.s, xf.projection(), mip_alpha<LiveF>(std::min(q.a, 256)),
                     q.sample == Sample::Nearest ? MipForm::Nearest : MipForm::Bilinear, q.level, *sc.mip, kScreen, c);
            return true;
        }
        return false;
    }
    if (l.fmt == LayerFmt::Masked && l.mask && xf.angle == 0 && xf.scale > 0 && q.sample == Sample::Nearest) {
        Rect s = q.src.intersect(Rect{l.ox, l.oy, l.ox + l.s.w, l.oy + l.s.h});
        blend_scaled_rect(l.s, l.mask, Rect{s.x0 - l.ox, s.y0 - l.oy, s.x1 - l.ox, s.y1 - l.oy}, t.s, c, xf.scale,
                          xf.x + xf.scale * l.ox, xf.y + xf.scale * l.oy, l.key, q.a, true);
        return true;
    }
    Rect full = t.s.rect(), have{0, 0, l.s.w, l.s.h};
    bool whole = l.ox == 0 && l.oy == 0 && l.s.w == t.s.w && l.s.h == t.s.h && t.s.w == kPortraitW &&
                 same_rect(q.src.intersect(have), have) && same_rect(c, full);
    if (first && whole && l.fmt == LayerFmt::Opaque && q.sample == Sample::Nearest && xf.angle != 0 && !xf.swivel &&
        xf.d > 0 && cos_deg(xf.angle) > 0 && xf.scale == kOne && xf.x == 0 && xf.y == 0) {
        sc.rows.resize(kPortraitW);
        turnstile_rows(xf.angle, xf.hinge, xf.d, sc.rows.data());
        warp(l.s, t.s, sc.rows.data(), f.ground, q.a, true, xf.eye);
        return true;
    }
    return false;
}

Surface surface_of(Pix* px, int w, int h)
{
    Surface s;
    s.px = px;
    s.w = w;
    s.h = h;
    s.stride = std::is_same_v<LiveL, Rot> ? h : w;
    return s;
}

void draw_range(const Frame& f, int i0, int i1, Tgt& t, fx dx, fx dy, Rect clip, const FrameSources& src,
                FrameScratch& sc, int level, int first, bool& ok)
{
    for (int i = i0; i < i1; i++) {
        const Op& o = f.ops[i];
        if (o.kind == OpKind::End)
            continue;
        Rect oc = shift(o.clip, dx, dy).intersect(clip);
        if (o.kind == OpKind::Group) {
            int end = group_end(f, i, i1);
            if ((o.xf.plain() && o.a >= 256 && !o.frozen) || level >= kFrameDepth) {
                draw_range(f, i + 1, end, t, dx + o.xf.x, dy + o.xf.y, oc, src, sc, level, -1, ok);
            } else {
                ok = false;
                Op q = o;
                q.kind = OpKind::Quad;
                q.xf.x += dx;
                q.xf.y += dy;
                q.blend = Blend::Over;
                Rect inner = o.src.empty() ? range_bounds(f, i + 1, end) : o.src;
                Rect area = inner.intersect(preimage(q, oc.intersect(t.clip).intersect(t.s.rect())));
                if (!area.empty()) {
                    FrameScratch::Level& lv = sc.lv[level];
                    int w = area.x1 - area.x0, h = area.y1 - area.y0;
                    lv.px.assign((size_t)w * h, Pix{});
                    lv.m.assign((size_t)w * h, 0);
                    Surface s = surface_of(lv.px.data(), w, h);
                    Tgt st{s, lv.m.data(), s.rect()};
                    draw_range(f, i + 1, end, st, fx_int(-area.x0), fx_int(-area.y0), st.clip, src, sc, level + 1, -1,
                               ok);
                    LayerRef lr{s, lv.m.data(), 0, LayerFmt::Masked, area.x0, area.y0};
                    q.src = area;
                    quad_px(q, lr, t, oc);
                }
            }
            i = end;
            continue;
        }
        if (o.kind == OpKind::Quad) {
            LayerRef lr;
            if (!src.layer(o.layer, lr)) {
                ok = false;
                continue;
            }
            Op q = o;
            q.xf.x += dx;
            q.xf.y += dy;
            if (run_quad(f, i == first, q, lr, t, oc, sc))
                continue;
            ok = false;
            if (lr.fmt != LayerFmt::Mip)
                quad_px(q, lr, t, oc);
            continue;
        }
        Rect r = shift(o.src, dx, dy);
        Canvas c(t.s, t.m);
        if (o.kind == OpKind::Paint) {
            Op q = o;
            q.xf.x += dx;
            q.xf.y += dy;
            q.src = r;
            q.clip = oc;
            c.set_clip(r.intersect(oc).intersect(t.clip));
            ok = src.paint(q, c) && ok;
            continue;
        }
        c.set_clip(oc.intersect(t.clip));
        if (o.kind == OpKind::Fill) {
            c.blend(r, o.rgb, o.a);
            continue;
        }
        Rect p = r.intersect(c.clip());
        for (int y = p.y0; y < p.y1; y++) {
            uint32_t rgb = 0;
            int ra = 0;
            if (src.strip(o.layer, y - r.y0, rgb, ra))
                c.blend(Rect{r.x0, y, r.x1, y + 1}, rgb, ra * o.a / 256);
        }
    }
}

void ground_rows(const Frame& f, Canvas& c, const FrameSources& src)
{
    Rect clip = c.clip();
    int strips[Frame::kOps], n = 0, depth = 0;
    for (int i = 0; i < f.n; i++) {
        const Op& o = f.ops[i];
        if (o.kind == OpKind::Group)
            depth++;
        else if (o.kind == OpKind::End)
            depth -= depth > 0;
        else if (depth == 0 && o.kind == OpKind::Strip && o.a >= 256) {
            Rect p = o.src.intersect(o.clip).intersect(kScreen).intersect(clip);
            if (!p.empty() && p.x0 <= clip.x0 && p.x1 >= clip.x1)
                strips[n++] = i;
        }
    }
    auto covered = [&](int y) {
        for (int k = 0; k < n; k++) {
            const Op& o = f.ops[strips[k]];
            Rect p = o.src.intersect(o.clip).intersect(kScreen).intersect(clip);
            uint32_t rgb = 0;
            int ra = 0;
            if (y >= p.y0 && y < p.y1 && src.strip(o.layer, y - o.src.y0, rgb, ra) && ra * o.a / 256 >= 256)
                return true;
        }
        return false;
    };
    for (int y = clip.y0; y < clip.y1;) {
        if (n && covered(y)) {
            y++;
            continue;
        }
        int e = y + 1;
        while (e < clip.y1 && !(n && covered(e)))
            e++;
        c.ground(Rect{clip.x0, y, clip.x1, e}, f.ground);
        y = e;
    }
}

}

bool Op::same(const Op& o) const
{
    return kind == o.kind && id == o.id && layer == o.layer && same_rect(src, o.src) && same_rect(bounds, o.bounds) &&
           xf == o.xf && a == o.a && same_rect(clip, o.clip) && sample == o.sample && blend == o.blend &&
           rgb == o.rgb && frozen == o.frozen && level == o.level;
}

Rect op_bounds(const Op& o) { return o.bounds.empty() ? geom(o) : o.bounds; }

size_t FrameScratch::bytes() const
{
    size_t n = rows.capacity() * sizeof(WarpRow);
    for (const Level& l : lv)
        n += l.px.capacity() * sizeof(Pix) + l.m.capacity();
    if (mip)
        n += mip->cols.capacity() * sizeof(MipCol) + mip->runs.capacity() * sizeof(MipRun) +
             mip->idx.capacity() * sizeof(int);
    return n;
}

void FrameScratch::release()
{
    for (Level& l : lv) {
        std::vector<Pix>().swap(l.px);
        std::vector<uint8_t>().swap(l.m);
    }
    std::vector<WarpRow>().swap(rows);
    mip.reset();
}

Rect frame_damage(const Frame& prev, const Frame& next, const FrameSources& src)
{
    if (prev.n == 0 || prev.ground != next.ground || (next.flags & kFrameFull) || prev.over_cap || next.over_cap ||
        prev.over_depth || next.over_depth || prev.phase != next.phase || prev.over != next.over ||
        (prev.flags & ~kFrameFull) != (next.flags & ~kFrameFull))
        return kScreen;
    Walk pws, nws;
    Walk* pw = &pws;
    Walk* nw = &nws;
    if (!walk(prev, pws) || !walk(next, nws))
        return kScreen;
    Rect d;
    for (int i = 0; i < nw->ng; i++) {
        const Entry& e = nw->groups[i];
        const Entry* p = find(pw->groups, pw->ng, e.key);
        if (p && (!prev.ops[p->op].same(next.ops[e.op]) || !chain_same(prev, *p, next, e)))
            d = d.unite(p->b).unite(e.b);
    }
    for (int i = 0; i < pw->ng; i++)
        if (!find(nw->groups, nw->ng, pw->groups[i].key))
            d = d.unite(pw->groups[i].b);
    int rank_p[Frame::kOps], rank_n[Frame::kOps], match[Frame::kOps];
    int common = 0;
    for (int i = 0; i < nw->nl; i++) {
        const Entry& e = nw->leaves[i];
        const Entry* p = find(pw->leaves, pw->nl, e.key);
        match[i] = p ? (int)(p - pw->leaves) : -1;
        if (!p) {
            d = d.unite(e.b);
            continue;
        }
        rank_n[common] = i;
        rank_p[common++] = match[i];
        const Op& po = prev.ops[p->op];
        const Op& no = next.ops[e.op];
        if (!po.same(no) || !chain_same(prev, *p, next, e)) {
            d = d.unite(p->b).unite(e.b);
        } else if (po.version != no.version) {
            Rect r;
            if (no.kind == OpKind::Quad && no.bounds.empty() && src.damage(no.layer, po.version, no.version, r)) {
                Rect b = sampled(no, r.intersect(no.src));
                d = d.unite(chain(next, b.intersect(no.clip), e.groups, e.depth));
            } else if (no.kind != OpKind::Fill) {
                d = d.unite(p->b).unite(e.b);
            }
        }
    }
    for (int i = 0; i < pw->nl; i++) {
        bool kept = false;
        for (int j = 0; j < nw->nl && !kept; j++)
            kept = match[j] == i;
        if (!kept)
            d = d.unite(pw->leaves[i].b);
    }
    for (int a = 0; a < common; a++)
        for (int b = a + 1; b < common; b++) {
            if (rank_p[a] < rank_p[b])
                continue;
            const Entry& na = nw->leaves[rank_n[a]];
            const Entry& nb = nw->leaves[rank_n[b]];
            const Entry& pa = pw->leaves[rank_p[a]];
            const Entry& pb = pw->leaves[rank_p[b]];
            Rect ua = na.b.unite(pa.b), ub = nb.b.unite(pb.b);
            if (!ua.intersect(ub).empty())
                d = d.unite(ua).unite(ub);
        }
    return d.intersect(kScreen);
}

bool frame_draw(const Frame& f, Canvas& c, const FrameSources& src, FrameScratch& sc)
{
    ground_rows(f, c, src);
    Tgt t{c.surface(), c.mask(), c.clip()};
    int first = 0;
    while (first < f.n && f.ops[first].kind == OpKind::End)
        first++;
    bool ok = true;
    draw_range(f, 0, f.n, t, 0, 0, kScreen, src, sc, 0, first, ok);
    return ok;
}

}
