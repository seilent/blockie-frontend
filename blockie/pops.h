#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "../gfx/canvas.h"

namespace fd {

struct PopRow {
    int n = 0;
    int64_t t = 0;
    int b = 432;
    bool motion = false;
    bool step = false;
    bool ok = true;
    Rect art;
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

void pop_count(const uint32_t* a, const uint32_t* b, int w, int h, int bx, Rect skip, std::array<int, 3>& out,
               std::array<int64_t, 3>& energy, bool* same);
int pop_judge(const std::vector<PopRow>& rows, const std::vector<PopDiff>& diffs, bool lead,
              std::vector<std::string>& lines);

}
