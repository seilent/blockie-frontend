#include "screen_selector.h"

#include <cstdio>

#include "../ui/roles.h"

namespace fd {

namespace {

struct System {
    const char* name;
    int games;
};

constexpr System kSystems[] = {
    {"Game Boy", 28},       {"Game Boy Color", 19}, {"NES", 63},
    {"SNES", 47},           {"Genesis", 35},        {"Game Boy Advance", 52},
};

constexpr int kLabelX = 512;
constexpr int kFirstBaseline = 96;
constexpr int kPitch = 96;
constexpr int kSubtitleX = 532;
constexpr int kSubtitleDrop = 27;
constexpr int kFocusBarX = 496;
constexpr int kFocusBarWidth = 4;
constexpr int kFocusBarTopInset = 48;
constexpr int kFocusBarHeight = 75;
constexpr int kWatermarkX = 512;
constexpr int kWatermarkBaseline = 688;
constexpr const char* kWatermarkWord = "blockie";

constexpr int kGridX = 120;
constexpr int kTileSide = 154;
constexpr int kGridGutter = 4;
constexpr int kGridCols = 2;
constexpr int kGridRows = 4;
constexpr int kGridRowPitch = 158;
constexpr int kSectionBaseline = 70;
constexpr int kTileFirstTop = 80;
constexpr const char* kSectionLabel = "Recent";

int x64(int px) { return px * 64; }

void count_line(char* out, size_t n, int games) { snprintf(out, n, games == 1 ? "%d game" : "%d games", games); }

}

void draw_selector(Canvas& c, Fonts& fonts, Pane focus)
{
    bool on = focus == Pane::Selector;

    c.fill(c.surface().rect(), tok::color_bg);

    fx pane = on ? tok::motion_quickplayAlpha : tok::alpha_full;
    int tile_alpha = alpha256(pane);
    fonts.draw(c, role::quickplayHeader.style(), kSectionLabel, x64(kGridX), kSectionBaseline, kWhite,
               alpha256(fx_mul(role::quickplayHeader.alpha, pane)));
    for (int r = 0; r < kGridRows; r++)
        for (int col = 0; col < kGridCols; col++) {
            int x = kGridX + col * (kTileSide + kGridGutter);
            int y = kTileFirstTop + r * kGridRowPitch;
            c.blend({x, y, x + kTileSide, y + kTileSide}, tok::color_tile, tile_alpha);
        }

    int focus_item = on ? 0 : -1;
    for (int i = 0; i < (int)(sizeof kSystems / sizeof kSystems[0]); i++) {
        int baseline = kFirstBaseline + i * kPitch;
        int label_alpha = alpha256(i == focus_item ? role::homeItem.alpha : tok::type_homeItem_alphaDim);
        fonts.draw(c, role::homeItem.style(), kSystems[i].name, x64(kLabelX), baseline, kWhite, label_alpha);

        char sub[32];
        count_line(sub, sizeof sub, kSystems[i].games);
        fonts.draw(c, role::homeSubtitle.style(), sub, x64(kSubtitleX), baseline + kSubtitleDrop, kWhite,
                   alpha256(role::homeSubtitle.alpha));

        if (i == focus_item) {
            int top = baseline - kFocusBarTopInset;
            c.fill({kFocusBarX, top, kFocusBarX + kFocusBarWidth, top + kFocusBarHeight}, kWhite);
        }
    }

    int watermark_alpha = on ? alpha256(role::watermark.alpha) : 0;
    fonts.draw(c, role::watermark.style(), kWatermarkWord, x64(kWatermarkX), kWatermarkBaseline, kWhite,
               watermark_alpha);
}

}
