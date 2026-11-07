#include "warp.h"

#include <algorithm>

#include "../core/curves.h"
#include "../core/prof.h"
#include "pairs.h"

namespace fd {

void turnstile_rows(fx angle_deg, int hinge_px, int d_px, WarpRow* rows)
{
    fx c = cos_deg(angle_deg), sn = sin_deg(angle_deg);
    for (int x = 0; x < kPortraitW; x++) {
        WarpRow& r = rows[kPortraitW - 1 - x];
        int X = x - hinge_px;
        int64_t den = c - (int64_t)X * sn / d_px;
        if (den <= 0) {
            r.src = -1;
            continue;
        }
        int64_t a = (int64_t)X * ((int64_t)1 << (2 * kFxBits)) / den;
        int sx = hinge_px + (int)((a + kHalf) >> kFxBits);
        if (sx < 0 || sx >= kPortraitW) {
            r.src = -1;
            continue;
        }
        r.src = kPortraitW - 1 - sx;
        r.step = (fx)(((int64_t)c << kFxBits) / den);
    }
}

namespace {

constexpr int channel(uint32_t rgb, int shift) { return rgb >> shift & 0xFF; }

uint32_t lerp8(uint32_t d, uint32_t s, int a)
{
    uint32_t out = 0;
    for (int sh : {16, 8, 0})
        out |= (uint32_t)((channel(s, sh) * a + channel(d, sh) * (256 - a) + 128) >> 8) << sh;
    return out;
}

int64_t floor_div(int64_t a, int64_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

struct EyeRun {
    int y0 = 0, n = 0;
    uint32_t acc = 0;
};

EyeRun eye_run(int h, fx step, int eye)
{
    EyeRun r;
    int64_t e = (int64_t)eye << kFxBits;
    if (step <= 0) {
        if (step == 0 && eye >= 0 && eye < h) {
            r.n = h;
            r.acc = (uint32_t)e;
        }
        return r;
    }
    int64_t up = std::min<int64_t>(eye, floor_div(e, step)), a0 = e - up * step, y0 = eye - up;
    int64_t end = (int64_t)h << kFxBits;
    if (a0 >= end || y0 >= h)
        return r;
    r.y0 = (int)y0;
    r.n = (int)std::min<int64_t>(h - y0, (end - 1 - a0) / step + 1);
    r.acc = (uint32_t)a0;
    return r;
}

int row_span(int w, int lw, fx step)
{
    if (step < 0)
        return 0;
    if (step == 0)
        return w;
    int64_t lim = ((int64_t)lw << kFxBits) - kHalf;
    return (int)std::clamp<int64_t>((lim + step - 1) / step, 0, w);
}

}

template <class F, class L>
void warp(const SurfaceT<F, L>& src, const SurfaceT<F, L>& dst, const WarpRow* rows, uint32_t bg, int a, bool motion,
          int eye)
{
    prof::Scope ps(prof::Warp);
    using T = typename F::T;
    if constexpr (std::is_same_v<L, Rot>) {
        for (int row = 0; row < dst.w; row++) {
            uint32_t* d = dst.mem_row(row);
            const WarpRow& r = rows[row];
            if (r.src < 0 || a <= 0) {
                fill_px(d, bg, dst.h);
                continue;
            }
            const uint32_t* s = src.mem_row(r.src);
            EyeRun e = eye_run(dst.h, r.step, eye);
            if (e.y0)
                fill_px(d, bg, e.y0);
            d += e.y0;
            int n = e.n, tail = dst.h - e.y0;
            uint32_t acc = e.acc, step = (uint32_t)r.step;
            int i = 0;
            if (a >= 256) {
                if (step == (uint32_t)kOne) {
                    copy_px(d, s + (acc >> kFxBits), n);
                    i = n;
                } else {
                    for (; i + 4 <= n; i += 4) {
                        d[i] = s[acc >> kFxBits];
                        acc += step;
                        d[i + 1] = s[acc >> kFxBits];
                        acc += step;
                        d[i + 2] = s[acc >> kFxBits];
                        acc += step;
                        d[i + 3] = s[acc >> kFxBits];
                        acc += step;
                    }
                    for (; i < n; i++, acc += step)
                        d[i] = s[acc >> kFxBits];
                }
            } else {
                for (; i < n; i++, acc += step)
                    d[i] = blend_px(bg, s[acc >> kFxBits], a);
            }
            if (n < tail)
                fill_px(d + n, bg, tail - n);
        }
    } else {
        struct Col {
            const T* p;
            uint32_t acc, step;
            int y0, n;
        };
        int w = dst.w, h = dst.h;
        T g = F::pack(bg);
        std::vector<Col> cols(w);
        for (int x = 0; x < w; x++) {
            const WarpRow& r = rows[w - 1 - x];
            Col& k = cols[x];
            EyeRun e = r.src < 0 || a <= 0 ? EyeRun{} : eye_run(h, r.step, eye);
            k.acc = e.acc;
            k.step = (uint32_t)r.step;
            k.y0 = e.y0;
            k.n = e.n;
            k.p = k.n ? src.px + (w - 1 - r.src) : nullptr;
        }
        size_t stride = src.stride;
        auto run = [&](auto px, auto at_eye) {
            for (int y = 0; y < h; y++) {
                T* d = dst.row(y);
                for (int x = 0; x < w; x++) {
                    Col& k = cols[x];
                    if constexpr (decltype(at_eye)::value) {
                        if ((uint32_t)(y - k.y0) >= (uint32_t)k.n) {
                            d[x] = g;
                            continue;
                        }
                    } else if (y >= k.n) {
                        d[x] = g;
                        continue;
                    }
                    T v = k.p[(k.acc >> kFxBits) * stride];
                    k.acc += k.step;
                    d[x] = px(v);
                }
            }
        };
        auto go = [&](auto px) {
            if (eye)
                run(px, std::true_type{});
            else
                run(px, std::false_type{});
        };
        if (a >= 256)
            go([](T v) { return v; });
        else if constexpr (std::is_same_v<F, R565>) {
            int k5 = alpha5(a);
            if (motion)
                go([&](T v) { return lerp565(g, v, k5); });
            else
                go([&](T v) { return blend565(g, v, a); });
        } else
            go([&](T v) { return F::blend(g, v, a); });
    }
}

Rect projected_bounds(Rect s, const Projection& pr)
{
    fx c = cos_deg(pr.angle), sn = sin_deg(pr.angle);
    Rect out{kPortraitW, kPortraitH, 0, 0};
    for (int x : {s.x0, s.x1})
        for (int y : {s.y0, s.y1}) {
            int64_t X0 = x - pr.hinge;
            int64_t w = kOne + X0 * sn / pr.d;
            if (w <= 0)
                return kScreen;
            int64_t lx = ((int64_t)pr.hinge << kFxBits) + X0 * c * kOne / w;
            int64_t ly = ((int64_t)y << (2 * kFxBits)) / w;
            int dx = (int)((pr.x + (lx * pr.scale >> kFxBits)) >> kFxBits);
            int dy = (int)((pr.y + (ly * pr.scale >> kFxBits)) >> kFxBits);
            out = {std::min(out.x0, dx), std::min(out.y0, dy), std::max(out.x1, dx), std::max(out.y1, dy)};
        }
    return out.grow(2);
}

namespace {

template <class F, class L, class Out>
void project_core(const SurfaceT<F, L>& src, Rect s, Rect d, const Projection& pr, uint32_t bg, Out out)
{
    if (d.empty() || pr.scale <= 0)
        return;
    int n = std::clamp((int)((kOne + pr.scale - 1) / pr.scale), 1, 4);
    int total = n * n;
    fx c = cos_deg(pr.angle), sn = sin_deg(pr.angle);
    int64_t inv = ((int64_t)kOne << kFxBits) / pr.scale;
    int64_t sub[4];
    for (int i = 0; i < n; i++)
        sub[i] = ((int64_t)(2 * i + 1) << kFxBits) / (2 * n);
    for (int x = d.x0; x < d.x1; x++) {
        int sx[4];
        int64_t step[4];
        int live = 0;
        for (int i = 0; i < n; i++) {
            sx[i] = -1;
            int64_t xl = ((((int64_t)x << kFxBits) + sub[i] - pr.x) * inv) >> kFxBits;
            int64_t xp = xl - ((int64_t)pr.hinge << kFxBits);
            int64_t den = c - ((xp * sn) >> kFxBits) / pr.d;
            if (den <= 0)
                continue;
            int v = pr.hinge + (int)((xp * kOne / den) >> kFxBits);
            if (v < s.x0 || v >= s.x1)
                continue;
            sx[i] = v;
            step[i] = ((((int64_t)c << kFxBits) / den) * inv) >> kFxBits;
            live++;
        }
        if (!live)
            continue;
        for (int y = d.y0; y < d.y1; y++) {
            uint32_t r = 0, g = 0, b = 0;
            int k = 0;
            for (int j = 0; j < n; j++) {
                int64_t Y = ((int64_t)y << kFxBits) + sub[j] - pr.y;
                for (int i = 0; i < n; i++) {
                    if (sx[i] < 0)
                        continue;
                    int sy = (int)((Y * step[i]) >> (2 * kFxBits));
                    if (sy < s.y0 || sy >= s.y1)
                        continue;
                    uint32_t v = src.mem_row(src.w - 1 - sx[i])[sy];
                    if (v == bg)
                        continue;
                    r += v >> 16 & 0xFF;
                    g += v >> 8 & 0xFF;
                    b += v & 0xFF;
                    k++;
                }
            }
            if (k)
                out(x, y, (r / k) << 16 | (g / k) << 8 | b / k, k * 256 / total);
        }
    }
}

template <class F, class Out>
void project_rows(const SurfaceT<F, Rows>& src, const uint8_t* mask, Rect s, Rect d, const Projection& pr, uint32_t bg,
                  Out out)
{
    using T = typename F::T;
    if (d.empty() || pr.scale <= 0)
        return;
    int n = std::clamp((int)((kOne + pr.scale - 1) / pr.scale), 1, 4);
    int total = n * n;
    fx c = cos_deg(pr.angle), sn = sin_deg(pr.angle);
    int64_t inv = ((int64_t)kOne << kFxBits) / pr.scale;
    int64_t sub[4];
    for (int i = 0; i < n; i++)
        sub[i] = ((int64_t)(2 * i + 1) << kFxBits) / (2 * n);
    struct Col {
        int sx[4];
        int64_t step[4];
        bool live;
    };
    std::vector<Col> cols(d.x1 - d.x0);
    for (int x = d.x0; x < d.x1; x++) {
        Col& k = cols[x - d.x0];
        k.live = false;
        for (int i = 0; i < n; i++) {
            k.sx[i] = -1;
            int64_t xl = ((((int64_t)x << kFxBits) + sub[i] - pr.x) * inv) >> kFxBits;
            int64_t xp = xl - ((int64_t)pr.hinge << kFxBits);
            int64_t den = c - ((xp * sn) >> kFxBits) / pr.d;
            if (den <= 0)
                continue;
            int v = pr.hinge + (int)((xp * kOne / den) >> kFxBits);
            if (v < s.x0 || v >= s.x1)
                continue;
            k.sx[i] = v;
            k.step[i] = ((((int64_t)c << kFxBits) / den) * inv) >> kFxBits;
            k.live = true;
        }
    }
    T key = F::pack(bg);
    uint32_t ground = F::unpack(key);
    int kr = channel(ground, 16), kg = channel(ground, 8), kb = channel(ground, 0);
    for (int y = d.y0; y < d.y1; y++) {
        int64_t Y[4];
        for (int j = 0; j < n; j++)
            Y[j] = ((int64_t)y << kFxBits) + sub[j] - pr.y;
        for (int x = d.x0; x < d.x1; x++) {
            const Col& k = cols[x - d.x0];
            if (!k.live)
                continue;
            uint32_t r = 0, g = 0, b = 0, w = 0;
            for (int j = 0; j < n; j++)
                for (int i = 0; i < n; i++) {
                    if (k.sx[i] < 0)
                        continue;
                    int sy = (int)((Y[j] * k.step[i]) >> (2 * kFxBits));
                    if (sy < s.y0 || sy >= s.y1)
                        continue;
                    size_t at = src.index(k.sx[i], sy);
                    T p = src.px[at];
                    uint32_t v = F::unpack(p);
                    if (!mask) {
                        if (p == key)
                            continue;
                        r += channel(v, 16);
                        g += channel(v, 8);
                        b += channel(v, 0);
                        w++;
                        continue;
                    }
                    int m = mask[at];
                    if (!m)
                        continue;
                    r += std::max(0, channel(v, 16) * 255 - kr * (255 - m));
                    g += std::max(0, channel(v, 8) * 255 - kg * (255 - m));
                    b += std::max(0, channel(v, 0) * 255 - kb * (255 - m));
                    w += m;
                }
            if (!w)
                continue;
            if (!mask) {
                out(x, y, (r / w) << 16 | (g / w) << 8 | b / w, (int)(w * 256 / total));
                continue;
            }
            uint32_t half = w / 2;
            r = std::min<uint32_t>(255, (r + half) / w);
            g = std::min<uint32_t>(255, (g + half) / w);
            b = std::min<uint32_t>(255, (b + half) / w);
            out(x, y, r << 16 | g << 8 | b, (int)(w * 256 / (255 * total)));
        }
    }
}

template <class F> uint32_t compose_rgb(typename F::T o, uint32_t v, int aa)
{
    if constexpr (std::is_same_v<F, X888>)
        return aa >= 256 ? v : blend_px(o, v, aa);
    else
        return F::pack(aa >= 256 ? v : lerp8(F::unpack(o), v, aa));
}

template <class F, class L>
void project_into(const SurfaceT<F, L>& src, const uint8_t* mask, Rect s, const SurfaceT<F, L>& dst, Rect clip,
                  const Projection& pr, uint32_t bg, int a)
{
    if (a <= 0)
        return;
    Rect d = projected_bounds(s, pr).intersect(clip);
    if constexpr (std::is_same_v<L, Rot>) {
        project_core(src, s, d, pr, bg, [&](int x, int y, uint32_t v, int cov) {
            uint32_t& o = dst.mem_row(dst.w - 1 - x)[y];
            int aa = a * cov >> 8;
            o = aa >= 256 ? v : blend_px(o, v, aa);
        });
    } else {
        project_rows(src, mask, s, d, pr, bg, [&](int x, int y, uint32_t v, int cov) {
            typename F::T& o = dst.at(x, y);
            o = (typename F::T)compose_rgb<F>(o, v, a * cov >> 8);
        });
    }
}

template <class F, class L>
void sprite_into(const SurfaceT<F, L>& src, const uint8_t* mask, Rect s, const Projection& pr, uint32_t bg,
                 SpriteT<F, L>& out)
{
    out.r = projected_bounds(s, pr).intersect(kScreen);
    int h = out.r.y1 - out.r.y0, w = out.r.x1 - out.r.x0;
    out.px.assign(out.r.empty() ? 0 : (size_t)w * h, 0);
    if constexpr (std::is_same_v<L, Rot>) {
        project_core(src, s, out.r, pr, bg, [&](int x, int y, uint32_t v, int cov) {
            out.px[(size_t)(x - out.r.x0) * h + (y - out.r.y0)] = (uint32_t)std::min(cov, 255) << 24 | v;
        });
    } else {
        project_rows(src, mask, s, out.r, pr, bg, [&](int x, int y, uint32_t v, int cov) {
            out.px[(size_t)(y - out.r.y0) * w + (x - out.r.x0)] =
                (uint32_t)std::min(cov, 255) << kSpriteShift<F> | F::pack(v);
        });
    }
}

void swivel_motion565(const SurfaceT<R565, Rows>& page, const SurfaceT<R565, Rows>& layer, const uint8_t* mask,
                      const SurfaceT<R565, Rows>& dst, const SwivelCol* cols, uint16_t g, int kc, int kb, int kv,
                      int a)
{
    for (int y = 0; y < dst.h; y++) {
        const uint16_t* s = page.row(y);
        uint16_t* d = dst.row(y);
        const SwivelCol& k = cols[y];
        int n = k.src >= 0 && a > 0 ? row_span(dst.w, layer.w, k.w) : 0;
        const uint16_t* l = n ? layer.row(k.src) : nullptr;
        const uint8_t* m = n && mask ? mask + layer.index(0, k.src) : nullptr;
        int32_t acc = kHalf, step = k.w;
        int x = 0;
        bool pairs = (((uintptr_t)s ^ (uintptr_t)d) & 2) == 0;
        if (pairs && n > 0 && (uintptr_t)d & 2) {
            int sx = acc >> kFxBits;
            acc += step;
            uint16_t v = l[sx];
            bool ground = m ? !m[sx] : v == g;
            d[0] = ground ? lerp565(s[0], g, kc) : lerp565(lerp565(s[0], g, kb), v, kv);
            x = 1;
        }
        if (pairs)
            for (; x + 1 < n; x += 2) {
                int sx0 = acc >> kFxBits, sx1 = (acc + step) >> kFxBits;
                acc += 2 * step;
                uint16_t v0 = l[sx0], v1 = l[sx1];
                bool g0 = m ? !m[sx0] : v0 == g, g1 = m ? !m[sx1] : v1 == g;
                uint32_t pp = *(const PixPair*)(s + x);
                uint16_t p0 = (uint16_t)pp, p1 = (uint16_t)(pp >> 16);
                uint32_t q0 = g0 ? lerp565(p0, g, kc) : lerp565(lerp565(p0, g, kb), v0, kv);
                uint32_t q1 = g1 ? lerp565(p1, g, kc) : lerp565(lerp565(p1, g, kb), v1, kv);
                *(PixPair*)(d + x) = q0 | q1 << 16;
            }
        for (; x < n; x++) {
            int sx = acc >> kFxBits;
            acc += step;
            uint16_t v = l[sx];
            bool ground = m ? !m[sx] : v == g;
            d[x] = ground ? lerp565(s[x], g, kc) : lerp565(lerp565(s[x], g, kb), v, kv);
        }
        if (pairs && x < dst.w && (uintptr_t)(d + x) & 2) {
            d[x] = lerp565(s[x], g, kb);
            x++;
        }
        if (pairs)
            for (; x + 1 < dst.w; x += 2) {
                uint32_t pp = *(const PixPair*)(s + x);
                *(PixPair*)(d + x) =
                    (uint32_t)lerp565((uint16_t)pp, g, kb) | (uint32_t)lerp565((uint16_t)(pp >> 16), g, kb) << 16;
            }
        for (; x < dst.w; x++)
            d[x] = lerp565(s[x], g, kb);
    }
}

template <class F, class L>
void swivel_into(const SurfaceT<F, L>& page, const SurfaceT<F, L>& layer, const uint8_t* mask, const SurfaceT<F, L>& dst,
                 const SwivelCol* cols, uint32_t bg, int base_a, int a, bool motion)
{
    using T = typename F::T;
    int comb = 256 - ((256 - base_a) * (256 - a) >> 8);
    if constexpr (std::is_same_v<L, Rot>) {
        for (int r = 0; r < dst.w; r++) {
            int xs = dst.w - 1 - r;
            const uint32_t* s = page.mem_row(r);
            uint32_t* d = dst.mem_row(r);
            for (int j = 0; j < dst.h; j++) {
                const SwivelCol& k = cols[j];
                uint32_t v = bg;
                bool in = false;
                if (k.src >= 0 && a > 0) {
                    int sx = (int)(((int64_t)xs * k.w + kHalf) >> kFxBits);
                    if (sx < layer.w) {
                        v = layer.mem_row(layer.w - 1 - sx)[k.src];
                        in = true;
                    }
                }
                int ba = in && v == bg ? comb : base_a;
                uint32_t base = ba >= 256 ? bg : ba <= 0 ? s[j] : blend_px(s[j], bg, ba);
                if (in && v != bg)
                    base = a >= 256 ? v : blend_px(base, v, a);
                d[j] = base;
            }
        }
    } else {
        T g = F::pack(bg);
        auto dim = [&](T p, int ba) -> T { return ba >= 256 ? g : ba <= 0 ? p : F::blend(p, g, ba); };
        int A = std::min(a, 256), B = std::clamp(base_a, 0, 256);
        uint32_t wp = (uint32_t)(256 - B) * (256 - A), wg = (uint32_t)B * (256 - A), wv = (uint32_t)A * 256;
        uint32_t gr = (uint32_t)(g >> 11) * wg + 32768, gm = (uint32_t)(g >> 5 & 63) * wg + 32768,
                 gb = (uint32_t)(g & 31) * wg + 32768;
        auto cover = [&](T p, T v) -> T {
            if constexpr (std::is_same_v<F, X888>) {
                T base = dim(p, base_a);
                return a >= 256 ? v : blend_px(base, v, a);
            } else {
                uint32_t r = ((uint32_t)(p >> 11) * wp + (uint32_t)(v >> 11) * wv + gr) >> 16;
                uint32_t m = ((uint32_t)(p >> 5 & 63) * wp + (uint32_t)(v >> 5 & 63) * wv + gm) >> 16;
                uint32_t b = ((uint32_t)(p & 31) * wp + (uint32_t)(v & 31) * wv + gb) >> 16;
                return (T)(r << 11 | m << 5 | b);
            }
        };
        auto run = [&](auto dim_comb, auto dim_base, auto over) {
            for (int y = 0; y < dst.h; y++) {
                const T* s = page.row(y);
                T* d = dst.row(y);
                const SwivelCol& k = cols[y];
                int n = k.src >= 0 && a > 0 ? row_span(dst.w, layer.w, k.w) : 0;
                const T* l = n ? layer.row(k.src) : nullptr;
                const uint8_t* m = n && mask ? mask + layer.index(0, k.src) : nullptr;
                int32_t acc = kHalf, step = k.w;
                auto tap = [&](int x) -> T {
                    int sx = acc >> kFxBits;
                    acc += step;
                    T v = l[sx];
                    bool ground = m ? !m[sx] : v == g;
                    return ground ? dim_comb(s[x]) : over(s[x], v);
                };
                if constexpr (std::is_same_v<T, uint16_t>) {
                    pair_span(d, 0, n, tap);
                    pair_span(d, n, dst.w, [&](int x) { return dim_base(s[x]); });
                } else {
                    for (int x = 0; x < n; x++)
                        d[x] = tap(x);
                    for (int x = n; x < dst.w; x++)
                        d[x] = dim_base(s[x]);
                }
            }
        };
        if constexpr (std::is_same_v<F, R565>) {
            if (motion) {
                swivel_motion565(page, layer, mask, dst, cols, g, alpha5(comb), alpha5(base_a), alpha5(a), a);
                return;
            }
        }
        run([&](T p) { return dim(p, comb); }, [&](T p) { return dim(p, base_a); },
            [&](T p, T v) { return cover(p, v); });
    }
}

}

template <class F, class L>
void project(const SurfaceT<F, L>& src, Rect s, const SurfaceT<F, L>& dst, Rect clip, const Projection& pr, uint32_t bg,
             int a)
{
    prof::Scope ps(prof::Project);
    project_into(src, nullptr, s, dst, clip, pr, bg, a);
}

template <class F, class L>
void project(const SurfaceT<F, L>& src, const uint8_t* mask, Rect s, const SurfaceT<F, L>& dst, Rect clip,
             const Projection& pr, uint32_t bg, int a)
{
    prof::Scope ps(prof::Project);
    project_into(src, mask, s, dst, clip, pr, bg, a);
}

template <class F, class L>
void project_sprite(const SurfaceT<F, L>& src, Rect s, const Projection& pr, uint32_t bg, SpriteT<F, L>& out)
{
    prof::Scope ps(prof::ProjectSprite);
    sprite_into(src, nullptr, s, pr, bg, out);
}

template <class F, class L>
void project_sprite(const SurfaceT<F, L>& src, const uint8_t* mask, Rect s, const Projection& pr, uint32_t bg,
                    SpriteT<F, L>& out)
{
    prof::Scope ps(prof::ProjectSprite);
    sprite_into(src, mask, s, pr, bg, out);
}

template <class F, class L> void draw_sprite(CanvasT<F, L>& c, const SpriteT<F, L>& s, int a)
{
    prof::Scope ps(prof::DrawSprite);
    Rect d = s.r.intersect(c.clip());
    if (d.empty() || a <= 0)
        return;
    SurfaceT<F, L> dst = c.surface();
    if constexpr (std::is_same_v<L, Rot>) {
        int h = s.r.y1 - s.r.y0;
        for (int x = d.x0; x < d.x1; x++) {
            const uint32_t* col = s.px.data() + (size_t)(x - s.r.x0) * h + (d.y0 - s.r.y0);
            uint32_t* row = dst.mem_row(dst.w - 1 - x);
            for (int y = d.y0; y < d.y1; y++) {
                uint32_t v = col[y - d.y0];
                int cov = v >> 24;
                if (!cov)
                    continue;
                int aa = a * (cov + 1) >> 8;
                row[y] = aa >= 256 ? (v & 0xFFFFFF) : blend_px(row[y], v & 0xFFFFFF, aa);
            }
        }
    } else {
        using T = typename F::T;
        constexpr int sh = kSpriteShift<F>;
        int w = s.r.x1 - s.r.x0;
        uint8_t* mk = c.mask();
        for (int y = d.y0; y < d.y1; y++) {
            const uint32_t* src = s.px.data() + (size_t)(y - s.r.y0) * w + (d.x0 - s.r.x0);
            T* row = dst.row(y);
            for (int x = d.x0; x < d.x1; x++) {
                uint32_t v = src[x - d.x0];
                int cov = v >> sh;
                if (!cov)
                    continue;
                int aa = a * (cov + 1) >> 8;
                T p = (T)(v & ((1u << sh) - 1));
                row[x] = aa >= 256 ? p : F::blend(row[x], p, aa);
                if (mk) {
                    size_t i = dst.index(x, y);
                    mk[i] = cover8(mk[i], aa);
                }
            }
        }
    }
}

void swivel_cols(fx angle, int hinge, int d, SwivelCol* cols)
{
    fx c = cos_deg(angle), sn = sin_deg(angle);
    for (int y = 0; y < kPortraitH; y++) {
        SwivelCol& k = cols[y];
        int ys = y - hinge;
        int64_t den = c + (int64_t)ys * sn / d;
        if (den <= 0) {
            k.src = -1;
            continue;
        }
        int sy = hinge + (int)(((int64_t)ys * kOne * kOne / den + kHalf) >> kFxBits);
        if (sy < 0 || sy >= kPortraitH) {
            k.src = -1;
            continue;
        }
        k.src = sy;
        k.w = (fx)(((int64_t)c << kFxBits) / den);
    }
}

template <class F, class L>
void swivel(const SurfaceT<F, L>& page, const SurfaceT<F, L>& layer, const SurfaceT<F, L>& dst, const SwivelCol* cols,
            uint32_t bg, int base_a, int a, bool motion)
{
    prof::Scope ps(prof::Swivel);
    swivel_into(page, layer, nullptr, dst, cols, bg, base_a, a, motion);
}

template <class F, class L>
void swivel(const SurfaceT<F, L>& page, const SurfaceT<F, L>& layer, const uint8_t* mask, const SurfaceT<F, L>& dst,
            const SwivelCol* cols, uint32_t bg, int base_a, int a, bool motion)
{
    prof::Scope ps(prof::Swivel);
    swivel_into(page, layer, mask, dst, cols, bg, base_a, a, motion);
}

using S = SurfaceT<X888, Rot>;
using SR = SurfaceT<X888, Rows>;
using S16 = SurfaceT<R565, Rows>;

template void warp<X888, Rot>(const S&, const S&, const WarpRow*, uint32_t, int, bool, int);
template void project<X888, Rot>(const S&, Rect, const S&, Rect, const Projection&, uint32_t, int);
template void project_sprite<X888, Rot>(const S&, Rect, const Projection&, uint32_t, SpriteT<X888, Rot>&);
template void draw_sprite<X888, Rot>(CanvasT<X888, Rot>&, const SpriteT<X888, Rot>&, int);
template void swivel<X888, Rot>(const S&, const S&, const S&, const SwivelCol*, uint32_t, int, int, bool);

template void warp<X888, Rows>(const SR&, const SR&, const WarpRow*, uint32_t, int, bool, int);
template void project<X888, Rows>(const SR&, Rect, const SR&, Rect, const Projection&, uint32_t, int);
template void project_sprite<X888, Rows>(const SR&, Rect, const Projection&, uint32_t, SpriteT<X888, Rows>&);
template void draw_sprite<X888, Rows>(CanvasT<X888, Rows>&, const SpriteT<X888, Rows>&, int);
template void swivel<X888, Rows>(const SR&, const SR&, const SR&, const SwivelCol*, uint32_t, int, int, bool);

template void warp<R565, Rows>(const S16&, const S16&, const WarpRow*, uint32_t, int, bool, int);
template void project<R565, Rows>(const S16&, const uint8_t*, Rect, const S16&, Rect, const Projection&, uint32_t, int);
template void project_sprite<R565, Rows>(const S16&, const uint8_t*, Rect, const Projection&, uint32_t,
                                         SpriteT<R565, Rows>&);
template void draw_sprite<R565, Rows>(CanvasT<R565, Rows>&, const SpriteT<R565, Rows>&, int);
template void swivel<R565, Rows>(const S16&, const S16&, const uint8_t*, const S16&, const SwivelCol*, uint32_t, int,
                                 int, bool);

}
