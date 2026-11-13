#pragma once
#include <algorithm>
#include <cstdint>

#include "../core/fx.h"

namespace fd {

constexpr fx kDotSide = fx_int(2);
constexpr fx kDotsX0 = fx_int(20);
constexpr fx kDotsX1 = fx_int(460);
constexpr fx kDotsY = fx_int(98);

struct RingArc {
    int start = 0;
    int sweep = 0;
};

inline RingArc ring_arc(int64_t done, int64_t total, int start)
{
    if (total <= 0)
        return {start, 0};
    return {start, (int)std::min<double>(360.0 * (double)std::max<int64_t>(done, 0) / (double)total, 360 * 99 / 100)};
}

}
