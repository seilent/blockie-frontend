#include "pagelog.h"

#include <algorithm>

namespace fd {

bool page_log(eng::PoseLog& log, PageLogState& st, const MotionCtl& ctl, const PageRun& run, int n, int png,
              int64_t tf)
{
    if (!log.on() || !run.in)
        return true;
    int64_t ft = ctl.forward_time(tf);
    eng::Frame f = page_frame(run.form, ft);
    int sid = page_sid(run);
    if (std::find(st.sids.begin(), st.sids.end(), sid) == st.sids.end()) {
        st.sids.push_back(sid);
        log.surface(sid, "page", "step", kScreen);
    }
    if (ctl.t0() != st.t0 || ctl.reversed() != st.back) {
        st.t0 = ctl.t0();
        st.back = ctl.reversed();
        for (const PageRow& r : run.rows)
            log.row(sid, r.name.c_str(), r.r, r.ink);
        std::vector<std::string> names = page_block_names(run.form);
        log.fold(ctl.t0(), "step", fold_kind_name(ctl.kind()), ctl.reversed(), form_thirds(run.form), f, false, false,
                 sid, &names);
    }
    return log.frame(n, png, tf, ft, page_rest(run.form, ft, ctl.reversed(), run.in->settled()), f, *run.in, -1,
                     sid);
}

}
