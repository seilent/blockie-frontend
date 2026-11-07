#include "gesture.h"

namespace fd {

namespace {

int64_t mag(int64_t v) { return v < 0 ? -v : v; }

}

Axis axis_of(int64_t dx, int64_t dy) { return mag(dy) < mag(dx) ? Axis::Horizontal : Axis::Vertical; }

bool touch_tap(fx x0, fx y0, fx x1, fx y1)
{
    int64_t dx = (int64_t)x1 - x0, dy = (int64_t)y1 - y0;
    int64_t slop = std_u(tok::touchSlop);
    return dx * dx + dy * dy <= slop * slop;
}

Swipe swipe_of(int64_t dx, int64_t dy)
{
    if (mag(dx) <= std_u(tok::gesture_swipe) || mag(dx) * 10 <= mag(dy) * 14)
        return Swipe::None;
    return dx < 0 ? Swipe::Left : Swipe::Right;
}

Swipe swipe_up_of(int64_t dx, int64_t dy)
{
    if (dy >= -(int64_t)std_u(tok::gesture_swipe) || mag(dy) * 10 <= mag(dx) * 14)
        return Swipe::None;
    return Swipe::Up;
}

Swipe swipe_down_of(int64_t dx, int64_t dy)
{
    if (dy <= (int64_t)std_u(tok::gesture_swipe) || mag(dy) * 10 <= mag(dx) * 14)
        return Swipe::None;
    return Swipe::Down;
}

void Gesture::arm(fx x, fx y, Edge edge, bool bottom, bool turn, int64_t ms)
{
    state_ = State::Pending;
    last_ = {x, y, ms};
    samples_ = 1;
    x0_ = x;
    y0_ = y;
    edge_ = edge;
    bottom_ = bottom;
    turn_ = turn;
    vertical_ = down_ = turned_ = land_ = kept_ = false;
}

void Gesture::arm_vertical(fx x, fx y, bool down, int64_t ms)
{
    arm(x, y, Edge::None, false, false, ms);
    vertical_ = true;
    down_ = down;
}

bool Gesture::move(fx x, fx y, int64_t ms)
{
    prev_ = last_;
    last_ = {x, y, ms};
    samples_++;
    if (state_ == State::Vertical && turn_)
        return turn(x, y);
    if (state_ == State::Horizontal && turned_ && turn_ && !kept_ &&
        axis_of((int64_t)x - x0_, (int64_t)y - y0_) == Axis::Vertical) {
        state_ = State::Vertical;
        turned_ = false;
        ax_ = px_ = x;
        ay_ = py_ = y;
        return false;
    }
    if (state_ != State::Pending || touch_tap(x0_, y0_, x, y))
        return false;
    state_ = axis_of((int64_t)x - x0_, (int64_t)y - y0_) == Axis::Horizontal ? State::Horizontal : State::Vertical;
    ax_ = px_ = x;
    ay_ = py_ = y;
    return true;
}

bool Gesture::turn(fx x, fx y)
{
    if (land_) {
        land_ = false;
        int64_t on = (int64_t)x - lx_, was = (int64_t)lx_ - ax_;
        if (on == 0 || (on < 0) != (was < 0)) {
            ax_ = lx_;
            ay_ = ly_;
        }
    } else if (!touch_tap(px_, py_, x, y) && axis_of((int64_t)x - ax_, (int64_t)y - ay_) == Axis::Horizontal) {
        land_ = true;
        lx_ = px_ = x;
        ly_ = py_ = y;
        return false;
    }
    px_ = x;
    py_ = y;
    if (axis_of((int64_t)x - ax_, (int64_t)y - ay_) == Axis::Vertical) {
        ax_ = x;
        ay_ = y;
        return false;
    }
    if (touch_tap(ax_, ay_, x, y))
        return false;
    state_ = State::Horizontal;
    x0_ = ax_;
    y0_ = ay_;
    edge_ = Edge::None;
    turned_ = true;
    return true;
}

Swipe Gesture::commit_at(fx x, fx y) const
{
    return state_ == State::Horizontal ? swipe_of((int64_t)x - x0_, (int64_t)y - y0_) : Swipe::None;
}

bool Gesture::lift_point(int64_t ms, bool bare, fx& x, fx& y) const
{
    if (!bare || samples_ < 2 || (state_ != State::Vertical && state_ != State::Horizontal))
        return false;
    int64_t step = last_.ms - prev_.ms, gap = ms - last_.ms;
    if (step <= 0 || gap < 0 || gap > step)
        return false;
    x = last_.x + (fx)(((int64_t)last_.x - prev_.x) * gap / step);
    y = last_.y + (fx)(((int64_t)last_.y - prev_.y) * gap / step);
    return true;
}

Swipe Gesture::lift_commit(fx x, fx y, int64_t ms, bool bare, bool& turned, fx& lift) const
{
    Gesture g = *this;
    Swipe s = g.up(x, y, ms, bare);
    turned = g.turned_;
    lift = g.lift_x_;
    return s;
}

Swipe Gesture::up(fx x, fx y, int64_t ms, bool bare)
{
    fx lx = 0, ly = 0;
    if (lift_point(ms, bare, lx, ly)) {
        move(lx, ly, ms);
        x = lx;
        y = ly;
    }
    lift_x_ = x;
    State s = state_;
    state_ = State::Idle;
    int64_t dx = (int64_t)x - x0_, dy = (int64_t)y - y0_;
    if (vertical_) {
        if (s != State::Pending && s != State::Vertical)
            return Swipe::None;
        if (swipe_up_of(dx, dy) == Swipe::Up)
            return Swipe::Up;
        return down_ ? swipe_down_of(dx, dy) : Swipe::None;
    }
    if (bottom_ && (s == State::Pending || s == State::Vertical) && swipe_up_of(dx, dy) == Swipe::Up)
        return Swipe::Up;
    if (s != State::Pending && s != State::Horizontal)
        return Swipe::None;
    return swipe_of(dx, dy);
}

}
