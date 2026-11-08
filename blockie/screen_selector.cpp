#include "screen_selector.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>

#include "../ui/roles.h"
#include "text_wrap.h"
#include "tile_grid.h"

namespace fd {

namespace {

constexpr int kLabelX = 512;
constexpr int kFirstBaseline = 96;
constexpr int kPitch = kMenuPitchPx;
constexpr int kSubtitleX = 532;
constexpr int kSubtitleDrop = 35;
constexpr int kFocusBarX = 496;
constexpr int kFocusBarWidth = 4;
constexpr int kFocusBarTopInset = 56;
constexpr int kFocusBarHeight = 83;
constexpr int kWatermarkX = 512;
constexpr int kWatermarkBaseline = 688;
constexpr const char* kWatermarkWord = "blockie";

constexpr int kSectionBaseline = 70;
constexpr const char* kSectionLabel = "Recent";

constexpr int kDetailX = 512;
constexpr int kDetailRight = 928;
constexpr int kDetailSystemBaseline = 120;
constexpr int kDetailTitleBaseline = 232;
constexpr int kDetailTitleMaxLines = 4;

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

void draw_scatter_dots(Canvas& c, uint32_t ink)
{
    Rect sr = c.surface().rect();
    uint32_t s = kScatterSeed;
    for (int y = sr.y0; y + kScatterDot <= sr.y1; y += kScatterGrid)
        for (int x = sr.x0; x + kScatterDot <= sr.x1; x += kScatterGrid) {
            if (scatter_next(s) % 1000 >= (uint32_t)kScatterPerThousand)
                continue;
            int a = kScatterAlphaLo + (int)(scatter_next(s) % (kScatterAlphaHi - kScatterAlphaLo + 1));
            c.blend({x, y, x + kScatterDot, y + kScatterDot}, ink, a);
        }
}

void count_line(char* out, size_t n, int games) { snprintf(out, n, games == 1 ? "%d game" : "%d games", games); }

std::vector<std::string> title_lines(Fonts& fonts, const std::string& title)
{
    TextStyle ts = role::npTitle.style();
    int avail64 = (kDetailRight - kDetailX) * 64;
    return wrap_lines(title, avail64, kDetailTitleMaxLines,
                      [&](const std::string& s) { return (int)fonts.width64(ts, s.c_str()); });
}

MenuPose pose_of(const SelectorModel& model, int i)
{
    if (i >= 0 && i < (int)model.poses.size())
        return model.poses[i];
    return {};
}

}

int detail_title_bottom(Fonts& fonts, const SelectorModel& model)
{
    TextStyle ts = role::npTitle.style();
    int pitch = fonts.ascent(ts.px) + fonts.descent(ts.px);
    std::vector<std::string> lines = title_lines(fonts, model.game_title);
    int n = std::max(1, (int)lines.size());
    return kDetailTitleBaseline + (n - 1) * pitch + fonts.descent(ts.px);
}

void draw_selector(Canvas& c, Fonts& fonts, const SelectorModel& model, Pane focus_pane, ArtFit, bool scatter,
                   bool detail, const Theme& th)
{
    bool on = focus_pane == Pane::Selector;

    c.fill(c.surface().rect(), th.bg);
    if (scatter)
        draw_scatter_dots(c, th.ink);

    fx pane = on ? tok::motion_quickplayAlpha : tok::alpha_full;
    int tile_alpha = alpha256(pane) * model.grid_a256 >> 8;
    const char* header = model.section_label.empty() ? kSectionLabel : model.section_label.c_str();
    fonts.draw(c, role::quickplayHeader.style(), header, x64(kGridX), kSectionBaseline, th.ink,
               alpha256(fx_mul(role::quickplayHeader.alpha, pane)) * model.grid_a256 >> 8);
    for (int k = 0; k < kGridTiles && k < (int)model.tiles.size(); k++) {
        const Image* art = model.tiles[k].get();
        if (!art || art->w <= 0 || art->h <= 0)
            continue;
        Rect tr = grid_tile_rect(k);
        c.blit(*art, tr.x0 + (kTileSide - art->w) / 2, tr.y0 + (kTileSide - art->h) / 2, tile_alpha);
    }

    if (!on && model.tile_focus >= 0 && model.tile_focus < kGridTiles) {
        Rect tr = grid_tile_rect(model.tile_focus);
        c.frame_aa(Rect64::of(tr.grow(kTileFocusBorder), Zoom{}), Rect64::of(tr, Zoom{}), th.focus,
                   model.grid_a256);
    }

    if (detail) {
        Rect prev = c.clip();
        Rect sr = c.surface().rect();
        c.set_clip({kDetailX, sr.y0, kDetailRight, sr.y1});
        fonts.draw(c, role::npArtist.style(), model.game_system.c_str(), x64(kDetailX), kDetailSystemBaseline, th.ink,
                   alpha256(role::npArtist.alpha));
        TextStyle ts = role::npTitle.style();
        int title_alpha = alpha256(role::npTitle.alpha);
        int pitch = fonts.ascent(ts.px) + fonts.descent(ts.px);
        std::vector<std::string> lines = title_lines(fonts, model.game_title);
        for (size_t li = 0; li < lines.size(); li++)
            fonts.draw(c, ts, lines[li].c_str(), x64(kDetailX), kDetailTitleBaseline + (int)li * pitch, th.ink,
                       title_alpha);
        c.set_clip(prev);
        return;
    }

    TextStyle sub_style = role::homeSubtitle.style();
    int slot_lo = kFirstBaseline - kPitch / 2;
    int slot_hi = kFirstBaseline + (kMenuVisibleRows - 1) * kPitch + kPitch / 2;
    int focus_item = on ? model.focus : -1;
    for (int i = 0; i < (int)model.systems.size(); i++) {
        MenuPose ps = pose_of(model, i);
        int slot = kFirstBaseline + i * kPitch - model.menu_top_px;
        if (slot <= slot_lo || slot >= slot_hi)
            continue;
        int baseline = slot;
        int label_alpha =
            alpha256(i == focus_item ? role::homeItem.alpha : tok::type_homeItem_alphaDim) * ps.a256 >> 8;
        fonts.draw(c, role::homeItem.style(), model.systems[i].name.c_str(), x64(kLabelX + ps.dx), baseline, th.ink,
                   label_alpha);

        std::string sub = model.systems[i].subtitle;
        if (sub.empty()) {
            char count[32];
            count_line(count, sizeof count, model.systems[i].games);
            sub = count;
        }
        fonts.draw(c, sub_style, sub.c_str(), x64(kSubtitleX + ps.dx), baseline + kSubtitleDrop, th.ink,
                   alpha256(role::homeSubtitle.alpha) * ps.a256 >> 8);

        if (i == focus_item) {
            int top = baseline - kFocusBarTopInset;
            c.blend({kFocusBarX + ps.dx, top, kFocusBarX + ps.dx + kFocusBarWidth, top + kFocusBarHeight}, th.focus,
                    model.bar_a256);
        }
    }

    int watermark_alpha = on ? alpha256(role::watermark.alpha) : 0;
    fonts.draw(c, role::watermark.style(), kWatermarkWord, x64(kWatermarkX), kWatermarkBaseline, th.ink,
               watermark_alpha);
}

}
