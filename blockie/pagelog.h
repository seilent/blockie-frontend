#pragma once
#include <cstdint>
#include <vector>

#include "engine.h"
#include "motion.h"
#include "pageform.h"

namespace fd {

struct PageLogState {
    std::vector<int> sids;
    int64_t t0 = -1;
    bool back = false;
};

bool page_log(eng::PoseLog& log, PageLogState& st, const MotionCtl& ctl, const PageRun& run, int n, int png,
              int64_t tf);

}
