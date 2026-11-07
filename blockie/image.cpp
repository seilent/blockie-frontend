#include "image.h"

#include <cstdio>

#include <spng.h>

namespace fd {

namespace {

constexpr uint32_t kMaxSide = 4096;
constexpr size_t kMaxChunk = 4u << 20;
constexpr size_t kMaxCache = 64u << 20;

}

bool load_png(const std::string& path, DecodedImage& out)
{
    out = DecodedImage{};
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f)
        return false;
    spng_ctx* ctx = spng_ctx_new(0);
    if (!ctx) {
        std::fclose(f);
        return false;
    }
    spng_set_image_limits(ctx, kMaxSide, kMaxSide);
    spng_set_chunk_limits(ctx, kMaxChunk, kMaxCache);
    bool ok = false;
    struct spng_ihdr ihdr;
    size_t need = 0;
    if (spng_set_png_file(ctx, f) == 0 && spng_get_ihdr(ctx, &ihdr) == 0 &&
        spng_decoded_image_size(ctx, SPNG_FMT_RGB8, &need) == 0 &&
        need == (size_t)ihdr.width * ihdr.height * 3) {
        std::vector<uint8_t> buf(need);
        if (spng_decode_image(ctx, buf.data(), need, SPNG_FMT_RGB8, 0) == 0) {
            out.w = (int)ihdr.width;
            out.h = (int)ihdr.height;
            out.rgb.resize((size_t)out.w * out.h);
            for (size_t i = 0; i < out.rgb.size(); i++)
                out.rgb[i] = ((uint32_t)buf[i * 3] << 16) | ((uint32_t)buf[i * 3 + 1] << 8) |
                             (uint32_t)buf[i * 3 + 2];
            ok = out.ok();
        }
    }
    spng_ctx_free(ctx);
    std::fclose(f);
    return ok;
}

}
