#pragma once
#include <vector>

#include "../core/fx.h"
#include "canvas.h"

namespace fd {

struct WarpRow {
    int src = -1;
    fx step = kOne;
};

struct Projection {
    fx x = 0, y = 0;
    fx scale = kOne;
    fx angle = 0;
    int hinge = 0;
    int d = 1;
};

template <class F, class L> struct SpriteT {
    Rect r;
    std::vector<uint32_t> px;
};

using Sprite = SpriteT<LiveF, LiveL>;

template <class F> constexpr int kSpriteShift = std::is_same_v<F, X888> ? 24 : 16;

struct SwivelCol {
    int src = -1;
    fx w = kOne;
};

void turnstile_rows(fx angle_deg, int hinge_px, int d_px, WarpRow* rows);
template <class F = LiveF, class L = LiveL>
void warp(const SurfaceT<F, L>& src, const SurfaceT<F, L>& dst, const WarpRow* rows, uint32_t bg, int a256,
          bool motion = false, int eye = 0);
Rect projected_bounds(Rect src, const Projection& pr);
template <class F = LiveF, class L = LiveL>
void project(const SurfaceT<F, L>& src, Rect src_rect, const SurfaceT<F, L>& dst, Rect clip, const Projection& pr,
             uint32_t bg, int a256);
template <class F = LiveF, class L = LiveL>
void project(const SurfaceT<F, L>& src, const uint8_t* mask, Rect src_rect, const SurfaceT<F, L>& dst, Rect clip,
             const Projection& pr, uint32_t bg, int a256);
template <class F = LiveF, class L = LiveL>
void project_sprite(const SurfaceT<F, L>& src, Rect src_rect, const Projection& pr, uint32_t bg, SpriteT<F, L>& out);
template <class F = LiveF, class L = LiveL>
void project_sprite(const SurfaceT<F, L>& src, const uint8_t* mask, Rect src_rect, const Projection& pr, uint32_t bg,
                    SpriteT<F, L>& out);
template <class F = LiveF, class L = LiveL> void draw_sprite(CanvasT<F, L>& c, const SpriteT<F, L>& s, int a256);
void swivel_cols(fx angle_deg, int hinge_px, int d_px, SwivelCol* cols);
template <class F = LiveF, class L = LiveL>
void swivel(const SurfaceT<F, L>& page, const SurfaceT<F, L>& layer, const SurfaceT<F, L>& dst, const SwivelCol* cols,
            uint32_t bg, int base_a256, int a256, bool motion = false);
template <class F = LiveF, class L = LiveL>
void swivel(const SurfaceT<F, L>& page, const SurfaceT<F, L>& layer, const uint8_t* mask, const SurfaceT<F, L>& dst,
            const SwivelCol* cols, uint32_t bg, int base_a256, int a256, bool motion = false);

}
