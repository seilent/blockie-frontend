#pragma once
#include "../core/fx.h"

#include "canvas.h"
#include "warp.h"

namespace fd {

template <class F = LiveF, class L = LiveL>
void scrim(const SurfaceT<F, L>& page, const SurfaceT<F, L>& dst, uint32_t color, int a256, bool motion = false);
template <class F = LiveF, class L = LiveL>
void scrim(const SurfaceT<F, L>& page, const SurfaceT<F, L>& dst, Rect portrait, uint32_t color, int a256,
           bool motion = false);
template <class F = LiveF, class L = LiveL>
void blend_shifted(const SurfaceT<F, L>& layer, const SurfaceT<F, L>& dst, Rect portrait, int dy, int a256,
                   bool motion = false);
template <class F = LiveF, class L = LiveL>
void scrim_shifted(const SurfaceT<F, L>& page, const SurfaceT<F, L>& layer, const SurfaceT<F, L>& dst, uint32_t color,
                   int scrim_a256, Rect portrait, int dy, int a256, bool motion = false);
template <class F = LiveF, class L = LiveL>
void scrim_shifted_rows(const SurfaceT<F, L>& page, const SurfaceT<F, L>& layer, const SurfaceT<F, L>& dst,
                        uint32_t color, int scrim_a256, Rect portrait, int dy, int a256, int r0, int r1);
template <class F = LiveF, class L = LiveL>
void scrim_coverage_rows(const SurfaceT<F, L>& page, const SurfaceT<F, L>& layer, const SurfaceT<F, L>& dst,
                         uint32_t scrim_color, int scrim_a256, Rect portrait, fx scale, int cx, int cy, uint32_t color,
                         int a256, int r0, int r1);
template <class F = LiveF, class L = LiveL>
void coverage_scaled(const SurfaceT<F, L>& layer, const SurfaceT<F, L>& dst, Rect portrait, fx scale, int cx, int cy,
                     uint32_t color, int a256, bool motion = false);
template <class F = LiveF, class L = LiveL>
void coverage_scaled(const SurfaceT<F, L>& layer, const uint8_t* mask, const SurfaceT<F, L>& dst, Rect portrait,
                     fx scale, int cx, int cy, uint32_t color, int a256, bool motion = false);
template <class F = LiveF, class L = LiveL>
void blend_scaled(const SurfaceT<F, L>& layer, const SurfaceT<F, L>& dst, fx scale, int cx, int cy, uint32_t bg,
                  int a256, bool motion = false);
template <class F = LiveF, class L = LiveL>
void blend_scaled(const SurfaceT<F, L>& layer, const uint8_t* mask, const SurfaceT<F, L>& dst, fx scale, int cx,
                  int cy, uint32_t ground, int a256, bool motion = false);
template <class F = LiveF, class L = LiveL>
void blend_scaled_rect(const SurfaceT<F, L>& layer, const uint8_t* mask, Rect src, const SurfaceT<F, L>& dst, Rect clip,
                       fx scale, fx tx, fx ty, uint32_t ground, int a256, bool motion = false);
template <class F = LiveF, class L = LiveL>
void scrim_scaled(const SurfaceT<F, L>& page, const SurfaceT<F, L>& layer, const SurfaceT<F, L>& dst, uint32_t color,
                  int scrim_a256, fx scale, int cx, int cy, uint32_t bg, int a256, bool motion = false);
template <class F = LiveF, class L = LiveL>
void scrim_scaled(const SurfaceT<F, L>& page, const SurfaceT<F, L>& layer, const uint8_t* mask,
                  const SurfaceT<F, L>& dst, uint32_t color, int scrim_a256, fx scale, int cx, int cy, uint32_t ground,
                  int a256, bool motion = false);
template <class F = LiveF, class L = LiveL>
void blend_scaled_premul(const SurfaceT<F, L>& layer, const uint8_t* alpha, const SurfaceT<F, L>& dst, fx scale, int cx,
                         int cy, int a256);
template <class F = LiveF, class L = LiveL>
void mix_scaled_premul(const SurfaceT<F, L>& under, const SurfaceT<F, L>& over, int mix_a256,
                       const SurfaceT<F, L>& layer, const uint8_t* alpha, const SurfaceT<F, L>& dst, fx scale, int cx,
                       int cy, int a256, bool motion = false);
template <class F = LiveF, class L = LiveL>
void precompose_premul(const SurfaceT<F, L>& under, const SurfaceT<F, L>& layer, const uint8_t* alpha,
                       const SurfaceT<F, L>& dst, Rect band = kScreen);
template <class F = LiveF, class L = LiveL>
void mix_scaled(const SurfaceT<F, L>& page, const SurfaceT<F, L>& q, const SurfaceT<F, L>& dst, int a256, fx scale,
                int cx, int cy, bool motion = false);
template <class F = LiveF, class L = LiveL>
void mix(const SurfaceT<F, L>& under, const SurfaceT<F, L>& over, const SurfaceT<F, L>& dst, int a256,
         bool motion = false);
template <class F = LiveF, class L = LiveL>
void blend_band(const SurfaceT<F, L>& layer, const SurfaceT<F, L>& dst, Rect band, int dx, int a256);

}
