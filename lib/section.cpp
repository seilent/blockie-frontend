#include "section.h"

#include <vector>

#include "kana.h"
#include "util.h"

namespace fd {

namespace {

const char* kLetters[kSectionCount] = {"#", "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m", "n", "o",
    "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z", "\xE3\x81\x82", "\xE3\x81\x8B", "\xE3\x81\x95",
    "\xE3\x81\x9F", "\xE3\x81\xAA", "\xE3\x81\xAF", "\xE3\x81\xBE", "\xE3\x82\x84", "\xE3\x82\x89", "\xE3\x82\x8F",
    "\xE6\xBC\xA2"};

constexpr uint32_t kRowStarts[10] = {0x3041, 0x304B, 0x3055, 0x305F, 0x306A, 0x306F, 0x307E, 0x3083, 0x3089, 0x308E};
constexpr uint32_t kSmallKatakanaExt[16] = {0x304F, 0x3057, 0x3059, 0x3068, 0x306C, 0x306F, 0x3072, 0x3075, 0x3078,
    0x307B, 0x3080, 0x3089, 0x308A, 0x308B, 0x308C, 0x308D};
constexpr uint16_t kHalfKana[59] = {0x30FB, 0x30F2, 0x30A1, 0x30A3, 0x30A5, 0x30A7, 0x30A9, 0x30E3, 0x30E5, 0x30E7,
    0x30C3, 0x30FC, 0x30A2, 0x30A4, 0x30A6, 0x30A8, 0x30AA, 0x30AB, 0x30AD, 0x30AF, 0x30B1, 0x30B3, 0x30B5, 0x30B7,
    0x30B9, 0x30BB, 0x30BD, 0x30BF, 0x30C1, 0x30C4, 0x30C6, 0x30C8, 0x30CA, 0x30CB, 0x30CC, 0x30CD, 0x30CE, 0x30CF,
    0x30D2, 0x30D5, 0x30D8, 0x30DB, 0x30DE, 0x30DF, 0x30E0, 0x30E1, 0x30E2, 0x30E4, 0x30E6, 0x30E8, 0x30E9, 0x30EA,
    0x30EB, 0x30EC, 0x30ED, 0x30EF, 0x30F3, 0x3099, 0x309A};

constexpr int kHashRank = 1, kLatinRank = 2, kKanaRank = 28, kKanjiRank = 38;

bool kana_mark(uint32_t c) { return (c >= 0x3099 && c <= 0x309E) || (c >= 0x30FB && c <= 0x30FE); }
bool hiragana(uint32_t c) { return c >= 0x3041 && c <= 0x3096; }

uint32_t base_hiragana(uint32_t c)
{
    if (c <= 0x304A)
        return (c - 0x3041) % 2 == 0 ? c + 1 : c;
    if (c <= 0x3062)
        return (c - 0x304B) % 2 == 1 ? c - 1 : c;
    if (c <= 0x3065)
        return 0x3064;
    if (c <= 0x3069)
        return (c - 0x3066) % 2 == 1 ? c - 1 : c;
    if (c <= 0x306E)
        return c;
    if (c <= 0x307D)
        return 0x306F + (c - 0x306F) / 3 * 3;
    if (c <= 0x3082)
        return c;
    if (c <= 0x3088)
        return (c - 0x3083) % 2 == 0 ? c + 1 : c;
    if (c == 0x308E)
        return 0x308F;
    if (c == 0x3094)
        return 0x3046;
    if (c == 0x3095)
        return 0x304B;
    if (c == 0x3096)
        return 0x3051;
    return c;
}

int kana_row(uint32_t base)
{
    for (int i = 9; i > 0; i--)
        if (base >= kRowStarts[i])
            return i;
    return 0;
}

std::vector<uint32_t> decode(std::string_view s)
{
    std::vector<uint32_t> out;
    const char* p = s.data();
    const char* end = p + s.size();
    while (p < end)
        out.push_back(utf8_next(p, end));
    return out;
}

std::string encode(const std::vector<uint32_t>& v, size_t from)
{
    std::string out;
    for (size_t i = from; i < v.size(); i++)
        utf8_put(out, v[i]);
    return out;
}

}

uint32_t lower(uint32_t c)
{
    if (c >= 'A' && c <= 'Z')
        return c + 32;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7)
        return c + 32;
    if (c >= 0x391 && c <= 0x3A9 && c != 0x3A2)
        return c + 32;
    if (c >= 0x410 && c <= 0x42F)
        return c + 32;
    if (c >= 0x400 && c <= 0x40F)
        return c + 80;
    return c;
}

