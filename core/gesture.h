#pragma once
#include <cstdint>

#include "fx.h"

namespace fd {

enum class Axis : uint8_t { Vertical, Horizontal };

Axis axis_of(int64_t dx, int64_t dy);
bool touch_tap(fx x0, fx y0, fx x1, fx y1);

enum class Swipe : uint8_t { None, Left, Right, Up, Down };

Swipe swipe_of(int64_t dx, int64_t dy);
Swipe swipe_up_of(int64_t dx, int64_t dy);
Swipe swipe_down_of(int64_t dx, int64_t dy);

class Gesture {
public:
    enum class State : uint8_t { Idle, Pending, Vertical, Horizontal, NoCommit };
    enum class Edge : uint8_t { None, Left, Right };

    void reset()
    {
        state_ = State::Idle;
        edge_ = Edge::None;
        vertical_ = down_ = turn_ = turned_ = land_ = kept_ = false;
    }
    void arm(fx x, fx y, Edge edge = Edge::None, bool bottom = false, bool turn = false, int64_t ms = 0);
    void arm_vertical(fx x, fx y, bool down, int64_t ms = 0);
    bool move(fx x, fx y, int64_t ms = 0);
    Swipe up(fx x, fx y, int64_t ms = 0, bool bare = false);
    Swipe lift_commit(fx x, fx y, int64_t ms, bool bare, bool& turned, fx& lift) const;
    fx lift_x() const { return lift_x_; }
    void cancel()
    {
        if (state_ != State::Idle)
            state_ = State::NoCommit;
    }
    State state() const { return state_; }
    Edge edge() const { return edge_; }
    bool turned() const { return turned_; }
    fx origin_x() const { return x0_; }
    Swipe commit_at(fx x, fx y) const;
    void keep() { kept_ = true; }

private:
    struct Sample {
        fx x = 0, y = 0;
        int64_t ms = 0;
    };

    bool turn(fx x, fx y);
    bool lift_point(int64_t ms, bool bare, fx& x, fx& y) const;

    State state_ = State::Idle;
    fx x0_ = 0, y0_ = 0, ax_ = 0, ay_ = 0, px_ = 0, py_ = 0, lx_ = 0, ly_ = 0, lift_x_ = 0;
    Sample prev_, last_;
    int samples_ = 0;
    Edge edge_ = Edge::None;
    bool bottom_ = false, vertical_ = false, down_ = false, turn_ = false, turned_ = false, land_ = false;
    bool kept_ = false;
};

}
