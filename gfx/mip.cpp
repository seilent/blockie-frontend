#include "mip.h"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <type_traits>

#include "../core/curves.h"
#ifdef FD_BOARD_A105
#include "kernels.h"
#endif

namespace fd {

namespace {

constexpr int kBlock = 1 << kMipBlockBits;

struct Col {
    int64_t u = 0, step = 0;
    bool live = false;
};

struct Map {
    const Projection& pr;
    fx c, sn;
    int64_t inv;

    explicit Map(const Projection& p)
        : pr(p), c(cos_deg(p.angle)), sn(sin_deg(p.angle)), inv(((int64_t)kOne << kMipBits) / p.scale)
    {
    }

    Col col(int x) const
    {
        Col k;
        int64_t xl = ((((int64_t)x << kFxBits) + kHalf - pr.x) * inv) >> kFxBits;
        int64_t xp = xl - ((int64_t)pr.hinge << kMipBits);
        int64_t den = ((int64_t)c << (kMipBits - kFxBits)) - ((xp * sn) >> kFxBits) / pr.d;
        if (den <= 0)
            return k;
        k.u = ((int64_t)pr.hinge << kFxBits) + xp * kOne / den;
        if constexpr (kMipWide)
            k.step = ((int64_t)c << (kMipBits - kFxBits)) * inv / den;
        else
            k.step = ((((int64_t)c << kFxBits) / den) * inv) >> kFxBits;
        k.live = true;
        return k;
    }

