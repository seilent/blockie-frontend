#include "bench.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <thread>
#include <unordered_set>

#include <fcntl.h>
#include <time.h>
#include <unistd.h>

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
constexpr const char* kFreqPaths[2] = {"/sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq",
                                       "/sys/devices/system/cpu/cpufreq/policy6/scaling_cur_freq"};

struct Rig {
    Buffer from, to, dst, out;
    SlidePlan plan;
    SlideText text;
    std::vector<Art> covers;
    Rect head;
    std::vector<ScaleAxis> xs, ys;
    uint32_t sum = 0;
    ScaleHelper* helper = nullptr;
    std::vector<ScaleJob> jobs;
    uint32_t ui_cpus = 0;
    uint32_t helper_cpus = 0;
    int freq_fd[2] = {-1, -1};
};

struct Sample {
    int scale_us = 0;
    int ui_us = -1;
    int helper_us = -1;
    int wake_us = -1;
    int chunks = 0;
    int ui_chunks = 0;
    int helper_chunks = 0;
    bool same_cpu = false;
};

struct Times {
    std::vector<int> scale_us;
    std::vector<int> frame_us;
    std::vector<int> ui_us;
    std::vector<int> helper_us;
    std::vector<int> wake_us;
    std::vector<int> freq[2];
    int same = 0;
    int64_t chunks = 0;
    int64_t ui_chunks = 0;
    int64_t helper_chunks = 0;

    void add(const Times& o)
    {
        auto cat = [](std::vector<int>& a, const std::vector<int>& b) { a.insert(a.end(), b.begin(), b.end()); };
        cat(scale_us, o.scale_us);
        cat(frame_us, o.frame_us);
        cat(ui_us, o.ui_us);
        cat(helper_us, o.helper_us);
        cat(wake_us, o.wake_us);
        cat(freq[0], o.freq[0]);
        cat(freq[1], o.freq[1]);
        same += o.same;
        chunks += o.chunks;
        ui_chunks += o.ui_chunks;
        helper_chunks += o.helper_chunks;
    }
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

DecodedImage noise_source(int k)
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
    return d;
}

Art synthetic(int k) { return std::make_shared<const Image>(cover_tile(noise_source(k), kBigTileSide)); }

uint32_t cpu_bit(int cpu) { return cpu >= 0 && cpu < 32 ? 1u << cpu : 0; }

int read_khz(int fd)
{
    char b[32];
    ssize_t n = fd >= 0 ? pread(fd, b, sizeof b - 1, 0) : -1;
    if (n <= 0)
        return -1;
    b[n] = 0;
    return std::atoi(b);
}

int mhz_p50(const std::vector<int>& khz)
{
    int v = pct_us(khz, 50);
    return v < 0 ? -1 : v / 1000;
}

int mhz_min(const std::vector<int>& khz)
{
    if (khz.empty())
        return -1;
    int v = *std::min_element(khz.begin(), khz.end());
    return v < 0 ? -1 : v / 1000;
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

int64_t grow_frame(Rig& r, Fonts& fonts, GrowClock c, Scaler k, int64_t t, int64_t prev, Sample& sm)
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
    if (r.helper) {
        r.jobs.clear();
        for (size_t i = 0; i < r.covers.size(); i++)
            r.jobs.push_back({r.covers[i].get(), &r.xs[i % kBigCols], &r.ys[i / kBigCols]});
        r.helper->run(d, r.jobs, k);
    } else {
        for (size_t i = 0; i < r.covers.size(); i++)
            scale_tile(d, *r.covers[i], r.xs[i % kBigCols], r.ys[i / kBigCols], k);
    }
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
    if (r.helper) {
        ScaleSplit sp = r.helper->last();
        r.ui_cpus |= cpu_bit(sp.ui_cpu);
        r.helper_cpus |= cpu_bit(sp.helper_cpu);
        sm.ui_us = sp.ui_us;
        sm.helper_us = sp.helper_us;
        sm.wake_us = sp.wake_us;
        sm.chunks = sp.chunks;
        sm.ui_chunks = sp.ui_chunks;
        sm.helper_chunks = sp.helper_chunks;
        sm.same_cpu = sp.ui_cpu >= 0 && sp.ui_cpu == sp.helper_cpu;
    }
    sm.scale_us = (int)(s1 - s0);
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
        Sample sm;
        int fu = (int)grow_frame(r, fonts, c, k, t, prev, sm);
        prev = t;
        out.scale_us.push_back(sm.scale_us);
        out.frame_us.push_back(fu);
        if (sm.ui_us >= 0) {
            out.ui_us.push_back(sm.ui_us);
            out.helper_us.push_back(sm.helper_us);
            out.wake_us.push_back(sm.wake_us);
            out.same += sm.same_cpu ? 1 : 0;
            out.chunks += sm.chunks;
            out.ui_chunks += sm.ui_chunks;
            out.helper_chunks += sm.helper_chunks;
        }
        for (int q = 0; q < 2; q++)
            out.freq[q].push_back(read_khz(r.freq_fd[q]));
    }
    return out;
}

