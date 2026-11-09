#include <cstdio>

#include "../tile_grid.h"

using namespace fd;

namespace {

int g_fail = 0;
int g_total = 0;

void check(bool ok, const char* what)
{
    g_total++;
    if (ok) {
        std::printf("PASS %s\n", what);
    } else {
        std::printf("FAIL %s\n", what);
        g_fail++;
    }
}

bool same(Rect a, Rect b) { return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1; }

bool same64(Rect64 a, Rect b)
{
    return a.x0 == b.x0 * 64 && a.y0 == b.y0 * 64 && a.x1 == b.x1 * 64 && a.y1 == b.y1 * 64;
}

bool rows_fit(const GridSpec& s)
{
    int focus_end = kTileFirstTop + (s.focus_rows - 1) * s.pitch + s.side + kTileFocusBorder;
    int next_end = focus_end + s.pitch;
    int band_top = kTileFirstTop + (s.band_rows - 1) * s.pitch;
    return focus_end <= kBandBottom && next_end > kBandBottom && band_top < kBandBottom &&
           band_top + s.pitch >= kBandBottom;
}

}

int main()
{
    bool cols01 = true;
    for (int row = 0; row < 6; row++)
        for (int col = 0; col < 2; col++) {
            Rect want{120 + col * 158, 80 + row * 158, 274 + col * 158, 234 + row * 158};
            if (!same(grid_tile_rect(row * 3 + col, 3), want) || !same(grid_tile_rect(row * 2 + col, 2), want))
                cols01 = false;
        }
    check(cols01, "columns 0 and 1 are the same rects for 2 and 3 columns");

    check(kPaneSplit == 432 && kPaneSplitWide == 648, "the pane boundary is 432 collapsed and 648 expanded");
    check(kGridColsWide == 3, "3 columns of today's tile fit left of 648");
    Rect c2 = grid_tile_rect(2, 3);
    check(grid_tile_rect(0, 3).x0 == 120 && grid_tile_rect(1, 3).x0 == 278 && c2.x0 == 436 && c2.x1 == 590,
          "the 3 column grid sits at x 120, 278, 436 with its right edge at 590");
    check(grid_tile_rect(3, 3).y0 == 238 && grid_tile_rect(9, 3).y0 == 554 && grid_tile_rect(9, 3).y1 == 708,
          "rows from y 80 at pitch 158, the fourth ends at 708");

    check(kBigTileSide == 232 && kBigGutter == 16, "the big tile is tileL 232 px with the 16 px tileLGutter");
    check(big_tile_rect(0).x0 == 120 && big_tile_rect(1).x0 == 368 && big_tile_rect(1).x1 == 600,
          "the big grid sits at x 120 and 368 with its right edge at 600");
    check(big_tile_rect(2).y0 == 328 && big_tile_rect(4).y0 == 576 && big_tile_rect(4).y1 > 720,
          "big rows at 80, 328 and 576, the third cut by the canvas");

    check(kBandTop == 76 && kBandBottom == 712, "the band runs from y 76 to 712");
    GridSpec st = grid_spec(TileMode::Standard);
    check(st.cols == kGridColsWide && st.side == kTileSide && st.gutter == kGridGutter && st.pitch == kGridRowPitch &&
              st.focus_rows == 4 && st.band_rows == 4 && st.pad_rows == 2 && st.right == 590 &&
              st.right == grid_tile_rect(st.cols - 1, st.cols).x1,
          "standard: 3 columns of 154 at gutter 4 and pitch 158, 4 focus and band rows, pad 2, right edge 590");
    GridSpec lg = grid_spec(TileMode::Large);
    check(lg.cols == 2 && lg.side == 232 && lg.gutter == 16 && lg.pitch == 248 && lg.focus_rows == 2 &&
              lg.band_rows == 3 && lg.pad_rows == 2 && lg.right == 600 && lg.right == big_tile_rect(lg.cols - 1).x1,
          "large: 2 columns of 232 at gutter 16 and pitch 248, 2 focus rows, 3 band rows, pad 2, right edge 600");
    check(rows_fit(st) && rows_fit(lg),
          "focus rows are the whole rows inside the band, band rows the rows that meet it, in both modes");

    bool ends = true;
    for (int k = 0; k < 12; k++)
        for (int q = 0; q <= 4; q++) {
            Rect big = big_tile_rect(k);
            big.y0 -= q * kBigPitch;
            big.y1 -= q * kBigPitch;
            if (!same64(grow_tile_rect(k, 0, q), grid_tile_rect(k, kGridCols)) ||
                !same64(grow_tile_rect(k, kOne, q), big))
                ends = false;
        }
    check(ends, "the grow pose is the collapsed rect at g 0 and the large rect at g 1, games 0 to 11, scroll 0 to 4");
    bool grows = true;
    int side = 0, pitch = 0;
    for (fx g = 0; g <= kOne; g += kOne / 256) {
        Rect64 a = grow_tile_rect(0, g, 0), b = grow_tile_rect(3, g, 0);
        int s = a.x1 - a.x0, p = b.y0 - a.y0;
        if (s < side || p < pitch || b.x0 - a.x0 != p || b.x1 - b.x0 != s)
            grows = false;
        side = s;
        pitch = p;
    }
    check(grows, "the grow pose stays rigid and its side and pitch never shrink as g rises");

    bool rows = true;
    for (int k = 0; k < 40; k++)
        if (slot_of(k, 3, SlotMap::RowMajor) != k || game_at(k, 3, SlotMap::RowMajor, 40) != k)
            rows = false;
    check(rows && game_at(12, 3, SlotMap::RowMajor, 12) == -1, "row major slots are the game index");

    const int want_c[12] = {0, 1, 3, 4, 6, 7, 9, 10, 2, 5, 8, 11};
    bool cmap = true;
    for (int k = 0; k < 12; k++)
        if (slot_of(k, 3, SlotMap::RecentC) != want_c[k])
            cmap = false;
    check(cmap, "recent (c): games 0 to 7 keep their 2 column slots, 8 to 11 fill column 2 top to bottom");
    check(slot_of(12, 3, SlotMap::RecentC) == -1, "recent (c) has no slot for a 13th game");

    bool inverse = true;
    for (int n = 0; n <= 12; n++)
        for (int k = 0; k < n; k++)
            if (game_at(slot_of(k, 3, SlotMap::RecentC), 3, SlotMap::RecentC, n) != k)
                inverse = false;
    check(inverse, "game_at inverts slot_of under recent (c) for every count");
    check(game_at(2, 3, SlotMap::RecentC, 8) == -1 && game_at(2, 3, SlotMap::RecentC, 9) == 8,
          "recent (c): column 2 stays empty at 8 games and takes the 9th");
    check(game_at(5, 3, SlotMap::RecentC, 9) == -1 && game_at(9, 3, SlotMap::RecentC, 12) == 6,
          "recent (c): empty slots read -1, row 3 column 0 is game 6");

    bool two = true;
    for (int k = 0; k < 8; k++)
        if (slot_of(k, 2, SlotMap::RecentC) != k || game_at(k, 2, SlotMap::RecentC, 8) != k)
            two = false;
    check(two, "at 2 columns recent (c) is row major");

    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
