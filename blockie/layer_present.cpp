#include "layer_present.h"

#include <algorithm>

namespace fd {

bool regions_visible(const std::vector<AlphaRegion>& regions)
{
    for (const AlphaRegion& g : regions)
        if (g.a256 > 0 && !g.r.empty())
            return true;
    return false;
}

uint8_t region_a8(int a256) { return (uint8_t)((std::clamp(a256, 0, 256) * 255 + 128) >> 8); }

void write_argb(const Surface& src, const std::vector<AlphaRegion>& regions, uint32_t* dst, int stride)
{
    for (int y = 0; y < src.h; y++)
        std::fill(dst + (size_t)y * stride, dst + (size_t)y * stride + src.w, 0u);
    for (const AlphaRegion& g : regions) {
        Rect r = g.r.intersect(src.rect());
        uint32_t a = region_a8(g.a256);
        for (int y = r.y0; y < r.y1; y++) {
            uint32_t* row = dst + (size_t)y * stride;
            for (int x = r.x0; x < r.x1; x++) {
                uint32_t c = LiveF::unpack(src.at(x, y));
                uint32_t rr = ((c >> 16) & 0xFF) * a + 127, gg = ((c >> 8) & 0xFF) * a + 127, bb = (c & 0xFF) * a + 127;
                row[x] = a << 24 | (rr / 255) << 16 | (gg / 255) << 8 | bb / 255;
            }
        }
    }
}

}
