#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "../homeflow.h"
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

    {
        HomeFlow g(fonts, FlowOpts{});
        g.model.systems = {{"GBA", 12, ""}};
        g.kinds = {MenuKind::System};
        g.games = [](int) {
            std::vector<HomeGame> list;
            for (int i = 0; i < 12; i++)
                list.push_back({8000 + i, "Synth Launch Game " + std::to_string(i), "GBA"});
            return list;
        };
        g.home.load = [](const HomeGame& h) -> Art {
            auto img = std::make_shared<Image>();
            img->w = 2;
            img->h = 2;
            img->mem.assign(4, 0x302010u + (uint32_t)(h.id % 100) * 0x030201u);
            return img;
        };
        Buffer gout;
        g.last_frame = [&gout](const Surface& s) { copy(gout.surface(), s); };
        LaunchStart next = LaunchStart::Refused;
        int asked = 0;
        g.launch_game = [&](const HomeGame&) {
            asked++;
            return next;
        };
        g.setup();
        int64_t gt = settle(g, gout, 0);
        g.input(PadAction::Accept, false, gt);
        gt = settle(g, gout, gt);
        g.input(PadAction::Accept, false, gt);
        bool refused = asked == 1 && !g.launched() && !g.ctl.active();
        next = LaunchStart::Cut;
        g.input(PadAction::Accept, false, gt);
        bool cut = asked == 2 && g.launched() && !g.ctl.active();
        check(refused && cut, "a refused launch starts nothing and a cut launch starts no motion");
        next = LaunchStart::Leave;
        Buffer press;
        copy(gout.surface(), press.surface());
        g.input(PadAction::Accept, false, gt);
        bool leaving = g.launched() && g.ctl.active() && g.ctl.kind() == FoldKind::Launch &&
                       g.ctl.run_length() == 33 && g.boundary(gt + 16) == kPaneSplitWide &&
                       near(g.ctl.from(), press.surface(), 960);
        g.frame(gt, gout.surface());
        bool frame0 = near(gout.surface(), press.surface(), 960);
        g.frame(gt + 33, gout.surface());
        bool dark = gout.surface().at(10, 10) == LiveF::pack(0) && gout.surface().at(700, 400) == LiveF::pack(0);
        check(leaving && frame0 && dark, "Accept on a system game leaves from the last presented frame with the"
                                         " boundary at rest, black at 33");
        g.ctl.finish(gt + 50);
        g.come_back(gt + 1000);
        bool back = g.ctl.active() && g.ctl.kind() == FoldKind::Return && g.boundary(gt + 1100) == kPaneSplitWide;
        int64_t tl = settle(g, gout, gt + 1000);
        Buffer fresh;
        g.at(tl);
        g.draw(fresh.surface(), g.model, g.nav, false, g.nav.pane, kScreen);
        check(back && !g.ctl.active() && near(gout.surface(), fresh.surface(), 960),
              "the return keeps the boundary at rest and ends on a fresh render of the system view");
    }
    {
        Nav n;
        n.stack = {Screen::Settings};
        bool home_push = page_under(n, true) == Screen::Home;
        bool settings_pop = page_under(n, false) == Screen::Settings;
        n.stack = {Screen::Settings, Screen::Bluetooth};
        bool settings_push = page_under(n, true) == Screen::Settings;
        n.stack.clear();
        bool home_pop = page_under(n, false) == Screen::Home;
        check(home_push && settings_push && settings_pop && home_pop,
              "page seed base: the home under a push from the menu, settings under a push from its row, the pop mirrors");
    }
    {
        HomeFlow g(fonts, FlowOpts{});
        g.model.systems = {{"settings", 0, ""}};
        g.kinds = {MenuKind::Settings};
        int row_y = 196 + 4 * 86 + 43;
        g.page_seed_y = [&](Screen s) { return s == Screen::Settings ? row_y : -1; };
        g.page_input = [&](Screen s, PadAction a, bool rep) {
            if (s == Screen::Settings && a == PadAction::Accept && !rep) {
                g.nav.stack.push_back(Screen::Bluetooth);
                g.push(Screen::Bluetooth);
                return false;
            }
            return a == PadAction::Back && !rep;
        };
        g.setup();
        int64_t pt = 1000;
        g.input(PadAction::Accept, false, pt);
        bool from_home = g.nav.top() == Screen::Settings && g.page_seed() == -1;
        g.input(PadAction::Accept, false, pt += 2000);
        bool from_row = g.nav.top() == Screen::Bluetooth && g.page_seed() == row_y;
        g.input(PadAction::Back, false, pt += 2000);
        bool back_to_row = g.nav.top() == Screen::Settings && g.page_seed() == row_y;
        g.input(PadAction::Back, false, pt += 2000);
        bool back_home = g.nav.top() == Screen::Home && g.page_seed() == -1;
        check(from_home && from_row && back_to_row && back_home,
              "page seed: a push from settings and its pop plan the row fold at focus_y, the home keeps the menu fold");
    }

    {
        HomeFlow pf(fonts, o);
        pf.model.systems = {{"GB", 3, ""}, {"ports", 4, ""}};
        pf.kinds = {MenuKind::System, MenuKind::System};
        std::vector<HomeGame> ports = {{2001, "Blockie Test Alpha", "ports"},
                                       {2002, "Blockie Test Beta", "ports"},
                                       {2003, "Blockie Test Gamma", "ports"},
                                       {2004, "Blockie Test Delta", "ports"}};
        pf.games = [&ports](int) { return ports; };
        std::vector<int64_t> asked;
        pf.covers_wanted = [&asked](const std::vector<HomeGame>& want, int) {
            for (const HomeGame& g : want)
                asked.push_back(g.id);
        };
        Buffer pout;
        pf.setup();
        int64_t pt = settle(pf, pout, 0);
        pf.input(PadAction::Down, false, pt);
        pt = settle(pf, pout, pt);
        pf.input(PadAction::Accept, false, pt);
        pt = settle(pf, pout, pt);
        pf.input(PadAction::Right, false, pt);
        pt = settle(pf, pout, pt);
        bool on_beta = pf.nav.system_view && pf.nav.sys_focus == 1;
        std::vector<HomeGame> next = {{2005, "Blockie Test Aardvark", "ports"}, ports[0], ports[1], ports[3]};
        asked.clear();
        std::vector<int64_t> gone = pf.regame(1, next);
        check(on_beta && pf.nav.sys_focus == 2 && pf.home.sysgames[2].id == 2002 &&
                  pf.model.game_title == "Blockie Test Beta" && pf.nav.sys_total == 4,
              "a ports rescan keeps focus on the same port when its row moves");
        check(gone.size() == 1 && gone[0] == 2003, "a ports rescan returns the removed ids so their decode jobs drop");
        check(std::find(asked.begin(), asked.end(), 2005) != asked.end(), "a ports rescan asks for a new port's cover");
        check(pf.regame(0, next).empty() && pf.nav.sys_focus == 2,
              "a rescan of a system not on screen changes nothing");
        pf.regame(1, {ports[0]});
        check(pf.nav.sys_focus == 0 && pf.nav.sys_total == 1 && pf.model.game_title == "Blockie Test Alpha",
              "a ports rescan that drops the focused port keeps the focus in range");
    }

    {
        HomeFlow rf(fonts, o);
        rf.model.systems = {{"ports", 3, ""}};
        rf.kinds = {MenuKind::System};
        Buffer rout;
        rf.setup();
        std::vector<HomeGame> rl = {{3001, "Blockie Test One", "ports"},
                                    {3002, "Blockie Test Two", "ports"},
                                    {3003, "Blockie Test Three", "ports"}};
        rf.home.deal_recent(rf.model, rl);
        int64_t rt = settle(rf, rout, 0);
        rf.input(PadAction::Left, false, rt);
        rt = settle(rf, rout, rt);
        rf.input(PadAction::Right, false, rt);
        rt = settle(rf, rout, rt);
        bool on_two = rf.nav.tile_focus == 1 && rf.nav.pane == Pane::Details;
        rf.recent_changed({{3002, "Blockie Test Two Renamed", "ports"}, rl[0], rl[2]});
        check(on_two && rf.nav.tile_focus == 0 && rf.model.game_title == "Blockie Test Two Renamed" &&
                  rf.nav.pane == Pane::Details,
              "a ports rescan keeps the Recent focus on the same game");
    }

    {
        HomeFlow sa(fonts, o);
        sa.model.systems = {{"ports", 0, ""}, {"GB", 3, ""}};
        sa.kinds = {MenuKind::System, MenuKind::System};
        sa.games = [&sa](int sys) {
            std::vector<HomeGame> list;
            if (sa.nav.store)
                for (int i = 0; i < 3; i++)
                    list.push_back({(int64_t(1) << 40) + 10 * sys + i, "blockie test " + std::to_string(i), "porter"});
            return list;
        };
        Buffer so;
        sa.last_frame = [&so](const Surface& s) { copy(so.surface(), s); };
        sa.ctl.can_animate = []() { return true; };
        int entries = 0;
        sa.store_entry = [&](int sys) {
            if (sys != 0)
                return false;
            entries++;
            SelectorModel m;
            m.systems = {{"all", 3, "3 ports"}, {"puzzle", 3, "3 ports"}};
            m.section_label = "all";
            sa.enter_store(m);
            return true;
        };
        sa.setup();
        int64_t t = 0;
        sa.frame(t, so.surface());
        sa.input(PadAction::Accept, false, t);
        check(entries == 0 && !sa.nav.store, "f2: accept on an empty ports entry asks store_empty, not store_entry");
        sa.input(PadAction::Context, false, t);
        check(sa.nav.store && sa.ctl.active() && sa.ctl.kind() == FoldKind::Push && sa.model.systems.size() == 2,
              "f2: the left face button opens the store with the page push");
        t = settle(sa, so, t);
        sa.swap_tiles(t, [&sa]() { sa.model.section_label = "puzzle"; });
        bool forward = sa.ctl.active() && sa.ctl.kind() == FoldKind::Swap && sa.ctl.forward_time(t + 50) == 50;
        sa.frame(t + swap_ms() / 2, so.surface());
        check(forward && sa.ctl.forward_time(t + swap_ms() / 2) == swap_ms() / 2 && sa.model.section_label == "puzzle",
              "f2: the preview swap plays T5 forward, mid swap at half its time");
        t = settle(sa, so, t + swap_ms() / 2);
        sa.nav.focus = 1;
        sa.input(PadAction::Back, false, t);
        check(!sa.nav.store && sa.model.systems.size() == 2 && sa.model.systems[0].name == "ports" &&
                  sa.nav.focus == 0 && sa.ctl.kind() == FoldKind::Pop,
              "f2: back on the genre list restores the home with ports focused");
    }

    for (int big = 0; big < 2; big++) {
        FlowOpts bo;
        bo.big = big == 1;
        HomeFlow ex(fonts, bo);
        ex.model.systems = {{"ports", 20, ""}};
        ex.kinds = {MenuKind::System};
        std::vector<HomeGame> first, second;
        for (int i = 0; i < 20; i++)
            first.push_back({(int64_t(1) << 40) + i, "blockie test " + std::to_string(i), "porter"});
        std::vector<uint32_t> px((size_t)kTileSide * kTileSide, 0xE0C040);
        Art art = std::make_shared<const Image>(Image::from_portrait(px.data(), kTileSide, kTileSide));
        ex.games = [&first](int) { return first; };
        ex.covers_wanted = [&](const std::vector<HomeGame>& want, int) {
            for (const HomeGame& g : want)
                ex.art(g.id, kTileSide, ex.home.epoch, art);
        };
        Buffer eo;
        ex.last_frame = [&eo](const Surface& s) { copy(eo.surface(), s); };
        ex.ctl.can_animate = []() { return true; };
        ex.setup();
        int64_t t = 0;
        ex.frame(t, eo.surface());
        ex.input(PadAction::Accept, false, t);
        t = settle(ex, eo, t);
        for (int i = 0; i < 6; i++) {
            ex.input(PadAction::Down, false, t);
            t = settle(ex, eo, t);
        }
        second = first;
        second.erase(second.begin() + ex.nav.sys_focus);
        second.push_back({(int64_t(1) << 40) + 99, "blockie test 99", "porter"});
        Buffer before;
        copy(eo.surface(), before.surface());
        ex.swap_tiles(t, [&]() { ex.regame(0, second); });
        bool started = ex.ctl.active() && ex.ctl.kind() == FoldKind::Swap;
        Buffer early, mid;
        ex.frame(t + 25, early.surface());
        int dx = swap_leave(25, 1).dx;
        int cols = 0, seen = 0;
        Surface bs = before.surface(), es = early.surface();
        GridSpec g = grid_spec(big ? TileMode::Large : TileMode::Standard);
        for (int c = 0; c < g.cols; c++) {
            int x = kGridX + c * (g.side + g.gutter) + g.side / 2;
            for (int y = kBandTop; y < kBandBottom; y += 8)
                if (bs.at(x, y) != bs.at(2, kBandBottom - 2)) {
                    cols++;
                    seen += es.at(x + dx, y) != bs.at(2, kBandBottom - 2) ? 1 : 0;
                    break;
                }
        }
        check(started && ex.nav.sys_scroll > 0 && cols == g.cols && seen == g.cols,
              big ? "f2 large: each old column of a scrolled expanded grid shows at t 25, shifted by the leave"
                  : "f2: each old column of a scrolled expanded grid shows at t 25, shifted by the leave");
        ex.frame(t + swap_ms() / 2, mid.surface());
        bool half = ex.ctl.forward_time(t + swap_ms() / 2) == swap_ms() / 2;
        t = settle(ex, eo, t + swap_ms() / 2);
        bool right = true;
        Surface ms = mid.surface(), fs = eo.surface();
        for (int y = 0; y < ms.h && right; y++)
            for (int x = kPaneSplitWide; x < ms.w && right; x++)
                right = ms.at(x, y) == fs.at(x, y);
        check(started && half && right && ex.home.sysgames.size() == 20,
              big ? "f2: the expanded grid swaps by T5 in large mode, the right pane the new one mid swap"
                  : "f2: the expanded grid swaps by T5, the right pane the new one mid swap");
    }

    for (int light = 0; light < 2; light++)
        for (int big = 0; big < 2; big++) {
            set_theme_light(light == 1);
            FlowOpts dop;
            dop.big = big == 1;
            HomeFlow dw(fonts, dop);
            dw.model.systems = {{"ports", 0, ""}};
            dw.kinds = {MenuKind::System};
            Buffer dout;
            dw.last_frame = [&dout](const Surface& s) { copy(dout.surface(), s); };
            dw.ctl.can_animate = []() { return true; };
            auto tiles = [](uint32_t rgb) {
                std::vector<uint32_t> px((size_t)kTileSide * kTileSide, rgb);
                std::vector<Art> v;
                for (int k = 0; k < kGridTiles; k++)
                    v.push_back(std::make_shared<const Image>(Image::from_portrait(px.data(), kTileSide, kTileSide)));
                return v;
            };
            dw.store_entry = [&](int) {
                SelectorModel m;
                m.systems = {{"action", 8, "8 ports"}, {"puzzle", 8, "8 ports"}, {"rpg", 8, "8 ports"}};
                m.section_label = "action";
                m.tiles = tiles(0x506070);
                dw.enter_store(m);
                return true;
            };
            dw.setup();
            int64_t t = 0;
            dw.frame(t, dout.surface());
            dw.input(PadAction::Context, false, t);
            t = settle(dw, dout, t);
            bool whole = true;
            uint32_t shade = 0x607050;
            for (const char* word : {"puzzle", "rpg", "jump"}) {
                shade ^= 0x101010;
                dw.swap_tiles(t, [&]() {
                    dw.model.section_label = word;
                    dw.model.tiles = tiles(shade);
                });
                Buffer mid;
                dw.frame(t + 125, mid.surface());
                t = settle(dw, dout, t + 125);
                Rect ink = header_ink_rect(fonts, dw.model);
                Surface ms = mid.surface(), ls = dout.surface();
                int found = 0;
                for (int y = kBandTop; y < ink.y1; y++)
                    for (int x = std::max(0, ink.x0 - 24); x < ink.x1; x++) {
                        bool in_tile = false;
                        for (int k = 0; k < kGridTiles; k++) {
                            Rect r = grid_tile_rect(k, kGridCols);
                            in_tile = in_tile || (x >= r.x0 && x < r.x1 && y >= r.y0 && y < r.y1);
                        }
                        if (in_tile || ls.at(x, y) == theme().bg)
                            continue;
                        found++;
                        whole = whole && x >= ink.x0 && ms.at(x, y) == ls.at(x, y);
                    }
                whole = whole && found > 0;
            }
            std::string mode = light ? (big ? "light large" : "light") : (big ? "dark large" : "dark");
            std::string name = "f2 " + mode + ": the new word's gutter ink below y 76 shows at t 125" +
                               " (accepted misfire: a moving row pixel equal to ground)";
            check(whole, name.c_str());
        }
    set_theme_light(false);

    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
