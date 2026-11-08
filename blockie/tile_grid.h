#pragma once

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

constexpr Rect grid_tile_rect(int k)
{
    int x = kGridX + k % kGridCols * (kTileSide + kGridGutter);
    int y = kTileFirstTop + k / kGridCols * kGridRowPitch;
    return Rect{x, y, x + kTileSide, y + kTileSide};
}

}
