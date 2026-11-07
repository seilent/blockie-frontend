#pragma once
#include <cstdint>
#include <type_traits>
#include <vector>

#include "warp.h"

namespace fd {

enum class MipForm { Nearest, Bilinear };

constexpr fx kMipSwitch = 2 * kOne;
constexpr fx kMipSwitchSqrt2 = 92682;
constexpr int kMipBlockBits = 4;
constexpr bool kMipWide = kPortraitH * 64 > kOne;
constexpr int kMipBits = kMipWide ? 24 : kFxBits;
constexpr fx kMipMinScale = kMipWide ? kOne >> 6 : 1;
using MipAcc = std::conditional_t<kMipWide, int64_t, int32_t>;
template <class F> constexpr int kMipOpaque = std::is_same_v<F, X888> ? 256 : 32;

enum : uint8_t { kMipEmpty, kMipPartial, kMipFull };

struct MipBlocks {
    int w = 0, h = 0;
    std::vector<uint8_t> f;
};

template <class F> struct MipLevelT {
    using T = typename F::T;
    int ox = 0, oy = 0, w = 0, h = 0;
    Rect src, r;
    T k = 0, tint = 0;
    bool built = false, cover = false;
    std::vector<T> v;
    std::vector<uint8_t> m;
    MipBlocks blocks;
};

template <class F> struct MipViewT {
    using T = typename F::T;
    const T* v = nullptr;
    const uint8_t* m = nullptr;
    size_t stride = 0;
    int shift = 0, ox = 0, oy = 0;
    Rect r, src;
    T k = 0, tint = 0;
    const MipBlocks* blocks = nullptr;
};

template <class F> struct MipLayerT {
    MipViewT<F> l0, l1;
};

#ifdef FD_MIP_X888
constexpr bool kMipX888 = true;
#else
constexpr bool kMipX888 = false;
#endif
template <class F> using MipT = std::conditional_t<kMipX888, F, R565>;

using MipLevel = MipLevelT<MipT<LiveF>>;
using MipView = MipViewT<MipT<LiveF>>;
using MipLayer = MipLayerT<MipT<LiveF>>;

static_assert(!board::kMipPan || (std::is_same_v<LiveL, Rows> && std::is_same_v<MipT<LiveF>, LiveF>),
              "a mip pan board keeps its levels as rows in the live format");

template <class F> constexpr int mip_alpha(int a256) { return std::is_same_v<F, R565> ? alpha5(a256) : a256; }

struct MipCol {
    MipAcc a = 0, inc = 0;
    int t = 0, c = 0, fx = 0, bx0 = 0, bx1 = 0;
    bool live = false, in = false;
};

struct MipRun {
    int x = 0, n = 0, s = 0;
};

struct MipScratch {
    std::vector<MipCol> cols;
    std::vector<MipRun> runs;
    std::vector<int> idx;

    MipScratch();
};

struct MipDraw {
    Rect r;
    int level = -1;
};

void mip_alloc(MipLevelT<R565>& out, Rect r0, bool coverage);
void mip_drop(MipLevelT<R565>& out);
template <class F = LiveF, class L = LiveL>
void mip_build(const SurfaceT<F, L>& src, const uint8_t* mask, Rect r, uint32_t ground, MipLevelT<MipT<F>>& out);
template <class F = LiveF, class L = LiveL>
void mip_update(const SurfaceT<F, L>& src, const uint8_t* mask, Rect dirty, MipLevelT<MipT<F>>& out);
template <class F = LiveF, class L = LiveL>
void mip_blocks(const SurfaceT<F, L>& src, const uint8_t* mask, Rect r, uint32_t ground, Rect dirty, MipBlocks& out);
template <class F = LiveF, class L = LiveL>
MipViewT<MipT<F>> mip_view(const SurfaceT<F, L>& src, const uint8_t* mask, Rect r, uint32_t ground,
                           const MipBlocks* blocks = nullptr);
template <class F = LiveF, class L = LiveL>
MipViewT<MipT<F>> mip_view(const SurfaceT<F, L>& src, const uint8_t* mask, int x, int y, uint32_t ground);
MipViewT<R565> mip_view(const MipLevelT<R565>& l);
void mip_build(const uint8_t* cov, size_t stride, Rect r, uint32_t tint, MipLevelT<R565>& out);
#ifdef FD_MIP_X888
void mip_alloc(MipLevelT<X888>& out, Rect r0, bool coverage);
void mip_drop(MipLevelT<X888>& out);
MipViewT<X888> mip_view(const MipLevelT<X888>& l);
void mip_build(const uint8_t* cov, size_t stride, Rect r, uint32_t tint, MipLevelT<X888>& out);
#endif
void mip_blocks(const uint8_t* cov, size_t stride, Rect r, Rect dirty, MipBlocks& out);
template <class F = MipT<LiveF>>
MipViewT<F> mip_cover(const uint8_t* cov, size_t stride, Rect r, uint32_t tint, const MipBlocks* blocks = nullptr);
fx mip_step(Rect src, const Projection& pr);
int mip_pick(Rect src, const Projection& pr, fx sw);
template <class F = LiveF, class L = LiveL>
MipDraw mip_qp(const MipLayerT<MipT<F>>& l, const SurfaceT<F, L>& dst, const Projection& pr, int a, MipForm form,
               int level, MipScratch& sc, Rect clip = kScreen);
template <class F = LiveF, class L = LiveL> void mip_ground(const SurfaceT<F, L>& dst, Rect keep, uint32_t ground);
template <class F = LiveF, class L = LiveL>
int mip_menu(const MipLayerT<MipT<F>>& l, const SurfaceT<F, L>& dst, const Projection& pr, int a, MipForm form,
             int level, MipScratch& sc, Rect busy = kScreen, Rect clip = kScreen);
template <class F = LiveF, class L = LiveL>
void mip_pan(const MipLayerT<MipT<F>>& qp, const MipLayerT<MipT<F>>& menu, const SurfaceT<F, L>& dst,
             const Projection& pq, int qa, MipForm qf, int ql, const Projection& pm, int ma, MipForm mf, int ml,
             uint32_t ground, MipScratch& sc, const MipLayerT<MipT<F>>& ring = {}, int ra = 0);
template <class F = LiveF, class L = LiveL>
int mip_sprite(const MipLayerT<MipT<F>>& l, const Projection& pr, MipForm form, int level, MipScratch& sc,
               SpriteT<F, L>& out);

}