    int64_t y16(int y) const { return ((int64_t)y << kFxBits) + kHalf - pr.y; }
    int64_t row(int y, int64_t step) const { return y16(y) * step; }
};

int near_x(int64_t u, int sh) { return (int)(u >> (kFxBits + sh)); }
int bil_x(int64_t u, int sh) { return (int)(((u >> sh) - kHalf + (1 << 10)) >> 11); }

bool inside(const Rect& r, int i, int j) { return i >= r.x0 && i < r.x1 && j >= r.y0 && j < r.y1; }

template <class F> size_t at(const MipViewT<F>& lv, int i, int j)
{
    return (size_t)(j - lv.oy) * lv.stride + (i - lv.ox);
}

template <class F> struct TapT {
    typename F::T v;
    int m;
};

template <class F> TapT<F> texel(const MipViewT<F>& lv, int i, int j)
{
    if (!inside(lv.r, i, j))
        return {lv.k, 0};
    size_t a = at(lv, i, j);
    typename F::T v = lv.v[a];
    return {v, lv.m ? lv.m[a] : v == lv.k ? 0 : 255};
}

uint16_t bil_px(uint16_t a, uint16_t b, uint16_t c, uint16_t d, int fx5, int fy5)
{
    int w00 = (32 - fx5) * (32 - fy5), w10 = fx5 * (32 - fy5), w01 = (32 - fx5) * fy5, w11 = fx5 * fy5;
    auto lane = [&](int sh, int top) {
        int s = (a >> sh & top) * w00 + (b >> sh & top) * w10 + (c >> sh & top) * w01 + (d >> sh & top) * w11;
        return (uint16_t)(((s + 512) >> 10) << sh);
    };
    return (uint16_t)(lane(11, 31) | lane(5, 63) | lane(0, 31));
}

#ifdef FD_MIP_X888
uint64_t lanes888(uint32_t p) { return (p & 0xFF) | (uint64_t)(p & 0xFF00) << 13 | (uint64_t)(p & 0xFF0000) << 26; }

uint32_t fold888(uint64_t x)
{
    return (uint32_t)(x & 0xFF) | (uint32_t)(x >> 13 & 0xFF00) | (uint32_t)(x >> 26 & 0xFF0000);
}

constexpr uint64_t kRound888 = 512 | (uint64_t)512 << 21 | (uint64_t)512 << 42;

uint32_t bil_px(uint32_t a, uint32_t b, uint32_t c, uint32_t d, int fx5, int fy5)
{
    uint64_t w00 = (32 - fx5) * (32 - fy5), w10 = fx5 * (32 - fy5), w01 = (32 - fx5) * fy5, w11 = fx5 * fy5;
    return fold888((lanes888(a) * w00 + lanes888(b) * w10 + lanes888(c) * w01 + lanes888(d) * w11 + kRound888) >> 10);
}
#endif

int bil8(int a, int b, int c, int d, int fx5, int fy5)
{
    return (a * (32 - fx5) * (32 - fy5) + b * fx5 * (32 - fy5) + c * (32 - fx5) * fy5 + d * fx5 * fy5 + 512) >> 10;
}

template <class F> TapT<F> tap_bil(const MipViewT<F>& lv, int tx, int ty)
{
    int i = tx >> 5, j = ty >> 5, fx5 = tx & 31, fy5 = ty & 31;
    TapT<F> a = texel(lv, i, j), b = texel(lv, i + 1, j), c = texel(lv, i, j + 1), d = texel(lv, i + 1, j + 1);
    return {bil_px(a.v, b.v, c.v, d.v, fx5, fy5), bil8(a.m, b.m, c.m, d.m, fx5, fy5)};
}

uint8_t flag(const MipViewT<R565>& lv, int i, int j)
{
    const MipBlocks* b = lv.blocks;
    if (!b)
        return kMipPartial;
    if (!inside(lv.r, i, j))
        return kMipEmpty;
    i -= lv.ox;
    j -= lv.oy;
    if (i < 0 || j < 0)
        return kMipEmpty;
    int bx = i >> kMipBlockBits, by = j >> kMipBlockBits;
    if (bx >= b->w || by >= b->h)
        return kMipEmpty;
    return b->f[(size_t)by * b->w + bx];
}

#ifdef FD_MIP_X888
uint8_t flag(const MipViewT<X888>& lv, int i, int j)
{
    const MipBlocks* b = lv.blocks;
    if (!b)
        return kMipPartial;
    if (!inside(lv.r, i, j))
        return kMipEmpty;
    i -= lv.ox;
    j -= lv.oy;
    if (i < 0 || j < 0)
        return kMipEmpty;
    int bx = i >> kMipBlockBits, by = j >> kMipBlockBits;
    if (bx >= b->w || by >= b->h)
        return kMipEmpty;
    return b->f[(size_t)by * b->w + bx];
}
#endif

template <class F> struct PlaneT {
    const typename F::T* v;
    const uint8_t* m;
    size_t stride;
    int ox, oy;
    Rect grid, r;
    typename F::T k;
};

uint8_t block_flag(const PlaneT<R565>& p, int bx, int by)
{
    Rect b = Rect{p.ox + bx * kBlock, p.oy + by * kBlock, p.ox + (bx + 1) * kBlock, p.oy + (by + 1) * kBlock}
                 .intersect(p.grid);
    Rect in = b.intersect(p.r);
    if (in.empty())
        return kMipEmpty;
    bool full = in.x0 == b.x0 && in.y0 == b.y0 && in.x1 == b.x1 && in.y1 == b.y1, empty = true;
    uint32_t kk = (uint32_t)p.k | (uint32_t)p.k << 16;
    int n = in.x1 - in.x0;
    for (int j = in.y0; j < in.y1 && (full || empty); j++) {
        size_t row = (size_t)(j - p.oy) * p.stride + (in.x0 - p.ox);
        const uint8_t* pm = p.m + row;
        const uint16_t* pv = p.v + row;
        int i = 0;
        if (!((uintptr_t)pm & 3) && !((uintptr_t)pv & 3))
            for (; i + 4 <= n; i += 4) {
                uint32_t mw, v0, v1;
                memcpy(&mw, pm + i, 4);
                memcpy(&v0, pv + i, 4);
                memcpy(&v1, pv + i + 2, 4);
                if (mw != 0xFFFFFFFFu)
                    full = false;
                if (mw || v0 != kk || v1 != kk)
                    empty = false;
            }
        for (; i < n; i++) {
            if (pm[i] != 255)
                full = false;
            if (pm[i] || pv[i] != p.k)
                empty = false;
        }
    }
    return empty ? kMipEmpty : full ? kMipFull : kMipPartial;
}

#ifdef FD_MIP_X888
uint8_t block_flag(const PlaneT<X888>& p, int bx, int by)
{
    Rect b = Rect{p.ox + bx * kBlock, p.oy + by * kBlock, p.ox + (bx + 1) * kBlock, p.oy + (by + 1) * kBlock}
                 .intersect(p.grid);
    Rect in = b.intersect(p.r);
    if (in.empty())
        return kMipEmpty;
    bool full = in.x0 == b.x0 && in.y0 == b.y0 && in.x1 == b.x1 && in.y1 == b.y1, empty = true;
    int n = in.x1 - in.x0;
    for (int j = in.y0; j < in.y1 && (full || empty); j++) {
        size_t row = (size_t)(j - p.oy) * p.stride + (in.x0 - p.ox);
        const uint8_t* pm = p.m + row;
        const uint32_t* pv = p.v + row;
        int i = 0;
        if (!((uintptr_t)pm & 3))
            for (; i + 4 <= n; i += 4) {
                uint32_t mw;
                memcpy(&mw, pm + i, 4);
                if (mw != 0xFFFFFFFFu)
                    full = false;
                if (mw || ((pv[i] ^ p.k) | (pv[i + 1] ^ p.k) | (pv[i + 2] ^ p.k) | (pv[i + 3] ^ p.k)))
                    empty = false;
            }
        for (; i < n; i++) {
            if (pm[i] != 255)
                full = false;
            if (pm[i] || pv[i] != p.k)
                empty = false;
        }
    }
    return empty ? kMipEmpty : full ? kMipFull : kMipPartial;
}
#endif

struct MaskPlane {
    const uint8_t* m;
    size_t stride;
    int ox, oy;
    Rect grid, r;
};

uint8_t block_flag(const MaskPlane& p, int bx, int by)
{
    Rect b = Rect{p.ox + bx * kBlock, p.oy + by * kBlock, p.ox + (bx + 1) * kBlock, p.oy + (by + 1) * kBlock}
                 .intersect(p.grid);
    Rect in = b.intersect(p.r);
    if (in.empty())
        return kMipEmpty;
    bool full = in.x0 == b.x0 && in.y0 == b.y0 && in.x1 == b.x1 && in.y1 == b.y1, empty = true;
    int n = in.x1 - in.x0;
    for (int j = in.y0; j < in.y1 && (full || empty); j++) {
        const uint8_t* pm = p.m + (size_t)(j - p.oy) * p.stride + (in.x0 - p.ox);
        int i = 0;
        if (!((uintptr_t)pm & 3))
            for (; i + 4 <= n; i += 4) {
                uint32_t mw;
                memcpy(&mw, pm + i, 4);
                if (mw != 0xFFFFFFFFu)
                    full = false;
                if (mw)
                    empty = false;
            }
        for (; i < n; i++) {
            if (pm[i] != 255)
                full = false;
            if (pm[i])
                empty = false;
        }
    }
    return empty ? kMipEmpty : full ? kMipFull : kMipPartial;
}

template <class P> void flag_blocks(const P& p, Rect t, MipBlocks& out)
{
    t = t.intersect(p.grid);
    if (t.empty())
        return;
    for (int by = (t.y0 - p.oy) >> kMipBlockBits; by < (t.y1 - p.oy + kBlock - 1) >> kMipBlockBits; by++)
        for (int bx = (t.x0 - p.ox) >> kMipBlockBits; bx < (t.x1 - p.ox + kBlock - 1) >> kMipBlockBits; bx++)
            out.f[(size_t)by * out.w + bx] = block_flag(p, bx, by);
}

template <class F> PlaneT<F> level_plane(const MipLevelT<F>& l)
{
    return {l.v.data(), l.m.data(), (size_t)l.w, l.ox, l.oy, {l.ox, l.oy, l.ox + l.w, l.oy + l.h}, l.r, l.k};
}

uint16_t mean_px(uint16_t a, uint16_t b, uint16_t c, uint16_t d) { return mean565(a, b, c, d); }

#ifdef FD_MIP_X888
uint32_t mean_px(uint32_t a, uint32_t b, uint32_t c, uint32_t d) { return mean888(a, b, c, d); }
#endif

template <class F, class L>
void build_texels(const SurfaceT<F, L>& src, const uint8_t* mask, Rect t, MipLevelT<F>& out)
{
    using T = typename F::T;
    Rect s = out.src;
    T k = out.k;
    int ia = (s.x0 + 1) >> 1, ib = s.x1 >> 1;
    for (int j = t.y0; j < t.y1; j++) {
        int y0 = 2 * j, y1 = 2 * j + 1;
        bool in0 = y0 >= s.y0 && y0 < s.y1, in1 = y1 >= s.y0 && y1 < s.y1;
        const T* p0 = in0 ? src.row(y0) : nullptr;
        const T* p1 = in1 ? src.row(y1) : nullptr;
        const uint8_t* m0 = in0 && mask ? mask + src.index(0, y0) : nullptr;
        const uint8_t* m1 = in1 && mask ? mask + src.index(0, y1) : nullptr;
        T* dv = out.v.data() + (size_t)(j - out.oy) * out.w;
        uint8_t* dm = mask ? out.m.data() + (size_t)(j - out.oy) * out.w : nullptr;
        int o = out.ox;
        auto pv = [&](const T* p, int x) { return p && x >= s.x0 && x < s.x1 ? p[x] : k; };
        auto pm = [&](const uint8_t* p, int x) { return p && x >= s.x0 && x < s.x1 ? (int)p[x] : 0; };
        auto edge = [&](int i) {
            int x0 = 2 * i, x1 = x0 + 1;
            dv[i - o] = mean_px(pv(p0, x0), pv(p0, x1), pv(p1, x0), pv(p1, x1));
            if (dm)
                dm[i - o] = mean8(pm(m0, x0), pm(m0, x1), pm(m1, x0), pm(m1, x1));
        };
        int lo = t.x0, hi = t.x0;
        if (in0 && in1) {
            lo = std::clamp(ia, t.x0, t.x1);
            hi = std::clamp(ib, lo, t.x1);
        }
        for (int i = t.x0; i < lo; i++)
            edge(i);
        for (int i = lo; i < hi; i++) {
            int x = 2 * i;
            dv[i - o] = mean_px(p0[x], p0[x + 1], p1[x], p1[x + 1]);
            if (dm)
                dm[i - o] = mean8(m0[x], m0[x + 1], m1[x], m1[x + 1]);
        }
        for (int i = hi; i < t.x1; i++)
            edge(i);
    }
}

template <class F> typename F::T ground_px(uint32_t g) { return F::pack(std::is_same_v<F, X888> ? g & 0xFFFFFF : g); }

Rect texels_of(Rect p) { return {p.x0 >> 1, p.y0 >> 1, (p.x1 + 1) >> 1, (p.y1 + 1) >> 1}; }

template <class F> bool has_l1(const MipLayerT<F>& l) { return l.l1.v || l.l1.m; }

template <class F> const MipViewT<F>& view_for(const MipLayerT<F>& l, int level)
{
    return level > 0 && has_l1(l) ? l.l1 : l.l0;
}

template <class F> int used(const MipLayerT<F>& l, int level) { return level > 0 && has_l1(l) ? 1 : 0; }

template <class F> void cover_build(const uint8_t* cov, size_t stride, Rect r, uint32_t tint, MipLevelT<F>& out)
{
    Rect t = r.empty() ? Rect{} : texels_of(r);
    out.ox = t.x0;
    out.oy = t.y0;
    out.w = t.x1 - t.x0;
    out.h = t.y1 - t.y0;
    std::vector<typename F::T>().swap(out.v);
    out.m.resize((size_t)out.w * out.h);
    out.blocks.w = (out.w + kBlock - 1) >> kMipBlockBits;
    out.blocks.h = (out.h + kBlock - 1) >> kMipBlockBits;
    out.blocks.f.resize((size_t)out.blocks.w * out.blocks.h);
    out.src = r;
    out.r = t;
    out.k = 0;
    out.tint = ground_px<F>(tint);
    out.cover = true;
    out.built = true;
    auto pm = [&](int x, int y) { return inside(r, x, y) ? (int)cov[(size_t)y * stride + x] : 0; };
    for (int j = t.y0; j < t.y1; j++)
        for (int i = t.x0; i < t.x1; i++) {
            int x = 2 * i, y = 2 * j;
            out.m[(size_t)(j - out.oy) * out.w + (i - out.ox)] =
                mean8(pm(x, y), pm(x + 1, y), pm(x, y + 1), pm(x + 1, y + 1));
        }
    Rect g{out.ox, out.oy, out.ox + out.w, out.oy + out.h};
    flag_blocks(MaskPlane{out.m.data(), (size_t)out.w, out.ox, out.oy, g, t}, g, out.blocks);
}

uint16_t tint_px(uint16_t d, uint16_t v, int m, int a5) { return tint565(d, v, m, a5); }

#ifdef FD_MIP_X888
uint32_t tint_px(uint32_t d, uint32_t v, int m, int a)
{
    if (m == 255 && a == 256)
        return v;
    uint32_t q = 0;
    for (int sh = 0; sh < 24; sh += 8) {
        int dc = (int)(d >> sh & 255), t = ((int)(v >> sh & 255) - dc) * m;
        q |= (uint32_t)(dc + (t >= 0 ? div255(t) : -div255(-t))) << sh;
    }
    return blend_px(d, q, a);
}
#endif

uint16_t unpremul(uint16_t v, int m, uint16_t k)
{
    auto lane = [&](int sh, int top) {
        int g = k >> sh & top, t = ((int)(v >> sh & top) - g) * 255;
        int q = g + (t >= 0 ? (t + m / 2) / m : -((-t + m / 2) / m));
        return (uint16_t)(std::clamp(q, 0, top) << sh);
    };
    return (uint16_t)(lane(11, 31) | lane(5, 63) | lane(0, 31));
}

#ifdef FD_MIP_X888
uint32_t unpremul(uint32_t v, int m, uint32_t k)
{
    auto lane = [&](int sh) {
        int g = k >> sh & 255, t = ((int)(v >> sh & 255) - g) * 255;
        int q = g + (t >= 0 ? (t + m / 2) / m : -((-t + m / 2) / m));
        return (uint32_t)std::clamp(q, 0, 255) << sh;
    };
    return lane(16) | lane(8) | lane(0);
}
#endif

template <class F> uint32_t sprite_px(TapT<F> t, typename F::T k, bool coverage)
{
    constexpr int sh = kSpriteShift<F>;
    if (!coverage)
        return t.v == k ? 0 : 255u << sh | t.v;
    if (t.m <= 0)
        return 0;
    if (t.m >= 255)
        return 255u << sh | t.v;
    return (uint32_t)t.m << sh | unpremul(t.v, t.m, k);
}

MipAcc acc(int64_t s) { return (MipAcc)(s >> kFxBits); }
int near_a(MipAcc a, int sh) { return (int)(a >> (kMipBits + sh)); }
int bil_a(MipAcc a, int sh)
{
    return (int)(((a >> sh) - ((MipAcc)1 << (kMipBits - 1)) + (1 << (kMipBits - 6))) >> (kMipBits - 5));
}

struct Lerp {
    uint32_t xk;
    uint32_t a5;
    uint16_t operator()(uint16_t s) const { return fold565(xk + (((spread565(s) - xk) * a5) >> 5)); }
};

Lerp lerp_of(uint16_t k, int a5) { return {spread565(k), (uint32_t)a5}; }

#ifdef FD_MIP_X888
struct Lerp32 {
    uint32_t k;
    int a;
    uint32_t operator()(uint32_t s) const { return blend_px(k, s, a); }
};

Lerp32 lerp_of(uint32_t k, int a) { return {k, a}; }
#endif

uint32_t rb_of(uint32_t t) { return t >> 11 | (t & 31) << 16; }

uint16_t bil_rows(const uint16_t* p0, const uint16_t* p1, int i, int fx5, int fy5)
{
    uint32_t wx0 = 32 - fx5, wx1 = fx5, wy0 = 32 - fy5, wy1 = fy5;
    uint32_t a = p0[i], b = p0[i + 1], c = p1[i], d = p1[i + 1];
    uint32_t top = rb_of(a) * wx0 + rb_of(b) * wx1, bot = rb_of(c) * wx0 + rb_of(d) * wx1;
    uint32_t x = top * wy0 + bot * wy1 + (512u | 512u << 16);
    uint32_t gt = (a >> 5 & 63) * wx0 + (b >> 5 & 63) * wx1, gb = (c >> 5 & 63) * wx0 + (d >> 5 & 63) * wx1;
    uint32_t g = (gt * wy0 + gb * wy1 + 512) >> 10;
    return (uint16_t)((x >> 10 & 63) << 11 | g << 5 | x >> 26);
}

#ifdef FD_MIP_X888
uint32_t bil_rows(const uint32_t* p0, const uint32_t* p1, int i, int fx5, int fy5)
{
    uint64_t wx0 = 32 - fx5, wx1 = fx5, wy0 = 32 - fy5, wy1 = fy5;
    uint64_t top = lanes888(p0[i]) * wx0 + lanes888(p0[i + 1]) * wx1;
    uint64_t bot = lanes888(p1[i]) * wx0 + lanes888(p1[i + 1]) * wx1;
    return fold888((top * wy0 + bot * wy1 + kRound888) >> 10);
}
#endif

int bil_mrows(const uint8_t* m0, const uint8_t* m1, int i, int fx5, int fy5)
{
    int top = m0[i] * (32 - fx5) + m0[i + 1] * fx5, bot = m1[i] * (32 - fx5) + m1[i + 1] * fx5;
    return (top * (32 - fy5) + bot * fy5 + 512) >> 10;
}

#ifdef FD_MIP_X888
bool empty_rows(const MipViewT<X888>& lv, int ta, int tb, int ca, int cb)
{
    const MipBlocks* bl = lv.blocks;
    int y0 = (std::min(ta, tb) - lv.oy) >> kMipBlockBits, y1 = (std::max(ta, tb) - lv.oy) >> kMipBlockBits;
    int x0 = std::min(ca, cb) >> kMipBlockBits, x1 = std::max(ca, cb) >> kMipBlockBits;
    for (int by = std::max(y0, 0); by <= std::min(y1, bl->h - 1); by++)
        for (int bx = std::max(x0, 0); bx <= std::min(x1, bl->w - 1); bx++)
            if (bl->f[(size_t)by * bl->w + bx] != kMipEmpty)
                return false;
    return true;
}
#endif

template <class T> void fill_row(T* d, T v, int n)
{
#ifdef FD_BOARD_A105
    if constexpr (std::is_same_v<T, uint32_t>) {
        kern::fill_span(d, v, n);
        return;
    }
#endif
    fill_px(d, v, n);
}

template <class Tap2> void pairs(int x0, int x1, Tap2 tap)
{
    int x = x0;
    for (; x + 1 < x1; x += 2)
        tap(x, x + 1);
    if (x < x1)
        tap(x, -1);
}

template <class F> const uint8_t* block_row(const MipViewT<F>& lv, int j)
{
    const MipBlocks* b = lv.blocks;
    int by = (j - lv.oy) >> kMipBlockBits;
    return b && by >= 0 && by < b->h ? b->f.data() + (size_t)by * b->w : nullptr;
}

template <class F> uint8_t block_at(const MipViewT<F>& lv, const uint8_t* row, int bx)
{
    if (!lv.blocks)
        return kMipPartial;
    return row && bx >= 0 && bx < lv.blocks->w ? row[bx] : (uint8_t)kMipEmpty;
}

template <class F>
bool span_all(const MipViewT<F>& lv, const uint8_t* f0, const uint8_t* f1, int b0, int b1, uint8_t want)
{
    if (!lv.blocks)
        return false;
    for (int b = b0; b <= b1; b++)
        if (block_at(lv, f0, b) != want || (f1 && block_at(lv, f1, b) != want))
            return false;
    return true;
}

template <class F, class L>
void tint_menu(const MipViewT<F>& lv, const SurfaceT<F, L>& dst, const Projection& pr, int a, MipForm form, Rect o,
               MipScratch& sc)
{
    using T = typename F::T;
    int w = o.x1 - o.x0, sh = lv.shift;
    bool near = form == MipForm::Nearest;
    Map map(pr);
    sc.cols.resize((size_t)w);
    int la = o.x1, lb = o.x0;
    for (int x = o.x0; x < o.x1; x++) {
        Col c = map.col(x);
        MipCol& m = sc.cols[x - o.x0];
        m.live = false;
        if (!c.live)
            continue;
        m.t = near ? near_x(c.u, sh) : bil_x(c.u, sh);
        int i0 = near ? m.t : m.t >> 5, i1 = near ? m.t : i0 + 1;
        if (i1 < lv.r.x0 || i0 >= lv.r.x1)
            continue;
        m.c = i0 - lv.ox;
        m.fx = m.t & 31;
        m.in = i0 >= lv.r.x0 && i1 < lv.r.x1;
        m.bx0 = m.c >> kMipBlockBits;
        m.bx1 = (m.c + 1) >> kMipBlockBits;
        m.a = acc(map.row(o.y0, c.step));
        m.inc = (MipAcc)c.step;
        m.live = true;
        la = std::min(la, x);
        lb = x + 1;
    }
    MipCol* cols = sc.cols.data();
    T v = lv.tint;
    bool fill = a == kMipOpaque<F>;
    int ry0 = lv.r.y0, ry1 = lv.r.y1;
    auto tap = [&](int x) { return near ? near_a(cols[x - o.x0].a, sh) : bil_a(cols[x - o.x0].a, sh); };
    auto cov = [&](int i, int j) { return inside(lv.r, i, j) ? (int)lv.m[at(lv, i, j)] : 0; };
    auto put = [&](T& d, int m) {
        if (m)
            d = tint_px(d, v, m, a);
    };
    auto slow = [&](T& d, const MipCol& c, int ty) {
        int i = c.t >> 5, j = ty >> 5;
        put(d, bil8(cov(i, j), cov(i + 1, j), cov(i, j + 1), cov(i + 1, j + 1), c.t & 31, ty & 31));
    };
    for (int y = o.y0; y < o.y1; y++) {
        T* d = dst.row(y);
        for (int x = la; x < lb;) {
            int xe = std::min(x + 16, lb), t0 = tap(x), t1 = tap(xe - 1);
            if (near && t0 == t1) {
                if (t0 >= ry0 && t0 < ry1) {
                    const uint8_t* p = lv.m + (size_t)(t0 - lv.oy) * lv.stride;
                    const uint8_t* fr = block_row(lv, t0);
                    int b0 = cols[x - o.x0].bx0, b1 = cols[xe - 1 - o.x0].bx0;
                    if (fill && span_all(lv, fr, nullptr, b0, b1, kMipFull))
                        fill_row(d + x, v, xe - x);
                    else if (!span_all(lv, fr, nullptr, b0, b1, kMipEmpty))
                        for (int xx = x; xx < xe; xx++)
                            put(d[xx], p[cols[xx - o.x0].c]);
                }
            } else if (near) {
                for (int xx = x; xx < xe; xx++) {
                    int j = tap(xx);
                    if (j >= ry0 && j < ry1)
                        put(d[xx], lv.m[(size_t)(j - lv.oy) * lv.stride + cols[xx - o.x0].c]);
                }
            } else if (int j = t0 >> 5; j == t1 >> 5) {
                const uint8_t *f0 = block_row(lv, j), *f1 = block_row(lv, j + 1);
                int b0 = cols[x - o.x0].bx0, b1 = cols[xe - 1 - o.x0].bx1;
                if (j + 1 >= ry0 && j < ry1 && !span_all(lv, f0, f1, b0, b1, kMipEmpty)) {
                    bool edge = j < ry0 || j + 1 >= ry1, full = fill && span_all(lv, f0, f1, b0, b1, kMipFull);
                    const uint8_t* m0 = edge ? nullptr : lv.m + (size_t)(j - lv.oy) * lv.stride;
                    for (int xx = x; xx < xe; xx++) {
                        const MipCol& c = cols[xx - o.x0];
                        int ty = t0 == t1 ? t0 : tap(xx);
                        if (edge || !c.in)
                            slow(d[xx], c, ty);
                        else if (full)
                            d[xx] = v;
                        else
                            put(d[xx], bil_mrows(m0, m0 + lv.stride, c.c, c.fx, ty & 31));
                    }
                }
            } else {
                for (int xx = x; xx < xe; xx++) {
                    const MipCol& c = cols[xx - o.x0];
                    int ty = tap(xx), jj = ty >> 5;
                    if (jj + 1 < ry0 || jj >= ry1)
                        continue;
                    if (!c.in || jj < ry0 || jj + 1 >= ry1) {
                        slow(d[xx], c, ty);
                        continue;
                    }
                    const uint8_t* m0 = lv.m + (size_t)(jj - lv.oy) * lv.stride;
                    put(d[xx], bil_mrows(m0, m0 + lv.stride, c.c, c.fx, ty & 31));
                }
            }
            x = xe;
        }
        for (int x = la; x < lb; x++)
            cols[x - o.x0].a += cols[x - o.x0].inc;
    }
}

}

MipScratch::MipScratch()
{
    cols.reserve(kMaxDim + 2);
    runs.reserve(kMaxDim + 2);
    idx.reserve(kMaxDim + 2);
}

void mip_alloc(MipLevelT<R565>& out, Rect r0, bool coverage)
{
    Rect t = r0.empty() ? Rect{} : texels_of(r0);
    out.ox = t.x0;
    out.oy = t.y0;
    out.w = t.x1 - t.x0;
    out.h = t.y1 - t.y0;
    size_t n = (size_t)out.w * out.h;
    out.v.resize(n);
    out.m.resize(coverage ? n : 0);
    out.blocks.w = coverage ? (out.w + kBlock - 1) >> kMipBlockBits : 0;
    out.blocks.h = coverage ? (out.h + kBlock - 1) >> kMipBlockBits : 0;
    out.blocks.f.resize((size_t)out.blocks.w * out.blocks.h);
    out.built = false;
}

void mip_drop(MipLevelT<R565>& out) { out.built = false; }

void mip_build(const uint8_t* cov, size_t stride, Rect r, uint32_t tint, MipLevelT<R565>& out)
{
    cover_build(cov, stride, r, tint, out);
}

MipViewT<R565> mip_view(const MipLevelT<R565>& l)
{
    MipViewT<R565> v;
    if (!l.built)
        return v;
    v.v = l.cover ? nullptr : l.v.data();
    v.m = l.m.empty() ? nullptr : l.m.data();
    v.stride = l.w;
    v.shift = 1;
    v.ox = l.ox;
    v.oy = l.oy;
    v.r = l.r;
    v.src = l.src;
    v.k = l.k;
    v.tint = l.tint;
    v.blocks = l.blocks.f.empty() ? nullptr : &l.blocks;
    return v;
}

#ifdef FD_MIP_X888
void mip_alloc(MipLevelT<X888>& out, Rect r0, bool coverage)
{
    Rect t = r0.empty() ? Rect{} : texels_of(r0);
    out.ox = t.x0;
    out.oy = t.y0;
    out.w = t.x1 - t.x0;
    out.h = t.y1 - t.y0;
    size_t n = (size_t)out.w * out.h;
    out.v.resize(n);
    out.m.resize(coverage ? n : 0);
    out.blocks.w = coverage ? (out.w + kBlock - 1) >> kMipBlockBits : 0;
    out.blocks.h = coverage ? (out.h + kBlock - 1) >> kMipBlockBits : 0;
    out.blocks.f.resize((size_t)out.blocks.w * out.blocks.h);
    out.built = false;
}

void mip_drop(MipLevelT<X888>& out) { out.built = false; }

void mip_build(const uint8_t* cov, size_t stride, Rect r, uint32_t tint, MipLevelT<X888>& out)
{
    cover_build(cov, stride, r, tint, out);
}

MipViewT<X888> mip_view(const MipLevelT<X888>& l)
{
    MipViewT<X888> v;
    if (!l.built)
        return v;
    v.v = l.cover ? nullptr : l.v.data();
    v.m = l.m.empty() ? nullptr : l.m.data();
    v.stride = l.w;
    v.shift = 1;
    v.ox = l.ox;
    v.oy = l.oy;
    v.r = l.r;
    v.src = l.src;
    v.k = l.k;
    v.tint = l.tint;
    v.blocks = l.blocks.f.empty() ? nullptr : &l.blocks;
    return v;
}
#endif

template <class F, class L>
void mip_build(const SurfaceT<F, L>& src, const uint8_t* mask, Rect r, uint32_t ground, MipLevelT<MipT<F>>& out)
{
    static_assert(std::is_same_v<L, Rows>, "mip levels are rows");
    Rect s = r.intersect(src.rect());
    Rect t = s.empty() ? Rect{} : texels_of(s);
    bool fits = t.x0 >= out.ox && t.y0 >= out.oy && t.x1 <= out.ox + out.w && t.y1 <= out.oy + out.h;
    if (!fits || out.m.empty() != !mask || out.v.size() != (size_t)out.w * out.h)
        mip_alloc(out, s, mask);
    out.src = s;
    out.r = t;
    out.k = ground_px<F>(ground);
    out.built = true;
    out.cover = false;
    build_texels(src, mask, t, out);
    if (mask)
        flag_blocks(level_plane(out), {out.ox, out.oy, out.ox + out.w, out.oy + out.h}, out.blocks);
}

template <class F, class L>
void mip_update(const SurfaceT<F, L>& src, const uint8_t* mask, Rect dirty, MipLevelT<MipT<F>>& out)
{
    static_assert(std::is_same_v<L, Rows>, "mip levels are rows");
    if (!out.built || out.cover)
        return;
    if (out.m.empty() != !mask) {
        mip_build(src, mask, out.src, F::unpack(out.k), out);
        return;
    }
    Rect t = texels_of(dirty.intersect(out.src)).intersect(out.r);
    if (t.empty())
        return;
    build_texels(src, mask, t, out);
    if (mask)
        flag_blocks(level_plane(out), t, out.blocks);
}

template <class F, class L>
void mip_blocks(const SurfaceT<F, L>& src, const uint8_t* mask, Rect r, uint32_t ground, Rect dirty, MipBlocks& out)
{
    static_assert(std::is_same_v<L, Rows>, "mip levels are rows");
    if (!mask) {
        out.w = out.h = 0;
        out.f.clear();
        return;
    }
    int bw = (src.w + kBlock - 1) >> kMipBlockBits, bh = (src.h + kBlock - 1) >> kMipBlockBits;
    if (out.w != bw || out.h != bh || out.f.size() != (size_t)bw * bh) {
        out.w = bw;
        out.h = bh;
        out.f.assign((size_t)bw * bh, kMipEmpty);
        dirty = src.rect();
    }
    PlaneT<F> p{src.px, mask, (size_t)src.stride, 0, 0, src.rect(), r.intersect(src.rect()), ground_px<F>(ground)};
    flag_blocks(p, dirty, out);
}

template <class F, class L>
MipViewT<MipT<F>> mip_view(const SurfaceT<F, L>& src, const uint8_t* mask, Rect r, uint32_t ground,
                           const MipBlocks* blocks)
{
    static_assert(std::is_same_v<L, Rows>, "mip levels are rows");
    MipViewT<F> v;
    v.v = src.px;
    v.m = mask;
    v.stride = src.stride;
    v.r = v.src = r.intersect(src.rect());
    v.k = ground_px<F>(ground);
    v.blocks = blocks && !blocks->f.empty() ? blocks : nullptr;
    return v;
}

void mip_blocks(const uint8_t* cov, size_t stride, Rect r, Rect dirty, MipBlocks& out)
{
    Rect g{0, 0, std::max(r.x1, 0), std::max(r.y1, 0)};
    int bw = (g.x1 + kBlock - 1) >> kMipBlockBits, bh = (g.y1 + kBlock - 1) >> kMipBlockBits;
    if (out.w != bw || out.h != bh || out.f.size() != (size_t)bw * bh) {
        out.w = bw;
        out.h = bh;
        out.f.assign((size_t)bw * bh, kMipEmpty);
        dirty = g;
    }
    flag_blocks(MaskPlane{cov, stride, 0, 0, g, r}, dirty, out);
}

template <class F>
MipViewT<F> mip_cover(const uint8_t* cov, size_t stride, Rect r, uint32_t tint, const MipBlocks* blocks)
{
    MipViewT<F> v;
    v.m = cov;
    v.stride = stride;
    v.r = v.src = r;
    v.tint = ground_px<F>(tint);
    v.blocks = blocks && !blocks->f.empty() ? blocks : nullptr;
    return v;
}

template <class F, class L>
MipViewT<MipT<F>> mip_view(const SurfaceT<F, L>& src, const uint8_t* mask, int x, int y, uint32_t ground)
{
    MipViewT<MipT<F>> v = mip_view(src, mask, src.rect(), ground);
    v.ox = x;
    v.oy = y;
    v.r = v.src = Rect{x, y, x + v.r.x1, y + v.r.y1}.intersect(kScreen);
    return v;
}

fx mip_step(Rect src, const Projection& pr)
{
    if (pr.scale < kMipMinScale)
        return 0;
    Map map(pr);
    Rect o = projected_bounds(src, pr).intersect(kScreen);
    if (o.empty())
        return 0;
    int64_t far[2] = {map.y16(o.y0), map.y16(o.y1 - 1)};
    int64_t best = 0;
    Col a = map.col(o.x0);
    for (int x = o.x0; x < o.x1; x++) {
        Col b = map.col(x + 1);
        int sx = a.live ? near_x(a.u, 0) : -1;
        if (a.live && sx >= src.x0 && sx < src.x1) {
            best = std::max(best, a.step);
            if (b.live) {
                best = std::max(best, std::abs(b.u - a.u) << (kMipBits - kFxBits));
                for (int64_t y : far)
                    best = std::max(best, std::abs(y * (b.step - a.step)) >> kFxBits);
            }
        }
        a = b;
    }
    if constexpr (kMipWide)
        best >>= kMipBits - kFxBits;
    return (fx)std::min<int64_t>(best, INT32_MAX);
}

int mip_pick(Rect src, const Projection& pr, fx sw) { return mip_step(src, pr) >= sw ? 1 : 0; }

template <class F, class L>
MipDraw mip_qp(const MipLayerT<MipT<F>>& l, const SurfaceT<F, L>& dst, const Projection& pr, int a, MipForm form,
               int level, MipScratch& sc, Rect clip)
{
    static_assert(std::is_same_v<L, Rows>, "mip levels are rows");
    using T = typename F::T;
    if (pr.scale < kMipMinScale || pr.angle != 0)
        return {};
    Rect o = projected_bounds(l.l0.src, pr).intersect(dst.rect()).intersect(clip);
    if (o.empty())
        return {};
    const MipViewT<F>& lv = view_for(l, level);
    if (!lv.v)
        return {};
    MipDraw out{o, used(l, level)};
    T k = lv.k;
    int w = o.x1 - o.x0, sh = lv.shift;
    bool near = form == MipForm::Nearest;
    Map map(pr);
    sc.cols.resize((size_t)w);
    sc.idx.resize((size_t)w);
    int64_t step = 0;
    int xa = o.x1, xb = o.x0;
    for (int x = o.x0; x < o.x1; x++) {
        Col c = map.col(x);
        MipCol& m = sc.cols[x - o.x0];
        m.live = c.live;
        m.in = false;
        if (!c.live)
            continue;
        if (!step)
            step = c.step;
        if (near) {
            m.t = near_x(c.u, sh);
            m.in = m.t >= lv.r.x0 && m.t < lv.r.x1;
            m.c = m.t - lv.ox;
        } else {
            m.t = bil_x(c.u, sh);
            int i = m.t >> 5;
            m.fx = m.t & 31;
            m.in = i >= lv.r.x0 && i + 1 < lv.r.x1;
            m.c = i - lv.ox;
        }
        sc.idx[x - o.x0] = m.c;
        if (m.in) {
            xa = std::min(xa, x);
            xb = std::max(xb, x + 1);
        }
    }
    if (a <= 0 || !step) {
        for (int y = o.y0; y < o.y1; y++)
            fill_row(dst.row(y) + o.x0, k, w);
        return out;
    }
    if (xb < xa)
        xa = xb = o.x0;
    MipAcc s = acc(map.row(o.y0, step)), inc = (MipAcc)step;
    auto lp = lerp_of(k, a);
    bool copy = a >= kMipOpaque<F>;
    const MipCol* cols = sc.cols.data();
    int pt = INT32_MIN;
    const T* pd = nullptr;
    if (near) {
        sc.runs.clear();
        for (int x = xa; x < xb; x++) {
            MipRun* b = sc.runs.empty() ? nullptr : &sc.runs.back();
            if (b && cols[x - o.x0].c == b->s + b->n)
                b->n++;
            else
                sc.runs.push_back({x, 1, cols[x - o.x0].c});
        }
        bool runs = sc.runs.size() * 4 <= (size_t)(xb - xa);
        for (int y = o.y0; y < o.y1; y++, s += inc) {
            T* d = dst.row(y);
            int j = near_a(s, sh);
            if (j < lv.r.y0 || j >= lv.r.y1) {
                fill_row(d + o.x0, k, w);
                pt = INT32_MIN;
                continue;
            }
            if (j == pt) {
                copy_px(d + o.x0, pd + o.x0, w);
                pd = d;
                continue;
            }
            pt = j;
            pd = d;
            const T* p = lv.v + (size_t)(j - lv.oy) * lv.stride;
            fill_row(d + o.x0, k, xa - o.x0);
            fill_row(d + xb, k, o.x1 - xb);
            if (runs) {
                for (const MipRun& r : sc.runs) {
                    if (copy) {
                        copy_px(d + r.x, p + r.s, r.n);
                        continue;
                    }
                    const T* q = p + r.s;
                    T* e = d + r.x;
                    pairs(0, r.n, [&](int i0, int i1) {
                        T v0 = q[i0], v1 = i1 >= 0 ? q[i1] : 0;
                        e[i0] = lp(v0);
                        if (i1 >= 0)
                            e[i1] = lp(v1);
                    });
                }
            } else {
                const int* ci = sc.idx.data();
                pairs(xa, xb, [&](int x0, int x1) {
                    T v0 = p[ci[x0 - o.x0]], v1 = x1 >= 0 ? p[ci[x1 - o.x0]] : 0;
                    d[x0] = copy ? v0 : lp(v0);
                    if (x1 >= 0)
                        d[x1] = copy ? v1 : lp(v1);
                });
            }
        }
        return out;
    }
    for (int y = o.y0; y < o.y1; y++, s += inc) {
        T* d = dst.row(y);
        int ty = bil_a(s, sh), j = ty >> 5, fy = ty & 31;
        if (ty == pt) {
            copy_px(d + o.x0, pd + o.x0, w);
            pd = d;
            continue;
        }
        pt = ty;
        pd = d;
        bool rows = j >= lv.r.y0 && j + 1 < lv.r.y1;
        int fa = rows ? xa : o.x1, fb = rows ? xb : o.x1;
        for (int x = o.x0; x < fa; x++) {
            T v = cols[x - o.x0].live ? tap_bil(lv, cols[x - o.x0].t, ty).v : k;
            d[x] = copy ? v : lp(v);
        }
        if (rows) {
            const T* p0 = lv.v + (size_t)(j - lv.oy) * lv.stride;
            const T* p1 = p0 + lv.stride;
            for (int x = fa; x < fb; x++) {
                T v = bil_rows(p0, p1, cols[x - o.x0].c, cols[x - o.x0].fx, fy);
                d[x] = copy ? v : lp(v);
            }
        }
        for (int x = fb; x < o.x1; x++) {
            T v = cols[x - o.x0].live ? tap_bil(lv, cols[x - o.x0].t, ty).v : k;
            d[x] = copy ? v : lp(v);
        }
    }
    return out;
}

template <class F, class L> void mip_ground(const SurfaceT<F, L>& dst, Rect keep, uint32_t ground)
{
    static_assert(std::is_same_v<L, Rows>, "mip levels are rows");
    typename F::T k = ground_px<F>(ground);
    keep = keep.intersect(dst.rect());
    for (int y = 0; y < dst.h; y++) {
        typename F::T* d = dst.row(y);
        if (keep.empty() || y < keep.y0 || y >= keep.y1) {
            fill_row(d, k, dst.w);
            continue;
        }
        fill_row(d, k, keep.x0);
        fill_row(d + keep.x1, k, dst.w - keep.x1);
    }
}

template <class F, class L>
int mip_menu(const MipLayerT<MipT<F>>& l, const SurfaceT<F, L>& dst, const Projection& pr, int a, MipForm form,
             int level, MipScratch& sc, Rect busy, Rect clip)
{
    static_assert(std::is_same_v<L, Rows>, "mip levels are rows");
    using T = typename F::T;
    if (a <= 0 || pr.scale < kMipMinScale)
        return -1;
    Rect o = projected_bounds(l.l0.src, pr).intersect(dst.rect()).intersect(clip);
    if (o.empty())
        return -1;
    const MipViewT<F>& lv = view_for(l, level);
    if (!lv.v) {
        tint_menu(lv, dst, pr, a, form, o, sc);
        return used(l, level);
    }
    T k = lv.k;
    int w = o.x1 - o.x0, sh = lv.shift;
    bool near = form == MipForm::Nearest;
    Map map(pr);
    sc.cols.resize((size_t)w);
    for (int x = o.x0; x < o.x1; x++) {
        Col c = map.col(x);
        MipCol& m = sc.cols[x - o.x0];
        m.live = false;
        if (!c.live)
            continue;
        m.t = near ? near_x(c.u, sh) : bil_x(c.u, sh);
        int i0 = near ? m.t : m.t >> 5, i1 = near ? m.t : i0 + 1;
        if (i1 < lv.r.x0 || i0 >= lv.r.x1)
            continue;
        m.c = i0 - lv.ox;
        m.fx = m.t & 31;
        m.in = i0 >= lv.r.x0 && i1 < lv.r.x1;
        m.bx0 = m.c >> kMipBlockBits;
        m.bx1 = (m.c + 1) >> kMipBlockBits;
        m.a = acc(map.row(o.y0, c.step));
        m.inc = (MipAcc)c.step;
        m.live = true;
    }
    auto lp = lerp_of(k, a);
    auto over = [&](T& d, T v, int m) {
        if (d == k)
            d = lp(v);
        else if (m || v != k) {
            if constexpr (std::is_same_v<F, R565>)
                d = over_ground5(d, v, m, k, a);
            else
                d = over_ground256(d, v, m, k, a);
        }
    };
    const MipCol* cols = sc.cols.data();
    int ry0 = lv.r.y0, ry1 = lv.r.y1;
    int la = o.x1, lb = o.x0;
    for (int x = o.x0; x < o.x1; x++)
        if (cols[x - o.x0].live) {
            la = std::min(la, x);
            lb = x + 1;
        }
    auto slow = [&](T* d, int x, const MipCol& c, int ty) {
        int i = c.t >> 5, j = ty >> 5;
        uint8_t g00 = flag(lv, i, j), g11 = flag(lv, i + 1, j + 1);
        uint8_t g10 = flag(lv, i + 1, j), g01 = flag(lv, i, j + 1);
        if ((g00 | g11 | g10 | g01) == kMipEmpty)
            return;
        TapT<F> t = tap_bil(lv, c.t, ty);
        over(d[x], t.v, g00 == kMipFull && g11 == kMipFull && g10 == kMipFull && g01 == kMipFull ? 255 : t.m);
    };
    auto empty_span = [&](const uint8_t* f0, const uint8_t* f1, int b0, int b1) {
        if (!lv.blocks)
            return false;
        for (int b = b0; b <= b1; b++)
            if (block_at(lv, f0, b) != kMipEmpty || (f1 && block_at(lv, f1, b) != kMipEmpty))
                return false;
        return true;
    };
    bool copy = a >= kMipOpaque<F>;
    auto full_span = [&](const uint8_t* f0, const uint8_t* f1, int b0, int b1) {
        if (!copy || !lv.blocks)
            return false;
        for (int b = b0; b <= b1; b++)
            if (block_at(lv, f0, b) != kMipFull || (f1 && block_at(lv, f1, b) != kMipFull))
                return false;
        return true;
    };
    auto ground = [&](T* d, int x, int xe, int t0, int t1, auto& tap_t) {
        if (near) {
            if (t0 == t1) {
                if (t0 < ry0 || t0 >= ry1)
                    return;
                const T* p = lv.v + (size_t)(t0 - lv.oy) * lv.stride;
                if (empty_span(block_row(lv, t0), nullptr, cols[x - o.x0].bx0, cols[xe - 1 - o.x0].bx0))
                    return;
                if (copy)
                    for (int xx = x; xx < xe; xx++)
                        d[xx] = p[cols[xx - o.x0].c];
                else
                    for (int xx = x; xx < xe; xx++)
                        d[xx] = lp(p[cols[xx - o.x0].c]);
                return;
            }
            for (int xx = x; xx < xe; xx++) {
                int j = tap_t(xx);
                if (j < ry0 || j >= ry1)
                    continue;
                T v = lv.v[(size_t)(j - lv.oy) * lv.stride + cols[xx - o.x0].c];
                d[xx] = copy ? v : lp(v);
            }
            return;
        }
        int j = t0 >> 5;
        bool one = j == t1 >> 5, inner = one && j >= ry0 && j + 1 < ry1;
        if (one && (j + 1 < ry0 || j >= ry1))
            return;
        const T* p0 = inner ? lv.v + (size_t)(j - lv.oy) * lv.stride : nullptr;
        if (inner && empty_span(block_row(lv, j), block_row(lv, j + 1), cols[x - o.x0].bx0, cols[xe - 1 - o.x0].bx1))
            return;
        for (int xx = x; xx < xe; xx++) {
            const MipCol& c = cols[xx - o.x0];
            int ty = t0 == t1 ? t0 : tap_t(xx), jj = ty >> 5;
            T v;
            if (inner && c.in)
                v = bil_rows(p0, p0 + lv.stride, c.c, c.fx, ty & 31);
            else if (!one && c.in && jj >= ry0 && jj + 1 < ry1) {
                const T* q = lv.v + (size_t)(jj - lv.oy) * lv.stride;
                v = bil_rows(q, q + lv.stride, c.c, c.fx, ty & 31);
            } else {
                if (jj + 1 < ry0 || jj >= ry1)
                    continue;
                v = tap_bil(lv, c.t, ty).v;
            }
            d[xx] = copy ? v : lp(v);
        }
    };
    for (int y = o.y0; y < o.y1; y++) {
        T* d = dst.row(y);
        int dy = y - o.y0;
        auto tap_t = [&](int x) {
            const MipCol& c = cols[x - o.x0];
            MipAcc s = c.a + c.inc * dy;
            return near ? near_a(s, sh) : bil_a(s, sh);
        };
        bool row_busy = y >= busy.y0 && y < busy.y1;
        for (int x = la; x < lb;) {
            int xe = std::min(x + 16, lb);
            if (row_busy && x < busy.x0 && xe > busy.x0)
                xe = busy.x0;
            else if (row_busy && x < busy.x1 && xe > busy.x1)
                xe = busy.x1;
            int t0 = tap_t(x), t1 = tap_t(xe - 1);
            if constexpr (std::is_same_v<F, X888>) {
                if (near && lv.blocks && empty_rows(lv, t0, t1, cols[x - o.x0].c, cols[xe - 1 - o.x0].c)) {
                    x = xe;
                    continue;
                }
            }
            int k0 = near ? t0 : t0 >> 5;
            if ((near ? t1 : t1 >> 5) != k0) {
                int lo = x + 1, hi = xe - 1;
                while (lo < hi) {
                    int m = (lo + hi) / 2, tm = tap_t(m);
                    if ((near ? tm : tm >> 5) == k0)
                        lo = m + 1;
                    else
                        hi = m;
                }
                xe = lo;
                t1 = tap_t(xe - 1);
            }
            if (!row_busy || xe <= busy.x0 || x >= busy.x1) {
                ground(d, x, xe, t0, t1, tap_t);
                x = xe;
                continue;
            }
            if (near) {
                auto pixel = [&](int xx, size_t row, const uint8_t* fr) {
                    const MipCol& c = cols[xx - o.x0];
                    uint8_t f = block_at(lv, fr, c.bx0);
                    if (f == kMipEmpty)
                        return;
                    T v = lv.v[row + c.c];
                    if (d[xx] == k)
                        d[xx] = lp(v);
                    else
                        over(d[xx], v, f == kMipFull ? 255 : lv.m ? lv.m[row + c.c] : v == k ? 0 : 255);
                };
                if (t0 == t1) {
                    if (t0 >= ry0 && t0 < ry1) {
                        size_t row = (size_t)(t0 - lv.oy) * lv.stride;
                        const uint8_t* fr = block_row(lv, t0);
                        int b0 = cols[x - o.x0].bx0, b1 = cols[xe - 1 - o.x0].bx0;
                        if (full_span(fr, nullptr, b0, b1))
                            for (int xx = x; xx < xe; xx++)
                                d[xx] = lv.v[row + cols[xx - o.x0].c];
                        else if (!empty_span(fr, nullptr, b0, b1))
                            for (int xx = x; xx < xe; xx++)
                                pixel(xx, row, fr);
                    }
                } else {
                    for (int xx = x; xx < xe; xx++) {
                        int j = tap_t(xx);
                        if (j >= ry0 && j < ry1)
                            pixel(xx, (size_t)(j - lv.oy) * lv.stride, block_row(lv, j));
                    }
                }
                x = xe;
                continue;
            }
            struct RowPtrs {
                const T *p0, *p1;
                const uint8_t *m0, *m1, *f0, *f1;
            };
            auto rows_of = [&](int j) {
                size_t row = (size_t)(j - lv.oy) * lv.stride;
                const uint8_t* m0 = lv.m ? lv.m + row : nullptr;
                return RowPtrs{lv.v + row, lv.v + row + lv.stride, m0, m0 ? m0 + lv.stride : nullptr, block_row(lv, j),
                            block_row(lv, j + 1)};
            };
            auto fast = [&](int xx, const MipCol& c, const RowPtrs& r, int ty) {
                uint8_t g00 = block_at(lv, r.f0, c.bx0), g10 = block_at(lv, r.f0, c.bx1);
                uint8_t g01 = block_at(lv, r.f1, c.bx0), g11 = block_at(lv, r.f1, c.bx1);
                if ((g00 | g11 | g10 | g01) == kMipEmpty)
                    return;
                int fy = ty & 31;
                T v = bil_rows(r.p0, r.p1, c.c, c.fx, fy);
                bool full = g00 == kMipFull && g11 == kMipFull && g10 == kMipFull && g01 == kMipFull;
                int m = full ? 255 : r.m0 ? bil_mrows(r.m0, r.m1, c.c, c.fx, fy) : tap_bil(lv, c.t, ty).m;
                over(d[xx], v, m);
            };
            int j = t0 >> 5;
            if (j == t1 >> 5) {
                if (j + 1 < ry0 || j >= ry1) {
                    x = xe;
                    continue;
                }
                bool edge = j < ry0 || j + 1 >= ry1;
                RowPtrs r = edge ? RowPtrs{} : rows_of(j);
                if (!edge && empty_span(r.f0, r.f1, cols[x - o.x0].bx0, cols[xe - 1 - o.x0].bx1)) {
                    x = xe;
                    continue;
                }
                if (!edge && t0 == t1 && full_span(r.f0, r.f1, cols[x - o.x0].bx0, cols[xe - 1 - o.x0].bx1)) {
                    for (int xx = x; xx < xe; xx++) {
                        const MipCol& c = cols[xx - o.x0];
                        if (c.in)
                            d[xx] = bil_rows(r.p0, r.p1, c.c, c.fx, t0 & 31);
                        else
                            slow(d, xx, c, t0);
                    }
                    x = xe;
                    continue;
                }
                for (int xx = x; xx < xe; xx++) {
                    const MipCol& c = cols[xx - o.x0];
                    int ty = t0 == t1 ? t0 : tap_t(xx);
                    if (edge || !c.in)
                        slow(d, xx, c, ty);
                    else
                        fast(xx, c, r, ty);
                }
                x = xe;
                continue;
            }
            for (int xx = x; xx < xe; xx++) {
                const MipCol& c = cols[xx - o.x0];
                int ty = tap_t(xx), jj = ty >> 5;
                if (jj + 1 < ry0 || jj >= ry1)
                    continue;
                if (!c.in || jj < ry0 || jj + 1 >= ry1)
                    slow(d, xx, c, ty);
                else
                    fast(xx, c, rows_of(jj), ty);
            }
            x = xe;
        }
    }
    return used(l, level);
}

template <class F, class L>
void mip_pan(const MipLayerT<MipT<F>>& qp, const MipLayerT<MipT<F>>& menu, const SurfaceT<F, L>& dst,
             const Projection& pq, int qa, MipForm qf, int ql, const Projection& pm, int ma, MipForm mf, int ml,
             uint32_t ground, MipScratch& sc, const MipLayerT<MipT<F>>& ring, int ra)
{
    static_assert(std::is_same_v<L, Rows>, "mip levels are rows");
    Rect qb = pq.scale >= kMipMinScale ? projected_bounds(qp.l0.src, pq).intersect(dst.rect()) : Rect{};
    Rect mb = pm.scale >= kMipMinScale && ma > 0 ? projected_bounds(menu.l0.src, pm) : Rect{};
    Rect rb = pm.scale >= kMipMinScale && ra > 0 && ring.l0.v ? projected_bounds(ring.l0.src, pm).intersect(dst.rect())
                                                              : Rect{};
    Rect over = mb.unite(rb).intersect(qb);
    if (over.empty())
        mip_qp(qp, dst, pq, qa, qf, ql, sc);
    else
        for (Rect r : {Rect{qb.x0, qb.y0, over.x0, qb.y1}, Rect{over.x1, qb.y0, qb.x1, qb.y1},
                       Rect{over.x0, qb.y0, over.x1, over.y0}, Rect{over.x0, over.y1, over.x1, qb.y1}})
            if (!r.empty())
                mip_qp(qp, dst, pq, qa, qf, ql, sc, r);
    typename F::T k = ground_px<F>(ground);
    mip_ground(dst, qb, ground);
    for (int y = over.y0; y < over.y1; y++)
        fill_row(dst.row(y) + over.x0, k, over.x1 - over.x0);
    mip_menu(menu, dst, pm, ma, mf, ml, sc, Rect{});
    if (!rb.empty())
        mip_menu(ring, dst, pm, ra, mf, 0, sc, rb);
    if (!over.empty())
        mip_menu(qp, dst, pq, qa, qf, ql, sc, over, over);
}

template <class F, class L>
int mip_sprite(const MipLayerT<MipT<F>>& l, const Projection& pr, MipForm form, int level, MipScratch& sc,
               SpriteT<F, L>& out)
{
    static_assert(std::is_same_v<L, Rows>, "mip levels are rows");
    const MipViewT<F>& lv = view_for(l, level);
    out.r = pr.scale >= kMipMinScale && lv.v ? projected_bounds(l.l0.src, pr).intersect(kScreen) : Rect{};
    int w = out.r.x1 - out.r.x0, h = out.r.y1 - out.r.y0;
    out.px.assign(out.r.empty() ? 0 : (size_t)w * h, 0);
    if (out.r.empty())
        return -1;
    int sh = lv.shift;
    bool near = form == MipForm::Nearest, coverage = lv.m != nullptr;
    Map map(pr);
    sc.cols.resize((size_t)w);
    for (int x = out.r.x0; x < out.r.x1; x++) {
        Col c = map.col(x);
        MipCol& m = sc.cols[x - out.r.x0];
        m.live = c.live;
        if (!c.live)
            continue;
        m.t = near ? near_x(c.u, sh) : bil_x(c.u, sh);
        int i0 = near ? m.t : m.t >> 5;
        m.c = i0 - lv.ox;
        m.fx = m.t & 31;
        m.in = i0 >= lv.r.x0 && (near ? i0 : i0 + 1) < lv.r.x1;
        m.a = acc(map.row(out.r.y0, c.step));
        m.inc = (MipAcc)c.step;
    }
    MipCol* cols = sc.cols.data();
    for (int y = out.r.y0; y < out.r.y1; y++) {
        uint32_t* row = out.px.data() + (size_t)(y - out.r.y0) * w;
        for (int x = out.r.x0; x < out.r.x1; x++) {
            MipCol& c = cols[x - out.r.x0];
            if (!c.live)
                continue;
            MipAcc s = c.a;
            c.a += c.inc;
            TapT<F> t;
            if (near) {
                int j = near_a(s, sh);
                if (c.in && j >= lv.r.y0 && j < lv.r.y1) {
                    size_t at = (size_t)(j - lv.oy) * lv.stride + c.c;
                    typename F::T v = lv.v[at];
                    t = {v, lv.m ? lv.m[at] : v == lv.k ? 0 : 255};
                } else
                    t = texel(lv, c.t, j);
            } else {
                int ty = bil_a(s, sh), j = ty >> 5;
                if (c.in && j >= lv.r.y0 && j + 1 < lv.r.y1) {
                    size_t at = (size_t)(j - lv.oy) * lv.stride;
                    t = {bil_rows(lv.v + at, lv.v + at + lv.stride, c.c, c.fx, ty & 31),
                         lv.m ? bil_mrows(lv.m + at, lv.m + at + lv.stride, c.c, c.fx, ty & 31) : 0};
                } else
                    t = tap_bil(lv, c.t, ty);
            }
            row[x - out.r.x0] = sprite_px(t, lv.k, coverage);
        }
    }
    return used(l, level);
}

using S16 = SurfaceT<R565, Rows>;
using L16 = MipLayerT<R565>;

template void mip_build<R565, Rows>(const S16&, const uint8_t*, Rect, uint32_t, MipLevelT<R565>&);
template void mip_update<R565, Rows>(const S16&, const uint8_t*, Rect, MipLevelT<R565>&);
template void mip_blocks<R565, Rows>(const S16&, const uint8_t*, Rect, uint32_t, Rect, MipBlocks&);
template MipViewT<R565> mip_view<R565, Rows>(const S16&, const uint8_t*, Rect, uint32_t, const MipBlocks*);
template MipViewT<R565> mip_view<R565, Rows>(const S16&, const uint8_t*, int, int, uint32_t);
template MipDraw mip_qp<R565, Rows>(const L16&, const S16&, const Projection&, int, MipForm, int, MipScratch&, Rect);
template void mip_ground<R565, Rows>(const S16&, Rect, uint32_t);
template int mip_menu<R565, Rows>(const L16&, const S16&, const Projection&, int, MipForm, int, MipScratch&, Rect,
                                  Rect);
template void mip_pan<R565, Rows>(const L16&, const L16&, const S16&, const Projection&, int, MipForm, int,
                                  const Projection&, int, MipForm, int, uint32_t, MipScratch&, const L16&, int);
template int mip_sprite<R565, Rows>(const L16&, const Projection&, MipForm, int, MipScratch&, SpriteT<R565, Rows>&);
template MipViewT<R565> mip_cover<R565>(const uint8_t*, size_t, Rect, uint32_t, const MipBlocks*);

#ifdef FD_MIP_X888
using S32 = SurfaceT<X888, Rows>;
using L32 = MipLayerT<X888>;

template void mip_build<X888, Rows>(const S32&, const uint8_t*, Rect, uint32_t, MipLevelT<X888>&);
template void mip_update<X888, Rows>(const S32&, const uint8_t*, Rect, MipLevelT<X888>&);
template void mip_blocks<X888, Rows>(const S32&, const uint8_t*, Rect, uint32_t, Rect, MipBlocks&);
template MipViewT<X888> mip_view<X888, Rows>(const S32&, const uint8_t*, Rect, uint32_t, const MipBlocks*);
template MipViewT<X888> mip_view<X888, Rows>(const S32&, const uint8_t*, int, int, uint32_t);
template MipDraw mip_qp<X888, Rows>(const L32&, const S32&, const Projection&, int, MipForm, int, MipScratch&, Rect);
template void mip_ground<X888, Rows>(const S32&, Rect, uint32_t);
template int mip_menu<X888, Rows>(const L32&, const S32&, const Projection&, int, MipForm, int, MipScratch&, Rect,
                                  Rect);
template void mip_pan<X888, Rows>(const L32&, const L32&, const S32&, const Projection&, int, MipForm, int,
                                  const Projection&, int, MipForm, int, uint32_t, MipScratch&, const L32&, int);
template int mip_sprite<X888, Rows>(const L32&, const Projection&, MipForm, int, MipScratch&, SpriteT<X888, Rows>&);
template MipViewT<X888> mip_cover<X888>(const uint8_t*, size_t, Rect, uint32_t, const MipBlocks*);
#endif

}
