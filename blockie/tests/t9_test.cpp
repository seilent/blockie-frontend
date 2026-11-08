#include <cstdio>
#include <string>
#include <vector>

#include "../../ui/roles.h"
#include "../screen_selector.h"
#include "../t9.h"
#include "../text_wrap.h"
#include "../theme.h"

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

std::string title(int i) { return "Synth " + std::to_string(i); }

bool rest(T9Pose p) { return p.shift == 0 && p.a256 == 256; }

}

int main(int argc, char** argv)
{
    check(T9::out_ms() == 500 && T9::in_ms() == 400 && T9::out_px() == 100 && T9::in_px() == -40,
          "T9 is out 500 to +50u and in 400 from -20u");

    {
        T9 t;
        t.set(title(0));
        check(!t.running(0) && rest(t.pose(0)) && t.shown(0) == title(0), "a set title rests");
        t.change(title(0), 10);
        check(!t.running(10), "the same title starts no motion");
        t.change(title(1), 100);
        T9Pose mid = t.pose(350);
        check(mid.shift == 75 && mid.a256 == 64 && t.shown(350) == title(0), "out: easeOut to +50u and o 0, old title");
        T9Pose in = t.pose(800);
        check(in.shift == -30 && in.a256 == 64 && t.shown(800) == title(1), "in: easeIn from -20u and o 0, new title");
        check(t.running(999) && !t.running(1000) && t.end() == 1000, "a single change lands 900 ms after it");
        check(rest(t.pose(1000)), "landed at rest");
    }

    {
        T9 t;
        t.set(title(0));
        std::vector<int64_t> ticks = {0, 300, 380, 460, 540, 620, 700, 780, 860, 940};
        std::vector<std::string> seen = {title(0)};
        size_t k = 0;
        T9Pose before{};
        T9Pose after{};
        for (int64_t ms = 0; ms <= 2000; ms++) {
            if (k < ticks.size() && ticks[k] == ms) {
                if (ms == 540)
                    before = t.pose(ms);
                t.change(title((int)k + 1), ms);
                if (ms == 540)
                    after = t.pose(ms);
                k++;
            }
            if (t.shown(ms) != seen.back())
                seen.push_back(t.shown(ms));
        }
        check(seen.size() == 3 && seen[1] == title(4) && seen[2] == title(10),
              "held at 300 then 80: the out shows title 4 after the first out, then title 10");
        check(before.shift == -40 && before.a256 == 2 && after.shift == before.shift && after.a256 == before.a256,
              "a change during the in restarts the out from the current values");
        check(t.end() == 1440 && t.end() - 940 <= 900, "lands within 900 ms of the last change");
        check(t.shown(1439) == title(10) && t.running(1439) && !t.running(1440) && rest(t.pose(1440)),
              "the newest title shows when it lands");
    }

    {
        T9 t;
        t.set(title(0));
        t.change(title(1), 0);
        t.change(title(2), 100);
        t.land();
        check(!t.running(100) && t.shown(100) == title(2) && rest(t.pose(100)), "land shows the newest title at rest");
    }

    {
        std::string dir = argc > 1 ? argv[1] : "fonts";
        Fonts fonts;
        if (!fonts.open(dir, {})) {
            std::printf("FAIL fonts not found in %s\n", dir.c_str());
            return 1;
        }
        fonts.begin_frame(-1);
        const uint32_t sentinel = 0x00FF00;
        const std::string longer =
            "Synth Alpha Bravo Charlie Delta Echo Foxtrot Golf Hotel India Juliett Kilo Lima Mike";
        const std::string jtitle = "jumping jacks just joined";
        Buffer buf;
        Surface s = buf.surface();
        for (bool edge : {false, true}) {
            SelectorModel m;
            m.game_system = "SYNTH";
            m.t9_edge = edge;
            T9 t;
            t.set(longer);
            t.change(jtitle, 0);
            bool inside = true, masked = true, inked = false;
            for (int64_t ms = 0; ms <= 900; ms += 4) {
                T9Pose p = t.pose(ms);
                m.game_title = t.shown(ms);
                m.title_shift = p.shift;
                m.title_a256 = p.a256;
                Canvas c(s);
                c.fill(s.rect(), sentinel);
                draw_turned(c, fonts, m, true, theme_dark());
                for (int y = 0; y < s.h; y++)
                    for (int x = 0; x < s.w; x++) {
                        bool ground = LiveF::unpack(s.at(x, y)) == sentinel;
                        if (!ground && x < 648)
                            inside = false;
                        if (!ground && x >= 648)
                            inked = true;
                        if (!ground && edge && p.shift < 0 && y >= 680)
                            masked = false;
                    }
            }
            check(inside && inked, edge ? "t9-edge: no turned pixel outside the pane at any t"
                                        : "t9-pane: no turned pixel outside the pane at any t");
            if (edge)
                check(masked, "t9-edge: during the in leg nothing shows below y 680");
        }

        SelectorModel m;
        m.game_system = "SYNTH";
        m.game_title = "Synth Old Title";
        {
            Canvas c(s);
            draw_turned(c, fonts, m, true, theme_dark());
        }
        size_t r0 = turned_rasters();
        T9 t;
        t.set(m.game_title);
        const std::string fresh = "Synth New Title Alpha Bravo Charlie Delta Echo Foxtrot Golf Hotel India";
        t.change(fresh, 0);
        SelectorModel w = m;
        w.game_title = fresh;
        warm_turned(fonts, w);
        size_t r1 = turned_rasters();
        TextStyle ts = role::npTitle.style();
        auto measure = [&](const std::string& x) { return (int)fonts.width64(ts, x.c_str()); };
        size_t lines = wrap_lines(fresh, 648 * 64, 3, measure).size();
        for (int64_t ms = 0; ms <= 900; ms += 16) {
            T9Pose p = t.pose(ms);
            m.game_title = t.shown(ms);
            m.title_shift = p.shift;
            m.title_a256 = p.a256;
            Canvas c(s);
            draw_turned(c, fonts, m, true, theme_dark());
        }
        size_t r2 = turned_rasters();
        check(lines >= 2 && r1 - r0 == lines && r2 == r1,
              "one raster per (title, line): the new lines rasterize at the change, none in the 900 ms of frames");
    }

    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
