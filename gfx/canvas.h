#pragma once
#include <algorithm>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <vector>

#include "../board/board.h"
#include "../core/fx.h"
#include "pagealloc.h"
#include "pixel.h"

namespace fd {

constexpr int kPortraitW = board::kScreenW;
constexpr int kPortraitH = board::kScreenH;
constexpr int kMaxDim = std::max(kPortraitW, kPortraitH);
#ifdef FD_ROT
constexpr int kMemW = kPortraitH;
constexpr int kMemH = kPortraitW;
#endif

struct Rect {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;

    bool empty() const { return x0 >= x1 || y0 >= y1; }
    Rect unite(const Rect& o) const;
    Rect intersect(const Rect& o) const;
    Rect grow(int d) const { return {x0 - d, y0 - d, x1 + d, y1 + d}; }
};

constexpr Rect kScreen{0, 0, kPortraitW, kPortraitH};

struct Zoom {
    fx s = kOne;
    int x64 = 0, y64 = 0;

    bool on() const { return s != kOne; }
    int x(int v64) const { return x64 + (int)(((int64_t)(v64 - x64) * s) >> kFxBits); }
    int y(int v64) const { return y64 + (int)(((int64_t)(v64 - y64) * s) >> kFxBits); }
    int px_x(int v) const { return (x(v * 64) + 32) >> 6; }
    int px_y(int v) const { return (y(v * 64) + 32) >> 6; }
    Rect rect(Rect r) const { return on() ? Rect{px_x(r.x0), px_y(r.y0), px_x(r.x1), px_y(r.y1)} : r; }
};

struct Rect64 {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;

    static Rect64 of(Rect r, const Zoom& z) { return {z.x(r.x0 * 64), z.y(r.y0 * 64), z.x(r.x1 * 64), z.y(r.y1 * 64)}; }
    Rect outer() const { return {x0 >> 6, y0 >> 6, (x1 + 63) >> 6, (y1 + 63) >> 6}; }
};

template <class F> struct SurfaceT<F, Rot> {
    using T = typename F::T;
    T* px = nullptr;
    int stride = kPortraitH;
    int w = kPortraitW, h = kPortraitH;

    Rect rect() const { return {0, 0, w, h}; }
    T* mem_row(int r) const { return px + (size_t)r * stride; }
    Rect mem_rect(Rect p) const
    {
        p = p.intersect(rect());
        if (p.empty())
            return {};
        return {p.y0, w - p.x1, p.y1, w - p.x0};
    }
    size_t index(int x, int y) const { return (size_t)(w - 1 - x) * stride + y; }
    T& at(int x, int y) const { return px[index(x, y)]; }
};

template <class F> struct SurfaceT<F, Rows> {
    using T = typename F::T;
    T* px = nullptr;
    int stride = kPortraitW;
    int w = kPortraitW, h = kPortraitH;

    Rect rect() const { return {0, 0, w, h}; }
    T* row(int y) const { return px + (size_t)y * stride; }
    size_t index(int x, int y) const { return (size_t)y * stride + x; }
    T& at(int x, int y) const { return px[index(x, y)]; }
};

template <class F, class L> struct BufferT {
    using T = typename F::T;
    int w = kPortraitW, h = kPortraitH;
    std::vector<T> pix;

    BufferT() : pix((size_t)w * h) {}
    BufferT(int width, int height) : w(width), h(height), pix((size_t)width * height) {}
    SurfaceT<F, L> surface()
    {
        if constexpr (std::is_same_v<L, Rot>)
            return {pix.data(), h, w, h};
        else
            return {pix.data(), w, w, h};
    }
};

template <class F, class L> struct LayerT {
    BufferT<F, L> buf;
    std::vector<uint8_t> mask;

    LayerT() : mask(buf.pix.size()) {}
    LayerT(int width, int height) : buf(width, height), mask(buf.pix.size()) {}
    SurfaceT<F, L> surface() { return buf.surface(); }
};

template <class T> using ImageAlloc = std::conditional_t<board::kMem.ledger, PageAlloc<T>, std::allocator<T>>;

template <class F, class L> struct ImageT {
    using T = typename F::T;
    int w = 0, h = 0;
    std::vector<T, ImageAlloc<T>> mem;

    size_t index(int x, int y) const { return L::index(w, h, x, y); }
    T at(int x, int y) const { return mem[index(x, y)]; }
    static ImageT from_portrait(const uint32_t* rgb, int w, int h);
};

template <class F, class L> class CanvasT {
public:
    using T = typename F::T;
    using S = SurfaceT<F, L>;

    CanvasT() = default;
    explicit CanvasT(S s, uint8_t* mask = nullptr) : s_(s), mask_(mask), bound_(s.rect()), clip_(s.rect()) {}
    explicit CanvasT(LayerT<F, L>& l) : CanvasT(l.surface(), l.mask.data()) {}

    S surface() const { return s_; }
    uint8_t* mask() const { return mask_; }
    void set_clip(Rect r) { clip_ = r.intersect(bound_); }
    Rect clip() const { return clip_; }
    void set_bound(Rect r)
    {
        bound_ = r.intersect(s_.rect());
        clip_ = clip_.intersect(bound_);
    }
    Rect bound() const { return bound_; }

    void fill(Rect r, uint32_t rgb);
    void ground(Rect r, uint32_t rgb);
    void blend(Rect r, uint32_t rgb, int a256);
    void vgradient(Rect r, uint32_t base, uint32_t over, int a_top, int a_bottom, int y_top, int y_bottom);
    void blit(const ImageT<F, L>& img, int x, int y, int a256 = 256);
    void coverage(const uint8_t* cov, int w, int h, int x, int y, uint32_t rgb, int a256);
    void fill_aa(Rect64 r, uint32_t rgb, int a256);
    void frame_aa(Rect64 outer, Rect64 inner, uint32_t rgb, int a256);
    void blit_bilinear(const ImageT<F, L>& img, Rect64 r, int a256, bool keep = false);

private:
    void paint(Rect r, uint32_t rgb, uint8_t m);
    void cover(size_t i, int a256)
    {
        if (mask_)
            mask_[i] = cover8(mask_[i], a256);
    }

    S s_;
    uint8_t* mask_ = nullptr;
    Rect bound_ = kScreen;
    Rect clip_ = kScreen;
};

#ifdef FD_ROT
Rect mem_rect(Rect portrait);
#endif

}
