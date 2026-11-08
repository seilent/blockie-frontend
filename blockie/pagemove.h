#pragma once
#include <cstdint>

namespace fd {

struct ItemPose {
    int a256 = 256;
    int dx = 0;
};

class PageMove {
public:
    void enter(int64_t now, int count, int stagger_ms);
    void land() { on_ = false; }
    bool running(int64_t now) const { return on_ && now < end_; }
    int64_t end() const { return on_ ? end_ : 0; }
    ItemPose item(int64_t now, int i) const;
    int grid_a256(int64_t now) const;
    int bar_a256(int64_t now) const;

private:
    bool on_ = false;
    int stagger_ = 0;
    int64_t start_ = 0;
    int64_t end_ = 0;
};

}
