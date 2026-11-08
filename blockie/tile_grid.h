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

constexpr Rect grid_tile_rect(int k)
{
    int x = kGridX + k % kGridCols * (kTileSide + kGridGutter);
    int y = kTileFirstTop + k / kGridCols * kGridRowPitch;
    return Rect{x, y, x + kTileSide, y + kTileSide};
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
