#pragma once
#include <cstdint>

#include "../core/press.h"
#include "../gfx/canvas.h"
#include "../gfx/glyph.h"
#include "navkind.h"
#include "roles.h"
#include "size.h"

namespace fd {

class NavBar {
public:
    enum Slot { Back, Home, Custom, kSlots };
    struct Look {
        fx alpha[kSlots] = {};
        const char* custom = "play";
        int press[kSlots] = {};
        int y = 0;
        fx pop = kOne;
        fx ppu = 0;
        bool operator==(const Look& o) const;
        bool operator!=(const Look& o) const { return !(*this == o); }
    };

    static fx top() { return canvas_h() - tok::navH; }
    static Rect band() { return {0, px(top()), kPortraitW, kPortraitH}; }
    static const char* glyph_of(NavButton b, bool playing);
    static void icons(std::vector<Icon>& out);
    static void press_icons(std::vector<Icon>& out, NavButton custom);

    void set_order(NavOrder o) { order_ = o; }
    void set_rings(bool np) { np_ = np; }
    NavOrder order() const { return order_; }
    fx center(int slot) const;
    int hit(fx x, fx y, const fx alpha[kSlots]) const;
    void draw(Canvas& c, GlyphCache& g, const Look& l, int a256);
    Look look(const fx alpha[kSlots], const char* custom, fx pop, int y, int64_t now);

    PressSlots press;

private:
    NavOrder order_ = NavOrder::BackLeft;
    bool np_ = false;
};

}
