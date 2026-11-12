#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <signal.h>
#include <sys/wait.h>

#include "../../gfx/compose.h"
#include "../frame_gate.h"
#include "../homeflow.h"
#include "../launchflow.h"
#include "../leftmove.h"
#include "../theme.h"
#include "../toast_strip.h"

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

constexpr const char* kNs = "game not started";
constexpr uint32_t kDrop = 0x40C080;

int exited(int code) { return W_EXITCODE(code, 0); }

void copy(const Surface& src, const Surface& dst)
{
    for (int y = 0; y < dst.h; y++)
        copy_px(dst.row(y), src.row(y), dst.w);
}

bool equal(const Surface& a, const Surface& b)
{
    for (int y = 0; y < a.h; y++)
        if (!std::equal(a.row(y), a.row(y) + a.w, b.row(y)))
            return false;
    return true;
}

bool filled(const Surface& s, Rect r, uint32_t rgb)
{
    uint32_t v = LiveF::pack(rgb);
    for (int y = r.y0; y < r.y1; y++)
        for (int x = r.x0; x < r.x1; x++)
            if (s.at(x, y) != v)
                return false;
    return true;
}

bool near(int a, int b) { return a - b <= 1 && b - a <= 1; }

bool holds_color(const Surface& s, uint32_t rgb)
{
    for (int y = 0; y < s.h; y++)
        for (int x = 0; x < s.w; x++) {
            uint32_t v = LiveF::unpack(s.at(x, y));
            bool r = near((int)(v >> 16 & 0xFF), (int)(rgb >> 16 & 0xFF));
            bool g = near((int)(v >> 8 & 0xFF), (int)(rgb >> 8 & 0xFF));
            if (r && g && near((int)(v & 0xFF), (int)(rgb & 0xFF)))
                return true;
        }
    return false;
}

Art solid(uint32_t v)
{
    auto img = std::make_shared<Image>();
    img->w = 2;
    img->h = 2;
    img->mem.assign(4, LiveF::pack(v));
    return img;
}

bool words(const LaunchOutcome& o, const char* l1, const char* l2) { return o.line1 == l1 && o.line2 == l2; }

FlowOpts opts(bool big)
{
    FlowOpts o;
    o.big = big;
    return o;
}

struct Rig {
    HomeFlow flow;
    LaunchFlow launch;
    Repeat repeat;
    LaunchKeys keys{launch, repeat};
    std::vector<std::pair<PadAction, int64_t>> steps;
    Buffer shown;
    bool active = true;
    bool child = false;
    bool pending = false;
    HomeGame played;
    int blacks = 0;
    std::vector<std::string> landed;
    std::vector<std::string> toasts;
    std::unordered_set<int64_t> missing;
    std::vector<std::pair<int64_t, Art>> held;

    Rig(Fonts& fonts, bool big) : flow(fonts, opts(big))
    {
        flow.model.systems = {{"GBA", 40, ""}, {"GB", 3, ""}};
        flow.kinds = {MenuKind::System, MenuKind::System};
        flow.games = [](int sys) {
            std::vector<HomeGame> list;
            int n = sys == 0 ? 40 : 3;
            for (int i = 0; i < n; i++)
                list.push_back(
                    {1000 * (sys + 1) + i, "Synth Launch Game " + std::to_string(i), sys == 0 ? "GBA" : "GB"});
            return list;
        };
        flow.home.load = [this](const HomeGame& g) -> Art {
            if (missing.count(g.id))
                return nullptr;
            return solid(0x203040u + (uint32_t)(g.id % 97) * 0x020103u);
        };
        flow.last_frame = [this](const Surface& s) { copy(shown.surface(), s); };
        flow.launch_game = [this](const HomeGame& g) {
            played = g;
            child = true;
            launch.start(true, active);
            return LaunchStart::Leave;
        };
        flow.setup();
        flow.ctl.can_animate = [this]() { return launch.can_animate(active, child); };
        flow.ctl.landed = [this](const char* what, const char* why) {
            landed.push_back(std::string(what) + ":" + why);
            launch.landed(what);
        };
        LaunchHooks& h = launch.hooks;
        h.land = [this](int64_t now, const char* why) { flow.land_all(now, why); };
        h.reverse = [this](int64_t now) { return flow.launch_back(now); };
        h.fold_end = [this]() { return flow.ctl.active() ? flow.ctl.t0() + flow.ctl.run_length() : (int64_t)-1; };
        h.finish = [this](int64_t now) { flow.ctl.finish(now); };
        h.come_back = [this](int64_t now) { flow.come_back(now); };
        h.covers_pending = [this]() { return pending; };
        h.played = [this]() { flow.game_ran(played_first(flow.home.recent, played, flow.home.recent_cap)); };
        h.theme = [this](bool light) {
            set_theme_light(light);
            flow.theme_changed();
        };
        h.drops = [this]() {
            for (const auto& d : held)
                flow.art(d.first, kTileSide, flow.home.epoch, d.second);
            held.clear();
        };
        h.toast = [this](const std::string& line1, const std::string& line2) {
            toasts.push_back(line1 + "/" + line2);
        };
        h.act = [this](PadAction a, int64_t now) { keys.act(a, now); };
        keys.apply = [this](PadAction a, bool rep, int64_t now) {
            steps.push_back({a, now});
            flow.input(a, rep, now);
        };
    }

    void drop(int64_t id, const Art& a) { held.push_back({id, a}); }

    void theme_to(bool light) { launch.theme(light); }

    void recent(int n)
    {
        std::vector<HomeGame> list;
        for (int k = 0; k < n; k++)
            list.push_back({900 + k, "Synth Recent " + std::to_string(k), "GB"});
        flow.home.deal_recent(flow.model, list);
    }

    bool saw(const std::string& s) const { return std::find(landed.begin(), landed.end(), s) != landed.end(); }

    void hold(PadAction a, int64_t now) { keys.press(a, kFromKeys, !child, now); }

    void release(PadAction a, int64_t now) { repeat.release(a, kFromKeys, now); }

    void press(PadAction a, int64_t now)
    {
        hold(a, now);
        release(a, now);
    }

    bool frame(int64_t now)
    {
        while (repeat.active() && repeat.deadline() <= now) {
            int64_t d = repeat.deadline();
            PadAction a = PadAction::Up;
            if (repeat.due(d, a))
                keys.repeat(a, d);
        }
        if (!launch.can_animate(active, child))
            flow.land_all(now, child ? "child" : "hidden");
        launch.step(now);
        LaunchDraw kind = launch.draw(true);
        if (kind == LaunchDraw::Black) {
            Canvas c(shown.surface());
            c.fill(kScreen, tok::color_black);
            launch.black_shown();
            blacks++;
            return true;
        }
        if (kind == LaunchDraw::None)
            return false;
        flow.frame(now, shown.surface());
        return true;
    }

