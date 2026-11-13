#include "image.h"

#include <algorithm>
#include <cstdio>

#include <spng.h>

#include "tile_grid.h"

namespace fd {

namespace {

constexpr uint32_t kMaxSide = 4096;
constexpr size_t kMaxChunk = 4u << 20;
constexpr size_t kMaxCache = 64u << 20;

struct Tap {
    int i0, i1, w;
};

void taps(int k0, int n, int r0, int r1, int src, std::vector<Tap>& out)
{
    out.resize((size_t)n);
    int64_t d = r1 - r0;
    for (int k = 0; k < n; k++) {
        int p = (k0 + k) * 64;
        int64_t v = std::clamp<int64_t>(((int64_t)(p + 32 - r0) * src * 256) / d - 128, 0, (int64_t)(src - 1) * 256);
        Tap& t = out[(size_t)k];
        t.i0 = (int)(v >> 8);
        t.w = (int)(v & 255);
        t.i1 = std::min(t.i0 + 1, src - 1);
    }
}

}

Image cover_tile(const DecodedImage& d, int side)
{
    Image img;
    if (!d.ok() || side <= 0)
        return img;
    Rect fr = fit_rect(d.w, d.h, 0, 0, side);
    Rect cr = fr.intersect({0, 0, side, side});
    if (cr.empty())
        return img;
    int fw = fr.x1 - fr.x0, fh = fr.y1 - fr.y0;
    img.w = cr.x1 - cr.x0;
    img.h = cr.y1 - cr.y0;
    std::vector<Tap> cols, rows;
    taps(cr.x0 - fr.x0, img.w, 0, fw * 64, d.w, cols);
    taps(cr.y0 - fr.y0, img.h, 0, fh * 64, d.h, rows);
    img.mem.resize((size_t)img.w * img.h);
    auto at = [&](int sx, int sy) { return LiveF::unpack(LiveF::pack(d.rgb[(size_t)sy * d.w + sx])); };
    for (int j = 0; j < img.h; j++) {
        const Tap& ty = rows[(size_t)j];
        for (int i = 0; i < img.w; i++) {
            const Tap& tx = cols[(size_t)i];
            uint32_t p00 = at(tx.i0, ty.i0), p10 = at(tx.i1, ty.i0), p01 = at(tx.i0, ty.i1), p11 = at(tx.i1, ty.i1);
            uint32_t o = 0;
            for (int sh = 0; sh < 24; sh += 8) {
                int c00 = p00 >> sh & 255, c10 = p10 >> sh & 255, c01 = p01 >> sh & 255, c11 = p11 >> sh & 255;
                int top = c00 * (256 - tx.w) + c10 * tx.w, bot = c01 * (256 - tx.w) + c11 * tx.w;
                o |= (uint32_t)((top * (256 - ty.w) + bot * ty.w + 32768) >> 16) << sh;
            }
            img.mem[img.index(i, j)] = LiveF::pack(o);
        }
    }
    return img;
}

namespace {

spng_ctx* png_ctx()
{
    spng_ctx* ctx = spng_ctx_new(0);
    if (ctx) {
        spng_set_image_limits(ctx, kMaxSide, kMaxSide);
        spng_set_chunk_limits(ctx, kMaxChunk, kMaxCache);
    }
    return ctx;
}

bool decode_png(spng_ctx* ctx, bool source, DecodedImage& out)
{
    bool ok = false;
    struct spng_ihdr ihdr;
    size_t need = 0;
    if (source && spng_get_ihdr(ctx, &ihdr) == 0 &&
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
    return ok;
}

}

bool load_png(const std::string& path, DecodedImage& out)
{
    out = DecodedImage{};
    FILE* f = std::fopen(path.c_str(), "rbe");
    if (!f)
        return false;
    spng_ctx* ctx = png_ctx();
    if (!ctx) {
        std::fclose(f);
        return false;
    }
    bool ok = decode_png(ctx, spng_set_png_file(ctx, f) == 0, out);
    spng_ctx_free(ctx);
    std::fclose(f);
    return ok;
}

bool load_png_buffer(const std::vector<uint8_t>& buf, DecodedImage& out)
{
    out = DecodedImage{};
    spng_ctx* ctx = png_ctx();
    if (!ctx)
        return false;
    bool ok = decode_png(ctx, spng_set_png_buffer(ctx, buf.data(), buf.size()) == 0, out);
    spng_ctx_free(ctx);
    return ok;
}

}
