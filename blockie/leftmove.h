#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

#include "../gfx/canvas.h"
#include "tile_grid.h"

namespace fd {

enum class GrowClock { None, G, G6, GS };

struct LeftPlan {
    Rect header;
    GridSpec grid = grid_spec(TileMode::Standard);
    int grid_y = 0;
    Rect band{0, kBandTop, kPaneSplitWide, kBandBottom};
    Rect focus_from;
    Rect focus_to;
    bool collapse = false;
    bool prepared = false;
    std::vector<Rect> kept;
    std::vector<Rect> leaves;
    std::vector<Rect> bands;
    GrowClock grow = GrowClock::None;
    int grow_q = 0;
    bool from_set = false;
    GridSpec from_grid = grid_spec(TileMode::Standard);
    int from_y = 0;
    Rect from_focus;
};

struct SwapLeg {
    int dx = 0;
    int a256 = 0;
};

constexpr int64_t swap_ms()
{
    return fx_round(tok::motion_swapOut) + std::max(fx_round(tok::motion_swapRows), fx_round(tok::motion_swapRowFade));
}

fx cubic_out(fx t);
SwapLeg swap_leave(int64_t t, int dir);
SwapLeg swap_arrive(int64_t t, int dir);
int swap_header(int64_t t, bool collapse);
void swap_prepare(LeftPlan& p, const Surface& a, const Surface& b);
void swap_compose(const Surface& a, const Surface& b, const Surface& dst, const LeftPlan& p, int64_t t, int x_end,
                  uint32_t ground);

}
