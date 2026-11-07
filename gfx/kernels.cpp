#include "kernels.h"

#include <algorithm>
#include <type_traits>
#include <vector>

#include "../core/curves.h"

#if defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace fd::kern {

namespace {

inline uint32_t blend1(uint32_t d, uint32_t s, uint32_t a)
{
    uint32_t rb = d & 0xFF00FF, g = d & 0xFF00;
    rb = (rb + ((((s & 0xFF00FF) - rb) * a) >> 8)) & 0xFF00FF;
    g = (g + ((((s & 0xFF00) - g) * a) >> 8)) & 0xFF00;
    return rb | g;
}

#if defined(__aarch64__)
inline uint32x4_t blend4(uint32x4_t d, uint32x4_t s, uint32_t a)
{
    const uint32x4_t mrb = vdupq_n_u32(0xFF00FF), mg = vdupq_n_u32(0xFF00);
    uint32x4_t rb = vandq_u32(d, mrb), g = vandq_u32(d, mg);
    uint32x4_t drb = vmulq_n_u32(vsubq_u32(vandq_u32(s, mrb), rb), a);
    uint32x4_t dg = vmulq_n_u32(vsubq_u32(vandq_u32(s, mg), g), a);
    rb = vandq_u32(vaddq_u32(rb, vshrq_n_u32(drb, 8)), mrb);
    g = vandq_u32(vaddq_u32(g, vshrq_n_u32(dg, 8)), mg);
    return vorrq_u32(rb, g);
}
#endif

struct Recip {
    uint32_t m[17];
};

constexpr Recip make_recip()
{
    Recip r{};
    for (int k = 1; k <= 16; k++)
        r.m[k] = (uint32_t)((65536 + k - 1) / k);
    return r;
}

constexpr Recip kRecip = make_recip();

inline uint32_t div_k(uint32_t v, int k) { return (v * kRecip.m[k]) >> 16; }

template <class L> uint32_t* col_of(const SurfaceT<X888, L>& p, int x, int& step)
{
    if constexpr (std::is_same_v<L, Rows>) {
        step = p.stride;
        return p.px + x;
    } else {
        step = 1;
        return p.px + (size_t)(p.w - 1 - x) * p.stride;
    }
}

template <class L> Rect mem_of(const SurfaceT<X888, L>& p, Rect r)
{
    if constexpr (std::is_same_v<L, Rows>)
        return r.intersect(p.rect());
    else
        return p.mem_rect(r);
}

struct Cols {
    int n = 0, total = 1;
    int64_t sub[4]{};
    int cov[17]{};
    fx c = 0, sn = 0;
    int64_t inv = 0;
};

struct Col {
    int live = 0;
    int sx[4];
    int64_t step[4];
};

Col setup_col(int x, Rect s, const Projection& pr, const Cols& k)
{
    Col o;
    for (int i = 0; i < k.n; i++) {
        int64_t xl = ((((int64_t)x << kFxBits) + k.sub[i] - pr.x) * k.inv) >> kFxBits;
        int64_t xp = xl - ((int64_t)pr.hinge << kFxBits);
        int64_t den = k.c - ((xp * k.sn) >> kFxBits) / pr.d;
        if (den <= 0)
            continue;
        int v = pr.hinge + (int)((xp * kOne / den) >> kFxBits);
        if (v < s.x0 || v >= s.x1)
            continue;
        o.sx[o.live] = v;
        o.step[o.live] = ((((int64_t)k.c << kFxBits) / den) * k.inv) >> kFxBits;
        o.live++;
    }
    return o;
}

inline void put(uint32_t& o, uint32_t rb, uint32_t g, int cnt, int a, const Cols& k)
{
    uint32_t v = div_k(rb >> 16, cnt) << 16 | div_k(g >> 8, cnt) << 8 | div_k(rb & 0xFFFF, cnt);
    int aa = a * k.cov[cnt] >> 8;
    o = aa >= 256 ? v : blend1(o, v, (uint32_t)aa);
}

template <int N, class L>
void project_cols(const SurfaceT<X888, L>& src, Rect s, const SurfaceT<X888, L>& dst, Rect d, const Projection& pr,
                  const Cols& k, uint32_t bg, int a)
{
    const unsigned sh = (unsigned)(s.y1 - s.y0);
    for (int x = d.x0; x < d.x1; x++) {
        Col cl = setup_col(x, s, pr, k);
        if (!cl.live)
            continue;
        const uint32_t* col[N];
        int cstep = 1;
        int64_t acc[N][N], inc[N];
        for (int i = 0; i < N; i++) {
            bool on = i < cl.live;
            col[i] = col_of(src, cl.sx[on ? i : 0], cstep);
            inc[i] = on ? cl.step[i] << kFxBits : 0;
            for (int j = 0; j < N; j++)
                acc[j][i] = on ? (((int64_t)d.y0 << kFxBits) + k.sub[j] - pr.y) * cl.step[i]
                               : (int64_t)(s.y0 - 1) * ((int64_t)1 << (2 * kFxBits));
        }
        int ostep;
        uint32_t* out = col_of(dst, x, ostep);
        for (int y = d.y0; y < d.y1; y++) {
            uint32_t rb = 0, g = 0;
            int cnt = 0;
            for (int j = 0; j < N; j++)
                for (int i = 0; i < N; i++) {
                    int sy = (int)(acc[j][i] >> (2 * kFxBits));
                    acc[j][i] += inc[i];
                    if ((unsigned)sy - (unsigned)s.y0 >= sh)
                        continue;
                    uint32_t v = col[i][(size_t)sy * cstep];
                    if (v == bg)
                        continue;
                    rb += v & 0xFF00FF;
                    g += v & 0xFF00;
                    cnt++;
                }
            if (cnt)
                put(out[(size_t)y * ostep], rb, g, cnt, a, k);
        }
    }
}

void project_rows(const SurfaceT<X888, Rows>& src, Rect s, const SurfaceT<X888, Rows>& dst, Rect d,
                  const Projection& pr, const Cols& k, const std::vector<Col>& cols, uint32_t bg, int a)
{
    const int n = k.n, w = d.x1 - d.x0;
    const unsigned sh = (unsigned)(s.y1 - s.y0);
    int sstep;
    const uint32_t* base = col_of(src, 0, sstep);
    for (int y = d.y0; y < d.y1; y++) {
        int64_t Y[4];
        for (int j = 0; j < n; j++)
            Y[j] = ((int64_t)y << kFxBits) + k.sub[j] - pr.y;
        uint32_t* out = dst.px + (size_t)y * dst.stride;
        for (int x = 0; x < w; x++) {
            const Col& cl = cols[(size_t)x];
            uint32_t rb = 0, g = 0;
            int cnt = 0;
            for (int j = 0; j < n; j++)
                for (int i = 0; i < cl.live; i++) {
                    int sy = (int)((Y[j] * cl.step[i]) >> (2 * kFxBits));
                    if ((unsigned)sy - (unsigned)s.y0 >= sh)
                        continue;
                    uint32_t v = base[(size_t)sy * sstep + cl.sx[i]];
                    if (v == bg)
                        continue;
                    rb += v & 0xFF00FF;
                    g += v & 0xFF00;
                    cnt++;
                }
            if (cnt)
                put(out[d.x0 + x], rb, g, cnt, a, k);
        }
    }
}

template <class L, class F> void each_row(const SurfaceT<X888, L>& p, Rect portrait, F f)
{
    Rect m = mem_of(p, portrait);
    for (int r = m.y0; r < m.y1; r++)
        f(p.px + (size_t)r * p.stride + m.x0, m.x1 - m.x0);
}

}

