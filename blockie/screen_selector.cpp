#include "screen_selector.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <utility>

#include "../ui/roles.h"
#include "text_wrap.h"
#include "tile_grid.h"
#include "vtext.h"

namespace fd {

namespace {

constexpr int kLabelX = 512;
constexpr int kFirstBaseline = kMenuFirstBaseline;
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

namespace {

constexpr int kTurnStart = std_px(tok::turnedLineStart);
constexpr int kTurnEnd = std_px(tok::edge);
constexpr int kTurnWrap = kTurnStart - kTurnEnd;
constexpr int kTurnTitleStep = 64;
constexpr int kTurnTitleLines = 3;
constexpr size_t kTurnWrapKeep = 4;
constexpr int kBandTop = kTileFirstTop - kTileFocusBorder;
constexpr int kBandBottom = kTileFirstTop + (kGridRows - 1) * kGridRowPitch + kTileSide + kTileFocusBorder;

VText& turned_cache()
{
    static VText cache;
    return cache;
}

const std::vector<std::string>& turned_title(Fonts& fonts, const std::string& title)
{
    static std::vector<std::pair<std::string, std::vector<std::string>>> keep;
    for (auto it = keep.begin(); it != keep.end(); ++it)
        if (it->first == title) {
            std::rotate(keep.begin(), it, it + 1);
            return keep.front().second;
        }
    TextStyle ts = role::npTitle.style();
    auto measure = [&](const std::string& s) { return (int)fonts.width64(ts, s.c_str()); };
    std::vector<std::string> lines = wrap_lines(title, kTurnWrap * 64, kTurnTitleLines, measure);
    keep.insert(keep.begin(), {title, std::move(lines)});
    if (keep.size() > kTurnWrapKeep)
        keep.pop_back();
    return keep.front().second;
}

int turn_x(const SelectorModel& m)
{
    int right = m.big ? big_tile_rect(kBigCols - 1).x1 : grid_tile_rect(kGridColsWide - 1, kGridColsWide).x1;
    return right + (kDetailX - kPaneSplit);
}

void turned_line(Canvas& c, Fonts& fonts, const SelectorModel& m, const TextStyle& st, const std::string& text,
                 int index, const std::string& line, int ascent_x, int shift, uint32_t ink, int a)
{
    if (a <= 0 || line.empty())
        return;
    Rect prev = c.clip();
    Rect sr = c.surface().rect();
    Rect pane{kPaneSplitWide, sr.y0, sr.x1, sr.y1};
    if (m.t9_edge && shift < 0)
        pane.y1 = std::min(pane.y1, kTurnStart);
    c.set_clip(pane.intersect(prev));
    turned_cache().draw(c, fonts, st, text, kTurnWrap, index, line, ascent_x, kTurnStart - shift, sr.y1, ink, a);
    c.set_clip(prev);
}

void draw_turned_detail(Canvas& c, Fonts& fonts, const SelectorModel& model, int x0, const Theme& th)
{
    TextStyle as = role::npArtist.style();
    TextStyle ts = role::npTitle.style();
    int sys_base = x0 + fonts.ascent(as.px);
    turned_line(c, fonts, model, as, model.game_system, 0, model.game_system, x0, model.turn_shift, th.ink,
                alpha256(role::npArtist.alpha) * model.turn_a256 >> 8);
    const std::vector<std::string>& lines = turned_title(fonts, model.game_title);
    int asc = fonts.ascent(ts.px);
    int pitch = asc + fonts.descent(ts.px);
    int ta = (alpha256(role::npTitle.alpha) * model.title_a256 >> 8) * model.turn_a256 >> 8;
    for (size_t li = 0; li < lines.size(); li++) {
        int base = sys_base + kTurnTitleStep + (int)li * pitch;
        turned_line(c, fonts, model, ts, model.game_title, (int)li, lines[li], base - asc,
                    model.turn_shift + model.title_shift, th.ink, ta);
    }
}

void draw_stacked_menu(Canvas& c, Fonts& fonts, const SelectorModel& model, int x0, const Theme& th)
{
    TextStyle ls = role::homeItem.style();
    TextStyle ss = role::homeSubtitle.style();
    int la = alpha256(tok::type_homeItem_alphaDim);
    int sa = alpha256(role::homeSubtitle.alpha);
    Rect prev = c.clip();
    Rect sr = c.surface().rect();
    c.set_clip(Rect{kPaneSplitWide, sr.y0, sr.x1, sr.y1}.intersect(prev));
    int slot_lo = kFirstBaseline - kPitch / 2;
    int slot_hi = kFirstBaseline + (kMenuVisibleRows - 1) * kPitch + kPitch / 2;
    for (int i = 0; i < (int)model.systems.size(); i++) {
        int slot = kFirstBaseline + i * kPitch - model.menu_top_px;
        if (slot <= slot_lo || slot >= slot_hi)
            continue;
        const SelectorItem& item = model.systems[i];
        fonts.draw(c, ls, item.name.c_str(), x64(x0), slot, th.ink, la);
        std::string sub = item.subtitle;
        if (sub.empty()) {
            char count[32];
            count_line(count, sizeof count, item.games);
            sub = count;
        }
        fonts.draw(c, ss, sub.c_str(), x64(x0 + (kSubtitleX - kLabelX)), slot + kSubtitleDrop, th.ink, sa);
    }
    c.set_clip(prev);
}

void draw_expanded(Canvas& c, Fonts& fonts, const SelectorModel& model, bool detail, const Theme& th)
{
    c.fill(c.surface().rect(), th.bg);
    int cols = model.big ? kBigCols : kGridColsWide;
    int tile_alpha = alpha256(tok::alpha_full) * model.grid_a256 >> 8;
    Rect prev = c.clip();
    Rect band = model.grid_band ? Rect{0, kBandTop, kPaneSplitWide, kBandBottom} : c.surface().rect();
    band = band.intersect(prev);
    auto slot_rect = [&](int k) {
        int s = slot_of(k, cols, model.slots);
        Rect r = model.big ? big_tile_rect(s) : grid_tile_rect(s, cols);
        r.y0 += model.grid_y_px;
        r.y1 += model.grid_y_px;
        return r;
    };
    for (int k = 0; k < (int)model.tiles.size(); k++) {
        const Image* art = model.tiles[k].get();
        if (!art || art->w <= 0 || art->h <= 0 || slot_of(k, cols, model.slots) < 0)
            continue;
        Rect tr = slot_rect(k);
        Rect vis = tr.intersect(band);
        if (vis.empty())
            continue;
        int side = tr.x1 - tr.x0;
        c.set_clip(vis);
        if (art->w == side && art->h == side) {
            c.blit(*art, tr.x0, tr.y0, tile_alpha);
        } else {
            Rect fr = fit_rect(art->w, art->h, tr.x0, tr.y0, side);
            c.blit_bilinear(*art, Rect64::of(fr, Zoom{}), tile_alpha, true);
        }
    }
    c.set_clip(band);
    if (model.tile_focus >= 0 && model.tile_focus < (int)model.tiles.size() &&
        slot_of(model.tile_focus, cols, model.slots) >= 0) {
        Rect tr = slot_rect(model.tile_focus);
        c.frame_aa(Rect64::of(tr.grow(kTileFocusBorder), Zoom{}), Rect64::of(tr, Zoom{}), th.focus,
                   model.grid_a256);
    }
    c.set_clip(prev);
    if (prev.x1 > kPaneSplitWide)
        draw_turned(c, fonts, model, detail, th);
}

}

void draw_turned(Canvas& c, Fonts& fonts, const SelectorModel& model, bool detail, const Theme& th)
{
    int x0 = turn_x(model);
    if (!detail)
        draw_stacked_menu(c, fonts, model, x0, th);
    else if (model.turn_a256 > 0)
        draw_turned_detail(c, fonts, model, x0, th);
}

void warm_turned(Fonts& fonts, const SelectorModel& model)
{
    if (!model.game_system.empty())
        turned_cache().line(fonts, role::npArtist.style(), model.game_system, kTurnWrap, 0, model.game_system);
    const std::vector<std::string>& lines = turned_title(fonts, model.game_title);
    for (size_t li = 0; li < lines.size(); li++)
        if (!lines[li].empty())
            turned_cache().line(fonts, role::npTitle.style(), model.game_title, kTurnWrap, (int)li, lines[li]);
}

size_t turned_rasters() { return turned_cache().rasters(); }

Rect header_rect(Fonts& fonts, const SelectorModel& model)
{
    const char* header = model.section_label.empty() ? kSectionLabel : model.section_label.c_str();
    TextStyle st = role::quickplayHeader.style();
    int left64 = 0, right64 = 0;
    fonts.ink64(st, header, left64, right64);
    int adv64 = fonts.width64(st, header);
    int x0 = kGridX + std::min(0, left64 >> 6);
    int x1 = kGridX + ((std::max(right64, adv64) + 63) >> 6) + 1;
    return {x0, 0, x1, kTileFirstTop - kTileFocusBorder};
}

void draw_selector(Canvas& c, Fonts& fonts, const SelectorModel& model, Pane focus_pane, bool scatter, bool detail,
                   const Theme& th, bool expanded)
{
    if (expanded) {
        draw_expanded(c, fonts, model, detail, th);
        return;
    }
    bool on = focus_pane == Pane::Selector;

    c.fill(c.surface().rect(), th.bg);
    if (scatter)
        draw_scatter_dots(c, th.ink);

    fx pane = on ? tok::motion_quickplayAlpha : tok::alpha_full;
    fx head_pane = model.header_dim ? tok::motion_quickplayAlpha : pane;
    int tile_alpha = alpha256(pane) * model.grid_a256 >> 8;
    const char* header = model.section_label.empty() ? kSectionLabel : model.section_label.c_str();
    fonts.draw(c, role::quickplayHeader.style(), header, x64(kGridX), kSectionBaseline, th.ink,
               alpha256(fx_mul(role::quickplayHeader.alpha, head_pane)) * model.grid_a256 >> 8);
    for (int k = 0; k < kGridTiles && k < (int)model.tiles.size(); k++) {
        const Image* art = model.tiles[k].get();
        if (!art || art->w <= 0 || art->h <= 0)
            continue;
        Rect tr = grid_tile_rect(k, kGridCols);
        c.blit(*art, tr.x0 + (kTileSide - art->w) / 2, tr.y0 + (kTileSide - art->h) / 2, tile_alpha);
    }

    if (!on && model.tile_focus >= 0 && model.tile_focus < kGridTiles) {
        Rect tr = grid_tile_rect(model.tile_focus, kGridCols);
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
        if (on && i < (int)model.label_a256.size())
            label_alpha = model.label_a256[i] * ps.a256 >> 8;
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

        if (i == focus_item && model.bar_baseline == kBarAtFocus) {
            int top = baseline - kFocusBarTopInset;
            c.blend({kFocusBarX + ps.dx, top, kFocusBarX + ps.dx + kFocusBarWidth, top + kFocusBarHeight}, th.focus,
                    model.bar_a256);
        }
    }

    if (on && model.bar_baseline != kBarAtFocus && model.bar_baseline > slot_lo && model.bar_baseline < slot_hi) {
        int dx = pose_of(model, model.focus).dx;
        int top = model.bar_baseline - kFocusBarTopInset;
        c.blend({kFocusBarX + dx, top, kFocusBarX + dx + kFocusBarWidth, top + kFocusBarHeight}, th.focus,
                model.bar_a256);
    }

    int watermark_alpha = on ? alpha256(role::watermark.alpha) : 0;
    fonts.draw(c, role::watermark.style(), kWatermarkWord, x64(kWatermarkX), kWatermarkBaseline, th.ink,
               watermark_alpha);
}

}
