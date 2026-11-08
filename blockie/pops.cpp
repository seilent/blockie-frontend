#include "pops.h"

#include <algorithm>
#include <cstdio>

#include "tile_grid.h"

namespace fd {

namespace {

int delta(uint32_t a, uint32_t b)
{
    int m = 0;
    for (int sh = 0; sh <= 16; sh += 8) {
        int d = (int)(a >> sh & 0xFF) - (int)(b >> sh & 0xFF);
        m = std::max(m, d < 0 ? -d : d);
    }
    return m;
}

int floor_of(int r) { return r == 2 ? kPopMinPane : kPopMin; }

const char* const kRegion[] = {"x < 432", "432 to b", "right of b"};

std::string num(long long v)
{
    char buf[32];
    snprintf(buf, sizeof buf, "%lld", v);
    return buf;
}

std::string pad(long long v, int width)
{
    std::string s = num(v);
    return s.size() < (size_t)width ? std::string((size_t)width - s.size(), ' ') + s : s;
}

}

void pop_count(const uint32_t* a, const uint32_t* b, int w, int h, int bx, Rect skip, std::array<int, 3>& out,
               std::array<int64_t, 3>& energy, bool* same)
{
    out = {0, 0, 0};
    energy = {0, 0, 0};
    bx = std::max(kPaneSplit, bx);
    bool eq = true;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint32_t pa = a[(size_t)y * w + x], pb = b[(size_t)y * w + x];
            if (pa != pb)
                eq = false;
            if (x >= skip.x0 && x < skip.x1 && y >= skip.y0 && y < skip.y1)
                continue;
            int d = delta(pa, pb);
            if (d == 0)
                continue;
            int g = x < kPaneSplit ? 0 : x < bx ? 1 : 2;
            energy[g] += d;
            if (d > kPopDelta)
                out[g]++;
        }
    if (same)
        *same = eq;
}

int pop_judge(const std::vector<PopRow>& rows, const std::vector<PopDiff>& diffs, bool lead,
              std::vector<std::string>& lines)
{
    int fails = 0;
    size_t n = rows.size();
    if (n == 0 || diffs.size() != n) {
        lines.push_back("FAIL no frames");
        return 1;
    }
    size_t first = n, last = 0;
    for (size_t i = 0; i < n; i++)
        if (rows[i].motion) {
            first = std::min(first, i);
            last = i;
        }
    for (size_t i = 1; i < n; i++) {
        const PopRow& r = rows[i];
        const PopDiff& df = diffs[i];
        std::string line = pad(r.n, 4) + " " + pad(r.t, 6) + " ms b " + pad(r.b, 3);
        line += r.motion ? " m" : " -";
        line += r.step ? "s" : "-";
        line += r.art.empty() ? "-" : "a";
        for (int g = 0; g < 3; g++)
            line += " " + pad(df.d[g], 7);
        if (!r.ok) {
            line += r.step ? "  FAIL the step frame is not the frame before with the step alone applied"
                           : "  FAIL the end check: the composite at T is not the landed frame";
            fails++;
        }
        if (!r.art.empty() && !r.motion && (df.d[0] || df.d[1] || df.d[2])) {
            line += "  FAIL an art frame changes outside its slot rects";
            fails++;
        }
        if (r.motion && !r.step) {
            for (int g = 0; g < 3; g++) {
                if (df.d[g] <= floor_of(g))
                    continue;
                int64_t e = df.e[g];
                int64_t before = 0, around = 0;
                for (int k = 1; k <= kPopReach; k++) {
                    if (i >= (size_t)k + 1 && rows[i - k].motion)
                        before = std::max(before, diffs[i - k].e[g]);
                    if (i + k < n && rows[i + k].motion)
                        around = std::max(around, diffs[i + k].e[g]);
                }
                around = std::max(around, before);
                if (i + 1 < n && df.e_skip[g] * kPopRatio < e && e > kPopRatio * before) {
                    line += std::string("  FAIL flash in ") + kRegion[g] + " (the frames around it differ by " +
                            num(df.e_skip[g]) + ")";
                    fails++;
                }
                if (e > kPopRatio * around) {
                    line += std::string("  FAIL step in ") + kRegion[g] + " (largest motion neighbour " +
                            num(around) + ")";
                    fails++;
                }
            }
        }
        lines.push_back(line);
    }
    if (first == n) {
        lines.push_back("FAIL no motion frame");
        fails++;
    } else if (lead) {
        bool still = true;
        for (size_t i = 1; i < first; i++)
            if (!diffs[i].same)
                still = false;
        if (first < (size_t)kStillFrames || !still) {
            lines.push_back("FAIL " + num((long long)first) + " still frames before the input, want " +
                            num(kStillFrames) + " identical");
            fails++;
        } else {
            lines.push_back("PASS " + num((long long)first) + " identical still frames before the input");
        }
    } else {
        lines.push_back("PASS no lead check, the motion starts at launch");
    }
    size_t trail = first == n ? 0 : n - 1 - last;
    bool still = true;
    for (size_t i = last + 1; i < n; i++)
        if (!diffs[i].same)
            still = false;
    if (trail < (size_t)kStillFrames || !still) {
        lines.push_back("FAIL " + num((long long)trail) + " still frames after landing, want " + num(kStillFrames) +
                        " identical");
        fails++;
    } else {
        lines.push_back("PASS " + num((long long)trail) + " identical still frames after landing");
    }
    lines.push_back(fails ? "FAIL " + num(fails) + " findings" : std::string("PASS no pops"));
    return fails;
}

}
