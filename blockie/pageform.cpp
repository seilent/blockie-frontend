#include "pageform.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>

#include "screen_selector.h"
#include "theme.h"
#include "tile_grid.h"

namespace fd {

namespace {

constexpr int kW = kPortraitW;
constexpr int kH = kPortraitH;
constexpr int kSettleX = std_px(41 * kOne / 2);
constexpr int kSettleAxis = kH - std_px((fx)(35.3 * kOne + 0.5));
constexpr fx kSettleRise = (fx)(0.21 * kOne + 0.5);
constexpr int kMenuCut = 37;
constexpr int kLeaveZ = 1000;
constexpr int kPressedZ = 2000;
constexpr int kHeadZ = kLeaveZ - 2;
constexpr int kHeadOverZ = kPressedZ + 1;
constexpr int kHeadBand = std_px(tok::headerBand);
constexpr int kStripTop = std_px(tok::pivotTop);
constexpr int kStripBottom = std_px(tok::pivotBottom);
constexpr int kHeadCap =
    (int)(3 * (3 * (int64_t)(eng::kFoldMaxMs - eng::kTailMs) - eng::kStepThirds) / eng::kStepThirds);
static_assert(kHeadBand == 140 && kStripTop == 116 && kStripBottom == 172,
              "the word's band 70 u, the strip 58 to 86 u");
static_assert(kHeadCap == 20, "a header item starts at most at index 20");
static_assert((kHeadCap * eng::kStepThirds + 1) / 3 + eng::kStepThirds + 3 * eng::kTailMs <= 3 * eng::kFoldMaxMs,
              "an item at the cap lands with its tail inside 800 ms");
static_assert(kSettleX == 41, "the settle starts 20.5 u right");
static_assert(kSettleAxis == 649, "the rows unfold about H - 35.3 u");
static_assert(kSettleRise == kOne - (fx)(0.79 * kOne + 0.5), "a row rises 0.21 of its distance to the axis, sy 0.79");
static_assert(kMenuCut + kMenuVisibleRows * kMenuPitchPx == 613, "the menu items end where the watermark starts");

int add_block(eng::Frame& fr, const PageInk& k, int source, Rect src)
{
    eng::Block b;
    b.id = (uint16_t)(fr.blocks.size() + 1);
    b.ink = k.ink;
    b.z = source;
    b.source = source;
    b.src = src;
    fr.blocks.push_back(b);
    eng::Shade s;
    s.step = k.ink == eng::Ink::Art ? eng::kArtStep : eng::kTextStep;
    fr.shades.push_back(s);
    return (int)fr.blocks.size() - 1;
}

void add_pose(eng::Frame& fr, int block, int dx, Rect clip, int dy = 0)
{
    const Rect& s = fr.blocks[(size_t)block].src;
    eng::Pose q;
    q.block = block;
    q.rect = {s.x0 + dx, s.y0 + dy, s.x1 + dx, s.y1 + dy};
    q.clip = clip;
    fr.poses.push_back(q);
}

bool left_pane(const PageRow& e) { return e.r.x0 == 0 && e.r.x1 < kW; }

constexpr int64_t kPartLeg3 = (int64_t)eng::kPageSteps * eng::kStepThirds;
constexpr int64_t kPartStagger3 = eng::kStepThirds;
constexpr int kPartCap = (int)((3 * (int64_t)(eng::kFoldMaxMs - eng::kTailMs) - kPartLeg3) / kPartStagger3) - 1;
static_assert(kPartCap == 3 &&
                  (kPartCap + 1) * kPartStagger3 + kPartLeg3 + 3 * (int64_t)eng::kTailMs <= 3 * (int64_t)eng::kFoldMaxMs,
              "S caps the bands at 3 inside 800 ms");

int part_move(int dist, int64_t lt3)
{
    int d = std::abs(dist);
    if (d == 0 || lt3 <= 0)
        return 0;
    int m = (int)(((int64_t)d * eng::step_progress(lt3, eng::kPageSteps) + (int64_t)eng::kPageSteps * kOne / 2) /
                  ((int64_t)eng::kPageSteps * kOne));
    return dist < 0 ? -m : m;
}

int settle_dy(const PageRow& e)
{
    return e.r.y0 == 0 ? 0 : fx_round(fx_mul(fx_int(kSettleAxis - e.r.y0), kSettleRise));
}

std::vector<int> ranks(const std::vector<PageRow>& els)
{
    std::vector<int> order(els.size());
    for (size_t i = 0; i < order.size(); i++)
        order[i] = (int)i;
    std::stable_sort(order.begin(), order.end(),
                     [&](int a, int b) { return els[(size_t)a].r.y0 < els[(size_t)b].r.y0; });
    std::vector<int> rank(els.size());
    for (size_t r = 0; r < order.size(); r++)
        rank[(size_t)order[r]] = (int)r;
    return rank;
}

int band_in(const std::vector<int>& bands, const PageRow& e)
{
    int band = 0;
    for (size_t i = 0; i < bands.size(); i++)
        if (bands[i] <= e.r.y0)
            band = (int)i;
    return std::min(band, kPartCap) + (e.pressed ? 1 : 0);
}

int add_el(eng::Frame& fr, const PageRow& e, int source, int z, std::vector<std::string>* names)
{
    int b = add_block(fr, {e.r, e.ink}, source, e.r);
    fr.blocks[(size_t)b].z = z;
    if (names)
        names->push_back(e.name);
    return b;
}

int head_move(int dist, int64_t lt3) { return fx_round(fx_mul(fx_int(dist), eng::step_progress(lt3, 1))); }

void head_frame(const PageHead& h, int side, bool back, int64_t t3, eng::Frame& fr, std::vector<std::string>* names)
{
    auto item = [&](const PageRow& e, int source, int order, Rect clip, int drop, int z) {
        int b = add_el(fr, e, source, z, names);
        int64_t lt3 = t3 - head_start3(std::min(order, kHeadCap));
        int dy = back ? head_move(drop, lt3) - drop : head_move(kSettleX, lt3) - kSettleX;
        add_pose(fr, b, 0, clip, dy);
    };
    assert(h.rank.size() == h.tabs.size());
    int z = back ? kHeadOverZ : kHeadZ;
    for (size_t k = 0; k < h.letters.size(); k++)
        item(h.letters[k], h.layer ? 2 + side : side, (int)k, {0, 0, kW, kHeadBand}, kHeadBand, z);
    for (size_t j = 0; j < h.tabs.size(); j++)
        item(h.tabs[j], side, h.rank[j], {0, kStripTop, kW, kStripBottom}, kStripBottom - kStripTop, z + 1);
}

std::vector<int> row_groups(const PageForm& f, Rect col)
{
    std::vector<int> ys{col.y0};
    for (const PageRow& e : f.under_els)
        for (int y : {e.r.y0, e.r.y1})
            if (y > col.y0 && y < col.y1)
                ys.push_back(y);
    std::sort(ys.begin(), ys.end());
    ys.erase(std::unique(ys.begin(), ys.end()), ys.end());
    return ys;
}

bool covered(const PageForm& f, const std::vector<int>& bands, const std::vector<int>& ys, Rect col, int64_t t3,
             std::vector<std::pair<int, int>>& spans)
{
    for (int y : ys) {
        spans.clear();
        for (const PageRow& e : f.under_els) {
            if (y < e.r.y0 || y >= e.r.y1)
                continue;
            int dist = left_pane(e) ? e.r.x1 : kW - e.r.x0;
            int m = part_move(dist, t3 - part_start3(band_in(bands, e)));
            int dx = left_pane(e) ? -m : m;
            spans.push_back({e.r.x0 + dx, e.r.x1 + dx});
        }
        std::sort(spans.begin(), spans.end());
        int x = col.x0;
        for (const std::pair<int, int>& sp : spans)
            if (sp.first <= x)
                x = std::max(x, sp.second);
        if (x < col.x1)
            return false;
    }
    return true;
}

void part_frame(const PageForm& f, int64_t t, eng::Frame& fr, std::vector<std::string>* names = nullptr)
{
    bool back = f.closing;
    int64_t t3 = back ? std::max<int64_t>(0, form_thirds(f) - 3 * t) : 3 * t;
    std::vector<int> rin = ranks(f.over_els), rout = ranks(f.under_els), bands = part_bands(f);
    bool hunder = false;
    bool head = head_side(f, hunder);
    for (size_t i = 0; i < f.over_els.size(); i++) {
        const PageRow& e = f.over_els[i];
        if (head && !hunder && e.r.y0 == 0)
            continue;
        int b = add_el(fr, e, 1, rin[i], names);
        int dx = 0, dy = 0;
        if (!back) {
            dx = kSettleX - part_move(kSettleX, t3);
            int sy = settle_dy(e);
            dy = sy - part_move(sy, t3);
        }
        add_pose(fr, b, dx, kScreen, dy);
    }
    for (size_t i = 0; i < f.under_els.size(); i++) {
        const PageRow& e = f.under_els[i];
        if (head && hunder && e.r.y0 == 0)
            continue;
        int b = add_el(fr, e, 0, e.pressed ? kPressedZ : kLeaveZ + rout[i], names);
        int dist = left_pane(e) ? e.r.x1 : kW - e.r.x0;
        int m = part_move(dist, t3 - part_start3(band_in(bands, e)));
        int off = back ? dist - m : m;
        add_pose(fr, b, left_pane(e) ? -off : off, kScreen);
    }
    if (head)
        head_frame(form_head(f), hunder ? 0 : 1, back, t3, fr, names);
}

}

Rect page_art(int split, bool band)
{
    return {0, band ? kBandTop : 0, split + kTileFocusBorder, band ? kBandBottom : kH};
}

std::vector<PageInk> page_inks(Rect art)
{
    art = art.intersect(kScreen);
    if (art.empty())
        return {{kScreen, eng::Ink::Text}};
    std::vector<PageInk> out;
    for (PageInk k : {PageInk{{0, 0, kW, art.y0}, eng::Ink::Text}, PageInk{{0, art.y0, art.x0, art.y1}, eng::Ink::Text},
                      PageInk{art, eng::Ink::Art}, PageInk{{art.x1, art.y0, kW, art.y1}, eng::Ink::Text},
                      PageInk{{0, art.y1, kW, kH}, eng::Ink::Text}})
        if (!k.r.empty())
            out.push_back(k);
    return out;
}

std::vector<PageRow> home_elements(int split, bool band, int pressed)
{
    int x1 = split + kTileFocusBorder, top = band ? kBandTop : 0;
    std::vector<PageRow> out;
    if (top > 0)
        out.push_back({"home-head", {0, 0, x1, top}, eng::Ink::Text});
    for (int k = 0; k < kGridRows; k++) {
        int y0 = top + k * kGridRowPitch;
        out.push_back({"home-grid" + std::to_string(k), {0, y0, x1, k == kGridRows - 1 ? kH : y0 + kGridRowPitch},
                       eng::Ink::Art});
    }
    out.push_back({"home-menu-top", {x1, 0, kW, kMenuCut}, eng::Ink::Text});
    for (int k = 0; k < kMenuVisibleRows; k++) {
        int y0 = kMenuCut + k * kMenuPitchPx;
        out.push_back({"home-menu" + std::to_string(k), {x1, y0, kW, y0 + kMenuPitchPx}, eng::Ink::Text, k == pressed});
    }
    out.push_back({"home-menu-mark", {x1, kMenuCut + kMenuVisibleRows * kMenuPitchPx, kW, kH}, eng::Ink::Text});
    return out;
}

std::vector<PageRow> page_elements(const std::string& word, const char* head, const std::vector<PageRow>& rows)
{
    std::vector<PageRow> sorted = rows;
    std::stable_sort(sorted.begin(), sorted.end(), [](const PageRow& a, const PageRow& b) { return a.r.y0 < b.r.y0; });
    std::vector<PageRow> out{{word + "-" + head, {0, 0, kW, 0}, eng::Ink::Text}};
    for (const PageRow& r : sorted) {
        int y0 = std::clamp(r.r.y0, 0, kH), y1 = std::clamp(r.r.y1, 0, kH);
        PageRow& last = out.back();
        if (y0 < last.r.y1 || y0 == last.r.y0) {
            last.name = last.r.empty() && out.size() == 1 ? r.name : last.name + "+" + r.name;
            last.r.y1 = std::max(last.r.y1, y1);
            last.pressed = last.pressed || r.pressed;
            continue;
        }
        last.r.y1 = y0;
        out.push_back({r.name, {0, y0, kW, y1}, r.ink, r.pressed});
    }
    if (out.back().r.empty())
        out.back().r.y1 = kH;
    else if (out.back().r.y1 < kH)
        out.push_back({word + "-ground", {0, out.back().r.y1, kW, kH}, eng::Ink::Text});
    return out;
}

bool head_side(const PageForm& f, bool& under)
{
    under = f.closing;
    const PageHead& h = under ? f.under_head : f.over_head;
    return !(h.letters.empty() && h.tabs.empty());
}

const PageHead& form_head(const PageForm& f) { return f.closing ? f.under_head : f.over_head; }

int64_t head_start3(int order) { return ((int64_t)order * eng::kStepThirds + 1) / 3; }

std::vector<int> tab_ranks(const PageForm& f, const PageHead& h)
{
    std::vector<int64_t> first(h.tabs.size(), INT64_MAX);
    if (!f.closing) {
        std::vector<int> bands = part_bands(f);
        std::vector<std::vector<int>> ys;
        for (const PageRow& t : h.tabs)
            ys.push_back(row_groups(f, t.r));
        std::vector<std::pair<int, int>> spans;
        int64_t d3 = form_thirds(f);
        size_t open = h.tabs.size();
        for (int64_t t3 = 0; t3 <= d3 && open > 0; t3 += 3)
            for (size_t j = 0; j < h.tabs.size(); j++)
                if (first[j] == INT64_MAX && !covered(f, bands, ys[j], h.tabs[j].r, t3, spans)) {
                    first[j] = t3;
                    open--;
                }
    }
    std::vector<int> order(h.tabs.size());
    for (size_t j = 0; j < order.size(); j++)
        order[j] = (int)j;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        return first[(size_t)a] != first[(size_t)b] ? first[(size_t)a] < first[(size_t)b]
                                                    : h.tabs[(size_t)a].r.x0 < h.tabs[(size_t)b].r.x0;
    });
    std::vector<int> rank(order.size());
    for (size_t r = 0; r < order.size(); r++)
        rank[(size_t)order[r]] = (int)r;
    return rank;
}

