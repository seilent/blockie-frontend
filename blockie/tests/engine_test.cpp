#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <unistd.h>

#include "../../core/curves.h"
#include "../dumpz.h"
#include "../engine.h"
#include "../rowruns.h"
#include "../slide.h"

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

constexpr uint32_t kGround = 0x111111;
constexpr uint32_t kInk = 0xC0C0C0;

int one_step(int step, int64_t dt) { return (int)((int64_t)step * 256 * 3 * dt / 50); }

void paint(Buffer& b, Rect r, uint32_t v)
{
    Surface s = b.surface();
    for (int y = r.y0; y < r.y1; y++)
        for (int x = r.x0; x < r.x1; x++)
            s.row(y)[x] = v;
}

size_t at(const eng::Instance& in, int x, int y)
{
    Rect r = in.rect();
    return (size_t)(y - r.y0) * (size_t)(r.x1 - r.x0) + (size_t)(x - r.x0);
}

bool same_held(const eng::Instance& a, const eng::Instance& b)
{
    for (size_t i = 0; i < a.ids().size(); i++)
        if (!a.ids()[i] && a.held()[i] != b.held()[i])
            return false;
    return true;
}

}

int main()
{
    {
        bool step = eng::step_progress(0, 2) == 0 && eng::step_progress(eng::kStepThirds, 2) == kOne &&
                    eng::step_progress(2 * eng::kStepThirds, 2) == 2 * kOne &&
                    eng::step_progress(9 * eng::kStepThirds, 2) == 2 * kOne;
        bool mono = true;
        for (int t3 = 1; t3 <= 2 * eng::kStepThirds; t3++)
            mono = mono && eng::step_progress(t3, 2) >= eng::step_progress(t3 - 1, 2);
        check(step && mono, "engine: the step curve starts, ends and rises as the language states");
    }
    {
        Buffer src, out;
        paint(src, {0, 0, src.w, src.h}, kGround);
        paint(src, {10, 10, 20, 20}, kInk);
        eng::Instance in({0, 0, 64, 32});
        eng::Frame f;
        eng::Block b;
        b.id = 5;
        b.src = {0, 0, 64, 32};
        f.blocks = {b};
        f.shades = {eng::Shade{}};
        eng::Pose q;
        q.rect = {0, 0, 64, 32};
        q.clip = {0, 0, 64, 32};
        f.poses = {q};
        in.begin(1000, false);
        in.advance(1000, 1000);
        in.compose(out.surface(), f, {src.surface()}, kGround, 64);
        bool ids = in.ids()[at(in, 12, 12)] == 5 && in.ids()[at(in, 2, 2)] == 5;
        bool at_t = in.e()[at(in, 12, 12)] == eng::kFull && in.settled();
        check(ids && at_t, "engine: every covered pixel carries its block's id, ink and ground, the first frame at T");
        eng::Block c = b;
        c.id = 6;
        c.ink = eng::Ink::Text;
        f.blocks.push_back(c);
        eng::Shade text;
        text.step = eng::kTextStep;
        f.shades.push_back(text);
        eng::Pose qc = q;
        qc.block = 1;
        qc.rect = {30, 0, 94, 32};
        f.poses.push_back(qc);
        in.advance(1000, 1017);
        in.compose(out.surface(), f, {src.surface()}, kGround, 64);
        bool art = in.e()[at(in, 12, 12)] == eng::kFull;
        bool rise = in.e()[at(in, 42, 12)] == one_step(eng::kTextStep, 17) && (in.age()[at(in, 42, 12)] & 0x80);
        bool held = !in.settled() && in.ready();
        in.advance(1000, 1034);
        in.compose(out.surface(), f, {src.surface()}, kGround, 64);
        held = held && !in.ready();
        check(art && rise && held, "engine: text lit from ground rises one text step, glowing, and holds the rest; "
                                   "ready latches the frame before's settled state");
        bool landed = false;
        for (int n = 2; n < 40 && !landed; n++) {
            in.advance(1000, 1000 + 17 * n);
            in.compose(out.surface(), f, {src.surface()}, kGround, 64);
            landed = in.settled();
        }
        in.advance(1000, 1000 + 17 * 41);
        in.compose(out.surface(), f, {src.surface()}, kGround, 64);
        check(landed && in.ready(), "engine: P-hold ends once every e is at T, and the next compose latches ready");
        f.poses.pop_back();
        f.blocks.pop_back();
        f.shades.pop_back();
        eng::Pose moved = q;
        moved.rect = {4, 0, 68, 32};
        f.poses = {moved};
        in.begin(5000, false);
        in.advance(5000, 5000);
        in.compose(out.surface(), f, {src.surface()}, kGround, 64);
        bool seeded = in.e()[at(in, 15, 12)] == eng::kFull && !(in.age()[at(in, 15, 12)] & 0x80) && in.settled();
        check(seeded, "engine: begin of a fresh fold re-seeds, so its first frame is the rest at T");
    }
    {
        Buffer src, out;
        paint(src, {0, 0, src.w, src.h}, kGround);
        paint(src, {0, 0, 60, 30}, kInk);
        eng::Instance in({0, 0, 64, 32});
        eng::Frame f;
        eng::Block b;
        b.id = 9;
        b.src = {0, 0, 64, 32};
        f.blocks = {b};
        f.shades = {eng::Shade{}};
        eng::Pose q;
        q.rect = {0, 0, 64, 32};
        q.clip = {0, 0, 64, 32};
        f.poses = {q};
        in.begin(0, false);
        in.advance(0, 0);
        in.compose(out.surface(), f, {src.surface()}, kGround, 20);
        for (int n = 1; n < 8; n++) {
            in.advance(0, 17 * n);
            in.compose(out.surface(), f, {src.surface()}, kGround, 20);
        }
        in.advance(0, 17 * 8);
        in.compose(out.surface(), f, {src.surface()}, kGround, 40);
        bool cap = in.e()[at(in, 30, 5)] == eng::kFull && !(in.age()[at(in, 30, 5)] & 0x80);
        check(cap, "engine: a static block uncovered beside its own pixel at T takes T, not glowing (cap)");
    }
    {
        Buffer src, out;
        Surface s = src.surface();
        for (int y = 0; y < src.h; y++)
            for (int x = 0; x < src.w; x++)
                s.row(y)[x] = x >= 10 && x < 20 && y >= 10 && y < 20 ? 0xFF808080u : 0xFF202020u;
        eng::Instance in({0, 0, 64, 32}, true);
        eng::Frame f;
        eng::Block b;
        b.id = 3;
        b.src = {0, 0, 64, 32};
        f.blocks = {b};
        f.shades = {eng::Shade{}};
        eng::Pose q;
        q.rect = {0, 0, 64, 32};
        q.clip = {0, 0, 64, 32};
        f.poses = {q};
        in.begin(0, false);
        in.advance(0, 0);
        in.compose(out.surface(), f, {src.surface()}, 0xFF202020u, 64);
        bool cover = out.surface().row(2)[2] == 0xFF202020u && in.ids()[at(in, 2, 2)] == 3;
        bool ink = out.surface().row(12)[12] == 0xFF808080u && in.ids()[at(in, 12, 12)] == 3;
        eng::Block c = b;
        c.id = 4;
        c.ink = eng::Ink::Text;
        f.blocks.push_back(c);
        f.shades.push_back(eng::Shade{});
        eng::Pose qc = q;
        qc.block = 1;
        qc.rect = {30, 0, 94, 32};
        f.poses.push_back(qc);
        in.advance(0, 17);
        in.compose(out.surface(), f, {src.surface()}, 0xFF202020u, 64);
        uint32_t mid = out.surface().row(12)[42];
        bool glow = (mid >> 24) == 0xFF && mid != 0xFF808080u && mid != 0xFF202020u;
        check(cover && ink && glow && in.argb(),
              "engine: the ARGB panel covers opaquely, its ground is its scrim, arriving ink glows over the scrim");
    }
    {
        eng::Instance in({0, 0, 16, 8});
        Buffer src, out;
        paint(src, {0, 0, src.w, src.h}, kGround);
        paint(src, {2, 2, 6, 6}, kInk);
        eng::Frame f;
        eng::Block b;
        b.id = 300;
        b.src = {0, 0, 16, 8};
        f.blocks = {b};
        f.shades = {eng::Shade{}};
        eng::Pose q;
        q.rect = {0, 0, 16, 8};
        q.clip = {0, 0, 16, 8};
        f.poses = {q};
        in.begin(0, false);
        in.advance(0, 0);
        in.compose(out.surface(), f, {src.surface()}, kGround, 16);
        const char* tmp = std::getenv("TMPDIR");
        std::string tmpl = std::string(tmp && *tmp ? tmp : "/tmp") + "/blockie-test-poselog-XXXXXX";
        std::vector<char> buf(tmpl.begin(), tmpl.end());
        buf.push_back('\0');
        std::string dir = mkdtemp(buf.data()) ? std::string(buf.data()) : std::string();
        eng::PoseLog log;
        bool ok = !dir.empty() && log.open(dir + "/poses.txt", dir);
        eng::Frame logged = f;
        logged.blocks[0].tile = true;
        logged.blocks[0].source = 1;
        log.fold(0, "step", "expand", false, 500, logged);
        ok = ok && log.frame(0, 0, 0, 0, 0, f, in) && log.close();
        std::vector<uint16_t> ids(in.ids().size());
        ok = ok && load_z(dir + "/id-00000.z", ids.data(), ids.size() * sizeof(uint16_t)) && ids == in.ids() &&
             ids[at(in, 3, 3)] == 300;
        std::vector<uint8_t> cov(in.cover().size());
        ok = ok && load_z(dir + "/c-00000.z", cov.data(), cov.size()) && cov == in.cover() && cov[at(in, 3, 3)] == 1;
        std::vector<std::string> fields;
        if (FILE* fp = std::fopen((dir + "/poses.txt").c_str(), "r")) {
            char lb[512];
            while (std::fgets(lb, sizeof lb, fp)) {
                std::string l(lb);
                if (l.compare(0, 6, "block ") != 0)
                    continue;
                fields.clear();
                size_t a = 0;
                while ((a = l.find_first_not_of(" \n", a)) != std::string::npos) {
                    size_t z = l.find_first_of(" \n", a);
                    fields.push_back(l.substr(a, z - a));
                    a = z;
                }
            }
            std::fclose(fp);
        }
        bool line = fields.size() == 20 && fields[18] == "1" && fields[19] == "1";
        for (const char* n : {"/poses.txt", "/e-00000.z", "/id-00000.z", "/g-00000.z", "/c-00000.z"})
            std::remove((dir + n).c_str());
        if (!dir.empty())
            rmdir(dir.c_str());
        check(ok && line, "engine: the pose log writes 16 bit ids that round-trip, id 300 kept, the cover mask (c, 1 "
                          "where a pose covers) that round-trips, and a block line of 19 fields after the keyword, "
                          "tile at 17 and the source surface at 18");
    }
    {
        SlidePlan ss;
        slide_lang(ss, pane_surface());
        bool vals = ss.moves.pane_end == 417 && ss.pane_stop == 417 && ss.moves.fold == 567 &&
                    ss.left == LeftMove::Stepper;
        check(vals, "engine: the per row pane's end, 417 ms, the grid waiting for it, the fold 567 ms (section 5), and "
                    "the stepper grid");
    }
    {
        bool rows = true;
        for (bool collapse : {false, true}) {
            SlidePlan sp;
            sp.target = kPaneSplitWide;
            sp.moves.collapse = collapse;
            slide_lang(sp, pane_surface());
            int64_t dr = sp.moves.pane_end, d3 = 1250, leg3 = 750;
            int64_t start3[9] = {500, 0, 0, 0, 250, 250, 250, 500, 500};
            eng::Frame f;
            auto off = [&](int k) {
                if (k == 0)
                    return f.ground.empty() ? kPortraitH : kPortraitH - f.ground.y1;
                const eng::Pose& q = f.poses[(size_t)k];
                return f.blocks[(size_t)q.block].src.y0 - q.rect.y0;
            };
            pane_frame_at(sp, 0, kPortraitW, kPortraitH, f);
            auto same = [](Rect a, Rect b) { return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1; };
            rows = rows && f.blocks.size() == 9 && f.poses.size() == 9 && f.blocks[0].id == 1 &&
                   f.blocks[0].source == 1 && f.blocks[0].z == 0 && f.blocks[1].id == 2 && f.blocks[1].z == 2 &&
                   same(f.blocks[1].src, {kPaneLeftEdge, 0, kPortraitW, 37}) && f.blocks[8].id == 9 &&
                   same(f.blocks[8].src, {kPaneLeftEdge, 613, kPortraitW, kPortraitH}) &&
                   (collapse ? f.ground.empty() : same(f.ground, {kPaneLeftEdge, 0, kPortraitW, kPortraitH}));
            for (int k = 0; k < 9; k++)
                rows = rows && off(k) == (collapse ? 720 : 0);
            pane_frame_at(sp, dr, kPortraitW, kPortraitH, f);
            for (int k = 0; k < 9; k++)
                rows = rows && off(k) == (collapse ? 0 : 720);
            for (int k = 0; k < 9; k++) {
                int64_t at3 = collapse ? d3 - start3[k] - leg3 : start3[k];
                pane_frame_at(sp, at3 / 3, kPortraitW, kPortraitH, f);
                rows = rows && off(k) == (collapse ? 720 : 0);
                pane_frame_at(sp, at3 / 3 + 20, kPortraitW, kPortraitH, f);
                rows = rows && (collapse ? off(k) < 720 : off(k) > 0);
            }
            for (int64_t u = 0; u <= dr; u++) {
                pane_frame_at(sp, u, kPortraitW, kPortraitH, f);
                for (int k = 1; k < 9; k++)
                    rows = rows && off(k) >= off(0) && (k == 1 || off(k) <= off(k - 1));
                rows = rows && f.poses[0].clip.y0 == (f.ground.empty() ? 0 : f.ground.y1);
            }
        }
        check(rows, "engine: the per row pane (6.4): 8 row blocks over the frame's cover only ground panel on the "
                    "menu column, the title beneath; three waves one step apart, the panel with the last; every row "
                    "leads the panel's bottom edge, top first, 720 px at D_r; the title shows only below the panel's "
                    "edge; the collapse runs each leg mirrored, the panel first and the top strip last");
    }
    {
        SlidePlan ex, co;
        ex.target = co.target = kPaneSplitWide;
        co.moves.collapse = true;
        slide_lang(ex, pane_surface());
        slide_lang(co, pane_surface());
        int64_t d = co.moves.fold, de = ex.moves.fold;
        bool collapse = pane_rest(co, d, false, true) == -1 && pane_rest(co, d, false, false) == -1 &&
                        pane_rest(co, d / 2, false, false) == 0 && pane_rest(co, 0, false, false) == 0 &&
                        pane_rest(co, 0, false, true) == 1;
        bool expand = pane_rest(ex, 0, false, true) == -1 && pane_rest(ex, de / 2, false, true) == 0 &&
                      pane_rest(ex, de, false, false) == 0 && pane_rest(ex, de, false, true) == 1;
        bool back = pane_rest(ex, 0, true, true) == 1 && pane_rest(ex, 0, true, false) == 0;
        for (int64_t ft = 0; ft <= de - eng::kTailMs; ft++)
            back = back && pane_rest(ex, ft, true, false) != -1 && pane_rest(ex, ft, true, true) != -1;
        check(collapse && expand && back, "engine: the pane's pose log rest follows page_rest's rule: -1 at the fold's "
                                          "start rest, a collapse's first frame included, settled or not, 1 at its "
                                          "landing once settled, else 0, and a reversal never -1");
    }
    for (int speed : {13, 45}) {
        Buffer src, out;
        paint(src, {0, 0, src.w, src.h}, kGround);
        paint(src, {50, 10, 60, 20}, kInk);
        eng::Instance in({0, 0, 400, 32});
        eng::Frame f;
        eng::Block b;
        b.id = 11;
        b.ink = eng::Ink::Text;
        b.src = {0, 0, 120, 32};
        eng::Shade text;
        text.step = eng::kTextStep;
        f.blocks = {b};
        f.shades = {text};
        bool kept = true;
        in.begin(0, false);
        for (int n = 0; n < 6; n++) {
            eng::Pose q;
            q.rect = {speed * n, 0, speed * n + 120, 32};
            q.clip = {0, 0, 400, 32};
            f.poses = {q};
            in.advance(0, 17 * n);
            in.compose(out.surface(), f, {src.surface()}, kGround, 400);
            for (int x = 50 + speed * n; x < 60 + speed * n; x++)
                kept = kept && in.e()[at(in, x, 12)] == eng::kFull;
        }
        std::string what = "engine: text moving " + std::to_string(speed) + " px a frame keeps every ink pixel at T";
        check(kept, what.c_str());
    }
    {
        Buffer src, out;
        paint(src, {0, 0, src.w, src.h}, kGround);
        paint(src, {100, 10, 110, 20}, kInk);
        eng::Instance in({0, 0, 400, 32});
        eng::Frame f;
        eng::Block b;
        b.id = 12;
        b.ink = eng::Ink::Text;
        b.src = {0, 0, 120, 32};
        eng::Shade text;
        text.step = eng::kTextStep;
        f.blocks = {b};
        f.shades = {text};
        bool kept = true;
        in.begin(0, false);
        for (int n = 0; n < 6; n++) {
            eng::Pose q;
            q.rect = {45 * n, 0, 45 * n + 120, 32};
            q.clip = {0, 0, 400, 32};
            f.poses = {q};
            in.advance(0, 17 * n);
            in.compose(out.surface(), f, {src.surface()}, kGround, 400);
            for (int x = 100 + 45 * n; x < 110 + 45 * n; x++)
                kept = kept && in.e()[at(in, x, 12)] == eng::kFull && !(in.age()[at(in, x, 12)] & 0x80);
        }
        check(kept, "engine: ink 20 px from the leading edge at 45 px a frame stays at T after frame 0 (e travels "
                    "with the block)");
    }
    {
        Buffer plane, title, out;
        paint(plane, {0, 0, plane.w, plane.h}, kGround);
        paint(plane, {32, 10, 40, 20}, kInk);
        paint(title, {0, 0, title.w, title.h}, kGround);
        paint(title, {0, 10, 200, 20}, kInk);
        eng::Instance in({0, 0, 200, 32});
        eng::Block bt, bp;
        bt.id = 2;
        bt.ink = eng::Ink::Text;
        bt.source = 1;
        bt.src = {0, 0, 200, 32};
        bp.id = 1;
        bp.ink = eng::Ink::Text;
        bp.z = 1;
        bp.src = {0, 0, 40, 32};
        eng::Shade text;
        text.step = eng::kTextStep;
        eng::Frame f;
        f.blocks = {bp, bt};
        f.shades = {text, text};
        auto at_x = [&](int x0) {
            eng::Pose qt, qp;
            qt.block = 1;
            qt.rect = {0, 0, 200, 32};
            qt.clip = {0, 0, 200, 32};
            qp.block = 0;
            qp.rect = {x0, 0, x0 + 40, 32};
            qp.clip = {0, 0, 200, 32};
            f.poses = {qt, qp};
        };
        in.begin(0, false);
        int xs[] = {0, 20, 40, 60};
        for (int n = 0; n < 4; n++) {
            at_x(xs[n]);
            in.advance(0, 17 * n);
            in.compose(out.surface(), f, {plane.surface(), title.surface()}, kGround, 200);
        }
        size_t i = at(in, 55, 12);
        bool reveal = in.ids()[i] == 2 && in.e()[i] == one_step(eng::kTextStep, 17) && (in.age()[i] & 0x80);
        check(reveal, "engine: a title pixel revealed 2 frames after the plane's ink passed starts at one text step");
        bool cover = true, reach = false;
        for (int n = 0; n <= 10; n++) {
            at_x(200 - 20 * n);
            in.advance(0, 17 * (n + 4));
            in.compose(out.surface(), f, {plane.surface(), title.surface()}, kGround, 200);
            if (n == 1)
                cover = in.ids()[at(in, 195, 12)] == 1 && in.e()[at(in, 195, 12)] == one_step(eng::kTextStep, 17) &&
                        (in.age()[at(in, 195, 12)] & 0x80);
            if (n == 9)
                reach = in.e()[at(in, 200 - 20 * n + 15, 12)] == eng::kFull;
        }
        check(cover && reach, "engine: a pixel the plane brings in on a collapse starts at one text step, and that "
                              "content, carried with the plane, reaches T within 150 ms");
    }
    {
        Buffer plane, under, out;
        paint(plane, {0, 0, plane.w, plane.h}, kGround);
        paint(plane, {0, 10, 100, 20}, kInk);
        paint(under, {0, 0, under.w, under.h}, kGround);
        eng::Instance in({0, 0, 200, 32});
        eng::Block bp;
        bp.id = 1;
        bp.ink = eng::Ink::Text;
        bp.src = {0, 0, 100, 32};
        eng::Shade text;
        text.step = eng::kTextStep;
        eng::Frame f;
        f.blocks = {bp};
        f.shades = {text};
        bool entered = true;
        in.begin(0, false);
        for (int n = 0; n <= 3; n++) {
            eng::Pose q;
            q.rect = {200 - 20 * n, 0, 300 - 20 * n, 32};
            q.clip = {0, 0, 200, 32};
            f.poses = {q};
            in.advance(0, 17 * n);
            in.compose(out.surface(), f, {plane.surface()}, kGround, 200);
            if (n == 2)
                for (int x = 180; x < 200; x++)
                    entered = entered && in.ids()[at(in, x, 12)] == 1 &&
                              in.e()[at(in, x, 12)] == one_step(eng::kTextStep, 17) && (in.age()[at(in, x, 12)] & 0x80);
        }
        check(entered, "engine: only a pixel the block did not show last frame takes the owner rule: a 100 px plane "
                       "entering through the instance edge at 20 px a frame starts its newly entered columns at one "
                       "step, glowing, on its second entering frame");
    }
    {
        Buffer src, out;
        paint(src, {0, 0, src.w, src.h}, kGround);
        paint(src, {0, 0, 64, 32}, kInk);
        eng::Instance in({0, 0, 128, 32});
        eng::Block old_tile, new_tile;
        old_tile.id = 20;
        old_tile.z = 1;
        old_tile.src = {0, 0, 64, 32};
        old_tile.tile = true;
        new_tile.id = 21;
        new_tile.src = {0, 0, 64, 32};
        new_tile.tile = true;
        eng::Shade art;
        art.t = eng::kFull;
        eng::Frame f;
        f.blocks = {old_tile, new_tile};
        f.shades = {art, art};
        eng::Pose qo, qn;
        qo.block = 0;
        qo.rect = {0, 0, 64, 32};
        qo.clip = {0, 0, 128, 32};
        qn.block = 1;
        qn.rect = {0, 0, 64, 32};
        qn.clip = {0, 0, 128, 32};
        eng::Frame none = f;
        none.poses.clear();
        in.begin(0, false);
        in.advance(0, 0);
        in.compose(out.surface(), none, {src.surface()}, kGround, 128);
        f.poses = {qn, qo};
        in.advance(0, 17);
        in.compose(out.surface(), f, {src.surface()}, kGround, 128);
        int e1 = in.e()[at(in, 10, 12)];
        qo.rect = {40, 0, 104, 32};
        f.poses = {qn, qo};
        in.advance(0, 34);
        in.compose(out.surface(), f, {src.surface()}, kGround, 128);
        bool cont = e1 == one_step(eng::kArtStep, 17) && in.ids()[at(in, 10, 12)] == 21 &&
                    in.e()[at(in, 10, 12)] == e1 + one_step(eng::kDimStep, 17) && !(in.age()[at(in, 10, 12)] & 0x80);
        check(cont, "engine: a lone row's new tile revealed under the leaving old tile continues from its e");
        eng::Block page;
        page.id = 30;
        page.z = 2;
        page.src = {0, 0, 64, 32};
        f.blocks.push_back(page);
        f.shades.push_back(art);
        eng::Pose qg;
        qg.block = 2;
        qg.rect = {0, 0, 64, 32};
        qg.clip = {0, 0, 128, 32};
        f.poses = {qn, qo, qg};
        in.advance(0, 51);
        in.compose(out.surface(), f, {src.surface()}, kGround, 128);
        bool ground = in.ids()[at(in, 10, 12)] == 30 && in.e()[at(in, 10, 12)] == one_step(eng::kArtStep, 17) &&
                      (in.age()[at(in, 10, 12)] & 0x80);
        check(ground, "engine: a page block with art ink over a tile's pixel starts from ground, it is not a tile");
    }
    {
        Buffer old_title, new_title, out;
        paint(old_title, {0, 0, old_title.w, old_title.h}, kGround);
        paint(old_title, {4, 10, 60, 20}, kInk);
        paint(new_title, {0, 0, new_title.w, new_title.h}, kGround);
        paint(new_title, {4, 10, 60, 20}, 0xA0A0A0);
        eng::Instance in({0, 0, 64, 32});
        eng::Block a, b;
        a.id = 2;
        a.ink = eng::Ink::Text;
        a.src = {0, 0, 64, 32};
        b.id = 3;
        b.ink = eng::Ink::Text;
        b.source = 1;
        b.src = {0, 0, 64, 32};
        eng::Shade text;
        text.step = eng::kTextStep;
        eng::Pose q;
        q.rect = {0, 0, 64, 32};
        q.clip = {0, 0, 64, 32};
        eng::Frame f;
        f.blocks = {a};
        f.shades = {text};
        f.poses = {q};
        in.begin(0, false);
        for (int n = 0; n < 3; n++) {
            in.advance(0, 17 * n);
            in.compose(out.surface(), f, {old_title.surface(), new_title.surface()}, kGround, 64);
        }
        bool at_t = in.e()[at(in, 20, 12)] == eng::kFull;
        f.blocks = {b};
        in.advance(0, 51);
        in.compose(out.surface(), f, {old_title.surface(), new_title.surface()}, kGround, 64);
        bool fresh = in.ids()[at(in, 20, 12)] == 3 && in.e()[at(in, 20, 12)] == one_step(eng::kTextStep, 17) &&
                     (in.age()[at(in, 20, 12)] & 0x80);
        check(at_t && fresh, "engine: a replacement title starts from ground, never at the old title's level (a new "
                             "block id at the same place)");
    }
    {
        Buffer src, out;
        paint(src, {0, 0, src.w, src.h}, kGround);
        paint(src, {4, 4, 124, 28}, kInk);
        eng::Instance in({0, 0, 128, 32});
        eng::Block low, top;
        low.id = 40;
        low.src = {0, 0, 128, 32};
        top.id = 41;
        top.z = 1;
        top.src = {0, 0, 128, 32};
        eng::Shade dep;
        dep.dep = true;
        eng::Frame f;
        f.blocks = {low, top};
        f.shades = {dep, eng::Shade{}};
        eng::Pose ql, qt;
        ql.rect = {0, 0, 128, 32};
        ql.clip = {0, 0, 128, 32};
        qt.block = 1;
        qt.rect = {64, 0, 192, 32};
        qt.clip = {0, 0, 128, 32};
        f.poses = {ql, qt};
        in.begin(0, false);
        for (int n = 0; n < 3; n++) {
            in.advance(0, 17 * n);
            in.compose(out.surface(), f, {src.surface()}, kGround, 128);
        }
        size_t u = at(in, 80, 12), v = at(in, 20, 12);
        bool under = in.ids()[u] == 41 && in.e()[u] == eng::kFull && in.e()[v] == eng::kFull;
        qt.rect = {96, 0, 224, 32};
        f.poses = {ql, qt};
        in.begin(51, true);
        in.advance(51, 51);
        in.compose(out.surface(), f, {src.surface()}, kGround, 128);
        int want = one_step(eng::kArtStep, 17);
        bool one = in.ids()[u] == 40 && in.e()[u] == want && (in.age()[u] & 0x80);
        bool own = in.e()[v] == eng::kFull;
        if (!under || !one || !own)
            std::printf("  uncovered (80, 12): id %d e %d want %d glow %d; own (20, 12): e %d want %d\n",
                        in.ids()[u], in.e()[u], want, (in.age()[u] & 0x80) ? 1 : 0, in.e()[v], eng::kFull);
        check(under && one && own, "engine: a departing pixel uncovered from under another block starts at one step, "
                                   "its own shown pixels keep their e");
    }
    {
        SlidePlan sp;
        sp.target = kPaneSplitWide;
        sp.lang_on = true;
        sp.pane = std::make_shared<eng::Instance>(Rect{0, 0, 64, 32});
        Buffer src, out;
        paint(src, {0, 0, src.w, src.h}, kGround);
        paint(src, {10, 10, 20, 20}, kInk);
        eng::Frame f;
        eng::Block b;
        b.id = 7;
        b.ink = eng::Ink::Text;
        b.src = {0, 0, 64, 32};
        f.blocks = {b};
        eng::Shade text;
        text.step = eng::kTextStep;
        f.shades = {text};
        eng::Pose q;
        q.rect = {40, 0, 104, 32};
        q.clip = {0, 0, 64, 32};
        f.poses = {q};
        eng::Frame none = f;
        none.poses.clear();
        slide_advance(sp, 1000, 1000);
        sp.pane->compose(out.surface(), none, {src.surface()}, kGround, 64);
        slide_advance(sp, 1000, 1017);
        sp.pane->compose(out.surface(), f, {src.surface()}, kGround, 64);
        size_t i = at(*sp.pane, 52, 12);
        uint16_t e = sp.pane->e()[i];
        bool glowing = sp.pane->age()[i] & 0x80;
        sp.moves.reversed = true;
        slide_advance(sp, 1017, 1034);
        bool kept = sp.pane->reversed() && sp.pane->e()[i] == e && (sp.pane->age()[i] & 0x80) && glowing &&
                    e > 0 && e < eng::kFull;
        check(kept, "engine: a reversal through slide_advance keeps a glowing pixel's e and its glow bit");
    }
    {
        bool held = true;
        for (bool collapse : {false, true}) {
            SlidePlan sp;
            sp.target = kPaneSplitWide;
            sp.moves.collapse = collapse;
            slide_lang(sp, pane_surface());
            for (int64_t t = 0; t <= sp.moves.fold; t++)
                held = held && slide_pose(sp, t).boundary == kPaneSplitWide;
        }
        check(held, "engine: the language sweeps no boundary: the grid's end is 648 on every frame of an expand and "
                    "a collapse, s 0 included");
    }
    {
        auto one = [](uint16_t id, int x0, int w, const eng::Shade& sh) {
            eng::Frame f;
            eng::Block b;
            b.id = id;
            b.src = {0, 0, w, 32};
            f.blocks = {b};
            f.shades = {sh};
            eng::Pose q;
            q.rect = {x0, 0, x0 + w, 32};
            q.clip = {0, 0, 192, 32};
            f.poses = {q};
            return f;
        };
        auto two = [](eng::Frame f, const eng::Frame& g) {
            f.blocks.push_back(g.blocks[0]);
            f.shades.push_back(g.shades[0]);
            eng::Pose q = g.poses[0];
            q.block = (int)f.blocks.size() - 1;
            f.poses.push_back(q);
            return f;
        };
        Buffer src, out;
        paint(src, {0, 0, src.w, src.h}, kGround);
        paint(src, {0, 0, 192, 32}, kInk);
        eng::Shade dep;
        dep.dep = true;
        dep.t = 153 << 8;
        dep.step = eng::kTextStep;
        eng::Shade top;
        top.t = eng::kFull;
        eng::Frame none;
        const uint16_t ring = (uint16_t)(9 | eng::kRingBit);
        auto pass = [&](eng::Instance& in, const eng::Frame& f, int64_t now) {
            in.advance(in.t0(), now);
            in.compose(out.surface(), f, {src.surface()}, kGround, 192);
        };

        eng::Shade fast;
        fast.step = eng::kTextStep;
        eng::Instance a({0, 0, 192, 32});
        a.begin(0, false);
        pass(a, none, 0);
        pass(a, one(ring, 0, 96, fast), 17);
        size_t ia = at(a, 40, 12);
        int e1 = a.e()[ia];
        bool lit1 = a.age()[ia] & 0x80;
        pass(a, one(ring, 0, 96, fast), 34);
        bool rises = a.ids()[ia] == ring && e1 == one_step(eng::kRingStep, 17) && lit1 &&
                     a.e()[ia] == 2 * one_step(eng::kRingStep, 17);
        check(rises, "engine (pinned): a ring lit from ground rises one ring step a frame, glowing");

        eng::Instance b({0, 0, 192, 32});
        b.begin(0, false);
        eng::Frame below = one(ring, 0, 96, dep);
        eng::Frame cover = one(10, 64, 128, top);
        cover.blocks[0].z = 1;
        eng::Frame both = two(below, cover);
        pass(b, none, 0);
        pass(b, both, 17);
        pass(b, both, 34);
        int lit_ring = b.e()[at(b, 24, 12)];
        b.begin(51, true);
        pass(b, one(ring, 96, 96, dep), 51);
        size_t carried = at(b, 120, 12), owner = at(b, 170, 12);
        bool hold = lit_ring == 2 * one_step(eng::kRingStep, 17) && lit_ring < dep.t && b.e()[carried] == lit_ring &&
                    b.e()[owner] == one_step(eng::kRingStep, 17) && (b.age()[owner] & 0x80);
        check(hold, "engine (pinned): after Back a ring hold keeps its carried pixels at min(e, T) and starts a "
                    "pixel another block owned last frame at one ring step");

        eng::Instance c({0, 0, 192, 32});
        c.begin(0, false);
        eng::Frame clipped = one(ring, 0, 96, dep);
        clipped.poses[0].clip = {0, 0, 64, 32};
        for (int n = 0; n < 3; n++)
            pass(c, clipped, 17 * n);
        c.begin(51, true);
        pass(c, one(ring, 96, 96, dep), 51);
        size_t fresh = at(c, 170, 12);
        check(c.ids()[fresh] == ring && c.e()[fresh] == dep.t,
              "engine (pinned): after Back a ring hold newly covering a pixel no block owned takes min(T, the "
              "block's lowest e at Back), T for a ring only block");

        eng::Instance c2({0, 0, 192, 32});
        c2.begin(0, false);
        eng::Frame ink9 = one(9, 0, 96, top);
        ink9.poses[0].clip = {0, 0, 64, 32};
        pass(c2, none, 0);
        pass(c2, ink9, 17);
        pass(c2, ink9, 34);
        int low9 = c2.e()[at(c2, 40, 12)];
        c2.begin(51, true);
        pass(c2, one(ring, 96, 96, dep), 51);
        size_t far = at(c2, 170, 12);
        check(low9 == 2 * one_step(eng::kArtStep, 17) && low9 < dep.t && c2.ids()[far] == ring && c2.e()[far] == low9,
              "engine (pinned): after Back a ring hold newly covering a pixel no block owned takes the block's lowest e "
              "at Back when that is under T");

        eng::Instance c3({0, 0, 192, 32}), c4({0, 0, 192, 32});
        c3.set_segments(true);
        c3.set_verify(true);
        c4.set_segments(true);
        c4.begin(0, false);
        pass(c4, none, 0);
        pass(c4, ink9, 17);
        pass(c4, ink9, 34);
        c4.begin(51, true);
        pass(c4, one(ring, 96, 96, dep), 51);
        c3.begin(0, false);
        pass(c3, none, 0);
        pass(c3, ink9, 17);
        pass(c3, ink9, 34);
        int low3 = c3.e()[at(c3, 40, 12)];
        c3.begin(51, true);
        pass(c3, one(ring, 96, 96, dep), 51);
        check(c3.segment_frames() == 3 && low3 == low9 && c3.ids()[far] == ring && c3.e()[far] == low9 &&
                  c3.verify_scans() == 1 && c3.verify_fails() == 0 && c4.e() == c3.e() && c4.verify_scans() == 0,
              "engine: on segments, after Back a ring hold newly covering a pixel no block owned takes the block's "
              "lowest e at Back from last frame's runs; with verify on the scan agrees, with verify off no scan runs");

        eng::Instance d({0, 0, 192, 32});
        d.begin(0, false);
        eng::Shade low = dep;
        low.t = 100 << 8;
        eng::Frame seed = one(12, 0, 96, low);
        seed.poses[0].clip = {0, 0, 64, 32};
        pass(d, seed, 0);
        pass(d, one(12, 32, 96, dep), 17);
        size_t kept = at(d, 80, 12), entered = at(d, 110, 12);
        check(low.t < dep.t && d.ids()[kept] == 12 && d.e()[kept] == dep.t && !(d.age()[kept] & 0x80) &&
                  d.ids()[entered] == 12 && d.e()[entered] == dep.t && !(d.age()[entered] & 0x80),
              "engine (pinned): a departing block moving forward draws at its T, on its carried pixels after T rises "
              "and on the pixels it newly shows");

        eng::Instance r({0, 0, 192, 32});
        r.begin(0, false);
        pass(r, none, 0);
        pass(r, one((uint16_t)(11 | eng::kRingBit), 0, 96, dep), 17);
        pass(r, one((uint16_t)(11 | eng::kRingBit), 0, 96, dep), 34);
        size_t over = at(r, 40, 12);
        int ring_e = r.e()[over];
        r.begin(51, true);
        pass(r, one(11, 0, 96, dep), 51);
        check(ring_e == 2 * one_step(eng::kRingStep, 17) && ring_e < dep.t && r.ids()[over] == 11 &&
                  r.e()[over] == dep.t,
              "engine (pinned): after Back an ink pixel over its own ring's last pixel takes min(T, the block's lowest "
              "e at Back), not the ring's e");

        eng::Instance u({0, 0, 192, 32});
        u.begin(0, false);
        pass(u, one(5, 0, 96, eng::Shade{}), 0);
        pass(u, one(5, 0, 96, eng::Shade{}), 17);
        pass(u, none, 34);
        size_t left = at(u, 40, 12);
        uint16_t e_left = u.e()[left];
        uint8_t age_left = u.age()[left];
        u.compose(out.surface(), two(one(5, 0, 32, eng::Shade{}), one(13, 64, 32, eng::Shade{})), {src.surface()},
                  kGround, 192);
        check(u.ids()[left] == 0 && u.e()[left] == e_left && u.age()[left] == age_left && e_left == eng::kFull &&
                  age_left == 17 && out.surface().row(12)[40] == kGround,
              "engine (pinned): a compose that does not advance leaves an unowned pixel between two drawn blocks at "
              "its e and age and draws it as ground");

        eng::Instance z({0, 0, 192, 32});
        z.begin(0, false);
        eng::Shade zero;
        zero.t = 0;
        pass(z, one(6, 0, 96, zero), 0);
        check(z.e()[at(z, 40, 12)] == 0 && out.surface().row(12)[40] == kGround,
              "engine (pinned): a pixel at e 0 draws ground");

        eng::Shade lean;
        lean.t = 100 << 8;
        lean.sigma = 154;
        int a0 = (int)(((int64_t)lean.t * 256 + eng::kFull / 2) / eng::kFull);
        int a1 = std::min(256, (a0 * 256 + lean.sigma / 2) / lean.sigma);
        eng::Instance g({0, 0, 192, 32});
        g.begin(0, false);
        pass(g, one(7, 0, 96, lean), 0);
        uint32_t want = blend_px(kGround, kInk, a1);
        Buffer asrc, aout;
        paint(asrc, {0, 0, asrc.w, asrc.h}, 0xFF202020u);
        paint(asrc, {0, 0, 192, 32}, 0xFF808080u);
        eng::Instance ga({0, 0, 192, 32}, true);
        ga.begin(0, false);
        ga.advance(0, 0);
        ga.compose(aout.surface(), one(7, 0, 96, lean), {asrc.surface()}, 0xFF202020u, 192);
        uint32_t wa = 0xFF000000u;
        for (int sh = 0; sh < 24; sh += 8) {
            int gv = (int)((0xFF202020u >> sh) & 0xFF), v = (int)((0xFF808080u >> sh) & 0xFF);
            wa |= (uint32_t)(gv + (((v - gv) * a1 + 128) >> 8)) << sh;
        }
        check(a1 > 0 && a1 < 256 && out.surface().row(12)[40] == want && aout.surface().row(12)[40] == wa,
              "engine (pinned): a block of sigma 154 at e 0.39 draws at round(round(e x 256 / 65280) x 256 / sigma) in "
              "the RGB and the ARGB shade");

        eng::Shade wide = lean;
        wide.sigma = 200;
        int a2 = std::min(256, (a0 * 256 + wide.sigma / 2) / wide.sigma);
        eng::Instance m({0, 0, 192, 32});
        m.begin(0, false);
        pass(m, two(one(7, 0, 96, lean), one(8, 96, 96, wide)), 0);
        bool own = out.surface().row(12)[40] == blend_px(kGround, kInk, a1) &&
                   out.surface().row(12)[140] == blend_px(kGround, kInk, a2);
        pass(m, two(one(7, 0, 96, wide), one(8, 96, 96, lean)), 17);
        check(a1 != a2 && own && m.e()[at(m, 40, 12)] == lean.t && m.e()[at(m, 140, 12)] == lean.t &&
                  out.surface().row(12)[40] == blend_px(kGround, kInk, a2) &&
                  out.surface().row(12)[140] == blend_px(kGround, kInk, a1),
              "engine: two blocks of sigma 154 and 200 in one frame each draw at their own sigma, and at the new one "
              "after they swap");

        eng::Instance o({0, 0, 192, 32});
        o.begin(0, false);
        pass(o, two(one(10, 0, 96, top), one(11, 48, 96, top)), 0);
        bool later = o.ids()[at(o, 60, 12)] == 11 && o.ids()[at(o, 20, 12)] == 10 && o.ids()[at(o, 120, 12)] == 11;
        eng::Instance o2({0, 0, 192, 32});
        o2.begin(0, false);
        pass(o2, two(one(11, 48, 96, top), one(10, 0, 96, top)), 0);
        check(later && o2.ids()[at(o2, 60, 12)] == 10 && o2.ids()[at(o2, 20, 12)] == 10 &&
                  o2.ids()[at(o2, 120, 12)] == 11,
              "engine: of two overlapping blocks at one z, the later pose draws over the earlier, in either order");
    }
    {
        auto noise = [](Buffer& b, uint32_t seed) {
            Surface s = b.surface();
            for (int y = 0; y < s.h; y++)
                for (int x = 0; x < s.w; x++)
                    s.row(y)[x] = ((uint32_t)x * 2654435761u ^ (uint32_t)y * 40503u ^ seed) & 0xFFFFFFu;
        };
        auto same_in = [](Buffer& p, Buffer& q, Rect r) {
            Surface a = p.surface(), b = q.surface();
            for (int y = r.y0; y < r.y1; y++)
                for (int x = r.x0; x < r.x1; x++)
                    if (a.row(y)[x] != b.row(y)[x])
                        return false;
            return true;
        };
        auto planes = [](const eng::Instance& p, const eng::Instance& q) {
            return p.e() == q.e() && p.ids() == q.ids() && p.age() == q.age() && p.cover() == q.cover() &&
                   p.settled() == q.settled() && p.inited() == q.inited();
        };
        auto lead_run = [&](Rect rect, bool argb, auto frame_at, int64_t end, const std::vector<Surface>& src,
                            uint32_t ground, int64_t lead) {
            eng::Instance one(rect, argb), two(rect, argb), dirty(rect, argb);
            Buffer d1, d2, d3, scratch;
            noise(d1, 1);
            noise(d2, 1);
            noise(d3, 2);
            noise(scratch, 3);
            eng::Frame f0, f;
            frame_at(0, f0);
            const int64_t t0 = 1000;
            one.begin(t0, false);
            two.begin(t0, false);
            dirty.begin(t0, false);
            two.advance(t0, t0);
            two.compose(scratch.surface(), f0, src, ground, kPortraitW);
            bool ok = true;
            for (int n = 0; ok; n++) {
                int64_t s = lead + (int64_t)n * 1000 / 60;
                if (s > end)
                    break;
                frame_at(s, f);
                one.advance(t0, t0 + s);
                two.advance(t0, t0 + s);
                dirty.advance(t0, t0 + s);
                one.compose(d1.surface(), f, src, ground, kPortraitW, one.inited() ? nullptr : &f0);
                two.compose(d2.surface(), f, src, ground, kPortraitW);
                dirty.compose(d3.surface(), f, src, ground, kPortraitW, dirty.inited() ? nullptr : &f0);
                ok = d1.pix == d2.pix && planes(one, two) && planes(one, dirty) && same_in(d1, d3, rect);
            }
            return ok && one.seed_frames() == 1 && dirty.seed_frames() == 1 && two.seed_frames() == 0;
        };
        Buffer pa, pb;
        Surface sa = pa.surface(), sb = pb.surface();
        for (int y = 0; y < sa.h; y++)
            for (int x = 0; x < sa.w; x++) {
                sa.row(y)[x] = (x / 8 + y / 12) % 3 ? kGround : kInk;
                sb.row(y)[x] = (x / 10 + y / 7) % 4 ? kGround : 0x80A0C0u;
            }
        Rect pr{kPaneLeftEdge, 0, kPortraitW, kPortraitH};
        bool pane = true;
        for (bool collapse : {false, true})
            for (int64_t lead : {1, 8, 16}) {
                SlidePlan sp;
                sp.target = kPaneSplitWide;
                sp.moves.collapse = collapse;
                slide_lang(sp, pane_surface());
                int64_t fold = sp.moves.fold;
                auto at_u = [&](int64_t s, eng::Frame& f) {
                    pane_frame_at(sp, std::min(s, fold), kPortraitW, kPortraitH, f);
                };
                bool ok = lead_run(pr, false, at_u, lead + 100, {sa, sb}, kGround, lead);
                if (!ok)
                    std::printf("  pane %s lead %lld differs\n", collapse ? "collapse" : "expand", (long long)lead);
                pane = pane && ok;
            }
        check(pane, "engine: a pane fold whose input leads its first frame by 1, 8 or 16 ms (expand and "
                    "collapse) draws and keeps the same state as its start frame composed alone at t0 into another "
                    "target, and the same state over a different target, its start frame composed as state only");

        Buffer qa, qb;
        Surface ua = qa.surface(), ub = qb.surface();
        for (int y = 0; y < 32; y++)
            for (int x = 0; x < 192; x++) {
                ua.row(y)[x] = (x / 6 + y / 5) % 3 ? 0xFF202020u : 0xFF808080u;
                ub.row(y)[x] = (x / 9 + y / 4) % 2 ? 0xFF202020u : 0xFFC0A060u;
            }
        auto push_at = [](int64_t s, eng::Frame& f) {
            int x = 192 - (int)std::min<int64_t>(192, s * 192 / 300);
            f = eng::Frame{};
            for (int k = 0; k < 2; k++) {
                eng::Block b;
                b.id = (uint16_t)(k + 1);
                b.ink = k ? eng::Ink::Art : eng::Ink::Text;
                b.z = k;
                b.source = k;
                b.src = {0, 0, 192, 32};
                f.blocks.push_back(b);
                eng::Shade sh;
                sh.step = k ? eng::kArtStep : eng::kTextStep;
                f.shades.push_back(sh);
                eng::Pose q;
                q.block = k;
                q.rect = k ? Rect{x, 0, x + 192, 32} : Rect{x - 192, 0, x, 32};
                q.clip = {0, 0, 192, 32};
                f.poses.push_back(q);
            }
        };
        bool argb = true;
        for (int64_t lead : {1, 8, 16})
            argb = argb && lead_run({0, 0, 192, 32}, true, push_at, 450, {ua, ub}, 0xFF202020u, lead);
        check(argb, "engine: an ARGB push whose input leads its first frame by 1, 8 or 16 ms draws and keeps the same "
                    "state as its start frame composed alone at t0 into another target, its start frame composed as "
                    "state only");

        bool reused = true;
        for (int how = 0; how < 3; how++) {
            SlidePlan sp;
            sp.target = kPaneSplitWide;
            slide_lang(sp, pane_surface());
            SlidePlan cp = sp;
            cp.moves.collapse = true;
            int64_t fold = sp.moves.fold;
            eng::Instance kept(pr), fresh(pr);
            Buffer dk, df;
            noise(dk, 4);
            noise(df, 5);
            eng::Frame f, f0;
            int64_t t0 = 1000, last = how == 0 ? fold + eng::kTailMs : how == 1 ? 300 + eng::kTailMs : 150;
            kept.begin(t0, false);
            for (int64_t s = 0; s <= last; s += 17) {
                int64_t u = std::min(s, fold);
                if (how == 1 && s >= 150) {
                    if (!kept.reversed())
                        kept.begin(t0 + s, true);
                    u = std::max<int64_t>(0, 300 - s);
                }
                pane_frame_at(sp, u, kPortraitW, kPortraitH, f);
                kept.advance(kept.t0(), t0 + s);
                kept.compose(dk.surface(), f, {sa, sb}, kGround, kPortraitW);
            }
            int64_t t1 = 5000;
            pane_frame_at(cp, 0, kPortraitW, kPortraitH, f0);
            kept.begin(t1, false);
            fresh.begin(t1, false);
            bool ok = true;
            for (int64_t s = 8; s <= fold + eng::kTailMs && ok; s += 17) {
                pane_frame_at(cp, std::min(s, fold), kPortraitW, kPortraitH, f);
                kept.advance(t1, t1 + s);
                fresh.advance(t1, t1 + s);
                kept.compose(dk.surface(), f, {sa, sb}, kGround, kPortraitW, kept.inited() ? nullptr : &f0);
                fresh.compose(df.surface(), f, {sa, sb}, kGround, kPortraitW, fresh.inited() ? nullptr : &f0);
                ok = planes(kept, fresh) && same_in(dk, df, pr);
            }
            if (!ok)
                std::printf("  reused after %s differs\n", how == 0 ? "a fold" : how == 1 ? "a reversal" : "an input");
            reused = reused && ok;
        }
        check(reused, "engine: a collapse begun on the pane instance after a landed expand, a reversed landing or an "
                      "expand cut by an input at 150 ms draws and keeps the same state as on a fresh instance");

        auto clipped = [](int x0) {
            eng::Frame f;
            eng::Block b;
            b.id = 20;
            b.src = {0, 0, 192, 32};
            f.blocks = {b};
            f.shades = {eng::Shade{}};
            eng::Pose q;
            q.rect = {0, 0, 192, 32};
            q.clip = {x0, 0, 192, 32};
            f.poses = {q};
            return f;
        };
        eng::Frame wide = clipped(0), narrow = clipped(64);
        Rect nr{0, 0, 192, 32};
        eng::Instance one(nr), two(nr);
        Buffer d1, d2, scratch;
        noise(d1, 6);
        noise(d2, 6);
        one.begin(1000, false);
        two.begin(1000, false);
        two.advance(1000, 1000);
        two.compose(scratch.surface(), wide, {sa}, kGround, 192);
        one.advance(1000, 1008);
        two.advance(1000, 1008);
        one.compose(d1.surface(), narrow, {sa}, kGround, 192, &wide);
        two.compose(d2.surface(), narrow, {sa}, kGround, 192);
        check(!same_in(d1, d2, {0, 0, 64, 32}) && same_in(d1, d2, {64, 0, 192, 32}) && planes(one, two) &&
                  one.seed_frames() == 0,
              "engine: a start frame clipped from x 0 before a first frame clipped from x 64 leaves its draw in "
              "[0, 64), so composing the start frame alone into another target differs there and only there (the "
              "start is drawn, not state only, since the frame does not cover it)");
    }
    {
        Buffer big, small(100, 20), out;
        paint(big, {0, 0, big.w, big.h}, kInk);
        paint(small, {0, 0, 100, 20}, kInk);
        auto add = [](eng::Frame& f, uint16_t id, int source, Rect src, Rect rect, Rect clip, const eng::Shade& sh) {
            eng::Block b;
            b.id = id;
            b.source = source;
            b.src = src;
            f.blocks.push_back(b);
            f.shades.push_back(sh);
            eng::Pose q;
            q.block = (int)f.blocks.size() - 1;
            q.rect = rect;
            q.clip = clip;
            f.poses.push_back(q);
        };
        Rect all{0, 0, 192, 32};
        eng::Shade lean;
        lean.t = 100 << 8;
        eng::Frame f;
        add(f, 1, 0, {0, 0, 64, 8}, {10, 0, 74, 8}, {20, 0, 60, 32}, eng::Shade{});
        add(f, 2, 0, {0, 8, 80, 16}, {160, 8, 240, 16}, all, lean);
        add(f, 3, 1, {60, 0, 120, 8}, {0, 16, 60, 24}, all, eng::Shade{});
        add(f, 4, 1, {0, 16, 40, 24}, {100, 16, 140, 24}, all, eng::Shade{});
        add(f, 5, 0, {-10, 24, 30, 32}, {140, 24, 180, 32}, all, eng::Shade{});
        add(f, (uint16_t)(6 | eng::kRingBit), 0, {0, 24, 40, 32}, {0, 24, 40, 32}, all, lean);
        eng::Instance in(all);
        in.begin(0, false);
        in.advance(0, 0);
        in.compose(out.surface(), f, {big.surface(), small.surface()}, kGround, 192);
        auto own = [&](int x, int y) { return in.ids()[at(in, x, y)]; };
        auto e_at = [&](int x, int y) { return (int)in.e()[at(in, x, y)]; };
        auto cov = [&](int x, int y) { return in.cover()[at(in, x, y)]; };
        bool spans = own(19, 4) == 0 && own(20, 4) == 1 && own(59, 4) == 1 && own(60, 4) == 0 && own(159, 12) == 0 &&
                     own(160, 12) == 2 && own(191, 12) == 2 && own(39, 20) == 3 && own(40, 20) == 0 &&
                     own(100, 19) == 4 && own(100, 20) == 0 && own(149, 28) == 0 && own(150, 28) == 5 &&
                     own(179, 28) == 5 && own(180, 28) == 0;
        bool covers = cov(20, 4) && !cov(19, 4) && cov(150, 28) && !cov(149, 28) && cov(39, 20) && !cov(40, 20);
        bool state = e_at(20, 4) == eng::kFull && e_at(160, 12) == lean.t && e_at(19, 4) == 0 &&
                     in.age()[at(in, 19, 4)] == eng::kAgeMs && in.age()[at(in, 20, 4)] == 0 &&
                     own(10, 28) == (uint16_t)(6 | eng::kRingBit) && e_at(10, 28) == eng::kFull;
        Surface os = out.surface();
        int a = (int)(((int64_t)lean.t * 256 + eng::kFull / 2) / eng::kFull);
        bool drawn = os.row(4)[30] == kInk && os.row(4)[15] == kGround &&
                     os.row(12)[170] == blend_px(kGround, kInk, a) && os.row(20)[45] == kGround &&
                     os.row(28)[145] == kGround && os.row(28)[10] == kInk;
        eng::Frame h;
        add(h, 8, 0, {0, 0, 96, 8}, {0, 0, 96, 8}, all, lean);
        add(h, 9, 0, {0, 0, 96, 8}, {48, 0, 144, 8}, all, eng::Shade{});
        add(h, 10, 0, {0, 0, 40, 8}, {0, 8, 100, 16}, all, eng::Shade{});
        eng::Instance lap(all);
        lap.begin(0, false);
        lap.advance(0, 0);
        lap.compose(out.surface(), h, {big.surface()}, kGround, 192);
        auto lid = [&](int x, int y) { return lap.ids()[at(lap, x, y)]; };
        auto le = [&](int x, int y) { return (int)lap.e()[at(lap, x, y)]; };
        bool later = lid(20, 4) == 8 && le(20, 4) == lean.t && lid(47, 4) == 8 && lid(48, 4) == 9 &&
                     le(48, 4) == eng::kFull && lid(120, 4) == 9 && le(120, 4) == eng::kFull && lid(144, 4) == 0;
        bool sized = lid(39, 12) == 10 && lid(40, 12) == 0 && lid(99, 12) == 0 && !lap.cover()[at(lap, 40, 12)];
        check(later && sized, "engine: of two overlapping blocks of different t at one z in one row the later pose "
                              "owns the overlap at its own t, and a pose rect wider than its block's src marks only the "
                              "src's width");
        check(spans && covers && state && drawn,
              "engine: a first pass marks, covers and draws exactly the pixels a pose's clip, the instance rect, "
              "the source's right and bottom edges and a negative source origin leave, at e = t (a ring at full) "
              "and age 0");

    }
    {
        bool exact = true;
        uint32_t row[257], want[257];
        for (int a = 0; a <= 256; a++)
            for (uint32_t g = 0; g < 256; g++) {
                uint32_t ground = g << 24 | g << 16 | (255 - g) << 8 | g;
                for (uint32_t k = 0; k < 257; k++) {
                    uint32_t v = k & 0xFF;
                    row[k] = (v ^ 0xA5) << 24 | v << 16 | v << 8 | (255 - v);
                    want[k] = blend_px(ground, row[k], a);
                }
                eng::blend_row(row, 257, ground, a);
                for (int k = 0; k < 257; k++)
                    exact = exact && row[k] == want[k];
            }
        check(exact, "engine: the row blend equals blend_px for every a from 0 to 256, every 8 bit ground and source "
                     "value in every channel and both channel orders, in the vector body and the tail");
    }
    {
        uint32_t seed = 777;
        auto rnd = [&](int n) {
            seed = seed * 1664525u + 1013904223u;
            return (int)((seed >> 8) % (uint32_t)n);
        };
        uint32_t seed2 = 991;
        auto pick = [&](int n) {
            seed2 = seed2 * 1664525u + 1013904223u;
            return (int)((seed2 >> 8) % (uint32_t)n);
        };
        Buffer s0, s1, s2(192, 24);
        Surface u0 = s0.surface(), u1 = s1.surface(), u2 = s2.surface();
        for (int y = 0; y < 64; y++)
            for (int x = 0; x < 192; x++) {
                u0.row(y)[x] = (x / 5 + y / 3) % 3 ? 0xFF111111u : 0xFFC0C0C0u;
                u1.row(y)[x] = (x / 7 + y / 4) % 2 ? 0xFF111111u : 0xFF80A0C0u;
                if (y < 24)
                    u2.row(y)[x] = (x / 3 + y / 5) % 2 ? 0xFF111111u : 0xFFA0C080u;
            }
        struct Mover {
            uint16_t id;
            int x, y, w, h, vx, vy, source, z, from, to, cv;
            Rect clip;
            bool tile;
            eng::Shade sh;
        };
        bool same = true, engaged = true;
        int frames = 0, trusted = 0, fill_frames = 0, short_frames = 0, held = 0, vscans = 0, vfails = 0;
        for (int trial = 0; trial < 24; trial++) {
            bool argb = trial % 4 == 3, grow = trial % 3 == 1;
            Rect rect{0, 0, 192, 64};
            eng::Instance px(rect, argb), sg(rect, argb), tr(rect, argb);
            sg.set_segments(true);
            tr.set_segments(true);
            tr.trust_dst(true);
            sg.set_verify(true);
            tr.set_verify(true);
            Buffer d1, d2, d3;
            std::vector<Mover> ms;
            int count = 2 + rnd(5);
            for (int k = 0; k < count; k++) {
                Mover m;
                m.id = (uint16_t)(30 + k);
                m.w = 16 + rnd(80);
                m.h = 8 + rnd(40);
                m.x = rnd(192) - 40;
                m.y = rnd(64) - 10;
                m.vx = rnd(13) - 6;
                m.vy = rnd(7) - 3;
                m.source = rnd(2);
                m.z = rnd(3);
                m.from = rnd(10);
                m.to = 12 + rnd(20);
                m.tile = rnd(3) == 0;
                m.clip = rect;
                m.cv = 0;
                if (trial % 6 == 3 || pick(4) == 0) {
                    m.clip = {4 + pick(40), pick(16), 120 + pick(73), 40 + pick(25)};
                    m.cv = pick(3) - 1;
                }
                if (k == 0 || pick(4) == 0)
                    m.source = 2;
                m.sh.t = rnd(3) == 0 ? (100 << 8) : rnd(2) ? (153 << 8) : eng::kFull;
                m.sh.end = m.sh.t;
                m.sh.step = rnd(2) ? eng::kTextStep : eng::kArtStep;
                m.sh.dep = rnd(4) == 0;
                m.sh.sigma = rnd(5) == 0 ? 154 : 256;
                if (rnd(5) == 0)
                    m.id |= eng::kRingBit;
                ms.push_back(m);
            }
            int back_at = rnd(3) ? 14 + rnd(6) : -1, nf = 0;
            int64_t t0 = 0;
            px.begin(0, false);
            sg.begin(0, false);
            tr.begin(0, false);
            for (int n = 0; n < 30 && same; n++) {
                int64_t now = n * 17;
                if (n == back_at) {
                    t0 = now;
                    px.begin(now, true);
                    sg.begin(now, true);
                    tr.begin(now, true);
                    for (Mover& m : ms) {
                        m.vx = -m.vx;
                        m.vy = -m.vy;
                    }
                }
                eng::Frame f;
                for (const Mover& m : ms) {
                    if (n < m.from || n > m.to)
                        continue;
                    int k = n < back_at || back_at < 0 ? n : 2 * back_at - n;
                    eng::Block b;
                    b.id = m.id;
                    b.z = m.z;
                    b.source = m.source;
                    b.tile = m.tile;
                    b.src = {0, 0, m.w, m.h};
                    f.blocks.push_back(b);
                    f.shades.push_back(m.sh);
                    eng::Pose q;
                    q.block = (int)f.blocks.size() - 1;
                    q.rect = {m.x + m.vx * k, m.y + m.vy * k, m.x + m.vx * k + m.w, m.y + m.vy * k + m.h};
                    q.clip = {m.clip.x0, m.clip.y0, m.clip.x1 + m.cv * 2 * n, m.clip.y1};
                    f.poses.push_back(q);
                }
                int clip = grow ? std::min(192, 60 + 10 * n) : 192;
                bool right = !f.poses.empty(), skips = false;
                for (const eng::Pose& q : f.poses) {
                    right = right && q.clip.x0 > 0;
                    Rect vis = q.rect.intersect(q.clip).intersect(rect).intersect({0, 0, clip, 64});
                    skips = skips || (f.blocks[(size_t)q.block].source == 2 && !vis.empty() && vis.y1 > q.rect.y0 + 24);
                }
                fill_frames += right;
                short_frames += skips;
                px.advance(t0, now);
                sg.advance(t0, now);
                tr.advance(t0, now);
                px.compose(d1.surface(), f, {u0, u1, u2}, argb ? 0xFF111111u : 0x111111u, clip);
                sg.compose(d2.surface(), f, {u0, u1, u2}, argb ? 0xFF111111u : 0x111111u, clip);
                tr.compose(d3.surface(), f, {u0, u1, u2}, argb ? 0xFF111111u : 0x111111u, clip);
                same = same && d1.pix == d2.pix && px.e() == sg.e() && px.ids() == sg.ids() && px.age() == sg.age() &&
                       px.cover() == sg.cover() && px.settled() == sg.settled() && d1.pix == d3.pix &&
                       px.e() == tr.e() && px.ids() == tr.ids() && px.age() == tr.age() && px.cover() == tr.cover() &&
                       px.settled() == tr.settled() && same_held(px, sg) && same_held(px, tr);
                if (!same)
                    std::printf("  segments differ: trial %d frame %d\n", trial, n);
                frames++;
                nf++;
            }
            engaged = engaged && px.segment_frames() == 0 && sg.segment_frames() == nf - 1 && px.trusted_frames() == 0 &&
                      sg.trusted_frames() == 0 && px.held_frames() == sg.held_frames() &&
                      px.held_frames() == tr.held_frames();
            trusted += tr.trusted_frames();
            held += px.held_frames();
            vscans += sg.verify_scans() + tr.verify_scans();
            vfails += sg.verify_fails() + tr.verify_fails() + px.verify_scans();
        }
        check(same && engaged && frames > 500 && trusted > 300 && fill_frames > 0 && short_frames > 0 && held > 0 &&
                  vscans > 0 && vfails == 0,
              "engine: the segment pass, and the segment pass on a trusted persistent target, draw and keep the same "
              "state as the pixel pass over 24 random folds of moving, entering, leaving, tile, ring, dep and ARGB "
              "blocks with Back mid fold, a growing clip, per pose clips (some with a moving right edge, every "
              "pose clipped right of x 0 in some frames) and blocks drawn past the bottom of a source shorter than they "
              "are in some frames, the held id of every pixel no block covers included and the held id rule taking "
              "effect on the same frames in all three, every frame after the first of each fold on segments and most of "
              "them trusted, every Back's lowest e from the runs agreeing with the verify scan");
    }
    {
        bool match = true, before = true, after = true, state = true, counts = true;
        for (int pass = 0; pass < 2; pass++) {
            bool argb = pass == 1;
            Buffer s0, s1;
            Surface u0 = s0.surface(), u1 = s1.surface();
            for (int y = 0; y < 64; y++)
                for (int x = 0; x < 192; x++) {
                    u0.row(y)[x] = (x / 5 + y / 3) % 3 ? 0xFF111111u : 0xFFC0C0C0u;
                    u1.row(y)[x] = (x / 7 + y / 4) % 2 ? 0xFF111111u : 0xFF80A0C0u;
                }
            Rect rect{0, 0, 192, 64};
            uint32_t ground = argb ? 0xFF111111u : 0x111111u;
            eng::Instance px(rect, argb), iv(rect, argb), nv(rect, argb);
            for (eng::Instance* in : {&iv, &nv}) {
                in->set_segments(true);
                in->trust_dst(true);
            }
            Buffer d1, d2, d3, d19;
            for (eng::Instance* in : {&px, &iv, &nv})
                in->begin(0, false);
            const int nf = 40, change = 20;
            const Rect stale{20, 4, 84, 28};
            for (int n = 0; n < nf; n++) {
                if (n == change) {
                    for (int y = 0; y < 24; y++)
                        for (int x = 0; x < 64; x++)
                            u0.row(y)[x] = (x / 3 + y / 2) % 2 ? 0xFF204060u : 0xFFE0D0B0u;
                    iv.invalidate();
                }
                eng::Frame f;
                auto add = [&](uint16_t id, int source, Rect src, int x, int y, int t) {
                    eng::Block b;
                    b.id = id;
                    b.source = source;
                    b.src = src;
                    f.blocks.push_back(b);
                    eng::Shade sh;
                    sh.t = sh.end = t;
                    f.shades.push_back(sh);
                    eng::Pose q;
                    q.block = (int)f.blocks.size() - 1;
                    q.rect = {x, y, x + src.x1 - src.x0, y + src.y1 - src.y0};
                    q.clip = rect;
                    f.poses.push_back(q);
                };
                add(40, 0, {0, 0, 64, 24}, 20, 4, eng::kFull);
                add(41, 1, {64, 0, 128, 24}, 100, 4, 153 << 8);
                add(42, 0, {0, 30, 40, 46}, 4 * n, 40, eng::kFull);
                for (eng::Instance* in : {&px, &iv, &nv})
                    in->advance(0, n * 17);
                px.compose(d1.surface(), f, {u0, u1}, ground, 192);
                iv.compose(d2.surface(), f, {u0, u1}, ground, 192);
                nv.compose(d3.surface(), f, {u0, u1}, ground, 192);
                match = match && d1.pix == d2.pix && px.e() == iv.e() && px.ids() == iv.ids() &&
                        px.age() == iv.age() && px.cover() == iv.cover() && px.settled() == iv.settled();
                state = state && px.e() == nv.e() && px.ids() == nv.ids() && px.age() == nv.age() &&
                        px.cover() == nv.cover();
                if (n < change) {
                    before = before && d1.pix == d3.pix;
                    d19.pix = d3.pix;
                    continue;
                }
                bool kept = d1.pix != d3.pix;
                for (int y = 0; y < 64; y++)
                    for (int x = 0; x < 192; x++) {
                        bool in = x >= stale.x0 && x < stale.x1 && y >= stale.y0 && y < stale.y1;
                        uint32_t v = d3.surface().row(y)[x];
                        kept = kept && v == (in ? d19.surface().row(y)[x] : d1.surface().row(y)[x]);
                    }
                after = after && kept;
            }
            counts = counts && px.trusted_frames() == 0 && nv.trusted_frames() == nf - 1 &&
                     iv.trusted_frames() == nf - 2;
        }
        check(match && state && before && after && counts,
              "engine: a trusted target whose source changes in place mid fold draws and keeps the pixel pass's "
              "state when invalidate() comes before the next compose, in RGB and ARGB, with trust back on the frame "
              "after; without it (the negative control) the target keeps the changed block's frame 19 pixels from that "
              "frame on and matches the pixel pass everywhere else, while e, ids, age and cover still match");
    }
    {
        Buffer src, out;
        paint(src, {0, 0, src.w, src.h}, kGround);
        paint(src, {4, 4, 92, 28}, kInk);
        const uint16_t ring_a = (uint16_t)(30 | eng::kRingBit), ring_b = (uint16_t)(31 | eng::kRingBit);
        const int low = 153 << 8;
        bool foreign = true, same = true, tile = true, own_ring = true, other_ring = true, own_ink = true, stale = true,
             later = true, reversed = true, segs = true;
        for (int mode = 0; mode < 2; mode++) {
            auto run = [&](uint16_t first, int t, int empty, bool reverse, uint16_t second, bool as_tile, int64_t dt) {
                eng::Instance in({0, 0, 128, 32});
                in.set_segments(mode == 1);
                auto frame = [&](uint16_t id, bool tiled) {
                    eng::Frame f;
                    eng::Block b;
                    b.id = id;
                    b.tile = tiled;
                    b.src = {0, 0, 96, 32};
                    f.blocks = {b};
                    eng::Shade sh;
                    sh.t = sh.end = t;
                    f.shades = {sh};
                    eng::Pose q;
                    q.rect = {0, 0, 96, 32};
                    q.clip = {0, 0, 128, 32};
                    f.poses = {q};
                    return f;
                };
                eng::Frame none;
                in.begin(0, false);
                int64_t now = 0;
                for (int n = 0; n < 3; n++, now += 17) {
                    in.advance(0, now);
                    in.compose(out.surface(), frame(first, false), {src.surface()}, kGround, 128);
                }
                for (int n = 0; n < empty; n++, now += 17) {
                    in.advance(0, now);
                    in.compose(out.surface(), none, {src.surface()}, kGround, 128);
                }
                int64_t when = now - 17 + dt, t0 = 0;
                if (reverse) {
                    t0 = when;
                    in.begin(t0, true);
                }
                in.advance(t0, when);
                in.compose(out.surface(), frame(second, as_tile), {src.surface()}, kGround, 128);
                segs = segs && in.segment_frames() == (mode == 1 ? 3 + empty : 0);
                size_t i = at(in, 40, 12);
                return std::make_pair((int)in.e()[i], (in.age()[i] & 0x80) != 0);
            };
            auto f = run(30, eng::kFull, 1, false, 31, false, 20);
            foreign = foreign && f.first == one_step(eng::kArtStep, 20) && f.second;
            auto a = run(30, eng::kFull, 1, false, 30, false, 20);
            same = same && a.first == eng::kFull && !a.second;
            auto t = run(30, eng::kFull, 1, false, 31, true, 20);
            tile = tile && t.first == eng::kFull && !t.second;
            auto r = run(30, low, 1, false, ring_a, false, 20);
            own_ring = own_ring && r.first == std::min(eng::kFull, low + one_step(eng::kRingStep, 20)) && !r.second;
            auto o = run(30, low, 1, false, ring_b, false, 20);
            other_ring = other_ring && o.first == one_step(eng::kRingStep, 20) && o.second;
            auto k = run(ring_a, low, 1, false, 30, false, 20);
            own_ink = own_ink && k.first == low && !k.second;
            auto g = run(30, eng::kFull, 6, false, 30, false, 17);
            stale = stale && g.first == one_step(eng::kArtStep, 17) && g.second;
            auto lf = run(30, eng::kFull, 3, false, 31, false, 17);
            auto ls = run(30, eng::kFull, 3, false, 30, false, 17);
            later = later && lf.first == one_step(eng::kArtStep, 17) && lf.second && ls.first == eng::kFull && !ls.second;
            auto rf = run(30, eng::kFull, 1, true, 31, false, 20);
            auto rs = run(30, eng::kFull, 1, true, 30, false, 20);
            reversed = reversed && rf.first == one_step(eng::kArtStep, 20) && rf.second && rs.first == eng::kFull &&
                       !rs.second;
        }
        check(foreign && same && tile && stale && later && reversed && segs,
              "engine: a different block over plain ground vacated 20 or 51 ms ago starts from ground at one step, "
              "glowing, and the same block resumes its e, also across a rule 2 reversal; a tile block resumes the "
              "screen pixel's e as the grid does; past 100 ms the same block starts at one step too; on the pixel pass "
              "and on segments");
        check(own_ring && other_ring && own_ink,
              "engine: the held id compares with the ring bit masked: a ring over its own block's ink vacated 20 ms "
              "ago resumes from that e, its ink over its vacated ring resumes, another block's ring there starts at "
              "one ring step, glowing; on the pixel pass and on segments");
    }
    {
        Buffer src, out;
        paint(src, {0, 0, src.w, src.h}, kGround);
        paint(src, {4, 4, 92, 28}, kInk);
        bool counted = true;
        for (int mode = 0; mode < 2; mode++)
            for (bool dep : {false, true}) {
                eng::Instance in({0, 0, 128, 32});
                in.set_segments(mode == 1);
                auto frame = [&](uint16_t id) {
                    eng::Frame f;
                    eng::Block b;
                    b.id = id;
                    b.src = {0, 0, 96, 32};
                    f.blocks = {b};
                    eng::Shade sh;
                    sh.dep = id == 31 && dep;
                    f.shades = {sh};
                    eng::Pose q;
                    q.rect = {0, 0, 96, 32};
                    q.clip = {0, 0, 128, 32};
                    f.poses = {q};
                    return f;
                };
                in.begin(0, false);
                for (int n = 0; n < 3; n++) {
                    in.advance(0, 17 * n);
                    in.compose(out.surface(), frame(30), {src.surface()}, kGround, 128);
                }
                in.advance(0, 51);
                in.compose(out.surface(), eng::Frame{}, {src.surface()}, kGround, 128);
                in.advance(0, 71);
                in.compose(out.surface(), frame(31), {src.surface()}, kGround, 128);
                size_t i = at(in, 40, 12);
                bool drawn = dep ? in.e()[i] == eng::kFull : in.e()[i] == one_step(eng::kArtStep, 20);
                counted = counted && drawn && in.held_frames() == (dep ? 0 : 1);
            }
        check(counted, "engine: held_frames() counts a pass only where the held id started a pixel from ground: a "
                       "different block over plain ground vacated 20 ms ago counts, the same block arriving as a "
                       "departing block (drawn at its T) does not; on the pixel pass and on segments");
    }
    {
        Buffer src, out;
        paint(src, {0, 0, src.w, src.h}, kGround);
        paint(src, {4, 4, 92, 28}, kInk);
        const uint16_t ring = (uint16_t)(31 | eng::kRingBit);
        auto frame = [](const std::vector<std::pair<uint16_t, Rect>>& on, bool dep) {
            eng::Frame f;
            for (const std::pair<uint16_t, Rect>& o : on) {
                eng::Block b;
                b.id = o.first;
                b.src = {o.second.x0, 0, o.second.x1, 32};
                eng::Shade sh;
                sh.dep = dep;
                eng::Pose q;
                q.block = (int)f.blocks.size();
                q.rect = o.second;
                q.clip = {0, 0, 128, 32};
                f.blocks.push_back(b);
                f.shades.push_back(sh);
                f.poses.push_back(q);
            }
            return f;
        };
        bool excluded = true;
        for (int mode = 0; mode < 2; mode++)
            for (int kind = 0; kind < 4; kind++) {
                bool edge = kind >= 2, control = kind & 1;
                eng::Instance in({0, 0, 128, 32});
                in.set_segments(mode == 1);
                in.begin(0, false);
                for (int n = 0; n < 3; n++) {
                    in.advance(0, 17 * n);
                    in.compose(out.surface(), frame({{30, {0, 0, 96, 32}}}, false), {src.surface()}, kGround, 128);
                }
                in.advance(0, 51);
                if (edge)
                    in.compose(out.surface(), frame({{32, {0, 0, 32, 32}}, {31, {32, 0, 96, 32}}}, false),
                               {src.surface()}, kGround, 32);
                else
                    in.compose(out.surface(), eng::Frame{}, {src.surface()}, kGround, 128);
                int64_t t0 = edge || control ? 0 : 71;
                if (t0)
                    in.begin(t0, true);
                in.advance(t0, 71);
                if (edge)
                    in.compose(out.surface(),
                               frame({{32, {0, 0, 32, 32}}, {31, {32 + control, 0, 96 + control, 32}}}, false),
                               {src.surface()}, kGround, 128);
                else
                    in.compose(out.surface(), frame({{ring, {0, 0, 96, 32}}}, true), {src.surface()}, kGround, 128);
                int e = in.e()[at(in, 40, 12)];
                int want = !control ? eng::kFull : edge ? one_step(eng::kArtStep, 20) : one_step(eng::kRingStep, 20);
                excluded = excluded && e == want && in.held_frames() == (control ? 1 : 0);
            }
        check(excluded, "engine: held_frames() leaves out a held start the rule draws without the held id: a departing "
                        "block's ring under a reversal (drawn at its back value) and a still block's columns a growing "
                        "clip reveals (the edge cap, drawn at T) do not count, the same ring without the reversal and "
                        "the same block moved by 1 px count; on the pixel pass and on segments");
    }
    {
        struct Pv {
            int v;
            bool operator==(const Pv& o) const { return v == o.v; }
        };
        RowRuns<Pv> rr;
        rr.begin(3);
        rr.row(0);
        rr.emit(0, 4, {1});
        rr.emit(4, 8, {1});
        rr.emit(8, 8, {3});
        rr.emit(8, 10, {2});
        rr.row(1);
        rr.emit(0, 10, {1});
        rr.row(2);
        rr.emit(10, 12, {1});
        rr.end(3);
        std::vector<std::vector<int>> got;
        auto take = [&](int r, int u, int v, int shift) {
            got.clear();
            rr.walk(r, u, v, shift, [&](int a, int b, const RowRuns<Pv>::Run& g) { got.push_back({a, b, g.p.v}); });
            return got;
        };
        bool rows = rr.last(0) - rr.first(0) == 2 && rr.first(0)[0].x1 == 8 && rr.first(0)[1].p.v == 2 &&
                    rr.last(1) - rr.first(1) == 1 && rr.first(1)[0].x1 == 10 && rr.last(2) - rr.first(2) == 1 &&
                    rr.first(2)[0].x0 == 10 && rr.first(2)[0].x1 == 12;
        bool walks = take(0, 3, 9, 0) == std::vector<std::vector<int>>{{3, 8, 1}, {8, 9, 2}} &&
                     take(0, 3, 9, 2) == std::vector<std::vector<int>>{{3, 9, 1}} &&
                     take(1, 0, 10, 0) == std::vector<std::vector<int>>{{0, 10, 1}};
        std::vector<std::pair<int, int>> cuts;
        cut_at(0, 10, std::vector<int>{3, 7, 12, 0}, [&](int a, int b) { cuts.push_back({a, b}); });
        bool cut = cuts == std::vector<std::pair<int, int>>{{0, 3}, {3, 7}, {7, 10}};
        check(rows && walks && cut,
              "rowruns: runs merge equal neighbours within a row only, a run that starts a row where the row before "
              "ended on an equal run starting its own run, a walk yields each run's part of [u, v) shifted by the read "
              "offset, and cuts split at the listed points inside [u, v)");
    }
    {
        Buffer src;
        paint(src, {0, 0, src.w, src.h}, kGround);
        paint(src, {10, 10, 120, 90}, kInk);
        paint(src, {200, 40, 300, 160}, 0x40C080);
        auto frame_at = [](int dx, Rect rect, eng::Frame& f) {
            f = eng::Frame{};
            const uint16_t ids[] = {5, 6, (uint16_t)(7 | eng::kRingBit), 9};
            for (int k = 0; k < 4; k++) {
                eng::Block b;
                b.id = ids[k];
                b.z = k;
                b.src = {40 * k, 10, 40 * k + 160, 170};
                f.blocks.push_back(b);
                eng::Pose q;
                q.block = k;
                q.rect = {120 + 60 * k + dx, rect.y0 + 20 + 30 * k, 280 + 60 * k + dx, rect.y0 + 180 + 30 * k};
                q.clip = rect;
                f.poses.push_back(q);
            }
            f.shades.assign(3, eng::Shade{});
            f.shades[1].t = eng::kFull / 2;
        };
        auto run = [&](bool prep, Rect rect, int clip) {
            eng::Instance fast(rect), pix(rect);
            fast.set_segments(true);
            fast.set_verify(true);
            Buffer d1, d2;
            eng::Frame f0, f;
            frame_at(0, rect, f0);
            const int64_t t0 = 1000;
            if (prep)
                while (!fast.prepared())
                    fast.prepare(50);
            fast.begin(t0, false);
            pix.begin(t0, false);
            bool ok = true;
            for (int n = 1; ok && n < 6; n++) {
                frame_at(4 * n, rect, f);
                fast.advance(t0, t0 + 16 * n);
                pix.advance(t0, t0 + 16 * n);
                fast.compose(d1.surface(), f, {src.surface()}, kGround, clip, fast.inited() ? nullptr : &f0);
                pix.compose(d2.surface(), f, {src.surface()}, kGround, clip, pix.inited() ? nullptr : &f0);
                ok = d1.pix == d2.pix && fast.e() == pix.e() && fast.ids() == pix.ids() && fast.age() == pix.age() &&
                     fast.held() == pix.held() && fast.act_lo() == pix.act_lo() && fast.act_hi() == pix.act_hi();
            }
            return ok && fast.seed_frames() == 1 && pix.seed_frames() == 1 && fast.verify_scans() > 0 &&
                   fast.verify_fails() == 0;
        };
        bool all = true;
        for (bool prep : {false, true})
            for (Rect rect : {Rect{100, 0, 640, 360}, Rect{100, 0, 800, 360}, Rect{100, 600, 640, 800}})
                all = all && run(prep, rect, 640);
        check(all, "S1: a state only start on segments builds its planes and runs from the spans (a ring id, a block "
                   "with no shade), equal to the per pixel start of a plain instance on every frame (planes and act), "
                   "its runs equal to seg_build's under verify, also after an L1 prepared begin, a clip_x of 640 "
                   "under a wider rect and a rect taller than dst");
        Buffer thin(200, src.h);
        paint(thin, {0, 0, thin.w, thin.h}, kInk);
        auto seed_case = [&](char want, bool& same) {
            Rect rect{100, 0, 640, 360};
            eng::Instance fast(rect), pix(rect);
            fast.set_segments(true);
            fast.set_verify(true);
            Buffer d1, d2;
            eng::Frame f0, s0, f;
            frame_at(0, rect, f0);
            frame_at(want == 'f' ? 40 : 0, rect, s0);
            std::vector<Surface> in = {want == 's' ? thin.surface() : src.surface()};
            const int64_t t0 = 1000;
            while (!fast.prepared())
                fast.prepare(50);
            if (want != '-') {
                uint32_t g = want == 'g' ? 0x123456u : kGround;
                int clip = want == 'c' ? 300 : 640;
                bool done = fast.prepare_start(d1.surface(), s0, {src.surface()}, g, clip, 40);
                for (int k = 0; !done && k < 100 && (want != 'p' || k < 1); k++)
                    done = fast.prepare_start(d1.surface(), s0, {src.surface()}, g, clip, 40);
            }
            fast.begin(t0, false);
            pix.begin(t0, false);
            same = true;
            for (int n = want == 'n' ? 0 : 1; same && n < 6; n++) {
                frame_at(4 * n, want == 'm' && n == 1 ? Rect{rect.x0 + 60, rect.y0, rect.x1, rect.y1} : rect, f);
                fast.advance(t0, t0 + 16 * n);
                pix.advance(t0, t0 + 16 * n);
                fast.compose(d1.surface(), f, in, kGround, 640, fast.inited() ? nullptr : &f0);
                pix.compose(d2.surface(), f, in, kGround, 640, pix.inited() ? nullptr : &f0);
                same = d1.pix == d2.pix && fast.e() == pix.e() && fast.ids() == pix.ids() && fast.age() == pix.age() &&
                       fast.held() == pix.held() && fast.act_lo() == pix.act_lo() && fast.act_hi() == pix.act_hi();
            }
            same = same && fast.verify_fails() == 0 && !fast.start_busy();
            return fast.ps();
        };
        bool seeds = true;
        std::string letters;
        for (char want : {'a', 'p', 'f', 'g', 's', 'c', 'm', 'n', '-'}) {
            bool same = false;
            letters += seed_case(want, same);
            seeds = seeds && same;
        }
        check(seeds, "seed: a push start prepared at idle (prepare_start, row slices) and adopted by frame 0's "
                     "compose, or discarded by a press after one slice, a changed start frame, ground, source, clip, "
                     "a frame 0 whose marks fail or a first pass with no start, equal on every frame (pixels, planes "
                     "and act) to the per pixel start of a plain instance, with no seed left");
        check(letters == "apfgscmn-",
              ("seed letters: a adopted, p partial, f frame, g ground, s sources, c clip, m marks, n no start, - none "
               "prepared (got " +
               letters + ")")
                  .c_str());
        auto row_at = [](int dx, Rect panel, eng::Frame& f) {
            f = eng::Frame{};
            eng::Block b;
            b.id = 5;
            b.src = {0, 10, 160, 170};
            f.blocks.push_back(b);
            eng::Pose q;
            q.block = 0;
            q.rect = {300 + dx, 20, 460 + dx, 180};
            q.clip = {300, 0, 640, 360};
            f.poses.push_back(q);
            f.shades.assign(1, eng::Shade{});
            f.ground = panel;
        };
        bool panel_eq = true, panel_flat = true, panel_px = true, panel_cover = true, trail = false;
        {
            Rect rect{100, 0, 640, 360};
            int gw = rect.x1 - rect.x0;
            eng::Instance fg(rect), pg(rect), fn(rect);
            fg.set_segments(true);
            fn.set_segments(true);
            fg.trust_dst(true);
            fn.trust_dst(true);
            fg.set_verify(true);
            Buffer dg, dp, dn;
            eng::Frame s0, sn, f, g;
            row_at(0, rect, s0);
            row_at(0, Rect{}, sn);
            const int64_t t0 = 1000, t1 = t0 + 96;
            for (eng::Instance* in : {&fg, &pg, &fn})
                in->begin(t0, false);
            for (int n = 1; n <= 10; n++) {
                bool back = n > 5;
                if (n == 6)
                    for (eng::Instance* in : {&fg, &pg, &fn})
                        in->begin(t1, true);
                int k = back ? 10 - n : n, y1 = 360 - 30 * k;
                row_at(8 * k, Rect{100, 0, 640, y1}, f);
                row_at(8 * k, Rect{}, g);
                for (eng::Instance* in : {&fg, &pg, &fn})
                    in->advance(back ? t1 : t0, back ? t1 + 16 * (n - 5) : t0 + 16 * n);
                fg.compose(dg.surface(), f, {src.surface()}, kGround, 640, fg.inited() ? nullptr : &s0);
                pg.compose(dp.surface(), f, {src.surface()}, kGround, 640, pg.inited() ? nullptr : &s0);
                fn.compose(dn.surface(), g, {src.surface()}, kGround, 640, fn.inited() ? nullptr : &sn);
                panel_eq = panel_eq && dg.pix == dp.pix && fg.e() == pg.e() && fg.ids() == pg.ids() &&
                           fg.age() == pg.age() && fg.held() == pg.held() && fg.act_lo() == pg.act_lo() &&
                           fg.act_hi() == pg.act_hi();
                panel_flat = panel_flat && fg.e() == fn.e() && fg.ids() == fn.ids() && fg.age() == fn.age() &&
                             fg.held() == fn.held();
                Surface sg = dg.surface(), sp = dn.surface();
                for (int y = rect.y0; panel_px && y < rect.y1; y++)
                    for (int x = 0; panel_px && x < rect.x1; x++) {
                        bool strip = x >= rect.x0 && x < 300;
                        panel_px = strip ? y >= y1 || sg.at(x, y) == kGround : sg.at(x, y) == sp.at(x, y);
                    }
                const std::vector<uint8_t> &cg = fg.cover(), &cn = fn.cover();
                for (int y = rect.y0; panel_cover && y < rect.y1; y++)
                    for (int x = 0; panel_cover && x < gw; x++) {
                        size_t i = (size_t)(y - rect.y0) * gw + (size_t)x;
                        panel_cover = y < y1 ? cg[i] != 0 : cg[i] == cn[i];
                    }
                for (int y = 20; n == 5 && y < 180; y++)
                    for (int x = 300; x < 340; x++)
                        trail = trail || fg.held()[(size_t)(y - rect.y0) * gw + (size_t)(x - rect.x0)] == 5;
            }
            panel_eq = panel_eq && fg.verify_fails() == 0;
        }
        check(panel_eq && panel_flat && panel_px && panel_cover && trail,
              "ground rect: a row moving right and back (a rule 2 reversal) over its own trail inside a ground rect "
              "whose bottom edge moves every frame, on segments with a trusted target, equals the pixel path "
              "(pixels, planes, act), keeps the e, ids, age and held ids of the same frames without the rect (plain "
              "ground) with its held trail inside the rect, draws ground over the rect left of the row's clip and "
              "every pixel right of it as without the rect, and sets cover() over the rect and nowhere else beyond "
              "the poses, with 0 verify fails");
        auto widened = [&](Rect sr, Rect fr, bool want, bool l5) {
            Rect rect{100, 0, 640, 360};
            eng::Instance in(rect), pix(rect);
            in.set_segments(true);
            Buffer d, dp;
            paint(d, {0, 0, d.w, d.h}, kInk);
            paint(dp, {0, 0, dp.w, dp.h}, kInk);
            eng::Frame s, f;
            row_at(0, sr, s);
            row_at(4, fr, f);
            in.begin(1000, false);
            in.advance(1000, 1016);
            if (l5) {
                in.trust_dst(true);
                in.set_verify(true);
                in.adopt_source(0);
                in.adopt_start(true);
            }
            in.compose(d.surface(), f, {src.surface()}, kGround, 640, &s);
            pix.begin(1000, false);
            pix.advance(1000, 1000);
            pix.compose(dp.surface(), s, {src.surface()}, kGround, 640);
            pix.advance(1000, 1016);
            pix.compose(dp.surface(), f, {src.surface()}, kGround, 640);
            Surface sd = d.surface();
            Rect u = sr.empty() ? fr : fr.empty() ? sr : sr.unite(fr);
            bool ok = in.seed_frames() == 1 && d.pix == dp.pix && (!l5 || (in.adopts() == 1 && in.adopt_fails() == 0));
            for (int y = rect.y0; ok && y < rect.y1; y++)
                for (int x = rect.x0; ok && x < 300; x++) {
                    bool in_u = !u.empty() && x >= u.x0 && y >= u.y0 && y < u.y1;
                    ok = sd.at(x, y) == (want && in_u ? kGround : kInk);
                }
            return ok;
        };
        bool wcase[5] = {widened({100, 0, 640, 360}, {100, 0, 640, 330}, true, false),
                         widened({100, 0, 640, 360}, {200, 0, 640, 360}, true, false),
                         widened({100, 0, 640, 300}, {100, 0, 640, 360}, true, false), widened({}, {}, false, false),
                         widened({100, 0, 640, 360}, {100, 0, 640, 330}, true, true)};
        check(wcase[0] && wcase[1] && wcase[2] && wcase[3] && wcase[4],
              ("ground rect start: after a state only start, frame 0 fills ground over the start's ground rect where "
               "its own rect has left it (a rect lifted or narrowed at frame 0), also when L5 adopts the target (no "
               "gap kept, 0 two pass fails), over its own rect where it grew, and nothing left of the clip with no "
               "rect, equal in every case to a pixel path that draws the start in full before frame 0 (lifted " +
               std::to_string(wcase[0]) + ", narrowed " + std::to_string(wcase[1]) + ", grew " +
               std::to_string(wcase[2]) + ", none " + std::to_string(wcase[3]) + ", lifted under L5 " +
               std::to_string(wcase[4]) + ")")
                  .c_str());
        struct L5Case {
            bool same = false;
            int adopts = 0, structs = 0, fails = 0;
        };
        auto l5_case = [&](int v, bool checked, bool verify) {
            Rect rect{100, 0, 640, 360};
            Buffer src, other;
            paint(src, {0, 0, src.w, src.h}, kGround);
            paint(src, {150, 50, 290, 130}, kInk);
            paint(src, {330, 70, 470, 190}, 0x40C080);
            if (v == 1)
                paint(src, {500, 250, 600, 330}, 0xC04040);
            paint(other, {0, 0, other.w, other.h}, 0x204060);
            const Rect srcs[] = {{140, 40, 300, 140}, {320, 60, 480, 200}};
            bool under = v == 7 || v == 8 || v == 9;
            auto make = [&](bool start) {
                eng::Frame g;
                for (int k = 0; k < 2; k++) {
                    eng::Block b;
                    b.id = (uint16_t)(5 + k);
                    b.z = under ? 1 - k : k;
                    b.src = under && k == 1 ? Rect{160, 60, 280, 120} : srcs[k];
                    b.source = (v == 2 && k == 0) || (under && k == 1) ? 1 : 0;
                    g.blocks.push_back(b);
                    eng::Pose q;
                    q.block = k;
                    int dx = v == 3 && k == 0 ? 8 : (v == 8 || v == 9) && k == 0 && !start ? 100 : 0;
                    q.rect = {b.src.x0 + dx, b.src.y0, b.src.x1 + dx, b.src.y1};
                    q.clip = rect;
                    g.poses.push_back(q);
                }
                g.shades.assign(2, eng::Shade{});
                if (v == 4)
                    g.shades[0].t = eng::kFull / 2;
                if (v == 9)
                    g.shades[0].t = g.shades[1].t = eng::kFull / 2;
                return g;
            };
            eng::Frame f = make(false), f0 = make(true);
            eng::Instance fast(rect), pix(rect);
            fast.set_segments(true);
            fast.trust_dst(true);
            fast.set_verify(verify);
            Buffer d1 = src, d2 = src;
            if (v == 6)
                paint(d1, {200, 90, 201, 91}, 0x123456);
            const int64_t t0 = 1000;
            fast.begin(t0, false);
            pix.begin(t0, false);
            fast.advance(t0, t0 + 16);
            pix.advance(t0, t0 + 16);
            fast.adopt_source(0);
            fast.adopt_start(true, checked || v != 1, checked || v == 1 || v == 8 || v == 9);
            fast.keep_any((v == 8 || v == 9) && !checked);
            fast.compose(d1.surface(), f, {src.surface(), other.surface()}, kGround, 640, &f0);
            pix.compose(d2.surface(), f, {src.surface(), other.surface()}, kGround, 640, &f0);
            L5Case r;
            r.same = d1.pix == d2.pix;
            r.adopts = fast.adopts();
            r.structs = fast.adopt_struct();
            r.fails = fast.adopt_fails();
            return r;
        };
        L5Case ok = l5_case(0, true, true), gap = l5_case(1, true, true);
        bool exact = ok.same && ok.adopts == 1 && ok.structs == 0 && ok.fails == 0 && gap.same && gap.adopts == 1 &&
                     gap.fails == 0;
        int mutants = 0, differ = 0, caught = 0, refused = 0;
        for (int v : {1, 2, 3, 4}) {
            L5Case c = l5_case(v, true, false), m = l5_case(v, false, false), mv = l5_case(v, false, true);
            bool held = v == 1 ? c.adopts == 1 : c.structs == 1 && c.adopts == 0;
            refused += c.same && held;
            mutants++;
            differ += !m.same && m.adopts == 1;
            caught += mv.same && mv.fails == 1 && mv.adopts == 0;
        }
        L5Case stale = l5_case(6, true, false), stale_v = l5_case(6, true, true);
        L5Case hidden = l5_case(7, true, true), uncover = l5_case(8, true, true), any = l5_case(8, false, false);
        L5Case shaded = l5_case(9, true, true), any_shaded = l5_case(9, false, false);
        bool owns = hidden.same && hidden.adopts == 1 && hidden.structs == 0 && hidden.fails == 0 && uncover.same &&
                    uncover.adopts == 1 && uncover.fails == 0 && !any.same && any.adopts == 1 && shaded.same &&
                    shaded.adopts == 1 && shaded.fails == 0 && !any_shaded.same && any_shaded.adopts == 1;
        char said[1024];
        std::snprintf(said, sizeof said,
                      "L5: a state only start adopted over a target that is its from copy at identity is exact "
                      "(adopts %d, verify fails %d), and so is one that differs only on gap pixels frame 0 rewrites "
                      "(fails %d); "
                      "the gap reset and the structural rule refuse or rewrite each clause (%d of 4); each clause's "
                      "unchecked mutant differs from the pixel path (%d of %d) and fails the two pass compare (%d of "
                      "%d); a stale kept pixel differs without verify (%d) and is caught by it (fails %d); a non "
                      "target block that owns no start pixel adopts exactly (adopts %d, i %d), its pixels uncovered "
                      "on frame 0 are drawn on the copy and the shaded paths (same %d %d), and a keep that skips "
                      "them differs on each (%d %d)",
                      ok.adopts, ok.fails, gap.fails, refused, differ, mutants, caught, mutants, (int)!stale.same,
                      stale_v.fails, hidden.adopts, hidden.structs, (int)uncover.same, (int)shaded.same, (int)!any.same,
                      (int)!any_shaded.same);
        check(exact && refused == 4 && differ == mutants && caught == mutants && !stale.same && stale.adopts == 1 &&
                  stale_v.same && stale_v.fails == 1 && owns,
              said);
    }
    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
