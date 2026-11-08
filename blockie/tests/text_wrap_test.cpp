#include <cstdio>
#include <string>
#include <vector>

#include "../text_wrap.h"

using namespace fd;

namespace {

int g_fail = 0;
int g_total = 0;

void check(bool ok, const char* what)
{
    g_total++;
    if (ok) {
        std::printf("PASS %s\n", what);
    } else {
        std::printf("FAIL %s\n", what);
        g_fail++;
    }
}

int chars(const std::string& s) { return (int)s.size(); }

bool ends_with(const std::string& s, const std::string& suf)
{
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

bool valid_utf8(const std::string& s)
{
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = (unsigned char)s[i];
        int n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        if (n == 0 || i + n > s.size())
            return false;
        for (int k = 1; k < n; k++)
            if (((unsigned char)s[i + k] & 0xC0) != 0x80)
                return false;
        i += n;
    }
    return true;
}

}

int main()
{
    auto m = [](const std::string& s) { return chars(s); };

    std::vector<std::string> one = wrap_lines("Synth Game", 20, 4, m);
    check(one.size() == 1, "short title is one line");
    check(one.size() == 1 && one[0] == "Synth Game", "short title kept verbatim");
    check(one.size() == 1 && !ends_with(one[0], "..."), "short title has no ellipsis");

    std::vector<std::string> three = wrap_lines("Alpha Bravo Charlie Delta", 11, 4, m);
    check(three.size() == 3, "wraps to three lines within the width");
    check(three.size() == 3 && three[0] == "Alpha Bravo", "first line packs two words up to the width");
    check(three.size() == 3 && three[2] == "Delta", "last word on its own line");
    check(three.size() == 3 && !ends_with(three[2], "..."), "within max lines has no ellipsis");

    std::vector<std::string> many = wrap_lines("Aa Bb Cc Dd Ee Ff Gg Hh Ii Jj", 5, 4, m);
    check((int)many.size() == 4, "over-limit title is clamped to max lines");
    check(many.size() == 4 && ends_with(many.back(), "..."), "clamped last line ends with ellipsis");
    check(many.size() == 4 && chars(many.back()) <= 5, "ellipsised last line fits the width");
    check(many.size() == 4 && many[0] == "Aa Bb" && many.back() == "Gg...", "clamp keeps the first lines intact");

    std::vector<std::string> longword = wrap_lines("Supercalifragilistic", 5, 4, m);
    check(longword.size() == 1, "an unbreakable word stays one line");
    check(longword.size() == 1 && !ends_with(longword[0], "..."), "single long line is not ellipsised");

    check(wrap_lines("", 10, 4, m).empty(), "empty title yields no lines");

    std::vector<std::string> utf = wrap_lines("caf\xC3\xA9 zz", 7, 1, m);
    check(utf.size() == 1, "accented title clamps to one line");
    check(utf.size() == 1 && ends_with(utf[0], "..."), "accented clamped line ends with ellipsis");
    check(utf.size() == 1 && valid_utf8(utf[0]), "ellipsis trim does not split a utf8 code point");

    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
