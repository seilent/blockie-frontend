#pragma once
#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "../gfx/canvas.h"
#include "../gfx/font.h"

namespace fd {

struct VLine {
    std::vector<uint8_t> plane;
    int w = 0;
    int h = 0;
    int ascent = 0;
    int pen = 0;
};

void vtext_turn(const uint8_t* mask, int w, int h, uint8_t* plane);
bool vtext_raster(Fonts& f, const TextStyle& st, const std::string& s, int cut, VLine& out);
Rect vtext_rect(const VLine& l, int ascent_x, int pen_y);
int vtext_pen(const VLine& l, int pen_y, int bottom);
void vtext_draw(Canvas& c, const VLine& l, int ascent_x, int pen_y, uint32_t rgb, int a256);

class VText {
public:
    static constexpr size_t kCap = 48;

    explicit VText(size_t cap = kCap) : cap_(cap) {}

    std::shared_ptr<const VLine> line(Fonts& f, const TextStyle& st, const std::string& text, int wrap, int index,
                                      const std::string& line_text);
    void draw(Canvas& c, Fonts& f, const TextStyle& st, const std::string& text, int wrap, int index,
              const std::string& line_text, int ascent_x, int pen_y, int bottom, uint32_t rgb, int a256);
    size_t size() const { return lru_.size(); }
    size_t rasters() const { return rasters_; }
    void clear() { lru_.clear(); }

private:
    struct Entry {
        std::string text;
        TextStyle st;
        int wrap = 0;
        int index = 0;
        std::shared_ptr<const VLine> l;
    };

    size_t cap_;
    size_t rasters_ = 0;
    std::list<Entry> lru_;
};

}
