#pragma once
#include <algorithm>
#include <cstdint>

#include "font.h"

namespace fd::fontkey {

inline uint32_t apply_case(uint32_t c, Case k)
{
    if (k == Case::Lower && c >= 'A' && c <= 'Z')
        return c + 32;
    if (k == Case::Upper && c >= 'a' && c <= 'z')
        return c - 32;
    return c;
}

inline int scale64(int fu, int px, int upem) { return (int)((int64_t)fu * px * 64 / upem); }

inline uint64_t key_of(int b, int fi, int px, int gid)
{
    return ((uint64_t)b << 56) | ((uint64_t)fi << 40) | ((uint64_t)px << 24) | (uint64_t)gid;
}

inline int key_bucket(uint64_t k) { return (int)(k >> 56); }

inline int inside(int v, int lo, int hi) { return std::max(lo, std::min(hi - 1, v)); }

}
