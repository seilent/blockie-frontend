#include "dumpz.h"

#include <cstdio>
#include <vector>

#include <zlib.h>

namespace fd {

bool dump_z(const std::string& path, const void* data, size_t n)
{
    uLongf len = compressBound((uLong)n);
    std::vector<Bytef> buf(len);
    if (compress2(buf.data(), &len, (const Bytef*)data, (uLong)n, 1) != Z_OK)
        return false;
    FILE* f = fopen(path.c_str(), "wb");
    if (!f)
        return false;
    bool ok = fwrite(buf.data(), 1, len, f) == len;
    return fclose(f) == 0 && ok;
}

bool load_z(const std::string& path, void* data, size_t n)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f)
        return false;
    std::vector<Bytef> buf;
    Bytef chunk[65536];
    size_t got;
    while ((got = fread(chunk, 1, sizeof chunk, f)) > 0)
        buf.insert(buf.end(), chunk, chunk + got);
    bool ok = !ferror(f);
    if (fclose(f) != 0 || !ok)
        return false;
    z_stream z{};
    if (inflateInit(&z) != Z_OK)
        return false;
    z.next_in = buf.data();
    z.avail_in = (uInt)buf.size();
    z.next_out = (Bytef*)data;
    z.avail_out = (uInt)n;
    bool done = inflate(&z, Z_FINISH) == Z_STREAM_END && z.total_out == n;
    inflateEnd(&z);
    return done;
}

}
