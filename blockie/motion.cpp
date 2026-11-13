#include "motion.h"

#include <algorithm>
#include <utility>

namespace fd {

namespace {

bool opens(FoldKind k)
{
    return k == FoldKind::Expand || k == FoldKind::Push || k == FoldKind::Launch || k == FoldKind::Return ||
           k == FoldKind::Swap;
}

bool inverse(FoldKind a, FoldKind b)
{
    return (a == FoldKind::Expand && b == FoldKind::Collapse) || (a == FoldKind::Collapse && b == FoldKind::Expand) ||
           (a == FoldKind::Push && b == FoldKind::Pop) || (a == FoldKind::Pop && b == FoldKind::Push);
}

}

const char* fold_kind_name(FoldKind k)
{
    switch (k) {
    case FoldKind::Expand: return "expand";
    case FoldKind::Collapse: return "collapse";
    case FoldKind::Push: return "push";
    case FoldKind::Pop: return "pop";
    case FoldKind::Startup: return "startup";
    case FoldKind::Launch: return "launch";
    case FoldKind::Return: return "return";
    case FoldKind::Swap: return "swap";
    }
    return "?";
}

bool MotionCtl::forward_dir() const { return opens(kind_) && !reversed_; }

int64_t MotionCtl::forward_time(int64_t now) const
{
    int64_t s = std::max<int64_t>(0, now - t0_);
    if (reversed_)
        return std::max<int64_t>(0, back_e_ - s);
    if (opens(kind_))
        return std::min(s, play_.duration);
    return std::max<int64_t>(0, play_.duration - s);
}

void MotionCtl::finish(int64_t now)
{
    if (fold_ && now - t0_ >= run_length())
        land("end");
}

void MotionCtl::land_channels(int64_t now, const char* why)
{
    for (Channel& c : channels_)
        if (c.running && c.running(now)) {
            if (c.land)
                c.land();
            if (landed)
                landed(c.name, why);
        }
}

void MotionCtl::land(const char* why)
{
    if (!fold_)
        return;
    fold_ = false;
    to_dirty_ = false;
    stale_ = !end_shown_;
    end_shown_ = false;
    if (landed)
        landed(fold_kind_name(kind_), why);
}

void MotionCtl::land_all(int64_t now, const char* why)
{
    land(why);
    land_channels(now, why);
}

void MotionCtl::dirty()
{
    if (!fold_)
        return;
    to_dirty_ = true;
    part_ = to_.surface().rect();
}

void MotionCtl::dirty(Rect r)
{
    if (!fold_)
        return;
    r = r.intersect(to_.surface().rect());
    if (r.empty())
        return;
    to_dirty_ = true;
    part_ = part_.unite(r);
}

void MotionCtl::render_to()
{
    int64_t c0 = clock_us ? clock_us() : 0;
    to_dirty_ = false;
    Rect full = to_.surface().rect();
    Rect bound = forward_dir() ? play_.to_bound.intersect(full) : full;
    if (!part_.empty())
        bound = bound.intersect(part_);
    part_ = Rect{};
    if (render && !bound.empty())
        render(to_.surface(), bound);
    if (play_.prepare) {
        Surface f = from_.surface();
        Surface t = to_.surface();
        bool fw = forward_dir();
        int64_t d = play_.prepare(fw ? f : t, fw ? t : f);
        if (d > 0)
            play_.duration = d;
    }
    prev_t_ = -1;
    if (clock_us)
        to_us_ = (int)(clock_us() - c0);
}

bool MotionCtl::compose_end(const Surface& dst) { return compose_at(forward_dir() ? play_.duration : 0, dst); }

bool MotionCtl::compose_at(int64_t ft, const Surface& dst)
{
    if (!play_.forward)
        return false;
    Surface f = from_.surface();
    Surface t = to_.surface();
    bool fw = forward_dir();
    play_.forward(fw ? f : t, fw ? t : f, dst, ft, -1);
    return true;
}

void MotionCtl::input(int64_t now, const std::function<InputResult()>& nav)
{
    finish(now);
    if (fold_ && play_.forward && to_dirty_)
        render_to();
    if (keep)
        keep();
    started_ = false;
    navigating_ = true;
    InputResult r = nav ? nav() : InputResult{};
    navigating_ = false;
    if (started_)
        return;
    if (!r.changed && !r.launched)
        return;
    land("input");
}

void MotionCtl::adopt_check(bool after, bool moving, int64_t now)
{
    char why = '-';
    if (after)
        why = 'k';
    else if (stale_)
        why = 's';
    else if (moving)
        why = 'm';
    else if (redraw_pending && redraw_pending())
        why = 'r';
    else if (target_current)
        why = target_current(now);
    from_target_ = why == 0;
    l5_ = why == 0 ? 'a' : why;
}

void MotionCtl::start(FoldKind k, FoldPlay play, int64_t now)
{
    started_ = true;
    finish(now);
    bool moving = false;
    for (const Channel& c : channels_)
        if (c.running && c.running(now))
            moving = true;
    land_channels(now, "fold");
    if (can_animate && !can_animate()) {
        land("hidden");
        stale_ = true;
        if (landed)
            landed(fold_kind_name(k), "hidden");
        return;
    }
    bool after = false;
    if (fold_) {
        if (inverse(kind_, k) && opens(kind_) && !reversed_ && play_.forward) {
            std::swap(from_, to_);
            back_e_ = now - t0_;
            if (play_.clamp_back)
                back_e_ = std::min(back_e_, play_.duration - play_.tail);
            t0_ = now;
            reversed_ = true;
            kind_ = k;
            end_shown_ = false;
            part_ = Rect{};
            from_us_ = -1;
            start_tag_ = "reversed";
            from_target_ = false;
            l5_ = 'v';
            if (settle)
                settle();
            render_to();
            return;
        }
        land("input");
        after = true;
    }
    kind_ = k;
    play_ = std::move(play);
    t0_ = now;
    reversed_ = false;
    back_e_ = 0;
    fold_ = true;
    end_shown_ = false;
    copied_ = false;
    from_target_ = false;
    l5_ = '-';
    prev_t_ = -1;
    part_ = Rect{};
    start_tag_ = "-";
    if (!play_.forward) {
        to_dirty_ = true;
        return;
    }
    int64_t c0 = clock_us ? clock_us() : 0;
    if (k == FoldKind::Launch) {
        copied_ = last != nullptr;
        if (copied_) {
            last(from_.surface());
            start_tag_ = "rest";
        }
    } else if (k != FoldKind::Return && (opens(k) || after)) {
        copied_ = !after && !moving && !stale_ && !play_.details && last && !(redraw_pending && redraw_pending());
        if (!play_.details && last)
            adopt_check(after, moving, now);
        else if (play_.details)
            l5_ = after ? 'k' : 'd';
        if (copied_) {
            last(from_.surface());
            start_tag_ = "rest";
        } else if (render_kept) {
            render_kept(from_.surface());
            start_tag_ = after ? "after" : "kept";
        } else if (after) {
            start_tag_ = "after";
        }
    } else if (k != FoldKind::Return && last) {
        adopt_check(false, moving, now);
        if (play_.kept_from && render_kept && (stale_ || (redraw_pending && redraw_pending()))) {
            render_kept(from_.surface());
            start_tag_ = "kept";
        } else {
            last(from_.surface());
            start_tag_ = "rest";
        }
    }
    from_us_ = clock_us ? (int)(clock_us() - c0) : -1;
    if (settle)
        settle();
    render_to();
    if (begun)
        begun();
}

bool MotionCtl::reverse(int64_t now)
{
    if (!fold_ || reversed_ || !opens(kind_) || !play_.forward)
        return false;
    back_e_ = std::min(std::max<int64_t>(0, now - t0_), play_.duration);
    t0_ = now;
    reversed_ = true;
    end_shown_ = false;
    to_dirty_ = false;
    part_ = Rect{};
    prev_t_ = -1;
    from_us_ = -1;
    return true;
}

bool MotionCtl::frame(int64_t now, const Surface& dst)
{
    if (fold_ && play_.forward) {
        if (end_shown_ && now - t0_ >= run_length())
            land("end");
    } else {
        finish(now);
    }
    damage_ = dst.rect();
    forward_us_ = -1;
    render_us_ = clock_us ? 0 : -1;
    auto timed = [&](auto f) {
        int64_t r0 = clock_us ? clock_us() : 0;
        f();
        if (clock_us)
            render_us_ += (int)(clock_us() - r0);
    };
    if (fold_ && play_.forward) {
        if (to_dirty_)
            timed([&]() { render_to(); });
        Surface f = from_.surface();
        Surface t = to_.surface();
        bool fw = forward_dir();
        int64_t ft = forward_time(now);
        int64_t c0 = clock_us ? clock_us() : 0;
        if (play_.advance)
            play_.advance(t0_, now);
        damage_ = play_.forward(fw ? f : t, fw ? t : f, dst, ft, prev_t_);
        if (clock_us)
            forward_us_ = (int)(clock_us() - c0);
        prev_t_ = ft;
        if (now - t0_ >= run_length())
            end_shown_ = true;
    } else {
        if (render)
            timed([&]() { render(dst, dst.rect()); });
        stale_ = false;
        prev_t_ = -1;
    }
    return animating(now);
}

bool MotionCtl::folding(int64_t now) const
{
    if (!fold_)
        return false;
    return play_.forward ? true : now - t0_ < run_length();
}

bool MotionCtl::animating(int64_t now) const
{
    if (folding(now))
        return true;
    for (const Channel& c : channels_)
        if (c.running && c.running(now))
            return true;
    return false;
}

}
