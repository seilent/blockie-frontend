#include "scale.h"

#include <algorithm>
#include <cstdint>

namespace fd {

namespace {

constexpr int kFull = 64 * 64;

int span64(int p, int a, int b) { return std::max(0, std::min(p + 64, b) - std::max(p, a)); }

uint32_t tap_channel(uint32_t p00, uint32_t p10, uint32_t p01, uint32_t p11, int wx, int wy)
{
    uint32_t o = 0;
    for (int sh = 0; sh < 24; sh += 8) {
        int c00 = p00 >> sh & 255, c10 = p10 >> sh & 255, c01 = p01 >> sh & 255, c11 = p11 >> sh & 255;
        int top = c00 * (256 - wx) + c10 * wx, bot = c01 * (256 - wx) + c11 * wx;
        o |= (uint32_t)((top * (256 - wy) + bot * wy + 32768) >> 16) << sh;
    }
    return o;
}

uint32_t tap_swar(uint32_t p00, uint32_t p10, uint32_t p01, uint32_t p11, uint32_t wx, uint32_t wy)
{
    uint32_t a = p00 & 0xFF00FF, b = p10 & 0xFF00FF, c = p01 & 0xFF00FF, d = p11 & 0xFF00FF;
    uint32_t top = ((a << 8) + (b - a) * wx + 0x800080) >> 8 & 0xFF00FF;
    uint32_t bot = ((c << 8) + (d - c) * wx + 0x800080) >> 8 & 0xFF00FF;
    uint32_t rb = ((top << 8) + (bot - top) * wy + 0x800080) >> 8 & 0xFF00FF;
    uint32_t ga = p00 & 0xFF00, gb = p10 & 0xFF00, gc = p01 & 0xFF00, gd = p11 & 0xFF00;
    uint32_t gt = (ga << 8) + (gb - ga) * wx;
    uint32_t gu = (gc << 8) + (gd - gc) * wx;
    uint32_t g = ((gt << 8) + (gu - gt) * wy + 0x800000) >> 16 & 0xFF00;
    return rb | g;
}

template <Scaler K>
void tile_rows(const Surface& dst, const Image& img, const ScaleAxis& xs, const ScaleAxis& ys, int i0, int i1, int j0,
               int j1)
{
    const ScaleTap* tx = xs.taps.data();
    const ScaleTap* ty = ys.taps.data();
    const uint32_t* px = img.mem.data();
    for (int j = j0; j < j1; j++) {
        const ScaleTap& y = ty[j];
        if (!y.cov)
            continue;
        const uint32_t* r0 = px + (size_t)y.i0 * img.w;
        const uint32_t* r1 = px + (size_t)y.i1 * img.w;
        uint32_t* d = dst.row(ys.p0 + j);
        for (int i = i0; i < i1; i++) {
            const ScaleTap& x = tx[i];
            uint32_t& o = d[xs.p0 + i];
            int c = x.cov * y.cov;
            if (c == kFull) {
                if constexpr (K == Scaler::Swar)
                    o = tap_swar(r0[x.i0], r0[x.i1], r1[x.i0], r1[x.i1], (uint32_t)x.w, (uint32_t)y.w);
                else
                    o = tap_channel(r0[x.i0], r0[x.i1], r1[x.i0], r1[x.i1], x.w, y.w);
                continue;
            }
            int a = c * 255 / kFull;
            if (a)
                o = blend_px(o, tap_channel(r0[x.i0], r0[x.i1], r1[x.i0], r1[x.i1], x.w, y.w), a + 1);
        }
    }
}

}

void scale_axis(ScaleAxis& out, int e0, int e1, int src, int lo, int hi)
{
    out.src = src;
    out.taps.clear();
    int p0 = std::max(lo, e0 >> 6), p1 = std::min(hi, (e1 + 63) >> 6);
    out.p0 = p0;
    if (src <= 0 || e1 <= e0 || p1 <= p0)
        return;
    out.taps.resize((size_t)(p1 - p0));
    int64_t d = e1 - e0;
    for (int p = p0; p < p1; p++) {
        int64_t c = (int64_t)(p * 64 + 32 - e0) * src * 256;
        int64_t v = std::clamp<int64_t>(c / d - 128, 0, (int64_t)(src - 1) * 256);
        ScaleTap& t = out.taps[(size_t)(p - p0)];
        t.i0 = (int)(v >> 8);
        t.w = (int)(v & 255);
        t.i1 = std::min(t.i0 + 1, src - 1);
        t.cov = span64(p * 64, e0, e1);
    }
}

void scale_tile(const Surface& dst, const Image& img, const ScaleAxis& xs, const ScaleAxis& ys, Scaler k)
{
    if (img.w <= 0 || img.h <= 0 || xs.src != img.w || ys.src != img.h)
        return;
    int i0 = std::max(0, -xs.p0), i1 = std::min((int)xs.taps.size(), dst.w - xs.p0);
    int j0 = std::max(0, -ys.p0), j1 = std::min((int)ys.taps.size(), dst.h - ys.p0);
    if (i0 >= i1 || j0 >= j1)
        return;
    if (k == Scaler::Swar)
        tile_rows<Scaler::Swar>(dst, img, xs, ys, i0, i1, j0, j1);
    else
        tile_rows<Scaler::Channel>(dst, img, xs, ys, i0, i1, j0, j1);
}

}
