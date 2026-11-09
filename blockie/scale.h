#pragma once
#include <atomic>
#include <climits>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

#include "../gfx/canvas.h"

namespace fd {

enum class Scaler { Channel, Swar };

struct ScaleTap {
    int i0 = 0, i1 = 0, w = 0, cov = 0;
};

struct ScaleAxis {
    int src = 0;
    int p0 = 0;
    std::vector<ScaleTap> taps;
};

struct ScaleJob {
    const Image* img = nullptr;
    const ScaleAxis* xs = nullptr;
    const ScaleAxis* ys = nullptr;
};

constexpr int kScaleChunkRows = 16;

enum class ScaleShare { Both, Ui, Helper };

struct ScaleSplit {
    int chunks = 0;
    int ui_chunks = 0;
    int helper_chunks = 0;
    int ui_cpu = -1;
    int helper_cpu = -1;
    int ui_us = 0;
    int helper_us = 0;
    int wake_us = 0;
    int64_t helper_rows = 0;
};

void scale_axis(ScaleAxis& out, int e0, int e1, int src, int lo, int hi);
int scale_tile(const Surface& dst, const Image& img, const ScaleAxis& xs, const ScaleAxis& ys, Scaler k, int y0 = 0,
               int y1 = INT_MAX);

class ScaleHelper {
public:
    ScaleHelper();
    ~ScaleHelper();
    ScaleHelper(const ScaleHelper&) = delete;
    ScaleHelper& operator=(const ScaleHelper&) = delete;

    void run(const Surface& dst, const std::vector<ScaleJob>& jobs, Scaler k, ScaleShare share = ScaleShare::Both);
    ScaleSplit last() const;

private:
    void loop();

    mutable std::mutex m_;
    std::condition_variable go_;
    std::condition_variable done_;
    Surface dst_;
    const std::vector<ScaleJob>* jobs_ = nullptr;
    Scaler k_ = Scaler::Swar;
    ScaleShare share_ = ScaleShare::Both;
    int y0_ = 0;
    int count_ = 0;
    int64_t posted_at_ = 0;
    std::atomic<int> next_{0};
    uint64_t posted_ = 0;
    uint64_t finished_ = 0;
    bool stop_ = false;
    ScaleSplit last_;
    std::thread thread_;
};

}
