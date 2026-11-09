#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "../homeflow.h"

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

int64_t settle(HomeFlow& flow, Buffer& out, int64_t t)
{
    while (flow.frame(t, out.surface()) && t < 20000)
        t += 16;
    return t + 16;
}

bool shows(const HomeFlow& flow, const HomeGame& g)
{
    return flow.model.game_title == g.name && flow.model.game_system == g.system;
}

void copy(const Surface& src, const Surface& dst)
{
    for (int y = 0; y < dst.h; y++)
        copy_px(dst.row(y), src.row(y), dst.w);
}

bool near(const Surface& a, const Surface& b, int x1)
{
    for (int y = 0; y < a.h; y++)
        for (int x = 0; x < x1; x++)
            for (int sh = 0; sh < 24; sh += 8)
                if (std::abs((int)(a.at(x, y) >> sh & 0xFF) - (int)(b.at(x, y) >> sh & 0xFF)) > 1)
                    return false;
    return true;
}

}

int main(int argc, char** argv)
{
    std::string dir = argc > 1 ? argv[1] : "fonts";
    Fonts fonts;
    if (!fonts.open(dir, {})) {
        std::printf("FAIL fonts not found in %s\n", dir.c_str());
        return 1;
    }
    fonts.begin_frame(-1);

    FlowOpts o;
    HomeFlow flow(fonts, o);
    flow.model.systems = {{"GB", 3, ""}, {"GBA", 3, ""}, {"PS1", 3, ""}};
    flow.kinds = {MenuKind::System, MenuKind::System, MenuKind::System};
    flow.games = [](int sys) {
        std::vector<HomeGame> list;
        const char* const names[] = {"GB", "GBA", "PS1"};
        for (int i = 0; i < 3; i++)
            list.push_back({1000 * (sys + 1) + i, "Synth System Game " + std::to_string(i), names[sys]});
        return list;
    };
    Buffer out;
    flow.last_frame = [&out](const Surface& s) {
        for (int y = 0; y < s.h; y++)
            copy_px(s.row(y), out.surface().row(y), s.w);
    };
    flow.setup();
    std::vector<HomeGame> recent = {{901, "Synth Recent A", "GB"},
                                    {902, "Synth Recent B", "GBA"},
                                    {903, "Synth Recent C", "PS1"},
                                    {904, "Synth Recent D", "GB"}};
    flow.home.deal_recent(flow.model, recent);
    int64_t t = settle(flow, out, 0);

    flow.input(PadAction::Left, false, t);
    check(flow.nav.pane == Pane::Details && shows(flow, recent[0]) && !flow.t9().running(t) &&
              flow.t9().shown(t) == T9Text{901, "Synth Recent A", "GB"},
          "Recent in shows game 0's title and system with no T9");
    t = settle(flow, out, t);
    check(!flow.animating(t) && shows(flow, recent[0]) && flow.t9().shown(t).id == 901,
          "the landed Recent browse keeps game 0's title");

    check(!flow.input(PadAction::Left, false, t) && !flow.animating(t) && flow.nav.tile_focus == 0 &&
              flow.nav.pane == Pane::Details && shows(flow, recent[0]),
          "a fresh left at game 0 is refused and starts nothing");

    flow.input(PadAction::Right, false, t);
    check(flow.nav.tile_focus == 1 && shows(flow, recent[1]) && flow.animating(t) && flow.t9().running(t) &&
              flow.t9().shown(t).id == 901 && flow.t9().shown(t + T9::out_ms()) == T9Text{902, "Synth Recent B", "GBA"},
          "right shows game 1's title and system, T9 runs and swaps them at the out's end");
    t = settle(flow, out, t);

    flow.input(PadAction::Back, false, t);
    check(flow.nav.pane == Pane::Selector && flow.model.game_title.empty() && flow.model.game_system.empty() &&
              flow.t9().shown(t) == T9Text{},
          "back clears the title and the system line");
    t = settle(flow, out, t);

    flow.input(PadAction::Left, false, t);
    t = settle(flow, out, t);
    flow.input(PadAction::Right, false, t);
    t = settle(flow, out, t);
    flow.game_ran(played_first(flow.home.recent, recent[2], flow.home.recent_cap));
    check(flow.nav.tile_focus == 0 && shows(flow, recent[2]) && !flow.t9().running(t) &&
              flow.t9().shown(t) == T9Text{903, "Synth Recent C", "PS1"},
          "a game that ran sets the new game 0's title and system at once");
    t = settle(flow, out, t);

    flow.input(PadAction::Back, false, t);
    t = settle(flow, out, t);
    flow.input(PadAction::Accept, false, t);
    t = settle(flow, out, t);
    flow.input(PadAction::Right, false, t);
    check(flow.nav.system_view && flow.model.game_system == "GB" && flow.model.game_title == "Synth System Game 1" &&
              flow.t9().shown(t + T9::out_ms()) == T9Text{1001, "Synth System Game 1", "GB"},
          "in the system view T9 carries the game and the system's name");

    {
        HomeFlow g(fonts, FlowOpts{});
        g.model.systems = {{"GBA", 40, ""}};
        g.kinds = {MenuKind::System};
        g.games = [](int) {
            std::vector<HomeGame> list;
            for (int i = 0; i < 40; i++)
                list.push_back({5000 + i, "Synth Glide Game " + std::to_string(i), "GBA"});
            return list;
        };
        g.home.load = [](const HomeGame& h) -> Art {
            auto img = std::make_shared<Image>();
            img->w = 2;
            img->h = 2;
            img->mem.assign(4, 0x204000u + (uint32_t)(h.id - 5000) * 0x050301u);
            return img;
        };
        Buffer gout, drawn;
        g.last_frame = [&gout](const Surface& s) { copy(gout.surface(), s); };
        g.setup();
        int64_t gt = settle(g, gout, 0);
        auto scroll_down = [&]() {
            for (int i = 0; i < 12; i++) {
                int s = g.nav.sys_scroll;
                g.input(PadAction::Down, false, gt);
                if (g.nav.sys_scroll != s)
                    return true;
                gt = settle(g, gout, gt);
            }
            return false;
        };
        auto swapped_from = [&](int64_t t_back) {
            int64_t t = t_back + 400;
            g.frame(t, gout.surface());
            return g.ctl.kind() == FoldKind::Collapse && g.ctl.forward_time(t) == 400 &&
                   near(gout.surface(), drawn.surface(), std::max(g.boundary(t), kPaneLeftEdge));
        };

        g.input(PadAction::Accept, false, gt);
        gt = settle(g, gout, gt);
        bool down = scroll_down();
        g.frame(gt + 40, gout.surface());
        copy(gout.surface(), drawn.surface());
        g.input(PadAction::Back, false, gt + 50);
        check(down && swapped_from(gt + 50),
              "Back 10 ms after the last frame of a grid glide: the collapse's rows sit where that frame drew them");
        gt = settle(g, gout, gt + 610);

        g.input(PadAction::Accept, false, gt);
        gt = settle(g, gout, gt);
        down = scroll_down();
        copy(gout.surface(), drawn.surface());
        g.input(PadAction::Back, false, gt);
        check(down && swapped_from(gt),
              "Down and Back in one dispatch: the rows and the border swap from the frame before the Down");
    }

    {
        FlowOpts lo;
        lo.big = true;
        HomeFlow g(fonts, lo);
        g.model.systems = {{"GBA", 40, ""}};
        g.kinds = {MenuKind::System};
        g.games = [](int) {
            std::vector<HomeGame> list;
            for (int i = 0; i < 40; i++)
                list.push_back({5000 + i, "Synth Large Game " + std::to_string(i), "GBA"});
            return list;
        };
        auto color = [](int i) { return 0x204000u + (uint32_t)i * 0x050301u; };
        g.home.load = [&color](const HomeGame& h) -> Art {
            auto img = std::make_shared<Image>();
            img->w = 2;
            img->h = 2;
            img->mem.assign(4, color((int)(h.id - 5000)));
            return img;
        };
        Buffer gout;
        g.last_frame = [&gout](const Surface& s) { copy(gout.surface(), s); };
        g.setup();
        int64_t gt = settle(g, gout, 0);
        g.input(PadAction::Accept, false, gt);
        gt = settle(g, gout, gt);
        auto close = [](uint32_t a, uint32_t b) {
            for (int sh = 0; sh < 24; sh += 8)
                if (std::abs((int)(a >> sh & 0xFF) - (int)(b >> sh & 0xFF)) > 1)
                    return false;
            return true;
        };
        bool rows = g.nav.system_view;
        for (int s = 1; s <= 2; s++) {
            for (int i = 0; i < 12 && g.nav.sys_scroll < s; i++) {
                g.input(PadAction::Down, false, gt);
                gt = settle(g, gout, gt);
            }
            Surface o = gout.surface();
            rows = rows && g.nav.sys_scroll == s && close(o.at(130, 90), color(2 * s)) &&
                   close(o.at(130, 340), color(2 * s + 2)) &&
                   close(o.at(130, 70), LiveF::pack(theme().bg));
        }
        check(rows, "large: a band scroll of 1 and 2 rows puts the first shown row at y 80, the next at 328");
    }

    for (int64_t e : {16, 33}) {
        HomeFlow g(fonts, FlowOpts{});
        g.model.systems = {{"GBA", 12, ""}};
        g.kinds = {MenuKind::System};
        g.games = [](int) {
            std::vector<HomeGame> list;
            for (int i = 0; i < 12; i++)
                list.push_back({6000 + i, "Synth Swap Game " + std::to_string(i), "GBA"});
            return list;
        };
        g.home.load = [](const HomeGame& h) -> Art {
            auto img = std::make_shared<Image>();
            img->w = 2;
            img->h = 2;
            img->mem.assign(4, 0x203040u + (uint32_t)(h.id % 100) * 0x050301u + (h.id >= 7000 ? 0x600000u : 0u));
            return img;
        };
        Buffer gout;
        std::vector<Buffer> fwd(3);
        g.last_frame = [&gout](const Surface& s) { copy(gout.surface(), s); };
        g.setup();
        std::vector<HomeGame> rec;
        for (int i = 0; i < 4; i++)
            rec.push_back({7000 + i, "Synth Swap Recent " + std::to_string(i), "GBA"});
        g.home.deal_recent(g.model, rec);
        int64_t gt = settle(g, gout, 0);
        g.input(PadAction::Accept, false, gt);
        bool open = g.ctl.kind() == FoldKind::Expand && !g.ctl.reversed();
        for (int64_t f : {0, 16, 33}) {
            if (f > e)
                break;
            g.frame(gt + f, gout.surface());
            copy(gout.surface(), fwd[f / 16].surface());
        }
        g.input(PadAction::Back, false, gt + e);
        bool back = g.ctl.reversed() && g.ctl.run_length() == e;
        bool poses = true;
        for (int64_t f : {16, 0}) {
            if (f >= e)
                continue;
            int64_t s = e - f;
            g.frame(gt + e + s, gout.surface());
            poses = poses && g.ctl.forward_time(gt + e + s) == f && near(gout.surface(), fwd[f / 16].surface(), 960);
        }
        check(open && back && poses, e == 16 ? "Back 16 ms into an enter reverses from e = 16, its frame at s = 16 is"
                                               " the forward frame at 0"
                                             : "Back 33 ms into an enter reverses from e = 33, its frames at s = 17 and"
                                               " 33 are the forward frames at 16 and 0");
    }

    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
