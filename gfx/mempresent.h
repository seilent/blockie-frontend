#pragma once
#include <cstdint>
#include <vector>

#include "canvas.h"

namespace fd {

enum class PresentMode { Direct, Copy, Warp };

struct PanDone {
    uint32_t seq;
    int64_t write_ns, pan_ns, end_ns;
    bool ok = true;
#if defined(FD_BOARD_A105) || defined(FD_BOARD_PB)
    int64_t gpu_ns = -1, ready_ns = -1, post_ns = -1;
#endif
};

template <class F, class L> class PresenterT;

template <> class PresenterT<X888, Rows> {
public:
    using S = SurfaceT<X888, Rows>;

    void open(PresentMode mode)
    {
        mode_ = mode;
        if (mode_ != PresentMode::Direct) {
            back_buf_.assign((size_t)kPortraitW * kPortraitH, 0);
            back_ = {back_buf_.data(), kPortraitW};
        }
        mem_fb_.assign((size_t)kPortraitW * kPortraitH, 0);
        fb_ = {mem_fb_.data(), kPortraitW};
    }
    PresentMode mode() const { return mode_; }
    int64_t show_at(int64_t now_ms, bool = true) const { return now_ms + show_lead_ms_; }
    int period_ms() const { return 16; }
    void set_show_lead(int ms) { show_lead_ms_ = ms; }

    S draw_target() const { return mode_ == PresentMode::Direct ? fb_ : back_; }
    S warp_target() const { return mode_ == PresentMode::Copy ? back_ : fb_; }
    S screen() const { return mode_ == PresentMode::Warp && !warped_ ? back_ : fb_; }
    S last() const { return {}; }
    uint32_t frame_presents() const { return frame_presents_; }
    uint32_t fulls() const { return fulls_; }

    void present(Rect dirty)
    {
        presents_++;
        bool full = covers(dirty);
        if (full)
            fulls_++;
        if (mode_ == PresentMode::Copy || (mode_ == PresentMode::Warp && warped_ && !full))
            copy_rows(dirty);
        if (full)
            warped_ = false;
    }
    void present_warp()
    {
        presents_++;
        fulls_++;
        if (mode_ == PresentMode::Copy)
            copy_rows(kScreen);
        warped_ = true;
    }
    void flip()
    {
        frame_presents_ = presents_;
        presents_ = 0;
    }

private:
    static bool covers(Rect r) { return r.x0 <= 0 && r.y0 <= 0 && r.x1 >= kPortraitW && r.y1 >= kPortraitH; }
    void copy_rows(Rect p)
    {
        p = p.intersect(kScreen);
        if (p.empty())
            return;
        for (int y = p.y0; y < p.y1; y++)
            copy_px(fb_.row(y) + p.x0, back_.row(y) + p.x0, p.x1 - p.x0);
    }

    PresentMode mode_ = PresentMode::Copy;
    std::vector<uint32_t> back_buf_;
    std::vector<uint32_t> mem_fb_;
    uint32_t presents_ = 0;
    uint32_t frame_presents_ = 0;
    uint32_t fulls_ = 0;
    int show_lead_ms_ = 0;
    bool warped_ = false;
    S back_;
    S fb_;
};

}