    int64_t settle(int64_t t)
    {
        for (int n = 0; n < 600; n++, t += 16) {
            frame(t);
            if (!flow.animating(t) && launch.phase() != LaunchPhase::Leaving && !launch.waiting_covers())
                break;
        }
        return t + 16;
    }

    LaunchOutcome reap(int status, bool stamp, int64_t now)
    {
        child = false;
        return launch.reap(status, stamp, "mgba", now, now + 300);
    }

    void cover(int64_t now)
    {
        active = false;
        launch.edges(false, child, now);
    }

    Buffer rest(int64_t now)
    {
        Buffer b;
        flow.at(now);
        flow.draw(b.surface(), flow.model, flow.nav, false, flow.nav.pane, kScreen);
        return b;
    }

    int64_t enter(int64_t t)
    {
        t = settle(t);
        press(PadAction::Accept, t);
        return settle(t);
    }
};

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
        struct Status {
            int status;
            const char* name;
            int cls;
            const char* l1;
            const char* l2;
        };
        const Status statuses[] = {
            {exited(75), "busy 75", 0, kNs, "another game is closing"},
            {exited(64), "config 64", 0, kNs, "game settings refused"},
            {exited(127), "exec 127", 0, kNs, "launcher missing"},
            {exited(69), "core 69", 0, "core not installed", "mgba"},
            {exited(71), "audio 71", 0, kNs, "audio not ready"},
            {exited(70), "crash 70", 1, kNs, "the emulator stopped"},
            {exited(1), "crash 1", 1, kNs, "the emulator stopped"},
            {exited(0), "quit 0", 2, kNs, "the core closed it"},
            {exited(129), "quit 129", 2, kNs, "the core closed it"},
            {exited(130), "quit 130", 2, kNs, "the core closed it"},
            {exited(143), "quit 143", 2, kNs, "the core closed it"},
            {W_EXITCODE(0, SIGKILL), "a signal", 2, kNs, "the core closed it"},
        };
        const LaunchPhase phases[] = {LaunchPhase::Leaving, LaunchPhase::Waiting, LaunchPhase::Covered};
        int cells = 0, bad = 0;
        bool loud = true;
        for (const Status& x : statuses)
            for (LaunchPhase at : phases)
                for (bool stamp : {false, true}) {
                    bool covered = at == LaunchPhase::Covered;
                    bool quit = x.cls == 2;
                    LaunchMotion motion = at == LaunchPhase::Leaving && !(quit && stamp) ? LaunchMotion::Reverse
                                                                                         : LaunchMotion::Return;
                    bool played = covered && x.cls != 0;
                    std::string l1 = x.cls == 1 && covered ? "game closed" : x.l1;
                    std::string l2 = x.l2;
                    if (quit && (covered || stamp)) {
                        l1.clear();
                        l2.clear();
                    }
                    LaunchOutcome o = launch_outcome(x.status, at, stamp, "mgba");
                    cells++;
                    if (o.motion == motion && o.played == played && o.line1 == l1 && o.line2 == l2)
                        continue;
                    bad++;
                    std::printf("INFO cell %s %s %s: motion %d played %d words %s / %s\n", x.name,
                                launch_phase_name(at), stamp ? "stamped" : "unstamped", (int)o.motion, o.played ? 1 : 0,
                                o.line1.c_str(), o.line2.c_str());
                    if (!quit && stamp && o.line1.empty())
                        loud = false;
                }
        check(cells == 72 && bad == 0, "the exit table, every status by leaving, waiting and covered, with and without"
                                       " the quit stamp, as ui-consistency 3.5");
        check(loud, "the quit stamp mutes only a quit; every other class keeps its toast with the stamp present");
        LaunchOutcome p = launch_refused(true, "mgba");
        LaunchOutcome s = launch_refused(false, "mgba");
        check(p.motion == LaunchMotion::None && !p.played && words(p, "core not installed", "mgba"),
              "a precheck refusal: no motion, core not installed and the core id");
        check(s.motion == LaunchMotion::None && !s.played && words(s, kNs, "could not start the launcher"),
              "a spawn failure: no motion, could not start the launcher");
    }

    {
        LaunchOutcome quit = port_outcome(exited(0), LaunchPhase::Waiting, false);
        LaunchOutcome stamped = port_outcome(exited(0), LaunchPhase::Waiting, true);
        LaunchOutcome oom = port_outcome(exited(1), LaunchPhase::Covered, false);
        LaunchOutcome oom_early = port_outcome(exited(1), LaunchPhase::Waiting, false);
        LaunchOutcome gone = port_outcome(exited(64), LaunchPhase::Leaving, false);
        LaunchOutcome busy = port_outcome(exited(75), LaunchPhase::Waiting, false);
        LaunchOutcome played = port_outcome(exited(0), LaunchPhase::Covered, false);
        check(words(quit, kNs, "the port quit") && words(stamped, "", ""), "a port that quits before covered says so");
        check(oom.played && words(oom, "game closed", "out of memory"), "the oom kill of a covered port");
        check(!oom_early.played && words(oom_early, kNs, "out of memory"), "the oom kill before covered");
        check(words(gone, kNs, "the port's files are missing"), "the wrapper's 64 path refusal for a port");
        check(words(busy, kNs, "another game is closing"), "the other refusals keep their words for a port");
        check(played.played && words(played, "", ""), "a port quit after covered is played with no toast");
        check(words(launch_helper_busy(HelperOp::Install), kNs, "a port is installing") &&
                  words(launch_helper_busy(HelperOp::Update), kNs, "a port is updating") &&
                  words(launch_helper_busy(HelperOp::Remove), kNs, "a port is being removed"),
              "the helper busy refusal names what the helper does");
        LaunchFlow f;
        f.start(false, true, true);
        LaunchOutcome r = f.reaped(exited(1), false, "", 0);
        check(words(r, kNs, "out of memory"), "a launch started as a port reaps with the port words");
        f.start(false, true);
        LaunchOutcome g = f.reaped(exited(1), false, "mgba", 0);
        check(words(g, kNs, "the emulator stopped"), "a game launch after a port keeps the game words");
    }

    {
        LaunchFlow f;
        bool idle = f.phase() == LaunchPhase::Idle && f.press(PadAction::Up) == LaunchInput::Act && !f.holds() &&
                    f.draws() && f.can_animate(true, false) && !f.can_animate(false, false);
        f.start(true, true);
        bool leaving = f.phase() == LaunchPhase::Leaving && f.press(PadAction::Up) == LaunchInput::Drop && f.holds() &&
                       f.draws() && f.can_animate(true, true) && f.can_animate(false, true) && !f.black_owed();
        f.landed("t9");
        f.landed("launch");
        bool waiting = f.phase() == LaunchPhase::Waiting && f.press(PadAction::Up) == LaunchInput::Drop && f.holds() &&
                       !f.draws() && !f.can_animate(true, true) && f.black_owed();
        f.black_shown();
        bool once = !f.black_owed() && !f.draws();
        bool same = !f.activation(true, true) && f.phase() == LaunchPhase::Waiting;
        bool covered = f.activation(false, true) && f.phase() == LaunchPhase::Covered && f.black_owed();
        f.black_shown();
        bool kept = !f.activation(true, true) && f.phase() == LaunchPhase::Covered && !f.activation(false, true) &&
                    f.phase() == LaunchPhase::Covered;
        f.landed("launch");
        LaunchOutcome o = f.reaped(exited(0), false, "mgba", 1300);
        bool returning = o.played && o.motion == LaunchMotion::Return && f.phase() == LaunchPhase::Returning &&
                         !f.holds() && !f.draws() && f.waiting_covers() && f.cover_deadline() == 1300 &&
                         f.can_animate(false, false) && !f.black_owed();
        std::vector<PadAction> acted;
        f.hooks.act = [&acted](PadAction a, int64_t) { acted.push_back(a); };
        bool keep = f.press(PadAction::Right) == LaunchInput::Keep && acted.empty() && f.waiting_covers() &&
                    f.deadline() == 1300;
        f.back();
        bool back = f.draws() && f.holds() && !f.waiting_covers() && f.cover_deadline() == -1;
        f.landed("launch");
        bool still = f.phase() == LaunchPhase::Returning;
        f.landed("return");
        std::string l1, l2;
        bool home = f.phase() == LaunchPhase::Idle && !f.take_toast(l1, l2);
        f.activation(true, false);
        bool due = acted.empty() && f.deadline() == 0;
        f.step(1467);
        bool acts = acted.size() == 1 && acted[0] == PadAction::Right && f.deadline() == -1;
        check(idle, "idle: input taken, nothing held, the home drawn");
        check(leaving, "leaving: input dropped, drops and theme held, frames run with a child and inactive");
        check(waiting && once,
              "a landing of the launch while leaving goes to waiting with one black frame owed, then none");
        check(same, "a map and unmap in one dispatch reads as active twice and is not covered");
        check(covered && kept, "the first deactivation with a child covers; a re-activation keeps it covered");
        check(returning, "only the reap leaves covered: returning, played, the cover wait to its deadline, frames"
                         " allowed with the toplevel not activated");
        check(back && still && home && !f.holds(),
              "the return fold starts after the wait, holds drops and theme until it lands to idle; a quit shows no"
              " toast");
        check(keep && due && acts, "a press while returning is kept, not acted, leaves the cover wait alone, and acts"
                                   " once on the first step at idle");

        LaunchFlow r;
        r.start(true, true);
        bool lone = !r.activation(false, false) && r.phase() == LaunchPhase::Leaving;
        LaunchOutcome b = r.reaped(exited(75), false, "mgba", 0);
        bool rev = b.motion == LaunchMotion::Reverse && r.phase() == LaunchPhase::Leaving && !r.take_toast(l1, l2);
        r.landed("launch");
        bool toast = r.phase() == LaunchPhase::Idle && !r.black_owed() && r.take_toast(l1, l2) && l1 == kNs &&
                     l2 == "another game is closing" && !r.take_toast(l1, l2);
        check(lone, "a deactivation with no child does not cover");
        check(rev && toast, "a busy reap while leaving reverses, lands to idle and then gives its toast once");

        LaunchFlow c;
        c.start(false, true);
        bool cut = c.phase() == LaunchPhase::Waiting && c.cut() && !c.black_owed();
        bool cut_cover = c.activation(false, true) && c.phase() == LaunchPhase::Covered && !c.black_owed();
        LaunchOutcome q = c.reaped(exited(70), false, "mgba", 0);
        bool cut_back = q.motion == LaunchMotion::None && q.played && c.phase() == LaunchPhase::Idle &&
                        c.take_toast(l1, l2) && l1 == "game closed";
        check(cut && cut_cover && cut_back, "arm A: no leave, no black frames, the reap cuts to the home");

        LaunchFlow w;
        w.start(true, true);
        w.landed("launch");
        LaunchOutcome s = w.reaped(exited(0), true, "mgba", 500);
        check(!s.played && s.line1.empty() && w.phase() == LaunchPhase::Returning,
              "a reap with the quit stamp while waiting returns with no toast");

        LaunchFlow v;
        v.start(true, true);
        LaunchOutcome z = v.reaped(exited(0), true, "mgba", 900);
        bool runs = z.motion == LaunchMotion::Return && v.phase() == LaunchPhase::Leaving && v.draws() &&
                    !v.black_owed() && v.reaped(exited(0), true, "mgba", 900).motion == LaunchMotion::None;
        v.landed("launch");
        check(runs && v.phase() == LaunchPhase::Returning && v.black_owed() && v.waiting_covers() &&
                  v.cover_deadline() == 900,
              "a quit with the stamp while leaving lets the leave run to its end, then returns with one black"
              " frame owed for the wait");
        LaunchFlow n;
        n.start(true, true);
        LaunchOutcome nz = n.reap(exited(0), true, "mgba", 100, 400);
        bool lands = nz.motion == LaunchMotion::Return && n.phase() == LaunchPhase::Returning && n.black_owed() &&
                     n.cover_deadline() == 400;
        LaunchFlow m;
        m.hooks.fold_end = []() { return (int64_t)500; };
        m.start(true, true);
        m.reap(exited(0), true, "mgba", 100, 400);
        bool stays = m.phase() == LaunchPhase::Leaving && !m.black_owed();
        check(lands && stays, "a quit with the stamp while leaving with no leave fold running lands at once into the"
                              " return; with the fold running it stays leaving");
        LaunchFlow k;
        k.start(true, true);
        k.activation(false, true);
        k.reaped(exited(0), false, "mgba", 900);
        check(k.phase() == LaunchPhase::Returning && k.black_owed(),
              "a black frame owed on the cover is still owed after a reap in the same dispatch");
    }

    {
        bool leave = leave_a256(0) == 256 && leave_a256(16) == alpha256(kOne - (fx)(16 * kOne / 33)) &&
                     leave_a256(33) == 0 && leave_a256(50) == 0 && launch_leave_ms(false) == 33 &&
                     launch_leave_ms(true) == 333;
        bool fade = return_a256(0) == 0 && return_a256(52) == 0 && return_a256(287) == 256 &&
                    near(return_a256(52 + 235 / 2), 128) && return_a256(367) == 256;
        int prev = return_dx(0);
        bool mono = true;
        for (int64_t t = 0; t <= 367; t++) {
            int d = return_dx(t);
            if (d < prev)
                mono = false;
            prev = d;
        }
        bool shift = return_dx(0) == -53 && return_dx(67) == -53 && return_dx(67 + 150) == -7 && return_dx(367) == 0 &&
                     mono && return_ms() == 367;
        check(leave, "the leave: the press frame o 1 to 0 linear over 33");
        check(fade, "the return in: o 0 to 1 linear from 52 over 235");
        check(shift, "the return in: x -53 to 0 cubicOut from 67 over 300, end 367");

        Buffer a, dst;
        Canvas ac(a.surface());
        ac.fill(kScreen, 0xF0F0F0);
        launch_compose(a.surface(), dst.surface(), 16);
        int k = leave_a256(16);
        uint32_t px = LiveF::unpack(dst.surface().at(300, 300));
        bool mid = near((int)(px & 0xFF), 0xF0 * k / 256) && near((int)(px >> 16 & 0xFF), 0xF0 * k / 256);
        launch_compose(a.surface(), dst.surface(), 33);
        bool dark = filled(dst.surface(), kScreen, 0x000000);
        launch_compose(a.surface(), dst.surface(), 0);
        bool first = equal(dst.surface(), a.surface());
        check(mid && dark && first, "the leave composes the press frame over black, black from 33");

        Canvas b0(a.surface());
        b0.fill(kScreen, 0x204060);
        b0.fill({100, 0, 110, kPortraitH}, 0xFFFFFF);
        return_compose(a.surface(), dst.surface(), 40);
        bool held_dark = filled(dst.surface(), kScreen, 0x000000);
        int dx = return_dx(167), ra = return_a256(167);
        return_compose(a.surface(), dst.surface(), 167);
        bool edge = filled(dst.surface(), {kPortraitW + dx, 0, kPortraitW, kPortraitH}, 0x000000);
        uint32_t lit = LiveF::unpack(dst.surface().at(100 + dx, 10));
        uint32_t body = LiveF::unpack(dst.surface().at(110 + dx, 10));
        bool moved = dx < 0 && near((int)(lit & 0xFF), 0xFF * ra / 256) && near((int)(body & 0xFF), 0x60 * ra / 256);
        return_compose(a.surface(), dst.surface(), 367);
        bool end = equal(dst.surface(), a.surface());
        check(held_dark, "the return holds black over the dark");
        check(edge && moved && end, "the return shifts the home from the left over black and ends on it");
    }

    {
        HomeState h;
        SelectorModel m;
        h.recent = {{900, "Synth Recent 0", "GB"}, {901, "Synth Recent 1", "GB"}, {902, "Synth Recent 2", "GB"}};
        m.tiles = {solid(0x101010), nullptr, solid(0x101010)};
        bool wait = covers_pending(h, m, [](int64_t id) { return id == 901; });
        bool none = !covers_pending(h, m, [](int64_t id) { return id == 900; });
        h.system_view = true;
        for (int i = 0; i < 6; i++)
            h.sysgames.push_back({2000 + i, "Synth Launch Game " + std::to_string(i), "GBA"});
        h.first = 3;
        bool sys = covers_pending(h, m, [](int64_t id) { return id == 2004; }) &&
                   !covers_pending(h, m, [](int64_t id) { return id == 901 || id == 2001; });
        check(wait && none && sys,
              "the cover wait looks at the shown grid's missing covers, Recent or the system window");
    }

    {
        ToastStrip ts;
        ts.show(1000, kNs, "another game is closing", false);
        bool in = ts.visible(1000) && ts.eased(1000) < kOne / 16 && ts.animating(1100) && ts.eased(1350) == kOne &&
                  !ts.animating(2000);
        bool hold = ts.deadline(2000) == 4850 && ts.visible(5099) && !ts.visible(5100) && ts.deadline(4900) == 5100 &&
                    ts.deadline(5200) == -1 && ts.animating(4900);
        check(in && hold, "the toast rises over 350, holds 3500 and leaves over 250");
        ts.show(2000, "core not installed", "mgba", false);
        bool swap = ts.line1(2100) == kNs && ts.line1(2200) == "core not installed" && ts.line2(2200) == "mgba" &&
                    ts.text(2100) < kOne && ts.text(2400) == kOne && ts.visible(5749) && !ts.visible(5750);
        check(swap, "a second toast during the hold swaps the words with the value dip and restarts the hold");
        ts.show(3000, kNs, "audio not ready", false);
        bool third = ts.line1(3050) == "core not installed" && ts.line2(3050) == "mgba" &&
                     ts.line2(3300) == "audio not ready";
        check(third, "a third toast after a finished swap keeps the shown words until its own swap");
        Buffer f, before;
        Canvas fc(f.surface());
        fc.fill(kScreen, 0x123456);
        copy(f.surface(), before.surface());
        InkLayer ink;
        ts.draw(fonts, ink, f.surface(), 3000);
        int strip = std_px(tok::toastHeight);
        bool below = true;
        for (int y = strip; y < kPortraitH && below; y++)
            below = std::equal(f.surface().row(y), f.surface().row(y) + kPortraitW, before.surface().row(y));
        bool drawn = !equal(f.surface(), before.surface());
        ts.under(f.surface());
        bool clean = equal(f.surface(), before.surface());
        ts.hide();
        check(below && drawn && clean, "the toast draws only the top strip and under() restores the frame under it");
        check(!ts.visible(3000) && ts.deadline(3000) == -1, "a hidden toast is gone");
    }

    {
        ToastClock a;
        a.show(1000, "core not installed", "mgba", false);
        a.show(3000, "core not installed", "mgba", false);
        bool same = a.text(3000) == kOne && a.text(3100) == kOne && a.text(3300) == kOne && !a.animating(3100) &&
                    a.line1(3100) == "core not installed" && a.end() == 6750;
        check(same, "the same words again during the hold restart the hold with no value dip");
        ToastClock b, c;
        b.show(1000, kNs, "another game is closing", false);
        c.show(1000, kNs, "another game is closing", false);
        b.show(2000, "core not installed", "mgba", false);
        c.show(2000, "core not installed", "mgba", false);
        c.show(2050, "core not installed", "mgba", false);
        bool kept = c.text(2100) == b.text(2100) && c.text(2250) == b.text(2250) && c.line1(2100) == kNs &&
                    c.line1(2200) == "core not installed" && b.end() == 5750 && c.end() == 5800;
        check(kept, "the words a running swap goes to, shown again, keep the swap and restart the hold");
    }

    {
        int full = std_px(tok::toastHeight);
        InkLayer ink;
        Buffer f, before;
        auto drawn = [&](ToastStrip& ts, int64_t now, int row) {
            Canvas fc(f.surface());
            fc.fill(kScreen, 0x123456);
            copy(f.surface(), before.surface());
            ts.draw(fonts, ink, f.surface(), now);
            return !std::equal(f.surface().row(row), f.surface().row(row) + kPortraitW, before.surface().row(row));
        };
        ToastStrip ts;
        ts.show(1000, kNs, "another game is closing", true);
        bool band = drawn(ts, 2000, kBandTop - 1) && !drawn(ts, 2000, kBandTop) && !drawn(ts, 2000, full - 1);
        ts.show(2500, "core not installed", "mgba", false);
        bool swapped = drawn(ts, 2900, kBandTop - 1) && !drawn(ts, 2900, full - 1);
        ts.hide();
        ts.show(6000, kNs, "audio not ready", false);
        bool page = drawn(ts, 7000, full - 1);
        check(band && swapped && page, "on the home the strip is 76 and ends at the grid's top, latched at the show"
                                       " through a swap made on a page; a toast shown on a page is 80");
    }

    {
        Rig r(fonts, false);
        r.recent(5);
        int64_t t = r.enter(0);
        Buffer press;
        copy(r.shown.surface(), press.surface());
        r.press(PadAction::Accept, t);
        bool starts = r.launch.phase() == LaunchPhase::Leaving && r.flow.ctl.active() &&
                      r.flow.ctl.kind() == FoldKind::Launch && r.child;
        r.frame(t);
        bool frame0 = equal(r.shown.surface(), press.surface());
        r.frame(t + 16);
        Buffer want;
        launch_compose(press.surface(), want.surface(), 16);
        bool leg = equal(r.shown.surface(), want.surface()) && !r.saw("launch:child");
        int b0 = r.blacks;
        r.frame(t + 33);
        bool dark = filled(r.shown.surface(), kScreen, 0x000000);
        bool waiting = r.saw("launch:end") && r.launch.phase() == LaunchPhase::Waiting && r.blacks == b0 + 1;
        bool none = !r.frame(t + 50) && !r.frame(t + 66);
        int focus = r.flow.nav.sys_focus;
        r.press(PadAction::Right, t + 90);
        bool dropped = r.flow.nav.sys_focus == focus;
        r.cover(t + 400);
        bool covered = r.launch.phase() == LaunchPhase::Covered && r.frame(t + 410) && r.blacks == b0 + 2;
        r.active = true;
        r.launch.activation(true, true);
        r.press(PadAction::Right, t + 500);
        bool kept = r.launch.phase() == LaunchPhase::Covered && r.flow.nav.sys_focus == focus;
        r.pending = true;
        LaunchOutcome o = r.reap(exited(0), false, t + 2000);
        bool reaped = o.played && r.launch.waiting_covers() && !r.frame(t + 2010) &&
                      r.flow.home.recent_hold.size() >= 1 && r.flow.home.recent[0].id == r.played.id;
        r.pending = false;
        r.frame(t + 2020);
        bool r0 = filled(r.shown.surface(), kScreen, 0x000000);
        r.frame(t + 2070);
        bool r50 = filled(r.shown.surface(), kScreen, 0x000000);
        Buffer fresh = r.rest(t + 2187);
        r.frame(t + 2187);
        return_compose(fresh.surface(), want.surface(), 167);
        bool r167 = equal(r.shown.surface(), want.surface());
        int64_t tl = r.settle(t + 2203);
        Buffer end = r.rest(tl);
        bool landed = r.saw("return:end") && r.launch.phase() == LaunchPhase::Idle &&
                      equal(r.shown.surface(), end.surface());
        check(starts && frame0, "Accept in a system starts the launch, frame 0 is the last presented frame");
        check(leg && dark, "the launch is not landed by its child; the leave at 16 and black at 33");
        check(waiting && none, "the leave lands to waiting with one black frame, then no frames");
        check(dropped && covered && kept, "input is dropped while waiting and covered; the deactivation covers with"
                                          " one black frame; a re-activation keeps it covered");
        check(reaped, "a quit after covered marks it played and re-deals Recent before the return's destination");
        check(r0 && r50 && r167 && landed, "the return holds black to 52, its frame at 167 is the fresh home composed"
                                           " at 167, and it ends on a fresh render");
    }

    for (int64_t d : {(int64_t)100, (int64_t)600}) {
        Rig r(fonts, false);
        int64_t t = r.enter(0);
        r.press(PadAction::Right, t);
        int64_t f = t;
        for (; f + 16 < t + d; f += 16)
            r.frame(f);
        Buffer press;
        copy(r.shown.surface(), press.surface());
        bool moving = r.flow.t9().running(t + d);
        r.press(PadAction::Accept, t + d);
        r.frame(t + d);
        bool frame0 = moving && equal(r.shown.surface(), press.surface()) && !r.flow.t9().running(t + d);
        check(frame0, d == 100 ? "Accept 100 ms after a focus move: frame 0 is the last presented frame, T9 lands in"
                                 " the model"
                               : "Accept 600 ms after a focus move: frame 0 is the last presented frame, T9 lands in"
                                 " the model");
    }

    {
        Rig r(fonts, false);
        int64_t t = r.enter(0);
        bool scrolled = false;
        for (int i = 0; i < 12 && !scrolled; i++) {
            int s = r.flow.nav.sys_scroll;
            r.press(PadAction::Down, t);
            scrolled = r.flow.nav.sys_scroll != s;
            if (!scrolled)
                t = r.settle(t);
        }
        r.frame(t + 16);
        r.frame(t + 33);
        Buffer press;
        copy(r.shown.surface(), press.surface());
        bool gliding = r.flow.animating(t + 40);
        r.press(PadAction::Accept, t + 40);
        r.frame(t + 40);
        check(scrolled && gliding && equal(r.shown.surface(), press.surface()),
              "Accept mid grid glide: frame 0 is the last presented frame");
    }

    {
        Rig r(fonts, false);
        int64_t t = r.enter(0);
        r.press(PadAction::Right, t);
        for (int64_t f = t; f <= t + 96; f += 16)
            r.frame(f);
        Buffer press;
        copy(r.shown.surface(), press.surface());
        r.press(PadAction::Accept, t + 100);
        r.frame(t + 100);
        r.frame(t + 116);
        LaunchOutcome o = r.reap(exited(75), false, t + 120);
        bool rev = o.motion == LaunchMotion::Reverse && r.flow.ctl.reversed() && r.flow.ctl.run_length() == 20;
        Buffer fresh = r.rest(t + 133);
        Buffer want;
        r.frame(t + 133);
        launch_compose(fresh.surface(), want.surface(), 7);
        bool back = equal(r.shown.surface(), want.surface());
        r.frame(t + 150);
        bool last = equal(r.shown.surface(), fresh.surface());
        r.frame(t + 166);
        bool landed = r.saw("launch:end") && r.launch.phase() == LaunchPhase::Idle &&
                      equal(r.shown.surface(), fresh.surface()) && r.toasts.size() == 1 &&
                      r.toasts[0] == std::string(kNs) + "/another game is closing";
        check(rev && back, "a busy reap 20 ms into the leave reverses from e 20 over a fresh render of the home");
        check(last && landed, "the reversal's last frame is a fresh render of the home at rest, it lands on it and"
                              " gives the busy toast");
        check(!equal(press.surface(), fresh.surface()), "the press mid T9 differs from the rest the reversal ends on");
    }

    {
        Rig r(fonts, false);
        r.recent(5);
        int64_t t = r.settle(0);
        r.press(PadAction::Left, t);
        t = r.settle(t);
        r.press(PadAction::Right, t);
        t = r.settle(t);
        r.press(PadAction::Right, t);
        t = r.settle(t);
        Buffer press;
        copy(r.shown.surface(), press.surface());
        r.press(PadAction::Accept, t);
        r.frame(t);
        bool frame0 = r.played.id == 902 && r.flow.ctl.kind() == FoldKind::Launch &&
                      equal(r.shown.surface(), press.surface());
        r.frame(t + 16);
        r.frame(t + 33);
        r.frame(t + 50);
        r.cover(t + 300);
        r.active = true;
        r.launch.activation(true, true);
        r.active = false;
        LaunchOutcome o = r.reap(exited(0), false, t + 3000);
        bool redeal = o.played && r.flow.home.recent[0].id == 902 && r.flow.nav.tile_focus == 0 &&
                      r.flow.nav.pane == Pane::Details;
        int64_t tl = r.settle(t + 3000);
        Buffer end = r.rest(tl);
        bool landed = r.saw("return:end") && !r.saw("return:hidden") && equal(r.shown.surface(), end.surface());
        check(frame0, "Accept in the Recent browse starts the same leave from the last presented frame");
        check(redeal && landed, "the return with the toplevel not activated runs to its end on the Recent browse with"
                                " the played game first and focused");
    }

    {
        Rig r(fonts, false);
        r.missing.insert(901);
        r.recent(5);
        int64_t t = r.settle(0);
        r.press(PadAction::Left, t);
        t = r.settle(t);
        r.press(PadAction::Accept, t);
        t = r.settle(t);
        bool waiting = r.launch.phase() == LaunchPhase::Waiting;
        Buffer before;
        copy(r.shown.surface(), before.surface());
        r.drop(901, solid(kDrop));
        r.theme_to(true);
        bool held = !r.frame(t) && !r.frame(t + 16) && equal(r.shown.surface(), before.surface()) &&
                    !r.flow.model.tiles[1] && !theme().light;
        r.reap(exited(0), false, t + 500);
        bool still = !r.flow.model.tiles[1] && !theme().light;
        r.frame(t + 500);
        bool flushed = still && r.flow.model.tiles[1] && theme().light && r.flow.ctl.kind() == FoldKind::Return;
        Surface to = r.flow.ctl.to();
        bool dest = holds_color(to, kDrop) && filled(to, {0, 714, 4, 720}, theme_light().bg);
        check(waiting && held, "an art drop and a theme change during waiting change no pixel and stay held");
        check(flushed && dest, "the cover wait applies them and they show in the return's destination");
        set_theme_light(false);
    }

    {
        Rig r(fonts, true);
        int64_t t = r.enter(0);
        Buffer press;
        copy(r.shown.surface(), press.surface());
        r.press(PadAction::Accept, t);
        r.frame(t);
        bool frame0 = equal(r.shown.surface(), press.surface());
        r.frame(t + 16);
        r.frame(t + 33);
        r.frame(t + 50);
        r.cover(t + 200);
        r.reap(exited(0), false, t + 1000);
        int64_t tl = r.settle(t + 1000);
        Buffer end = r.rest(tl);
        check(frame0 && r.saw("return:end") && equal(r.shown.surface(), end.surface()),
              "large: the launch starts from the last presented frame and the return ends on a fresh render");
    }

    {
        Rig r(fonts, false);
        int64_t t = r.enter(0);
        r.press(PadAction::Accept, t);
        r.frame(t);
        r.cover(t + 10);
        bool covered = r.saw("launch:covered") && r.launch.phase() == LaunchPhase::Covered && r.launch.black_owed() &&
                       r.frame(t + 16) && filled(r.shown.surface(), kScreen, 0x000000) && !r.flow.ctl.active();
        check(covered, "a deactivation while leaving lands the launch with why covered and draws one black frame");
    }

    {
        Rig r(fonts, false);
        int64_t t = r.enter(0);
        r.press(PadAction::Accept, t);
        r.frame(t);
        r.frame(t + 16);
        r.frame(t + 33);
        r.frame(t + 50);
        r.pending = true;
        r.reap(exited(0), true, t + 800);
        r.frame(t + 800);
        r.press(PadAction::Right, t + 810);
        bool wait = !r.frame(t + 826) && r.launch.waiting_covers() && r.launch.deadline() == t + 1100 &&
                    r.flow.nav.sys_focus == 0;
        r.pending = false;
        r.frame(t + 900);
        bool starts = r.flow.ctl.active() && r.flow.ctl.kind() == FoldKind::Return && r.flow.nav.sys_focus == 0;
        Buffer to, want;
        copy(r.flow.ctl.to(), to.surface());
        r.frame(t + 956);
        return_compose(to.surface(), want.surface(), 56);
        bool mid = r.flow.ctl.kind() == FoldKind::Return && equal(r.shown.surface(), want.surface());
        for (int64_t f = t + 972; f < t + 1267; f += 16)
            r.frame(f);
        bool held = r.flow.nav.sys_focus == 0 && r.launch.phase() == LaunchPhase::Returning;
        r.frame(t + 1267);
        bool landed = r.saw("return:end") && !r.saw("return:input") && r.launch.phase() == LaunchPhase::Idle &&
                      r.flow.nav.sys_focus == 0 && equal(r.shown.surface(), to.surface());
        r.frame(t + 1267);
        Buffer dest = r.rest(t + 1267);
        bool acts = r.flow.nav.sys_focus == 1 && equal(r.shown.surface(), dest.surface()) &&
                    r.steps.back().first == PadAction::Right && r.steps.back().second == t + 1267;
        check(wait && starts && mid && held, "a press during the cover wait is kept and leaves the wait alone; the"
                                             " return starts when the covers answer, its frame at 56 as composed");
        check(landed && acts, "the landing frame at 367 is the return's destination; the kept press acts on the next"
                              " step, its first frame the home it moved at t 0");
    }

    {
        auto returning = [](Rig& r) {
            int64_t t = r.enter(0);
            r.press(PadAction::Accept, t);
            r.frame(t);
            r.frame(t + 16);
            r.frame(t + 33);
            r.reap(exited(0), true, t + 600);
            r.frame(t + 600);
            return t + 600;
        };
        auto run = [](Rig& r, int64_t from, int64_t to) {
            for (int64_t f = from; f < to; f += 16)
                r.frame(f);
        };
        auto after = [](const Rig& r, size_t n0, int64_t base) {
            std::vector<std::pair<PadAction, int64_t>> s;
            for (size_t i = n0; i < r.steps.size(); i++)
                s.push_back({r.steps[i].first, r.steps[i].second - base});
            return s;
        };
        using Steps = std::vector<std::pair<PadAction, int64_t>>;
        const PadAction kR = PadAction::Right, kD = PadAction::Down;

        Rig a(fonts, false);
        int64_t ta = returning(a);
        size_t na = a.steps.size();
        run(a, ta + 16, ta + 50);
        a.hold(kR, ta + 50);
        run(a, ta + 64, ta + 200);
        a.release(kR, ta + 200);
        run(a, ta + 208, ta + 367);
        a.frame(ta + 367);
        a.frame(ta + 367);
        run(a, ta + 383, ta + 1200);
        check(after(a, na, ta) == Steps{{kR, 367}}, "Right released at 200 inside the return acts once at its landing,"
                                                    " with no repeat");

        Rig b(fonts, false);
        int64_t tb = returning(b);
        size_t nb = b.steps.size();
        run(b, tb + 16, tb + 50);
        b.hold(kR, tb + 50);
        run(b, tb + 64, tb + 367);
        b.frame(tb + 367);
        b.frame(tb + 367);
        run(b, tb + 383, tb + 900);
        b.release(kR, tb + 900);
        run(b, tb + 900, tb + 1200);
        check(after(b, nb, tb) == Steps{{kR, 367}, {kR, 667}, {kR, 747}, {kR, 827}},
              "Right held to 900 acts at the landing 367 and repeats as a fresh press at 667, 747 and 827");

        Rig c(fonts, false);
        int64_t tc = returning(c);
        size_t nc = c.steps.size();
        run(c, tc + 16, tc + 50);
        c.hold(kR, tc + 50);
        run(c, tc + 64, tc + 100);
        c.release(kR, tc + 100);
        c.hold(kD, tc + 150);
        run(c, tc + 160, tc + 200);
        c.release(kD, tc + 200);
        run(c, tc + 208, tc + 367);
        c.frame(tc + 367);
        c.frame(tc + 367);
        run(c, tc + 383, tc + 1200);
        check(after(c, nc, tc) == Steps{{kD, 367}}, "Right then Down inside the return: only Down acts");

        Rig d(fonts, false);
        int64_t td = returning(d);
        size_t nd = d.steps.size();
        int focus = d.flow.nav.sys_focus;
        d.press(kR, td + 50);
        d.active = false;
        d.launch.edges(false, false, td + 100);
        run(d, td + 112, td + 367);
        d.frame(td + 367);
        d.frame(td + 367);
        run(d, td + 383, td + 600);
        check(d.saw("return:end") && after(d, nd, td).empty() && d.flow.nav.sys_focus == focus &&
                  d.launch.deadline() == -1,
              "a landing while hidden drops the kept press");

        Rig e(fonts, false);
        int64_t te = returning(e);
        size_t ne = e.steps.size();
        run(e, te + 16, te + 70);
        e.hold(kR, te + 70);
        run(e, te + 86, te + 367);
        e.frame(te + 367);
        e.frame(te + 372);
        run(e, te + 388, te + 700);
        e.release(kR, te + 700);
        check(after(e, ne, te) == Steps{{kR, 372}, {kR, 672}},
              "a repeat due between the landing and the kept press's act does nothing; the act starts the repeat");
    }

    {
        Rig r(fonts, false);
        int64_t t = r.enter(0);
        r.press(PadAction::Accept, t);
        bool due = r.launch.deadline() == t + 33;
        r.frame(t);
        r.frame(t + 16);
        bool landed = r.frame(t + 40) && r.saw("launch:end") && r.launch.phase() == LaunchPhase::Waiting &&
                      filled(r.shown.surface(), kScreen, 0x000000);
        bool none = !r.frame(t + 56) && filled(r.shown.surface(), kScreen, 0x000000);
        check(due, "while leaving the loop's deadline is the leave's end");
        check(landed && none, "a leave whose end frame never showed lands on the first frame past 33 as the wait's"
                              " black, and no home frame follows");
    }

    {
        Rig r(fonts, false);
        int64_t t = r.enter(0);
        r.press(PadAction::Accept, t);
        r.frame(t);
        r.frame(t + 16);
        r.frame(t + 33);
        r.pending = true;
        r.reap(exited(0), false, t + 600);
        bool waits = r.launch.deadline() == t + 900 && !r.frame(t + 700) && !r.frame(t + 899) &&
                     r.launch.waiting_covers();
        bool goes = r.frame(t + 900) && r.flow.ctl.kind() == FoldKind::Return && !r.launch.waiting_covers();
        check(waits && goes, "with covers pending the return waits to its deadline, the reap plus 300, then starts");
    }

    {
        Rig r(fonts, false);
        int64_t t = r.enter(0);
        r.press(PadAction::Accept, t);
        r.frame(t);
        r.frame(t + 16);
        r.frame(t + 33);
        r.reap(exited(0), false, t + 600);
        r.frame(t + 600);
        bool back = r.flow.ctl.active() && r.flow.ctl.kind() == FoldKind::Return && r.launch.holds();
        r.theme_to(true);
        r.frame(t + 616);
        bool held = !theme().light;
        int64_t tl = r.settle(t + 632);
        bool due = !theme().light && r.launch.deadline() == 0;
        r.frame(tl);
        bool applied = due && r.saw("return:end") && r.launch.phase() == LaunchPhase::Idle && theme().light;
        set_theme_light(false);
        check(back && held && applied, "a theme change during the return fold waits for its landing, wakes the loop"
                                       " at idle and applies on the next step");
    }

    {
        Rig r(fonts, false);
        int64_t t = r.enter(0);
        r.press(PadAction::Accept, t);
        r.frame(t);
        r.frame(t + 16);
        r.frame(t + 33);
        r.reap(exited(70), false, t + 600);
        r.frame(t + 600);
        bool due = r.flow.ctl.kind() == FoldKind::Return && r.launch.deadline() == t + 967;
        for (int64_t f = t + 616; f < t + 967; f += 16)
            r.frame(f);
        bool running = r.launch.phase() == LaunchPhase::Returning && !r.saw("return:end") && r.toasts.empty();
        r.frame(t + 967);
        Buffer end = r.rest(t + 967);
        bool landed = r.saw("return:end") && r.launch.phase() == LaunchPhase::Idle &&
                      equal(r.shown.surface(), end.surface()) && r.toasts.size() == 1 &&
                      r.toasts[0] == std::string(kNs) + "/the emulator stopped";
        check(due && running && landed, "the return lands at its run length, the start plus 367, on a fresh render and"
                                        " hands its toast over there");
    }

    {
        set_theme_light(true);
        Rig r(fonts, false);
        int64_t t = r.enter(0);
        Buffer press, want;
        copy(r.shown.surface(), press.surface());
        r.press(PadAction::Accept, t);
        bool due = r.launch.deadline() == t + 333;
        r.frame(t);
        r.frame(t + 167);
        launch_compose(press.surface(), want.surface(), 167, 333);
        bool mid = r.launch.phase() == LaunchPhase::Leaving && equal(r.shown.surface(), want.surface()) &&
                   leave_a256(167, 333) == alpha256(kOne - (fx)(167 * kOne / 333));
        r.frame(t + 333);
        bool dark = r.saw("launch:end") && r.launch.phase() == LaunchPhase::Waiting &&
                    filled(r.shown.surface(), kScreen, 0x000000);
        set_theme_light(false);
        Rig d(fonts, false);
        int64_t u = d.enter(0);
        d.press(PadAction::Accept, u);
        bool kept = d.launch.deadline() == u + 33;
        check(due && mid && dark && kept, "in light the leave runs o 1 to 0 over 333 and lands on the wait's black;"
                                          " the dark theme keeps 33");
    }

    {
        set_theme_light(true);
        Rig r(fonts, false);
        int64_t t = r.enter(0);
        Buffer press, want;
        copy(r.shown.surface(), press.surface());
        r.press(PadAction::Accept, t);
        r.frame(t);
        r.reap(exited(0), true, t + 20);
        r.frame(t + 167);
        launch_compose(press.surface(), want.surface(), 167, launch_leave_ms(true));
        bool runs = r.launch.phase() == LaunchPhase::Leaving && r.flow.ctl.kind() == FoldKind::Launch &&
                    equal(r.shown.surface(), want.surface()) && !r.saw("launch:reaped");
        r.frame(t + 333);
        bool dark = r.saw("launch:end") && r.launch.phase() == LaunchPhase::Returning &&
                    filled(r.shown.surface(), kScreen, 0x000000);
        r.frame(t + 350);
        bool back = r.flow.ctl.active() && r.flow.ctl.kind() == FoldKind::Return;
        set_theme_light(false);
        check(runs && dark && back, "a stamp quit 20 ms into the light leave lets it run to black at 333, then the"
                                    " return starts");
    }

    {
        set_theme_light(true);
        Buffer a, dst;
        Canvas ac(a.surface());
        ac.fill(kScreen, 0xFFFFFF);
        launch_compose(a.surface(), dst.surface(), 333, launch_leave_ms(true));
        bool leave = filled(dst.surface(), kScreen, 0x000000);
        return_compose(a.surface(), dst.surface(), 0);
        bool back = filled(dst.surface(), kScreen, 0x000000);
        Rig r(fonts, false);
        int64_t t = r.enter(0);
        r.press(PadAction::Accept, t);
        r.frame(t);
        r.frame(t + 16);
        r.frame(t + 333);
        bool wait = r.launch.phase() == LaunchPhase::Waiting && filled(r.shown.surface(), kScreen, 0x000000);
        set_theme_light(false);
        check(leave && back && wait, "light: the leave at 333, the wait's black frame and the return at 0 are"
                                     " 0x000000");
    }

    {
        Buffer src, got, want;
        Surface ss = src.surface();
        uint32_t v = 0x9E3779B9u;
        for (int y = 0; y < ss.h; y++)
            for (int x = 0; x < ss.w; x++) {
                v = v * 1664525u + 1013904223u;
                ss.at(x, y) = v;
            }
        bool same = true;
        int bad = 999;
        for (bool light : {false, true}) {
            set_theme_light(light);
            for (int k = -2; k <= 258 && same; k++) {
                Canvas gc(got.surface());
                gc.fill(kScreen, 0x123456);
                leave_compose(src.surface(), got.surface(), k);
                Canvas wc(want.surface());
                wc.fill(kScreen, launch_ground());
                blend_band(src.surface(), want.surface(), want.surface().rect(), 0, k);
                same = equal(got.surface(), want.surface());
                if (!same)
                    bad = k;
            }
        }
        set_theme_light(false);
        if (!same)
            std::printf("INFO the one pass leave differs at a %d\n", bad);
        check(same, "the leave's one pass blend over black equals the fill and blend it replaces, byte for byte, for"
                    " every a from -2 to 258 in both themes on synthetic pixels");
    }

    {
        LaunchFlow q;
        q.start(true, true);
        q.landed("launch");
        q.reaped(exited(75), false, "mgba", 100);
        q.start(true, true);
        q.landed("launch");
        q.reaped(exited(0), true, "mgba", 200);
        q.back();
        q.landed("return");
        std::string l1, l2;
        check(q.phase() == LaunchPhase::Idle && !q.take_toast(l1, l2),
              "a launch started during the return drops the earlier result's toast");
    }

    {
        Rig r(fonts, false);
        int64_t t = r.enter(0);
        r.press(PadAction::Accept, t);
        r.frame(t);
        r.frame(t + 16);
        r.frame(t + 33);
        r.reap(exited(75), false, t + 200);
        r.settle(t + 200);
        check(r.toasts.size() == 1 && r.toasts[0] == std::string(kNs) + "/another game is closing",
              "the driver hands the busy toast over once the return lands");
    }

    {
        auto base = [](int64_t now, bool ready) {
            GateIn in;
            in.now = now;
            in.ready = ready;
            in.claim = true;
            in.activated = true;
            in.need_redraw = true;
            in.timeout = -1;
            return in;
        };

        LaunchFlow w;
        w.start(true, true);
        w.landed("launch");
        w.activation(false, true);
        w.black_shown();
        LaunchOutcome o = w.reaped(exited(0), false, "mgba", 1300);
        GateIn a = launch_gate(w, base(1000, true), false);
        FrameGate ga;
        bool spin = ga.timeout(a) == 0;
        GateStep sa = ga.step(a);
        check(o.motion == LaunchMotion::Return && w.waiting_covers() && !spin && !sa.draw,
              "the gate in the cover wait: no draw and no zero timeout with a redraw due and a buffer free");

        LaunchFlow b;
        b.start(true, true);
        b.landed("launch");
        b.activation(false, true);
        GateIn ib = base(1000, true);
        ib.activated = false;
        ib.need_redraw = false;
        GateIn gb_in = launch_gate(b, ib, true);
        FrameGate gb;
        bool wake = gb.timeout(gb_in) == 0;
        GateStep sb = gb.step(gb_in);
        check(b.black_owed() && wake && sb.draw && b.draw(sb.draw) == LaunchDraw::Black,
              "the gate with a black frame owed while covered: it wakes, draws, and the draw is the black frame");

        LaunchFlow c;
        c.start(true, true);
        c.landed("launch");
        c.black_shown();
        GateIn ic = base(1000, true);
        ic.need_redraw = true;
        FrameGate gc;
        GateIn wait_in = launch_gate(c, ic, true);
        bool waiting = gc.timeout(wait_in) != 0 && !gc.step(wait_in).draw;
        c.activation(false, true);
        c.black_shown();
        ic.activated = false;
        GateIn cover_in = launch_gate(c, ic, true);
        bool covered = c.phase() == LaunchPhase::Covered && gc.timeout(cover_in) != 0 && !gc.step(cover_in).draw;
        check(waiting && covered, "the gate while waiting and covered with a child and no black owed: no draw, no zero"
                                  " timeout");

        LaunchFlow d;
        d.start(true, true);
        GateIn id = base(1000, true);
        id.activated = false;
        FrameGate gd;
        GateStep sd = gd.step(launch_gate(d, id, true));
        gd.posted(true, true, 1000);
        GateIn late = base(1100, false);
        late.activated = false;
        late.claim = false;
        GateStep s100 = gd.step(launch_gate(d, late, true));
        late.now = 1101;
        GateStep s101 = gd.step(launch_gate(d, late, true));
        check(sd.draw && !s100.land_reason && s101.land_reason && std::strcmp(s101.land_reason, "watchdog") == 0,
              "the gate while leaving with a child: it draws, and a motion post arms the watchdog that lands at +101");
    }

    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
