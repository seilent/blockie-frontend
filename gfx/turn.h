#pragma once
#include <cstdint>
#include <vector>

struct stbtt_fontinfo;

namespace fd {

bool turn_glyph(const stbtt_fontinfo* info, int px, int gid, int angle, int& w, int& h, int& xoff, int& yoff,
                std::vector<uint8_t>& rows);

}
