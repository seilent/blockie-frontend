#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "../../ui/roles.h"
#include "../theme.h"
#include "../vtext.h"

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

bool equal(Buffer& a, Buffer& b) { return a.pix == b.pix; }

bool all_ground(Buffer& b, uint32_t ground, Rect r)
{
    Surface s = b.surface();
    for (int y = r.y0; y < r.y1; y++)
        for (int x = r.x0; x < r.x1; x++)
            if (LiveF::unpack(s.at(x, y)) != ground)
                return false;
    return true;
}

constexpr int kRest = 680;
constexpr int kTop = 32;
constexpr int kWrap = 648;
constexpr int kInShift = -40;
constexpr int kAscentX = 670;
constexpr Rect kPane{648, 0, 960, 720};

long long plane_ink(const VLine& l)
{
    long long sum = 0;
    for (uint8_t v : l.plane)
        sum += v;
    return sum;
}

long long tall_ink(Fonts& fonts, const TextStyle& st, const std::string& s)
{
    int w = (int)((fonts.width64(st, s.c_str()) + 63) >> 6) + 4 * st.px;
    Layer layer(w, 3 * st.px);
    Canvas c(layer);
    fonts.draw(c, st, s.c_str(), st.px * 64, 2 * st.px, kWhite, 256);
    long long sum = 0;
    for (uint8_t v : layer.mask)
        sum += v;
    return sum;
}

bool in_leg_keeps_ink(Fonts& fonts, const TextStyle& st, const std::string& s, long long& ink)
{
    VLine l;
    vtext_raster(fonts, st, s, kWrap, l);
    ink = plane_ink(l);
    bool ok = ink > 0 && ink == tall_ink(fonts, st, s);
    for (int shift = kInShift; shift <= 0; shift++) {
        int pen_y = vtext_pen(l, kRest - shift, kPane.y1);
        Rect r = vtext_rect(l, kAscentX, pen_y);
        if (r.y1 > kPane.y1 || r.y0 < 0 || r.x0 < kPane.x0 || r.x1 > kPane.x1)
            ok = false;
        Buffer clipped, wide;
        Canvas cc(clipped.surface()), oc(wide.surface());
        cc.fill(clipped.surface().rect(), theme_dark().bg);
        oc.fill(wide.surface().rect(), theme_dark().bg);
        cc.set_clip(kPane);
        vtext_draw(cc, l, kAscentX, pen_y, theme_dark().ink, 256);
        vtext_draw(oc, l, kAscentX, pen_y, theme_dark().ink, 256);
        if (!equal(clipped, wide))
            ok = false;
    }
    return ok;
}
}

