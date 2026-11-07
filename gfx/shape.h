#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "bidi.h"
#include "font.h"

namespace fd {

constexpr size_t kLayoutEntries = 128;
constexpr size_t kLayoutBytes = 96 << 10;

struct TextLayout {
    struct G {
        uint16_t face = 0;
        uint8_t level = 0;
        uint32_t gid = 0, byte = 0;
        int32_t adv = 0, dx = 0, dy = 0;
    };
    std::string text;
    BidiPara para;
    std::vector<G> g;
    bool complete = true;

    size_t bytes() const;
};

struct GlyphRun {
    struct G {
        uint16_t face = 0;
        uint32_t gid = 0;
        int32_t x64 = 0, dx64 = 0, dy64 = 0;
        uint32_t byte = 0;
    };
    TextStyle st;
    std::string plain;
    bool is_plain = false;
    std::vector<G> g;
    int width64 = 0, up = 0, down = 0, ink_top64 = 0, ink_bottom64 = 0, ink_right64 = 0;
    bool ranged = false, complete = true;
};

struct TextPrefix {
    struct G {
        int x = 0, end = 0, pen = 0, track = 0;
        bool ranged = false;
    };
    std::vector<G> g;
    bool rtl = false;

    bool span64(const TextLayout& l, size_t b, size_t e, int& w) const;
};

std::vector<RangedFace> rtl_faces(const std::string& dir);

}
