#include "compose.h"

#include <algorithm>
#include <cmath>
#include <type_traits>
#include <vector>

#include "../core/prof.h"
#include "pairs.h"
#include "xform.h"

namespace fd {

namespace {

struct Taps {
    int stack[kMaxDim];
    std::vector<int> heap;
    int* p = stack;

    explicit Taps(int n)
    {
        if (n > kMaxDim) {
            heap.resize((size_t)n);
            p = heap.data();
        }
    }
    int& operator[](int i) { return p[i]; }
};

int unscale(int v, int c, int64_t inv) { return c + (int)(((int64_t)(v - c) * inv + kHalf) >> kFxBits); }

int signed_div255(int v) { return v >= 0 ? div255(v) : -div255(-v); }

uint32_t premul888(uint32_t d, uint32_t v, int al, int a)
{
    int k = (al + 1) * a >> 8;
    uint32_t out = 0;
    for (int sh = 0; sh < 24; sh += 8) {
        int c = (int)(d >> sh & 0xFF) - ((int)(d >> sh & 0xFF) * k >> 8) + ((int)(v >> sh & 0xFF) * a >> 8);
        out |= (uint32_t)(c > 255 ? 255 : c) << sh;
    }
    return out;
}

uint16_t over_ground(uint16_t d, uint16_t v, int m, uint32_t g, int a)
{
    if (m >= 255)
        return a >= 256 ? v : blend565(d, v, a);
    uint32_t d8 = unpack565(d), v8 = unpack565(v), out = 0;
    for (int sh = 0; sh < 24; sh += 8) {
        int dc = (int)(d8 >> sh & 0xFF);
        int q = std::clamp((int)(v8 >> sh & 0xFF) + signed_div255((dc - (int)(g >> sh & 0xFF)) * (255 - m)), 0, 255);
        if (a < 256)
            q = (q * a + dc * (256 - a) + 128) >> 8;
        out |= (uint32_t)q << sh;
    }
    return pack565(out);
}

template <class F, class L>
void coverage_impl(const SurfaceT<F, L>& layer, const uint8_t* mask, const SurfaceT<F, L>& dst, Rect p, fx scale,
                   int cx, int cy, uint32_t color, int a, bool motion)
{
    p = p.intersect(layer.rect());
    if (p.empty() || a <= 0 || scale <= 0)
        return;
    auto fwd = [&](int v, int c) { return c + (int)(((int64_t)(v - c) * scale) >> kFxBits); };
    Rect d{fwd(p.x0, cx) - 1, fwd(p.y0, cy) - 1, fwd(p.x1, cx) + 2, fwd(p.y1, cy) + 2};
    d = d.intersect(dst.rect());
    int64_t inv = ((int64_t)kOne << kFxBits) / scale;
    if constexpr (std::is_same_v<L, Rot>) {
        Taps src_col(dst.h);
        for (int y = d.y0; y < d.y1; y++) {
            int sy = cy + (int)(((int64_t)(y - cy) * inv + kHalf) >> kFxBits);
            src_col[y] = sy >= p.y0 && sy < p.y1 ? sy : -1;
        }
        for (int x = d.x0; x < d.x1; x++) {
            int sx = cx + (int)(((int64_t)(x - cx) * inv + kHalf) >> kFxBits);
            if (sx < p.x0 || sx >= p.x1)
                continue;
            const uint32_t* s = layer.mem_row(layer.w - 1 - sx);
            uint32_t* row = dst.mem_row(dst.w - 1 - x);
            for (int y = d.y0; y < d.y1; y++) {
                int sc = src_col[y];
                if (sc < 0)
                    continue;
                int cov = s[sc] >> 8 & 0xFF;
                if (!cov)
                    continue;
                int aa = (cov + 1) * a >> 8;
                row[y] = blend_px(row[y], color, aa);
            }
        }
    } else {
        using T = typename F::T;
        T v = F::pack(color);
        Taps src_x(dst.w);
        for (int x = d.x0; x < d.x1; x++) {
            int sx = unscale(x, cx, inv);
            src_x[x] = sx >= p.x0 && sx < p.x1 ? sx : -1;
        }
        for (int y = d.y0; y < d.y1; y++) {
            int sy = unscale(y, cy, inv);
            if (sy < p.y0 || sy >= p.y1)
                continue;
            const T* s = layer.row(sy);
            const uint8_t* m = mask ? mask + layer.index(0, sy) : nullptr;
            T* row = dst.row(y);
            for (int x = d.x0; x < d.x1; x++) {
                int sc = src_x[x];
                if (sc < 0)
                    continue;
                int cov = m ? m[sc] : (int)(F::unpack(s[sc]) >> 8 & 0xFF);
                if (!cov)
                    continue;
                int aa = (cov + 1) * a >> 8;
                if constexpr (std::is_same_v<F, R565>)
                    row[x] = motion ? lerp565(row[x], v, alpha5(aa)) : blend565(row[x], v, aa);
                else
                    row[x] = F::blend(row[x], v, aa);
            }
        }
    }
}

template <class F, class L>
void blend_scaled_impl(const SurfaceT<F, L>& layer, const uint8_t* mask, const SurfaceT<F, L>& dst, fx scale, int cx,
                       int cy, uint32_t bg, int a, bool motion)
{
    if (a <= 0 || scale <= 0)
        return;
    int64_t inv = ((int64_t)kOne << kFxBits) / scale;
    if constexpr (std::is_same_v<L, Rot>) {
        Taps src_col(dst.h);
        for (int y = 0; y < dst.h; y++) {
            int sy = cy + (int)(((int64_t)(y - cy) * inv + kHalf) >> kFxBits);
            src_col[y] = sy >= 0 && sy < layer.h ? sy : -1;
        }
        for (int x = 0; x < dst.w; x++) {
            int sx = cx + (int)(((int64_t)(x - cx) * inv + kHalf) >> kFxBits);
            if (sx < 0 || sx >= layer.w)
                continue;
            const uint32_t* s = layer.mem_row(layer.w - 1 - sx);
            uint32_t* row = dst.mem_row(dst.w - 1 - x);
            for (int y = 0; y < dst.h; y++) {
                int sc = src_col[y];
                if (sc < 0)
                    continue;
                uint32_t v = s[sc];
                if (v == bg)
                    continue;
                row[y] = a >= 256 ? v : blend_px(row[y], v, a);
            }
        }
    } else {
        using T = typename F::T;
        uint32_t g = F::unpack(F::pack(bg));
        T key = F::pack(bg);
        Taps src_x(dst.w);
        for (int x = 0; x < dst.w; x++) {
            int sx = unscale(x, cx, inv);
            src_x[x] = sx >= 0 && sx < layer.w ? sx : -1;
        }
        for (int y = 0; y < dst.h; y++) {
            int sy = unscale(y, cy, inv);
            if (sy < 0 || sy >= layer.h)
                continue;
            const T* s = layer.row(sy);
            const uint8_t* m = mask ? mask + layer.index(0, sy) : nullptr;
            T* row = dst.row(y);
            for (int x = 0; x < dst.w; x++) {
                int sc = src_x[x];
                if (sc < 0)
                    continue;
                T v = s[sc];
                if (m) {
                    if (m[sc]) {
                        if constexpr (std::is_same_v<F, R565>)
                            row[x] = motion && a < 256 ? over_ground5(row[x], v, m[sc], key, alpha5(a))
                                                       : over_ground(row[x], v, m[sc], g, a);
                        else
                            row[x] = over_ground256(row[x], v, m[sc], key, a);
                    }
                    continue;
                }
                if (v == key)
                    continue;
                row[x] = a >= 256 ? v : F::blend(row[x], v, a);
            }
        }
    }
}

int64_t floor_div(int64_t a, int64_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

struct NearTap {
    int i;
    int64_t r, s;

    NearTap(int v, fx t, fx scale) : s(scale)
    {
        int64_t n = ((int64_t)v << kFxBits) + kHalf - t;
        i = (int)floor_div(n, s);
        r = n - (int64_t)i * s;
    }
    void next()
    {
        for (r += kOne; r >= s; r -= s)
            i++;
    }
};

int rect_taps(Taps& taps, int c0, int c1, int s0, int s1, fx t, fx scale, int& lo)
{
    lo = c1;
    int hi = c1;
    NearTap n(c0, t, scale);
    for (int v = c0; v < c1 && n.i < s1; v++, n.next())
        if (n.i >= s0) {
            if (lo == c1)
                lo = v;
            taps[v] = n.i;
            hi = v + 1;
        }
    return hi;
}

template <class F, class L>
void rect_impl(const SurfaceT<F, L>& layer, const uint8_t* mask, Rect s, const SurfaceT<F, L>& dst, Rect c, fx scale,
               fx tx, fx ty, uint32_t ground, int a, bool motion)
{
    using T = typename F::T;
    s = s.intersect(layer.rect());
    c = c.intersect(dst.rect());
    if (s.empty() || c.empty() || a <= 0 || scale <= 0)
        return;
    a = std::min(a, 256);
    Taps sx(dst.w), sy(dst.h);
    int xa = 0, ya = 0;
    int xb = rect_taps(sx, c.x0, c.x1, s.x0, s.x1, tx, scale, xa);
    int yb = rect_taps(sy, c.y0, c.y1, s.y0, s.y1, ty, scale, ya);
    if (xa >= xb || ya >= yb)
        return;
    T key = F::pack(ground);
    auto run = [&](auto px) {
        if constexpr (std::is_same_v<L, Rot>) {
            for (int x = xa; x < xb; x++) {
                size_t col = (size_t)(layer.w - 1 - sx[x]) * layer.stride;
                const T* l = layer.px + col;
                const uint8_t* m = mask ? mask + col : nullptr;
                T* row = dst.mem_row(dst.w - 1 - x);
                for (int y = ya; y < yb; y++)
                    row[y] = px(row[y], l, m, sy[y]);
            }
        } else {
            for (int y = ya; y < yb; y++) {
                size_t at = layer.index(0, sy[y]);
                const T* l = layer.px + at;
                const uint8_t* m = mask ? mask + at : nullptr;
                T* d = dst.row(y);
                if constexpr (std::is_same_v<F, R565>)
                    pair_span(d, xa, xb, [&](int x) { return px(d[x], l, m, sx[x]); });
                else
                    for (int x = xa; x < xb; x++)
                        d[x] = px(d[x], l, m, sx[x]);
            }
        }
    };
    if (!mask) {
        run([&](T d, const T* l, const uint8_t*, int i) {
            T v = l[i];
            return v == key ? d : a >= 256 ? v : F::blend(d, v, a);
        });
    } else if constexpr (std::is_same_v<F, R565>) {
        uint32_t g = unpack565(key);
        int a5 = alpha5(a);
        if (motion && a < 256)
            run([&](T d, const T* l, const uint8_t* m, int i) {
                int mm = m[i];
                return mm ? over_ground5(d, l[i], mm, key, a5) : d;
            });
        else
            run([&](T d, const T* l, const uint8_t* m, int i) {
                int mm = m[i];
                return mm ? over_ground(d, l[i], mm, g, a) : d;
            });
    } else {
        run([&](T d, const T* l, const uint8_t* m, int i) {
            int mm = m[i];
            return !mm ? d : mm >= 255 && a >= 256 ? l[i] : over_ground256(d, l[i], mm, key, a);
        });
    }
}

struct PremulTops {
    uint8_t c5[256], c6[256];
};

constexpr PremulTops make_tops()
{
    PremulTops t{};
    for (int al = 0; al < 256; al++) {
        t.c5[al] = (uint8_t)((al * 31 + 127) / 255);
        t.c6[al] = (uint8_t)((al * 63 + 127) / 255);
    }
    return t;
}

inline constexpr PremulTops kTops = make_tops();

uint32_t spread16(uint16_t p) { return (uint32_t)(p & 0xF800) << 5 | (p & 31); }

uint16_t mix565(uint16_t u, uint16_t o, int a)
{
    uint32_t ia = 256 - (uint32_t)a;
    uint32_t rb = (spread16(o) * (uint32_t)a + spread16(u) * ia + 0x00800080) >> 8;
    uint32_t g = ((uint32_t)(o >> 5 & 63) * (uint32_t)a + (uint32_t)(u >> 5 & 63) * ia + 128) >> 8;
    return (uint16_t)((rb >> 5 & 0xF800) | g << 5 | (rb & 31));
}

uint16_t premul565(uint16_t d, uint16_t v, int al, uint32_t ik, uint32_t a)
{
    uint32_t t5 = kTops.c5[al], t6 = kTops.c6[al];
    uint32_t vr = std::min<uint32_t>(v >> 11, t5), vg = std::min<uint32_t>(v >> 5 & 63, t6),
             vb = std::min<uint32_t>(v & 31, t5);
    uint32_t rb = (spread16(d) * ik + (vr << 16 | vb) * a + 0x00800080) >> 8 & 0x003F003F;
    uint32_t g = std::min<uint32_t>(((uint32_t)(d >> 5 & 63) * ik + vg * a + 128) >> 8, 63);
    uint32_t ov = rb & 0x00200020;
    rb = (rb | (ov - (ov >> 5))) & 0x001F001F;
    return (uint16_t)((rb >> 5 & 0xF800) | g << 5 | (rb & 31));
}

[[gnu::noinline]] uint16_t mix_premul_edge(uint16_t u, uint16_t o, uint16_t v, int m, int mix_a, uint32_t ik, int a)
{
    return premul565(mix565(u, o, mix_a), v, m, ik, (uint32_t)a);
}

uint16_t mpm_full(uint16_t u, uint16_t o, uint16_t v, uint32_t wu, uint32_t wo, uint32_t wv)
{
    return fold565((spread565(u) * wu + spread565(o) * wo + spread565(v) * wv + 0x02008010) >> 5 & 0x07E0F81F);
}

void mix_premul_motion(const SurfaceT<R565, Rows>& under, const SurfaceT<R565, Rows>& over, int mix_a,
                       const SurfaceT<R565, Rows>& layer, const uint8_t* alpha, const SurfaceT<R565, Rows>& dst,
                       const int* src_x, int x0, int x1, int64_t inv, int cy, int a, const uint32_t* ik)
{
    int mk = alpha5(mix_a), v5 = a >= 256 ? 32 : alpha5(a), o5 = (mix_a * (256 - a) + 1024) >> 11,
        u5 = 32 - v5 - o5;
    if (u5 < 0) {
        o5 += u5;
        u5 = 0;
    }
    uint32_t wu = (uint32_t)u5, wo = (uint32_t)o5, wv = (uint32_t)v5;
    for (int y = 0; y < dst.h; y++) {
        const uint16_t* u = under.row(y);
        const uint16_t* o = over.row(y);
        uint16_t* d = dst.row(y);
        int sy = unscale(y, cy, inv);
        bool on = sy >= 0 && sy < layer.h;
        int l0 = on ? x0 : dst.w, l1 = on ? x1 : dst.w;
        for (int x = 0; x < l0; x++)
            d[x] = lerp565(u[x], o[x], mk);
        for (int x = l1; x < dst.w; x++)
            d[x] = lerp565(u[x], o[x], mk);
        if (!on)
            continue;
        const uint16_t* l = layer.row(sy);
        const uint8_t* m = alpha + layer.index(0, sy);
        int e0 = std::min(x0 + (int)((uintptr_t)(d + x0) >> 1 & 1), x1), e1 = e0 + ((x1 - e0) & ~1);
        auto single = [&](int x) {
            int sc = src_x[x], mm = m[sc];
            d[x] = !mm         ? lerp565(u[x], o[x], mk)
                   : mm >= 255 ? mpm_full(u[x], o[x], l[sc], wu, wo, wv)
                               : mix_premul_edge(u[x], o[x], l[sc], mm, mix_a, ik[mm], a);
        };
        for (int x = x0; x < e0; x++)
            single(x);
        for (int x = e0; x < e1; x += 2) {
            int c0 = src_x[x], c1 = src_x[x + 1], m0 = m[c0], m1 = m[c1];
            uint32_t q0, q1;
            if ((m0 & m1) == 255) {
                q0 = mpm_full(u[x], o[x], l[c0], wu, wo, wv);
                q1 = mpm_full(u[x + 1], o[x + 1], l[c1], wu, wo, wv);
            } else if ((m0 | m1) == 0) {
                q0 = lerp565(u[x], o[x], mk);
                q1 = lerp565(u[x + 1], o[x + 1], mk);
            } else {
                q0 = !m0         ? lerp565(u[x], o[x], mk)
                     : m0 >= 255 ? mpm_full(u[x], o[x], l[c0], wu, wo, wv)
                                 : mix_premul_edge(u[x], o[x], l[c0], m0, mix_a, ik[m0], a);
                q1 = !m1         ? lerp565(u[x + 1], o[x + 1], mk)
                     : m1 >= 255 ? mpm_full(u[x + 1], o[x + 1], l[c1], wu, wo, wv)
                                 : mix_premul_edge(u[x + 1], o[x + 1], l[c1], m1, mix_a, ik[m1], a);
            }
            *(PixPair*)(d + x) = q0 | q1 << 16;
        }
        for (int x = e1; x < x1; x++)
            single(x);
    }
}

template <int Mode> uint16_t pm_mix(uint16_t u, uint16_t o, int k)
{
    if constexpr (Mode == 0)
        return u;
    else if constexpr (Mode == 1)
        return o;
    else if constexpr (Mode == 2)
        return lerp565(u, o, k);
    else
        return mix565(u, o, k);
}

template <int Mode>
void premul565(const SurfaceT<R565, Rows>* under, const SurfaceT<R565, Rows>* over, int k,
               const SurfaceT<R565, Rows>& layer, const uint8_t* alpha, const SurfaceT<R565, Rows>& dst,
               const int* src_x, int x0, int x1, int64_t inv, int cy, bool premul_on, int a, const uint32_t* ik)
{
    for (int y = 0; y < dst.h; y++) {
        uint16_t* d = dst.row(y);
        int sy = premul_on ? unscale(y, cy, inv) : -1;
        bool on = sy >= 0 && sy < layer.h;
        const uint16_t* u = nullptr;
        const uint16_t* o = nullptr;
        if constexpr (Mode != 4) {
            u = under->row(y);
            o = over->row(y);
            int l0 = on ? x0 : dst.w, l1 = on ? x1 : dst.w;
            for (int x = 0; x < l0; x++)
                d[x] = pm_mix<Mode>(u[x], o[x], k);
            for (int x = l1; x < dst.w; x++)
                d[x] = pm_mix<Mode>(u[x], o[x], k);
        }
        if (!on)
            continue;
        const uint16_t* s = layer.row(sy);
        const uint8_t* al = alpha + layer.index(0, sy);
        for (int x = x0; x < x1; x++) {
            uint16_t p;
            if constexpr (Mode == 4)
                p = d[x];
            else
                p = pm_mix<Mode>(u[x], o[x], k);
            int sc = src_x[x], m = al[sc];
            if (m >= 255)
                d[x] = mix565(p, s[sc], a);
            else
                d[x] = m ? premul565(p, s[sc], m, ik[m], (uint32_t)a) : p;
        }
    }
}

template <class F, class L>
void premul_impl(const SurfaceT<F, L>* under, const SurfaceT<F, L>* over, int mix_a, const SurfaceT<F, L>& layer,
                 const uint8_t* alpha, const SurfaceT<F, L>& dst, fx scale, int cx, int cy, int a, bool motion)
{
    using T = typename F::T;
    a = std::clamp(a, 0, 256);
    bool premul_on = a > 0 && scale > 0;
    int64_t inv = premul_on ? ((int64_t)kOne << kFxBits) / scale : kOne;
    Taps src_x(dst.w);
    int x0 = dst.w, x1 = dst.w;
    for (int x = 0; x < dst.w; x++) {
        int sx = unscale(x, cx, inv);
        src_x[x] = sx >= 0 && sx < layer.w ? sx : -1;
        if (src_x[x] >= 0) {
            x0 = std::min(x0, x);
            x1 = x + 1;
        }
    }
    uint32_t ik[256];
    for (int al = 0; al < 256; al++)
        ik[al] = 256 - (uint32_t)((al + 1) * a >> 8);
    if constexpr (std::is_same_v<F, R565>) {
        if (motion && under && mix_a > 0 && mix_a < 256 && premul_on) {
            mix_premul_motion(*under, *over, mix_a, layer, alpha, dst, src_x.p, x0, x1, inv, cy, a, ik);
            return;
        }
        if (!under)
            premul565<4>(under, over, 0, layer, alpha, dst, src_x.p, x0, x1, inv, cy, premul_on, a, ik);
        else if (mix_a <= 0)
            premul565<0>(under, over, 0, layer, alpha, dst, src_x.p, x0, x1, inv, cy, premul_on, a, ik);
        else if (mix_a >= 256)
            premul565<1>(under, over, 0, layer, alpha, dst, src_x.p, x0, x1, inv, cy, premul_on, a, ik);
        else if (motion)
            premul565<2>(under, over, alpha5(mix_a), layer, alpha, dst, src_x.p, x0, x1, inv, cy, premul_on, a, ik);
        else
            premul565<3>(under, over, mix_a, layer, alpha, dst, src_x.p, x0, x1, inv, cy, premul_on, a, ik);
    } else {
        auto run = [&](auto mx) {
            for (int y = 0; y < dst.h; y++) {
                T* row = dst.row(y);
                const T* u = under ? under->row(y) : nullptr;
                const T* o = under ? over->row(y) : nullptr;
                int sy = premul_on ? unscale(y, cy, inv) : -1;
                bool on = sy >= 0 && sy < layer.h;
                int l0 = on ? x0 : dst.w, l1 = on ? x1 : dst.w;
                if (u) {
                    for (int x = 0; x < l0; x++)
                        row[x] = mx(u[x], o[x]);
                    for (int x = l1; x < dst.w; x++)
                        row[x] = mx(u[x], o[x]);
                }
                if (!on)
                    continue;
                const T* s = layer.row(sy);
                const uint8_t* al = alpha + layer.index(0, sy);
                for (int x = x0; x < x1; x++) {
                    T p = u ? mx(u[x], o[x]) : row[x];
                    int sc = src_x[x], m = al[sc];
                    row[x] = m ? premul888(p, s[sc], m, a) : p;
                }
            }
        };
        if (mix_a <= 0)
            run([](T u, T) { return u; });
        else if (mix_a >= 256)
            run([](T, T o) { return o; });
        else
            run([mix_a](T u, T o) { return F::blend(u, o, mix_a); });
    }
}

[[gnu::noinline]] uint16_t scrim_edge(uint16_t p, uint16_t l, int m, uint16_t v, uint16_t key, int sa, int la)
{
    auto lane = [&](int sh, int top) {
        int c = (p >> sh & top) * (256 - sa) + (v >> sh & top) * sa;
        int q = std::clamp((l >> sh & top) * 256 + signed_div255((c - (key >> sh & top) * 256) * (255 - m)), 0,
                           top * 256);
        return (uint16_t)(((c * (256 - la) + q * la + 32768) >> 16) << sh);
    };
    return (uint16_t)(lane(11, 31) | lane(5, 63) | lane(0, 31));
}

uint16_t x10_full(uint16_t p, uint16_t l, uint32_t ws, uint32_t wv, uint32_t crb, uint32_t cg)
{
    uint32_t rb = (spread16(p) * ws + spread16(l) * wv + crb) >> 10 & 0x001F001F;
    uint32_t gg = ((uint32_t)(p >> 5 & 63) * ws + (uint32_t)(l >> 5 & 63) * wv + cg) >> 10;
    return (uint16_t)((rb >> 5 & 0xF800) | gg << 5 | (rb & 31));
}

[[gnu::noinline]] uint16_t x10_edge(uint16_t p, uint16_t l, int m, uint16_t v, int sa, uint32_t g, int a)
{
    return over_ground(blend565(p, v, sa), l, m, g, a);
}

template <int Mode>
void scrim_scaled_x10(const SurfaceT<R565, Rows>& page, const SurfaceT<R565, Rows>& layer, const uint8_t* mask,
                      const SurfaceT<R565, Rows>& dst, const int* src_x, int x0, int x1, int64_t inv, int cy,
                      uint16_t v, uint32_t g, int sa, int la, int a, int sk)
{
    uint32_t ws = (uint32_t)(((256 - sa) * (256 - la) + 32) >> 6), wv = (uint32_t)la * 4, wc = 1024 - ws - wv;
    uint32_t crb = spread16(v) * wc + 0x02000200, cg = (uint32_t)(v >> 5 & 63) * wc + 512;
    for (int y = 0; y < dst.h; y++) {
        const uint16_t* s = page.row(y);
        uint16_t* d = dst.row(y);
        int sy = unscale(y, cy, inv);
        bool on = sy >= 0 && sy < layer.h;
        int u0 = on ? x0 : dst.w, u1 = on ? x1 : dst.w;
        if constexpr (Mode == 2) {
            fill_px(d, v, u0);
            fill_px(d + u1, v, dst.w - u1);
        } else if constexpr (Mode == 0) {
            if (s != d) {
                copy_px(d, s, u0);
                copy_px(d + u1, s + u1, dst.w - u1);
            }
        } else {
            for (int x = 0; x < u0; x++)
                d[x] = lerp565(s[x], v, sk);
            for (int x = u1; x < dst.w; x++)
                d[x] = lerp565(s[x], v, sk);
        }
        if (!on)
            continue;
        const uint16_t* l = layer.row(sy);
        const uint8_t* m = mask + layer.index(0, sy);
        for (int x = x0; x < x1; x++) {
            int sc = src_x[x], mm = m[sc];
            uint16_t p = Mode == 2 ? v : s[x];
            if (!mm) {
                if constexpr (Mode == 1)
                    d[x] = lerp565(p, v, sk);
                else
                    d[x] = p;
            } else {
                d[x] = mm >= 255 ? x10_full(p, l[sc], ws, wv, crb, cg) : x10_edge(p, l[sc], mm, v, sa, g, a);
            }
        }
    }
}

void scrim_scaled_p5(const SurfaceT<R565, Rows>& page, const SurfaceT<R565, Rows>& layer, const uint8_t* mask,
                     const SurfaceT<R565, Rows>& dst, const int* src_x, int x0, int x1, int64_t inv, int cy, uint16_t v,
                     uint16_t key, int sa, int la, int sk, uint32_t ws, uint32_t wv, uint32_t k5)
{
    for (int y = 0; y < dst.h; y++) {
        const uint16_t* s = page.row(y);
        uint16_t* d = dst.row(y);
        int sy = unscale(y, cy, inv);
        if (sy < 0 || sy >= layer.h) {
            for (int x = 0; x < dst.w; x++)
                d[x] = lerp565(s[x], v, sk);
            continue;
        }
        const uint16_t* l = layer.row(sy);
        const uint8_t* m = mask + layer.index(0, sy);
        for (int x = 0; x < x0; x++)
            d[x] = lerp565(s[x], v, sk);
        int e0 = x1, e1 = x1;
        if ((((uintptr_t)s ^ (uintptr_t)d) & 2) == 0) {
            e0 = std::min(x0 + (int)((uintptr_t)(d + x0) >> 1 & 1), x1);
            e1 = e0 + ((x1 - e0) & ~1);
        }
        for (int x = x0; x < e0; x++) {
            int sc = src_x[x], mm = m[sc];
            uint16_t p = s[x];
            d[x] = !mm         ? lerp565(p, v, sk)
                   : mm >= 255 ? fold565((spread565(p) * ws + spread565(l[sc]) * wv + k5) >> 5 & 0x07E0F81F)
                               : scrim_edge(p, l[sc], mm, v, key, sa, la);
        }
        for (int x = e0; x < e1; x += 2) {
            uint32_t pp = *(const PixPair*)(s + x);
            int c0 = src_x[x], c1 = src_x[x + 1], m0 = m[c0], m1 = m[c1];
            uint16_t p0 = (uint16_t)pp, p1 = (uint16_t)(pp >> 16);
            uint32_t o0, o1;
            if ((m0 & m1) == 255) {
                o0 = fold565((spread565(p0) * ws + spread565(l[c0]) * wv + k5) >> 5 & 0x07E0F81F);
                o1 = fold565((spread565(p1) * ws + spread565(l[c1]) * wv + k5) >> 5 & 0x07E0F81F);
            } else {
                o0 = !m0         ? lerp565(p0, v, sk)
                     : m0 >= 255 ? fold565((spread565(p0) * ws + spread565(l[c0]) * wv + k5) >> 5 & 0x07E0F81F)
                                 : scrim_edge(p0, l[c0], m0, v, key, sa, la);
                o1 = !m1         ? lerp565(p1, v, sk)
                     : m1 >= 255 ? fold565((spread565(p1) * ws + spread565(l[c1]) * wv + k5) >> 5 & 0x07E0F81F)
                                 : scrim_edge(p1, l[c1], m1, v, key, sa, la);
            }
            *(PixPair*)(d + x) = o0 | o1 << 16;
        }
        for (int x = e1; x < x1; x++) {
            int sc = src_x[x], mm = m[sc];
            uint16_t p = s[x];
            d[x] = !mm         ? lerp565(p, v, sk)
                   : mm >= 255 ? fold565((spread565(p) * ws + spread565(l[sc]) * wv + k5) >> 5 & 0x07E0F81F)
                               : scrim_edge(p, l[sc], mm, v, key, sa, la);
        }
        for (int x = x1; x < dst.w; x++)
            d[x] = lerp565(s[x], v, sk);
    }
}

template <class F, class L>
void scrim_scaled_impl(const SurfaceT<F, L>& page, const SurfaceT<F, L>& layer, const uint8_t* mask,
                       const SurfaceT<F, L>& dst, uint32_t color, int scrim_a, fx scale, int cx, int cy,
                       uint32_t ground, int a, bool motion)
{
    uint16_t v = pack565(color), key = pack565(ground);
    uint32_t g = unpack565(key);
    int sa = std::clamp(scrim_a, 0, 256);
    bool on = a > 0 && scale > 0;
    int64_t inv = on ? ((int64_t)kOne << kFxBits) / scale : kOne;
    Taps src_x(dst.w);
    int x0 = dst.w, x1 = dst.w;
    if (on) {
        for (int x = 0; x < dst.w; x++) {
            int sx = unscale(x, cx, inv);
            src_x[x] = sx >= 0 && sx < layer.w ? sx : -1;
            if (src_x[x] >= 0) {
                x0 = std::min(x0, x);
                x1 = x + 1;
            }
        }
    }
    auto run = [&](auto sp, auto edge, auto full) {
        auto plain = [&](const uint16_t* s, uint16_t* d, int from, int to) {
            int x = from;
            if ((((uintptr_t)s ^ (uintptr_t)d) & 2) != 0) {
                for (; x < to; x++)
                    d[x] = sp(s[x]);
                return;
            }
            if ((uintptr_t)(d + x) & 2 && x < to) {
                d[x] = sp(s[x]);
                x++;
            }
            for (; x + 1 < to; x += 2) {
                uint32_t pp = *(const PixPair*)(s + x);
                *(PixPair*)(d + x) = (uint32_t)sp((uint16_t)pp) | (uint32_t)sp((uint16_t)(pp >> 16)) << 16;
            }
            if (x < to)
                d[x] = sp(s[x]);
        };
        for (int y = 0; y < dst.h; y++) {
            const uint16_t* s = page.row(y);
            uint16_t* d = dst.row(y);
            int sy = on ? unscale(y, cy, inv) : -1;
            if (sy < 0 || sy >= layer.h) {
                plain(s, d, 0, dst.w);
                continue;
            }
            const uint16_t* l = layer.row(sy);
            const uint8_t* m = mask + layer.index(0, sy);
            auto one = [&](uint16_t p, int x) {
                int sc = src_x[x], mm = m[sc];
                return !mm ? sp(p) : mm >= 255 ? full(p, l[sc]) : edge(p, l[sc], mm);
            };
            plain(s, d, 0, x0);
            for (int x = x0; x < x1; x++)
                d[x] = one(s[x], x);
            plain(s, d, x1, dst.w);
        }
    };
    if (!motion) {
        auto edge = [&](uint16_t p, uint16_t l, int m) { return over_ground(blend565(p, v, sa), l, m, g, a); };
        run([&](uint16_t p) { return blend565(p, v, sa); }, edge,
            [&](uint16_t p, uint16_t l) { return edge(p, l, 255); });
        return;
    }
    int sk = alpha5(sa), la = std::clamp(a, 0, 256);
    if (la >= 256 || sa == 0 || sa == 256) {
        if (sa >= 256)
            scrim_scaled_x10<2>(page, layer, mask, dst, src_x.p, x0, x1, inv, cy, v, g, sa, la, a, sk);
        else if (sa <= 0)
            scrim_scaled_x10<0>(page, layer, mask, dst, src_x.p, x0, x1, inv, cy, v, g, sa, la, a, sk);
        else
            scrim_scaled_x10<1>(page, layer, mask, dst, src_x.p, x0, x1, inv, cy, v, g, sa, la, a, sk);
        return;
    }
    int s5 = ((256 - sa) * (256 - la) + 1024) >> 11, v5 = (la * 32 + 128) >> 8, c5 = 32 - s5 - v5;
    if (c5 < 0) {
        s5 += c5;
        c5 = 0;
    }
    scrim_scaled_p5(page, layer, mask, dst, src_x.p, x0, x1, inv, cy, v, key, sa, la, sk, (uint32_t)s5, (uint32_t)v5,
                    spread565(v) * (uint32_t)c5 + 0x02008010);
}

template <bool Motion> uint16_t shifted_px(uint16_t d, uint16_t s, int k)
{
    if constexpr (Motion)
        return lerp565(d, s, k);
    else
        return blend565(d, s, k);
}

template <class F, class L>
const typename F::T* shift_row(const SurfaceT<F, L>& page, const PageShift<F, L>& sh, int y,
                               std::vector<typename F::T>& fill)
{
    int y1 = std::min(sh.y1, page.h);
    if (!sh.ground || sh.dy <= 0 || y < sh.y0 || y >= y1)
        return page.row(y);
    if (y + sh.dy < y1)
        return page.row(y + sh.dy);
    fill.assign((size_t)page.w, sh.ground->at(0, y));
    return fill.data();
}

template <bool Motion>
void scrim_shifted565(const SurfaceT<R565, Rows>& page, const SurfaceT<R565, Rows>& layer,
                      const SurfaceT<R565, Rows>& dst, uint16_t v, int sa, int la, Rect q, int y0, int y1, int dy,
                      const PageShift<R565, Rows>& sh)
{
    std::vector<uint16_t> fill;
    for (int y = 0; y < dst.h; y++) {
        const uint16_t* s = shift_row(page, sh, y, fill);
        uint16_t* d = dst.row(y);
        int x0 = y >= y0 && y < y1 ? q.x0 : dst.w, x1 = x0 < dst.w ? q.x1 : dst.w;
        for (int x = 0; x < x0; x++)
            d[x] = shifted_px<Motion>(s[x], v, sa);
        if (x0 < dst.w) {
            const uint16_t* l = layer.row(y - dy);
            for (int x = x0; x < x1; x++)
                d[x] = shifted_px<Motion>(shifted_px<Motion>(s[x], v, sa), l[x], la);
            for (int x = x1; x < dst.w; x++)
                d[x] = shifted_px<Motion>(s[x], v, sa);
        }
    }
}

template <int Mode> uint16_t mixs_px(uint16_t p, uint16_t v, int k)
{
    if constexpr (Mode == 0)
        return v;
    else if constexpr (Mode == 1)
        return lerp565(p, v, k);
    else
        return mix565(p, v, k);
}

template <int Mode>
void mix_scaled565(const SurfaceT<R565, Rows>& page, const SurfaceT<R565, Rows>& q, const SurfaceT<R565, Rows>& dst,
                   int k, bool on, const int* src_x, int x0, int x1, int64_t inv, int cy)
{
    for (int y = 0; y < dst.h; y++) {
        const uint16_t* s = page.row(y);
        uint16_t* d = dst.row(y);
        int sy = on ? unscale(y, cy, inv) : -1;
        if (sy < 0 || sy >= q.h) {
            if (s != d)
                copy_px(d, s, dst.w);
            continue;
        }
        const uint16_t* l = q.row(sy);
        if (s != d) {
            copy_px(d, s, x0);
            copy_px(d + x1, s + x1, dst.w - x1);
        }
        int x = x0;
        if (x < x1 && (uintptr_t)(d + x) & 2) {
            d[x] = mixs_px<Mode>(s[x], l[src_x[x]], k);
            x++;
        }
        for (; x + 1 < x1; x += 2) {
            uint32_t lo = mixs_px<Mode>(s[x], l[src_x[x]], k);
            uint32_t hi = mixs_px<Mode>(s[x + 1], l[src_x[x + 1]], k);
            *(PixPair*)(d + x) = lo | hi << 16;
        }
        if (x < x1)
            d[x] = mixs_px<Mode>(s[x], l[src_x[x]], k);
    }
}

[[gnu::noinline]] void scrim_lanes(const SurfaceT<R565, Rows>& page, const SurfaceT<R565, Rows>& dst, Rect q, uint16_t v,
                                   int a)
{
    Blend565 b(v, a);
    int n = q.x1 - q.x0;
    for (int y = q.y0; y < q.y1; y++)
        b.span(dst.row(y) + q.x0, page.row(y) + q.x0, n);
}

[[gnu::noinline]] void scrim_motion(const SurfaceT<R565, Rows>& page, const SurfaceT<R565, Rows>& dst, Rect q,
                                    uint16_t v, int k)
{
    int n = q.x1 - q.x0;
    for (int y = q.y0; y < q.y1; y++) {
        const uint16_t* s = page.row(y) + q.x0;
        pair_span(dst.row(y) + q.x0, 0, n, [&](int x) { return lerp565(s[x], v, k); });
    }
}

}

template <class F, class L>
void scrim(const SurfaceT<F, L>& page, const SurfaceT<F, L>& dst, uint32_t color, int a, bool motion)
{
    scrim(page, dst, dst.rect(), color, a, motion);
}

template <class F, class L>
void scrim(const SurfaceT<F, L>& page, const SurfaceT<F, L>& dst, Rect p, uint32_t color, int a, bool motion)
{
    prof::Scope ps(prof::Scrim);
    if constexpr (std::is_same_v<L, Rot>) {
        Rect m = dst.mem_rect(p);
        int n = m.x1 - m.x0;
        for (int r = m.y0; r < m.y1; r++) {
            const uint32_t* s = page.mem_row(r) + m.x0;
            uint32_t* d = dst.mem_row(r) + m.x0;
            if (a <= 0) {
                if (s != d)
                    copy_px(d, s, n);
                continue;
            }
            if (a >= 256) {
                fill_px(d, color, n);
                continue;
            }
            for (int j = 0; j < n; j++)
                d[j] = blend_px(s[j], color, a);
        }
    } else {
        using T = typename F::T;
        Rect q = p.intersect(dst.rect());
        if (q.empty())
            return;
        T v = F::pack(color);
        int n = q.x1 - q.x0;
        if constexpr (std::is_same_v<F, R565> && std::is_same_v<L, Rows>) {
            if (a > 0 && a < 256) {
                if (motion)
                    scrim_motion(page, dst, q, v, alpha5(a));
                else
                    scrim_lanes(page, dst, q, v, a);
                return;
            }
        }
        for (int y = q.y0; y < q.y1; y++) {
            const T* s = page.row(y);
            T* d = dst.row(y);
            if (a <= 0) {
                if (s != d)
                    copy_px(d + q.x0, s + q.x0, n);
            } else if (a >= 256) {
                fill_px(d + q.x0, v, n);
            } else {
                for (int x = q.x0; x < q.x1; x++)
                    d[x] = F::blend(s[x], v, a);
            }
        }
    }
}

template <class F, class L>
void blend_shifted(const SurfaceT<F, L>& layer, const SurfaceT<F, L>& dst, Rect p, int dy, int a, bool motion)
{
    prof::Scope ps(prof::BlendShifted);
    if (a <= 0)
        return;
    if constexpr (std::is_same_v<L, Rot>) {
        Rect m = dst.mem_rect(p);
        int j0 = std::max(m.x0 + dy, 0), j1 = std::min(m.x1 + dy, dst.h);
        for (int r = m.y0; r < m.y1; r++) {
            const uint32_t* s = layer.mem_row(r);
            uint32_t* d = dst.mem_row(r);
            if (a >= 256) {
                if (j1 > j0)
                    copy_px(d + j0, s + j0 - dy, j1 - j0);
                continue;
            }
            for (int j = j0; j < j1; j++)
                d[j] = blend_px(d[j], s[j - dy], a);
        }
    } else {
        using T = typename F::T;
        Rect q = p.intersect(dst.rect());
        if (q.empty())
            return;
        int y0 = std::max(q.y0 + dy, 0), y1 = std::min(q.y1 + dy, dst.h), n = q.x1 - q.x0, k = alpha5(a);
        for (int y = y0; y < y1; y++) {
            const T* s = layer.row(y - dy) + q.x0;
            T* d = dst.row(y) + q.x0;
            if (a >= 256)
                copy_px(d, s, n);
            else if constexpr (std::is_same_v<F, R565>) {
                if (motion)
                    for (int i = 0; i < n; i++)
                        d[i] = lerp565(d[i], s[i], k);
                else
                    for (int i = 0; i < n; i++)
                        d[i] = blend565(d[i], s[i], a);
            } else
                for (int i = 0; i < n; i++)
                    d[i] = F::blend(d[i], s[i], a);
        }
    }
}

namespace {

void scrim_row(const uint32_t* s, uint32_t* d, int j0, int j1, uint32_t color, int a)
{
    if (j1 <= j0)
        return;
    if (a <= 0) {
        if (s != d)
            copy_px(d + j0, s + j0, j1 - j0);
    } else if (a >= 256) {
        fill_px(d + j0, color, j1 - j0);
    } else {
        for (int j = j0; j < j1; j++)
            d[j] = blend_px(s[j], color, a);
    }
}

inline uint32_t scrim_px(uint32_t s, uint32_t color, int a) { return a <= 0 ? s : a >= 256 ? color : blend_px(s, color, a); }

}

template <class F, class L>
void scrim_shifted_rows(const SurfaceT<F, L>& page, const SurfaceT<F, L>& layer, const SurfaceT<F, L>& dst,
                        uint32_t color, int scrim_a, Rect p, int dy, int a, int r0, int r1,
                        const PageShift<F, L>& shift)
{
    if constexpr (std::is_same_v<L, Rot>) {
        prof::Scope ps(prof::ScrimShifted);
        Rect m = dst.mem_rect(p);
        int j0 = std::max(m.x0 + dy, 0), j1 = std::min(m.x1 + dy, dst.h);
        r0 = std::max(r0, 0);
        r1 = std::min(r1, dst.w);
        std::vector<uint32_t> tmp(shift.ground && shift.dy > 0 ? (size_t)dst.h : 0);
        for (int r = r0; r < r1; r++) {
            const uint32_t* s = page.mem_row(r);
            if (!tmp.empty()) {
                std::copy(s, s + dst.h, tmp.begin());
                int y1 = std::min(shift.y1, dst.h);
                for (int j = std::max(shift.y0, 0); j < y1; j++)
                    tmp[(size_t)j] = j + shift.dy < y1 ? s[j + shift.dy] : shift.ground->at(0, j);
                s = tmp.data();
            }
            uint32_t* d = dst.mem_row(r);
            if (a <= 0 || r < m.y0 || r >= m.y1 || j1 <= j0) {
                scrim_row(s, d, 0, dst.h, color, scrim_a);
                continue;
            }
            const uint32_t* l = layer.mem_row(r);
            scrim_row(s, d, 0, j0, color, scrim_a);
            if (a >= 256)
                copy_px(d + j0, l + j0 - dy, j1 - j0);
            else
                for (int j = j0; j < j1; j++)
                    d[j] = blend_px(scrim_px(s[j], color, scrim_a), l[j - dy], a);
            scrim_row(s, d, j1, dst.h, color, scrim_a);
        }
    } else {
        scrim_shifted(page, layer, dst, color, scrim_a, p, dy, a, false, shift);
    }
}

template <class F, class L>
void scrim_coverage_rows(const SurfaceT<F, L>& page, const SurfaceT<F, L>& layer, const SurfaceT<F, L>& dst,
                         uint32_t scrim_color, int scrim_a, Rect p, fx scale, int cx, int cy, uint32_t color, int a,
                         int r0, int r1)
{
    if constexpr (std::is_same_v<L, Rot>) {
        prof::Scope pc(prof::Coverage);
        r0 = std::max(r0, 0);
        r1 = std::min(r1, dst.w);
        p = p.intersect(layer.rect());
        bool on = !p.empty() && a > 0 && scale > 0;
        auto fwd = [&](int v, int c) { return c + (int)(((int64_t)(v - c) * scale) >> kFxBits); };
        Rect d{};
        int64_t inv = 0;
        Taps src_col(dst.h);
        if (on) {
            d = Rect{fwd(p.x0, cx) - 1, fwd(p.y0, cy) - 1, fwd(p.x1, cx) + 2, fwd(p.y1, cy) + 2}.intersect(dst.rect());
            inv = ((int64_t)kOne << kFxBits) / scale;
            for (int y = d.y0; y < d.y1; y++) {
                int sy = cy + (int)(((int64_t)(y - cy) * inv + kHalf) >> kFxBits);
                src_col[y] = sy >= p.y0 && sy < p.y1 ? sy : -1;
            }
        }
        for (int r = r0; r < r1; r++) {
            const uint32_t* s = page.mem_row(r);
            uint32_t* row = dst.mem_row(r);
            int x = dst.w - 1 - r;
            const uint32_t* l = nullptr;
            if (on && x >= d.x0 && x < d.x1) {
                int sx = cx + (int)(((int64_t)(x - cx) * inv + kHalf) >> kFxBits);
                if (sx >= p.x0 && sx < p.x1)
                    l = layer.mem_row(layer.w - 1 - sx);
            }
            if (!l || d.y1 <= d.y0) {
                scrim_row(s, row, 0, dst.h, scrim_color, scrim_a);
                continue;
            }
            scrim_row(s, row, 0, d.y0, scrim_color, scrim_a);
            for (int y = d.y0; y < d.y1; y++) {
                uint32_t v = scrim_px(s[y], scrim_color, scrim_a);
                int sc = src_col[y];
                int cov = sc >= 0 ? l[sc] >> 8 & 0xFF : 0;
                row[y] = cov ? blend_px(v, color, (cov + 1) * a >> 8) : v;
            }
            scrim_row(s, row, d.y1, dst.h, scrim_color, scrim_a);
        }
    } else {
        scrim(page, dst, scrim_color, scrim_a);
        coverage_scaled(layer, nullptr, dst, p, scale, cx, cy, color, a);
    }
}

template <class F, class L>
void scrim_shifted(const SurfaceT<F, L>& page, const SurfaceT<F, L>& layer, const SurfaceT<F, L>& dst, uint32_t color,
                   int scrim_a, Rect p, int dy, int a, bool motion, const PageShift<F, L>& shift)
{
    if constexpr (std::is_same_v<L, Rot>) {
        scrim_shifted_rows(page, layer, dst, color, scrim_a, p, dy, a, 0, dst.w, shift);
    } else if constexpr (!std::is_same_v<F, R565>) {
        prof::Scope ps(prof::ScrimShifted);
        if (shift.ground && shift.dy > 0) {
            using T = typename F::T;
            T v = F::pack(color);
            std::vector<T> fill;
            for (int y = 0; y < dst.h; y++) {
                const T* s = shift_row(page, shift, y, fill);
                T* d = dst.row(y);
                for (int x = 0; x < dst.w; x++)
                    d[x] = scrim_a <= 0 ? s[x] : scrim_a >= 256 ? v : F::blend(s[x], v, scrim_a);
            }
        } else {
            scrim(page, dst, color, scrim_a);
        }
        blend_shifted(layer, dst, p, dy, a);
    } else {
        prof::Scope ps(prof::ScrimShifted);
        uint16_t v = pack565(color);
        int sa = std::clamp(scrim_a, 0, 256), la = std::clamp(a, 0, 256);
        if (motion) {
            sa = alpha5(sa);
            la = alpha5(la);
        }
        Rect q = p.intersect(dst.rect());
        int y0 = q.empty() || a <= 0 ? 0 : std::max(q.y0 + dy, 0);
        int y1 = q.empty() || a <= 0 ? 0 : std::min(q.y1 + dy, dst.h);
        if (motion)
            scrim_shifted565<true>(page, layer, dst, v, sa, la, q, y0, y1, dy, shift);
        else
            scrim_shifted565<false>(page, layer, dst, v, sa, la, q, y0, y1, dy, shift);
    }
}

template <class F, class L>
void coverage_scaled(const SurfaceT<F, L>& layer, const SurfaceT<F, L>& dst, Rect p, fx scale, int cx, int cy,
                     uint32_t color, int a, bool motion)
{
    prof::Scope ps(prof::Coverage);
    coverage_impl(layer, nullptr, dst, p, scale, cx, cy, color, a, motion);
}

template <class F, class L>
void coverage_scaled(const SurfaceT<F, L>& layer, const uint8_t* mask, const SurfaceT<F, L>& dst, Rect p, fx scale,
                     int cx, int cy, uint32_t color, int a, bool motion)
{
    prof::Scope ps(prof::Coverage);
    coverage_impl(layer, mask, dst, p, scale, cx, cy, color, a, motion);
}

bool homography_fx(const double* inv, HomographyFx& out)
{
    double s = 0;
    for (int x : {0, kPortraitW})
        for (int y : {0, kPortraitH})
            s = std::max(s, std::fabs(inv[6] * x + inv[7] * y + inv[8]));
    if (!(s > 0))
        return false;
    for (int i = 0; i < 9; i++) {
        double v = inv[i] / s * (double)(1 << 30);
        if (!(std::fabs(v) < 1e17))
            return false;
        out.m[i] = std::llround(v);
    }
    return true;
}

namespace {

void homography_at(int64_t U, int64_t V, int64_t W, int& u, int& v)
{
    constexpr int64_t kFar = (int64_t)1 << 30;
    if (W <= 0) {
        u = v = (int)-kFar;
        return;
    }
    u = (int)std::clamp(U * 65536 / W, -kFar, kFar);
    v = (int)std::clamp(V * 65536 / W, -kFar, kFar);
}

Rect homography_bounds(const double* h, Rect r)
{
    double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
    for (int px : {r.x0, r.x1})
        for (int py : {r.y0, r.y1}) {
            double w = h[6] * px + h[7] * py + h[8];
            if (!(w > 0))
                return kScreen;
            double u = (h[0] * px + h[1] * py + h[2]) / w, v = (h[3] * px + h[4] * py + h[5]) / w;
            x0 = std::min(x0, u);
            y0 = std::min(y0, v);
            x1 = std::max(x1, u);
            y1 = std::max(y1, v);
        }
    auto clamp = [](double v) { return std::clamp(v, -1e6, 1e6); };
    return Rect{(int)std::floor(clamp(x0)), (int)std::floor(clamp(y0)), (int)std::ceil(clamp(x1)),
                (int)std::ceil(clamp(y1))}
        .grow(1);
}

}

void homography_span(const HomographyFx& h, int y, int x0, int n, int* u, int* v)
{
    const int64_t* m = h.m;
    int64_t yy = 2 * (int64_t)y + 1, xx = 2 * (int64_t)x0 + 1;
    int64_t U = m[0] * xx + m[1] * yy + 2 * m[2], V = m[3] * xx + m[4] * yy + 2 * m[5],
            W = m[6] * xx + m[7] * yy + 2 * m[8];
    int ua, va;
    homography_at(U, V, W, ua, va);
    for (int i = 0; i < n; i += 16) {
        U += m[0] * 32;
        V += m[3] * 32;
        W += m[6] * 32;
        int ub, vb;
        homography_at(U, V, W, ub, vb);
        int k = std::min(16, n - i);
        for (int j = 0; j < k; j++) {
            u[i + j] = ua + (int)(((int64_t)(ub - ua) * j) >> 4);
            v[i + j] = va + (int)(((int64_t)(vb - va) * j) >> 4);
        }
        ua = ub;
        va = vb;
    }
}

namespace {

template <bool Green, class F, class L>
void homography_impl(const SurfaceT<F, L>& layer, const uint8_t* mask, const SurfaceT<F, L>& dst, Rect p,
                     const HomographyFx& hf, Rect b, uint32_t color, int a, bool nearest, bool motion)
{
    using T = typename F::T;
    auto green = [](T v) -> uint32_t { return F::unpack(v) >> 8 & 0xFF; };
    auto tap = [&](int x, int y) -> uint32_t {
        if (x < p.x0 || x >= p.x1 || y < p.y0 || y >= p.y1)
            return 0;
        if constexpr (Green)
            return green(layer.row(y)[x]);
        else
            return mask[layer.index(x, y)];
    };
    T c = F::pack(color);
    int ax = b.x0 & ~15, n = b.x1 - ax;
    size_t rs = layer.index(0, 1) - layer.index(0, 0);
    Taps us(n), vs(n);
    for (int y = b.y0; y < b.y1; y++) {
        homography_span(hf, y, ax, n, &us[0], &vs[0]);
        T* row = dst.row(y) + ax;
        for (int i = b.x0 - ax; i < n; i++) {
            int cov;
            if (nearest) {
                cov = (int)tap(us[i] >> 16, vs[i] >> 16);
            } else {
                int uu = us[i] - 32768, vv = vs[i] - 32768, si = uu >> 16, sj = vv >> 16;
                if (si < p.x0 - 1 || si >= p.x1 || sj < p.y0 - 1 || sj >= p.y1)
                    continue;
                uint32_t c00, c10, c01, c11;
                if (si >= p.x0 && si + 1 < p.x1 && sj >= p.y0 && sj + 1 < p.y1) {
                    if constexpr (Green) {
                        const T* r0 = layer.row(sj) + si;
                        const T* r1 = layer.row(sj + 1) + si;
                        c00 = green(r0[0]);
                        c10 = green(r0[1]);
                        c01 = green(r1[0]);
                        c11 = green(r1[1]);
                    } else {
                        const uint8_t* m0 = mask + layer.index(si, sj);
                        c00 = m0[0];
                        c10 = m0[1];
                        c01 = m0[rs];
                        c11 = m0[rs + 1];
                    }
                } else {
                    c00 = tap(si, sj);
                    c10 = tap(si + 1, sj);
                    c01 = tap(si, sj + 1);
                    c11 = tap(si + 1, sj + 1);
                }
                if (!(c00 | c10 | c01 | c11))
                    continue;
                uint32_t fu = ((uint32_t)(uu & 0xFFFF) + 8) >> 4, fv = ((uint32_t)(vv & 0xFFFF) + 8) >> 4;
                cov = (int)(((c00 * (4096 - fu) + c10 * fu) * (4096 - fv) + (c01 * (4096 - fu) + c11 * fu) * fv +
                             (1u << 23)) >>
                            24);
            }
            if (!cov)
                continue;
            int aa = (cov + 1) * a >> 8;
            if constexpr (std::is_same_v<F, R565>)
                row[i] = motion ? lerp565(row[i], c, alpha5(aa)) : blend565(row[i], c, aa);
            else
                row[i] = F::blend(row[i], c, aa);
        }
    }
}

}

template <class F, class L>
void coverage_homography(const SurfaceT<F, L>& layer, const uint8_t* mask, const SurfaceT<F, L>& dst, Rect p,
                         const double* h, uint32_t color, int a, bool nearest, bool motion)
{
    static_assert(!std::is_same_v<L, Rot>);
    prof::Scope ps(prof::Coverage);
    p = p.intersect(layer.rect());
    if (p.empty() || a <= 0)
        return;
    double inv[9];
    HomographyFx hf;
    if (!mat3_invert(h, inv) || !homography_fx(inv, hf))
        return;
    Rect b = homography_bounds(h, nearest ? p : p.grow(1)).intersect(dst.rect());
    if (b.empty())
        return;
    if (mask)
        homography_impl<false>(layer, mask, dst, p, hf, b, color, a, nearest, motion);
    else
        homography_impl<true>(layer, mask, dst, p, hf, b, color, a, nearest, motion);
}

template <class F, class L>
void image_homography(const ImageT<F, L>& img, Rect at, const SurfaceT<F, L>& dst, Rect clip, const double* h,
                      bool nearest)
{
    static_assert(!std::is_same_v<L, Rot>);
    if (at.empty() || img.w != at.x1 - at.x0 || img.h != at.y1 - at.y0)
        return;
    double inv[9];
    HomographyFx hf;
    if (!mat3_invert(h, inv) || !homography_fx(inv, hf))
        return;
    Rect b = homography_bounds(h, at).intersect(clip).intersect(dst.rect());
    if (b.empty())
        return;
    using T = typename F::T;
    auto tap = [&](int x, int y) -> uint32_t { return F::unpack(img.at(x - at.x0, y - at.y0)); };
    int ax = b.x0 & ~15, n = b.x1 - ax;
    Taps us(n), vs(n);
    for (int y = b.y0; y < b.y1; y++) {
        homography_span(hf, y, ax, n, &us[0], &vs[0]);
        T* row = dst.row(y) + ax;
        for (int i = b.x0 - ax; i < n; i++) {
            int pu = us[i] >> 16, pv = vs[i] >> 16;
            if (pu < at.x0 || pu >= at.x1 || pv < at.y0 || pv >= at.y1)
                continue;
            if (nearest || at.x1 - at.x0 < 2 || at.y1 - at.y0 < 2) {
                row[i] = F::pack(tap(pu, pv));
                continue;
            }
            int uu = us[i] - 32768, vv = vs[i] - 32768;
            int si = std::clamp(uu >> 16, at.x0, at.x1 - 2), sj = std::clamp(vv >> 16, at.y0, at.y1 - 2);
            uint32_t fu = uu >> 16 < at.x0 ? 0 : uu >> 16 >= at.x1 - 1 ? 4096 : ((uint32_t)(uu & 0xFFFF) + 8) >> 4;
            uint32_t fv = vv >> 16 < at.y0 ? 0 : vv >> 16 >= at.y1 - 1 ? 4096 : ((uint32_t)(vv & 0xFFFF) + 8) >> 4;
            uint32_t c00 = tap(si, sj), c10 = tap(si + 1, sj), c01 = tap(si, sj + 1), c11 = tap(si + 1, sj + 1);
            auto mix = [&](int sh) -> uint32_t {
                uint32_t p00 = c00 >> sh & 0xFF, p10 = c10 >> sh & 0xFF, p01 = c01 >> sh & 0xFF, p11 = c11 >> sh & 0xFF;
                return ((p00 * (4096 - fu) + p10 * fu) * (4096 - fv) + (p01 * (4096 - fu) + p11 * fu) * fv + (1u << 23)) >>
                       24;
            };
            row[i] = F::pack(mix(16) << 16 | mix(8) << 8 | mix(0));
        }
    }
}

template <class F, class L>
void blend_scaled(const SurfaceT<F, L>& layer, const SurfaceT<F, L>& dst, fx scale, int cx, int cy, uint32_t bg,
                  int a, bool motion)
{
    prof::Scope ps(prof::BlendScaled);
    blend_scaled_impl(layer, nullptr, dst, scale, cx, cy, bg, a, motion);
}

template <class F, class L>
void blend_scaled(const SurfaceT<F, L>& layer, const uint8_t* mask, const SurfaceT<F, L>& dst, fx scale, int cx,
                  int cy, uint32_t ground, int a, bool motion)
{
    prof::Scope ps(prof::BlendScaled);
    blend_scaled_impl(layer, mask, dst, scale, cx, cy, ground, a, motion);
}

template <class F, class L>
void blend_scaled_rect(const SurfaceT<F, L>& layer, const uint8_t* mask, Rect src, const SurfaceT<F, L>& dst, Rect clip,
                       fx scale, fx tx, fx ty, uint32_t ground, int a, bool motion)
{
    prof::Scope ps(prof::BlendScaled);
    rect_impl(layer, mask, src, dst, clip, scale, tx, ty, ground, a, motion);
}

template <class F, class L>
void scrim_scaled(const SurfaceT<F, L>& page, const SurfaceT<F, L>& layer, const SurfaceT<F, L>& dst, uint32_t color,
                  int scrim_a, fx scale, int cx, int cy, uint32_t bg, int a, bool motion)
{
    scrim(page, dst, color, scrim_a, motion);
    blend_scaled(layer, dst, scale, cx, cy, bg, a, motion);
}

template <class F, class L>
void scrim_scaled(const SurfaceT<F, L>& page, const SurfaceT<F, L>& layer, const uint8_t* mask,
                  const SurfaceT<F, L>& dst, uint32_t color, int scrim_a, fx scale, int cx, int cy, uint32_t ground,
                  int a, bool motion)
{
    if constexpr (!std::is_same_v<F, R565>) {
        scrim(page, dst, color, scrim_a, motion);
        blend_scaled(layer, mask, dst, scale, cx, cy, ground, a, motion);
    } else {
        prof::Scope ps(prof::ScrimScaled);
        scrim_scaled_impl(page, layer, mask, dst, color, scrim_a, scale, cx, cy, ground, a, motion);
    }
}

template <class F, class L>
void blend_scaled_premul(const SurfaceT<F, L>& layer, const uint8_t* alpha, const SurfaceT<F, L>& dst, fx scale, int cx,
                         int cy, int a)
{
    prof::Scope ps(prof::BlendScaledPremul);
    if (a <= 0 || scale <= 0)
        return;
    if constexpr (std::is_same_v<L, Rot>) {
        int64_t inv = ((int64_t)kOne << kFxBits) / scale;
        Taps src_col(dst.h);
        for (int y = 0; y < dst.h; y++) {
            int sy = cy + (int)(((int64_t)(y - cy) * inv + kHalf) >> kFxBits);
            src_col[y] = sy >= 0 && sy < layer.h ? sy : -1;
        }
        for (int x = 0; x < dst.w; x++) {
            int sx = cx + (int)(((int64_t)(x - cx) * inv + kHalf) >> kFxBits);
            if (sx < 0 || sx >= layer.w)
                continue;
            int sr = layer.w - 1 - sx;
            const uint32_t* s = layer.mem_row(sr);
            const uint8_t* al = alpha + (size_t)sr * layer.stride;
            uint32_t* row = dst.mem_row(dst.w - 1 - x);
            for (int y = 0; y < dst.h; y++) {
                int sc = src_col[y];
                if (sc < 0 || !al[sc])
                    continue;
                row[y] = premul888(row[y], s[sc], al[sc], a);
            }
        }
    } else {
        premul_impl<F, L>(nullptr, nullptr, 0, layer, alpha, dst, scale, cx, cy, a, false);
    }
}

template <class F, class L>
void mix_scaled_premul(const SurfaceT<F, L>& under, const SurfaceT<F, L>& over, int mix_a, const SurfaceT<F, L>& layer,
                       const uint8_t* alpha, const SurfaceT<F, L>& dst, fx scale, int cx, int cy, int a, bool motion)
{
    prof::Scope ps(prof::MixScaledPremul);
    if constexpr (std::is_same_v<L, Rot>) {
        mix(under, over, dst, mix_a);
        blend_scaled_premul(layer, alpha, dst, scale, cx, cy, a);
    } else {
        premul_impl<F, L>(&under, &over, mix_a, layer, alpha, dst, scale, cx, cy, a, motion);
    }
}

template <class F, class L>
void precompose_premul(const SurfaceT<F, L>& under, const SurfaceT<F, L>& layer, const uint8_t* alpha,
                       const SurfaceT<F, L>& dst, Rect band)
{
    prof::Scope ps(prof::BlendScaledPremul);
    Rect b = band.intersect(dst.rect()).intersect(layer.rect());
    if (b.empty())
        return;
    if constexpr (std::is_same_v<L, Rot>) {
        for (int y = b.y0; y < b.y1; y++)
            for (int x = b.x0; x < b.x1; x++) {
                size_t i = layer.index(x, y);
                int m = alpha[i];
                uint32_t u = under.at(x, y);
                dst.at(x, y) = m >= 255 ? layer.at(x, y) : m ? premul888(u, layer.at(x, y), m, 256) : u;
            }
    } else {
        using T = typename F::T;
        for (int y = b.y0; y < b.y1; y++) {
            const T* u = under.row(y);
            const T* l = layer.row(y);
            const uint8_t* al = alpha + layer.index(0, y);
            T* d = dst.row(y);
            auto px = [&](int x) -> T {
                int m = al[x];
                if constexpr (std::is_same_v<F, R565>)
                    return m >= 255 ? l[x] : m ? premul565(u[x], l[x], m, 256 - (uint32_t)(m + 1), 256) : u[x];
                else
                    return m >= 255 ? l[x] : m ? premul888(u[x], l[x], m, 256) : u[x];
            };
            if constexpr (std::is_same_v<F, R565>)
                pair_span(d, b.x0, b.x1, px);
            else
                for (int x = b.x0; x < b.x1; x++)
                    d[x] = px(x);
        }
    }
}

template <class F, class L>
void mix_scaled(const SurfaceT<F, L>& page, const SurfaceT<F, L>& q, const SurfaceT<F, L>& dst, int a, fx scale,
                int cx, int cy, bool motion)
{
    prof::Scope ps(prof::Mix);
    using T = typename F::T;
    a = std::clamp(a, 0, 256);
    bool on = a > 0 && scale > 0;
    int64_t inv = on ? ((int64_t)kOne << kFxBits) / scale : kOne;
    Taps src_x(dst.w);
    int x0 = dst.w, x1 = dst.w;
    if (on)
        for (int x = 0; x < dst.w; x++) {
            int sx = unscale(x, cx, inv);
            src_x[x] = sx >= 0 && sx < q.w ? sx : -1;
            if (src_x[x] >= 0) {
                x0 = std::min(x0, x);
                x1 = x + 1;
            }
        }
    if constexpr (std::is_same_v<L, Rot>) {
        for (int y = 0; y < dst.h; y++) {
            int sy = on ? unscale(y, cy, inv) : -1;
            bool in = sy >= 0 && sy < q.h;
            for (int x = 0; x < dst.w; x++) {
                T p = page.at(x, y);
                if (in && x >= x0 && x < x1) {
                    T v = q.at(src_x[x], sy);
                    p = a >= 256 ? v : F::blend(p, v, a);
                }
                dst.at(x, y) = p;
            }
        }
    } else if constexpr (std::is_same_v<F, R565>) {
        if (a >= 256)
            mix_scaled565<0>(page, q, dst, 0, on, src_x.p, x0, x1, inv, cy);
        else if (motion)
            mix_scaled565<1>(page, q, dst, alpha5(a), on, src_x.p, x0, x1, inv, cy);
        else
            mix_scaled565<2>(page, q, dst, a, on, src_x.p, x0, x1, inv, cy);
    } else {
        auto run = [&](auto px) {
            for (int y = 0; y < dst.h; y++) {
                const T* s = page.row(y);
                T* d = dst.row(y);
                int sy = on ? unscale(y, cy, inv) : -1;
                if (sy < 0 || sy >= q.h) {
                    if (s != d)
                        copy_px(d, s, dst.w);
                    continue;
                }
                const T* l = q.row(sy);
                if (s != d) {
                    copy_px(d, s, x0);
                    copy_px(d + x1, s + x1, dst.w - x1);
                }
                for (int x = x0; x < x1; x++)
                    d[x] = px(s[x], l[src_x[x]]);
            }
        };
        if (a >= 256)
            run([](T, T v) { return v; });
        else
            run([a](T p, T v) { return F::blend(p, v, a); });
    }
}

template <class F, class L>
void mix(const SurfaceT<F, L>& under, const SurfaceT<F, L>& over, const SurfaceT<F, L>& dst, int a, bool motion)
{
    prof::Scope ps(prof::Mix);
    if constexpr (std::is_same_v<L, Rot>) {
        for (int r = 0; r < dst.w; r++) {
            const uint32_t* u = under.mem_row(r);
            const uint32_t* o = over.mem_row(r);
            uint32_t* d = dst.mem_row(r);
            if (a <= 0)
                copy_px(d, u, dst.h);
            else if (a >= 256)
                copy_px(d, o, dst.h);
            else
                for (int j = 0; j < dst.h; j++)
                    d[j] = blend_px(u[j], o[j], a);
        }
    } else {
        using T = typename F::T;
        int k = alpha5(a);
        for (int y = 0; y < dst.h; y++) {
            const T* u = under.row(y);
            const T* o = over.row(y);
            T* d = dst.row(y);
            if (a <= 0)
                copy_px(d, u, dst.w);
            else if (a >= 256)
                copy_px(d, o, dst.w);
            else if constexpr (std::is_same_v<F, R565>) {
                if (motion)
                    for (int x = 0; x < dst.w; x++)
                        d[x] = lerp565(u[x], o[x], k);
                else
                    for (int x = 0; x < dst.w; x++)
                        d[x] = blend565(u[x], o[x], a);
            } else
                for (int x = 0; x < dst.w; x++)
                    d[x] = F::blend(u[x], o[x], a);
        }
    }
}

template <class F, class L>
void blend_band(const SurfaceT<F, L>& layer, const SurfaceT<F, L>& dst, Rect b, int dx, int a)
{
    prof::Scope ps(prof::BlendBand);
    b = b.intersect(dst.rect());
    if (b.empty() || a <= 0)
        return;
    if constexpr (std::is_same_v<L, Rot>) {
        for (int x = b.x0; x < b.x1; x++) {
            int sx = x - dx;
            if (sx < 0 || sx >= layer.w)
                continue;
            const uint32_t* s = layer.mem_row(layer.w - 1 - sx);
            uint32_t* d = dst.mem_row(dst.w - 1 - x);
            if (a >= 256) {
                copy_px(d + b.y0, s + b.y0, b.y1 - b.y0);
                continue;
            }
            for (int y = b.y0; y < b.y1; y++)
                d[y] = blend_px(d[y], s[y], a);
        }
    } else {
        using T = typename F::T;
        int x0 = std::max(b.x0, dx), x1 = std::min(b.x1, layer.w + dx);
        if (x1 <= x0)
            return;
        for (int y = b.y0; y < b.y1; y++) {
            const T* s = layer.row(y) + x0 - dx;
            T* d = dst.row(y) + x0;
            if (a >= 256)
                copy_px(d, s, x1 - x0);
            else
                for (int i = 0; i < x1 - x0; i++)
                    d[i] = F::blend(d[i], s[i], a);
        }
    }
}

#define FD_COMPOSE(F, L)                                                                                              \
    template void scrim<F, L>(const SurfaceT<F, L>&, const SurfaceT<F, L>&, uint32_t, int, bool);                     \
    template void scrim<F, L>(const SurfaceT<F, L>&, const SurfaceT<F, L>&, Rect, uint32_t, int, bool);               \
    template void blend_shifted<F, L>(const SurfaceT<F, L>&, const SurfaceT<F, L>&, Rect, int, int, bool);            \
    template void scrim_shifted<F, L>(const SurfaceT<F, L>&, const SurfaceT<F, L>&, const SurfaceT<F, L>&, uint32_t,  \
                                      int, Rect, int, int, bool, const PageShift<F, L>&);                             \
    template void coverage_scaled<F, L>(const SurfaceT<F, L>&, const uint8_t*, const SurfaceT<F, L>&, Rect, fx, int,  \
                                        int, uint32_t, int, bool);                                                    \
    template void blend_scaled<F, L>(const SurfaceT<F, L>&, const uint8_t*, const SurfaceT<F, L>&, fx, int, int,      \
                                     uint32_t, int, bool);                                                            \
    template void blend_scaled_rect<F, L>(const SurfaceT<F, L>&, const uint8_t*, Rect, const SurfaceT<F, L>&, Rect,   \
                                          fx, fx, fx, uint32_t, int, bool);                                           \
    template void scrim_scaled<F, L>(const SurfaceT<F, L>&, const SurfaceT<F, L>&, const uint8_t*,                    \
                                     const SurfaceT<F, L>&, uint32_t, int, fx, int, int, uint32_t, int, bool);        \
    template void blend_scaled_premul<F, L>(const SurfaceT<F, L>&, const uint8_t*, const SurfaceT<F, L>&, fx, int,    \
                                            int, int);                                                                \
    template void mix_scaled_premul<F, L>(const SurfaceT<F, L>&, const SurfaceT<F, L>&, int, const SurfaceT<F, L>&,   \
                                          const uint8_t*, const SurfaceT<F, L>&, fx, int, int, int, bool);            \
    template void mix<F, L>(const SurfaceT<F, L>&, const SurfaceT<F, L>&, const SurfaceT<F, L>&, int, bool);          \
    template void precompose_premul<F, L>(const SurfaceT<F, L>&, const SurfaceT<F, L>&, const uint8_t*,               \
                                          const SurfaceT<F, L>&, Rect);                                               \
    template void mix_scaled<F, L>(const SurfaceT<F, L>&, const SurfaceT<F, L>&, const SurfaceT<F, L>&, int, fx, int, \
                                   int, bool);                                                                        \
    template void blend_band<F, L>(const SurfaceT<F, L>&, const SurfaceT<F, L>&, Rect, int, int);                   \
    template void scrim_shifted_rows<F, L>(const SurfaceT<F, L>&, const SurfaceT<F, L>&, const SurfaceT<F, L>&,       \
                                           uint32_t, int, Rect, int, int, int, int, const PageShift<F, L>&);          \
    template void scrim_coverage_rows<F, L>(const SurfaceT<F, L>&, const SurfaceT<F, L>&, const SurfaceT<F, L>&,      \
                                            uint32_t, int, Rect, fx, int, int, uint32_t, int, int, int);

#define FD_COMPOSE_KEYED(F, L)                                                                                        \
    template void coverage_scaled<F, L>(const SurfaceT<F, L>&, const SurfaceT<F, L>&, Rect, fx, int, int, uint32_t,   \
                                        int, bool);                                                                   \
    template void blend_scaled<F, L>(const SurfaceT<F, L>&, const SurfaceT<F, L>&, fx, int, int, uint32_t, int,       \
                                     bool);                                                                           \
    template void scrim_scaled<F, L>(const SurfaceT<F, L>&, const SurfaceT<F, L>&, const SurfaceT<F, L>&, uint32_t,   \
                                     int, fx, int, int, uint32_t, int, bool);

FD_COMPOSE(X888, Rot)
FD_COMPOSE(X888, Rows)
FD_COMPOSE(R565, Rows)
FD_COMPOSE_KEYED(X888, Rot)
FD_COMPOSE_KEYED(X888, Rows)

#define FD_COMPOSE_ROWS(F, L)                                                                                         \
    template void coverage_homography<F, L>(const SurfaceT<F, L>&, const uint8_t*, const SurfaceT<F, L>&, Rect,       \
                                            const double*, uint32_t, int, bool, bool);                              \
    template void image_homography<F, L>(const ImageT<F, L>&, Rect, const SurfaceT<F, L>&, Rect, const double*, bool);

FD_COMPOSE_ROWS(X888, Rows)
FD_COMPOSE_ROWS(R565, Rows)

}
