#pragma once
#include <cstdint>
#include <vector>

#include "../gfx/canvas.h"
#include "tile_grid.h"

namespace fd {

enum class FoldEdge { Right, Left, Down, Up };

struct FoldCell {
    int x = 0, y = 0;
    int ring = 0;
    FoldEdge edge = FoldEdge::Right;
};

struct FoldPlan {
    int maxring = 0;
    std::vector<FoldCell> cells;
};

constexpr int kFoldCell = 48;

FoldPlan fold_page(int v);
FoldPlan fold_page_row(int cy);
void left_fade(const Surface& a, const Surface& b, const Surface& dst, int to_a256);
int64_t fold_duration(const FoldPlan& p);
void fold_compose(const Surface& from, const Surface& to, const Surface& dst, const FoldPlan& p, int64_t t_ms);

}
