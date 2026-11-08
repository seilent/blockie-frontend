#include "motion.h"

#include <algorithm>
#include <utility>

namespace fd {

namespace {

bool opens(FoldKind k) { return k == FoldKind::Expand || k == FoldKind::Push; }

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
}

void MotionCtl::input(int64_t now, const std::function<InputResult()>& nav)
{
    finish(now);
    if (fold_ && play_.forward && to_dirty_)
        render_to();
    if (keep)
        keep();
    started_ = false;
    InputResult r = nav ? nav() : InputResult{};
    if (started_)
        return;
    if (!r.changed && !r.launched)
        return;
    land("input");
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
            t0_ = now;
            reversed_ = true;
            kind_ = k;
            end_shown_ = false;
            part_ = Rect{};
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
    prev_t_ = -1;
    part_ = Rect{};
    if (!play_.forward) {
        to_dirty_ = true;
        return;
    }
    if (opens(k) || after) {
        copied_ = !after && !moving && !stale_ && !play_.details && last && !(redraw_pending && redraw_pending());
        if (copied_)
            last(from_.surface());
        else if (render_kept)
            render_kept(from_.surface());
    } else if (last) {
        last(from_.surface());
    }
    if (settle)
        settle();
    render_to();
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
    if (fold_ && play_.forward) {
        if (to_dirty_)
            render_to();
        Surface f = from_.surface();
        Surface t = to_.surface();
        bool fw = forward_dir();
        int64_t ft = forward_time(now);
        damage_ = play_.forward(fw ? f : t, fw ? t : f, dst, ft, prev_t_);
        prev_t_ = ft;
        if (now - t0_ >= run_length())
            end_shown_ = true;
    } else {
        if (render)
            render(dst, dst.rect());
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
