#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "../board/board.h"

namespace fd {

static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "two pixels per word assume little endian");

using PixPair = uint32_t __attribute__((may_alias));

inline uint32_t blend_px(uint32_t d, uint32_t s, int a)
{
    uint32_t rb = d & 0xFF00FF, g = d & 0xFF00;
    rb = (rb + ((((s & 0xFF00FF) - rb) * a) >> 8)) & 0xFF00FF;
    g = (g + ((((s & 0xFF00) - g) * a) >> 8)) & 0xFF00;
    return rb | g;
}

struct Pack565 {
    uint16_t r[256], g[256], b[256];
};

constexpr Pack565 make_pack565()
{
    Pack565 t{};
    for (unsigned v = 0; v < 256; v++) {
        t.r[v] = (uint16_t)((v * 31 + 127) / 255 << 11);
        t.g[v] = (uint16_t)((v * 63 + 127) / 255 << 5);
        t.b[v] = (uint16_t)((v * 31 + 127) / 255);
    }
    return t;
}

inline constexpr Pack565 kPack565 = make_pack565();

constexpr uint16_t pack565(uint32_t c)
{
    return (uint16_t)(kPack565.r[c >> 16 & 0xFF] | kPack565.g[c >> 8 & 0xFF] | kPack565.b[c & 0xFF]);
}

constexpr uint32_t unpack565(uint16_t p)
{
    uint32_t r = p >> 11, g = p >> 5 & 63, b = p & 31;
    return (r << 3 | r >> 2) << 16 | (g << 2 | g >> 4) << 8 | (b << 3 | b >> 2);
}

inline uint16_t blend565(uint16_t d, uint16_t s, int a)
{
    uint32_t ia = 256 - a;
    uint32_t r = ((uint32_t)(s >> 11) * a + (uint32_t)(d >> 11) * ia + 128) >> 8;
    uint32_t g = ((uint32_t)(s >> 5 & 63) * a + (uint32_t)(d >> 5 & 63) * ia + 128) >> 8;
    uint32_t b = ((uint32_t)(s & 31) * a + (uint32_t)(d & 31) * ia + 128) >> 8;
    return (uint16_t)(r << 11 | g << 5 | b);
}

constexpr int alpha5(int a256) { return a256 >= 256 ? 32 : a256 <= 0 ? 0 : (a256 + 4) >> 3; }

inline uint16_t lerp565(uint16_t d, uint16_t s, int a5)
{
    uint32_t xd = (d | (uint32_t)d << 16) & 0x07E0F81F, xs = (s | (uint32_t)s << 16) & 0x07E0F81F;
    uint32_t x = (xd + (((xs - xd) * (uint32_t)a5) >> 5)) & 0x07E0F81F;
    return (uint16_t)(x | x >> 16);
}

constexpr int div255(int v) { return (v + 128 + ((v + 128) >> 8)) >> 8; }

constexpr int div8160(int t) { return (int)(((uint32_t)(t + 522240) >> 5) * 32897u >> 23) - 64; }

constexpr uint32_t spread565(uint16_t p) { return (p | (uint32_t)p << 16) & 0x07E0F81F; }

constexpr uint16_t fold565(uint32_t x)
{
    x &= 0x07E0F81F;
    return (uint16_t)(x | x >> 16);
}

constexpr uint16_t mean565(uint16_t a, uint16_t b, uint16_t c, uint16_t d)
{
    return fold565((spread565(a) + spread565(b) + spread565(c) + spread565(d) + 0x00401002) >> 2);
}

constexpr uint32_t mean888(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    uint32_t rb = ((a & 0xFF00FF) + (b & 0xFF00FF) + (c & 0xFF00FF) + (d & 0xFF00FF) + 0x20002) >> 2 & 0xFF00FF;
    uint32_t g = ((a & 0xFF00) + (b & 0xFF00) + (c & 0xFF00) + (d & 0xFF00) + 0x200) >> 2 & 0xFF00;
    return rb | g;
}

constexpr uint8_t mean8(int a, int b, int c, int d) { return (uint8_t)((a + b + c + d + 2) >> 2); }

inline uint16_t over_ground5(uint16_t d, uint16_t v, int m, uint16_t k, int a5)
{
    if (m >= 255 || d == k)
        return lerp565(d, v, a5);
    int im = 255 - m;
    auto lane = [&](int sh, int top) {
        int dc = d >> sh & top;
        int q = std::clamp((int)(v >> sh & top) * 255 + (dc - (int)(k >> sh & top)) * im, 0, top * 255);
        return (uint16_t)((dc + div8160((q - dc * 255) * a5 + 4080)) << sh);
    };
    return (uint16_t)(lane(11, 31) | lane(5, 63) | lane(0, 31));
}

inline uint16_t tint565(uint16_t d, uint16_t v, int m, int a5)
{
    if (m == 255 && a5 == 32)
        return v;
    int ma = m * a5;
    bool solid = m == 255;
    auto lane = [&](int sh, int top) {
        int dc = d >> sh & top, e = (int)(v >> sh & top) - dc;
        return (uint16_t)((dc + (solid ? (e * a5 + 16) >> 5 : div8160(e * ma + 4080))) << sh);
    };
    return (uint16_t)(lane(11, 31) | lane(5, 63) | lane(0, 31));
}

