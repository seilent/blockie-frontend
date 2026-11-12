#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "image.h"

namespace fd {

constexpr int kJpegMaxSide = 4096;
constexpr int64_t kJpegMaxPixels = 16777216;
constexpr int kJpegMaxMemoryMb = 64;
constexpr int kJpegScanLimit = 500;
constexpr size_t kCoverMaxBytes = 64u << 20;

class JpegDecoder {
public:
    JpegDecoder();
    ~JpegDecoder();
    JpegDecoder(const JpegDecoder&) = delete;
    JpegDecoder& operator=(const JpegDecoder&) = delete;

    bool load(const std::vector<uint8_t>& buf, int cover_side, DecodedImage& out);

private:
    void* tj_ = nullptr;
};

bool load_cover(JpegDecoder& jpeg, const std::string& path, int cover_side, DecodedImage& out);

}
