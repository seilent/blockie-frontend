#include "ink.h"

#include <algorithm>

namespace fd {

void ink_clear(const Surface& layer, Rect area)
{
    Rect r = area.intersect(layer.rect());
    for (int y = r.y0; y < r.y1; y++)
        for (int x = r.x0; x < r.x1; x++)
            layer.at(x, y) = LiveF::pack(0);
}

void ink_compose(const Surface& layer, const Surface& dst, uint32_t ink, Rect area)
{
    Rect r = area.intersect(layer.rect()).intersect(dst.rect());
    int ir = (int)((ink >> 16) & 0xFF);
    int ig = (int)((ink >> 8) & 0xFF);
    int ib = (int)(ink & 0xFF);
    for (int y = r.y0; y < r.y1; y++)
        for (int x = r.x0; x < r.x1; x++) {
            uint32_t cov = (LiveF::unpack(layer.at(x, y)) >> 8) & 0xFF;
            if (cov == 0)
                continue;
            uint32_t d = LiveF::unpack(dst.at(x, y));
            int dr = (int)((d >> 16) & 0xFF);
            int dg = (int)((d >> 8) & 0xFF);
            int db = (int)(d & 0xFF);
            int a = (int)cov;
            dr = (dr * (255 - a) + ir * a + 127) / 255;
            dg = (dg * (255 - a) + ig * a + 127) / 255;
            db = (db * (255 - a) + ib * a + 127) / 255;
            dst.at(x, y) = LiveF::pack(((uint32_t)dr << 16) | ((uint32_t)dg << 8) | (uint32_t)db);
        }
}

void ink_add(const Surface& layer, const Surface& more, int keep256, int more256, Rect area)
{
    Rect r = area.intersect(layer.rect()).intersect(more.rect());
    for (int y = r.y0; y < r.y1; y++)
        for (int x = r.x0; x < r.x1; x++) {
            int a = (int)((LiveF::unpack(layer.at(x, y)) >> 8) & 0xFF);
            int b = (int)((LiveF::unpack(more.at(x, y)) >> 8) & 0xFF);
            uint32_t v = (uint32_t)std::min(255, (a * keep256 + b * more256 + 128) >> 8);
            layer.at(x, y) = LiveF::pack(v << 16 | v << 8 | v);
        }
}

Canvas& InkLayer::begin(Rect area)
{
    area_ = area.intersect(buf_.surface().rect());
    ink_clear(buf_.surface(), area_);
    canvas_ = Canvas(buf_.surface());
    canvas_.set_clip(area_);
    return canvas_;
}

void InkLayer::compose(const Surface& dst, uint32_t ink) { ink_compose(buf_.surface(), dst, ink, area_); }

}