inline uint32_t over_ground256(uint32_t d, uint32_t v, int m, uint32_t k, int a256)
{
    if (m >= 255 || d == k)
        return blend_px(d, v, a256);
    int im = 255 - m;
    uint32_t q = 0;
    for (int sh = 0; sh < 24; sh += 8) {
        int t = ((int)(d >> sh & 255) - (int)(k >> sh & 255)) * im;
        q |= (uint32_t)std::clamp((int)(v >> sh & 255) + (t >= 0 ? div255(t) : -div255(-t)), 0, 255) << sh;
    }
    return blend_px(d, q, a256);
}

constexpr uint8_t cover8(uint8_t m, int a256)
{
    int a = a256 >= 256 ? 255 : (a256 * 255 + 128) >> 8;
    return (uint8_t)(m + div255((255 - m) * a));
}

struct X888 {
    using T = uint32_t;
    static constexpr T pack(uint32_t rgb) { return rgb; }
    static constexpr uint32_t unpack(T p) { return p; }
    static T blend(T d, T s, int a) { return blend_px(d, s, a); }
};

struct R565 {
    using T = uint16_t;
    static constexpr T pack(uint32_t rgb) { return pack565(rgb); }
    static constexpr uint32_t unpack(T p) { return unpack565(p); }
    static T blend(T d, T s, int a) { return blend565(d, s, a); }
};

struct Rot {
    static constexpr size_t index(int w, int h, int x, int y) { return (size_t)(w - 1 - x) * h + y; }
};

struct Rows {
    static constexpr size_t index(int w, int h, int x, int y)
    {
        (void)h;
        return (size_t)y * w + x;
    }
};

template <class F, class L> struct SurfaceT;
template <class F, class L> struct ImageT;
template <class F, class L> struct BufferT;
template <class F, class L> struct LayerT;
template <class F, class L> class CanvasT;

constexpr bool kPix16 = board::kFbBpp == 16 && !board::kFbRotated;
#ifdef FD_PIX16
static_assert(kPix16, "FD_PIX16 is for the 16 bpp row boards");
#else
static_assert(!kPix16, "a 16 bpp row board defines FD_PIX16");
#endif
#ifdef FD_ROT
static_assert(board::kFbRotated, "FD_ROT is for the rotated boards");
#else
static_assert(!board::kFbRotated, "a rotated board defines FD_ROT");
#endif
static_assert(board::kFbBpp == 16 || board::kFbBpp == 32, "the panel is 16 or 32 bpp");
static_assert(!(board::kFbBpp == 16 && board::kFbRotated), "a 16 bpp board renders in rows");

using LiveF = std::conditional_t<board::kFbBpp == 16, R565, X888>;
using LiveL = std::conditional_t<board::kFbRotated, Rot, Rows>;
using Surface = SurfaceT<LiveF, LiveL>;
using Image = ImageT<LiveF, LiveL>;
using Buffer = BufferT<LiveF, LiveL>;
using Layer = LayerT<LiveF, LiveL>;
using Canvas = CanvasT<LiveF, LiveL>;
using Pix = LiveF::T;

#ifndef __mips__
#define FD_MIP_X888 1
#endif

inline void copy_px(uint32_t* dst, const uint32_t* s, int n)
{
    volatile uint32_t* d = dst;
    int i = 0;
    for (; i + 4 <= n; i += 4) {
        uint32_t a = s[i], b = s[i + 1], c = s[i + 2], e = s[i + 3];
        d[i] = a;
        d[i + 1] = b;
        d[i + 2] = c;
        d[i + 3] = e;
    }
    for (; i < n; i++)
        d[i] = s[i];
}

inline void fill_px(uint32_t* dst, uint32_t v, int n)
{
    volatile uint32_t* d = dst;
    int i = 0;
    for (; i + 4 <= n; i += 4) {
        d[i] = v;
        d[i + 1] = v;
        d[i + 2] = v;
        d[i + 3] = v;
    }
    for (; i < n; i++)
        d[i] = v;
}

inline void copy_px(uint16_t* dst, const uint16_t* s, int n)
{
    if (n <= 0)
        return;
    if (((uintptr_t)dst ^ (uintptr_t)s) & 2) {
        volatile uint16_t* d = dst;
        for (int i = 0; i < n; i++)
            d[i] = s[i];
        return;
    }
    if ((uintptr_t)dst & 2) {
        *(volatile uint16_t*)dst = *s;
        dst++;
        s++;
        n--;
    }
    volatile PixPair* d = (PixPair*)dst;
    const PixPair* p = (const PixPair*)s;
    int k = n >> 1, i = 0;
    for (; i + 4 <= k; i += 4) {
        uint32_t a = p[i], b = p[i + 1], c = p[i + 2], e = p[i + 3];
        d[i] = a;
        d[i + 1] = b;
        d[i + 2] = c;
        d[i + 3] = e;
    }
    for (; i < k; i++)
        d[i] = p[i];
    if (n & 1)
        *(volatile uint16_t*)(dst + n - 1) = s[n - 1];
}

inline void fill_px(uint16_t* dst, uint16_t v, int n)
{
    if (n <= 0)
        return;
    if ((uintptr_t)dst & 2) {
        *(volatile uint16_t*)dst = v;
        dst++;
        n--;
    }
    volatile PixPair* d = (PixPair*)dst;
    uint32_t w = (uint32_t)v | (uint32_t)v << 16;
    int k = n >> 1, i = 0;
    for (; i + 4 <= k; i += 4) {
        d[i] = w;
        d[i + 1] = w;
        d[i + 2] = w;
        d[i + 3] = w;
    }
    for (; i < k; i++)
        d[i] = w;
    if (n & 1)
        *(volatile uint16_t*)(dst + n - 1) = v;
}

}
