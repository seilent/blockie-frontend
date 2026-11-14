#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "../../ui/qr.h"
#include "../../ui/size.h"
#include "../store_flow.h"
#include "../theme.h"

using namespace fd;

namespace {

int g_fail = 0;
int g_total = 0;

void check(bool ok, const char* what)
{
    g_total++;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        g_fail++;
}

StorePort port(const char* name, const char* genre)
{
    StorePort p;
    p.name = name;
    p.title = name;
    p.genres = {genre};
    p.size = 4096;
    return p;
}

struct Rig {
    Fonts& fonts;
    GlyphCache glyphs;
    PortPage pp{fonts, glyphs};
    FlowOpts fo;
    HomeFlow flow{fonts, fo};
    Children kids;
    PortsHelper helper;
    StoreFlow sf;
    Buffer out;
    int64_t now = 0;
    int rescans = 0, rereads = 0, toasts = 0, spawns = 0, launches = 0, redraws = 0, shots = 0;
    bool online = true;
    std::vector<IndexPort> index;
    SteamState steam;

    explicit Rig(Fonts& f) : fonts(f), sf(flow, pp, helper, kids)
    {
        helper.spawn = [this](const std::vector<std::string>&, int* fd) {
            *fd = -1;
            spawns++;
            return (pid_t)1;
        };
        helper.signal_child = [](pid_t, int) {};
        sf.host.now = [this]() { return now; };
        sf.host.is_ports = [](int sys) { return sys == 0; };
        sf.host.online = [this]() { return online; };
        sf.host.stale = []() { return false; };
        sf.host.index = [this]() { return index; };
        sf.host.steam = [this]() { return steam; };
        sf.host.rescan = [this]() { rescans++; };
        sf.host.reread = [this]() { rereads++; };
        sf.host.preview_art = [](const std::vector<HomeGame>&, const std::vector<HomeGame>&) {};
        sf.host.shot = [this](const StoreItem*) { shots++; };
        sf.host.toast = [this](const std::string&, const std::string&) { toasts++; };
        sf.host.closed = []() {};
        sf.host.redraw = [this]() { redraws++; };
        sf.attach();
        flow.model.systems = {{kPortsSystem, 0, ""}};
        flow.kinds = {MenuKind::System};
        flow.games = [this](int sys) { return flow.nav.store ? sf.games(sys) : std::vector<HomeGame>(); };
        flow.last_frame = [this](const Surface& s) {
            for (int y = 0; y < s.h; y++)
                copy_px(s.row(y), out.surface().row(y), s.w);
        };
        flow.ctl.can_animate = []() { return true; };
        flow.launch_game = [this](const HomeGame&) {
            launches++;
            return LaunchStart::Refused;
        };
        flow.page_input = [this](Screen s, PadAction a, bool r) { return sf.page_input(s, a, r, now); };
        flow.draw_page = [this](Canvas& c, Screen s, int64_t t) {
            InkLayer ink;
            if (s == Screen::SteamLogin)
                pp.draw_login(c, ink, t);
            else
                pp.draw(c, ink, t);
        };
        flow.setup();
    }

