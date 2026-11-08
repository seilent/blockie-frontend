#include "vtext.h"

#include <algorithm>

#include "../ui/roles.h"

namespace fd {

namespace {

bool same_style(const TextStyle& a, const TextStyle& b)
{
    return a.weight == b.weight && a.px == b.px && a.track64 == b.track64 && a.tcase == b.tcase;
}

}

void vtext_turn(const uint8_t* mask, int w, int h, uint8_t* plane)
{
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            plane[(size_t)(w - 1 - x) * h + y] = mask[(size_t)y * w + x];
}

bool vtext_raster(Fonts& f, const TextStyle& st, const std::string& s, int cut, VLine& out)
{
    int asc = f.ascent(st.px);
    int desc = f.descent(st.px);
    int line = std::max(1, asc + desc);
    int h = line + desc;
    int left64 = 0, right64 = 0;
    f.ink64(st, s.c_str(), left64, right64);
    int adv64 = f.width64(st, s.c_str());
    int pen = left64 < 0 ? (-left64 + 63) >> 6 : 0;
    int w = std::max(1, pen + ((std::max(right64, adv64) + 63) >> 6) + 1);
    if (cut > 0)
        w = std::min(w, pen + cut);
    Layer layer(w, h);
    Canvas c(layer);
    bool done = f.draw(c, st, s.c_str(), pen * 64, asc, kWhite, 256);
    int rows = line;
    for (int y = h - 1; y >= line && rows == line; y--)
        for (int x = 0; x < w; x++)
            if (layer.mask[(size_t)y * w + x]) {
                rows = y + 1;
                break;
            }
    out.w = rows;
    out.h = w;
    out.ascent = asc;
    out.pen = pen;
    out.plane.assign((size_t)w * rows, 0);
    vtext_turn(layer.mask.data(), w, rows, out.plane.data());
    return done;
}

Rect vtext_rect(const VLine& l, int ascent_x, int pen_y)
{
    int y = pen_y - (l.h - l.pen);
    return {ascent_x, y, ascent_x + l.w, y + l.h};
}

int vtext_pen(const VLine& l, int pen_y, int bottom) { return std::min(pen_y, bottom - l.pen); }

void vtext_draw(Canvas& c, const VLine& l, int ascent_x, int pen_y, uint32_t rgb, int a256)
{
    if (l.plane.empty() || a256 <= 0)
        return;
    Rect r = vtext_rect(l, ascent_x, pen_y);
    c.coverage(l.plane.data(), l.w, l.h, r.x0, r.y0, rgb, a256);
}

std::shared_ptr<const VLine> VText::line(Fonts& f, const TextStyle& st, const std::string& text, int wrap, int index,
                                         const std::string& line_text)
{
    for (auto it = lru_.begin(); it != lru_.end(); ++it)
        if (it->index == index && it->wrap == wrap && same_style(it->st, st) && it->text == text) {
            lru_.splice(lru_.begin(), lru_, it);
            return lru_.front().l;
        }
    std::shared_ptr<VLine> l = std::make_shared<VLine>();
    bool done = vtext_raster(f, st, line_text, wrap, *l);
    rasters_++;
    if (done && cap_ > 0) {
        Entry e;
        e.text = text;
        e.st = st;
        e.wrap = wrap;
        e.index = index;
        e.l = l;
        lru_.push_front(std::move(e));
        while (lru_.size() > cap_)
            lru_.pop_back();
    }
    return l;
}

void VText::draw(Canvas& c, Fonts& f, const TextStyle& st, const std::string& text, int wrap, int index,
                 const std::string& line_text, int ascent_x, int pen_y, int bottom, uint32_t rgb, int a256)
{
    if (a256 <= 0 || line_text.empty())
        return;
    std::shared_ptr<const VLine> l = line(f, st, text, wrap, index, line_text);
    vtext_draw(c, *l, ascent_x, vtext_pen(*l, pen_y, bottom), rgb, a256);
}

}
