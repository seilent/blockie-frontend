#pragma once
#include <cstdint>
#include <functional>
#include <vector>

#include "../gfx/canvas.h"

namespace fd {

enum class FoldKind { Expand, Collapse, Push, Pop, Startup };

struct FoldPlay {
    int64_t duration = 0;
    bool details = false;
    Rect to_bound = kScreen;
    std::function<Rect(const Surface& a, const Surface& b, const Surface& dst, int64_t t, int64_t prev)> forward;
    std::function<int64_t(const Surface& a, const Surface& b)> prepare;
};

struct Channel {
    const char* name = "";
    std::function<bool(int64_t)> running;
    std::function<void()> land;
};

struct InputResult {
    bool changed = false;
    bool launched = false;
};

const char* fold_kind_name(FoldKind k);

class MotionCtl {
public:
    std::function<void(const Surface&, Rect bound)> render;
    std::function<void(const Surface&)> render_kept;
    std::function<void(const Surface&)> last;
    std::function<void()> keep;
    std::function<void()> settle;
    std::function<bool()> can_animate;
    std::function<bool()> redraw_pending;
    std::function<int64_t()> clock_us;
    std::function<void(const char* what, const char* why)> landed;

    void add(Channel c) { channels_.push_back(std::move(c)); }
    void input(int64_t now, const std::function<InputResult()>& nav);
    void start(FoldKind k, FoldPlay play, int64_t now);
    void dirty();
    void dirty(Rect r);
    void land(const char* why);
    void land_all(int64_t now, const char* why);
    bool frame(int64_t now, const Surface& dst);
    bool folding(int64_t now) const;
    bool animating(int64_t now) const;
    bool started() const { return started_; }
    bool active() const { return fold_; }
    bool details() const { return fold_ && play_.details; }
    bool copied() const { return copied_; }
    bool details_play() const { return play_.details; }
    bool navigating() const { return navigating_; }
    int from_us() const { return from_us_; }
    int to_us() const { return to_us_; }
    bool compose_end(const Surface& dst);
    FoldKind kind() const { return kind_; }
    bool reversed() const { return reversed_; }
    int64_t t0() const { return t0_; }
    int64_t run_length() const { return reversed_ ? back_e_ : play_.duration; }
    int64_t forward_time(int64_t now) const;
    Rect damage() const { return damage_; }
    Surface from() { return from_.surface(); }
    Surface to() { return to_.surface(); }
    void finish(int64_t now);

private:
    void land_channels(int64_t now, const char* why);
    void render_to();
    bool forward_dir() const;

    Buffer from_;
    Buffer to_;
    bool fold_ = false;
    FoldKind kind_ = FoldKind::Expand;
    FoldPlay play_;
    int64_t t0_ = 0;
    bool reversed_ = false;
    int64_t back_e_ = 0;
    bool to_dirty_ = false;
    Rect part_;
    bool started_ = false;
    bool copied_ = false;
    bool end_shown_ = false;
    bool stale_ = false;
    int from_us_ = -1;
    bool navigating_ = false;
    int to_us_ = -1;
    int64_t prev_t_ = -1;
    Rect damage_ = kScreen;
    std::vector<Channel> channels_;
};

}
