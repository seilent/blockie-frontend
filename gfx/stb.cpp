#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "turn.h"

namespace fd {

bool turn_glyph(const stbtt_fontinfo* info, int px, int gid, int angle, int& w, int& h, int& xoff, int& yoff,
                std::vector<uint8_t>& rows)
{
    stbtt_vertex* v = nullptr;
    int n = stbtt_GetGlyphShape(info, gid, &v);
    if (n <= 0)
        return false;
    int m = 1;
    auto most = [&](short x, short y) { m = std::max({m, std::abs((int)x), std::abs((int)y)}); };
    for (int i = 0; i < n; i++) {
        most(v[i].x, v[i].y);
        if (v[i].type == STBTT_vcurve || v[i].type == STBTT_vcubic)
            most(v[i].cx, v[i].cy);
        if (v[i].type == STBTT_vcubic)
            most(v[i].cx1, v[i].cy1);
    }
    const float k = std::min(8.0f, 23000.0f / (float)m), r = 0.70710678f, s = angle > 0 ? r : -r;
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    auto turn = [&](short& x, short& y) {
        float a = (float)x * k, b = (float)y * k;
        x = (short)lroundf(a * r + b * s);
        y = (short)lroundf(b * r - a * s);
        x0 = std::min(x0, (float)x);
        x1 = std::max(x1, (float)x);
        y0 = std::min(y0, (float)y);
        y1 = std::max(y1, (float)y);
    };
    for (int i = 0; i < n; i++) {
        turn(v[i].x, v[i].y);
        if (v[i].type == STBTT_vcurve || v[i].type == STBTT_vcubic)
            turn(v[i].cx, v[i].cy);
        if (v[i].type == STBTT_vcubic)
            turn(v[i].cx1, v[i].cy1);
    }
    float sc = stbtt_ScaleForMappingEmToPixels(info, (float)px) / k;
    int ix0 = (int)floorf(x0 * sc), ix1 = (int)ceilf(x1 * sc), iy0 = (int)floorf(-y1 * sc), iy1 = (int)ceilf(-y0 * sc);
    bool ok = ix1 > ix0 && iy1 > iy0;
    if (ok) {
        w = ix1 - ix0;
        h = iy1 - iy0;
        xoff = ix0;
        yoff = iy0;
        constexpr int kStrip = 64;
        std::vector<uint8_t> strip((size_t)w * std::min(h, kStrip + 1));
        std::vector<std::pair<int, int>> runs((size_t)std::min(h, kStrip + 1));
        rows.clear();
        rows.shrink_to_fit();
        rows.reserve((size_t)h * 4);
        rows.assign((size_t)h * 4, 0);
        for (int y0 = 0, sh = 0; y0 < h; y0 += sh) {
            sh = std::min(kStrip, h - y0);
            if (y0 + sh < h && iy0 + y0 + sh == 0)
                sh++;
            std::fill(strip.begin(), strip.end(), 0);
            stbtt__bitmap bm{w, sh, w, strip.data()};
            stbtt_Rasterize(&bm, 0.35f, v, n, sc, sc, 0, 0, ix0, iy0 + y0, 1, info->userdata);
            size_t total = 0;
            for (int y = 0; y < sh; y++) {
                const uint8_t* row = strip.data() + (size_t)y * w;
                int a = 0, b = w;
                while (a < b && !row[a])
                    a++;
                while (b > a && !row[b - 1])
                    b--;
                runs[(size_t)y] = {a, b};
                total += (size_t)(b - a);
            }
            rows.reserve(rows.size() + total);
            for (int y = 0; y < sh; y++) {
                const uint8_t* row = strip.data() + (size_t)y * w;
                int a = runs[(size_t)y].first, b = runs[(size_t)y].second;
                size_t hd = (size_t)(y0 + y) * 4;
                rows[hd] = (uint8_t)a;
                rows[hd + 1] = (uint8_t)(a >> 8);
                rows[hd + 2] = (uint8_t)(b - a);
                rows[hd + 3] = (uint8_t)((b - a) >> 8);
                rows.insert(rows.end(), row + a, row + b);
            }
        }
    }
    stbtt_FreeShape(info, v);
    return ok;
}

}