std::vector<Surface> page_sources(const Surface& a, const Surface& b, const Surface words[2])
{
    return {a, b, words[0], words[1]};
}

std::vector<int> part_bands(const PageForm& f)
{
    auto tops = [](const std::vector<PageRow>& els) {
        std::vector<int> v;
        for (const PageRow& e : els)
            v.push_back(e.r.y0);
        std::sort(v.begin(), v.end());
        v.erase(std::unique(v.begin(), v.end()), v.end());
        return v;
    };
    std::vector<int> a = tops(f.under_els), b = tops(f.over_els);
    return !b.empty() && (a.empty() || b.size() <= a.size()) ? b : a;
}

int part_band(const PageForm& f, const PageRow& e) { return band_in(part_bands(f), e); }

int64_t part_start3(int band) { return band * kPartStagger3; }

int64_t form_thirds(const PageForm& f)
{
    int64_t d3 = kPartLeg3;
    std::vector<int> bands = part_bands(f);
    for (const PageRow& e : f.under_els)
        d3 = std::max(d3, part_start3(band_in(bands, e)) + kPartLeg3);
    bool under = false;
    if (head_side(f, under)) {
        const PageHead& h = form_head(f);
        int last = (int)h.letters.size() - 1;
        for (int r : h.rank)
            last = std::max(last, r);
        d3 = std::max(d3, head_start3(std::min(last, kHeadCap)) + eng::kStepThirds);
    }
    return d3;
}

