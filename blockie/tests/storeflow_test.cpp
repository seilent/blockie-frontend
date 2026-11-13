#include <cstdio>
#include <string>
#include <vector>

#include "../store_flow.h"

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
        check(asked && held && r.pp.focus() == 0 && r.pp.action_count(r.now) == 1,
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
        o.now += kPortDoneHoldMs;
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
        s.now += kPortDoneHoldMs;
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
        for (int y = 522; y < 526; y++)
            for (int x = 100; x < kPortRowClip; x++)
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
                for (int x = 100; x < kPortRowClip; x++)
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
                for (int x = 100; x < kPortRowClip; x++)
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

    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
