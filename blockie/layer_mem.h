#pragma once
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "layer_present.h"

namespace fd {

void composite_argb(const uint32_t* argb, int stride, const Surface& plate, const Surface& out);

class MemPresenter : public LayerPresenter {
public:
    MemPresenter();
    ~MemPresenter() override;
    MemPresenter(const MemPresenter&) = delete;
    MemPresenter& operator=(const MemPresenter&) = delete;

    bool open() override { return opens_; }
    Surface frame() override { return frame_.surface(); }
    bool ready() const override;
    uint32_t present(const std::vector<AlphaRegion>& regions) override;
    int fd() const override { return efd_; }
    bool pump(std::vector<PresentNote>& out) override;

    void set_opens(bool on) { opens_ = on; }
    void discard_next(int n) { discard_ = n; }
    void ready_for(uint32_t n) { limit_ = n; }
    void pause(uint32_t after, int ms)
    {
        pause_after_ = after;
        pause_ms_ = ms;
    }
    void hold_notes() { hold_ = true; }
    void stall_while(const std::string& path) { stall_ = path; }
    void log_to(int fd) { log_ = fd; }
    void close() { closed_ = true; }
    const std::vector<uint32_t>& argb() const { return argb_; }
    uint32_t argb_at(int x, int y) const { return argb_[(size_t)y * kPortraitW + (size_t)x]; }

private:
    void note(const char* what);

    std::string stall_;
    Buffer frame_;
    std::vector<uint32_t> argb_ = std::vector<uint32_t>((size_t)kPortraitW * kPortraitH);
    std::deque<PresentNote> notes_;
    uint32_t seq_ = 0, limit_ = UINT32_MAX, pause_after_ = UINT32_MAX;
    int pause_ms_ = 0;
    int64_t paused_at_ = -1;
    int discard_ = 0, efd_ = -1, log_ = -1;
    bool opens_ = true, closed_ = false, hold_ = false;
};

}
