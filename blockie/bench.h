#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "../gfx/font.h"
#include "homeflow.h"
#include "library.h"

namespace fd {

void bench_grow(HomeFlow& flow, Fonts& fonts, Library& lib, const std::vector<std::string>& sys_ids, bool idle,
                int64_t waited_ms);

}
