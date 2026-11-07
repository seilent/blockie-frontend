#pragma once
#include <cstddef>
#include <cstdint>

#include "kinds.h"

namespace fd::board {

constexpr int kScreenW = 960;
constexpr int kScreenH = 720;
constexpr int kFbBpp = 32;
constexpr bool kFbRotated = false;
constexpr bool kMipPan = true;
constexpr bool kHasWifi = true;
constexpr MemBudget kMem{GateMode::Total, 268288, 96256, true, 100663296, 9437184, 2359296, 32, 4194304, 1048576, 262144,
                         0, 3937428, 791700, 3979560};

}
