#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace fd {

struct DecodedImage {
    int w = 0;
    int h = 0;
    std::vector<uint32_t> rgb;

    bool ok() const { return w > 0 && h > 0 && rgb.size() == (size_t)w * h; }
};

bool load_png(const std::string& path, DecodedImage& out);

}
