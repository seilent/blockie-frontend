#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <unistd.h>

#include "../image.h"

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

    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
