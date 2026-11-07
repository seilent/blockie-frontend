#include "accent.h"

#include <cstring>

#include "roles.h"
#include "tokens.h"

namespace fd {

namespace {

constexpr int kStep = (int)(tok::accent_hueStep >> 16);
constexpr int kCount = (int)(tok::accent_hueCount >> 16);

constexpr int first_hue_token()
{
    for (int i = 0; i < tok::kTokenCount; i++) {
        const char* a = tok::kTokens[i].name;
        const char* b = "accent.h0";
        int k = 0;
        while (a[k] && a[k] == b[k])
            k++;
        if (!a[k] && !b[k])
            return i;
    }
    return -1;
}

constexpr int kFirst = first_hue_token();
static_assert(kFirst >= 0 && kFirst + kCount <= tok::kTokenCount, "accent hue tokens");

struct Hues {
    uint32_t rgb[kCount];
};

constexpr Hues hues()
{
    Hues h{};
    for (int i = 0; i < kCount; i++)
        h.rgb[i] = (uint32_t)tok::kTokens[kFirst + i].num;
    return h;
}

constexpr Hues kHues = hues();

int g_hue = (int)(tok::accent_hueDefault >> 16);
bool g_off = false;

}

int hue_on_grid(int hue)
{
    int v = hue + kStep / 2;
    int n = (v >= 0 ? v / kStep : -((-v + kStep - 1) / kStep)) % kCount;
    return (n + kCount) % kCount * kStep;
}

uint32_t accent_for(int hue) { return kHues.rgb[hue_on_grid(hue) / kStep]; }

uint32_t accent() { return g_off ? kWhite : accent_for(g_hue); }

int accent_hue() { return g_hue; }

void set_accent_hue(int hue) { g_hue = hue_on_grid(hue); }

bool accent_off() { return g_off; }

void set_accent_off(bool off) { g_off = off; }

void page_ground(Canvas& c, Rect r)
{
    c.vgradient(r, tok::color_bg, accent(), g_off ? 0 : alpha256(tok::pageSurface_alphaTop), 0, 0, kPortraitH);
}

bool accent_bold(TextStyle& s)
{
    if (!g_off || s.weight == Weight::Bold)
        return false;
    s.weight = Weight::Bold;
    return true;
}

}