uint32_t nfkc(uint32_t c)
{
    if (c >= 0xFF01 && c <= 0xFF5E)
        return c - 0xFF01 + 0x21;
    if (c >= 0xFF65 && c <= 0xFF9F)
        return kHalfKana[c - 0xFF65];
    if (c == 0x3000)
        return ' ';
    return c;
}

bool han(uint32_t c)
{
    return (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0x3400 && c <= 0x4DBF) || (c >= 0xF900 && c <= 0xFAFF) ||
           (c >= 0x20000 && c <= 0x3FFFF) || c == 0x3005 || c == 0x3007;
}

uint32_t to_hiragana(uint32_t c)
{
    if (c >= 0x30A1 && c <= 0x30F6)
        return c - 0x60;
    if (c >= 0x30F7 && c <= 0x30FA)
        return 0x308F + (c - 0x30F7);
    if (c >= 0x31F0 && c <= 0x31FF)
        return kSmallKatakanaExt[c - 0x31F0];
    return c;
}

std::string fold_lower(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    const char* p = s.data();
    const char* end = p + s.size();
    while (p < end)
        utf8_put(out, lower(utf8_next(p, end)));
    return out;
}

uint64_t artist_key(std::string_view name) { return hash64(fold_lower(trim(name))); }

const char* section_letter(int i) { return i >= 0 && i < kSectionCount ? kLetters[i] : kLetters[0]; }

SectionKey section_key(std::string_view title, bool ignore_the)
{
    std::string key = fold_lower(trim(title));
    if (ignore_the && key.compare(0, 4, "the ") == 0)
        key = trim(std::string_view(key).substr(4));
    unsigned char first = key.empty() ? 0 : (unsigned char)key[0];
    if (first >= 'a' && first <= 'z')
        return {1 + (first - 'a'), std::string(1, (char)(kLatinRank + (first - 'a'))) + key};
    if (first < 0x80)
        return {0, std::string(1, (char)kHashRank) + key};
    std::vector<uint32_t> n = decode(key);
    for (uint32_t& c : n)
        c = lower(nfkc(c));
    size_t i = 0;
    while (i < n.size() && kana_mark(n[i]))
        i++;
    if (i < n.size()) {
        uint32_t c = n[i];
        if (i == 0 && c >= 'a' && c <= 'z')
            return {1 + (int)(c - 'a'), std::string(1, (char)(kLatinRank + (c - 'a'))) + encode(n, 0)};
        uint32_t h = to_hiragana(c);
        if (hiragana(h)) {
            int row = kana_row(base_hiragana(h));
            std::string folded, hira;
            for (size_t k = i; k < n.size(); k++) {
                uint32_t u = n[k];
                uint32_t hu = to_hiragana(u);
                if (!kana_mark(u))
                    utf8_put(folded, hiragana(hu) ? base_hiragana(hu) : hu);
                utf8_put(hira, hu);
            }
            return {27 + row, std::string(1, (char)(kKanaRank + row)) + folded + '\x01' + hira};
        }
        if (han(c))
            return {37, std::string(1, (char)kKanjiRank) + encode(n, i)};
    }
    return {0, std::string(1, (char)kHashRank) + key};
}

}
