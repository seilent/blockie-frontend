#pragma once
#include <cstdint>
#include <memory>
#include <vector>

#include "../core/fx.h"
#include "../gfx/canvas.h"
#include "../gfx/mip.h"
#include "../gfx/xform.h"

namespace fd {

constexpr Rect kNoClip{-(1 << 28), -(1 << 28), 1 << 28, 1 << 28};
constexpr int kFrameDepth = 4;

enum class OpKind : uint8_t { Quad, Fill, Strip, Group, End, Paint };
enum class Sample : uint8_t { Nearest, Linear };
enum class Blend : uint8_t { Copy, Over };

struct Op {
    OpKind kind = OpKind::Fill;
    uint16_t id = 0;
    uint16_t layer = 0;
    uint32_t version = 0;
    Rect src;
    Rect bounds;
    Xform xf;
    int a = 256;
    Rect clip = kScreen;
    Sample sample = Sample::Nearest;
    Blend blend = Blend::Over;
    uint32_t rgb = 0;
    bool frozen = false;
    int level = 0;

    bool same(const Op& o) const;
};

constexpr uint32_t kFrameFull = 1, kFramePan = 2, kFrameFront = 4;

struct Frame {
    static constexpr int kOps = 48;
    uint32_t ground = 0;
    Op ops[kOps];
    int n = 0;
    bool over_cap = false;
    bool over_depth = false;
    int open = 0;
    Rect damage;
    const char* label = "";
    int phase = 0, over = 0;
    uint32_t flags = 0;

    void reset(uint32_t g, const char* l, int ph, int ov, uint32_t fl)
    {
        ground = g;
        n = 0;
        over_cap = false;
        over_depth = false;
        open = 0;
        damage = {};
        label = l;
        phase = ph;
        over = ov;
        flags = fl;
    }
    Op* add(OpKind k, uint16_t id)
    {
        if (n >= kOps) {
            over_cap = true;
            return nullptr;
        }
        if (k == OpKind::Group && open++ >= kFrameDepth)
            over_depth = true;
        if (k == OpKind::End && open > 0)
            open--;
        Op& o = ops[n++];
        o = Op{};
        o.kind = k;
        o.id = id;
        return &o;
    }
};

enum class LayerFmt : uint8_t { Opaque, Masked, Mip };

struct LayerRef {
    Surface s;
    const uint8_t* mask = nullptr;
    uint32_t key = 0;
    LayerFmt fmt = LayerFmt::Opaque;
    int ox = 0, oy = 0;
    const MipLayer* mip = nullptr;
};

struct FrameSources {
    virtual ~FrameSources() = default;
    virtual bool layer(uint16_t, LayerRef&) const { return false; }
    virtual bool strip(uint16_t, int, uint32_t&, int&) const { return false; }
    virtual bool damage(uint16_t, uint32_t, uint32_t, Rect&) const { return false; }
    virtual bool paint(const Op&, Canvas&) const { return false; }
};

struct FrameScratch {
    struct Level {
        std::vector<Pix> px;
        std::vector<uint8_t> m;
    };
    Level lv[kFrameDepth];
    std::vector<WarpRow> rows;
    std::unique_ptr<MipScratch> mip;

    size_t bytes() const;
    void release();
};

Rect op_bounds(const Op& o);
Rect frame_damage(const Frame& prev, const Frame& next, const FrameSources& src);
bool frame_draw(const Frame& f, Canvas& c, const FrameSources& src, FrameScratch& sc);

}
