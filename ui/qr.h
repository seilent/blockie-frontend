#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "../gfx/canvas.h"

namespace fd {

constexpr int kQrQuiet = 4;

struct QrGrid {
    int version = 0, size = 0;
    std::vector<uint8_t> dark;
    bool at(int x, int y) const { return x >= 0 && y >= 0 && x < size && y < size && dark[(size_t)(y * size + x)]; }
};

bool qr_encode(const std::string& text, QrGrid& out);
int qr_module_px(const QrGrid& q, int box);
Rect qr_draw(Canvas& c, const QrGrid& q, Rect box, uint32_t ink, uint32_t paper);

}
