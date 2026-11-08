#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "../pops.h"

using namespace fd;

namespace {

int g_fail = 0;
int g_total = 0;

void check(bool ok, const char* what)
{
    g_total++;
    if (ok) {
        std::printf("PASS %s\n", what);
    } else {
        std::printf("FAIL %s\n", what);
        g_fail++;
    }
}

constexpr int kW = 960;
constexpr int kH = 96;
constexpr int kStill = 14;
constexpr int kMotion = 10;
constexpr uint32_t kGround = 0x111111;
constexpr uint32_t kBlock = 0xC0C0C0;

using Frame = std::vector<uint32_t>;

struct Clip {
    std::vector<Frame> frames;
    std::vector<PopRow> rows;
};

void fill(Frame& f, int x0, int x1, uint32_t v)
{
    x0 = std::max(0, x0);
    x1 = std::min(kW, x1);
    for (int y = 0; y < kH; y++)
        for (int x = x0; x < x1; x++)
            f[(size_t)y * kW + x] = v;
}

void box(Frame& f, int x0, int x1, int y0, int y1, uint32_t v)
{
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            f[(size_t)y * kW + x] = v;
}

uint32_t gray(int v) { return (uint32_t)v * 0x010101u; }

PopRow row(int i, int steps)
{
    PopRow r;
    r.n = i;
    r.t = i * 16;
    r.b = 648;
    r.motion = i >= kStill && i < kStill + steps;
    return r;
}

Clip path(const std::vector<int>& steps)
{
    Clip c;
    int n = (int)steps.size();
    for (int i = 0; i < kStill + n + kStill; i++) {
        int m = i - kStill;
        int k = m < 0 ? 0 : m < n ? steps[m] : steps.back();
        Frame f((size_t)kW * kH, kGround);
        fill(f, 600 + k * 20, 720 + k * 20, kBlock);
        c.frames.push_back(f);
        c.rows.push_back(row(i, n));
    }
    return c;
}

Clip clean() { return path({1, 2, 3, 4, 5, 6, 7, 8, 9, 10}); }

Clip ramp(int jump_at)
{
    Clip c;
    const int steps[] = {7, 8, 9, 8, 7, 9};
    int level = 0x40;
    for (int i = 0; i < kStill + kMotion + kStill; i++) {
        int m = i - kStill;
        if (m >= 0 && m < kMotion)
            level += steps[m % 6];
        Frame f((size_t)kW * kH, kGround);
        box(f, 700, 750, 10, 40, gray(level));
        box(f, 800, 850, 10, 40, gray(m >= 0 && m == jump_at ? 0x40 + 95 : 0x40));
        c.frames.push_back(f);
        c.rows.push_back(row(i, kMotion));
    }
    return c;
}

int judge(const Clip& c, bool lead, std::vector<std::string>& lines)
{
    std::vector<PopDiff> diffs(c.frames.size());
    for (size_t i = 1; i < c.frames.size(); i++) {
        pop_count(c.frames[i - 1].data(), c.frames[i].data(), kW, kH, c.rows[i].b, c.rows[i].art, diffs[i].d,
                  diffs[i].e, &diffs[i].same);
        if (i + 1 < c.frames.size())
            pop_count(c.frames[i - 1].data(), c.frames[i + 1].data(), kW, kH, c.rows[i].b, c.rows[i].art,
                      diffs[i].skip, diffs[i].e_skip, nullptr);
    }
    return pop_judge(c.rows, diffs, lead, lines);
}

int judge(const Clip& c, bool lead = true)
{
    std::vector<std::string> lines;
    return judge(c, lead, lines);
}

bool names(const Clip& c, const char* fail)
{
    std::vector<std::string> lines;
    if (judge(c, true, lines) == 0)
        return false;
    for (const std::string& l : lines)
        if (l.find(fail) != std::string::npos)
            return true;
    return false;
}

}

int main()
{
    check(judge(clean()) == 0, "a clean slide with 14 still frames on each side passes");

    {
        Clip c = clean();
        for (size_t i = kStill; i < c.frames.size(); i++)
            fill(c.frames[i], 0, 432, 0x808080);
        check(names(c, "FAIL step in x < 432"), "a whole left pane switch at the input frame fails as a step");
    }
    {
        Clip c = clean();
        fill(c.frames[kStill + 4], 0, 432, 0xF0F0F0);
        check(names(c, "FAIL flash in x < 432"), "a one frame flash in the middle of the motion fails as a flash");
    }
    {
        Clip c = clean();
        fill(c.frames[kStill], 648, 960, 0xE0E0E0);
        check(names(c, "FAIL flash in right of b"), "a flash at the exit input frame fails as a flash");
    }
    {
        Clip c = clean();
        c.frames[3][100] = kGround + 0x010101;
        check(judge(c) > 0, "still frames before the input compare exactly, a one level change fails");
        Clip t = clean();
        t.frames[t.frames.size() - 2][100] = kGround + 0x010101;
        check(judge(t) > 0, "still frames after landing compare exactly");
    }
    {
        Clip c = clean();
        c.rows[kStill + kMotion].ok = false;
        check(judge(c) > 0, "a failed end check fails the clip");
    }
    {
        Clip c = clean();
        for (size_t i = 6; i < c.frames.size(); i++)
            fill(c.frames[i], 120, 274, 0x804020);
        c.rows[6].art = Rect{116, 0, 278, kH};
        check(judge(c) > 0, "an art drop still fails the lead check: the frames before the input changed");
        Clip m = clean();
        for (size_t i = kStill + 3; i < m.frames.size(); i++)
            fill(m.frames[i], 120, 274, 0x804020);
        m.rows[kStill + 3].art = Rect{116, 0, 278, kH};
        check(judge(m) == 0, "an art drop inside its slot rect during the motion is exempt");
        Clip o = clean();
        for (size_t i = kStill + 3; i < o.frames.size(); i++)
            fill(o.frames[i], 0, 432, 0x804020);
        o.rows[kStill + 3].art = Rect{116, 0, 278, kH};
        check(judge(o) > 0, "an art frame that changes outside its slot rect is judged like any frame");
    }
    {
        Clip c = clean();
        for (PopRow& r : c.rows)
            r.motion = r.n < kMotion;
        for (size_t i = 0; i < c.frames.size(); i++) {
            Frame f((size_t)kW * kH, kGround);
            int k = std::min((int)i + 1, kMotion);
            fill(f, 600 + k * 20, 720 + k * 20, kBlock);
            c.frames[i] = f;
        }
        check(judge(c, false) == 0 && judge(c, true) > 0, "--no-lead skips only the lead check for a launch motion");
    }

    check(judge(path({1, 2, 3, 4, 5, 4, 3, 2, 1, 0})) == 0, "an exact turnaround on the motion path passes");
    check(judge(path({1, 2, 3, 4, 5, 3, 2, 1, 0})) == 0, "a turnaround whose return is off by one step passes");
    check(judge(ramp(-1)) == 0, "a ramp of 7, 8 and 9 levels a frame over 1500 px passes");
    check(names(ramp(5), "FAIL flash in right of b"),
          "a 95 level jump of a second 1500 px block for one frame inside the ramp fails as a flash");
    {
        Clip c = path({1, 2, 3, 4, 5, 4, 3, 2, 1, 0});
        fill(c.frames[kStill + 4], 648, 960, 0xE0E0E0);
        check(names(c, "FAIL flash in right of b"), "a one frame wrong buffer at a turnaround fails as a flash");
    }

    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