void report_set(Rig& r, Fonts& fonts, GrowClock c, Scaler k, bool paced, const char* sc)
{
    auto ms = [](int us) { return us / 1000.0; };
    r.plan.moves.grow = c;
    const char* pace = paced ? "paced" : "unpaced";
    Times all, trace;
    for (int run = 0; run <= kRuns; run++) {
        Times one = run_clock(r, fonts, c, k, paced);
        if (run == 0)
            continue;
        if (run == 1)
            trace = one;
        all.add(one);
    }
    int sp95 = pct_us(all.scale_us, 95), fp95 = pct_us(all.frame_us, 95);
    bool pass = sp95 >= 0 && sp95 <= kPassScaleUs && fp95 >= 0 && fp95 <= kPassFrameUs;
    std::fprintf(stderr,
                 "blockie-frontend: bench grow clock %s scaler %s pace %s runs %d frames %zu scale p50 %.2f"
                 " p95 %.2f max %.2f frame p50 %.2f p95 %.2f max %.2f ms sum %08x %s\n",
                 clock_name(c), sc, pace, kRuns, trace.frame_us.size(), ms(pct_us(all.scale_us, 50)), ms(sp95),
                 ms(pct_us(all.scale_us, 100)), ms(pct_us(all.frame_us, 50)), ms(fp95), ms(pct_us(all.frame_us, 100)),
                 (unsigned)r.sum, !paced ? "-" : pass ? "pass" : "fail");
    if (paced)
        std::fprintf(stderr,
                     "blockie-frontend: bench grow trace clock %s scaler %s pace %s frames %zu scale %s"
                     " frame %s ms\n",
                     clock_name(c), sc, pace, trace.frame_us.size(), list_ms(trace.scale_us).c_str(),
                     list_ms(trace.frame_us).c_str());
    auto part = [](int us) { return us < 0 ? -1.0 : us / 1000.0; };
    std::fprintf(stderr,
                 "blockie-frontend: bench grow set clock %s scaler %s pace %s ui p50 %.2f p95 %.2f helper p50 %.2f"
                 " p95 %.2f wake p50 %.2f p95 %.2f ms chunks ui %lld helper %lld of %lld same cpu %d of %zu"
                 " policy0 p50 %d min %d policy6 p50 %d min %d MHz\n",
                 clock_name(c), sc, pace, part(pct_us(all.ui_us, 50)), part(pct_us(all.ui_us, 95)),
                 part(pct_us(all.helper_us, 50)), part(pct_us(all.helper_us, 95)), part(pct_us(all.wake_us, 50)),
                 part(pct_us(all.wake_us, 95)), (long long)all.ui_chunks, (long long)all.helper_chunks,
                 (long long)all.chunks, all.same, all.ui_us.size(), mhz_p50(all.freq[0]), mhz_min(all.freq[0]),
                 mhz_p50(all.freq[1]), mhz_min(all.freq[1]));
}

class DecodeLoad {
public:
    explicit DecodeLoad(const std::vector<std::string>& paths) : paths_(paths), t0_(bench_us())
    {
        thread_ = std::thread([this]() { loop(); });
    }
    ~DecodeLoad() { finish(); }
    DecodeLoad(const DecodeLoad&) = delete;
    DecodeLoad& operator=(const DecodeLoad&) = delete;

