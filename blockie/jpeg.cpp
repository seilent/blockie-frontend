#include "jpeg.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <turbojpeg.h>
#include <unistd.h>

namespace fd {

namespace {

constexpr uint8_t kPngMagic[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
constexpr uint8_t kJpegMagic[3] = {0xFF, 0xD8, 0xFF};

bool read_file(const std::string& path, std::vector<uint8_t>& out)
{
    out.clear();
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOCTTY);
    if (fd < 0)
        return false;
    struct stat sb;
    bool ok = fstat(fd, &sb) == 0 && S_ISREG(sb.st_mode) && sb.st_size > 0 && (size_t)sb.st_size <= kCoverMaxBytes;
    if (ok)
        out.resize((size_t)sb.st_size);
    size_t got = 0;
    while (ok && got < out.size()) {
        ssize_t r = read(fd, out.data() + got, out.size() - got);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            break;
        got += (size_t)r;
    }
    close(fd);
    out.resize(got);
    return ok && got > 0;
}

int64_t area(int w, int h, tjscalingfactor f) { return (int64_t)TJSCALED(w, f) * TJSCALED(h, f); }

}

JpegDecoder::JpegDecoder()
{
    tjhandle tj = tj3Init(TJINIT_DECOMPRESS);
    if (tj && (tj3Set(tj, TJPARAM_MAXPIXELS, (int)kJpegMaxPixels) != 0 ||
               tj3Set(tj, TJPARAM_MAXMEMORY, kJpegMaxMemoryMb) != 0 ||
               tj3Set(tj, TJPARAM_SCANLIMIT, kJpegScanLimit) != 0 || tj3Set(tj, TJPARAM_STOPONWARNING, 1) != 0)) {
        tj3Destroy(tj);
        tj = nullptr;
    }
    if (!tj) {
        std::fprintf(stderr, "blockie-frontend: turbojpeg did not start, jpeg covers are off\n");
        return;
    }
    tj_ = tj;
}

JpegDecoder::~JpegDecoder()
{
    if (tj_)
        tj3Destroy(tj_);
}

bool JpegDecoder::load(const std::vector<uint8_t>& buf, int cover_side, DecodedImage& out)
{
    out = DecodedImage{};
    if (!tj_ || buf.empty() || tj3DecompressHeader(tj_, buf.data(), buf.size()) != 0)
        return false;
    if (tj3Get(tj_, TJPARAM_LOSSLESS) != 0 || tj3Get(tj_, TJPARAM_PRECISION) != 8)
        return false;
    int w = tj3Get(tj_, TJPARAM_JPEGWIDTH), h = tj3Get(tj_, TJPARAM_JPEGHEIGHT);
    int cs = tj3Get(tj_, TJPARAM_COLORSPACE);
    if (w <= 0 || h <= 0 || w > kJpegMaxSide || h > kJpegMaxSide || (int64_t)w * h > kJpegMaxPixels)
        return false;
    if (cs == TJCS_CMYK || cs == TJCS_YCCK)
        return false;
    tjscalingfactor pick = TJUNSCALED;
    int count = 0;
    tjscalingfactor* sf = tj3GetScalingFactors(&count);
    for (int i = 0; sf && i < count; i++)
        if (std::min(TJSCALED(w, sf[i]), TJSCALED(h, sf[i])) >= cover_side && area(w, h, sf[i]) < area(w, h, pick))
            pick = sf[i];
    if (tj3SetScalingFactor(tj_, pick) != 0)
        return false;
    int sw = TJSCALED(w, pick), sh = TJSCALED(h, pick);
    std::vector<uint8_t> rgb((size_t)sw * sh * 3);
    if (tj3Decompress8(tj_, buf.data(), buf.size(), rgb.data(), sw * 3, TJPF_RGB) != 0)
        return false;
    out.w = sw;
    out.h = sh;
    out.rgb.resize((size_t)sw * sh);
    for (size_t i = 0; i < out.rgb.size(); i++)
        out.rgb[i] = ((uint32_t)rgb[i * 3] << 16) | ((uint32_t)rgb[i * 3 + 1] << 8) | (uint32_t)rgb[i * 3 + 2];
    return out.ok();
}

bool load_cover(JpegDecoder& jpeg, const std::string& path, int cover_side, DecodedImage& out)
{
    out = DecodedImage{};
    std::vector<uint8_t> buf;
    if (!read_file(path, buf))
        return false;
    if (buf.size() >= sizeof kPngMagic && std::memcmp(buf.data(), kPngMagic, sizeof kPngMagic) == 0)
        return load_png_buffer(buf, out);
    if (buf.size() >= sizeof kJpegMagic && std::memcmp(buf.data(), kJpegMagic, sizeof kJpegMagic) == 0)
        return jpeg.load(buf, cover_side, out);
    return false;
}

}
