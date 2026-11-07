#include "png.h"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace fd {

namespace {

uint32_t crc_table[256];

void crc_init()
{
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++)
            c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}

uint32_t crc(const uint8_t* p, size_t n, uint32_t c = 0xFFFFFFFFu)
{
    for (size_t i = 0; i < n; i++)
        c = crc_table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c;
}

void be32(std::vector<uint8_t>& v, uint32_t x)
{
    v.push_back(x >> 24);
    v.push_back(x >> 16);
    v.push_back(x >> 8);
    v.push_back(x);
}

void chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data)
{
    be32(out, (uint32_t)data.size());
    size_t at = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    be32(out, crc(out.data() + at, out.size() - at) ^ 0xFFFFFFFFu);
}

}

template <class F, class L> bool write_png(const std::string& path, const SurfaceT<F, L>& s)
{
    crc_init();
    std::vector<uint8_t> raw;
    raw.reserve((size_t)s.h * (1 + s.w * 3));
    for (int y = 0; y < s.h; y++) {
        raw.push_back(0);
        for (int x = 0; x < s.w; x++) {
            uint32_t p = F::unpack(s.at(x, y));
            raw.push_back(p >> 16);
            raw.push_back(p >> 8);
            raw.push_back(p);
        }
    }
    std::vector<uint8_t> z{0x78, 0x01};
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw) {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    for (size_t off = 0; off < raw.size(); off += 65535) {
        size_t n = std::min<size_t>(65535, raw.size() - off);
        z.push_back(off + n == raw.size() ? 1 : 0);
        z.push_back(n & 0xFF);
        z.push_back(n >> 8);
        z.push_back(~n & 0xFF);
        z.push_back((~n >> 8) & 0xFF);
        z.insert(z.end(), raw.begin() + off, raw.begin() + off + n);
    }
    be32(z, (b << 16) | a);
    std::vector<uint8_t> out{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<uint8_t> ihdr;
    be32(ihdr, s.w);
    be32(ihdr, s.h);
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});
    FILE* f = fopen(path.c_str(), "wb");
    if (!f)
        return false;
    bool ok = fwrite(out.data(), 1, out.size(), f) == out.size();
    return fclose(f) == 0 && ok;
}

template bool write_png<X888, Rot>(const std::string&, const SurfaceT<X888, Rot>&);
template bool write_png<X888, Rows>(const std::string&, const SurfaceT<X888, Rows>&);
template bool write_png<R565, Rows>(const std::string&, const SurfaceT<R565, Rows>&);

}