    void finish()
    {
        stop_.store(true);
        if (!thread_.joinable())
            return;
        thread_.join();
        span_us_ = bench_us() - t0_;
    }
    int decodes() const { return decodes_.load(); }
    int pngs() const { return pngs_.load(); }
    int64_t cpu_us() const { return cpu_us_.load(); }
    int64_t span_us() const { return span_us_; }

private:
    void loop()
    {
        DecodedImage noise = noise_source(kCovers);
        for (size_t i = 0; !stop_.load(); i++) {
            DecodedImage d;
            bool png = !paths_.empty() && load_png(paths_[i % paths_.size()], d) && d.ok();
            Image img = cover_tile(png ? d : noise, kBigTileSide);
            if (img.mem.empty())
                continue;
            decodes_.fetch_add(1);
            if (png)
                pngs_.fetch_add(1);
        }
        struct timespec ts;
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
        cpu_us_.store((int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000);
    }

    const std::vector<std::string>& paths_;
    int64_t t0_ = 0;
    int64_t span_us_ = 0;
    std::atomic<bool> stop_{false};
    std::atomic<int> decodes_{0};
    std::atomic<int> pngs_{0};
    std::atomic<int64_t> cpu_us_{0};
    std::thread thread_;
};

bool k3_equal(Rig& r, Fonts& fonts, ScaleHelper& helper)
{
    Buffer one;
    bool same = true;
    for (Scaler k : {Scaler::Channel, Scaler::Swar})
        for (GrowClock c : {GrowClock::G, GrowClock::G6, GrowClock::GS}) {
            r.plan.moves.grow = c;
            int64_t prev = -1;
            for (int64_t i = 1; i * 1000 / kFps < grow_ms(c); i++) {
                int64_t t = i * 1000 / kFps;
                Sample sm;
                r.helper = nullptr;
                grow_frame(r, fonts, c, k, t, prev, sm);
                one.pix = r.dst.pix;
                r.helper = &helper;
                grow_frame(r, fonts, c, k, t, prev, sm);
                if (one.pix != r.dst.pix)
                    same = false;
                prev = t;
            }
        }
    r.helper = nullptr;
    return same;
}

}

void bench_grow(HomeFlow& flow, Fonts& fonts, Library& lib, const std::vector<std::string>& sys_ids, bool idle,
                int64_t waited_ms)
{
    Rig r;
    int real = 0;
    std::vector<std::string> paths = cover_paths(flow, lib, sys_ids);
    for (const std::string& p : paths) {
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
    for (int q = 0; q < 2; q++)
        r.freq_fd[q] = open(kFreqPaths[q], O_RDONLY | O_CLOEXEC);

    const Scaler scalers[] = {Scaler::Channel, Scaler::Swar};
    const GrowClock clocks[] = {GrowClock::G, GrowClock::G6, GrowClock::GS};
    for (bool paced : {true, false})
        for (Scaler k : scalers)
            for (GrowClock c : clocks)
                report_set(r, fonts, c, k, paced, k == Scaler::Swar ? "swar" : "channel");

    ScaleHelper helper;
    r.helper = &helper;
    r.sum = 0;
    for (bool paced : {true, false})
        for (Scaler k : scalers)
            for (GrowClock c : clocks)
                report_set(r, fonts, c, k, paced, k == Scaler::Swar ? "swar-k3" : "channel-k3");
    DecodeLoad load(paths);
    r.sum = 0;
    for (bool paced : {true, false})
        for (Scaler k : scalers)
            for (GrowClock c : clocks)
                report_set(r, fonts, c, k, paced, k == Scaler::Swar ? "swar-k3-load" : "channel-k3-load");
    load.finish();
    r.helper = nullptr;
    uint32_t ui_cpus = r.ui_cpus, helper_cpus = r.helper_cpus;
    bool equal = k3_equal(r, fonts, helper);
    std::fprintf(stderr,
                 "blockie-frontend: bench grow load decode %s decodes %d png %d load cpu %lld ms of span %lld ms ui"
                 " cpus 0x%02x helper cpus 0x%02x k3 equal %s\n",
                 load.decodes() > 0 ? "busy" : "idle", load.decodes(), load.pngs(), (long long)(load.cpu_us() / 1000),
                 (long long)(load.span_us() / 1000), (unsigned)ui_cpus, (unsigned)helper_cpus,
                 equal ? "yes" : "no");
    for (int q = 0; q < 2; q++)
        if (r.freq_fd[q] >= 0)
            close(r.freq_fd[q]);
}

}
