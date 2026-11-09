#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <vector>

#include "../scale.h"
#include "../tile_grid.h"

namespace {

int g_fail = 0;
int g_total = 0;

void check(bool ok, const char* what)
{
    g_total++;
    if (ok) {
        std::printf("PASS %s\n", what);
    } else {
        std::printf("FAIL %s\n", what);
        g_fail++;
    }
}

std::vector<uint32_t> noise(int w, int h, uint32_t seed)
{
    std::vector<uint32_t> px((size_t)w * h);
    uint32_t s = seed;
    for (uint32_t& p : px) {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        p = s & 0xffffffu;
    }
    return px;
}

fd::Image cover(int side, uint32_t seed)
{
    std::vector<uint32_t> px = noise(side, side, seed);
    return fd::Image::from_portrait(px.data(), side, side);
}

fd::Buffer ground(uint32_t seed)
{
    fd::Buffer b;
    std::vector<uint32_t> g = noise(b.w, b.h, seed);
    for (size_t i = 0; i < b.pix.size(); i++)
        b.pix[i] = fd::LiveF::pack(g[i]);
    return b;
}

void k1(fd::Buffer& b, const fd::Image& img, fd::Rect64 r, fd::Rect clip, fd::Scaler k)
{
    fd::ScaleAxis xs, ys;
    fd::scale_axis(xs, r.x0, r.x1, img.w, clip.x0, clip.x1);
    fd::scale_axis(ys, r.y0, r.y1, img.h, clip.y0, clip.y1);
    fd::scale_tile(b.surface(), img, xs, ys, k);
}

void carved(fd::Buffer& b, const fd::Image& img, fd::Rect64 r, fd::Rect clip)
{
    fd::Canvas c(b.surface());
    c.set_clip(clip);
    c.blit_bilinear(img, r, 256);
}

int channel_diff(uint32_t a, uint32_t b)
{
    int m = 0;
    for (int sh = 0; sh < 24; sh += 8)
        m = std::max(m, std::abs((int)(a >> sh & 255) - (int)(b >> sh & 255)));
    return m;
}

int max_diff(const fd::Buffer& a, const fd::Buffer& b)
{
    int m = 0;
    for (size_t i = 0; i < a.pix.size(); i++)
        m = std::max(m, channel_diff(fd::LiveF::unpack(a.pix[i]), fd::LiveF::unpack(b.pix[i])));
    return m;
}

fd::Rect64 square(int x64, int y64, int side64) { return {x64, y64, x64 + side64, y64 + side64}; }

bool inside(fd::Rect r, int x, int y) { return x >= r.x0 && x < r.x1 && y >= r.y0 && y < r.y1; }

}