    void land()
    {
        while (flow.frame(now, out.surface()) && now < 20000)
            now += 16;
        now += 16;
        sf.tick(now);
    }
};

bool same_home(Fonts& fonts, HomeFlow& flow)
{
    FlowOpts fo;
    HomeFlow ref(fonts, fo);
    ref.model = flow.model;
    ref.kinds = flow.kinds;
    ref.nav = flow.nav;
    ref.setup();
    Buffer a, b;
    flow.draw(a.surface(), flow.model, flow.nav, false, Pane::Selector, kScreen);
    ref.draw(b.surface(), ref.model, ref.nav, false, Pane::Selector, kScreen);
    Surface sa = a.surface(), sb = b.surface();
    for (int y = 0; y < sa.h; y++)
        for (int x = 0; x < sa.w; x++)
            if (sa.at(x, y) != sb.at(x, y))
                return false;
    return true;
}

bool to_hold(Rig& r)
{
    Catalog c;
    c.ports = {port("blockie-test-a.zip", "action")};
    r.sf.set_catalog(true, c, r.now);
    r.flow.input(PadAction::Context, false, r.now);
    r.land();
    r.flow.input(PadAction::Accept, false, r.now);
    r.land();
    r.flow.input(PadAction::Accept, false, r.now);
    r.land();
    r.flow.input(PadAction::Accept, false, r.now);
    bool asked = r.helper.running() && r.helper.cmd() == HelperCmd::Install;
    r.index = {{7, "blockie-test-a", "blockie-test-a.zip", "blockie-test-a", ""}};
    r.helper.line("done\tblockie-test-a.zip");
    r.helper.finish();
    r.now += 16;
    return asked && r.flow.nav.top() == Screen::Port && r.pp.action_count(r.now) == 1 &&
           r.pp.action_count(r.now + kPortDoneHoldMs) == 2;
}

const std::string kUrl38 = "https://s.team/q/1/1111111111111111111";

bool to_port(Rig& r, bool update = false)
{
    Catalog c;
    c.ports = {port("blockie-test-a.zip", "action")};
    c.ports[0].update = update;
    r.index = {{7, "blockie-test-a", "blockie-test-a.zip", "blockie-test-a", ""}};
    r.sf.set_catalog(true, c, r.now);
    r.flow.input(PadAction::Context, false, r.now);
    r.land();
    r.flow.input(PadAction::Accept, false, r.now);
    r.land();
    r.flow.input(PadAction::Accept, false, r.now);
    r.land();
    return r.flow.nav.top() == Screen::Port && r.pp.item().installed;
}

bool getting(Rig& r)
{
    r.steam.ports = {{"blockie-test-a.zip", false}};
    bool open = to_port(r);
    r.flow.input(PadAction::Accept, false, r.now);
    return open && r.helper.running() && r.helper.cmd() == HelperCmd::SteamGet &&
           r.helper.name() == "blockie-test-a.zip";
}

bool to_remove(Rig& r)
{
    Catalog c;
    c.ports = {port("blockie-test-a.zip", "action")};
    r.index = {{7, "blockie-test-a", "blockie-test-a.zip", "blockie-test-a", ""}};
    r.sf.set_catalog(true, c, r.now);
    r.flow.input(PadAction::Context, false, r.now);
    r.land();
    r.flow.input(PadAction::Accept, false, r.now);
    r.land();
    r.flow.input(PadAction::Accept, false, r.now);
    r.land();
    r.flow.input(PadAction::Down, false, r.now);
    r.flow.input(PadAction::Accept, false, r.now);
    r.flow.input(PadAction::Up, false, r.now);
    r.flow.input(PadAction::Accept, false, r.now);
    return r.helper.running() && r.helper.cmd() == HelperCmd::Remove && r.pp.focus() == 1;
}

Catalog three_genres()
{
    Catalog c;
    c.ports = {port("blockie-test-a.zip", "action"), port("blockie-test-b.zip", "puzzle"),
               port("blockie-test-r.zip", "rpg")};
    return c;
}

Catalog two_genres()
{
    Catalog c;
    c.ports = {port("blockie-test-a.zip", "action"), port("blockie-test-b.zip", "puzzle"),
               port("blockie-test-c.zip", "puzzle")};
    return c;
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

    {
        Rig r(fonts);
        r.flow.input(PadAction::Accept, false, r.now);
        check(r.flow.nav.store, "f2: accept on an empty ports entry opens the store");
    }

    {
        Rig r(fonts);
        r.sf.set_catalog(true, two_genres(), r.now);
        r.flow.input(PadAction::Context, false, r.now);
        r.land();
        check(r.flow.nav.store && r.sf.rows.size() == 4 && r.flow.model.section_label == "all",
              "f2: the store opens on all with its genres");
        r.sf.rebuild(r.now);
        check(!r.flow.ctl.active(), "f2: a rebuild with nothing changed starts no swap");
        r.flow.input(PadAction::Down, false, r.now);
        r.flow.input(PadAction::Up, false, r.now);
        r.now += kStorePreviewRestMs + 16;
        r.sf.tick(r.now);
        check(!r.flow.ctl.active(), "f2: a rest on the same genre starts no swap");
        r.flow.input(PadAction::Down, false, r.now);
        r.flow.input(PadAction::Down, false, r.now);
        r.land();
        r.now += kStorePreviewRestMs + 16;
        r.sf.tick(r.now);
        check(r.flow.ctl.active() && r.flow.ctl.kind() == FoldKind::Swap && r.flow.model.section_label == "action",
              "f2: a rest on another genre swaps the preview");
        r.land();
        int was = r.flow.nav.focus;
        Catalog c;
        c.ports = {port("blockie-test-b.zip", "puzzle")};
        r.sf.set_catalog(true, c, r.now);
        check(r.flow.nav.focus == was - 1, "f2: a genre that goes moves the focus to the row above");
    }

    {
        Rig r(fonts);
        r.sf.set_catalog(true, two_genres(), r.now);
        r.flow.input(PadAction::Context, false, r.now);
        r.land();
        r.flow.input(PadAction::Accept, false, r.now);
        r.land();
        r.flow.input(PadAction::Accept, false, r.now);
        r.land();
        check(r.flow.nav.top() == Screen::Port, "f2: accept on a tile opens the port page");
        Catalog c = two_genres();
        c.ports.push_back(port("blockie-test-0.zip", "action"));
        r.sf.set_catalog(true, c, r.now);
        bool held = r.flow.nav.top() == Screen::Port && !r.flow.ctl.active();
        r.flow.input(PadAction::Back, false, r.now);
        int drawn = r.redraws;
        r.land();
        check(r.redraws > drawn, "f2: a deferred grid settle asks for a redraw");
        check(held && r.flow.nav.system_view && r.flow.home.sysgames.size() == 4,
              "f2: a changed set under the port page lands on the back");
        bool swapping = r.flow.ctl.active() && r.flow.ctl.kind() == FoldKind::Swap;
        r.land();
        r.sf.rebuild(r.now);
        check(swapping && !r.flow.ctl.active(), "f2: the expanded grid swaps a changed set, not an unchanged one");
    }

    {
        Rig r(fonts);
        r.index = {{7, "blockie-test-a", "blockie-test-a.zip", "blockie-test-a", ""}};
        r.sf.set_catalog(true, two_genres(), r.now);
        r.helper.ask(r.kids, HelperCmd::Refresh, "");
        r.helper.ask(r.kids, HelperCmd::Install, "blockie-test-b.zip");
        HelperCmd c = HelperCmd::Refresh;
        check(r.helper.holds("blockie-test-b.zip", c) && c == HelperCmd::Install,
              "f2: the queued install is held for its port page");
        r.helper.line("done\trefresh");
        r.helper.finish();
        check(r.rereads == 1 && r.rescans == 0 && r.spawns == 2,
              "f2: a done refresh rereads the catalogue, no rescan, then the queued install runs");
        r.helper.line("fail\tblockie-test-b.zip\tunknown-runtime");
        r.helper.finish();
        check(r.rescans == 1 && r.toasts == 0,
              "f2: a failed install still rescans the index, unknown-runtime with no toast");
        r.helper.ask(r.kids, HelperCmd::Install, "blockie-test-c.zip");
        r.helper.line("fail\tblockie-test-c.zip\tnetwork");
        r.helper.finish();
        check(r.rescans == 2 && r.toasts == 1, "f2: a network failure rescans and shows its toast");
    }

    {
        Rig r(fonts);
        bool held = to_hold(r);
        r.flow.input(PadAction::Accept, false, r.now);
        bool quiet = r.launches == 0 && r.pp.action_count(r.now) == 2;
        r.flow.input(PadAction::Accept, false, r.now);
        check(held && quiet && r.launches == 1,
              "f2: an accept during the done hold lands the rows and does nothing, the next accept plays");
        Rig d(fonts);
        held = to_hold(d);
        d.flow.input(PadAction::Down, false, d.now);
        check(held && d.pp.focus() == 1 && d.pp.action_count(d.now) == 2,
              "f2: a direction during the done hold lands the rows and moves on them");
        Rig b(fonts);
        held = to_hold(b);
        b.flow.input(PadAction::Back, false, b.now);
        check(held && b.flow.nav.stack.empty() && b.launches == 0, "f2: back during the done hold lands and leaves");
    }

    {
        Rig r(fonts);
        bool asked = to_remove(r);
        r.index.clear();
        r.helper.line("done\tblockie-test-a.zip");
        r.helper.finish();
        r.now += 16;
        r.pp.tick(r.now);
        bool held = r.pp.focus() == 1 && r.pp.action_count(r.now) == 2;
        r.now += kPortDoneHoldMs;
        r.pp.tick(r.now);
        bool falling = r.pp.focus() == 1 && r.pp.action_count(r.now) == 2;
        r.now += eng::step_ms(eng::kTextStep);
        r.pp.tick(r.now);
        check(asked && held && falling && r.pp.focus() == 0 && r.pp.action_count(r.now) == 1,
              "f2: a removed row keeps the focus through the done hold, the focus moves up at the landing");
        Rig o(fonts);
        asked = to_remove(o);
        o.online = false;
        o.sf.online_changed(o.now);
        o.index.clear();
        o.helper.line("done\tblockie-test-a.zip");
        o.helper.finish();
        o.now += 16;
        o.pp.tick(o.now);
        held = o.pp.focus() == 1 && o.pp.action_count(o.now) == 2;
        o.now += kPortDoneHoldMs + eng::step_ms(eng::kTextStep);
        o.pp.tick(o.now);
        check(asked && held && o.pp.focus() == -1 && o.pp.action_count(o.now) == 0,
              "f2: a remove done offline leaves no rows, the column takes the focus only at the landing");
        Rig s(fonts);
        asked = to_remove(s);
        s.index.clear();
        s.helper.line("done\tblockie-test-a.zip");
        s.helper.finish();
        s.now += 16;
        s.online = false;
        s.sf.online_changed(s.now);
        held = s.pp.focus() == 1 && s.pp.action_count(s.now) == 2;
        s.now += kPortDoneHoldMs + eng::step_ms(eng::kTextStep);
        s.pp.tick(s.now);
        check(asked && held && s.pp.action_count(s.now) == 0,
              "f2: a second change inside the done hold keeps the shown rows until the landing");
    }

    {
        Rig r(fonts);
        Catalog c;
        c.ports = {port("blockie-test-a.zip", "action")};
        r.sf.set_catalog(true, c, r.now);
        r.flow.input(PadAction::Context, false, r.now);
        r.land();
        r.flow.input(PadAction::Accept, false, r.now);
        r.land();
        r.flow.input(PadAction::Accept, false, r.now);
        r.land();
        r.flow.input(PadAction::Accept, false, r.now);
        r.online = false;
        r.sf.online_changed(r.now);
        check(r.helper.running() && r.pp.action_count(r.now) == 1,
              "f2: going offline during an install keeps the working install row");
        r.helper.line("fail\tblockie-test-a.zip\tnetwork");
        r.helper.finish();
        r.now += kPortDoneHoldMs + 16;
        check(r.pp.action_count(r.now) == 0, "f2: once the install ends offline its row goes");
    }

    {
        Rig r(fonts);
        r.sf.set_catalog(true, two_genres(), r.now);
        r.flow.input(PadAction::Context, false, r.now);
        r.land();
        r.flow.input(PadAction::Accept, false, r.now);
        r.land();
        r.flow.input(PadAction::Back, false, r.now);
        r.land();
        check(r.flow.model.section_label == "all", "f2: back from a genre grid keeps the genre word over the preview");
        r.flow.input(PadAction::Down, false, r.now);
        r.flow.input(PadAction::Down, false, r.now);
        r.land();
        r.flow.input(PadAction::Accept, false, r.now);
        r.land();
        r.flow.input(PadAction::Accept, false, r.now);
        r.land();
        Catalog c;
        c.ports = {port("blockie-test-b.zip", "puzzle")};
        r.index = {{7, "blockie-test-b", "blockie-test-b.zip", "blockie-test-b", ""}};
        r.sf.set_catalog(true, c, r.now);
        bool kept = r.flow.nav.system_view && r.flow.nav.top() == Screen::Port && r.sf.rows[1].items.size() == 1;
        r.flow.input(PadAction::Back, false, r.now);
        int before_leave = r.redraws;
        r.land();
        bool asked = r.redraws > before_leave;
        r.land();
        check(kept && !r.flow.nav.system_view && r.flow.nav.stack.empty(),
              "f2: a genre that goes under the port page is left once the page closes");
        check(asked, "f2: leaving a gone genre's grid asks for a redraw");
    }

    {
        Rig r(fonts);
        r.sf.set_catalog(true, two_genres(), r.now);
        r.flow.input(PadAction::Context, false, r.now);
        r.land();
        for (int i = 0; i < 3; i++)
            r.flow.input(PadAction::Down, false, r.now);
        r.land();
        r.now += kStorePreviewRestMs + 16;
        r.sf.tick(r.now);
        r.land();
        Catalog c;
        c.ports = {port("blockie-test-b.zip", "puzzle"), port("blockie-test-c.zip", "puzzle")};
        r.sf.set_catalog(true, c, r.now);
        check(r.flow.nav.focus == 2 && !r.flow.ctl.active() && same_home(fonts, r.flow),
              "f2: the menu glides follow a focus the rebuild moved");
    }

    {
        Rig r(fonts);
        r.sf.set_catalog(true, three_genres(), r.now);
        r.flow.input(PadAction::Context, false, r.now);
        r.land();
        for (int i = 0; i < 3; i++)
            r.flow.input(PadAction::Down, false, r.now);
        r.flow.input(PadAction::Accept, false, r.now);
        r.land();
        r.now += kStorePreviewRestMs + 16;
        r.sf.tick(r.now);
        r.flow.input(PadAction::Back, false, r.now);
        r.land();
        r.land();
        int64_t a = r.sf.items.empty() ? -1 : r.sf.games(3).front().id;
        check(r.flow.model.section_label == "puzzle" && !r.flow.home.recent.empty() && r.flow.home.recent[0].id == a,
              "f2: a rest that ends inside a genre grid deals that genre's preview after the back");
    }

    {
        Rig r(fonts);
        r.sf.set_catalog(true, three_genres(), r.now);
        r.flow.input(PadAction::Context, false, r.now);
        r.land();
        r.flow.input(PadAction::Accept, false, r.now);
        r.land();
        r.flow.input(PadAction::Accept, false, r.now);
        r.land();
        r.helper.ask(r.kids, HelperCmd::Refresh, "");
        r.flow.input(PadAction::Accept, false, r.now);
        HelperCmd c = HelperCmd::Refresh;
        bool waiting = r.helper.holds(r.pp.item().name, c) && c == HelperCmd::Install;
        r.online = false;
        r.sf.online_changed(r.now);
        check(waiting && r.pp.action_count(r.now) == 1,
              "f2: going offline during a waiting install keeps the working install row");
    }

    {
        Rig r(fonts);
        Catalog c;
        c.ports = {port("blockie-test-a.zip", "action")};
        r.sf.set_catalog(true, c, r.now);
        r.flow.input(PadAction::Context, false, r.now);
        r.land();
        r.flow.input(PadAction::Accept, false, r.now);
        r.land();
        r.flow.input(PadAction::Accept, false, r.now);
        r.land();
        r.flow.input(PadAction::Accept, false, r.now);
        r.helper.line("progress\tdownload\t5\t-");
        Buffer page;
        Canvas pc(page.surface());
        InkLayer pink;
        r.pp.draw(pc, pink, r.now + 1000);
        Surface ps = page.surface();
        uint32_t ground = ps.at(2, kPortraitH - 2);
        int inked = 0;
        for (int y = kPortRowsTop + 42; y < kPortRowsTop + 46; y++)
            for (int x = kPortRowX + 100; x < kPortRowClip; x++)
                inked += ps.at(x, y) != ground ? 1 : 0;
        check(r.helper.running() && inked > 0,
              "f2: an install with no total draws the busy dots 21 u under the install row's top");
    }

    {
        auto dots_at = [&](Rig& r, int y0) {
            Buffer page;
            Canvas pc(page.surface());
            InkLayer pink;
            r.pp.draw(pc, pink, r.now + 1000);
            Surface ps = page.surface();
            uint32_t ground = ps.at(2, kPortraitH - 2);
            int inked = 0;
            for (int y = y0; y < y0 + 4; y++)
                for (int x = kPortRowX + 100; x < kPortRowClip; x++)
                    inked += ps.at(x, y) != ground ? 1 : 0;
            return inked;
        };
        auto to_remove = [&](Rig& r) {
            Catalog c;
            c.ports = {port("blockie-test-a.zip", "action"), port("blockie-test-b.zip", "action")};
            r.index = {{7, "blockie-test-a", "blockie-test-a.zip", "blockie-test-a", ""}};
            r.sf.set_catalog(true, c, r.now);
            r.flow.input(PadAction::Context, false, r.now);
            r.land();
            r.flow.input(PadAction::Accept, false, r.now);
            r.land();
            r.flow.input(PadAction::Accept, false, r.now);
            r.land();
            r.flow.input(PadAction::Down, false, r.now);
            r.flow.input(PadAction::Accept, false, r.now);
            r.flow.input(PadAction::Up, false, r.now);
            r.flow.input(PadAction::Accept, false, r.now);
        };
        Rig r(fonts);
        to_remove(r);
        check(r.helper.running() && r.helper.cmd() == HelperCmd::Remove && r.pp.words() == "removing" &&
                  dots_at(r, kPortRowsTop + 80 + 42) > 0,
              "f2: a running remove shows removing with the dots 42 px under its row's top");
        Rig q(fonts);
        q.helper.ask(q.kids, HelperCmd::Install, "blockie-test-b.zip");
        to_remove(q);
        bool waiting = q.pp.words() == "waiting" && dots_at(q, kPortRowsTop + 80 + 42) == 0;
        q.helper.line("done\tblockie-test-b.zip");
        q.helper.finish();
        check(waiting && q.helper.cmd() == HelperCmd::Remove && q.pp.words() == "removing" &&
                  dots_at(q, kPortRowsTop + 80 + 42) > 0,
              "f2: a remove waiting behind an install shows waiting, then removing with the dots");
    }

    {
        Rig r(fonts);
        Catalog c;
        c.ports = {port("blockie-test-a.zip", "action")};
        r.index = {{7, "blockie-test-a", "blockie-test-a.zip", "blockie-test-a", ""}};
        r.sf.set_catalog(true, c, r.now);
        r.flow.input(PadAction::Context, false, r.now);
        r.land();
        r.flow.input(PadAction::Accept, false, r.now);
        r.land();
        r.flow.input(PadAction::Accept, false, r.now);
        r.land();
        r.flow.input(PadAction::Down, false, r.now);
        r.flow.input(PadAction::Accept, false, r.now);
        r.flow.input(PadAction::Up, false, r.now);
        r.flow.input(PadAction::Accept, false, r.now);
        auto band = [&]() {
            Buffer page;
            Canvas pc(page.surface());
            InkLayer pink;
            r.pp.draw(pc, pink, r.now + 1000);
            Surface ps = page.surface();
            uint32_t ground = ps.at(2, kPortraitH - 2);
            int inked = 0;
            for (int y = kPortRowsTop + 80 + 42; y < kPortRowsTop + 80 + 46; y++)
                for (int x = kPortRowX + 100; x < kPortRowClip; x++)
                    inked += ps.at(x, y) != ground ? 1 : 0;
            return inked;
        };
        bool ok = r.helper.running() && r.helper.cmd() == HelperCmd::Remove;
        for (const char* l : {"progress\twait\t1\t10", "progress\twait\t2\t10"}) {
            r.now += 300;
            r.helper.line(l);
            ok = ok && r.pp.words() == "waiting" && !r.pp.arcing() && band() > 0;
        }
        r.now += 300;
        r.helper.line("progress\tremove\t0\t-");
        ok = ok && r.pp.words() == "removing" && !r.pp.arcing() && band() > 0;
        r.helper.line("done\tblockie-test-a.zip");
        r.helper.finish();
        check(ok, "f2: a remove waiting on the helper lock reads waiting with the dots, then removing, never an arc");
    }

    {
        Rig r(fonts);
        Catalog c;
        c.ports = {port("blockie-test-a.zip", "action")};
        c.ports[0].screenshot = "/synthetic/blockie-test-shot-a.jpg";
        r.sf.set_catalog(true, c, r.now);
        r.flow.input(PadAction::Context, false, r.now);
        r.land();
        r.flow.input(PadAction::Accept, false, r.now);
        r.land();
        r.flow.input(PadAction::Accept, false, r.now);
        r.land();
        int opened = r.shots;
        r.sf.set_catalog(true, c, r.now);
        bool same = r.shots == opened;
        c.ports[0].screenshot = "/synthetic/blockie-test-shot-a-new.jpg";
        r.sf.set_catalog(true, c, r.now);
        check(r.flow.nav.top() == Screen::Port && same && r.shots == opened + 1 &&
                  r.pp.item().shot == c.ports[0].screenshot,
              "f2: a refresh that changes the open port's screenshot asks for it again, an unchanged one does not");
    }

    {
        Rig r(fonts);
        r.steam.ports = {{"blockie-test-a.zip", false}};
        bool open = to_port(r);
        check(open && r.pp.action_count(r.now) == 3 && r.pp.focus() == 0,
              "sf1: get in steam.tsv opens the port page on get files above play and remove");
        r.flow.input(PadAction::Accept, false, r.now);
        check(r.helper.running() && r.helper.cmd() == HelperCmd::SteamGet && r.launches == 0,
              "sf1: accept on get files runs steam-get for the port");
        Rig g(fonts);
        g.steam.ports = {{"blockie-test-a.zip", true}};
        open = to_port(g);
        g.flow.input(PadAction::Accept, false, g.now);
        check(open && g.pp.action_count(g.now) == 2 && g.launches == 1 && !g.helper.running(),
              "sf1: a got port has no get files row");
        Rig u(fonts);
        u.steam.ports = {{"blockie-test-a.zip", false}};
        open = to_port(u, true);
        u.flow.input(PadAction::Accept, false, u.now);
        check(open && u.pp.action_count(u.now) == 3 && u.launches == 1 && !u.helper.running(),
              "sf1: with an update offered there is no get files row");
        Rig o(fonts);
        o.steam.ports = {{"blockie-test-a.zip", false}};
        o.online = false;
        open = to_port(o);
        o.flow.input(PadAction::Accept, false, o.now);
        check(open && o.pp.action_count(o.now) == 2 && o.launches == 1 && !o.helper.running(),
              "sf1: offline there is no get files row");
        Rig n(fonts);
        open = to_port(n);
        check(open && n.pp.action_count(n.now) == 2, "sf1: a port with no steam.tsv line has no get files row");
    }

    {
        Rig r(fonts);
        bool run = getting(r);
        r.helper.line("progress\tlogin\t0\t-");
        r.helper.line("progress\tcopy\t0\t3");
        r.flow.input(PadAction::Back, false, r.now);
        check(run && !r.pp.dialog_shown() && r.pp.copying() && r.flow.nav.top() == Screen::Port,
              "sf1: back while copying does nothing");
        Rig d(fonts);
        run = getting(d);
        d.helper.line("progress\tdownload\t1048576\t6012954214");
        d.flow.input(PadAction::Back, false, d.now);
        bool asked = d.pp.dialog_shown();
        d.flow.input(PadAction::Up, false, d.now);
        d.helper.line("progress\tcopy\t0\t3");
        bool closed = !d.pp.dialog_shown();
        d.flow.input(PadAction::Accept, false, d.now);
        check(run && asked && closed && d.helper.running() && d.helper.deadline() < 0,
              "sf1: the first copy line closes an open stop confirm and its accept does nothing");
        Rig s(fonts);
        run = getting(s);
        s.flow.input(PadAction::Back, false, s.now);
        s.flow.input(PadAction::Up, false, s.now);
        s.flow.input(PadAction::Accept, false, s.now);
        check(run && s.helper.deadline() >= 0, "sf1: stop on the stop confirm stops the get");
    }

    {
        Rig r(fonts);
        bool run = getting(r);
        r.helper.line("progress\tcopy\t3\t3");
        r.helper.line("done\tblockie-test-a.zip");
        r.steam.ports = {{"blockie-test-a.zip", true}};
        r.helper.finish();
        r.now += 16;
        bool held = r.pp.action_count(r.now) == 3 &&
                    r.pp.action_count(r.now + kPortDoneHoldMs + eng::step_ms(eng::kTextStep)) == 2;
        r.flow.input(PadAction::Accept, false, r.now);
        bool quiet = r.launches == 0 && !r.helper.running() && r.pp.action_count(r.now) == 2;
        r.flow.input(PadAction::Accept, false, r.now);
        check(run && held && quiet && r.launches == 1 && r.pp.focus() == 0,
              "sf1: a done get holds until its row has gone, an accept in the hold does nothing, then the focus is on "
              "play");
        Rig f(fonts);
        run = getting(f);
        f.helper.line("fail\tblockie-test-a.zip\towned");
        f.steam.ports.clear();
        bool toasts = f.pp.toasting(f.now);
        f.helper.finish();
        check(run && !toasts && f.pp.toasting(f.now) && f.pp.action_count(f.now) == 2,
              "sf1: fail owned toasts and the row goes once steam.tsv drops it");
    }

    {
        Rig r(fonts);
        bool run = getting(r);
        r.helper.line("progress\tlogin\t0\t-");
        r.helper.line("qr\t" + kUrl38);
        bool shown = r.pp.qr_shown();
        r.helper.line("progress\tlogin\t1\t300");
        bool kept = r.pp.qr_shown();
        r.helper.line("qr\thttp://s.team/q/1/1111111111111111111");
        bool refused = !r.pp.qr_shown();
        r.helper.line("qr\t" + kUrl38);
        r.helper.line("progress\tcheck\t0\t-");
        bool checked = !r.pp.qr_shown();
        r.helper.line("qr\t" + kUrl38);
        r.flow.input(PadAction::Back, false, r.now);
        r.flow.input(PadAction::Up, false, r.now);
        r.flow.input(PadAction::Accept, false, r.now);
        bool stopped = !r.pp.qr_shown();
        r.helper.line("qr\t" + kUrl38);
        check(run && shown && kept && refused && checked && stopped && !r.pp.qr_shown(),
              "sf1: the code shows on a qr line and clears on a refused line, a non login line and stop");
        r.helper.finish();
        check(!r.pp.qr_shown(), "sf1: a finished get leaves no code");
    }

    {
        QrGrid v3, v4, v18;
        bool enc = qr_encode(kUrl38, v3) && qr_encode("https://s.team/q/1/" + std::string(41, '2'), v4) &&
                   qr_encode("https://s.team/q/1/" + std::string(493, '3'), v18);
        check(enc && v3.version == 3 && qr_module_px(v3, kStoreShotSide) == 8 && v4.version == 4 &&
                  qr_module_px(v4, kStoreShotSide) == 7 && v18.version == 18 && qr_module_px(v18, kStoreShotSide) == 3,
              "sf1: 38 bytes is version 3 at 8 px, 60 bytes version 4 at 7 px, 512 bytes version 18 at 3 px");
        bool ok[2] = {false, false};
        for (int light = 0; light < 2; light++) {
            set_theme_light(light != 0);
            Rig r(fonts);
            bool run = getting(r);
            r.helper.line("qr\t" + kUrl38);
            Buffer b;
            Canvas c(b.surface());
            InkLayer ink;
            r.pp.draw(c, ink, r.now);
            Surface sb = b.surface();
            Pix dark = LiveF::pack(0x111111), white = LiveF::pack(0xFFFFFF);
            int full = 8 * 37, pad = (kStoreShotSide - full) / 2, x0 = kPortArtX + pad, y0 = kPortArtTop + pad;
            int m0 = x0 + 4 * 8, n0 = y0 + 4 * 8;
            ok[light] = run && sb.at(x0, y0) == white && sb.at(m0 - 1, n0) == white && sb.at(m0, n0) == dark &&
                        sb.at(m0 + 7 * 8 - 1, n0) == dark && sb.at(m0 + 7 * 8, n0) == white &&
                        sb.at(x0 + full - 1, y0 + full - 1) == white && (light || sb.at(x0 - 1, y0) == dark);
        }
        set_theme_light(false);
        check(ok[0] && ok[1],
              "sf1: the code fills the art rect on the page canvas, 8 px modules in a white quiet zone, both themes");
    }

    {
        auto reopen = [&](Rig& r) {
            int64_t id = r.pp.item().id;
            r.flow.nav.stack.pop_back();
            r.flow.pop(Screen::Port);
            r.land();
            r.flow.open_port(HomeGame{id, "", "", ""});
            r.land();
            r.flow.input(PadAction::Back, false, r.now);
            return r.flow.nav.top() == Screen::Port && !r.pp.dialog_shown() && r.pp.copying();
        };
        Rig r(fonts);
        bool run = getting(r);
        r.helper.line("progress\tcopy\t0\t3");
        check(run && reopen(r), "sf1: a page reopened through the deep link mid copy keeps Back doing nothing");
        Rig q(fonts);
        run = getting(q);
        q.helper.line("progress\tcopy\t0\t3");
        q.helper.ask(q.kids, HelperCmd::SteamGet, "blockie-test-b.zip");
        q.helper.ask(q.kids, HelperCmd::Install, "blockie-test-c.zip");
        check(run && reopen(q),
              "sf1: a cancelled queued get for another port keeps the copy hold on the reopened page");
    }

    {
        Rig r(fonts);
        r.helper.ask(r.kids, HelperCmd::SteamLogout, "");
        r.helper.line("fail\tsteam-logout\tother");
        r.helper.finish();
        check(r.toasts == 0, "sf1: a failed sign out leaves its toast to the host");
    }

    {
        auto login = [&](Rig& r) {
            bool run = getting(r);
            r.helper.line("progress\tlogin\t0\t-");
            r.land();
            return run && r.flow.nav.top() == Screen::SteamLogin;
        };
        Rig r(fonts);
        r.sf.l2 = true;
        bool up = login(r);
        size_t depth = r.flow.nav.stack.size();
        r.helper.line("qr\t" + kUrl38);
        r.helper.line("progress\tlogin\t1\t300");
        r.now += kProgressMs;
        r.pp.tick(r.now);
        check(up && r.pp.qr_shown() && r.pp.words() == "signing in 299" && r.flow.nav.top() == Screen::SteamLogin &&
                  r.flow.nav.stack.size() == depth,
              "sf1 l2: a login line pushes the page once, its qr and count lines reach it on top");
        Rig dl(fonts);
        dl.sf.l2 = true;
        bool dots = login(dl) && !dl.pp.arcing();
        check(dots && dl.pp.deadline(dl.now) > dl.now && dl.pp.deadline(dl.now, false) < 0,
              "sf1 l2: the busy dots ask for frames only while the port page is on top");
        r.helper.line("progress\tcheck\t0\t-");
        r.land();
        bool popped = r.flow.nav.top() == Screen::Port && !r.pp.qr_shown() && r.flow.nav.stack.size() == depth - 1;
        r.helper.line("progress\tlogin\t0\t-");
        r.land();
        check(popped && r.flow.nav.top() == Screen::SteamLogin,
              "sf1 l2: a check line pops the page and a retry login pushes it again");
    }

    {
        Rig w(fonts);
        w.sf.l2 = true;
        bool run = getting(w);
        w.now += 10000;
        w.sf.tick(w.now);
        w.flow.input(PadAction::Back, false, w.now);
        w.helper.line("progress\tlogin\t0\t-");
        bool waits = w.flow.nav.top() == Screen::Port && w.pp.login_wait() && w.sf.deadline() < 0;
        w.flow.input(PadAction::Accept, false, w.now);
        int64_t at = w.sf.deadline();
        bool timed = at == w.now + fx_round(tok::motion_dialogHide);
        w.now = at - 1;
        w.sf.tick(w.now);
        bool early = w.flow.nav.top() == Screen::Port;
        w.now = at;
        w.sf.tick(w.now);
        check(run && waits && timed && early && w.flow.nav.top() == Screen::SteamLogin && !w.pp.login_wait(),
              "sf1 l2: a login under an open confirm waits, the deadline is the close's end and the push starts there");
        Rig a(fonts);
        a.sf.l2 = true;
        run = getting(a);
        a.flow.input(PadAction::Back, false, a.now);
        a.helper.line("progress\tlogin\t0\t-");
        a.helper.line("qr\t" + kUrl38);
        a.flow.input(PadAction::Accept, false, a.now);
        Buffer b;
        Canvas c(b.surface());
        InkLayer ink;
        a.pp.draw(c, ink, a.pp.overlays_end());
        Surface sb = b.surface();
        bool bare = true;
        for (int y = kPortArtTop; y < kPortArtTop + kStoreShotSide; y++)
            for (int x = kPortArtX; x < kPortArtX + kStoreShotSide; x++)
                bare = bare && sb.at(x, y) == sb.at(2, kPortraitH - 2);
        check(run && a.pp.qr_shown() && a.pp.login_wait() && bare,
              "sf1 l2: while the push waits the port page's art slot draws no code");
        Rig d(fonts);
        d.sf.l2 = true;
        run = getting(d);
        d.flow.input(PadAction::Back, false, d.now);
        d.helper.line("progress\tlogin\t0\t-");
        d.flow.input(PadAction::Up, false, d.now);
        d.flow.input(PadAction::Accept, false, d.now);
        bool stopped = !d.pp.login_wait();
        d.now += 1000;
        d.sf.tick(d.now);
        Rig f(fonts);
        f.sf.l2 = true;
        run = run && getting(f);
        f.flow.input(PadAction::Back, false, f.now);
        f.helper.line("progress\tlogin\t0\t-");
        f.helper.line("fail\tblockie-test-a.zip\tlogin");
        f.helper.finish();
        bool failed = !f.pp.login_wait();
        f.now += 1000;
        f.sf.tick(f.now);
        check(run && stopped && failed && d.flow.nav.top() == Screen::Port && f.flow.nav.top() == Screen::Port,
              "sf1 l2: stop and a result drop a waiting push");
    }

    {
        auto login = [&](Rig& r) {
            bool run = getting(r);
            r.helper.line("progress\tlogin\t0\t-");
            r.land();
            return run && r.flow.nav.top() == Screen::SteamLogin;
        };
        Rig g(fonts);
        g.sf.l2 = true;
        bool on = login(g);
        size_t depth = g.flow.nav.stack.size();
        g.helper.line("qr\t" + kUrl38);
        g.flow.input(PadAction::Back, false, g.now);
        g.flow.input(PadAction::Up, false, g.now);
        g.flow.input(PadAction::Accept, false, g.now);
        bool clean = !g.pp.qr_shown() && !g.pp.login_dialog_visible(g.now);
        g.land();
        g.helper.line("progress\tlogin\t2\t300");
        g.land();
        bool kept = g.flow.nav.top() == Screen::Port && g.flow.nav.stack.size() == depth - 1;
        g.helper.line("fail\tblockie-test-a.zip\tcancel");
        g.helper.finish();
        g.land();
        Rig o(fonts);
        o.sf.l2 = true;
        on = on && login(o);
        size_t deep = o.flow.nav.stack.size();
        o.helper.line("progress\tcheck\t0\t-");
        o.helper.line("done\tblockie-test-a.zip");
        o.helper.finish();
        o.land();
        check(on && clean && kept && g.flow.nav.top() == Screen::Port && g.flow.nav.stack.size() == depth - 1 &&
                  o.flow.nav.top() == Screen::Port && o.flow.nav.stack.size() == deep - 1,
              "sf1 l2: every pop is guarded, stop then fail cancel and check then done each pop once");
        Rig c(fonts);
        c.sf.l2 = true;
        on = login(c);
        c.flow.input(PadAction::Back, false, c.now);
        c.land();
        bool asked = c.pp.login_dialog_shown();
        c.helper.line("progress\tcheck\t0\t-");
        check(on && asked && !c.pp.login_dialog_visible(c.now) && c.flow.nav.top() == Screen::Port &&
                  c.helper.running(),
              "sf1 l2: a check under the page's open confirm pops it with the confirm gone at once and no stop");
        Rig e(fonts);
        e.sf.l2 = true;
        on = login(e);
        e.helper.line("fail\tblockie-test-a.zip\tlogin");
        e.helper.finish();
        check(on && e.flow.nav.top() == Screen::Port && e.pp.toasting(e.now) && e.toasts == 0,
              "sf1 l2: a result pops the page before its toast, which shows on the port page");
        auto lit = [&](const Surface& s, int& lx, int& ly) {
            Rect b = login_box();
            uint32_t ground = s.at(2, kPortraitH - 2);
            for (int y = b.y0; y < b.y1; y++)
                for (int x = b.x0; x < b.x1; x++)
                    if (s.at(x, y) != ground) {
                        lx = x;
                        ly = y;
                        return true;
                    }
            return false;
        };
        int lx = 0, ly = 0;
        Rig p(fonts);
        p.sf.l2 = true;
        on = login(p);
        p.helper.line("qr\t" + kUrl38);
        p.flow.redraw();
        p.flow.frame(p.now, p.out.surface());
        bool coded = lit(p.out.surface(), lx, ly);
        p.helper.line("progress\tcheck\t0\t-");
        p.flow.frame(p.now, p.out.surface());
        Surface po = p.out.surface();
        check(on && coded && po.at(lx, ly) == po.at(2, kPortraitH - 2),
              "sf1 l2: the check pop's first frame renders the page fresh, with no code in the box");
        Rig q(fonts);
        q.sf.l2 = true;
        on = login(q);
        q.helper.line("qr\t" + kUrl38);
        q.flow.redraw();
        q.flow.frame(q.now, q.out.surface());
        coded = lit(q.out.surface(), lx, ly);
        q.flow.input(PadAction::Back, false, q.now);
        q.land();
        q.flow.input(PadAction::Up, false, q.now);
        q.land();
        q.flow.input(PadAction::Accept, false, q.now);
        q.flow.frame(q.now, q.out.surface());
        Surface qo = q.out.surface();
        check(on && coded && q.flow.nav.top() == Screen::Port && qo.at(lx, ly) == qo.at(2, kPortraitH - 2),
              "sf1 l2: the stop pop's first frame renders the page fresh, with no code in the box");
    }

    {
        auto waiting = [&](Rig& r) {
            bool run = getting(r);
            r.now += 10000;
            r.sf.tick(r.now);
            r.flow.input(PadAction::Back, false, r.now);
            r.helper.line("progress\tlogin\t0\t-");
            return run && r.pp.login_wait();
        };
        Rig m(fonts);
        m.sf.l2 = true;
        bool on = waiting(m);
        size_t depth = m.flow.nav.stack.size();
        m.flow.input(PadAction::Accept, false, m.now);
        m.now = m.sf.deadline();
        m.helper.line("progress\tlogin\t1\t300");
        bool once = m.flow.nav.top() == Screen::SteamLogin && !m.pp.login_wait();
        m.sf.tick(m.now);
        check(on && once && m.flow.nav.stack.size() == depth + 1,
              "sf1 l2: a login line on the close end's wake pushes once and clears the wait");
        Rig n(fonts);
        n.sf.l2 = true;
        on = waiting(n);
        n.helper.line("progress\tcheck\t0\t-");
        n.flow.input(PadAction::Accept, false, n.now);
        n.now = n.pp.overlays_end();
        n.sf.tick(n.now);
        check(on && !n.pp.login_wait() && n.flow.nav.top() == Screen::Port,
              "sf1 l2: a check line drops a waiting push");
        Rig q(fonts);
        q.sf.l2 = true;
        on = waiting(q);
        q.helper.ask(q.kids, HelperCmd::Install, "blockie-test-b.zip");
        q.helper.ask(q.kids, HelperCmd::Install, "blockie-test-c.zip");
        bool dropped = !q.pp.login_wait();
        q.flow.input(PadAction::Accept, false, q.now);
        q.now = q.pp.overlays_end();
        q.sf.tick(q.now);
        bool none = q.flow.nav.top() == Screen::Port;
        q.helper.line("progress\tlogin\t1\t300");
        check(on && dropped && none && q.flow.nav.top() == Screen::SteamLogin,
              "sf1 l2: a replaced ask's cancel drops the waiting push and the next login line pushes");
    }

    {
        Rig r(fonts);
        bool open = to_port(r);
        struct Shot {
            bool active;
            Screen top;
        };
        std::vector<Shot> shots;
        r.sf.host.shot = [&](const StoreItem*) { shots.push_back({r.flow.ctl.active(), r.flow.nav.top()}); };
        r.flow.input(PadAction::Back, false, r.now);
        bool none_yet = shots.empty();
        r.sf.tick(r.now);
        check(open && none_yet && shots.size() == 1 && shots[0].active && shots[0].top != Screen::Port,
              "f2: a port page pop asks for the next shot only once the pop has started");
    }

    {
        Rig r(fonts);
        bool open = to_port(r);
        r.flow.redraw();
        r.flow.input(PadAction::Back, false, r.now);
        check(open && r.flow.ctl.kind() == FoldKind::Pop && std::string(r.flow.ctl.start_tag()) == "rest",
              "c1: a port page pop on the square fold copies the last presented frame, even with a redraw pending");
    }

    {
        GlyphCache glyphs;
        PortPage pp(fonts, glyphs);
        StoreItem it;
        it.name = it.title = "blockie-test-a.zip";
        it.porter = "blockie-test-porter";
        it.genres = {"action"};
        it.size = 4096;
        it.port = 0;
        for (int i = 0; i < 20; i++)
            it.description += std::string(i ? "\n" : "") + "blockie test line " + std::to_string(i);
        pp.open(it, true, false);
        int top = px(tok::toastHeight);
        TextStyle st = role::npArtist.style();
        std::vector<Rect> r0 = pp.line_rects(0);
        bool inside = !r0.empty();
        for (const Rect& r : r0)
            inside = inside && r.y0 >= top && r.y1 <= kBandBottom;
        check(inside && r0[0].y0 == kPortPorterBase - fonts.ascent(st.px) &&
                  r0[0].y1 - r0[0].y0 >= fonts.ascent(st.px) + fonts.descent(st.px),
              "c1: at scroll 0 every line rect is a whole ink box inside 80 to 712, the porter's from its box top");
        pp.action(PadAction::Right, false, 900);
        pp.action(PadAction::Down, false, 1000);
        std::vector<Rect> mid = pp.line_rects(1050), r1 = pp.line_rects(1200);
        bool whole = !r1.empty() && r1[0].y0 > top && r0.size() == r1.size();
        for (const Rect& r : r1)
            whole = whole && r.y0 >= top && r.y1 <= kBandBottom;
        check(whole, "c1: at rest after a step no line is cut at 80 or 712, the porter leaves and a line enters");
        check(!mid.empty() && mid[0].y0 > r0[0].y0 && mid[0].y0 < r1[0].y0,
              "c1: in flight the clip's top moves between the start and target extents");
        check(mid.size() == r0.size() + 1,
              "c1: in flight the drawn set is the start set and the target set, the leaving and entering lines both");
        pp.action(PadAction::Down, false, 1300);
        std::vector<Rect> held = pp.line_rects(1350);
        pp.action(PadAction::Down, false, 1350);
        std::vector<Rect> next = pp.line_rects(1351);
        check(!held.empty() && held.size() == next.size() && std::abs(held[0].y0 - next[0].y0) <= 1,
              "c1: a key mid scroll starts from the frame drawn, its lines and its clip");
    }

    {
        Rig r(fonts);
        bool held = to_hold(r);
        int64_t done = r.now - 16;
        int64_t art = eng::step_ms(eng::kArtStep);
        int64_t end = done + kPortDoneHoldMs + art;
        bool span = r.pp.cross_end() == end && r.pp.animating(end - 1);
        InkLayer ink;
        Buffer b;
        Canvas c(b.surface());
        r.pp.draw(c, ink, done + kPortDoneHoldMs + 1);
        Surface s = b.surface();
        uint32_t g = s.at(kPortRowX / 2, kPortRowsTop + 120);
        int pitch = px(sz().rowHeight);
        int x0 = kPortRowX + px(tok::hubEdge - tok::focus_barOffset) + px(tok::focus_barWidth);
        auto inked = [&](int k, int lim) {
            int n = 0;
            for (int y = kPortRowsTop + pitch * k; y < kPortRowsTop + pitch * (k + 1); y++)
                for (int x = x0; x < kPortRowClip; x++)
                    for (int sh = 0; sh < 24; sh += 8)
                        n += std::abs((int)(s.at(x, y) >> sh & 0xFF) - (int)(g >> sh & 0xFF)) > lim;
            return n;
        };
        bool slots = inked(0, 40) > 0 && inked(1, 2) == 0;
        r.now = done + kPortDoneHoldMs + art / 2;
        r.pp.tick(r.now);
        r.flow.input(PadAction::Accept, false, r.now);
        bool quiet = r.launches == 0 && r.pp.cross_end() < r.now && r.pp.action_count(r.now) == 2;
        r.flow.input(PadAction::Accept, false, r.now);
        check(held && span && slots && quiet && r.launches == 1,
              "store block: an install done holds 250 ms, then play crosses in place and remove glows in at the art "
              "step, all inside the hold, so an accept in the glow lands it and does nothing, the next plays");
    }

    {
        Rig r(fonts);
        bool run = getting(r);
        r.steam.ports.clear();
        r.helper.line("fail\tblockie-test-a.zip\tnetwork");
        r.helper.finish();
        check(run && r.pp.action_count(r.now) == 2 && r.pp.cross_end() < r.now,
              "store block: a fail changes its rows at once, no cross");
    }

    {
        Rig r(fonts);
        bool held = to_hold(r);
        r.flow.input(PadAction::Down, false, r.now);
        bool landed = r.pp.cross_end() < r.now;
        Catalog c;
        c.ports = {port("blockie-test-a.zip", "action")};
        r.index.clear();
        r.sf.set_catalog(true, c, r.now);
        check(held && landed && r.pp.cross_end() < r.now && r.pp.action_count(r.now) == 1,
              "store block: a key that lands a done hold leaves nothing for a later change to cross");
    }

    {
        Rig r(fonts);
        bool held = to_hold(r);
        r.helper.on_start(HelperCmd::Remove, r.pp.item().name);
        check(held && r.pp.cross_end() < r.now && !r.pp.animating(r.now),
              "store block: a queued remove that starts inside a done hold lands the hold, nothing left to cross");
    }

    {
        Rig r(fonts);
        Catalog c;
        c.ports = {port("blockie-test-a.zip", "action")};
        r.sf.set_catalog(true, c, r.now);
        r.flow.input(PadAction::Context, false, r.now);
        r.land();
        bool open = r.flow.nav.store;
        int renders = 0;
        bool dealt = false;
        auto draw = r.flow.ctl.render;
        r.flow.ctl.render = [&r, &renders, &dealt, draw](const Surface& s, Rect b) {
            if (r.flow.ctl.active()) {
                renders++;
                dealt = r.flow.home.recent.size() == 1 && r.flow.home.recent[0].id == 9;
            }
            draw(s, b);
        };
        r.sf.host.closed = [&r]() { r.flow.recent_changed({HomeGame{9, "blockie-test-b", "", ""}}); };
        r.flow.input(PadAction::Back, false, r.now);
        bool out = r.flow.ctl.active() && r.flow.ctl.kind() == FoldKind::Pop && !r.flow.nav.store;
        r.land();
        check(open && out && renders == 1 && dealt,
              "store block: the store's close deals Recent before its fold renders the home, and renders it once");
    }

    {
        Rig r(fonts);
        Catalog c;
        c.ports = {port("blockie-test-a.zip", "action")};
        r.sf.set_catalog(true, c, r.now);
        r.flow.input(PadAction::Context, false, r.now);
        int64_t t0 = r.now;
        bool opening = r.flow.nav.store && r.flow.ctl.active() && r.flow.ctl.kind() == FoldKind::Push;
        while (r.now < t0 + 100) {
            r.flow.frame(r.now, r.out.surface());
            r.now += 16;
        }
        r.now = t0 + 100;
        bool mid = r.flow.ctl.active() && r.flow.ctl.kind() == FoldKind::Push;
        int renders = 0;
        bool dealt = false;
        auto draw = r.flow.ctl.render;
        r.flow.ctl.render = [&r, &renders, &dealt, draw](const Surface& s, Rect b) {
            if (r.flow.ctl.active()) {
                renders++;
                dealt = r.flow.home.recent.size() == 1 && r.flow.home.recent[0].id == 9;
            }
            draw(s, b);
        };
        r.sf.host.closed = [&r]() { r.flow.recent_changed({HomeGame{9, "blockie-test-b", "", ""}}); };
        r.flow.input(PadAction::Back, false, r.now);
        bool out = r.flow.ctl.active() && !r.flow.nav.store;
        r.land();
        check(opening && mid && out && renders == 1 && dealt,
              "store block: a back 100 ms into the store's open deals Recent before the close renders the home, "
              "and renders it once");
    }

    {
        int pitch = px(sz().rowHeight);
        int x0 = kPortRowX + px(tok::hubEdge - tok::focus_barOffset) + px(tok::focus_barWidth);
        const int64_t at[] = {0, 17, 33, 50, 67};
        const int rise[] = {0, 64, 125, 189, 254};
        auto shoot = [](Rig& r, Buffer& b, int64_t t) {
            InkLayer ink;
            Canvas c(b.surface());
            r.pp.draw(c, ink, t);
        };
        auto dist = [](const Surface& s, int x, int y) {
            uint32_t g = s.at(kPortRowX / 2, kPortRowsTop + 120);
            int d = 0;
            for (int sh = 0; sh < 24; sh += 8)
                d = std::max(d, std::abs((int)(s.at(x, y) >> sh & 0xFF) - (int)(g >> sh & 0xFF)));
            return d;
        };
        auto peak = [&](const Surface& s, int k) {
            int d = 0;
            for (int y = kPortRowsTop + pitch * k; y < kPortRowsTop + pitch * (k + 1); y++)
                for (int x = x0; x < kPortRowClip; x++)
                    d = std::max(d, dist(s, x, y));
            return d;
        };
        auto band_ink = [&](const Surface& s, int64_t* n) {
            int64_t sum = 0;
            for (int y = kPortRowsTop; y < kPortRowsTop + 3 * pitch; y++)
                for (int x = x0; x < kPortRowClip; x++) {
                    int d = dist(s, x, y);
                    sum += d;
                    if (n && d > 0)
                        (*n)++;
                }
            return sum;
        };
        auto steady = [&](Rig& r, int64_t hold) {
            Buffer b;
            int64_t n = 0, end = r.pp.cross_end();
            shoot(r, b, hold);
            int64_t first = band_ink(b.surface(), &n);
            std::vector<int64_t> mids;
            for (int64_t t : at)
                if (t > 0 && hold + t < end) {
                    shoot(r, b, hold + t);
                    mids.push_back(band_ink(b.surface(), nullptr));
                }
            shoot(r, b, end);
            int64_t last = band_ink(b.surface(), &n), low = std::min(first, last);
            bool ok = end > hold && !mids.empty();
            for (int64_t v : mids)
                ok = ok && v + n >= low;
            return ok;
        };
        auto updated_run = [](Rig& r) {
            bool open = to_port(r, true);
            r.flow.input(PadAction::Down, false, r.now);
            r.flow.input(PadAction::Accept, false, r.now);
            return open && r.helper.running() && r.helper.cmd() == HelperCmd::Update && r.pp.focus() == 1;
        };
        auto updated = [&](Rig& r) {
            bool asked = updated_run(r);
            r.helper.line("done\tblockie-test-a.zip");
            r.helper.finish();
            return asked && r.pp.action_count(r.now) == 3;
        };

        Rig u(fonts);
        bool up = updated(u);
        int64_t hold = u.now + kPortDoneHoldMs;
        bool span = u.pp.cross_end() == hold + 68 && u.pp.action_count(hold + 67) == 3 &&
                    u.pp.action_count(hold + 68) == 2;
        Buffer f0, mid, b, end;
        shoot(u, f0, hold);
        int full = peak(f0.surface(), 2);
        bool table = full > 40, falling = false, step_left = false;
        for (int i = 1; i < 5; i++) {
            Buffer& fr = i == 2 ? mid : b;
            shoot(u, fr, hold + at[i]);
            int d = peak(fr.surface(), 2);
            table = table && eng::step_a256(eng::kTextStep, at[i]) == rise[i] &&
                    std::abs(d - full * (256 - rise[i]) / 256) <= 3;
            falling = i == 3 ? d > 2 : falling;
            step_left = i == 4 ? d <= 3 : step_left;
        }
        shoot(u, end, u.pp.cross_end());
        bool ground = peak(end.surface(), 2) == 0;
        int seen[2] = {0, 0}, miss = 0;
        for (int y = kPortRowsTop + pitch; y < kPortRowsTop + 2 * pitch; y++)
            for (int x = x0; x < kPortRowClip; x++) {
                int o = dist(f0.surface(), x, y), n = dist(end.surface(), x, y), m = dist(mid.surface(), x, y);
                for (int side = 0; side < 2; side++)
                    if ((side ? n : o) > 40 && (side ? o : n) <= 2) {
                        seen[side] += m > 8;
                        miss += m <= 8;
                    }
            }
        check(up && seen[0] > 0 && seen[1] > 0 && miss == 0,
              "store block: an update done crosses update to remove in place, the frame mid cross holds both "
              "words' ink");
        check(up && span && table,
              "store block: the cross down follows the engine's text step, 68 ms from the hold's end, the falling "
              "row's ink at each 60 Hz frame as the table gives");
        check(up && falling && step_left && ground,
              "store block: the last row's fall reaches ground at the landing, one step from ground on the frame "
              "before");

        Rig su(fonts), sr(fonts), sg(fonts);
        bool ready = updated(su) && steady(su, su.now + kPortDoneHoldMs);
        bool removed = to_remove(sr);
        sr.index.clear();
        sr.helper.line("done\tblockie-test-a.zip");
        sr.helper.finish();
        removed = removed && sr.pp.action_count(sr.now + kPortDoneHoldMs + 68) == 1 &&
                  steady(sr, sr.now + kPortDoneHoldMs);
        bool got = getting(sg);
        sg.helper.line("done\tblockie-test-a.zip");
        sg.steam.ports = {{"blockie-test-a.zip", true}};
        sg.helper.finish();
        got = got && steady(sg, sg.now + kPortDoneHoldMs);
        check(ready && removed && got,
              "store block: no frame of an update, remove or get done's cross down is emptier than both ends in "
              "the rows band");

        Rig ka(fonts), kb(fonts);
        bool held = updated(ka) && updated(kb);
        ka.now += kPortDoneHoldMs - 100;
        ka.flow.input(PadAction::Accept, false, ka.now);
        bool landed = ka.pp.focus() == 0 && ka.pp.action_count(ka.now) == 2 && ka.pp.cross_end() < ka.now &&
                      !ka.pp.animating(ka.now) && ka.launches == 0;
        Buffer la, lb;
        shoot(ka, la, ka.now);
        shoot(kb, lb, kb.pp.cross_end());
        bool same = true;
        for (int y = kPortRowsTop; y < kPortraitH; y++)
            for (int x = 0; x < kPaneSplit; x++)
                same = same && la.surface().at(x, y) == lb.surface().at(x, y);
        check(held && landed && same,
              "store block: a key in an update done's hold lands on the end rows, as the cross down's landing "
              "draws them");

        auto stays = [&](Rig& r, bool get) {
            bool run = get ? getting(r) : updated_run(r);
            r.online = false;
            r.sf.online_changed(r.now);
            r.flow.input(PadAction::Back, false, r.now);
            r.flow.input(PadAction::Up, false, r.now);
            r.flow.input(PadAction::Accept, false, r.now);
            int64_t t = r.now;
            while (!r.pp.overlays_idle(r.now) && r.now < t + 1000)
                r.now += 16;
            r.helper.line("fail\tblockie-test-a.zip\tcancel");
            r.helper.finish();
            int64_t at0 = r.now, fin = r.pp.cross_end();
            Buffer s0, m, e;
            shoot(r, s0, at0);
            shoot(r, m, at0 + 50);
            shoot(r, e, fin);
            int full = peak(s0.surface(), 2), moved[2] = {0, 0}, missed = 0;
            for (int y = kPortRowsTop; y < kPortRowsTop + 2 * pitch; y++)
                for (int x = x0; x < kPortRowClip; x++)
                    if (dist(e.surface(), x, y) > 40 && dist(s0.surface(), x, y) <= 2) {
                        moved[(y - kPortRowsTop) / pitch] += dist(m.surface(), x, y) > 8;
                        missed += dist(m.surface(), x, y) <= 8;
                    }
            return run && fin == at0 + 68 && r.pp.action_count(fin) == 2 && full > 40 &&
                   peak(m.surface(), 2) <= full / 2 && moved[1] > 0 && (!get || moved[0] > 0) && missed == 0;
        };
        Rig xu(fonts), xg(fonts);
        check(stays(xu, false) && stays(xg, true),
              "store block: a stop taken offline on an update or a get crosses each row in place, no row draws "
              "away from its final row");

        auto above = [&](bool get) {
            Rig t(fonts), d(fonts), a(fonts);
            int row = get ? 1 : 0, rows = get ? 3 : 2;
            bool ready = true;
            for (Rig* r : {&t, &d, &a}) {
                if (get)
                    r->steam.ports = {{"blockie-test-a.zip", false}};
                ready = ready && updated(*r) && r->pp.action_count(r->pp.cross_end()) == rows;
            }
            t.now = t.pp.cross_end();
            t.pp.tick(t.now);
            bool timed = t.pp.focus() == row;
            t.flow.input(PadAction::Accept, false, t.now);
            timed = timed && t.launches == 1 && !t.helper.running();
            d.flow.input(PadAction::Down, false, d.now);
            bool down = d.pp.focus() == row + 1 && d.pp.action_count(d.now) == rows;
            a.flow.input(PadAction::Accept, false, a.now);
            bool quiet = a.launches == 0 && !a.helper.running() && a.pp.focus() == row;
            a.flow.input(PadAction::Accept, false, a.now);
            return ready && timed && down && quiet && a.launches == 1 && !a.helper.running();
        };
        check(above(false),
              "store block: an update done moves the focus to play, the row above update, a key in the hold lands "
              "there first");
        check(above(true),
              "store block: an update done that brings get files back moves the focus to play, the row above "
              "update, now row 1, a key in the hold lands there first");
    }

    {
        Rig r(fonts);
        bool asked = to_remove(r);
        r.flow.input(PadAction::Back, false, r.now);
        const Overlay& d = r.pp.dialog_box();
        bool words = d.shown && d.title == "stop removing" && d.items.size() == 2 && d.items[0] == "stop" &&
                     d.items[1] == "keep removing" && d.focus == 1;
        r.flow.input(PadAction::Accept, false, r.now);
        check(asked && words && !r.pp.dialog_shown() && r.helper.running() && r.helper.deadline() < 0,
              "store block: back during a running remove asks stop removing with keep removing, and keep removing "
              "closes it");
    }

    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