int64_t form_ms(const PageForm& f) { return (form_thirds(f) + 2) / 3; }

int64_t form_duration(const PageForm& f) { return form_ms(f) + eng::kTailMs; }

int64_t page_time(const PageForm& f, int64_t ft)
{
    int64_t d = form_ms(f);
    return std::clamp<int64_t>(f.closing ? ft - eng::kTailMs : ft, 0, d);
}

int page_rest(const PageForm& f, int64_t ft, bool reversed, bool settled)
{
    int64_t t = page_time(f, ft), d = form_ms(f);
    bool under = f.closing || reversed;
    if (under ? t <= 0 : t >= d)
        return settled ? 1 : 0;
    if (reversed)
        return 0;
    return (f.closing ? t >= d : t <= 0) ? -1 : 0;
}

int page_sid(const PageRun& run) { return eng::kPageSid + run.pair; }

void page_frame_at(const PageForm& f, int64_t ft, eng::Frame& fr)
{
    fr.blocks.clear();
    fr.shades.clear();
    fr.poses.clear();
    part_frame(f, page_time(f, ft), fr);
}

std::vector<std::string> page_block_names(const PageForm& f)
{
    std::vector<std::string> names;
    eng::Frame fr;
    part_frame(f, 0, fr, &names);
    return names;
}

