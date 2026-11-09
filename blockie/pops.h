#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "../gfx/canvas.h"

namespace fd {

struct PopRow {
    int n = 0;
    int64_t t = 0;
    int b = 432;
    bool motion = false;
    bool step = false;
    bool left_only = false;
    bool short_back = false;
    bool landing = false;
    bool ok = true;
    Rect art;
    std::string cause;
};

struct PopDiff {
    std::array<int, 3> d{};
    std::array<int, 3> skip{};
    std::array<int64_t, 3> e{};
    std::array<int64_t, 3> e_skip{};
    bool same = true;
};

constexpr int kPopMin = 4 * 48 * 48;
constexpr int kPopMinPane = 32 * 32;
constexpr int kPopRatio = 3;
constexpr int kPopDelta = 8;
constexpr int kPopReach = 3;
constexpr int kStillFrames = 12;

int pop_edge(const PopRow& r);
void pop_count(const uint32_t* a, const uint32_t* b, int w, int h, int bx, Rect skip, std::array<int, 3>& out,
               std::array<int64_t, 3>& energy, bool* same);
int pop_judge(const std::vector<PopRow>& rows, const std::vector<PopDiff>& diffs, bool lead,
              std::vector<std::string>& lines);

using BarRuns = std::vector<std::pair<int, int>>;

struct BarRow {
    int n = 0;
    bool sheet_vis = false;
    bool sheet_rest = false;
    int focus = 0;
    int first = 0;
    int rows_top = 0;
    int pitch = 0;
    bool bar_rest = false;
    int page_focus = 0;
    int page_top = 0;
    int page_pitch = 0;
    bool page_rest = false;
    bool event = false;
    BarRuns sheet_runs;
    BarRuns page_runs;
};

BarRuns bar_runs(const uint32_t* px, int w, int h, int x0, int bw, int xref);
int bar_judge(const std::vector<BarRow>& rows, std::vector<std::string>& lines);

}
