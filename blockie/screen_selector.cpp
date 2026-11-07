#include "screen_selector.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>

#include "../ui/roles.h"

namespace fd {

namespace {

constexpr int kLabelX = 512;
constexpr int kFirstBaseline = 96;
constexpr int kPitch = 96;
constexpr int kSubtitleX = 532;
constexpr int kSubtitleDrop = 35;
constexpr int kFocusBarX = 496;
constexpr int kFocusBarWidth = 4;
constexpr int kFocusBarTopInset = 56;
constexpr int kFocusBarHeight = 83;
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
constexpr int kTileFocusBorder = 4;
constexpr const char* kSectionLabel = "Recent";

constexpr int kDetailX = 512;
constexpr int kDetailSystemBaseline = 120;
constexpr int kDetailTitleBaseline = 232;

constexpr int kScatterGrid = 6;
constexpr int kScatterDot = 6;
constexpr int kScatterAlphaLo = 7;
constexpr int kScatterAlphaHi = 27;
constexpr int kScatterPerThousand = 70;
constexpr uint32_t kScatterSeed = 0x9e3779b9u;

int x64(int px) { return px * 64; }

uint32_t scatter_next(uint32_t& s)
{
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

void draw_scatter_dots(Canvas& c)
{
    Rect sr = c.surface().rect();
    uint32_t s = kScatterSeed;
    for (int y = sr.y0; y + kScatterDot <= sr.y1; y += kScatterGrid)
        for (int x = sr.x0; x + kScatterDot <= sr.x1; x += kScatterGrid) {
            if (scatter_next(s) % 1000 >= (uint32_t)kScatterPerThousand)
                continue;
            int a = kScatterAlphaLo + (int)(scatter_next(s) % (kScatterAlphaHi - kScatterAlphaLo + 1));
            c.blend({x, y, x + kScatterDot, y + kScatterDot}, kWhite, a);
        }
}

void count_line(char* out, size_t n, int games) { snprintf(out, n, games == 1 ? "%d game" : "%d games", games); }

Rect fit_rect(int sw, int sh, int tx, int ty, int side, ArtFit fit)
{
    if (sw <= 0 || sh <= 0)
        return {tx, ty, tx + side, ty + side};
    double s = (double)side / sw;
    double sy = (double)side / sh;
    s = fit == ArtFit::Cover ? std::max(s, sy) : std::min(s, sy);
    int w = (int)(sw * s + 0.5);
    int h = (int)(sh * s + 0.5);
    int x = tx + (side - w) / 2;
    int y = ty + (side - h) / 2;
    return {x, y, x + w, y + h};
}

}

void draw_selector(Canvas& c, Fonts& fonts, const SelectorModel& model, Pane focus_pane, ArtFit fit, bool scatter,
                   bool detail)
{
    bool on = focus_pane == Pane::Selector;

    c.fill(c.surface().rect(), tok::color_bg);
    if (scatter)
        draw_scatter_dots(c);

    fx pane = on ? tok::motion_quickplayAlpha : tok::alpha_full;
    int tile_alpha = alpha256(pane);
    const char* header = model.section_label.empty() ? kSectionLabel : model.section_label.c_str();
    fonts.draw(c, role::quickplayHeader.style(), header, x64(kGridX), kSectionBaseline, kWhite,
               alpha256(fx_mul(role::quickplayHeader.alpha, pane)));
    for (int r = 0; r < kGridRows; r++)
        for (int col = 0; col < kGridCols; col++) {
            int x = kGridX + col * (kTileSide + kGridGutter);
            int y = kTileFirstTop + r * kGridRowPitch;
            Rect tr{x, y, x + kTileSide, y + kTileSide};
            c.blend(tr, tok::color_tile, tile_alpha);
            int k = r * kGridCols + col;
            if (k >= (int)model.tiles.size() || model.tiles[k].w <= 0 || model.tiles[k].h <= 0)
                continue;
            const Image& art = model.tiles[k];
            Rect fr = fit_rect(art.w, art.h, x, y, kTileSide, fit);
            if (fit == ArtFit::Cover) {
                Rect prev = c.clip();
                c.set_clip(tr);
                c.blit_bilinear(art, Rect64::of(fr, Zoom{}), tile_alpha, true);
                c.set_clip(prev);
            } else {
                c.blit_bilinear(art, Rect64::of(fr, Zoom{}), tile_alpha, true);
            }
        }

    if (!on && model.tile_focus >= 0 && model.tile_focus < kGridRows * kGridCols) {
        int col = model.tile_focus % kGridCols;
        int r = model.tile_focus / kGridCols;
        int x = kGridX + col * (kTileSide + kGridGutter);
        int y = kTileFirstTop + r * kGridRowPitch;
        Rect tr{x, y, x + kTileSide, y + kTileSide};
        c.frame_aa(Rect64::of(tr.grow(kTileFocusBorder), Zoom{}), Rect64::of(tr, Zoom{}), kWhite, 256);
    }

    if (detail) {
        fonts.draw(c, role::npArtist.style(), model.game_system.c_str(), x64(kDetailX), kDetailSystemBaseline, kWhite,
                   alpha256(role::npArtist.alpha));
        fonts.draw(c, role::npTitle.style(), model.game_title.c_str(), x64(kDetailX), kDetailTitleBaseline, kWhite,
                   alpha256(role::npTitle.alpha));
        return;
    }

    int focus_item = on ? model.focus : -1;
    for (int i = 0; i < (int)model.systems.size(); i++) {
        int baseline = kFirstBaseline + i * kPitch;
        int label_alpha = alpha256(i == focus_item ? role::homeItem.alpha : tok::type_homeItem_alphaDim);
        fonts.draw(c, role::homeItem.style(), model.systems[i].name.c_str(), x64(kLabelX), baseline, kWhite,
                   label_alpha);

        char sub[32];
        count_line(sub, sizeof sub, model.systems[i].games);
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
