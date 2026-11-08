#pragma once

#include <algorithm>

#include "../gfx/canvas.h"

namespace fd {

constexpr int kGridX = 120;
constexpr int kTileSide = 154;
constexpr int kGridGutter = 4;
constexpr int kGridCols = 2;
constexpr int kGridRows = 4;
constexpr int kGridRowPitch = 158;
constexpr int kTileFirstTop = 80;
constexpr int kGridTiles = kGridCols * kGridRows;
constexpr int kTileFocusBorder = 4;

constexpr int kPaneSplit = 432;
constexpr int kPaneSplitWide = kPaneSplit + kPaneSplit / 2;
constexpr int kPaneLeftEdge = kPaneSplit + kTileFocusBorder;
constexpr int kGridColsWide = (kPaneSplitWide - kGridX + kGridGutter) / (kTileSide + kGridGutter);
constexpr int kBigTileSide = std_px(tok::tileL);
constexpr int kBigGutter = std_px(tok::tileLGutter);
constexpr int kBigPitch = kBigTileSide + kBigGutter;
constexpr int kBigCols = 2;
constexpr int kBigRows = 3;

constexpr Rect grid_tile_rect(int k, int cols)
{
    int x = kGridX + k % cols * (kTileSide + kGridGutter);
    int y = kTileFirstTop + k / cols * kGridRowPitch;
    return Rect{x, y, x + kTileSide, y + kTileSide};
}

constexpr Rect big_tile_rect(int k)
{
    int x = kGridX + k % kBigCols * kBigPitch;
    int y = kTileFirstTop + k / kBigCols * kBigPitch;
    return Rect{x, y, x + kBigTileSide, y + kBigTileSide};
}

enum class SlotMap { RowMajor, RecentC };

constexpr int slot_of(int game, int cols, SlotMap m)
{
    if (game < 0)
        return -1;
    if (m == SlotMap::RowMajor || cols <= kGridCols)
        return game;
    if (game < kGridTiles)
        return game / kGridCols * cols + game % kGridCols;
    int j = game - kGridTiles;
    int col = kGridCols + j / kGridRows;
    if (col >= cols)
        return -1;
    return j % kGridRows * cols + col;
}

constexpr int game_at(int slot, int cols, SlotMap m, int count)
{
    if (slot < 0 || cols <= 0)
        return -1;
    int g = slot;
    if (m == SlotMap::RecentC && cols > kGridCols) {
        int col = slot % cols, row = slot / cols;
        if (row >= kGridRows)
            return -1;
        g = col < kGridCols ? row * kGridCols + col : kGridTiles + (col - kGridCols) * kGridRows + row;
    }
    return g < count ? g : -1;
}

inline Rect fit_rect(int sw, int sh, int tx, int ty, int side)
{
    if (sw <= 0 || sh <= 0)
        return {tx, ty, tx + side, ty + side};
    double s = std::max((double)side / sw, (double)side / sh);
    int w = (int)(sw * s + 0.5);
    int h = (int)(sh * s + 0.5);
    int x = tx + (side - w) / 2;
    int y = ty + (side - h) / 2;
    return {x, y, x + w, y + h};
}

}
