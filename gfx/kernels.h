#pragma once
#include <cstddef>
#include <cstdint>

#include "../core/fx.h"
#include "canvas.h"
#include "warp.h"

namespace fd::kern {

void fill_span(uint32_t* d, uint32_t v, int n);
void blend_span(uint32_t* d, uint32_t v, int a256, int n);
void mix_span(uint32_t* d, const uint32_t* s, int a256, int n);

template <class L> void fill(const SurfaceT<X888, L>& p, Rect portrait, uint32_t v);
template <class L> void blend(const SurfaceT<X888, L>& p, Rect portrait, uint32_t v, int a256);
template <class L>
void project(const SurfaceT<X888, L>& src, Rect src_rect, const SurfaceT<X888, L>& dst, Rect out, const Projection& pr,
             uint32_t bg, int a256);

}