int main()
{
    const fd::Scaler both[] = {fd::Scaler::Channel, fd::Scaler::Swar};
    fd::Image img = cover(fd::kBigTileSide, 0x2545f491u);
    fd::Rect full{0, 0, fd::kPortraitW, fd::kPortraitH};
    fd::Buffer base = ground(0x9e3779b9u);

    {
        fd::Rect64 r = square(120 * 64, 80 * 64, fd::kBigTileSide * 64);
        bool copied = true, carved_eq = true;
        fd::Buffer ref = base;
        carved(ref, img, r, full);
        for (fd::Scaler k : both) {
            fd::Buffer got = base;
            k1(got, img, r, full, k);
            for (int y = 0; y < img.h; y++)
                for (int x = 0; x < img.w; x++)
                    if (got.surface().at(120 + x, 80 + y) != img.at(x, y))
                        copied = false;
            if (got.pix != ref.pix)
                carved_eq = false;
        }
        check(copied, "at identity both K1 variants copy the 232 image pixel for pixel");
        check(carved_eq, "at identity both K1 variants equal the carved bilinear");
    }

    {
        bool chan = true;
        int swar = 0;
        for (int side = fd::kTileSide; side <= fd::kBigTileSide; side++) {
            fd::Rect64 r = square(120 * 64 + 17, 80 * 64 + 41, side * 64 + 23);
            fd::Buffer ref = base, a = base, b = base;
            carved(ref, img, r, full);
            k1(a, img, r, full, fd::Scaler::Channel);
            k1(b, img, r, full, fd::Scaler::Swar);
            if (a.pix != ref.pix)
                chan = false;
            swar = std::max(swar, max_diff(ref, b));
        }
        check(chan, "K1 per channel equals the carved bilinear at sides 154 to 232 on fractional edges");
        check(swar <= 1, "K1 SWAR is within one level per channel of the carved bilinear at sides 154 to 232");
    }

    {
        bool chan = true;
        int swar = 0;
        for (fd::fx g = 0; g <= fd::kOne; g += fd::kOne / 16)
            for (int k : {0, 1, 3, 4}) {
                fd::Rect64 r = fd::grow_tile_rect(k, g, 0);
                fd::Buffer ref = base, a = base, b = base;
                carved(ref, img, r, full);
                k1(a, img, r, full, fd::Scaler::Channel);
                k1(b, img, r, full, fd::Scaler::Swar);
                if (a.pix != ref.pix)
                    chan = false;
                swar = std::max(swar, max_diff(ref, b));
            }
        check(chan && swar <= 1,
              "on the grow poses K1 per channel equals the carved bilinear and SWAR is within one level");
    }

    {
        fd::Rect clip{0, fd::kBandTop, 500, fd::kBandBottom};
        bool kept = true;
        for (fd::Scaler k : both)
            for (fd::Rect64 r : {fd::grow_tile_rect(7, fd::kOne / 2, 0), fd::grow_tile_rect(1, fd::kOne / 2, 1)}) {
                fd::Buffer whole = base, part = base;
                k1(whole, img, r, full, k);
                k1(part, img, r, clip, k);
                for (int y = 0; y < fd::kPortraitH; y++)
                    for (int x = 0; x < fd::kPortraitW; x++) {
                        size_t i = (size_t)y * fd::kPortraitW + x;
                        if (part.pix[i] != (inside(clip, x, y) ? whole.pix[i] : base.pix[i]))
                            kept = false;
                    }
            }
        check(kept, "the band and x < b clip keep every pixel outside and match the unclipped tile inside");
    }

    {
        fd::Rect64 r = square(300 * 64 + 9, 200 * 64 + 50, 190 * 64 + 31);
        fd::Rect outer = r.outer();
        fd::Buffer other = ground(0x85ebca6bu);
        bool outside = true, ring = true;
        int partial = 0;
        for (fd::Scaler k : both) {
            fd::Buffer a = base, b = other;
            k1(a, img, r, full, k);
            k1(b, img, r, full, k);
            for (int y = 0; y < fd::kPortraitH; y++)
                for (int x = 0; x < fd::kPortraitW; x++) {
                    size_t i = (size_t)y * fd::kPortraitW + x;
                    if (!inside(outer, x, y)) {
                        if (a.pix[i] != base.pix[i] || b.pix[i] != other.pix[i])
                            outside = false;
                        continue;
                    }
                    bool edge = x == outer.x0 || x == outer.x1 - 1 || y == outer.y0 || y == outer.y1 - 1;
                    if (a.pix[i] != b.pix[i]) {
                        partial++;
                        if (!edge)
                            ring = false;
                    }
                }
        }
        check(outside, "nothing is written outside the tile's outer rect");
        check(ring && partial > 0, "only the outer ring of pixels blends with the ground, the rest is opaque");
    }

    {
        fd::Image small = cover(fd::kTileSide, 7);
        fd::ScaleAxis xs, ys;
        fd::Rect64 r = square(120 * 64, 80 * 64, 200 * 64);
        fd::scale_axis(xs, r.x0, r.x1, fd::kBigTileSide, 0, fd::kPortraitW);
        fd::scale_axis(ys, r.y0, r.y1, fd::kBigTileSide, 0, fd::kPortraitH);
        fd::Buffer a = base;
        fd::scale_tile(a.surface(), small, xs, ys, fd::Scaler::Swar);
        check(a.pix == base.pix, "axes built for another source side draw nothing");
        fd::scale_axis(xs, r.x0, r.x1, fd::kBigTileSide, 0, 100);
        check(xs.taps.empty(), "an axis clipped wholly away from the tile has no taps");
    }

    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
