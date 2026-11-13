#pragma once
#include <cstdint>

#include "../gfx/canvas.h"
#include "../gfx/pixel.h"

namespace fd {

void ink_clear(const Surface& layer, Rect area);
void ink_compose(const Surface& layer, const Surface& dst, uint32_t ink, Rect area);
void ink_add(const Surface& layer, const Surface& more, int keep256, int more256, Rect area);

class InkLayer {
public:
    InkLayer() : canvas_(buf_.surface()) {}
    InkLayer(const InkLayer&) = delete;
    InkLayer& operator=(const InkLayer&) = delete;

    Canvas& begin(Rect area);
    void compose(const Surface& dst, uint32_t ink);
    Surface surface() { return buf_.surface(); }

private:
    Buffer buf_;
    Canvas canvas_;
    Rect area_{};
};

}
