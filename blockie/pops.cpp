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

int pop_edge(const PopRow& r) { return r.left_only ? std::max(r.b, kPaneLeftEdge) : r.b; }

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
    int designed = 0;
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
        line += r.left_only ? "l" : r.step ? "s" : "-";
        line += r.art.empty() ? "-" : "a";
        for (int g = 0; g < 3; g++)
            line += " " + pad(df.d[g], 7);
        if (r.step && !r.cause.empty()) {
            line += "  s " + r.cause;
            designed += r.ok;
        }
        if (!r.ok) {
            line += !r.cause.empty() ? "  FAIL the designed frame does not hold its assertion"
                    : r.step         ? "  FAIL the step frame is not the frame before with the step alone applied"
                                     : "  FAIL the end check: the composite at T is not the landed frame";
            fails++;
        }
        if (!r.art.empty() && !r.motion && (df.d[0] || df.d[1] || df.d[2])) {
            line += "  FAIL an art frame changes outside its slot rects";
            fails++;
        }
        if (r.motion && (!r.step || r.left_only)) {
            for (int g = r.left_only ? 2 : 0; g < 3; g++) {
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
                bool held = r.short_back && r.ok && i >= 2 && i + 1 < n && rows[i + 1].landing && rows[i + 1].ok &&
                            diffs[i - 1].same && df.e_skip == std::array<int64_t, 3>{};
                if (i + 1 < n && !held && df.e_skip[g] * kPopRatio < e && e > kPopRatio * before) {
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
        if (!still) {
            lines.push_back("FAIL " + num((long long)first) + " frames before the input, not all identical");
            fails++;
        } else if (first < (size_t)kStillFrames) {
            lines.push_back("FAIL " + num((long long)first) + " identical still frames before the input, want " +
                            num(kStillFrames));
            fails++;
        } else {
            lines.push_back("PASS " + num((long long)first) + " identical still frames before the input");
        }
    } else {
        lines.push_back("PASS no lead check, the motion starts at launch");
    }
    size_t settle = last;
    bool still = true;
    for (size_t i = last + 1; i < n; i++) {
        if (diffs[i].same)
            continue;
        if (rows[i].art.empty())
            still = false;
        else
            settle = i;
    }
    size_t trail = first == n ? 0 : n - 1 - settle;
    if (!still) {
        lines.push_back("FAIL " + num((long long)trail) + " frames after landing, not all identical");
        fails++;
    } else if (trail < (size_t)kStillFrames) {
        lines.push_back("FAIL " + num((long long)trail) + " identical still frames after landing, want " +
                        num(kStillFrames));
        fails++;
    } else {
        lines.push_back("PASS " + num((long long)trail) + " identical still frames after landing");
    }
    std::string tail = designed ? ", designed frames " + num(designed) : std::string();
    lines.push_back(fails ? "FAIL " + num(fails) + " findings" + tail : "PASS no pops" + tail);
    return fails;
}

BarRuns bar_runs(const uint32_t* px, int w, int h, int x0, int bw, int xref)
{
    BarRuns runs;
    int start = -1;
    for (int y = 0; y <= h; y++) {
        bool on = y < h;
        for (int x = x0; on && x < x0 + bw; x++)
            on = px[(size_t)y * w + x] != px[(size_t)y * w + xref];
        if (on && start < 0)
            start = y;
        if (!on && start >= 0) {
            runs.push_back({start, y});
            start = -1;
        }
    }
    return runs;
}

int bar_judge(const std::vector<BarRow>& rows, std::vector<std::string>& lines)
{
    int fails = 0, checked = 0;
    bool have = false;
    bool prev_sheet = false;
    int prev_y = 0, prev_target = 0;
    for (const BarRow& r : rows) {
        bool sheet = r.sheet_rest;
        if (!sheet && r.sheet_vis) {
            have = false;
            continue;
        }
        const BarRuns& runs = sheet ? r.sheet_runs : r.page_runs;
        int pitch = sheet ? r.pitch : r.page_pitch;
        int target = sheet ? r.rows_top + (r.focus - r.first) * r.pitch : r.page_top + r.page_focus * r.page_pitch;
        bool rest = sheet ? r.bar_rest : r.page_rest;
        const char* which = sheet ? "the sheet bar" : "the page bar";
        checked++;
        if (runs.size() != 1) {
            lines.push_back("FAIL bar frame " + num(r.n) + ": " + which + " has " + num((long long)runs.size()) +
                            " segments, want 1");
            fails++;
            have = false;
            continue;
        }
        int y = runs[0].first;
        if (runs[0].second - y != pitch) {
            lines.push_back("FAIL bar frame " + num(r.n) + ": " + which + " is " + num(runs[0].second - y) +
                            " px tall, a row is " + num(pitch));
            fails++;
        }
        if (rest && y != target) {
            lines.push_back("FAIL bar frame " + num(r.n) + ": " + which + " rests at y " + num(y) +
                            ", the focus row is at y " + num(target));
            fails++;
        }
        if (!rest && !r.event && have && prev_sheet == sheet && prev_target == target) {
            int d = y - target, dp = prev_y - target;
            if ((d < 0 ? -d : d) > (dp < 0 ? -dp : dp) || (d != 0 && dp != 0 && (d > 0) != (dp > 0))) {
                lines.push_back("FAIL bar frame " + num(r.n) + ": " + which + " moves away from the focus row, y " +
                                num(prev_y) + " to " + num(y) + ", the focus row at y " + num(target));
                fails++;
            }
        }
        have = true;
        prev_sheet = sheet;
        prev_y = y;
        prev_target = target;
    }
    lines.push_back(fails ? "FAIL the bar track: " + num(fails) + " findings on " + num(checked) + " frames"
                          : "PASS the bar track holds on " + num(checked) + " frames");
    return fails;
}

}
