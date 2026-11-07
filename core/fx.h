#pragma once
#include <cstdint>

#include "../ui/tokens.h"

namespace fd {

using fx = int32_t;

constexpr int kFxBits = 16;
constexpr fx kOne = 1 << kFxBits;
constexpr fx kHalf = 1 << (kFxBits - 1);

constexpr fx fx_int(int v) { return (fx)((uint32_t)v << kFxBits); }
constexpr fx fx_mul(fx a, fx b) { return (fx)(((int64_t)a * b) >> kFxBits); }
constexpr fx fx_div(fx a, fx b) { return (fx)(((int64_t)a * kOne) / b); }
constexpr int fx_floor(fx a) { return a >> kFxBits; }
constexpr int fx_round(fx a) { return (a + kHalf) >> kFxBits; }
constexpr fx fx_lerp(fx a, fx b, fx t) { return a + fx_mul(b - a, t); }
constexpr fx fx_clamp(fx v, fx lo, fx hi) { return v < lo ? lo : v > hi ? hi : v; }

inline thread_local fx tl_ppu = tok::u;

inline fx ppu() { return tl_ppu; }
inline fx to_px(fx u) { return fx_mul(u, tl_ppu); }
inline int px(fx u) { return fx_round(to_px(u)); }
inline int text_px(fx size_u)
{
    fx p = to_px(size_u);
    return fx_round(p < tok::minTextPx ? tok::minTextPx : p);
}
inline fx std_u(fx v) { return tl_ppu == tok::u ? v : fx_div(fx_int(fx_round(fx_mul(v, tok::u))), tl_ppu); }
inline fx to_std(fx v) { return (fx)((int64_t)v * tl_ppu / tok::u); }
constexpr fx std_to_px(fx u) { return fx_mul(u, tok::u); }
constexpr int std_px(fx u) { return fx_round(std_to_px(u)); }
constexpr int std_text_px(fx size_u)
{
    fx p = std_to_px(size_u);
    return fx_round(p < tok::minTextPx ? tok::minTextPx : p);
}
inline fx kept_u(fx v) { return tl_ppu == tok::u ? v : fx_div(fx_int(std_px(v)), tl_ppu); }
constexpr int alpha256(fx a) { return fx_round(fx_clamp(a, 0, kOne) * 256); }

}
