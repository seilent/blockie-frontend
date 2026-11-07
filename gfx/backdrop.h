#pragma once
#include <cstdint>

#include "canvas.h"
#if !defined(FD_ROT) && !defined(FD_PIX16)
#include "../core/prof.h"
#endif

namespace fd {

void shade_backdrop(const ImageT<X888, Rot>& img, const SurfaceT<X888, Rot>& dst, uint8_t* mask, Rect clip,
                    const int* shade, uint32_t bg);
void shade_backdrop(const ImageT<X888, Rows>& img, const SurfaceT<R565, Rows>& dst, uint8_t* mask, Rect clip,
                    const int* shade, uint32_t bg);

#if !defined(FD_ROT) && !defined(FD_PIX16)
inline void shade_backdrop(const ImageT<X888, Rows>& img, const SurfaceT<X888, Rows>& dst, uint8_t* mask, Rect clip,
                           const int* shade, uint32_t bg)
{
    prof::Scope ps(prof::Shade);
    Rect p = clip.intersect(dst.rect());
    if (p.empty() || img.w != dst.w || img.h != dst.h)
        return;
    int n = p.x1 - p.x0;
    for (int y = p.y0; y < p.y1; y++) {
        const uint32_t* s = img.mem.data() + img.index(p.x0, y);
        uint32_t* d = dst.row(y) + p.x0;
        int a = shade[y];
        for (int i = 0; i < n; i++)
            d[i] = blend_px(s[i], bg, a);
        if (mask)
            std::fill_n(mask + dst.index(p.x0, y), n, (uint8_t)255);
    }
}
#endif

}
