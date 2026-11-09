#include "bench.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <unordered_set>

#include <time.h>

#include "framestats.h"
#include "image.h"
#include "scale.h"
#include "slide.h"
#include "theme.h"
#include "tile_grid.h"

namespace fd {

namespace {

constexpr int kCovers = 12;
constexpr int kRuns = 10;
constexpr int kFps = 60;
constexpr int kPassScaleUs = 5000;
constexpr int kPassFrameUs = 12000;
constexpr int kSyntheticW = 512;
constexpr int kSyntheticH = 448;

struct Rig {
    Buffer from, to, dst, out;
    SlidePlan plan;
    SlideText text;
    std::vector<Art> covers;
    Rect head;
    std::vector<ScaleAxis> xs, ys;
    uint32_t sum = 0;
};

struct Times {
    std::vector<int> scale_us;
    std::vector<int> frame_us;
};

int64_t bench_us()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

void sleep_until_us(int64_t us)
{
    struct timespec ts;
    ts.tv_sec = (time_t)(us / 1000000);
    ts.tv_nsec = (long)(us % 1000000 * 1000);
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr) == EINTR) {
    }
}

std::string list_ms(const std::vector<int>& us)
{
    std::string s;
    char b[24];
    for (size_t i = 0; i < us.size(); i++) {
        std::snprintf(b, sizeof b, "%s%.1f", i ? "," : "", us[i] / 1000.0);
        s += b;
    }
    return s;
}

std::vector<std::string> cover_paths(HomeFlow& flow, Library& lib, const std::vector<std::string>& sys_ids)
{
    std::vector<std::string> out;
    std::unordered_set<int64_t> seen;
    auto add = [&](int64_t id) {
        if (!seen.insert(id).second)
            return;
        for (const MediaRef& m : lib.media(id))
            if (m.type == "covers") {
                out.push_back(m.path);
                return;
            }
    };
    for (const HomeGame& g : flow.home.recent)
        if ((int)out.size() < kCovers)
            add(g.id);
    for (const std::string& s : sys_ids)
        for (const Game& g : lib.games(s)) {
            if ((int)out.size() >= kCovers)
                return out;
            add(g.id);
        }
    return out;
}

Art synthetic(int k)
{
    DecodedImage d;
    d.w = kSyntheticW;
    d.h = kSyntheticH;
    d.rgb.resize((size_t)d.w * d.h);
    uint32_t s = 0x9e3779b9u * (uint32_t)(k + 1);
    for (uint32_t& p : d.rgb) {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        p = s & 0xffffffu;
    }
    return std::make_shared<const Image>(cover_tile(d, kBigTileSide));
}

const char* clock_name(GrowClock c)
{
    switch (c) {
    case GrowClock::G: return "g";
    case GrowClock::G6: return "g6";
    case GrowClock::GS: return "gs";
    case GrowClock::None: break;
    }
    return "none";
}

int64_t grow_frame(Rig& r, Fonts& fonts, GrowClock c, Scaler k, int64_t t, int64_t prev, int& scale_us)
{
    int64_t f0 = bench_us();
    fonts.begin_frame(-1);
    Surface a = r.from.surface(), d = r.dst.surface();
    slide_compose(a, r.to.surface(), d, r.plan, t, prev, theme().bg, r.text);
    int bx = std::clamp(slide_pose(r.plan, t).boundary, kPaneSplit, kPaneSplitWide);
    uint32_t g = LiveF::pack(theme().bg);
    for (int y = 0; y < d.h; y++)
        fill_px(d.row(y), g, bx);
    int leave = fx_round(tok::motion_leave);
    if (t < leave) {
        int ha = (int)(256 * (leave - t) / leave);
        Rect hr = r.head.intersect({0, 0, bx, d.h});
        for (int y = hr.y0; y < hr.y1; y++) {
            const uint32_t* as = a.row(y);
            uint32_t* dr = d.row(y);
            for (int x = hr.x0; x < hr.x1; x++)
                dr[x] = blend_px(g, as[x], ha);
        }
    }
    Rect clip{0, kBandTop, bx, kBandBottom};
    fx gp = grow_at(c, t);
    int64_t s0 = bench_us();
    for (size_t col = 0; col < r.xs.size(); col++) {
        Rect64 q = grow_tile_rect((int)col, gp, 0);
        scale_axis(r.xs[col], q.x0, q.x1, kBigTileSide, clip.x0, clip.x1);
    }
    for (size_t row = 0; row < r.ys.size(); row++) {
        Rect64 q = grow_tile_rect((int)row * kBigCols, gp, 0);
        scale_axis(r.ys[row], q.y0, q.y1, kBigTileSide, clip.y0, clip.y1);
    }
    for (size_t i = 0; i < r.covers.size(); i++)
        scale_tile(d, *r.covers[i], r.xs[i % kBigCols], r.ys[i / kBigCols], k);
    int64_t s1 = bench_us();
    Canvas cv(d);
    cv.set_clip(clip);
    Rect64 f = grow_tile_rect(0, gp, 0);
    int e = kTileFocusBorder * 64;
    cv.frame_aa({f.x0 - e, f.y0 - e, f.x1 + e, f.y1 + e}, f, theme().focus, 256);
    Surface o = r.out.surface();
    for (int y = 0; y < d.h; y++)
        std::memcpy(o.row(y), d.row(y), (size_t)d.w * sizeof(Pix));
    int64_t f1 = bench_us();
    for (int y : {150, 360, 600})
        r.sum = r.sum * 31 + o.row(y)[240] + o.row(y)[720];
    scale_us = (int)(s1 - s0);
    return f1 - f0;
}

