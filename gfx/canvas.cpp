#include "canvas.h"

#include <algorithm>
#include <cstddef>

#include "../core/prof.h"
#include "pairs.h"

namespace fd {

Rect Rect::unite(const Rect& o) const
{
    if (empty())
        return o;
    if (o.empty())
        return *this;
    return {std::min(x0, o.x0), std::min(y0, o.y0), std::max(x1, o.x1), std::max(y1, o.y1)};
}

Rect Rect::intersect(const Rect& o) const
{
    Rect r{std::max(x0, o.x0), std::max(y0, o.y0), std::min(x1, o.x1), std::min(y1, o.y1)};
    if (r.empty())
        return {};
    return r;
}

#ifdef FD_ROT
Rect mem_rect(Rect p) { return SurfaceT<X888, Rot>{}.mem_rect(p); }
#endif

template <class F, class L> ImageT<F, L> ImageT<F, L>::from_portrait(const uint32_t* rgb, int w, int h)
{
    ImageT img;
    img.w = w;
    img.h = h;
    img.mem.resize((size_t)w * h);
    if constexpr (std::is_same_v<L, Rot>) {
        for (int k = 0; k < w; k++)
            for (int j = 0; j < h; j++)
                img.mem[(size_t)k * h + j] = F::pack(rgb[(size_t)j * w + (w - 1 - k)]);
    } else {
        for (size_t i = 0; i < img.mem.size(); i++)
            img.mem[i] = F::pack(rgb[i]);
    }
    return img;
}

template <class F, class L> void CanvasT<F, L>::paint(Rect r, uint32_t rgb, uint8_t m)
{
    T v = F::pack(rgb);
    if constexpr (std::is_same_v<L, Rot>) {
        Rect mr = s_.mem_rect(r.intersect(clip_));
        for (int row = mr.y0; row < mr.y1; row++) {
            T* d = s_.mem_row(row);
            fill_px(d + mr.x0, v, mr.x1 - mr.x0);
            if (mask_)
                std::fill_n(mask_ + (size_t)row * s_.stride + mr.x0, mr.x1 - mr.x0, m);
        }
    } else {
        Rect p = r.intersect(clip_);
        for (int y = p.y0; y < p.y1; y++) {
            fill_px(s_.row(y) + p.x0, v, p.x1 - p.x0);
            if (mask_)
                std::fill_n(mask_ + s_.index(p.x0, y), p.x1 - p.x0, m);
        }
    }
}

template <class F, class L> void CanvasT<F, L>::fill(Rect r, uint32_t rgb) { paint(r, rgb, 255); }

template <class F, class L> void CanvasT<F, L>::ground(Rect r, uint32_t rgb)
{
    prof::Scope ps(prof::Ground);
    paint(r, rgb, 0);
}

template <class F, class L> void CanvasT<F, L>::blend(Rect r, uint32_t rgb, int a)
{
    if (a <= 0)
        return;
    if (a >= 256) {
        fill(r, rgb);
        return;
    }
    T v = F::pack(rgb);
    if constexpr (std::is_same_v<L, Rot>) {
        Rect m = s_.mem_rect(r.intersect(clip_));
        for (int row = m.y0; row < m.y1; row++) {
            T* d = s_.mem_row(row);
            for (int c = m.x0; c < m.x1; c++) {
                d[c] = F::blend(d[c], v, a);
                cover((size_t)row * s_.stride + c, a);
            }
        }
    } else {
        Rect p = r.intersect(clip_);
        for (int y = p.y0; y < p.y1; y++) {
            T* d = s_.row(y);
            for (int x = p.x0; x < p.x1; x++) {
                d[x] = F::blend(d[x], v, a);
                cover(s_.index(x, y), a);
            }
        }
    }
}

template <class F, class L>
void CanvasT<F, L>::vgradient(Rect r, uint32_t base, uint32_t over, int a_top, int a_bottom, int y_top, int y_bottom)
{
    if (keyed_) {
        ground(r, base);
        return;
    }
    Rect p = r.intersect(clip_);
    if (p.empty())
        return;
    int span = std::max(1, y_bottom - y_top);
    auto color = [&](int y) {
        int t = std::clamp(y - y_top, 0, span);
        int a = a_top + (a_bottom - a_top) * t / span;
        return F::pack(blend_px(base, over, a));
    };
    if constexpr (std::is_same_v<L, Rot>) {
        Rect m = s_.mem_rect(p);
        T stack[kMaxDim];
        std::vector<T> heap;
        T* line = stack;
        if (m.x1 > kMaxDim) {
            heap.resize((size_t)m.x1);
            line = heap.data();
        }
        for (int c = m.x0; c < m.x1; c++)
            line[c] = color(c);
        for (int row = m.y0; row < m.y1; row++) {
            copy_px(s_.mem_row(row) + m.x0, line + m.x0, m.x1 - m.x0);
            if (mask_)
                std::fill_n(mask_ + (size_t)row * s_.stride + m.x0, m.x1 - m.x0, (uint8_t)255);
        }
    } else {
        for (int y = p.y0; y < p.y1; y++) {
            fill_px(s_.row(y) + p.x0, color(y), p.x1 - p.x0);
            if (mask_)
                std::fill_n(mask_ + s_.index(p.x0, y), p.x1 - p.x0, (uint8_t)255);
        }
    }
}

template <class F, class L> void CanvasT<F, L>::blit(const ImageT<F, L>& img, int x, int y, int a)
{
    Rect pr = Rect{x, y, x + img.w, y + img.h}.intersect(clip_);
    if (pr.empty() || a <= 0)
        return;
    if constexpr (std::is_same_v<L, Rot>) {
        Rect m = s_.mem_rect(pr);
        for (int row = m.y0; row < m.y1; row++) {
            int pxc = s_.w - 1 - row;
            int k = img.w - 1 - (pxc - x);
            const T* s = img.mem.data() + (size_t)k * img.h + (m.x0 - y);
            T* d = s_.mem_row(row) + m.x0;
            int n = m.x1 - m.x0;
            if (a >= 256)
                copy_px(d, s, n);
            else
                for (int i = 0; i < n; i++)
                    d[i] = F::blend(d[i], s[i], a);
            if (mask_)
                for (int i = 0; i < n; i++)
                    cover((size_t)row * s_.stride + m.x0 + i, a);
        }
    } else {
        int n = pr.x1 - pr.x0;
        for (int py = pr.y0; py < pr.y1; py++) {
            const T* s = img.mem.data() + (size_t)(py - y) * img.w + (pr.x0 - x);
            T* d = s_.row(py) + pr.x0;
            if (a >= 256)
                copy_px(d, s, n);
            else
                for (int i = 0; i < n; i++)
                    d[i] = F::blend(d[i], s[i], a);
            if (mask_)
                for (int i = 0; i < n; i++)
                    cover(s_.index(pr.x0 + i, py), a);
        }
    }
}

template <class F, class L>
void CanvasT<F, L>::coverage(const uint8_t* cov, int w, int h, int x, int y, uint32_t rgb, int a)
{
    Rect pr = Rect{x, y, x + w, y + h}.intersect(clip_);
    if (pr.empty() || a <= 0)
        return;
    T v = F::pack(rgb);
    auto put = [&](T& d, size_t i, int c) {
        int aa = (c + 1) * a >> 8;
        d = aa >= 256 ? v : F::blend(d, v, aa);
        cover(i, aa);
    };
    if constexpr (std::is_same_v<L, Rot>) {
        Rect m = s_.mem_rect(pr);
        for (int row = m.y0; row < m.y1; row++) {
            int pxc = s_.w - 1 - row;
            int k = w - 1 - (pxc - x);
            const uint8_t* s = cov + (size_t)k * h + (m.x0 - y);
            T* d = s_.mem_row(row) + m.x0;
            int n = m.x1 - m.x0;
            for (int i = 0; i < n; i++)
                if (s[i])
                    put(d[i], (size_t)row * s_.stride + m.x0 + i, s[i]);
        }
    } else {
        int n = pr.x1 - pr.x0;
        for (int py = pr.y0; py < pr.y1; py++) {
            const uint8_t* s = cov + (size_t)(py - y) * w + (pr.x0 - x);
            T* d = s_.row(py) + pr.x0;
            for (int i = 0; i < n; i++)
                if (s[i])
                    put(d[i], s_.index(pr.x0 + i, py), s[i]);
        }
    }
}

template <class F, class L>
void CanvasT<F, L>::spans(const uint8_t* rows, int h, int x, int y, uint32_t rgb, int a)
{
    if (a <= 0 || y >= clip_.y1 || y + h <= clip_.y0)
        return;
    T v = F::pack(rgb);
    const ptrdiff_t step = (ptrdiff_t)s_.index(1, 0) - (ptrdiff_t)s_.index(0, 0);
    const uint8_t* run = rows + (size_t)h * 4;
    for (int r = 0; r < h; r++) {
        const uint8_t* hd = rows + (size_t)r * 4;
        int py = y + r, c0 = x + (hd[0] | hd[1] << 8), n = hd[2] | hd[3] << 8;
        int x0 = std::max(c0, clip_.x0), x1 = std::min(c0 + n, clip_.x1);
        if (py >= clip_.y0 && py < clip_.y1 && x0 < x1) {
            ptrdiff_t i = (ptrdiff_t)s_.index(x0, py);
            for (int px = x0; px < x1; px++, i += step) {
                int c = run[px - c0];
                if (!c)
                    continue;
                int aa = (c + 1) * a >> 8;
                s_.px[(size_t)i] = aa >= 256 ? v : F::blend(s_.px[(size_t)i], v, aa);
                cover((size_t)i, aa);
            }
        }
        run += n;
    }
}

template <class F, class L>
void CanvasT<F, L>::blit_cov(const ImageT<F, L>& img, const uint8_t* cov, int x, int y, int a)
{
    Rect pr = Rect{x, y, x + img.w, y + img.h}.intersect(clip_);
    if (pr.empty() || a <= 0)
        return;
    for (int py = pr.y0; py < pr.y1; py++)
        for (int px = pr.x0; px < pr.x1; px++) {
            size_t k = img.index(px - x, py - y);
            int c = cov[k];
            if (!c)
                continue;
            size_t i = s_.index(px, py);
            int aa = (c + 1) * a >> 8;
            s_.px[i] = aa >= 256 ? img.mem[k] : F::blend(s_.px[i], img.mem[k], aa);
            cover(i, aa);
        }
}

template <class F, class L>
void tile_into(const ImageT<F, L>& src, int w, int h, int angle, ImageT<F, L>& out, std::vector<uint8_t>& cov)
{
    constexpr int64_t kR = 46341;
    const int64_t half = (int64_t)w << 15, full = (int64_t)w << 16;
    if (angle)
        h = w;
    int bw = angle ? (int)(((int64_t)w * 92682 + 65535) >> 16) : w, bh = angle ? bw : h;
    out.w = bw;
    out.h = bh;
    out.mem.assign((size_t)bw * bh, F::pack(0));
    cov.assign(angle ? (size_t)bw * bh : 0, 0);
    int64_t dx = kR, dy = angle > 0 ? kR : -kR, center = (int64_t)bw << 15;
    auto edge = [&](int64_t e) -> int64_t {
        if (e >= kR)
            return 65536;
        if (e <= -kR)
            return 0;
        return e >= 0 ? 65536 - ((kR - e) * (kR - e) >> 16) : (kR + e) * (kR + e) >> 16;
    };
    const int64_t kx = ((int64_t)src.w << 16) / w, ky = ((int64_t)src.h << 16) / h;
    auto tap = [&](int64_t t, int n, int64_t k, int& i0, int& i1, int& f) {
        int64_t s = std::clamp<int64_t>((t * k >> 16) - 32768, 0, (int64_t)(n - 1) << 16);
        i0 = (int)(s >> 16);
        i1 = std::min(i0 + 1, n - 1);
        f = (int)(s >> 8 & 255);
    };
    for (int y = 0; y < bh; y++)
        for (int x = 0; x < bw; x++) {
            int64_t tx = ((int64_t)x << 16) + 32768, ty = ((int64_t)y << 16) + 32768;
            size_t k = out.index(x, y);
            if (angle) {
                int64_t px = tx - center, py = ty - center;
                tx = ((px * dx + py * dy) >> 16) + half;
                ty = ((py * dx - px * dy) >> 16) + half;
                int64_t c = edge(std::min(tx, full - tx)) * edge(std::min(ty, full - ty)) >> 16;
                cov[k] = (uint8_t)std::min<int64_t>(255, (c * 255 + 32768) >> 16);
                if (!cov[k])
                    continue;
            }
            int x0, x1, wx, y0, y1, wy;
            tap(tx, src.w, kx, x0, x1, wx);
            tap(ty, src.h, ky, y0, y1, wy);
            uint32_t p00 = F::unpack(src.at(x0, y0)), p10 = F::unpack(src.at(x1, y0)), p01 = F::unpack(src.at(x0, y1)),
                     p11 = F::unpack(src.at(x1, y1)), o = 0;
            for (int sh = 0; sh < 24; sh += 8) {
                int c00 = p00 >> sh & 255, c10 = p10 >> sh & 255, c01 = p01 >> sh & 255, c11 = p11 >> sh & 255;
                int top = c00 * (256 - wx) + c10 * wx, bot = c01 * (256 - wx) + c11 * wx;
                o |= (uint32_t)((top * (256 - wy) + bot * wy + 32768) >> 16) << sh;
            }
            out.mem[k] = F::pack(o);
        }
}

template void tile_into(const ImageT<X888, Rot>&, int, int, int, ImageT<X888, Rot>&, std::vector<uint8_t>&);
template void tile_into(const ImageT<X888, Rows>&, int, int, int, ImageT<X888, Rows>&, std::vector<uint8_t>&);
template void tile_into(const ImageT<R565, Rows>&, int, int, int, ImageT<R565, Rows>&, std::vector<uint8_t>&);

template <class F, class L>
void CanvasT<F, L>::coverage_turn(const uint8_t* cov, int w, int h, int x, int y, uint32_t rgb, int a)
{
    Rect pr = Rect{x, y, x + h, y + w}.intersect(clip_);
    if (pr.empty() || a <= 0)
        return;
    T v = F::pack(rgb);
    for (int py = pr.y0; py < pr.y1; py++)
        for (int px = pr.x0; px < pr.x1; px++) {
            int c = cov[L::index(w, h, w - 1 - (py - y), px - x)];
            if (!c)
                continue;
            size_t i = s_.index(px, py);
            int aa = (c + 1) * a >> 8;
            s_.px[i] = aa >= 256 ? v : F::blend(s_.px[i], v, aa);
            cover(i, aa);
        }
}

namespace {

int span64(int p, int a, int b) { return std::max(0, std::min(p + 64, b) - std::max(p, a)); }

template <class T> struct ArtKeep {
    const T* src = nullptr;
    int w = 0, h = 0, ox = 0, oy = 0, dw = 0, dh = 0;
    uint32_t probe = 0;
    int ow = 0, oh = 0;
    std::vector<T> rgb;
    std::vector<uint8_t> cov;
    int64_t used = 0;
};

template <class F, class L> struct Keeps {
    static ArtKeep<typename F::T> keep[2];
    static int64_t tick;
};

template <class F, class L> ArtKeep<typename F::T> Keeps<F, L>::keep[2];
template <class F, class L> int64_t Keeps<F, L>::tick = 0;

template <class F, class L> uint32_t art_probe(const ImageT<F, L>& img)
{
    size_t n = img.mem.size();
    return (uint32_t)img.mem[0] ^ (uint32_t)img.mem[n / 2] * 3u ^ (uint32_t)img.mem[n - 1] * 7u ^
           (uint32_t)img.mem[n / 3] * 13u;
}

struct Tap {
    int i0, i1, w, cov;
};

void taps(int n, int r0, int r1, int src, std::vector<Tap>& out)
{
    out.resize((size_t)n);
    int64_t d = r1 - r0;
    for (int k = 0; k < n; k++) {
        int p = k * 64;
        int64_t v = std::clamp<int64_t>(((int64_t)(p + 32 - r0) * src * 256) / d - 128, 0, (int64_t)(src - 1) * 256);
        Tap& t = out[(size_t)k];
        t.i0 = (int)(v >> 8);
        t.w = (int)(v & 255);
        t.i1 = std::min(t.i0 + 1, src - 1);
        t.cov = span64(p, r0, r1);
    }
}

template <class F, class L>
void bilinear(const ImageT<F, L>& img, Rect64 r, Rect out, std::vector<typename F::T>& rgb, std::vector<uint8_t>& cov)
{
    static std::vector<Tap> cols, rows;
    int ow = out.x1 - out.x0, oh = out.y1 - out.y0;
    rgb.assign((size_t)ow * oh, 0);
    cov.assign((size_t)ow * oh, 0);
    taps(ow, r.x0, r.x1, img.w, cols);
    taps(oh, r.y0, r.y1, img.h, rows);
    auto at = [&](int sx, int sy) { return F::unpack(img.at(sx, sy)); };
    for (int j = 0; j < oh; j++) {
        const Tap& ty = rows[(size_t)j];
        if (!ty.cov)
            continue;
        for (int i = 0; i < ow; i++) {
            const Tap& tx = cols[(size_t)i];
            if (!tx.cov)
                continue;
            uint32_t p00 = at(tx.i0, ty.i0), p10 = at(tx.i1, ty.i0), p01 = at(tx.i0, ty.i1), p11 = at(tx.i1, ty.i1);
            uint32_t o = 0;
            for (int sh = 0; sh < 24; sh += 8) {
                int c00 = p00 >> sh & 255, c10 = p10 >> sh & 255, c01 = p01 >> sh & 255, c11 = p11 >> sh & 255;
                int top = c00 * (256 - tx.w) + c10 * tx.w, bot = c01 * (256 - tx.w) + c11 * tx.w;
                o |= (uint32_t)((top * (256 - ty.w) + bot * ty.w + 32768) >> 16) << sh;
            }
            size_t k = (size_t)j * ow + i;
            rgb[k] = F::pack(o);
            cov[k] = (uint8_t)std::min(255, tx.cov * ty.cov * 255 / 4096);
        }
    }
}

}

template <class F, class L> void CanvasT<F, L>::fill_aa(Rect64 r, uint32_t rgb, int a)
{
    prof::Scope ps(prof::Fill);
    if (a <= 0 || r.x1 <= r.x0 || r.y1 <= r.y0)
        return;
    T v = F::pack(rgb);
    Rect o = r.outer().intersect(clip_);
    if (o.empty())
        return;
    constexpr bool rot = std::is_same_v<L, Rot>;
    int l0 = rot ? o.x0 : o.y0, l1 = rot ? o.x1 : o.y1, k0 = rot ? o.y0 : o.x0, k1 = rot ? o.y1 : o.x1;
    int e0 = rot ? r.x0 : r.y0, e1 = rot ? r.x1 : r.y1, q0 = rot ? r.y0 : r.x0, q1 = rot ? r.y1 : r.x1;
    int f0 = std::clamp((q0 + 63) >> 6, k0, k1), f1 = std::clamp(q1 >> 6, f0, k1);
    auto at = [&](int l, int k) { return rot ? s_.index(l, k) : s_.index(k, l); };
    auto b = [&] {
        if constexpr (std::is_same_v<F, R565>)
            return Blend565(v, std::min(a, 255));
        else
            return 0;
    }();
    for (int l = l0; l < l1; l++) {
        int cl = span64(l * 64, e0, e1);
        int s0 = cl == 64 ? f0 : k1, s1 = cl == 64 ? f1 : k1;
        auto edge = [&](int k) {
            int c = span64(k * 64, q0, q1) * cl;
            if (c <= 0)
                return;
            size_t i = at(l, k);
            T& d = s_.px[i];
            int aa = a * c >> 12;
            d = aa >= 256 ? v : F::blend(d, v, aa);
            cover(i, aa);
        };
        for (int k = k0; k < s0; k++)
            edge(k);
        if (s1 > s0) {
            size_t i = at(l, s0);
            T* d = s_.px + i;
            int n = s1 - s0;
            if (a >= 256)
                fill_px(d, v, n);
            else if constexpr (std::is_same_v<F, R565>)
                b.span(d, d, n);
            else
                for (int j = 0; j < n; j++)
                    d[j] = F::blend(d[j], v, a);
            if (mask_)
                for (int j = 0; j < n; j++)
                    mask_[i + j] = cover8(mask_[i + j], a);
        }
        for (int k = s1; k < k1; k++)
            edge(k);
    }
}

template <class F, class L> void CanvasT<F, L>::frame_aa(Rect64 r, Rect64 in, uint32_t rgb, int a)
{
    if (a <= 0 || r.x1 <= r.x0 || r.y1 <= r.y0)
        return;
    T v = F::pack(rgb);
    Rect o = r.outer().intersect(clip_);
    for (int y = o.y0; y < o.y1; y++) {
        int cy = span64(y * 64, r.y0, r.y1), iy = span64(y * 64, in.y0, in.y1);
        for (int x = o.x0; x < o.x1; x++) {
            int c = span64(x * 64, r.x0, r.x1) * cy - span64(x * 64, in.x0, in.x1) * iy;
            if (c <= 0)
                continue;
            size_t i = s_.index(x, y);
            T& d = s_.px[i];
            int aa = a * c >> 12;
            d = aa >= 256 ? v : F::blend(d, v, aa);
            cover(i, aa);
        }
    }
}

template <class F, class L> void CanvasT<F, L>::blit_bilinear(const ImageT<F, L>& img, Rect64 r, int a, bool keep)
{
    if (a <= 0 || img.w <= 0 || img.h <= 0 || r.x1 <= r.x0 || r.y1 <= r.y0)
        return;
    Rect out = r.outer();
    int ox = out.x0, oy = out.y0;
    Rect64 local{r.x0 - ox * 64, r.y0 - oy * 64, r.x1 - ox * 64, r.y1 - oy * 64};
    Rect lo{0, 0, out.x1 - ox, out.y1 - oy};
    using K = Keeps<F, L>;
    static ArtKeep<T> tmp;
    ArtKeep<T>* k = &tmp;
    if (keep) {
        uint32_t probe = art_probe(img);
        ArtKeep<T>* hit = nullptr;
        for (ArtKeep<T>& e : K::keep)
            if (e.src == img.mem.data() && e.w == img.w && e.h == img.h && e.ox == local.x0 && e.oy == local.y0 &&
                e.dw == local.x1 - local.x0 && e.dh == local.y1 - local.y0 && e.probe == probe)
                hit = &e;
        if (!hit) {
            hit = K::keep[0].used <= K::keep[1].used ? &K::keep[0] : &K::keep[1];
            hit->src = img.mem.data();
            hit->w = img.w;
            hit->h = img.h;
            hit->ox = local.x0;
            hit->oy = local.y0;
            hit->dw = local.x1 - local.x0;
            hit->dh = local.y1 - local.y0;
            hit->probe = probe;
            hit->ow = lo.x1;
            hit->oh = lo.y1;
            bilinear(img, local, lo, hit->rgb, hit->cov);
        }
        hit->used = ++K::tick;
        k = hit;
    } else {
        k->ow = lo.x1;
        k->oh = lo.y1;
        bilinear(img, local, lo, k->rgb, k->cov);
    }
    Rect d = Rect{ox, oy, ox + k->ow, oy + k->oh}.intersect(clip_);
    for (int y = d.y0; y < d.y1; y++)
        for (int x = d.x0; x < d.x1; x++) {
            size_t i = (size_t)(y - oy) * k->ow + (x - ox);
            int c = k->cov[i];
            if (!c)
                continue;
            size_t si = s_.index(x, y);
            T& o = s_.px[si];
            int aa = (c + 1) * a >> 8;
            o = aa >= 256 ? k->rgb[i] : F::blend(o, k->rgb[i], aa);
            cover(si, aa);
        }
}

template struct ImageT<X888, Rot>;
template struct ImageT<X888, Rows>;
template struct ImageT<R565, Rows>;
template class CanvasT<X888, Rot>;
template class CanvasT<X888, Rows>;
template class CanvasT<R565, Rows>;

}