eng::Frame page_frame(const PageForm& f, int64_t ft)
{
    eng::Frame fr;
    page_frame_at(f, ft, fr);
    return fr;
}

FoldPlay page_fold(const std::shared_ptr<PageRun>& run)
{
    FoldPlay p;
    p.duration = form_duration(run->form);
    p.tail = eng::kTailMs;
    p.clamp_back = true;
    p.kept_from = run->pop;
    p.advance = [run](int64_t t0, int64_t now) {
        if (t0 != run->t0) {
            run->in->begin(t0, run->t0 >= 0);
            run->t0 = t0;
        }
        run->in->advance(t0, now);
    };
    p.forward = [run](const Surface& a, const Surface& b, const Surface& dst, int64_t t, int64_t) {
        run->src = page_sources(a, b, run->words);
        eng::Frame start;
        if (!run->in->inited()) {
            int64_t c0 = run->in->clock_us ? run->in->clock_us() : 0;
            start = page_frame(run->form, run->form.closing ? form_duration(run->form) : 0);
            if (run->in->clock_us)
                run->in->add_build(run->in->clock_us() - c0);
        }
        page_frame_at(run->form, t, run->frame);
        run->in->compose(dst, run->frame, run->src, LiveF::pack(theme().bg), dst.w,
                         run->in->inited() ? nullptr : &start);
        return dst.rect();
    };
    return p;
}

}
