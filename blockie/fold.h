#pragma once
#include <cstdint>
#include <vector>

#include "../gfx/canvas.h"
#include "tile_grid.h"

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
    int64_t ring_unit = 0;
};

struct LeftFold {
    FoldPlan plan;
    Rect header;
    bool prepared = false;
    int top_ring = -1;
    std::vector<Rect> slots;
    std::vector<uint8_t> changed;
    std::vector<uint8_t> folds;
    std::vector<uint8_t> lines;
    std::vector<uint8_t> own;
};

constexpr int kFoldCell = 48;
constexpr int kLeftCols = 9;
constexpr int kLeftRows = 15;
constexpr int kLeftLine = kFoldCell + kTileFocusBorder;

FoldPlan fold_plan(FoldDir dir, int v, int title_bottom_px);
FoldPlan fold_left(int ring_frames);
LeftFold left_fold(int ring_frames, Rect header);
void left_prepare(LeftFold& lf, const Surface& a, const Surface& b);
int64_t left_span(const LeftFold& lf);
void left_compose(const Surface& a, const Surface& b, const Surface& dst, const LeftFold& lf, int64_t t_ms, int head);
void left_fade(const Surface& a, const Surface& b, const Surface& dst, int to_a256);
int64_t fold_duration(const FoldPlan& p);
void fold_compose(const Surface& from, const Surface& to, const Surface& dst, const FoldPlan& p, int64_t t_ms);

}
