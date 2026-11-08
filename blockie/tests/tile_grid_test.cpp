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
