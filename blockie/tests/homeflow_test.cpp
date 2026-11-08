#include <cstdio>
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

    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