Times run_clock(Rig& r, Fonts& fonts, GrowClock c, Scaler k, bool paced)
{
    Times out;
    int64_t end = grow_ms(c);
    int64_t start = bench_us();
    int64_t prev = -1;
    for (int64_t i = 1; i * 1000 / kFps < end; i++) {
        if (paced)
            sleep_until_us(start + i * 1000000 / kFps);
        int64_t t = i * 1000 / kFps;
        int su = 0;
        int fu = (int)grow_frame(r, fonts, c, k, t, prev, su);
        prev = t;
        out.scale_us.push_back(su);
        out.frame_us.push_back(fu);
    }
    return out;
}

}

void bench_grow(HomeFlow& flow, Fonts& fonts, Library& lib, const std::vector<std::string>& sys_ids, bool idle,
                int64_t waited_ms)
{
    Rig r;
    int real = 0;
    for (const std::string& p : cover_paths(flow, lib, sys_ids)) {
        DecodedImage d;
        if (!load_png(p, d) || !d.ok())
            continue;
        r.covers.push_back(std::make_shared<const Image>(cover_tile(d, kBigTileSide)));
        real++;
    }
    while ((int)r.covers.size() < kCovers)
        r.covers.push_back(synthetic((int)r.covers.size()));
    std::fprintf(stderr, "blockie-frontend: bench grow covers %d real %d synthetic %d workers %s waited %lld ms\n",
                 kCovers, real, kCovers - real, idle ? "idle" : "busy", (long long)waited_ms);

    flow.draw(r.from.surface(), flow.model, flow.nav, true, Pane::Selector, kScreen);
    SelectorModel em = flow.model;
    em.focus = flow.nav.focus;
    em.big = true;
    em.slots = SlotMap::RowMajor;
    em.grid_band = true;
    em.grid_y_px = 0;
    em.tiles = r.covers;
    em.tile_focus = 0;
    if (!flow.home.recent.empty()) {
        em.game_title = flow.home.recent[0].name;
        em.game_system = flow.home.recent[0].system;
    }
    {
        Canvas c(r.to.surface());
        c.set_bound({0, 0, kPaneSplitWide, kPortraitH});
        draw_selector(c, fonts, em, Pane::Details, false, false, theme(), true);
    }
    SelectorModel tm = em;
    tm.tiles.clear();
    warm_turned(fonts, tm);
    r.text = [&fonts, tm](Canvas& c, int shift, int a) {
        SelectorModel m = tm;
        m.turn_shift = shift;
        m.turn_a256 = a;
        draw_turned(c, fonts, m, theme());
    };
    r.head = header_rect(fonts, flow.model);
    r.plan = slide_plan(kPaneSplitWide, LeftMove::Fade, 0, r.head);
    r.xs.resize(kBigCols);
    r.ys.resize((r.covers.size() + kBigCols - 1) / kBigCols);

    const Scaler scalers[] = {Scaler::Channel, Scaler::Swar};
    const GrowClock clocks[] = {GrowClock::G, GrowClock::G6, GrowClock::GS};
    auto ms = [](int us) { return us / 1000.0; };
    for (bool paced : {true, false})
        for (Scaler k : scalers)
            for (GrowClock c : clocks) {
                r.plan.moves.grow = c;
                const char* sc = k == Scaler::Swar ? "swar" : "channel";
                const char* pace = paced ? "paced" : "unpaced";
                Times all, trace;
                for (int run = 0; run <= kRuns; run++) {
                    Times one = run_clock(r, fonts, c, k, paced);
                    if (run == 0)
                        continue;
                    if (run == 1)
                        trace = one;
                    all.scale_us.insert(all.scale_us.end(), one.scale_us.begin(), one.scale_us.end());
                    all.frame_us.insert(all.frame_us.end(), one.frame_us.begin(), one.frame_us.end());
                }
                int sp95 = pct_us(all.scale_us, 95), fp95 = pct_us(all.frame_us, 95);
                bool pass = sp95 >= 0 && sp95 <= kPassScaleUs && fp95 >= 0 && fp95 <= kPassFrameUs;
                std::fprintf(stderr,
                             "blockie-frontend: bench grow clock %s scaler %s pace %s runs %d frames %zu scale p50 %.2f"
                             " p95 %.2f max %.2f frame p50 %.2f p95 %.2f max %.2f ms sum %08x %s\n",
                             clock_name(c), sc, pace, kRuns, trace.frame_us.size(), ms(pct_us(all.scale_us, 50)),
                             ms(sp95), ms(pct_us(all.scale_us, 100)), ms(pct_us(all.frame_us, 50)), ms(fp95),
                             ms(pct_us(all.frame_us, 100)), (unsigned)r.sum, !paced ? "-" : pass ? "pass" : "fail");
                if (paced)
                    std::fprintf(stderr,
                                 "blockie-frontend: bench grow trace clock %s scaler %s pace %s frames %zu scale %s"
                                 " frame %s ms\n",
                                 clock_name(c), sc, pace, trace.frame_us.size(), list_ms(trace.scale_us).c_str(),
                                 list_ms(trace.frame_us).c_str());
            }
}

}
