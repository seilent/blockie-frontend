#pragma once
#include <cstdint>
#include <memory>
#include <vector>

#include "../gfx/canvas.h"
#include "../gfx/pixel.h"

namespace fd {

struct AlphaRegion {
    Rect r;
    int a256 = 0;
};

struct PresentNote {
    uint32_t seq = 0;
    bool presented = false;
};

bool regions_visible(const std::vector<AlphaRegion>& regions);
uint8_t region_a8(int a256);
void write_argb(const Surface& src, const std::vector<AlphaRegion>& regions, uint32_t* dst, int stride);

class LayerPresenter {
public:
    virtual ~LayerPresenter() = default;
    virtual bool open() = 0;
    virtual Surface frame() = 0;
    virtual bool ready() const = 0;
    virtual uint32_t present(const std::vector<AlphaRegion>& regions) = 0;
    virtual int fd() const = 0;
    virtual bool pump(std::vector<PresentNote>& out) = 0;
};

std::unique_ptr<LayerPresenter> make_layer_presenter();

}
