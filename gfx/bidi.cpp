#include "bidi.h"

#include <algorithm>
#include <fribidi.h>
#include <type_traits>

#include "../lib/util.h"

namespace fd {

static_assert(std::is_same_v<FriBidiChar, uint32_t> && std::is_same_v<FriBidiCharType, uint32_t> &&
              std::is_same_v<FriBidiLevel, int8_t>);

uint32_t bidi_first_strong(std::string_view s, Dir& d)
{
    const char* p = s.data();
    const char* e = p + s.size();
    int iso = 0;
    while (p < e) {
        uint32_t cp = utf8_next(p, e);
        FriBidiCharType t = fribidi_get_bidi_type(cp);
        if (t == FRIBIDI_TYPE_PDI) {
            if (iso > 0)
                iso--;
        } else if (FRIBIDI_IS_ISOLATE(t)) {
            iso++;
        } else if (iso == 0 && FRIBIDI_IS_LETTER(t)) {
            d = FRIBIDI_IS_RTL(t) ? Dir::Rtl : Dir::Ltr;
            return cp;
        }
    }
    d = Dir::Auto;
    return 0;
}

uint32_t bidi_first_rtl(std::string_view s)
{
    const char* p = s.data();
    const char* e = p + s.size();
    while (p < e) {
        uint32_t cp = utf8_next(p, e);
        FriBidiCharType t = fribidi_get_bidi_type(cp);
        if (FRIBIDI_IS_RTL(t) && !bidi_ignorable(cp))
            return cp;
    }
    return 0;
}

bool bidi_ignorable(uint32_t cp)
{
    return cp == 0x061C || cp == 0x034F || cp == 0xFEFF || (cp >= 0x200B && cp <= 0x200F) ||
           (cp >= 0x202A && cp <= 0x202E) || (cp >= 0x2060 && cp <= 0x2064) || (cp >= 0x2066 && cp <= 0x2069) ||
           (cp >= 0xFE00 && cp <= 0xFE0F) || (cp >= 0xE0100 && cp <= 0xE01EF);
}

bool bidi_plain(std::string_view s, Dir base)
{
    if (base == Dir::Rtl)
        return false;
    const char* p = s.data();
    const char* e = p + s.size();
    while (p < e) {
        if ((unsigned char)*p < 0x80) {
            p++;
            continue;
        }
        uint32_t cp = utf8_next(p, e);
        FriBidiCharType t = fribidi_get_bidi_type(cp);
        if (FRIBIDI_IS_RTL(t) || t == FRIBIDI_TYPE_AN || bidi_ignorable(cp))
            return false;
    }
    return true;
}

std::vector<std::string_view> bidi_pieces(std::string_view s)
{
    std::vector<std::string_view> out;
    while (s.size() > kBidiParaMax) {
        const char* p = s.data();
        const char* e = p + s.size();
        size_t at = 0, edge = 0, space = 0;
        while (at <= kBidiParaMax) {
            if (at > 0)
                edge = at;
            if (at > 0 && *p == ' ')
                space = at;
            utf8_next(p, e);
            at = p - s.data();
        }
        size_t cut = space ? space : edge;
        out.push_back(s.substr(0, cut));
        s.remove_prefix(space ? cut + 1 : cut);
    }
    if (out.empty() || !s.empty())
        out.push_back(s);
    return out;
}

BidiPara bidi_para(std::string_view s, Dir base)
{
    BidiPara p;
    const char* q = s.data();
    const char* e = q + s.size();
    p.cp.reserve(s.size());
    p.at.reserve(s.size() + 1);
    while (q < e) {
        p.at.push_back((uint32_t)(q - s.data()));
        p.cp.push_back(utf8_next(q, e));
    }
    p.at.push_back((uint32_t)s.size());
    FriBidiStrIndex n = (FriBidiStrIndex)p.cp.size();
    p.type.resize(n);
    p.level.resize(n);
    std::vector<FriBidiBracketType> br(n);
    fribidi_get_bidi_types(p.cp.data(), n, p.type.data());
    fribidi_get_bracket_types(p.cp.data(), n, p.type.data(), br.data());
    FriBidiParType dir = base == Dir::Rtl ? FRIBIDI_PAR_RTL : base == Dir::Ltr ? FRIBIDI_PAR_LTR : FRIBIDI_PAR_ON;
    if (!fribidi_get_par_embedding_levels_ex(p.type.data(), br.data(), n, &dir, p.level.data()))
        std::fill(p.level.begin(), p.level.end(), FRIBIDI_DIR_TO_LEVEL(dir));
    p.base = (uint8_t)FRIBIDI_DIR_TO_LEVEL(dir);
    fribidi_shape_mirroring(p.level.data(), n, p.cp.data());
    return p;
}

std::vector<BidiRun> bidi_runs(const BidiPara& p, size_t b, size_t e)
{
    std::vector<BidiRun> out;
    if (p.cp.empty())
        return out;
    auto last = p.at.end() - 1;
    size_t i0 = std::lower_bound(p.at.begin(), last, b) - p.at.begin();
    size_t i1 = std::lower_bound(p.at.begin(), last, e) - p.at.begin();
    if (i1 <= i0)
        return out;
    FriBidiStrIndex n = (FriBidiStrIndex)(i1 - i0);
    std::vector<FriBidiLevel> lv(p.level.begin() + i0, p.level.begin() + i1);
    std::vector<FriBidiStrIndex> map(n);
    for (FriBidiStrIndex i = 0; i < n; i++)
        map[i] = i;
    FriBidiParType dir = p.base & 1 ? FRIBIDI_PAR_RTL : FRIBIDI_PAR_LTR;
    if (!fribidi_reorder_line(0, p.type.data() + i0, n, 0, dir, lv.data(), nullptr, map.data())) {
        out.push_back({p.at[i0], p.at[i1], p.base});
        return out;
    }
    for (FriBidiStrIndex v = 0; v < n;) {
        FriBidiLevel l = lv[map[v]];
        int step = l & 1 ? -1 : 1;
        FriBidiStrIndex w = v + 1;
        while (w < n && lv[map[w]] == l && map[w] == map[w - 1] + step)
            w++;
        size_t lo = i0 + std::min(map[v], map[w - 1]), hi = i0 + std::max(map[v], map[w - 1]) + 1;
        out.push_back({p.at[lo], p.at[hi], (uint8_t)l});
        v = w;
    }
    return out;
}

}
