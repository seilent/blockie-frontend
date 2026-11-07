#include "backdrop.h"

#include <algorithm>

#include "../core/prof.h"

namespace fd {

void shade_backdrop(const ImageT<X888, Rot>& img, const SurfaceT<X888, Rot>& dst, uint8_t* mask, Rect clip,
                    const int* shade, uint32_t bg)
{
    prof::Scope ps(prof::Shade);
    Rect m = dst.mem_rect(clip);
    if (m.empty() || img.w != dst.w || img.h != dst.h)
        return;
    for (int row = m.y0; row < m.y1; row++) {
        int x = dst.w - 1 - row;
        const uint32_t* src = img.mem.data() + (size_t)(img.w - 1 - x) * img.h;
        uint32_t* d = dst.mem_row(row);
        for (int j = m.x0; j < m.x1; j++)
            d[j] = blend_px(src[j], bg, shade[j]);
        if (mask)
            std::fill_n(mask + (size_t)row * dst.stride + m.x0, m.x1 - m.x0, (uint8_t)255);
    }
}

void shade_backdrop(const ImageT<X888, Rows>& img, const SurfaceT<R565, Rows>& dst, uint8_t* mask, Rect clip,
                    const int* shade, uint32_t bg)
{
    prof::Scope ps(prof::Shade);
    Rect p = clip.intersect(dst.rect());
    if (p.empty() || img.w != dst.w || img.h != dst.h)
        return;
    int n = p.x1 - p.x0;
    for (int y = p.y0; y < p.y1; y++) {
        const uint32_t* s = img.mem.data() + img.index(p.x0, y);
        uint16_t* d = dst.row(y) + p.x0;
        int a = shade[y];
        for (int i = 0; i < n; i++)
            d[i] = pack565(blend_px(s[i], bg, a));
        if (mask)
            std::fill_n(mask + dst.index(p.x0, y), n, (uint8_t)255);
    }
}

}
