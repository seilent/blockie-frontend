#include "navbar.h"

#include <cstring>

namespace fd {

namespace {

void add_icon(std::vector<Icon>& out, const char* name)
{
    int size = px(tok::navGlyph);
    if (strcmp(name, "search") == 0) {
        out.push_back({"searchRing", size});
        out.push_back({"searchHandle", size});
    } else {
        out.push_back({name, size});
    }
}

}

bool NavBar::Look::operator==(const Look& o) const
{
    for (int i = 0; i < kSlots; i++)
        if (alpha256(alpha[i]) != alpha256(o.alpha[i]) || press[i] != o.press[i])
            return false;
    return strcmp(custom, o.custom) == 0 && y == o.y && pop == o.pop && ppu == o.ppu;
}

const char* NavBar::glyph_of(NavButton b, bool playing)
{
    switch (b) {
    case NavButton::Play:
        return playing ? "pause" : "play";
    case NavButton::NowPlaying:
        return "disc";
    case NavButton::Bluetooth:
        return "bluetooth";
    case NavButton::Next:
        return "next";
    case NavButton::Shuffle:
        return "shuffle";
    case NavButton::Search:
        return "search";
    case NavButton::Settings:
        return "gear";
    }
    return "play";
}

void NavBar::icons(std::vector<Icon>& out)
{
    add_icon(out, "back");
    add_icon(out, "home");
    for (int b = 0; b < kNavButtons; b++) {
        add_icon(out, glyph_of((NavButton)b, false));
        add_icon(out, glyph_of((NavButton)b, true));
    }
}

void NavBar::press_icons(std::vector<Icon>& out, NavButton custom)
{
    add_icon(out, "home");
    add_icon(out, glyph_of(custom, false));
    add_icon(out, glyph_of(custom, true));
}

fx NavBar::center(int slot) const
{
    int at = order_ == NavOrder::BackLeft ? slot : kSlots - 1 - slot;
    if (np_)
        return at == 0 ? tok::npRingCx0 : at == 1 ? tok::npRingCx1 : tok::npRingCx2;
    return at == 0 ? tok::npRingX0 : at == 1 ? canvas_w() / 2 : canvas_w() - tok::npRingX0;
}

int NavBar::hit(fx x, fx y, const fx alpha[kSlots]) const
{
    if (y < top() || y >= canvas_h() || x < 0 || x >= canvas_w())
        return -1;
    fx half = (canvas_w() / 2 - tok::npRingX0) / 2;
    for (int i = 0; i < kSlots; i++) {
        fx cx = center(i);
        if (alpha256(alpha[i]) > 0 && x >= cx - half && x < cx + half)
            return i;
    }
    return -1;
}

NavBar::Look NavBar::look(const fx alpha[kSlots], const char* custom, fx pop, int y, int64_t now)
{
    Look l;
    for (int i = 0; i < kSlots; i++) {
        l.alpha[i] = alpha[i];
        l.press[i] = press_step(press.draw(i, now));
    }
    l.custom = custom;
    l.pop = pop;
    l.y = y;
    l.ppu = ppu();
    return l;
}

void NavBar::draw(Canvas& c, GlyphCache& g, const Look& l, int a256)
{
    int size = px(tok::navGlyph), cy = px(top() + tok::navH / 2) + l.y;
    for (int i = 0; i < kSlots; i++) {
        int a = alpha256(fx_mul(tok::alpha_mid, l.alpha[i])) * a256 / 256;
        if (a <= 0)
            continue;
        fx s = press_factor(l.press[i]);
        int cx = px(center(i)), gs = i == Custom ? fx_round(size * l.pop) : size;
        const char* name = i == Back ? "back" : i == Home ? "home" : l.custom;
        if (strcmp(name, "search") == 0) {
            draw_centered(c, g.glyph("searchRing", gs, s), cx, cy, kWhite, a);
            draw_centered(c, g.glyph("searchHandle", gs, s), cx, cy, kWhite, a);
        } else {
            draw_centered(c, g.glyph(name, gs, s), cx, cy, kWhite, a);
        }
    }
}

}
