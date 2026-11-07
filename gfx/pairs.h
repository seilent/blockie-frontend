#pragma once
#include <cstdint>

#include "pixel.h"

namespace fd {

template <class Fn> inline void pair_span(uint16_t* d, int from, int to, Fn f)
{
    int x = from;
    if (x < to && (uintptr_t)(d + x) & 2) {
        d[x] = f(x);
        x++;
    }
    for (; x + 1 < to; x += 2) {
        uint32_t lo = f(x);
        uint32_t hi = f(x + 1);
        *(PixPair*)(d + x) = lo | hi << 16;
    }
    if (x < to)
        d[x] = f(x);
}

struct Blend565 {
    uint32_t ia, kr, kg, kb, lw = 0, lr = 0;

    Blend565(uint16_t v, int a)
        : ia(256u - (uint32_t)a), kr(((uint32_t)(v >> 11) * (uint32_t)a + 128) * 0x10001u),
          kg(((uint32_t)(v >> 5 & 63) * (uint32_t)a + 128) * 0x10001u),
          kb(((uint32_t)(v & 31) * (uint32_t)a + 128) * 0x10001u)
    {
        lr = pair(0);
    }
    uint32_t pair(uint32_t w) const
    {
        uint32_t b = ((w & 0x001F001Fu) * ia + kb) >> 8 & 0x001F001Fu;
        uint32_t g = ((w >> 5 & 0x003F003Fu) * ia + kg) >> 8 & 0x003F003Fu;
        uint32_t r = ((w >> 11 & 0x001F001Fu) * ia + kr) >> 8 & 0x001F001Fu;
        return b | g << 5 | r << 11;
    }
    uint16_t one(uint16_t p) const { return (uint16_t)pair(p); }
    [[gnu::noinline]] void span(uint16_t* d, const uint16_t* s, int n)
    {
        if (n <= 0)
            return;
        Blend565 k = *this;
        int i = 0;
        if (((uintptr_t)d ^ (uintptr_t)s) & 2) {
            for (; i < n; i++)
                d[i] = k.one(s[i]);
            return;
        }
        if ((uintptr_t)d & 2)
            d[i++] = k.one(s[0]);
        for (; i + 1 < n; i += 2) {
            uint32_t w = *(const PixPair*)(s + i);
            if (w != k.lw) {
                k.lw = w;
                k.lr = k.pair(w);
            }
            *(PixPair*)(d + i) = k.lr;
        }
        if (i < n)
            d[i] = k.one(s[i]);
        lw = k.lw;
        lr = k.lr;
    }
};

}
