#include "scale.h"

#include <algorithm>
#include <cstdint>

#include <pthread.h>
#include <sched.h>
#include <time.h>

namespace fd {

namespace {

constexpr int kSpan = 64;
constexpr int kFull = kSpan * kSpan;

int span64(int p, int a, int b) { return std::max(0, std::min(p + kSpan, b) - std::max(p, a)); }

uint32_t tap_channel(uint32_t p00, uint32_t p10, uint32_t p01, uint32_t p11, int wx, int wy)
{
    uint32_t o = 0;
    for (int sh = 0; sh < 24; sh += 8) {
        int c00 = p00 >> sh & 255, c10 = p10 >> sh & 255, c01 = p01 >> sh & 255, c11 = p11 >> sh & 255;
        int top = c00 * (256 - wx) + c10 * wx, bot = c01 * (256 - wx) + c11 * wx;
        o |= (uint32_t)((top * (256 - wy) + bot * wy + 32768) >> 16) << sh;
    }
    return o;
}

uint32_t tap_swar(uint32_t p00, uint32_t p10, uint32_t p01, uint32_t p11, uint32_t wx, uint32_t wy)
{
    uint32_t a = p00 & 0xFF00FF, b = p10 & 0xFF00FF, c = p01 & 0xFF00FF, d = p11 & 0xFF00FF;
    uint32_t top = ((a << 8) + (b - a) * wx + 0x800080) >> 8 & 0xFF00FF;
    uint32_t bot = ((c << 8) + (d - c) * wx + 0x800080) >> 8 & 0xFF00FF;
    uint32_t rb = ((top << 8) + (bot - top) * wy + 0x800080) >> 8 & 0xFF00FF;
    uint32_t ga = p00 & 0xFF00, gb = p10 & 0xFF00, gc = p01 & 0xFF00, gd = p11 & 0xFF00;
    uint32_t gt = (ga << 8) + (gb - ga) * wx;
    uint32_t gu = (gc << 8) + (gd - gc) * wx;
    uint32_t g = ((gt << 8) + (gu - gt) * wy + 0x800000) >> 16 & 0xFF00;
    return rb | g;
}

void edge_px(uint32_t& o, const uint32_t* r0, const uint32_t* r1, const ScaleTap& x, const ScaleTap& y)
{
    int a = x.cov * y.cov * 255 / kFull;
    if (a)
        o = blend_px(o, tap_channel(r0[x.i0], r0[x.i1], r1[x.i0], r1[x.i1], x.w, y.w), a + 1);
}

int64_t mono_us()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

struct Span {
    int y0 = 0, y1 = 0, w = 0;
};

Span job_span(const Surface& dst, const ScaleJob& j)
{
    const Image& img = *j.img;
    const ScaleAxis& xs = *j.xs;
    const ScaleAxis& ys = *j.ys;
    Span s;
    if (img.w <= 0 || img.h <= 0 || xs.src != img.w || ys.src != img.h)
        return s;
    int y0 = std::max(0, ys.p0), y1 = std::min(ys.p0 + (int)ys.taps.size(), dst.h);
    int w = std::min((int)xs.taps.size(), dst.w - xs.p0) - std::max(0, -xs.p0);
    if (y0 < y1 && w > 0)
        s = Span{y0, y1, w};
    return s;
}

template <Scaler K>
void tile_rows(const Surface& dst, const Image& img, const ScaleAxis& xs, const ScaleAxis& ys, int i0, int i1, int j0,
               int j1)
{
    const ScaleTap* tx = xs.taps.data();
    const ScaleTap* ty = ys.taps.data();
    const uint32_t* px = img.mem.data();
    int ia = i0, ib = i1;
    while (ia < ib && tx[ia].cov < kSpan)
        ia++;
    while (ib > ia && tx[ib - 1].cov < kSpan)
        ib--;
    for (int j = j0; j < j1; j++) {
        const ScaleTap& y = ty[j];
        if (!y.cov)
            continue;
        const uint32_t* r0 = px + (size_t)y.i0 * img.w;
        const uint32_t* r1 = px + (size_t)y.i1 * img.w;
        uint32_t* d = dst.row(ys.p0 + j);
        bool whole = y.cov == kSpan;
        int a = whole ? ia : i1, b = whole ? ib : i1;
        for (int i = i0; i < a; i++)
            edge_px(d[xs.p0 + i], r0, r1, tx[i], y);
        for (int i = a; i < b; i++) {
            const ScaleTap& x = tx[i];
            if constexpr (K == Scaler::Swar)
                d[xs.p0 + i] = tap_swar(r0[x.i0], r0[x.i1], r1[x.i0], r1[x.i1], (uint32_t)x.w, (uint32_t)y.w);
            else
                d[xs.p0 + i] = tap_channel(r0[x.i0], r0[x.i1], r1[x.i0], r1[x.i1], x.w, y.w);
        }
        for (int i = b; i < i1; i++)
            edge_px(d[xs.p0 + i], r0, r1, tx[i], y);
    }
}

int64_t pull_chunks(const Surface& dst, const std::vector<ScaleJob>& jobs, Scaler k, int y0, int count,
                    std::atomic<int>& next, int& taken)
{
    int64_t rows = 0;
    for (;;) {
        int c = next.fetch_add(1);
        if (c >= count)
            return rows;
        int a = y0 + c * kScaleChunkRows;
        for (const ScaleJob& j : jobs)
            rows += scale_tile(dst, *j.img, *j.xs, *j.ys, k, a, a + kScaleChunkRows);
        taken++;
    }
}

}

void scale_axis(ScaleAxis& out, int e0, int e1, int src, int lo, int hi)
{
    out.src = src;
    out.taps.clear();
    int p0 = std::max(lo, e0 >> 6), p1 = std::min(hi, (e1 + 63) >> 6);
    out.p0 = p0;
    if (src <= 0 || e1 <= e0 || p1 <= p0)
        return;
    out.taps.resize((size_t)(p1 - p0));
    int64_t d = e1 - e0;
    for (int p = p0; p < p1; p++) {
        int64_t c = (int64_t)(p * 64 + 32 - e0) * src * 256;
        int64_t v = std::clamp<int64_t>(c / d - 128, 0, (int64_t)(src - 1) * 256);
        ScaleTap& t = out.taps[(size_t)(p - p0)];
        t.i0 = (int)(v >> 8);
        t.w = (int)(v & 255);
        t.i1 = std::min(t.i0 + 1, src - 1);
        t.cov = span64(p * 64, e0, e1);
    }
}

int scale_tile(const Surface& dst, const Image& img, const ScaleAxis& xs, const ScaleAxis& ys, Scaler k, int y0, int y1)
{
    if (img.w <= 0 || img.h <= 0 || xs.src != img.w || ys.src != img.h)
        return 0;
    int i0 = std::max(0, -xs.p0), i1 = std::min((int)xs.taps.size(), dst.w - xs.p0);
    int j0 = std::max({0, -ys.p0, std::max(y0, 0) - ys.p0});
    int j1 = std::min({(int)ys.taps.size(), dst.h - ys.p0, std::min(y1, dst.h) - ys.p0});
    if (i0 >= i1 || j0 >= j1)
        return 0;
    if (k == Scaler::Swar)
        tile_rows<Scaler::Swar>(dst, img, xs, ys, i0, i1, j0, j1);
    else
        tile_rows<Scaler::Channel>(dst, img, xs, ys, i0, i1, j0, j1);
    return j1 - j0;
}

ScaleHelper::ScaleHelper()
{
    thread_ = std::thread([this]() { loop(); });
    pthread_setname_np(thread_.native_handle(), "fd-scale");
}

ScaleHelper::~ScaleHelper()
{
    {
        std::lock_guard<std::mutex> lk(m_);
        stop_ = true;
    }
    go_.notify_one();
    if (thread_.joinable())
        thread_.join();
}

void ScaleHelper::run(const Surface& dst, const std::vector<ScaleJob>& jobs, Scaler k, ScaleShare share)
{
    int lo = dst.h, hi = 0;
    for (const ScaleJob& j : jobs) {
        Span sp = job_span(dst, j);
        if (sp.w > 0) {
            lo = std::min(lo, sp.y0);
            hi = std::max(hi, sp.y1);
        }
    }
    int count = hi > lo ? (hi - lo + kScaleChunkRows - 1) / kScaleChunkRows : 0;
    {
        std::lock_guard<std::mutex> lk(m_);
        dst_ = dst;
        jobs_ = &jobs;
        k_ = k;
        share_ = share;
        y0_ = lo;
        count_ = count;
        next_.store(0);
        posted_at_ = mono_us();
        posted_++;
    }
    go_.notify_one();
    int64_t t0 = mono_us();
    int taken = 0;
    if (share != ScaleShare::Helper)
        pull_chunks(dst, jobs, k, lo, count, next_, taken);
    int us = (int)(mono_us() - t0);
    int cpu = sched_getcpu();
    std::unique_lock<std::mutex> lk(m_);
    done_.wait(lk, [&]() { return finished_ == posted_; });
    last_.chunks = count;
    last_.ui_chunks = taken;
    last_.ui_cpu = cpu;
    last_.ui_us = us;
}

ScaleSplit ScaleHelper::last() const
{
    std::lock_guard<std::mutex> lk(m_);
    return last_;
}

void ScaleHelper::loop()
{
    std::unique_lock<std::mutex> lk(m_);
    uint64_t seen = 0;
    for (;;) {
        go_.wait(lk, [&]() { return stop_ || posted_ != seen; });
        if (stop_)
            return;
        seen = posted_;
        Surface dst = dst_;
        const std::vector<ScaleJob>* jobs = jobs_;
        Scaler k = k_;
        ScaleShare share = share_;
        int y0 = y0_, count = count_;
        int64_t at = posted_at_;
        lk.unlock();
        int64_t t0 = mono_us();
        int taken = 0;
        int64_t rows = 0;
        if (share != ScaleShare::Ui)
            rows = pull_chunks(dst, *jobs, k, y0, count, next_, taken);
        int us = (int)(mono_us() - t0);
        int cpu = sched_getcpu();
        lk.lock();
        last_.helper_cpu = cpu;
        last_.helper_us = us;
        last_.wake_us = (int)(t0 - at);
        last_.helper_chunks = taken;
        last_.helper_rows = rows;
        finished_ = seen;
        done_.notify_one();
    }
}

}
