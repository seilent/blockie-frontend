#pragma once
#include <cstdint>
#include <cstring>

#include "../gfx/pixel.h"

namespace fd {

inline void blend_row(uint32_t* p, int n, uint32_t ground, int a)
{
    typedef uint32_t v4 __attribute__((vector_size(16)));
    const uint32_t rb = ground & 0xFF00FF, g = ground & 0xFF00, k = (uint32_t)a;
    int i = 0;
    for (; i + 4 <= n; i += 4) {
        v4 s;
        std::memcpy(&s, p + i, sizeof s);
        v4 lo = (rb + ((((s & 0xFF00FF) - rb) * k) >> 8)) & 0xFF00FF;
        v4 mid = (g + ((((s & 0xFF00) - g) * k) >> 8)) & 0xFF00;
        v4 out = lo | mid;
        std::memcpy(p + i, &out, sizeof out);
    }
    for (; i < n; i++)
        p[i] = blend_px(ground, p[i], a);
}

}