int main(int argc, char** argv)
{
    std::string dir = argc > 1 ? argv[1] : "fonts";
    std::vector<std::string> fallback;
    if (argc > 2 && argv[2][0])
        fallback.push_back(argv[2]);
    Fonts fonts;
    if (!fonts.open(dir, fallback)) {
        std::printf("FAIL fonts not found in %s\n", dir.c_str());
        return 1;
    }
    fonts.begin_frame(-1);

    check(std_px(tok::turnedLineStart) == kRest && std_px(tok::edge) == kTop &&
              std_px(tok::motion_trackShiftIn) == kInShift,
          "turned lines start at y 680, end at y 32, and T9 in starts 40 px below rest");

    {
        const int w = 7, h = 3;
        std::vector<uint8_t> mask(w * h), ccw(w * h);
        for (int i = 0; i < w * h; i++)
            mask[i] = (uint8_t)(i + 1);
        vtext_turn(mask.data(), w, h, ccw.data());
        bool rot = true;
        for (int py = 0; py < w; py++)
            for (int px = 0; px < h; px++)
                if (ccw[py * h + px] != mask[px * w + (w - 1 - py)])
                    rot = false;
        check(rot, "counter clockwise: the plane is the mask rotated, glyph tops left, reading up");
        check(ccw[0] == mask[w - 1] && ccw[w * h - 1] == mask[(h - 1) * w], "the turn is a rotation, not a transpose");
    }

    TextStyle st = role::npTitle.style();
    const std::string text = "Synth Title";
    VLine l;
    bool done = vtext_raster(fonts, st, text, 0, l);
    check(done && l.w >= fonts.ascent(st.px) + fonts.descent(st.px) && l.ascent == fonts.ascent(st.px),
          "a line rasterizes once into a plane at least one line high");
    {
        Layer ref(l.h, l.w);
        Canvas rc(ref);
        fonts.draw(rc, st, text.c_str(), l.pen * 64, l.ascent, kWhite, 256);
        bool same = (int)l.plane.size() == l.w * l.h;
        int inked = 0;
        for (int py = 0; same && py < l.h; py++)
            for (int px = 0; px < l.w; px++) {
                uint8_t v = ref.mask[(size_t)px * l.h + (l.h - 1 - py)];
                if (l.plane[(size_t)py * l.w + px] != v)
                    same = false;
                if (v)
                    inked++;
            }
        check(same && inked > 0, "the plane equals a reference rotation of the line's coverage mask");
    }

    Rect r = vtext_rect(l, kAscentX, kRest);
    check(r.x0 == kAscentX && r.x1 == kAscentX + l.w && r.y1 - l.pen == kRest && r.y1 - r.y0 == l.h,
          "the ascent side at x, the pen at y, reading up");
    check(vtext_pen(l, 720, 720) == 720 - l.pen && vtext_pen(l, kRest, 720) == std::min(kRest, 720 - l.pen),
          "the pen is clamped so the raster bottom never passes the bottom");

    for (const Theme* th : {&theme_dark(), &theme_light()})
        for (int a : {256, 154, 64}) {
            Buffer b1, b2;
            Canvas c1(b1.surface()), c2(b2.surface());
            c1.fill(b1.surface().rect(), th->bg);
            c2.fill(b2.surface().rect(), th->bg);
            vtext_draw(c1, l, kAscentX, kRest, th->ink, a);
            c2.coverage(l.plane.data(), l.w, l.h, r.x0, r.y0, th->ink, a);
            bool eq = equal(b1, b2) && !all_ground(b1, th->bg, r);
            check(eq, th->light ? "light: the draw equals Canvas::coverage on the plane at its alpha"
                                : "dark: the draw equals Canvas::coverage on the plane at its alpha");
        }

    {
        Buffer b;
        Canvas c(b.surface());
        c.fill(b.surface().rect(), theme_dark().bg);
        vtext_draw(c, l, kAscentX, kRest, theme_dark().ink, 0);
        check(all_ground(b, theme_dark().bg, b.surface().rect()), "alpha 0 draws nothing");
    }

    {
        Rect clip{0, (r.y0 + r.y1) / 2, 960, 720};
        Buffer b1, b2;
        Canvas c1(b1.surface()), c2(b2.surface());
        c1.fill(b1.surface().rect(), theme_dark().bg);
        c2.fill(b2.surface().rect(), theme_dark().bg);
        c1.set_clip(clip);
        c2.set_clip(clip);
        vtext_draw(c1, l, kAscentX, kRest, theme_dark().ink, 256);
        c2.coverage(l.plane.data(), l.w, l.h, r.x0, r.y0, theme_dark().ink, 256);
        check(equal(b1, b2) && all_ground(b1, theme_dark().bg, {0, 0, 960, clip.y0}), "the clip is honoured");

        Buffer b3;
        Canvas c3(b3.surface());
        c3.fill(b3.surface().rect(), theme_light().bg);
        Rect bound{r.x0, r.y0, r.x1, (r.y0 + r.y1) / 2};
        c3.set_bound(bound);
        vtext_draw(c3, l, kAscentX, kRest, theme_light().ink, 256);
        check(all_ground(b3, theme_light().bg, {0, bound.y1, 960, 720}) && !all_ground(b3, theme_light().bg, bound),
              "the bound is honoured");
    }

    {
        long long ink = 0;
        check(in_leg_keeps_ink(fonts, st, "jumping jacks just joined", ink),
              "\"j\" title: the plane holds all the ink of a 3 line tall raster, and keeps it through the in leg");
        TextStyle as = role::npArtist.style();
        check(in_leg_keeps_ink(fonts, as, "SYNTH", ink),
              "the system line: the plane holds all its ink and keeps it through the in leg");
        if (fallback.empty()) {
            std::printf("SKIP CJK glyph: no Noto Sans CJK JP Light face was passed\n");
        } else {
            check(in_leg_keeps_ink(fonts, st, "\xe6\x84\x9b", ink),
                  "CJK glyph alone: the plane holds all its ink and keeps it through the in leg");
        }
    }

    {
        const std::string word = "Supercalifragilisticexpialidocioussynthsynthsynth";
        VLine full, cut;
        vtext_raster(fonts, st, word, 0, full);
        vtext_raster(fonts, st, word, kWrap, cut);
        check(full.h > cut.pen + kWrap && cut.h == cut.pen + kWrap, "an over long word: the plane ends at pen + 648");
        Rect rest = vtext_rect(cut, kAscentX, kRest);
        check(rest.y0 == kTop, "an over long word: its top sits at y 32 at rest");
        Buffer at_rest;
        Canvas rc(at_rest.surface());
        rc.fill(at_rest.surface().rect(), theme_dark().bg);
        rc.set_clip(kPane);
        vtext_draw(rc, cut, kAscentX, kRest, theme_dark().ink, 256);
        int top = -1;
        for (int y = 0; y < 720 && top < 0; y++)
            for (int x = kPane.x0; x < kPane.x1; x++)
                if (LiveF::unpack(at_rest.surface().at(x, y)) != theme_dark().bg) {
                    top = y;
                    break;
                }
        check(top >= kTop && top < kTop + 8, "an over long word: the drawn ink starts at the cut, y 32, at rest");
        bool moves = true;
        for (int s = 5; s <= 30; s += 5) {
            Buffer up;
            Canvas uc(up.surface());
            uc.fill(up.surface().rect(), theme_dark().bg);
            uc.set_clip(kPane);
            vtext_draw(uc, cut, kAscentX, kRest - s, theme_dark().ink, 256);
            for (int y = 0; y + s < 720 && moves; y++)
                for (int x = kPane.x0; x < kPane.x1; x++)
                    if (up.surface().at(x, y) != at_rest.surface().at(x, y + s)) {
                        moves = false;
                        break;
                    }
        }
        check(moves, "an over long word: the drawn line, cut included, moves up pixel for pixel in the out leg");
    }

    {
        VText cache(2);
        Buffer b;
        Canvas c(b.surface());
        size_t r0 = cache.rasters();
        cache.draw(c, fonts, st, "Synth A", 0, 0, "Synth A", kAscentX, kRest, 720, kWhite, 256);
        check(cache.size() == 1 && cache.rasters() == r0 + 1, "a complete raster is stored");
        auto a1 = cache.line(fonts, st, "Synth A", 0, 0, "Synth A");
        auto a2 = cache.line(fonts, st, "Synth A", 0, 0, "Synth A");
        check(a1.get() == a2.get() && cache.size() == 1 && cache.rasters() == r0 + 1, "the same key is a hit");
        auto t1 = cache.line(fonts, st, "Synth A", kWrap, 1, "Synth A");
        auto t2 = cache.line(fonts, st, "Synth A", kWrap, 2, "Synth A");
        check(t1.get() != a1.get() && t2.get() != t1.get() && cache.size() == 2,
              "wrap and line are part of the key; the cache keeps its cap");
        auto a4 = cache.line(fonts, st, "Synth A", 0, 0, "Synth A");
        check(a4.get() != a1.get(), "the least recently used entry is evicted");

        Fonts cold;
        cold.open(dir, fallback);
        cold.begin_frame(0);
        VText fresh;
        fresh.line(cold, st, "Synth Cold", 0, 0, "Synth Cold");
        check(fresh.size() == 0, "an incomplete raster is not stored");
        cold.begin_frame(-1);
        fresh.line(cold, st, "Synth Cold", 0, 0, "Synth Cold");
        check(fresh.size() == 1, "the next complete raster is stored");
    }

    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