void fill_span(uint32_t* d, uint32_t v, int n)
{
    int i = 0;
#if defined(__aarch64__)
    uint32x4_t q = vdupq_n_u32(v);
    for (; i + 16 <= n; i += 16) {
        vst1q_u32(d + i, q);
        vst1q_u32(d + i + 4, q);
        vst1q_u32(d + i + 8, q);
        vst1q_u32(d + i + 12, q);
    }
    for (; i + 4 <= n; i += 4)
        vst1q_u32(d + i, q);
#endif
    for (; i < n; i++)
        d[i] = v;
}

void blend_span(uint32_t* d, uint32_t v, int a, int n)
{
    int i = 0;
#if defined(__aarch64__)
    uint32x4_t q = vdupq_n_u32(v);
    for (; i + 4 <= n; i += 4)
        vst1q_u32(d + i, blend4(vld1q_u32(d + i), q, (uint32_t)a));
#endif
    for (; i < n; i++)
        d[i] = blend1(d[i], v, (uint32_t)a);
}

void mix_span(uint32_t* d, const uint32_t* s, int a, int n)
{
    int i = 0;
#if defined(__aarch64__)
    for (; i + 4 <= n; i += 4)
        vst1q_u32(d + i, blend4(vld1q_u32(d + i), vld1q_u32(s + i), (uint32_t)a));
#endif
    for (; i < n; i++)
        d[i] = blend1(d[i], s[i], (uint32_t)a);
}

