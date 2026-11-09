#include "layer_mem.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <string>

#include <sys/eventfd.h>
#include <unistd.h>

namespace fd {

void composite_argb(const uint32_t* argb, int stride, const Surface& plate, const Surface& out)
{
    Rect r = plate.rect().intersect(out.rect());
    for (int y = r.y0; y < r.y1; y++)
        for (int x = r.x0; x < r.x1; x++) {
            uint32_t s = argb[(size_t)y * stride + x];
            uint32_t k = 255 - (s >> 24), p = LiveF::unpack(plate.at(x, y));
            uint32_t rr = ((s >> 16) & 0xFF) + (((p >> 16) & 0xFF) * k + 127) / 255;
            uint32_t gg = ((s >> 8) & 0xFF) + (((p >> 8) & 0xFF) * k + 127) / 255;
            uint32_t bb = (s & 0xFF) + ((p & 0xFF) * k + 127) / 255;
            out.at(x, y) = LiveF::pack(std::min(rr, 255u) << 16 | std::min(gg, 255u) << 8 | std::min(bb, 255u));
        }
}

namespace {

int64_t mono_ms()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

}

MemPresenter::MemPresenter() : efd_(eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)) {}

MemPresenter::~MemPresenter()
{
    if (efd_ >= 0)
        ::close(efd_);
}

void MemPresenter::note(const char* what)
{
    if (log_ < 0)
        return;
    std::string s = std::string(what) + "\n";
    if (write(log_, s.data(), s.size()) < 0)
        std::fprintf(stderr, "MemPresenter: cannot log %s", s.c_str());
}

bool MemPresenter::ready() const
{
    if (seq_ >= limit_ || (!stall_.empty() && access(stall_.c_str(), F_OK) == 0))
        return false;
    return paused_at_ < 0 || mono_ms() - paused_at_ >= pause_ms_;
}

uint32_t MemPresenter::present(const std::vector<AlphaRegion>& regions)
{
    write_argb(frame_.surface(), regions, argb_.data(), kPortraitW);
    seq_++;
    if (seq_ == pause_after_)
        paused_at_ = mono_ms();
    bool shown = discard_ <= 0;
    if (discard_ > 0)
        discard_--;
    int most = 0;
    for (const AlphaRegion& g : regions)
        if (!g.r.empty())
            most = std::max(most, (int)region_a8(g.a256));
    char buf[80];
    snprintf(buf, sizeof buf, "present %u %zu %d %d", seq_, regions.size(), regions_visible(regions) ? 1 : 0, most);
    note(buf);
    if (!hold_) {
        notes_.push_back({seq_, shown});
        uint64_t one = 1;
        if (efd_ >= 0 && write(efd_, &one, sizeof one) < 0)
            std::fprintf(stderr, "MemPresenter: eventfd write failed\n");
    }
    return seq_;
}

bool MemPresenter::pump(std::vector<PresentNote>& out)
{
    uint64_t v = 0;
    if (efd_ >= 0 && read(efd_, &v, sizeof v) < 0)
        v = 0;
    while (!notes_.empty()) {
        char buf[48];
        snprintf(buf, sizeof buf, "note %u %d", notes_.front().seq, notes_.front().presented ? 1 : 0);
        note(buf);
        out.push_back(notes_.front());
        notes_.pop_front();
    }
    return !closed_;
}

}
