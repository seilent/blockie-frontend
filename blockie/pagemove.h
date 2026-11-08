#pragma once
#include <cstdint>

#include "../core/fx.h"
#include "../gfx/canvas.h"

namespace fd {

enum class PagePhase { Idle, HomeExit, PageEnter, PageExit, HomeEnter };

struct ItemPose {
    int a256 = 256;
    int dx = 0;
    int dy = 0;
};

class PageMove {
public:
    void push(int64_t now, int focus, int count);
    void pop(int64_t now, int count);
    void enter(int64_t now, int count, int stagger_ms);
    void land() { on_ = false; }
    PagePhase phase(int64_t now) const;
    bool running(int64_t now) const { return phase(now) != PagePhase::Idle; }
    int64_t end() const { return on_ ? end_ : 0; }
    ItemPose item(int64_t now, int i) const;
    int grid_a256(int64_t now) const;
    int bar_a256(int64_t now) const;
    fx page_angle(int64_t now) const;
    int page_a256(int64_t now) const;
    int header_dy(int64_t now) const;
    int header_a256(int64_t now) const;

private:
    void enter_at(int64_t now, int64_t split, int count, int stagger_ms);
    fx header_fly(int64_t now) const;

    bool on_ = false;
    bool pop_ = false;
    int focus_ = 0;
    int stagger_ = 0;
    int64_t start_ = 0;
    int64_t split_ = 0;
    int64_t end_ = 0;
};

int turnstile_d_px();
void draw_turnstile(const Surface& page, const Surface& dst, fx angle_deg, int a256, uint32_t bg);

}
