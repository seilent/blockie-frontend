#pragma once
#include <vector>

#include "../gfx/canvas.h"

namespace fd {

enum class Scaler { Channel, Swar };

struct ScaleTap {
    int i0 = 0, i1 = 0, w = 0, cov = 0;
};

struct ScaleAxis {
    int src = 0;
    int p0 = 0;
    std::vector<ScaleTap> taps;
};

void scale_axis(ScaleAxis& out, int e0, int e1, int src, int lo, int hi);
void scale_tile(const Surface& dst, const Image& img, const ScaleAxis& xs, const ScaleAxis& ys, Scaler k);

}
