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

struct Flick {
    bool mark = true;
    bool back = true;
    bool ok = true;
    bool same_end = true;
    bool still_before = true;
    bool landing = true;
    bool land_ok = true;
};

Clip flick(const Flick& o)
{
    Clip c;
    for (int i = 0; i < kStill + 3 + kStill; i++) {
        Frame f((size_t)kW * kH, kGround);
        fill(f, 600, 720, kBlock);
        if (i == kStill + 1)
            fill(f, 700, 900, 0xE0E0E0);
        if (i >= kStill + 2 && !o.same_end)
            fill(f, 900, 960, 0x606060);
        if (i >= kStill && !o.still_before)
            fill(f, 100, 110, 0x606060);
        c.frames.push_back(f);
        c.rows.push_back(row(i, 3));
    }
    PopRow& pose = c.rows[kStill + 1];
    PopRow& land = c.rows[kStill + 2];
    pose.step = pose.left_only = o.mark;
    pose.short_back = o.mark && o.back;
    pose.ok = o.ok;
    pose.cause = o.mark ? "a reversal pose" : "";
    land.step = land.left_only = true;
    land.landing = o.landing;
    land.ok = o.land_ok;
    land.cause = "a landing";
    return c;
}

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
        pop_count(c.frames[i - 1].data(), c.frames[i].data(), kW, kH, pop_edge(c.rows[i]), c.rows[i].art, diffs[i].d,
                  diffs[i].e, &diffs[i].same);
        if (i + 1 < c.frames.size())
            pop_count(c.frames[i - 1].data(), c.frames[i + 1].data(), kW, kH, pop_edge(c.rows[i]), c.rows[i].art,
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

    {
        Clip c = clean();
        c.rows[kStill + 2].step = true;
        c.rows[kStill + 2].cause = "a designed frame";
        std::vector<std::string> lines;
        int fails = judge(c, true, lines);
        bool listed = false;
        for (const std::string& l : lines)
            listed = listed || l.find("  s a designed frame") != std::string::npos;
        check(fails == 0 && listed && lines.back() == "PASS no pops, designed frames 1",
              "a designed frame is listed with its cause and counted");
        c.rows[kStill + 2].ok = false;
        check(names(c, "FAIL the designed frame does not hold its assertion"),
              "a designed frame whose assertion fails is a FAIL");
    }
    {
        Clip c = clean();
        for (size_t i = kStill; i < c.frames.size(); i++)
            fill(c.frames[i], 0, 432, 0x808080);
        c.rows[kStill].step = true;
        c.rows[kStill].cause = "a whole pane switch";
        check(judge(c) == 0, "a designed frame skips the step and flash tests on its own frame");
        c.rows[kStill].step = false;
        c.rows[kStill].cause.clear();
        check(names(c, "FAIL step in x < 432"), "the same frame without the designed mark fails as a step");
    }
    {
        Clip c = clean();
        for (size_t i = kStill; i < c.frames.size(); i++)
            fill(c.frames[i], 0, 432, 0x808080);
        c.rows[kStill].step = true;
        c.rows[kStill].left_only = true;
        c.rows[kStill].cause = "a left pane swap";
        check(judge(c) == 0, "a designed left pane frame skips the step and flash tests left of b");
        fill(c.frames[kStill], 648, 960, 0xE0E0E0);
        check(names(c, "FAIL flash in right of b"), "a designed left pane frame still fails a flash right of b");
        c.rows[kStill].left_only = false;
        check(judge(c) == 0, "a designed whole frame skips the tests in every region");
        c.rows[kStill].left_only = true;
        std::vector<std::string> lines;
        judge(c, true, lines);
        bool marked = false;
        for (const std::string& l : lines)
            marked = marked || l.find(" ml- ") != std::string::npos;
        check(marked, "a designed left pane frame is printed as l, not s");
    }
    {
        check(judge(flick(Flick{})) == 0,
              "a reversal shorter than a frame: its designed pose and landing on the origin pass the flash test");
        Flick f;
        f.back = false;
        check(names(flick(f), "FAIL flash in right of b"),
              "the same one frame flash with no reversal still fails as a flash");
        f = Flick{};
        f.mark = false;
        check(names(flick(f), "FAIL flash in right of b"),
              "the same one frame flash on a frame that is not designed still fails as a flash");
        f = Flick{};
        f.ok = false;
        check(names(flick(f), "FAIL flash in right of b"),
              "the same one frame flash on a designed frame not equal to its fresh compose still fails as a flash");
        f = Flick{};
        f.same_end = false;
        check(names(flick(f), "FAIL flash in right of b"),
              "a landing that is not the frame before the input does not hold the flash");
        f = Flick{};
        f.still_before = false;
        check(names(flick(f), "FAIL flash in right of b"),
              "a frame before the pose that is still moving does not hold the flash");
        f = Flick{};
        f.landing = false;
        check(names(flick(f), "FAIL flash in right of b"),
              "a next frame that is not a landing does not hold the flash");
        f = Flick{};
        f.land_ok = false;
        check(names(flick(f), "FAIL flash in right of b"),
              "a landing whose own assertion failed does not hold the flash");
    }
    {
        PopRow r;
        r.b = 432;
        PopRow l = r;
        l.left_only = true;
        PopRow wide = l;
        wide.b = 500;
        check(pop_edge(r) == 432 && pop_edge(l) == 436 && pop_edge(wide) == 500,
              "right of b starts at 436 on a designed left pane frame while b is below it");
        Frame a((size_t)kW * kH, kGround), b = a;
        fill(b, 432, 436, kBlock);
        std::array<int, 3> n{};
        std::array<int64_t, 3> e{};
        pop_count(a.data(), b.data(), kW, kH, pop_edge(l), Rect{}, n, e, nullptr);
        bool left = n[2] == 0 && n[1] == 4 * kH;
        pop_count(a.data(), b.data(), kW, kH, pop_edge(r), Rect{}, n, e, nullptr);
        check(left && n[2] == 4 * kH, "x 432 to 436 counts left of b on a designed left pane frame, else right of b");
    }

    {
        Frame f((size_t)kW * kH, kGround);
        box(f, 16, 20, 10, 50, kBlock);
        BarRuns runs = bar_runs(f.data(), kW, kH, 16, 4, 0);
        check(runs.size() == 1 && runs[0].first == 10 && runs[0].second == 50, "bar_runs finds one 40 px bar at y 10");
        box(f, 16, 18, 60, 70, kBlock);
        check(bar_runs(f.data(), kW, kH, 16, 4, 0).size() == 1, "a run covers the whole bar width");
    }
    {
        auto at = [](int n, int y, bool rest, bool event) {
            BarRow r;
            r.n = n;
            r.sheet_vis = true;
            r.sheet_rest = true;
            r.focus = 2;
            r.rows_top = 100;
            r.pitch = 40;
            r.bar_rest = rest;
            r.event = event;
            r.sheet_runs = {{y, y + 40}};
            return r;
        };
        std::vector<std::string> lines;
        check(bar_judge({at(0, 100, false, true), at(1, 150, false, false), at(2, 180, true, false)}, lines) == 0 &&
                  lines.back() == "PASS the bar track holds on 3 frames",
              "a bar gliding toward the focus row and resting on it passes");
        check(bar_judge({at(0, 150, false, true), at(1, 120, false, false)}, lines) == 1,
              "a bar that moves away from the focus row fails");
        check(bar_judge({at(0, 140, true, false)}, lines) == 1, "a bar at rest off the focus row fails");
        BarRow two = at(0, 180, true, false);
        two.sheet_runs.push_back({300, 340});
        check(bar_judge({two}, lines) == 1, "two bar segments fail");
        BarRow tall = at(0, 180, true, false);
        tall.sheet_runs = {{180, 230}};
        check(bar_judge({tall}, lines) == 1, "a bar taller than a row fails");
        BarRow page;
        page.page_focus = 1;
        page.page_top = 196;
        page.page_pitch = 86;
        page.page_rest = true;
        page.page_runs = {{282, 368}};
        check(bar_judge({page}, lines) == 0, "the page bar at rest on its row passes while no sheet shows");
        BarRow mid = at(0, 0, false, false);
        mid.sheet_rest = false;
        mid.sheet_runs.clear();
        check(bar_judge({mid}, lines) == 0, "a frame with the sheet partly up is skipped");
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
