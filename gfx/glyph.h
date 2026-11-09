#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "../core/fx.h"
#include "canvas.h"

namespace fd {

struct Coverage {
    int w = 0, h = 0;
    std::vector<uint8_t> rot;

    size_t index(int x, int y) const { return LiveL::index(w, h, x, y); }
};

struct GlyphPath {
    const char* name;
    bool fill;
    const char* d;
};

const GlyphPath* glyph_path(size_t i);
bool glyph_coverage(const char* name, int px, Coverage& out, fx scale = kOne);
bool path_coverage(const char* d, bool fill, int px, Coverage& out, fx scale = kOne);
void ring_coverage(int px, int stroke_px, Coverage& out, fx scale = kOne);
void arc_coverage(int px, int stroke_px, int start_deg, int sweep_deg, Coverage& out, fx scale = kOne);
void glow_coverage(const Coverage& src, int grow, Coverage& out, int hole = 0, int reach = 0);
void dilate_coverage(const Coverage& src, int r, Coverage& out);
void scale_coverage(const Coverage& src, int px, Coverage& out);

struct Icon {
    const char* glyph = nullptr;
    int px = 0, stroke = 0;
    fx scale = kOne;
};

class GlyphCache {
public:
    const Coverage& glyph(const char* name, int px, fx scale = kOne);
    const Coverage& ring(int px, int stroke_px, fx scale = kOne);
    const Coverage& arc(int px, int stroke_px, int start_deg, int sweep_deg, fx scale = kOne);
    bool warm(const Icon& i);
    size_t size() const { return map_.size(); }
    size_t misses() const { return misses_; }
    size_t bytes() const { return bytes_; }

private:
    std::unordered_map<std::string, Coverage> map_;
    size_t misses_ = 0, bytes_ = 0;
};

void draw_centered(Canvas& c, const Coverage& cv, int cx, int cy, uint32_t rgb, int a256);

}
