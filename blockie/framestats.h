#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
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
    std::vector<int> engine_us;
    std::vector<int> render_us;
    std::vector<int> toast_us;
    std::vector<int> present_us;
    std::vector<int> done_us;
    int from_us = -1;
    int to_us = -1;
    int64_t last_us = -1;
    int redraws = 0;
    uint32_t why_mask = 0;
    int builds = 0;
    std::string tag = "-";
    std::string parts[2];

    void begin(const char* k)
    {
        if (!on)
            return;
        kind = k;
        work_us.clear();
        engine_us.clear();
        render_us.clear();
        toast_us.clear();
        present_us.clear();
        done_us.clear();
        from_us = -1;
        to_us = -1;
        last_us = -1;
        redraws = 0;
        why_mask = 0;
        builds = 0;
        tag = "-";
        parts[0].clear();
        parts[1].clear();
    }
    void frame(int64_t t0, int64_t t1, int engine, int render = 0, int toast = 0, int present = 0)
    {
        if (!kind)
            return;
        work_us.push_back((int)(t1 - t0));
        engine_us.push_back(std::max(0, engine));
        render_us.push_back(std::max(0, render));
        toast_us.push_back(std::max(0, toast));
        present_us.push_back(std::max(0, present));
        if (last_us < 0)
            last_us = t1;
    }
    void part(size_t i, const std::string& s)
    {
        if (kind && i < 2)
            parts[i] = s;
    }
    void build(int n)
    {
        if (kind)
            builds += n;
    }
    void redraw(uint32_t mask)
    {
        if (!kind)
            return;
        redraws++;
        why_mask |= mask;
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
    std::string line(const char* why) const
    {
        auto ms = [](int us) { return us < 0 ? -1.0 : us / 1000.0; };
        auto nth = [&](size_t i) { return ms(i < work_us.size() ? work_us[i] : -1); };
        std::vector<int> rest(work_us.size());
        for (size_t i = 0; i < rest.size(); i++)
            rest[i] = work_us[i] - (i < engine_us.size() ? engine_us[i] : 0);
        char buf[640];
        std::snprintf(buf, sizeof buf,
                      "blockie-frontend: framestats %s %s frames %zu work p95 %.1f max %.1f f0 %.1f f1 %.1f f2 %.1f"
                      " from %.1f to %.1f done %zu p95 %.1f max %.1f ms engine p95 %.1f max %.1f rest p95 %.1f max %.1f"
                      " ms render p95 %.1f max %.1f toast p95 %.1f max %.1f present p95 %.1f max %.1f ms",
                      kind ? kind : "", why, work_us.size(), ms(pct_us(work_us, 95)), ms(pct_us(work_us, 100)), nth(0),
                      nth(1), nth(2), ms(from_us), ms(to_us), done_us.size(), ms(pct_us(done_us, 95)),
                      ms(pct_us(done_us, 100)), ms(pct_us(engine_us, 95)), ms(pct_us(engine_us, 100)),
                      ms(pct_us(rest, 95)), ms(pct_us(rest, 100)), ms(pct_us(render_us, 95)),
                      ms(pct_us(render_us, 100)), ms(pct_us(toast_us, 95)), ms(pct_us(toast_us, 100)),
                      ms(pct_us(present_us, 95)), ms(pct_us(present_us, 100)));
        std::string out = buf;
        out += " redraws " + std::to_string(redraws) + " why ";
        for (int b = 0; b < 26; b++)
            if (why_mask & (1u << b))
                out += (char)('a' + b);
        if (!why_mask)
            out += "-";
        out += " tag " + tag;
        for (int i = 0; i < 2; i++)
            if (!parts[i].empty())
                out += " p" + std::to_string(i) + " " + parts[i];
        out += " builds " + std::to_string(builds);
        return out;
    }
    void end(const char* why)
    {
        if (!kind)
            return;
        std::fprintf(stderr, "%s\n", line(why).c_str());
        kind = nullptr;
    }
};

}
