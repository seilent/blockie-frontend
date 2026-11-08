#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <unistd.h>

#include "../../core/fx.h"
#include "../../ui/tokens.h"
#include "../image.h"
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

static const unsigned char kFixturePng[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x03, 0x08, 0x02, 0x00, 0x00, 0x00, 0x3b, 0x96, 0x39,
    0x91, 0x00, 0x00, 0x00, 0x2c, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x05, 0xc1, 0x41, 0x01, 0x00,
    0x30, 0x0c, 0x02, 0xb1, 0x53, 0x52, 0x25, 0x88, 0xe0, 0x5d, 0x25, 0x28, 0xd9, 0x1b, 0x11, 0x15,
    0xb8, 0x04, 0x40, 0x4c, 0xd0, 0xb1, 0xe0, 0x91, 0x15, 0xef, 0x39, 0x50, 0xa9, 0x9b, 0xe6, 0xfa,
    0x3e, 0xa4, 0x4b, 0x0b, 0x7d, 0x6a, 0x0f, 0xd4, 0xd3, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e,
    0x44, 0xae, 0x42, 0x60, 0x82,
};

static const uint32_t kExpected[] = {
    0x000000, 0x3c001e, 0x78003c, 0xb4005a,
    0x00501e, 0x3c503c, 0x78505a, 0xb45078,
    0x00a03c, 0x3ca05a, 0x78a078, 0xb4a096,
};

std::string write_tmp(const void* data, size_t n)
{
    char tmpl[] = "/tmp/blockie-img-XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0)
        return {};
    std::string path = tmpl;
    if (n && write(fd, data, n) != (ssize_t)n) {
        close(fd);
        unlink(path.c_str());
        return {};
    }
    close(fd);
    return path;
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

bool tile_matches_bilinear(int sw, int sh, int a256, int k)
{
    fd::DecodedImage d;
    d.w = sw;
    d.h = sh;
    d.rgb = noise(sw, sh, 0x2545f491u);
    fd::Image full = fd::Image::from_portrait(d.rgb.data(), sw, sh);
    fd::Image tile = fd::cover_tile(d, fd::kTileSide);
    fd::Rect tr = fd::grid_tile_rect(k, fd::kGridCols);
    fd::Buffer ref;
    fd::Buffer got;
    std::vector<uint32_t> ground = noise(ref.w, ref.h, 0x9e3779b9u);
    for (size_t i = 0; i < ref.pix.size(); i++)
        ref.pix[i] = got.pix[i] = fd::LiveF::pack(ground[i]);
    fd::Canvas rc(ref.surface());
    rc.set_clip(tr);
    rc.blit_bilinear(full, fd::Rect64::of(fd::fit_rect(sw, sh, tr.x0, tr.y0, fd::kTileSide), fd::Zoom{}), a256, true);
    fd::Canvas gc(got.surface());
    gc.blit(tile, tr.x0 + (fd::kTileSide - tile.w) / 2, tr.y0 + (fd::kTileSide - tile.h) / 2, a256);
    return tile.w == fd::kTileSide && tile.h == fd::kTileSide && ref.pix == got.pix;
}

}

int main()
{
    std::string png = write_tmp(kFixturePng, sizeof kFixturePng);
    check(!png.empty(), "wrote fixture png to a temp file");

    fd::DecodedImage img;
    bool decoded = fd::load_png(png, img);
    check(decoded, "load_png decodes the fixture");
    check(img.ok(), "decoded image passes ok()");
    check(img.w == 4 && img.h == 3, "decoded dimensions are 4x3");

    bool pixels_match = img.rgb.size() == 12;
    for (size_t i = 0; i < img.rgb.size() && i < 12; i++)
        if ((img.rgb[i] & 0xffffffu) != kExpected[i])
            pixels_match = false;
    check(pixels_match, "every decoded pixel matches the known fixture");

    if (!png.empty())
        unlink(png.c_str());

    fd::DecodedImage none;
    check(!fd::load_png("/tmp/blockie-img-does-not-exist-12345", none), "missing file returns false");
    check(none.w == 0 && none.rgb.empty(), "failed decode leaves an empty image");

    std::string garbage = write_tmp("not a png at all, just text", 27);
    check(!garbage.empty(), "wrote garbage temp file");
    fd::DecodedImage bad;
    check(!fd::load_png(garbage, bad), "non-png bytes return false");
    if (!garbage.empty())
        unlink(garbage.c_str());

    std::string truncated = write_tmp(kFixturePng, sizeof kFixturePng / 2);
    fd::DecodedImage trunc;
    check(!fd::load_png(truncated, trunc), "truncated png returns false");
    if (!truncated.empty())
        unlink(truncated.c_str());

    int pane = fd::alpha256(fd::tok::motion_quickplayAlpha);
    check(tile_matches_bilinear(512, 448, 256, 3), "a 512x448 cover tile equals blit_bilinear at full alpha");
    check(tile_matches_bilinear(512, 448, pane, 3), "a 512x448 cover tile equals blit_bilinear at the pane alpha");
    check(tile_matches_bilinear(300, 600, 256, 4), "a 300x600 cover tile equals blit_bilinear at full alpha");
    check(tile_matches_bilinear(300, 600, pane, 4), "a 300x600 cover tile equals blit_bilinear at the pane alpha");
    fd::Rect odd = fd::fit_rect(640, 480, 0, 0, fd::kTileSide);
    check(odd.x0 == -25 && odd.x1 - odd.x0 == 205 && odd.y0 == 0 && odd.y1 == fd::kTileSide,
          "a 640x480 cover is 205 wide at x offset -25");
    check(tile_matches_bilinear(640, 480, 256, 7), "a 640x480 cover tile equals blit_bilinear at full alpha");
    check(tile_matches_bilinear(640, 480, pane, 7), "a 640x480 cover tile equals blit_bilinear at the pane alpha");
    fd::DecodedImage empty;
    check(fd::cover_tile(empty, fd::kTileSide).w == 0, "an empty decode gives an empty tile");

    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
