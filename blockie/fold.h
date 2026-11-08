#pragma once
#include <cstdint>
#include <vector>

#include "../gfx/canvas.h"

namespace fd {

enum class FoldDir { ToDetails, ToSelector, PagePush, PagePop };
enum class FoldEdge { Right, Left, Down, Up };

struct FoldCell {
    int x = 0, y = 0;
    int ring = 0;
    FoldEdge edge = FoldEdge::Right;
    bool inside = false;
};

struct FoldTile {
    Rect r;
    int step = 0;
};

struct FoldPlan {
    FoldDir dir = FoldDir::ToDetails;
    Rect scope;
    int maxring = 0;
    std::vector<FoldCell> cells;
    std::vector<FoldTile> tiles;
    int tile_steps = 0;
};

FoldPlan fold_plan(FoldDir dir, int v, int title_bottom_px);
int64_t fold_duration(const FoldPlan& p);
void fold_compose(const Surface& from, const Surface& to, const Surface& dst, const FoldPlan& p, int64_t t_ms);

}
