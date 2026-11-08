#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace fd {

inline int pct_us(std::vector<int> v, int pct)
{
    if (v.empty())
        return -1;
    std::sort(v.begin(), v.end());
    size_t i = (v.size() * (size_t)pct + 99) / 100;
    return v[i > 0 ? i - 1 : 0];
}

struct FrameStats {
    bool on = false;
    const char* kind = nullptr;
    std::vector<int> work_us;
    std::vector<int> done_us;
    int from_us = -1;
    int to_us = -1;
    int64_t last_us = -1;

    void begin(const char* k)
    {
        if (!on)
            return;
        kind = k;
        work_us.clear();
        done_us.clear();
        from_us = -1;
        to_us = -1;
        last_us = -1;
    }
    void frame(int64_t t0, int64_t t1)
    {
        if (!kind)
            return;
        work_us.push_back((int)(t1 - t0));
        if (last_us < 0)
            last_us = t1;
    }
    void to(int us)
    {
        if (kind && to_us < 0)
            to_us = us;
    }
    void done(int64_t t)
    {
        if (!kind || last_us < 0)
            return;
        done_us.push_back((int)(t - last_us));
        last_us = t;
    }
    void end(const char* why)
    {
        if (!kind)
            return;
        auto ms = [](int us) { return us < 0 ? -1.0 : us / 1000.0; };
        auto nth = [&](size_t i) { return ms(i < work_us.size() ? work_us[i] : -1); };
        std::fprintf(stderr,
                     "blockie-frontend: framestats %s %s frames %zu work p95 %.1f max %.1f f0 %.1f f1 %.1f f2 %.1f"
                     " from %.1f to %.1f done %zu p95 %.1f max %.1f ms\n",
                     kind, why, work_us.size(), ms(pct_us(work_us, 95)), ms(pct_us(work_us, 100)), nth(0), nth(1),
                     nth(2), ms(from_us), ms(to_us), done_us.size(), ms(pct_us(done_us, 95)), ms(pct_us(done_us, 100)));
        kind = nullptr;
    }
};

}