template <class L> void fill(const SurfaceT<X888, L>& p, Rect portrait, uint32_t v)
{
    each_row(p, portrait, [&](uint32_t* d, int n) { fill_span(d, v, n); });
}

template <class L> void blend(const SurfaceT<X888, L>& p, Rect portrait, uint32_t v, int a)
{
    if (a <= 0)
        return;
    if (a >= 256) {
        fill(p, portrait, v);
        return;
    }
    each_row(p, portrait, [&](uint32_t* d, int n) { blend_span(d, v, a, n); });
}

template <class L>
void project(const SurfaceT<X888, L>& src, Rect s, const SurfaceT<X888, L>& dst, Rect d, const Projection& pr,
             uint32_t bg, int a)
{
    d = d.intersect(dst.rect());
    if (a <= 0 || d.empty() || pr.scale <= 0)
        return;
    Cols k;
    k.n = std::clamp((int)((kOne + pr.scale - 1) / pr.scale), 1, 4);
    k.total = k.n * k.n;
    for (int i = 0; i < k.n; i++)
        k.sub[i] = ((int64_t)(2 * i + 1) << kFxBits) / (2 * k.n);
    for (int i = 0; i <= 16; i++)
        k.cov[i] = i * 256 / k.total;
    k.c = cos_deg(pr.angle);
    k.sn = sin_deg(pr.angle);
    k.inv = ((int64_t)kOne << kFxBits) / pr.scale;
    if constexpr (std::is_same_v<L, Rot>) {
        switch (k.n) {
        case 1:
            project_cols<1>(src, s, dst, d, pr, k, bg, a);
            break;
        case 2:
            project_cols<2>(src, s, dst, d, pr, k, bg, a);
            break;
        case 3:
            project_cols<3>(src, s, dst, d, pr, k, bg, a);
            break;
        default:
            project_cols<4>(src, s, dst, d, pr, k, bg, a);
        }
    } else {
        std::vector<Col> cols((size_t)(d.x1 - d.x0));
        for (int x = d.x0; x < d.x1; x++)
            cols[(size_t)(x - d.x0)] = setup_col(x, s, pr, k);
        project_rows(src, s, dst, d, pr, k, cols, bg, a);
    }
}

template void fill(const SurfaceT<X888, Rot>&, Rect, uint32_t);
template void fill(const SurfaceT<X888, Rows>&, Rect, uint32_t);
template void blend(const SurfaceT<X888, Rot>&, Rect, uint32_t, int);
template void blend(const SurfaceT<X888, Rows>&, Rect, uint32_t, int);
template void project(const SurfaceT<X888, Rot>&, Rect, const SurfaceT<X888, Rot>&, Rect, const Projection&, uint32_t,
                      int);
template void project(const SurfaceT<X888, Rows>&, Rect, const SurfaceT<X888, Rows>&, Rect, const Projection&, uint32_t,
                      int);

}
