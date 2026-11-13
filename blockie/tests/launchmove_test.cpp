#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <signal.h>
#include <sys/wait.h>

#include "../../gfx/compose.h"
#include "../../ui/roles.h"
#include "../frame_gate.h"
#include "../homeflow.h"
#include "../launchflow.h"
#include "../leftmove.h"
#include "../pagerows.h"
#include "../screen_settings.h"
#include "../theme.h"
#include "../toast_strip.h"
#include "glow_cmp.h"

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

PageHead synth_head(Screen s, int tabs)
{
    PageHead h;
    bool set = s == Screen::Settings;
    h.word = set ? "settings" : "bluetooth";
    h.layer = set;
    for (int k = 0; k < 4; k++)
        h.letters.push_back({h.word + "-header-l" + std::to_string(k), Rect{k * 240, 0, (k + 1) * 240, 140},
                             eng::Ink::Text});
    for (int j = 0; set && j < tabs; j++)
        h.tabs.push_back({"settings-pivots-t" + std::to_string(j),
                          Rect{j * kPortraitW / tabs, 116, (j + 1) * kPortraitW / tabs, 172}, eng::Ink::Text});
    return h;
}

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

int64_t wall_us()
{
    return (int64_t)std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

int r50(Fonts& fonts, int reps)
{
    for (int kind = 0; kind < 2; kind++) {
        const char* name = kind ? "system" : "recent";
        int64_t skip[4] = {}, owned[4] = {}, band[4] = {};
        int64_t gr[2] = {}, gg[2] = {};
        int samples = 0;
        bool equal = true;
        for (int rep = 0; rep < reps; rep++) {
            FlowOpts fo;
            fo.lang = true;
            fo.fast = true;
            FlowOpts dry = fo;
            dry.dry = true;
            HomeFlow w(fonts, fo), d(fonts, dry);
            Buffer sw, sd;
            for (HomeFlow* f : {&w, &d}) {
                f->model.systems = {{"GBA", 40, ""}, {"GB", 30, ""}};
                f->kinds = {MenuKind::System, MenuKind::System};
                f->games = [](int sys) {
                    std::vector<HomeGame> list;
                    for (int i = 0; i < (sys == 0 ? 40 : 30); i++)
                        list.push_back(
                            {1000 * (sys + 1) + i, "Synth Bench Game " + std::to_string(i), sys == 0 ? "GBA" : "GB"});
                    return list;
                };
                f->home.load = [](const HomeGame& g) -> Art {
                    return solid(0x203040u + (uint32_t)(g.id % 97) * 0x020103u);
                };
                Buffer& scr = f == &w ? sw : sd;
                f->last_frame = [&scr](const Surface& s) { copy(scr.surface(), s); };
                f->setup();
                std::vector<HomeGame> recent;
                for (int k = 0; k < 12; k++)
                    recent.push_back({900 + k, "Synth Bench Recent " + std::to_string(k), "GB"});
                f->home.deal_recent(f->model, recent);
                f->set_clock(wall_us);
            }
            int64_t t = 0;
            for (int n = 0; n < 30; n++, t += 16) {
                w.frame(t, sw.surface());
                d.frame(t, sd.surface());
            }
            PadAction key = kind ? PadAction::Accept : PadAction::Left;
            w.input(key, false, t);
            d.input(key, false, t);
            t += 16;
            std::vector<uint8_t> ids0;
            std::vector<uint16_t> e0;
            std::vector<BlockPose> p0;
            int w0 = 0;
            for (int f = 0; f < 4; f++, t += 16) {
                Glow::Times a = w.glow().times, b = d.glow().times;
                w.frame(t, sw.surface());
                d.frame(t, sd.surface());
                const Glow& gw = w.glow();
                const Glow& gd = d.glow();
                equal = equal && gw.e == gd.e && gw.ids == gd.ids && gw.age == gd.age;
                const SlidePlan* plan = w.plan();
                if (!plan || !w.ctl.active())
                    break;
                int bx = std::clamp(w.boundary(t), kPaneSplit, kPortraitW);
                int wf = std::min(std::max(bx, kPaneLeftEdge), kPortraitW);
                const LeftPlan& lp = plan->moves;
                Rect gr_rect = block_glow_rect(lp);
                int gwid = gr_rect.x1 - gr_rect.x0, gh = gr_rect.y1 - gr_rect.y0;
                size_t n = (size_t)std::max(0, gwid) * (size_t)std::max(0, gh);
                if (f >= 1 && gw.ids.size() == n && ids0.size() == n && gw.e.size() == n && e0.size() == n) {
                    gr[0] += gw.times.frame_raster - a.frame_raster;
                    gr[1] += gd.times.frame_raster - b.frame_raster;
                    gg[0] += gw.times.frame_glow - a.frame_glow;
                    gg[1] += gd.times.frame_glow - b.frame_glow;
                    samples++;
                    size_t nk = lp.blocks.size();
                    std::vector<int> c0(nk, 0), c1(nk, 0), k0(nk, -1), k1(nk, -1);
                    std::vector<Rect> r0(nk), r1(nk);
                    std::vector<uint8_t> kept(nk, 0);
                    for (const BlockPose& q : p0) {
                        size_t k = (size_t)block_key(lp, q.block);
                        c0[k]++;
                        k0[k] = q.block;
                        r0[k] = q.rect;
                    }
                    for (const BlockPose& q : gw.poses) {
                        size_t k = (size_t)block_key(lp, q.block);
                        c1[k]++;
                        k1[k] = q.block;
                        r1[k] = q.rect;
                    }
                    for (size_t k = 0; k < nk; k++)
                        kept[k] = c0[k] == 1 && c1[k] == 1 && k0[k] == k1[k] && r0[k].x0 == r1[k].x0 &&
                                  r0[k].y0 == r1[k].y0;
                    int lim = std::min(w0, wf) - gr_rect.x0;
                    for (int r = 0; r < gh; r++) {
                        const uint8_t* in = gw.ids.data() + (size_t)r * gwid;
                        const uint8_t* ip = ids0.data() + (size_t)r * gwid;
                        const uint16_t* en = gw.e.data() + (size_t)r * gwid;
                        const uint16_t* ep = e0.data() + (size_t)r * gwid;
                        for (int xi = 0; xi < gwid; xi++) {
                            int id = in[xi];
                            if (!id)
                                continue;
                            owned[f]++;
                            size_t k = (size_t)((id & 0x7F) - 1);
                            if (xi < lim && ip[xi] == id && ep[xi] == en[xi] && k < nk && kept[k])
                                skip[f]++;
                        }
                    }
                    band[f] += (int64_t)n;
                }
                ids0 = gw.ids;
                e0 = gw.e;
                p0 = gw.poses;
                w0 = wf;
            }
        }
        for (int f = 1; f < 4; f++)
            std::printf("R50 frac %s f%d skip %lld owned %lld band %lld frac %.4f\n", name, f, (long long)skip[f],
                        (long long)owned[f], (long long)band[f], band[f] ? (double)skip[f] / (double)band[f] : 0.0);
        double n = samples ? (double)samples * 1000.0 : 1.0;
        double sr = gr[0] > 0 ? (double)(gr[0] - gr[1]) / (double)gr[0] : 0.0;
        double sg = gg[0] > 0 ? (double)(gg[0] - gg[1]) / (double)gg[0] : 0.0;
        std::printf("R50 share %s raster draw %.3f dry %.3f share %.3f glow shade %.3f dry %.3f share %.3f frames %d\n",
                    name, gr[0] / n, gr[1] / n, sr, gg[0] / n, gg[1] / n, sg, samples);
        std::printf("R50 state %s equal %d reps %d\n", name, equal ? 1 : 0, reps);
        std::printf("R50 rule %s bound = (gr x %.3f + gg x %.3f) x %.4f on frame 1\n", name, sr, sg,
                    band[1] ? (double)skip[1] / (double)band[1] : 0.0);
    }
    return 0;
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
    if (const char* r = std::getenv("BLOCKIE_R50"))
        return r50(fonts, std::max(1, std::atoi(r)));

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
        check(words(launch_helper_busy(HelperOp::SteamGet), kNs, "a port is getting files"),
              "sf1: the helper busy refusal during a get says a port is getting files");
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

    {
        struct Twin {
            HomeFlow flow;
            ToastStrip ts;
            InkLayer ink;
            Buffer scr;

            Twin(Fonts& fonts, const FlowOpts& o) : flow(fonts, o)
            {
                flow.model.systems = {{"GB", 3, ""}, {"GBA", 3, ""}, {"PS1", 3, ""}};
                flow.kinds = {MenuKind::System, MenuKind::System, MenuKind::System};
                flow.games = [](int sys) {
                    std::vector<HomeGame> list;
                    const char* const names[] = {"GB", "GBA", "PS1"};
                    for (int i = 0; i < 3; i++)
                        list.push_back({1000 * (sys + 1) + i, "Synth Twin Game " + std::to_string(i), names[sys]});
                    return list;
                };
                flow.last_frame = [this](const Surface& s) { copy(scr.surface(), s); };
                flow.setup();
                flow.home.deal_recent(flow.model, {{901, "Synth Twin A", "GB"},
                                                   {902, "Synth Twin B", "GBA"},
                                                   {903, "Synth Twin C", "PS1"},
                                                   {904, "Synth Twin D", "GB"}});
            }
            bool frame(Fonts& fonts, int64_t now)
            {
                ts.under(scr.surface());
                bool more = flow.frame(now, scr.surface());
                ts.draw(fonts, ink, scr.surface(), now);
                return more || ts.animating(now);
            }
        };
        bool light = theme().light;
        {
            bool same = true, trusted = true, after = true, live = true, shown = false, hidden = false, nobuild = true;
            std::string name = "step";
            FlowOpts fo;
            fo.lang = true;
            FlowOpts po = fo;
            fo.fast = true;
            Twin a(fonts, fo), b(fonts, po);
            int64_t t = 0;
            auto run = [&](int frames, const std::function<void(int)>& at) {
                for (int n = 0; n < frames; n++, t += 16) {
                    at(n);
                    a.frame(fonts, t);
                    b.frame(fonts, t);
                    same = same && a.scr.pix == b.scr.pix;
                    nobuild = nobuild && a.flow.glow().runs_built == 0;
                }
            };
            auto both = [&](const std::function<void(Twin&)>& f) {
                f(a);
                f(b);
            };
            run(60, [](int) {});
            both([&](Twin& w) { w.ts.show(t, "Synth toast", "held over the folds", true); });
            bool was = true;
            for (int k = 0; k < 40 && was; k++) {
                both([&](Twin& w) { w.flow.input(k % 2 ? PadAction::Back : PadAction::Left, false, t); });
                run(30, [&](int) {
                    bool now_on = a.ts.visible(t);
                    shown = shown || (now_on && a.flow.ctl.active());
                    hidden = hidden || (was && !now_on && a.flow.ctl.active());
                    was = now_on;
                });
            }
            run(60, [](int) {});
            if (a.flow.nav.pane == Pane::Details) {
                both([&](Twin& w) { w.flow.input(PadAction::Back, false, t); });
                run(60, [](int) {});
            }
            both([&](Twin& w) { w.flow.input(PadAction::Left, false, t); });
            int pre = -1;
            bool drawn = false;
            run(50, [&](int n) {
                int now_tr = a.flow.pane() ? a.flow.pane()->trusted_frames() : -1;
                bool grew = n > 1 && now_tr > pre && a.flow.ctl.active();
                pre = now_tr;
                if (!drawn && grew) {
                    drawn = true;
                    both([&](Twin& w) { w.flow.redraw(); });
                    int before = a.flow.pane() ? a.flow.pane()->trusted_frames() : -1;
                    a.frame(fonts, t);
                    b.frame(fonts, t);
                    same = same && a.scr.pix == b.scr.pix;
                    after = after && a.flow.pane() && a.flow.pane()->trusted_frames() == before;
                    t += 16;
                    pre = a.flow.pane()->trusted_frames();
                }
            });
            live = live && drawn;
            both([&](Twin& w) { w.flow.input(PadAction::Back, false, t); });
            run(5, [](int) {});
            both([&](Twin& w) { w.flow.input(PadAction::Left, false, t); });
            run(50, [](int) {});
            both([&](Twin& w) { w.flow.input(PadAction::Back, false, t); });
            run(50, [](int) {});
            const std::vector<HomeGame> fresh = {{905, "Synth Twin E", "GBA"},
                                                 {901, "Synth Twin A", "GB"},
                                                 {902, "Synth Twin B", "GBA"},
                                                 {903, "Synth Twin C", "PS1"},
                                                 {904, "Synth Twin D", "GB"}};
            Buffer prior;
            int ran = -1;
            bool tail = false, titled = false;
            both([&](Twin& w) { w.flow.input(PadAction::Left, false, t); });
            run(60, [&](int n) {
                if (ran >= 0 && n == ran + 1) {
                    tail = a.flow.ctl.active();
                    Surface p = prior.surface(), s = b.scr.surface();
                    for (int y = 0; y < s.h && !titled; y++)
                        for (int x = kPaneSplitWide; x < s.w && !titled; x++)
                            titled = p.at(x, y) != s.at(x, y);
                }
                bool rest = a.flow.ctl.active() && a.flow.ctl.kind() == FoldKind::Expand &&
                            t - 16 - a.flow.ctl.t0() >= a.flow.ctl.run_length() - kBlockTailMs;
                if (ran < 0 && n > 0 && rest) {
                    ran = n;
                    copy(b.scr.surface(), prior.surface());
                    both([&](Twin& w) { w.flow.game_ran(fresh); });
                }
            });
            both([&](Twin& w) { w.flow.input(PadAction::Back, false, t); });
            run(60, [](int) {});
            both([&](Twin& w) { w.flow.input(PadAction::Left, false, t); });
            run(4, [](int) {});
            set_theme_light(!light);
            both([&](Twin& w) { w.flow.theme_changed(); });
            run(30, [](int) {});
            set_theme_light(light);
            both([&](Twin& w) { w.flow.theme_changed(); });
            run(30, [](int) {});
            trusted = trusted && a.flow.pane() && a.flow.pane()->trusted_frames() > 0 &&
                      a.flow.pane()->segment_frames() > 0 && a.flow.glow().seg_frames > 0 && b.flow.pane() &&
                      b.flow.pane()->trusted_frames() == 0 && b.flow.glow().seg_frames == 0;
            check(same, (name + " R8 pane: a fast HomeFlow (pane segments on a trusted target, the grid on runs) "
                                "composes every frame equal to the pixel path into a persistent target with the toast "
                                "under and drawn around each frame: Recent expands and collapses, a redraw mid fold, a "
                                "collapse reversed by the inverse input, a Recent change on an expand's tail, an "
                                "expand cut by a theme switch and back")
                            .c_str());
            check(trusted && after && live,
                  (name + " R8 pane: the fast pane trusted and on segments and the grid on runs, the pixel flow "
                          "neither; the frame before a mid fold redraw trusted, the frame after it not")
                      .c_str());
            check(shown && hidden,
                  (name + " R8 pane: the toast shown, held and ending by its own clock during a fold").c_str());
            check(nobuild, (name + " R8 pane: the fast flow's grid never builds its row runs from the planes on any "
                                   "frame (expands, collapses, a reversal, landings by input, the theme's new gen, a "
                                   "redraw, the Recent change)")
                               .c_str());
            check(ran >= 0 && tail && titled,
                  (name + " R8 pane: a Recent change with a new focused title on an expand's tail frame, its "
                          "predecessor at rest, moves the pixel path's pane right of the split while the fold runs")
                      .c_str());
        }
    }
    {
        struct L5Pane {
            HomeFlow flow;
            ToastStrip ts;
            InkLayer ink;
            Buffer scr;
            char why = 0;

            L5Pane(Fonts& fonts, const FlowOpts& o, int recent, const std::string& title) : flow(fonts, o)
            {
                flow.model.systems = {{"GB", 9, ""}, {"GBA", 9, ""}, {"PS1", 9, ""}};
                flow.kinds = {MenuKind::System, MenuKind::System, MenuKind::System};
                flow.games = [title](int sys) {
                    std::vector<HomeGame> list;
                    const char* const names[] = {"GB", "GBA", "PS1"};
                    for (int i = 0; i < 9; i++)
                        list.push_back({1000 * (sys + 1) + i, title + " " + std::to_string(i), names[sys]});
                    return list;
                };
                flow.last_frame = [this](const Surface& s) {
                    copy(scr.surface(), s);
                    ts.under(s);
                };
                flow.ctl.target_current = [this](int64_t) { return why; };
                flow.setup();
                std::vector<HomeGame> r;
                for (int i = 0; i < recent; i++)
                    r.push_back({900 + i, title + " R" + std::to_string(i), i % 2 ? "GBA" : "GB"});
                flow.home.deal_recent(flow.model, r);
            }
            void frame(Fonts& fonts, int64_t now)
            {
                ts.under(scr.surface());
                flow.frame(now, scr.surface());
                ts.draw(fonts, ink, scr.surface(), now);
            }
            eng::Instance* pane() { return const_cast<eng::Instance*>(flow.pane()); }
        };
        bool light0 = theme().light;
        int folds = 0, adopts = 0, vadopts = 0, vfails = 0, refused = 0, wrong = 0, mutants = 0, differ = 0, stale = 0;
        int coincide = 0, runs = 0, gk[4] = {}, ak[4] = {}, ik[4] = {}, dcases = 0, dok = 0, rev_cases = 0, rev_ok = 0,
            after_cases = 0, after_ok = 0, rest_cases = 0, rest_rows = 0;
        int la[3] = {}, lm[3] = {}, lv[3] = {}, madopts = 0, dx = -1, dy = -1, vx = -1, vy = -1;
        std::string got, dk = "-", vk = "-";
        bool same = true, gate = true;
        for (bool light : {false, true})
            for (int c = 0; c < 3; c++) {
                set_theme_light(light);
                const int recent[] = {0, 1, 8};
                std::string title = c == 1 ? "Synth Twin Title Long Enough To Wrap Over Four Lines Of The Pane"
                                           : "Synth Twin";
                FlowOpts fo;
                fo.lang = true;
                FlowOpts po = fo;
                fo.fast = true;
                FlowOpts vo = fo;
                vo.verify = true;
                L5Pane a(fonts, fo, recent[c], title), b(fonts, po, recent[c], title),
                    v(fonts, vo, recent[c], title), mu(fonts, fo, recent[c], title);
                int64_t t = 0;
                bool forced = false, mdiff = false;
                auto all = [&](const std::function<void(L5Pane&)>& f) {
                    f(a);
                    f(b);
                    f(v);
                    f(mu);
                };
                auto run = [&](int frames) {
                    for (int n = 0; n < frames; n++, t += 16) {
                        all([&](L5Pane& w) { w.frame(fonts, t); });
                        same = same && a.scr.pix == b.scr.pix && v.scr.pix == b.scr.pix;
                        if (dx < 0 && a.scr.pix != b.scr.pix) {
                            Surface sa = a.scr.surface(), sb = b.scr.surface();
                            for (int y = 0; y < sa.h && dx < 0; y++)
                                for (int x = 0; x < sa.w && dx < 0; x++)
                                    if (sa.at(x, y) != sb.at(x, y)) {
                                        dx = x;
                                        dy = y;
                                        dk = a.flow.ctl.active() ? fold_kind_name(a.flow.ctl.kind()) : "rest";
                                    }
                        }
                        mdiff = mdiff || mu.scr.pix != b.scr.pix;
                    }
                };
                auto fold = [&](PadAction act, char want) {
                    all([&](L5Pane& w) { w.flow.input(act, false, t); });
                    t += 16;
                    int vf0 = v.pane() ? v.pane()->adopt_fails() : 0;
                    const char* kind = fold_kind_name(a.flow.ctl.kind());
                    bool took = a.flow.ctl.from_target();
                    gate = gate && took == v.flow.ctl.from_target() && took == (a.flow.ctl.l5() == 'a');
                    folds += took;
                    bool open = a.flow.ctl.kind() == FoldKind::Expand;
                    int fk = a.flow.ctl.details() ? (open ? 2 : 3) : open ? 0 : 1;
                    char letter = a.flow.ctl.l5();
                    int ad0 = a.pane() ? a.pane()->adopts() : 0, st0 = a.pane() ? a.pane()->adopt_struct() : 0;
                    if (want) {
                        refused++;
                        if (a.flow.ctl.l5() != want || took) {
                            wrong++;
                            got += std::string(1, want) + ">" + std::string(1, a.flow.ctl.l5()) + " ";
                        }
                        if (forced && mu.pane()) {
                            mu.pane()->adopt_start(true);
                            mutants++;
                        }
                    }
                    mdiff = false;
                    int adb = a.pane() ? a.pane()->adopts() : 0;
                    run(1);
                    bool adopted0 = (a.pane() ? a.pane()->adopts() : 0) > adb;
                    bool equal0 = a.scr.pix == b.scr.pix;
                    if (took && fk == 0 && adopted0) {
                        const eng::Instance* pi = a.flow.pane();
                        Rect pr = pi->rect();
                        int gw = pr.x1 - pr.x0, rows = 0;
                        const std::vector<uint16_t>& own = pi->ids();
                        for (int y = 0; y < pr.y1 - pr.y0; y++) {
                            bool rest = false;
                            for (int x = pr.x0; x < pr.x1 && !rest; x++)
                                rest = (own[(size_t)y * gw + (size_t)(x - pr.x0)] & eng::kIdMask) == 0;
                            rows += rest;
                        }
                        rest_cases++;
                        rest_rows += rows > 0 && equal0;
                    }
                    run(59);
                    differ += forced && want && mdiff;
                    if (vk == "-" && v.pane() && v.pane()->adopt_fails() > vf0)
                        vk = kind;
                    forced = false;
                    if (took) {
                        gk[fk]++;
                        ak[fk] += (a.pane() ? a.pane()->adopts() : 0) > ad0;
                        ik[fk] += (a.pane() ? a.pane()->adopt_struct() : 0) - st0;
                    }
                    if (fk == 2) {
                        dcases++;
                        dok += letter == 'd' && !took && (a.pane() ? a.pane()->adopts() : 0) == ad0 &&
                                   (a.pane() ? a.pane()->adopt_struct() : 0) == st0;
                    }
                };
                run(60);
                all([&](L5Pane& w) { w.ts.show(t, "Synth toast", "held over the folds", true); });
                run(40);
                for (int k : {0, 4, 8}) {
                    fold(PadAction::Accept, 0);
                    for (int i = 0; i < k; i++) {
                        all([&](L5Pane& w) { w.flow.input(PadAction::Right, false, t); });
                        run(30);
                    }
                    fold(PadAction::Back, 0);
                    all([&](L5Pane& w) { w.flow.input(PadAction::Down, false, t); });
                    run(30);
                }
                if (recent[c] > 0) {
                    fold(PadAction::Left, 0);
                    fold(PadAction::Back, 0);
                }
                all([&](L5Pane& w) { w.ts.show(t, "Synth toast", "in its ramp", false); });
                run(2);
                fold(PadAction::Accept, 0);
                fold(PadAction::Back, 0);
                all([&](L5Pane& w) { w.flow.input(PadAction::Up, false, t); });
                run(2);
                forced = true;
                fold(PadAction::Accept, 'm');
                fold(PadAction::Back, 0);
                all([&](L5Pane& w) {
                    w.flow.model.systems[2] = {"NES", 9, ""};
                    w.flow.redraw();
                });
                forced = true;
                fold(PadAction::Accept, 'r');
                fold(PadAction::Back, 0);
                for (char q : {'n', 'u', 'q'}) {
                    all([&](L5Pane& w) { w.why = q; });
                    fold(PadAction::Accept, q);
                    all([&](L5Pane& w) { w.why = 0; });
                    fold(PadAction::Back, 0);
                }
                if (c == 0 && !light) {
                    all([&](L5Pane& w) { w.flow.input(PadAction::Accept, false, t); });
                    t += 16;
                    run(3);
                    all([&](L5Pane& w) { w.flow.input(PadAction::Back, false, t); });
                    rev_cases++;
                    rev_ok += a.flow.ctl.reversed() && a.flow.ctl.l5() == 'v' && !a.flow.ctl.from_target();
                    run(60);
                }
                if (recent[c] > 0 && !light) {
                    all([&](L5Pane& w) { w.flow.input(PadAction::Accept, false, t); });
                    t += 16;
                    run(60);
                    all([&](L5Pane& w) { w.flow.input(PadAction::Back, false, t); });
                    run(3);
                    all([&](L5Pane& w) { w.flow.input(PadAction::Left, false, t); });
                    if (a.flow.ctl.details() && a.flow.ctl.kind() == FoldKind::Expand) {
                        after_cases++;
                        after_ok += a.flow.ctl.l5() == 'k' && !a.flow.ctl.from_target();
                    }
                    run(60);
                    all([&](L5Pane& w) { w.flow.input(PadAction::Back, false, t); });
                    run(60);
                }
                int ad0 = a.pane() ? a.pane()->adopts() : 0, ns0 = a.pane() ? a.pane()->adopt_nostart() : 0;
                all([&](L5Pane& w) { w.flow.input(PadAction::Accept, false, t); });
                run(3);
                coincide += a.pane() && a.pane()->adopt_nostart() == ns0 + 1 && a.pane()->adopts() == ad0;
                runs++;
                all([&](L5Pane& w) { w.flow.input(PadAction::Down, false, t); });
                stale += a.flow.ctl.stale();
                fold(PadAction::Back, 's');
                run(60);
                if (a.pane()) {
                    adopts += a.pane()->adopts();
                    vadopts += v.pane()->adopts();
                    vfails += v.pane()->adopt_fails();
                    la[0] += a.pane()->adopt_cleared();
                    lm[0] += mu.pane()->adopt_cleared();
                    la[1] += a.pane()->adopt_nostart();
                    lm[1] += mu.pane()->adopt_nostart();
                    la[2] += a.pane()->adopt_drawn();
                    lm[2] += mu.pane()->adopt_drawn();
                    madopts += mu.pane()->adopts();
                    lv[0] += v.pane()->adopt_cleared();
                    lv[1] += v.pane()->adopt_nostart();
                    lv[2] += v.pane()->adopt_drawn();
                    if (vx < 0 && v.pane()->adopt_fail_x() >= 0) {
                        vx = v.pane()->adopt_fail_x();
                        vy = v.pane()->adopt_fail_y();
                    }
                }
            }
        set_theme_light(light0);
        std::string per_kind;
        bool kinds_ok = true;
        const char* const kind_names[] = {"expand", "collapse", "details expand", "details collapse"};
        for (int k = 0; k < 4; k++) {
            per_kind += std::string(" ") + kind_names[k] + " gated " + std::to_string(gk[k]) + " adopted " +
                        std::to_string(ak[k]) + " i " + std::to_string(ik[k]) + ";";
            kinds_ok = kinds_ok && (k == 2 || (gk[k] > 0 && ak[k] > 0));
        }
        per_kind += " after start details letter k " + std::to_string(after_ok) + " of " + std::to_string(after_cases) +
                    ";";
        kinds_ok = kinds_ok && after_ok == after_cases;
        per_kind += " reversal letter v " + std::to_string(rev_ok) + " of " + std::to_string(rev_cases) + ";";
        kinds_ok = kinds_ok && rev_cases > 0 && rev_ok == rev_cases;
        per_kind += " details expand letter d, no adoption " + std::to_string(dok) + " of " + std::to_string(dcases) +
                    ";";
        kinds_ok = kinds_ok && dcases > 0 && dok == dcases;
        per_kind += " panel rows drawn as ground on adopted expand frame 0 " + std::to_string(rest_rows) + " of " +
                    std::to_string(rest_cases) + ";";
        kinds_ok = kinds_ok && rest_cases > 0 && rest_rows == rest_cases;
        char said[4096];
        std::snprintf(said, sizeof said,
                      "L5 pane: light and dark, Recent 0, 1 and 8, short and wrapping titles, no art, "
                      "focus on the first, middle and last tiles, a toast held and in its ramp: every fold from rest "
                      "the gate "
                      "takes is adopted with the target byte equal to the start render under verify (gated folds %d, "
                      "adopts %d, verify adopts %d fails %d), the fast and verify flows equal the pixel path every "
                      "frame (%d), the gate letters agree (%d); refusals m, r, n, u, q and s give their letter and no "
                      "adoption (%d cases, %d wrong, stale reached %d), and forced adoptions in the m and r states "
                      "differ from the pixel path (%d of %d), and a fold whose frame 0 lands at the input's own t "
                      "neither starts nor adopts (%d of %d), expand, collapse and the details collapse each gate and "
                      "adopt, the details expand shows d and never adopts (%s)"
                      " (a: adopts %d, cleared %d, no start %d, start drawn %d; v: adopts %d, fails %d, "
                      "cleared %d, no start %d, start drawn %d, "
                      "first fail x %d y %d in %s; mu: adopts %d, cleared %d, no start %d, start drawn %d; "
                      "first pixel difference x %d y %d in %s; wrong letters %s)",
                      folds, adopts, vadopts, vfails, (int)same, (int)gate, refused, wrong, stale, differ, mutants,
                      coincide, runs, per_kind.c_str(),
                      adopts, la[0], la[1], la[2], vadopts, vfails, lv[0], lv[1], lv[2], vx, vy, vk.c_str(), madopts,
                      lm[0],
                      lm[1], lm[2], dx, dy, dk.c_str(), got.c_str());
        check(same && gate && folds > 0 && adopts > 0 && vadopts > 0 && vfails == 0 && wrong == 0 && stale > 0 &&
                  runs > 0 && coincide == runs && kinds_ok,
              said);
        bool feq = true;
        int trail_px = 0, trail_bad = 0, tile_px = 0, tile_bad = 0, flip_frames = 0, flip_composed = 0;
        for (bool light : {false, true}) {
            set_theme_light(light);
            FlowOpts fo;
            fo.lang = true;
            FlowOpts po = fo;
            fo.fast = true;
            FlowOpts vo = fo;
            vo.verify = true;
            L5Pane a(fonts, fo, 8, "Synth Panel"), b(fonts, po, 8, "Synth Panel"), v(fonts, vo, 8, "Synth Panel");
            for (L5Pane* w : {&a, &b, &v})
                w->flow.covers_wanted = [w](const std::vector<HomeGame>& want, int) {
                    for (const HomeGame& g : want)
                        w->flow.art(g.id, kTileSide, w->flow.home.epoch, solid(0x6080A0));
                };
            int64_t t = 0;
            bool trail = false, tiles = false;
            std::vector<uint16_t> pe, pid, ph, ge;
            std::vector<uint8_t> pc;
            auto all = [&](const std::function<void(L5Pane&)>& f) {
                f(a);
                f(b);
                f(v);
            };
            auto step = [&]() {
                const eng::Instance* pb = b.flow.pane();
                if (pb) {
                    pe = pb->e();
                    pid = pb->ids();
                    ph = pb->held();
                    pc = pb->cover();
                }
                ge = b.flow.glow().e;
                int sf0 = a.flow.pane() ? a.flow.pane()->segment_frames() : 0;
                bool live = a.flow.ctl.active();
                all([&](L5Pane& w) { w.frame(fonts, t); });
                t += 16;
                if (trail && live && a.flow.ctl.active() && a.flow.ctl.reversed()) {
                    flip_frames++;
                    flip_composed += a.flow.pane() && a.flow.pane()->segment_frames() > sf0;
                }
                feq = feq && a.scr.pix == b.scr.pix && v.scr.pix == b.scr.pix;
                const eng::Instance* pa = a.flow.pane();
                pb = b.flow.pane();
                if (!pa || !pb || !b.flow.ctl.active() || pe.size() != pb->e().size())
                    return;
                feq = feq && pa->e() == pb->e() && pa->ids() == pb->ids() && pa->age() == pb->age() &&
                      pa->held() == pb->held() && pa->cover() == pb->cover();
                Rect pr = pb->rect();
                int pw = pr.x1 - pr.x0;
                const SlidePlan* sp = b.flow.plan();
                Rect g = sp && sp->pane_frame ? sp->pane_frame->ground.intersect(pr) : Rect{};
                for (int y = g.y0; trail && !g.empty() && y < g.y1; y++)
                    for (int x = g.x0; x < g.x1; x++) {
                        size_t i = (size_t)(y - pr.y0) * pw + (size_t)(x - pr.x0);
                        int id = pb->ids()[i] & eng::kIdMask;
                        if (id >= 2 && id <= 9 && (pid[i] & eng::kIdMask) == 0 && (ph[i] & eng::kIdMask) == id) {
                            trail_px++;
                            trail_bad += pb->e()[i] < pe[i];
                        }
                    }
                Rect gr = glow_rect_of(b.flow.band());
                int ggw = gr.x1 - gr.x0;
                const int art_step = (int)((int64_t)eng::kArtStep * 256 * 3 * 16 / 50);
                const Glow& gb = b.flow.glow();
                if (!tiles || ge.size() != gb.e.size() || gb.e.size() != (size_t)ggw * (size_t)(gr.y1 - gr.y0))
                    return;
                for (int y = std::max(gr.y0, pr.y0); y < std::min(gr.y1, pr.y1); y++)
                    for (int x = pr.x0; x < std::min(gr.x1, pr.x1); x++) {
                        size_t i = (size_t)(y - pr.y0) * pw + (size_t)(x - pr.x0);
                        size_t j = (size_t)(y - gr.y0) * ggw + (size_t)(x - gr.x0);
                        if (!pc[i] || pb->cover()[i] || !gb.ids[j])
                            continue;
                        tile_px++;
                        tile_bad += ge[j] != 0 || gb.e[j] == 0 || gb.e[j] > art_step + 1;
                    }
            };
            auto press = [&](PadAction k) {
                all([&](L5Pane& w) { w.flow.input(k, false, t); });
                t += 16;
            };
            for (int n = 0; n < 60; n++)
                step();
            tiles = true;
            press(PadAction::Accept);
            for (int n = 0; n < 60; n++)
                step();
            tiles = false;
            press(PadAction::Back);
            for (int n = 0; n < 60; n++)
                step();
            press(PadAction::Accept);
            for (int n = 0; n < 8; n++)
                step();
            trail = true;
            press(PadAction::Back);
            for (int n = 0; n < 60; n++)
                step();
        }
        set_theme_light(light0);
        std::snprintf(said, sizeof said,
                      "panel flows: light and dark: an expand into a system whose games have art, so its "
                      "tiles draw, and whose ground panel leaves column 2's resting tiles has them rise from ground "
                      "at one step under the panel's edge (%d px, %d wrong), and a "
                      "Back mid expand brings each row back over its own trail inside the panel with its e kept, not "
                      "restarted (%d px, %d wrong) while the pane composes on every frame of the reversed fold (%d of "
                      "%d), the fast and verify flows equal to the pixel flow on every frame, pane planes and cover "
                      "included (%d)",
                      tile_px, tile_bad, trail_px, trail_bad, flip_composed, flip_frames, (int)feq);
        check(feq && tile_px > 0 && tile_bad == 0 && trail_px > 0 && trail_bad == 0 && flip_frames > 0 &&
                  flip_composed == flip_frames,
              said);
    }
    {
        struct L5Page {
            HomeFlow flow;
            ToastStrip ts;
            InkLayer ink;
            Buffer scr;
            int pattern = 0;
            char why = 0;

            L5Page(Fonts& fonts, const FlowOpts& o, bool two = false) : flow(fonts, o)
            {
                flow.model.systems = {{"settings", 0, ""}};
                flow.kinds = {MenuKind::Settings};
                if (two) {
                    flow.model.systems = {{"GB", 3, ""}, {"settings", 0, ""}};
                    flow.kinds = {MenuKind::System, MenuKind::Settings};
                    flow.games = [](int) {
                        std::vector<HomeGame> list;
                        for (int i = 0; i < 3; i++)
                            list.push_back({1000 + i, "Synth Page Game " + std::to_string(i), "GB"});
                        return list;
                    };
                }
                flow.draw_page = [this](Canvas& c, Screen s, int64_t) {
                    c.fill(kScreen, theme().bg);
                    uint32_t ink = (s == Screen::Bluetooth ? 0x3060C0u : 0xC0C0C0u) ^ ((uint32_t)pattern * 0x0A0B0Cu);
                    for (int r = 0; r < 3; r++)
                        c.fill({40, 120 + r * 96, 900, 180 + r * 96}, ink & 0xFFFFFFu);
                };
                flow.page_input = [this](Screen s, PadAction a, bool rep) {
                    if (s == Screen::Settings && a == PadAction::Accept && !rep) {
                        flow.nav.stack.push_back(Screen::Bluetooth);
                        flow.push(Screen::Bluetooth);
                        return false;
                    }
                    return a == PadAction::Back && !rep;
                };
                flow.page_rows = [](Screen s) {
                    if (s == Screen::Settings)
                        return std::vector<PageRow>{{"settings-row0", Rect{0, 98, kPortraitW, 184}, eng::Ink::Text}};
                    return std::vector<PageRow>{{"bluetooth-row0", Rect{0, 120, kPortraitW, 216}, eng::Ink::Text}};
                };
                flow.page_head = [](Screen s) { return synth_head(s, 3); };
                flow.draw_word = [this](Canvas& c, const PageHead& h) {
                    flow.draw_page(c, h.word == "settings" ? Screen::Settings : Screen::Bluetooth, 0);
                };
                flow.last_frame = [this](const Surface& s) {
                    copy(scr.surface(), s);
                    ts.under(s);
                };
                flow.ctl.target_current = [this](int64_t) { return why; };
                flow.setup();
            }
            void frame(Fonts& fonts, int64_t now)
            {
                ts.under(scr.surface());
                flow.frame(now, scr.surface());
                ts.draw(fonts, ink, scr.surface(), now);
            }
            eng::Instance* page() { return const_cast<eng::Instance*>(flow.page_engine()); }
        };
        bool light0 = theme().light;
        int folds = 0, adopts = 0, vadopts = 0, vfails = 0, refused = 0, wrong = 0, mutants = 0, differ = 0;
        int gp[2] = {}, ap[2] = {}, ip[2] = {};
        int la[3] = {}, lm[3] = {}, lv[3] = {}, madopts = 0, dx = -1, dy = -1, vx = -1, vy = -1;
        std::string got, dk = "-", vk = "-";
        bool same = true, gate = true;
        for (bool light : {false, true}) {
            set_theme_light(light);
            FlowOpts fo;
            fo.lang = true;
            FlowOpts po = fo;
            fo.fast = true;
            FlowOpts vo = fo;
            vo.verify = true;
            L5Page a(fonts, fo), b(fonts, po), v(fonts, vo), mu(fonts, fo);
            int64_t t = 0;
            bool forced = false, mdiff = false;
            auto all = [&](const std::function<void(L5Page&)>& f) {
                f(a);
                f(b);
                f(v);
                f(mu);
            };
            auto run = [&](int frames) {
                for (int n = 0; n < frames; n++, t += 16) {
                    all([&](L5Page& w) { w.frame(fonts, t); });
                    same = same && a.scr.pix == b.scr.pix && v.scr.pix == b.scr.pix;
                    if (dx < 0 && a.scr.pix != b.scr.pix) {
                        Surface sa = a.scr.surface(), sb = b.scr.surface();
                        for (int y = 0; y < sa.h && dx < 0; y++)
                            for (int x = 0; x < sa.w && dx < 0; x++)
                                if (sa.at(x, y) != sb.at(x, y)) {
                                    dx = x;
                                    dy = y;
                                    dk = a.flow.ctl.active() ? fold_kind_name(a.flow.ctl.kind()) : "rest";
                                }
                    }
                    mdiff = mdiff || mu.scr.pix != b.scr.pix;
                }
            };
            auto fold = [&](PadAction act, char want) {
                all([&](L5Page& w) { w.flow.input(act, false, t); });
                t += 16;
                int vf0 = v.page() ? v.page()->adopt_fails() : 0;
                const char* kind = fold_kind_name(a.flow.ctl.kind());
                bool took = a.flow.ctl.from_target();
                gate = gate && took == v.flow.ctl.from_target() && took == (a.flow.ctl.l5() == 'a');
                folds += took;
                int fk = a.flow.ctl.kind() == FoldKind::Push ? 0 : 1;
                int ad0 = a.page() ? a.page()->adopts() : 0, st0 = a.page() ? a.page()->adopt_struct() : 0;
                if (want) {
                    refused++;
                    if (a.flow.ctl.l5() != want || took) {
                        wrong++;
                        got += std::string(1, want) + ">" + std::string(1, a.flow.ctl.l5()) + " ";
                    }
                    if (forced && mu.page()) {
                        mu.page()->adopt_start(true);
                        mutants++;
                    }
                }
                mdiff = false;
                run(60);
                differ += forced && want && mdiff;
                if (vk == "-" && v.page() && v.page()->adopt_fails() > vf0)
                    vk = kind;
                forced = false;
                if (took) {
                    gp[fk]++;
                    ap[fk] += (a.page() ? a.page()->adopts() : 0) > ad0;
                    ip[fk] += (a.page() ? a.page()->adopt_struct() : 0) - st0;
                }
            };
            run(60);
            all([&](L5Page& w) { w.ts.show(t, "Synth toast", "over the pages", true); });
            run(40);
            fold(PadAction::Accept, 0);
            fold(PadAction::Accept, 0);
            fold(PadAction::Back, 0);
            fold(PadAction::Back, 0);
            all([&](L5Page& w) { w.ts.show(t, "Synth toast", "in its ramp", false); });
            run(2);
            fold(PadAction::Accept, 0);
            fold(PadAction::Back, 0);
            all([&](L5Page& w) {
                w.pattern++;
                w.flow.model.tiles.assign(kGridTiles, nullptr);
                w.flow.model.tiles[kGridTiles - 2] = solid(0x2080C0u);
                w.flow.redraw();
            });
            forced = true;
            fold(PadAction::Accept, 'r');
            for (char q : {'n', 'u', 'q'}) {
                all([&](L5Page& w) { w.why = q; });
                fold(PadAction::Back, q);
                fold(PadAction::Accept, q);
                all([&](L5Page& w) { w.why = 0; });
            }
            fold(PadAction::Back, 0);
            if (a.page()) {
                adopts += a.page()->adopts();
                vadopts += v.page()->adopts();
                vfails += v.page()->adopt_fails();
                la[0] += a.page()->adopt_cleared();
                lm[0] += mu.page()->adopt_cleared();
                la[1] += a.page()->adopt_nostart();
                lm[1] += mu.page()->adopt_nostart();
                la[2] += a.page()->adopt_drawn();
                lm[2] += mu.page()->adopt_drawn();
                madopts += mu.page()->adopts();
                lv[0] += v.page()->adopt_cleared();
                lv[1] += v.page()->adopt_nostart();
                lv[2] += v.page()->adopt_drawn();
                if (vx < 0 && v.page()->adopt_fail_x() >= 0) {
                    vx = v.page()->adopt_fail_x();
                    vy = v.page()->adopt_fail_y();
                }
            }
        }
        const char guards[] = {'r', 'm', 's', 'k'};
        int cover5[4] = {}, letter5[4] = {}, differ5[4] = {}, caught5[4] = {}, why5[4][5] = {};
        eng::Instance::AdoptRef ref5[4];
        for (int gi = 0; gi < 4; gi++) {
            FlowOpts fo;
            fo.lang = true;
            FlowOpts po = fo;
            fo.fast = true;
            FlowOpts vo = fo;
            vo.verify = true;
            L5Page a(fonts, fo, true), b(fonts, po, true), mu(fonts, fo, true), mv(fonts, vo, true);
            int64_t t = 0;
            bool diff = false;
            auto all = [&](const std::function<void(L5Page&)>& f) {
                f(a);
                f(b);
                f(mu);
                f(mv);
            };
            auto run = [&](int frames) {
                for (int n = 0; n < frames; n++, t += 16) {
                    all([&](L5Page& w) { w.frame(fonts, t); });
                    diff = diff || mu.scr.pix != b.scr.pix;
                }
            };
            auto press = [&](PadAction act) { all([&](L5Page& w) { w.flow.input(act, false, t); }); };
            run(60);
            char g = guards[gi];
            if (g == 'm') {
                press(PadAction::Down);
                run(2);
            } else {
                press(PadAction::Down);
                run(60);
            }
            if (g == 'r')
                all([&](L5Page& w) {
                    w.flow.model.systems[0] = {"NES", 3, ""};
                    w.flow.model.tiles.assign(kGridTiles, nullptr);
                    w.flow.model.tiles[kGridTiles - 2] = solid(0x2080C0u);
                    w.flow.redraw();
                });
            if (g == 's' || g == 'k') {
                press(PadAction::Accept);
                run(60);
                press(PadAction::Back);
                run(3);
            }
            if (g == 's') {
                press(PadAction::Up);
                press(PadAction::Down);
            }
            press(PadAction::Accept);
            t += 16;
            cover5[gi]++;
            letter5[gi] += a.flow.ctl.l5() == g && !a.flow.ctl.from_target();
            auto lost = [&]() {
                const eng::Instance* e = mu.flow.page_engine();
                return e ? std::vector<int>{e->adopts(), e->adopt_struct(), e->adopt_cleared(), e->adopt_nostart(),
                                            e->adopt_drawn()}
                         : std::vector<int>(5, 0);
            };
            std::vector<int> m0 = lost();
            for (L5Page* w : {&mu, &mv})
                if (w->page())
                    w->page()->adopt_start(true, true);
            diff = false;
            run(60);
            std::vector<int> m1 = lost();
            for (int k = 0; k < 5; k++)
                why5[gi][k] += m1[(size_t)k] - m0[(size_t)k];
            if (mu.flow.page_engine() && ref5[gi].id < 0 && ref5[gi].s1 != 0)
                ref5[gi] = mu.flow.page_engine()->adopt_ref();
            differ5[gi] += diff;
            caught5[gi] += mv.page() && mv.page()->adopt_fails() > 0;
        }
        std::string page_kind;
        bool page_kinds = true;
        for (int k = 0; k < 2; k++) {
            page_kind += std::string(" ") + (k ? "pop" : "push") + " gated " + std::to_string(gp[k]) + " adopted " +
                         std::to_string(ap[k]) + " i " + std::to_string(ip[k]) + ";";
            page_kinds = page_kinds && gp[k] > 0 && ap[k] > 0;
        }
        std::string per5;
        bool all5 = true;
        for (int gi = 0; gi < 4; gi++) {
            per5 += std::string(" ") + guards[gi] + " " + std::to_string(letter5[gi]) + "/" +
                    std::to_string(differ5[gi]) + "/" + std::to_string(caught5[gi]) + " of " +
                    std::to_string(cover5[gi]) + " (mu adopts " + std::to_string(why5[gi][0]) + " i " +
                    std::to_string(why5[gi][1]) + " cleared " + std::to_string(why5[gi][2]) + " no start " +
                    std::to_string(why5[gi][3]) + " drawn " + std::to_string(why5[gi][4]) + " ref id " +
                    std::to_string(ref5[gi].id) + " src " + std::to_string(ref5[gi].source) + " ident " +
                    std::to_string(ref5[gi].ident) + " t " + std::to_string(ref5[gi].t) + " s1 " +
                    std::to_string(ref5[gi].s1) + ")";
            all5 = all5 && cover5[gi] > 0 && letter5[gi] == cover5[gi] && differ5[gi] == cover5[gi] &&
                   caught5[gi] == cover5[gi];
        }
        set_theme_light(light0);
        char said[4096];
        std::snprintf(said, sizeof said,
                      "L5 page: light and dark, home to settings to Bluetooth pushes and both pops on U's own back, "
                      "a toast held and in its ramp: every page fold from "
                      "rest the gate takes is adopted with the target byte equal to the start render under verify "
                      "(gated folds %d, adopts %d, verify adopts %d fails %d), "
                      "the fast and verify flows equal the pixel path every frame (%d), the gate letters agree (%d); "
                      "refusals r, n, u and q give their letter and no adoption (%d cases, %d wrong), and a forced "
                      "adoption over a changed page differs from the pixel path (%d of %d, counted), a forced "
                      "adoption on a push started past each opening guard, from_ render_kept, gives the "
                      "guard's letter, differs and fails the two pass compare (letter/differ/fail per guard:%s), "
                      "and push and pop each gate and adopt (%s)"
                      " (a: adopts %d, cleared %d, no start %d, start drawn %d; v: adopts %d, fails %d, "
                      "cleared %d, no start %d, start drawn %d, "
                      "first fail x %d y %d in %s; mu: adopts %d, cleared %d, no start %d, start drawn %d; "
                      "first pixel difference x %d y %d in %s; wrong letters %s)",
                      folds, adopts, vadopts, vfails, (int)same, (int)gate, refused, wrong, differ, mutants,
                      per5.c_str(), page_kind.c_str(),
                      adopts, la[0], la[1], la[2], vadopts, vfails, lv[0], lv[1], lv[2], vx, vy, vk.c_str(), madopts,
                      lm[0],
                      lm[1], lm[2], dx, dy, dk.c_str(), got.c_str());
        check(same && gate && folds > 0 && adopts > 0 && vadopts > 0 && vfails == 0 && wrong == 0 && all5 &&
                  page_kinds,
              said);
        int near_cases = 0, near_ok = 0;
        std::string near_kept;
        for (bool light : {false, true}) {
            set_theme_light(light);
            FlowOpts fo;
            fo.lang = true;
            FlowOpts po = fo;
            fo.fast = true;
            FlowOpts vo = fo;
            vo.verify = true;
            L5Page a(fonts, fo, true), b(fonts, po, true), v(fonts, vo, true);
            int64_t t = 0;
            bool eq = true;
            auto all = [&](const std::function<void(L5Page&)>& f) {
                f(a);
                f(b);
                f(v);
            };
            auto run = [&](int frames) {
                for (int n = 0; n < frames; n++, t += 16) {
                    all([&](L5Page& w) { w.frame(fonts, t); });
                    eq = eq && a.scr.pix == b.scr.pix && v.scr.pix == b.scr.pix;
                }
            };
            auto press = [&](PadAction act) {
                all([&](L5Page& w) { w.flow.input(act, false, t); });
                t += 16;
                run(60);
            };
            auto early = [&](bool page) {
                all([&](L5Page& w) { w.flow.input(PadAction::Accept, false, t); });
                t += 2;
                char letter = a.flow.ctl.l5();
                bool took = a.flow.ctl.from_target() && v.flow.ctl.from_target();
                const eng::Instance* ea = page ? a.flow.page_engine() : a.flow.pane();
                const eng::Instance* ev = page ? v.flow.page_engine() : v.flow.pane();
                int ad0 = ea ? ea->adopts() : 0, st0 = ea ? ea->adopt_struct() : 0;
                int va0 = ev ? ev->adopts() : 0, vf0 = ev ? ev->adopt_fails() : 0;
                eq = true;
                run(1);
                eng::Frame f0, s0;
                if (const PageRun* r = page ? a.flow.page_run() : nullptr) {
                    f0 = r->frame;
                    s0 = page_frame(r->form, 0);
                } else if (const SlidePlan* sp = page ? nullptr : a.flow.plan(); sp && sp->pane_frame) {
                    f0 = *sp->pane_frame;
                    pane_frame_at(*sp, 0, a.scr.w, a.scr.h, s0);
                }
                std::vector<uint8_t> still;
                for (size_t k = 0; k < f0.poses.size() && f0.poses.size() == s0.poses.size(); k++) {
                    const eng::Pose &q = f0.poses[k], &q0 = s0.poses[k];
                    const eng::Block& bk = f0.blocks[(size_t)q.block];
                    size_t id = (size_t)(bk.id & eng::kIdMask);
                    if (still.size() <= id)
                        still.resize(id + 1, 0);
                    still[id] = q.block == q0.block && q.rect.x0 == q0.rect.x0 && q.rect.y0 == q0.rect.y0 &&
                                q.rect.x1 == q0.rect.x1 && q.rect.y1 == q0.rect.y1;
                }
                int kept = 0, owned = 0;
                for (uint16_t id : ea ? ea->ids() : std::vector<uint16_t>()) {
                    size_t k = (size_t)(id & eng::kIdMask);
                    owned += k != 0;
                    kept += k != 0 && k < still.size() && still[k];
                }
                run(59);
                bool ok = letter == 'a' && took && ea && ev && ea->adopts() > ad0 && ea->adopt_struct() == st0 &&
                          ev->adopts() > va0 && ev->adopt_fails() == vf0 && kept > 0 && kept == owned && eq;
                near_cases++;
                near_ok += ok;
                near_kept += std::string(" ") + (page ? "push " : "expand ") + (light ? "light " : "dark ") +
                             std::to_string(kept) + " of " + std::to_string(owned) + (ok ? "" : " FAILED") + ";";
            };
            run(60);
            all([&](L5Page& w) { w.ts.show(t, "Synth toast", "held at 2 ms", true); });
            run(40);
            early(false);
            press(PadAction::Back);
            press(PadAction::Down);
            early(true);
            press(PadAction::Back);
        }
        set_theme_light(light0);
        std::snprintf(said, sizeof said,
                      "L5 2 ms: Step, light and dark, a home expand and a home to settings push whose frame 0 lands 2 "
                      "ms after the input each read a and adopt with 0 two pass fails, with every frame 0 owner at "
                      "its start pose (kept of owned px:%s), and the fast and verify flows equal the pixel path on "
                      "every frame (%d of %d)",
                      near_kept.c_str(), near_ok, near_cases);
        check(near_cases == 4 && near_ok == near_cases, said);
    }
    {
        struct G5 {
            HomeFlow flow;
            ToastStrip ts;
            InkLayer ink;
            Buffer scr;
            FrameGate gate;
            char why = 0;

            G5(Fonts& fonts, const FlowOpts& o) : flow(fonts, o)
            {
                flow.model.systems = {{"GB", 9, ""}, {"GBA", 9, ""}, {"PS1", 9, ""}};
                flow.kinds = {MenuKind::System, MenuKind::System, MenuKind::System};
                flow.games = [](int sys) {
                    std::vector<HomeGame> list;
                    const char* const names[] = {"GB", "GBA", "PS1"};
                    for (int i = 0; i < 9; i++)
                        list.push_back({1000 * (sys + 1) + i, "Synth Gate " + std::to_string(i), names[sys]});
                    return list;
                };
                flow.last_frame = [this](const Surface& s) {
                    copy(scr.surface(), s);
                    ts.under(s);
                };
                flow.ctl.target_current = [this](int64_t) { return why; };
                flow.setup();
                flow.home.deal_recent(flow.model, {{901, "Synth Gate A", "GB"}, {902, "Synth Gate B", "GBA"}});
            }
            bool draw(Fonts& fonts, int64_t now, bool gated)
            {
                GateIn in;
                in.now = now;
                in.ready = true;
                in.claim = true;
                in.activated = true;
                in.animating = flow.animating(now) || ts.animating(now);
                GateStep st = gate.step(in);
                if (gated && !st.draw)
                    return false;
                ts.under(scr.surface());
                bool core = flow.frame(now, scr.surface());
                ts.draw(fonts, ink, scr.surface(), now);
                bool moved = core || ts.animating(now);
                if (st.tail && moved) {
                    bad_tail += core && !flow.animating(now);
                    if (tail_by.empty())
                        tail_by = std::string(flow.scroll_glide().running(now) ? "scroll " : "") +
                                  (flow.bar_glide().running(now) ? "bar " : "") +
                                  (flow.ctl.active() ? "active " : "") + (flow.ctl.folding(now) ? "folding " : "") +
                                  (flow.animating(now) ? "animating " : "") + (ts.animating(now) ? "toast" : "");
                }
                gate.posted(true, moved, now);
                why = core ? 'e' : 0;
                return true;
            }
            eng::Instance* pane() { return const_cast<eng::Instance*>(flow.pane()); }
            int bad_tail = 0;
            std::string tail_by;
        };
        int points = 0, equal = 0, tails = 0, e_cases = 0, e_ok = 0, e_mutants = 0, e_differ = 0, clears[2] = {};
        std::string clear_why[2], settle_why;
        int settled = 0, settles = 0, bad_tails = 0;
        {
            FlowOpts fo;
            fo.lang = true;
            FlowOpts po = fo;
            fo.fast = true;
            G5 g(fonts, fo), r(fonts, fo);
            int64_t t = 0;
            auto run = [&](int frames) {
                for (int n = 0; n < frames; n++, t += 16) {
                    g.draw(fonts, t, true);
                    r.draw(fonts, t, false);
                }
            };
            run(60);
            for (PadAction a : {PadAction::Down, PadAction::Down, PadAction::Up, PadAction::Accept, PadAction::Right,
                                PadAction::Right, PadAction::Left, PadAction::Back, PadAction::Left, PadAction::Back}) {
                g.flow.input(a, false, t);
                r.flow.input(a, false, t);
                run(80);
                points++;
                equal += g.scr.pix == r.scr.pix;
            }
            tails += g.gate.tails();
            bad_tails += g.bad_tail;
            G5 c(fonts, fo), cm(fonts, fo), cb(fonts, po);
            int64_t u = 0;
            auto step = [&](int frames) {
                for (int n = 0; n < frames; n++, u += 16) {
                    c.draw(fonts, u, true);
                    cm.draw(fonts, u, true);
                    cb.draw(fonts, u, false);
                }
            };
            step(60);
            for (G5* w : {&c, &cm, &cb})
                w->flow.input(PadAction::Down, false, u);
            for (int n = 0; n < 200 && (c.flow.animating(u) || c.why != 'e'); n++)
                step(1);
            int before = c.pane() ? c.pane()->adopts() : 0;
            for (G5* w : {&c, &cm, &cb})
                w->flow.input(PadAction::Accept, false, u);
            u += 16;
            e_cases++;
            bool refused = c.flow.ctl.l5() == 'e' && !c.flow.ctl.from_target();
            if (cm.pane()) {
                cm.pane()->adopt_start(true);
                e_mutants++;
            }
            bool diff = false;
            for (int n = 0; n < 60; n++, u += 16) {
                c.draw(fonts, u, true);
                cm.draw(fonts, u, true);
                cb.draw(fonts, u, false);
                diff = diff || cm.scr.pix != cb.scr.pix;
            }
            e_ok += refused && (c.pane() ? c.pane()->adopts() : 0) == before;
            e_differ += diff;
            for (int mode = 0; mode < 2; mode++) {
                G5 d(fonts, fo);
                int64_t w = 0;
                auto settle = [&]() {
                    int tail0 = d.gate.tails(), n = 0;
                    d.tail_by.clear();
                    for (; n < 400; n++, w += 16)
                        if (!d.draw(fonts, w, true) && !d.flow.animating(w) && !d.flow.ctl.active())
                            break;
                    settled += n < 400;
                    settles++;
                    settle_why += " " + std::to_string(d.gate.tails() - tail0) +
                                  (d.tail_by.empty() ? "" : " (tail moved: " + d.tail_by + ")");
                };
                settle();
                if (mode == 0) {
                    d.flow.input(PadAction::Down, false, w);
                    settle();
                } else {
                    d.ts.show(w, "Synth toast", "in its ramp", false);
                    d.draw(fonts, w, true);
                    w += 16;
                }
                int ad = d.pane() ? d.pane()->adopts() : 0;
                bool ramp = mode == 0 || d.ts.animating(w);
                d.flow.input(PadAction::Accept, false, w);
                w += 16;
                char l5 = d.flow.ctl.l5();
                bool took = l5 == 'a';
                for (int n = 0; n < 60; n++, w += 16)
                    d.draw(fonts, w, true);
                const eng::Instance* e = d.flow.pane();
                bad_tails += d.bad_tail;
                if (e)
                    clear_why[mode] += " ref id " + std::to_string(e->adopt_ref().id) + " src " +
                                       std::to_string(e->adopt_ref().source) + " ident " +
                                       std::to_string(e->adopt_ref().ident) + " t " + std::to_string(e->adopt_ref().t) +
                                       " s1 " + std::to_string(e->adopt_ref().s1);
                clear_why[mode] += std::string(" ") + (l5 ? l5 : '0') + " adopts " +
                                   std::to_string(e ? e->adopts() - ad : -1) + " i " +
                                   std::to_string(e ? e->adopt_struct() : -1) + " cleared " +
                                   std::to_string(e ? e->adopt_cleared() : -1) + " no start " +
                                   std::to_string(e ? e->adopt_nostart() : -1) + " drawn " +
                                   std::to_string(e ? e->adopt_drawn() : -1) + ";";
                clears[mode] += ramp && took && e && e->adopts() == ad + 1;
            }
        }
        char said[2048];
        std::snprintf(said, sizeof said,
                      "L5 landing: a flow drawn only when its gate wants a frame ends every glide on the frame a flow "
                      "drawn every 16 ms lands on (%d of %d rest points, %d tail frames); a fold started while the "
                      "last posted frame still moved is refused with e and adopts nothing (%d of %d), its forced "
                      "mutant counted (%d differ of %d); after the landing frame a fold adopts (%d of 1), and so does "
                      "one started during a toast ramp with the flow at rest (%d of 1) (after the landing:%s toast "
                      "ramp:%s); every settle reaches rest within 400 ticks (%d of %d, tail frames each:%s), and no "
                      "tail frame reports flow motion while the flow is not animating (%d)",
                      equal, points, tails, e_ok, e_cases, e_differ, e_mutants, clears[0], clears[1],
                      clear_why[0].c_str(), clear_why[1].c_str(), settled, settles, settle_why.c_str(), bad_tails);
        check(points > 0 && equal == points && tails > 0 && e_cases > 0 && e_ok == e_cases && clears[0] == 1 &&
                  clears[1] == 1 && settles > 0 && settled == settles &&
                  bad_tails == 0,
              said);
    }
    {
        struct L2Twin {
            HomeFlow flow;
            ToastStrip ts;
            InkLayer ink;
            Buffer scr;

            L2Twin(Fonts& fonts, const FlowOpts& o) : flow(fonts, o)
            {
                flow.model.systems = {{"GB", 9, ""}, {"GBA", 9, ""}, {"PS1", 9, ""}};
                flow.kinds = {MenuKind::System, MenuKind::System, MenuKind::System};
                flow.games = [](int sys) {
                    std::vector<HomeGame> list;
                    const char* const names[] = {"GB", "GBA", "PS1"};
                    for (int i = 0; i < 9; i++)
                        list.push_back({1000 * (sys + 1) + i, "Synth L2 " + std::to_string(i), names[sys]});
                    return list;
                };
                flow.last_frame = [this](const Surface& s) {
                    copy(scr.surface(), s);
                    ts.under(s);
                };
                flow.ctl.target_current = [](int64_t) { return (char)0; };
                flow.setup();
                flow.home.deal_recent(flow.model, {{901, "Synth L2 A", "GB"}, {902, "Synth L2 B", "GBA"}});
            }
            void frame(Fonts& fonts, int64_t now)
            {
                ts.under(scr.surface());
                flow.frame(now, scr.surface());
                ts.draw(fonts, ink, scr.surface(), now);
            }
            Glow& glow() { return const_cast<Glow&>(flow.glow()); }
        };
        const char* const modes[] = {"full", "partial", "restart", "size", "cur1", "moved", "recent"};
        std::string seen[7];
        const char want[] = {'a', 'p', 'a', 'n', 'a', 'g', 'a'};
        int cases[7] = {}, ok[7] = {};
        for (bool light : {false, true})
            for (int md = 0; md < 7; md++) {
                set_theme_light(light);
                FlowOpts fo;
                fo.lang = true;
                fo.fast = true;
                L2Twin a(fonts, fo), b(fonts, fo);
                int64_t t = 0;
                bool same = true, planes = true, track = false, used = false;
                auto run = [&](int frames, bool cmp) {
                    for (int n = 0; n < frames; n++, t += 16) {
                        a.frame(fonts, t);
                        b.frame(fonts, t);
                        same = same && a.scr.pix == b.scr.pix;
                        planes = planes && (!cmp || !track || glow_all(a.flow.glow(), b.flow.glow()));
                        const Glow &ga = a.flow.glow(), &gb = b.flow.glow();
                        bool eq = ga.e == gb.e && ga.age == gb.age && ga.ids == gb.ids && ga.hid == gb.hid;
                        used = used || (track && md == 4 && !eq);
                    }
                };
                auto press = [&](PadAction k) {
                    a.flow.input(k, false, t);
                    b.flow.input(k, false, t);
                    t += 16;
                };
                a.ts.show(t, "Synth toast", "held", true);
                b.ts.show(t, "Synth toast", "held", true);
                run(60, false);
                press(PadAction::Accept);
                run(60, true);
                press(PadAction::Back);
                run(60, true);
                bool first_p = true;
                track = true;
                if (md == 1 || md == 2)
                    for (int k = 0; k < 3; k++)
                        a.flow.prepare(16);
                if (md == 2) {
                    press(PadAction::Accept);
                    run(1, true);
                    first_p = a.flow.glow().l2 == 'p';
                    run(59, true);
                    press(PadAction::Back);
                    run(60, true);
                }
                Rect gr = glow_rect_of(a.flow.band());
                int cap = 4 * ((gr.y1 - gr.y0) / 16 + 1), slices = 0;
                bool hung = false;
                if (md == 0 || md == 2 || md == 4 || md == 5 || md == 6) {
                    while (a.flow.glow_owed() && slices++ < cap)
                        a.flow.prepare(16);
                    hung = a.flow.glow_owed();
                }
                if (md == 3)
                    glow_prepare(a.glow(), a.flow.glow().gen + 1, Rect{gr.x0, gr.y0, gr.x1 + 16, gr.y1 + 16},
                                 100000);
                if (md == 4)
                    std::fill(a.glow().cur.begin(), a.glow().cur.end(), (uint8_t)1);
                if (md == 5)
                    a.glow().gen += 1;
                bool before = same;
                press(md == 6 ? PadAction::Left : PadAction::Accept);
                run(1, true);
                char got = a.flow.glow().l2;
                bool browse = md != 6 || a.flow.ctl.details();
                run(59, true);
                press(PadAction::Back);
                run(60, true);
                cases[md]++;
                seen[md] += std::string(" ") + (got ? got : '0') + (before ? "b" : "-") + (same ? "s" : "-") +
                            (planes ? "p" : "-") + (md != 6 ? "" : browse ? "d" : "-") + (hung ? "h" : "") +
                            (md != 4 ? "" : used ? "u" : "-");
                if (md == 4)
                    ok[md] += got == 'a' && before && (!same || used) && !hung;
                else
                    ok[md] += got == want[md] && first_p && same && planes && browse && !hung;
            }
        set_theme_light(false);
        std::string per;
        bool all = true;
        for (int md = 0; md < 7; md++) {
            per += std::string(" ") + modes[md] + " " + std::to_string(ok[md]) + " of " + std::to_string(cases[md]) +
                   " (" + seen[md] + " )";
            all = all && cases[md] > 0 && ok[md] == cases[md];
        }
        check(all, ("L2: the Glow planes prepared at idle are adopted by the next expand and by a Recent browse (a), a "
                    "partial prepare is "
                    "reset (p) and the next full one adopted after its cursor restarts, a prepare at another size is "
                    "reassigned (n), a prepare whose gen moved is reset (g), each with every frame equal and the "
                    "whole shared Glow (glow_all) equal to an unprepared twin from the fold after the prepare, and "
                    "a prepare whose cur plane is left at 1 differs in pixels or in e, age, ids or hid (letter, "
                    "then b before equal, s same, p Glow equal, d a Recent browse ran, h a prepare that never "
                    "finished, u the cur mutant changed e, "
                    "age, ids or hid, per case:" + per + ")")
                       .c_str());
    }
    {
        struct Pages {
            HomeFlow flow;
            ToastStrip ts;
            InkLayer ink;
            Buffer scr;
            int pattern = 0;
            int pivot = 0;
            bool under = true;

            Pages(Fonts& fonts, const FlowOpts& o, bool use_under) : flow(fonts, o), under(use_under)
            {
                flow.model.systems = {{"settings", 0, ""}};
                flow.kinds = {MenuKind::Settings};
                flow.draw_page = [this](Canvas& c, Screen s, int64_t) {
                    c.fill(kScreen, theme().bg);
                    uint32_t ink = (s == Screen::Bluetooth ? 0x3060C0u : 0xC0C0C0u) ^ ((uint32_t)pattern * 0x0A0B0Cu);
                    for (int r = 0; r < 3; r++)
                        c.fill({40, 120 + r * 96, 900, 180 + r * 96}, ink & 0xFFFFFFu);
                };
                flow.page_input = [this](Screen s, PadAction a, bool rep) {
                    if (s == Screen::Settings && a == PadAction::Accept && !rep) {
                        flow.nav.stack.push_back(Screen::Bluetooth);
                        flow.push(Screen::Bluetooth);
                        return false;
                    }
                    return a == PadAction::Back && !rep;
                };
                flow.page_rows = [](Screen s) {
                    if (s == Screen::Settings)
                        return std::vector<PageRow>{{"settings-row0", Rect{0, 98, kPortraitW, 184}, eng::Ink::Text}};
                    return std::vector<PageRow>{{"bluetooth-row0", Rect{0, 120, kPortraitW, 216}, eng::Ink::Text}};
                };
                flow.page_head = [this](Screen s) { return synth_head(s, (flow.opening() ? 0 : pivot) == 0 ? 3 : 2); };
                flow.page_open = [this](Screen s, int64_t) {
                    if (s == Screen::Settings)
                        pivot = 0;
                };
                flow.draw_word = [this](Canvas& c, const PageHead& h) {
                    flow.draw_page(c, h.word == "settings" ? Screen::Settings : Screen::Bluetooth, 0);
                };
                flow.last_frame = [this](const Surface& s) { copy(scr.surface(), s); };
                flow.setup();
            }
            void frame(Fonts& fonts, int64_t now)
            {
                if (under)
                    ts.under(scr.surface());
                flow.frame(now, scr.surface());
                ts.draw(fonts, ink, scr.surface(), now);
            }
        };
        {
            bool same = true, differ = false, trusted = true, after = true, reversed = true, bt = true;
            int redraws = 0, late = 0;
            int64_t t_push = 0;
            std::string name = "step";
            FlowOpts fo;
            fo.lang = true;
            FlowOpts po = fo;
            fo.fast = true;
            Pages a(fonts, fo, true), b(fonts, po, true), c(fonts, fo, false);
            int64_t clock = 0;
            a.flow.set_clock([&clock]() { return clock += 5; });
            int64_t t = 0;
            auto tr = [&]() { return a.flow.page_engine() ? a.flow.page_engine()->trusted_frames() : -1; };
            auto all = [&](const std::function<void(Pages&)>& f) {
                f(a);
                f(b);
                f(c);
            };
            auto run = [&](int frames, bool redraw) {
                for (int n = 0; n < frames; n++, t += 16) {
                    int before = -1;
                    bool tail = false;
                    if (redraw && n % 4 == 3 && a.flow.ctl.active()) {
                        all([&](Pages& w) {
                            w.pattern++;
                            w.flow.redraw();
                        });
                        before = tr();
                        redraws++;
                        tail = a.flow.page_run() && t - t_push - 16 >= form_ms(a.flow.page_run()->form);
                    }
                    a.frame(fonts, t);
                    b.frame(fonts, t);
                    c.frame(fonts, t);
                    if (tail && a.flow.ctl.active())
                        late++;
                    same = same && a.scr.pix == b.scr.pix;
                    differ = differ || c.scr.pix != b.scr.pix;
                    if (before >= 0)
                        after = after && tr() == before;
                }
            };
            run(60, false);
            all([&](Pages& w) { w.ts.show(t, "Synth toast", "over the pages", false); });
            t_push = t;
            all([&](Pages& w) { w.flow.input(PadAction::Accept, false, t); });
            run(14, true);
            all([&](Pages& w) { w.flow.input(PadAction::Back, false, t); });
            reversed = reversed && a.flow.ctl.reversed();
            run(60, false);
            t_push = t;
            all([&](Pages& w) { w.flow.input(PadAction::Accept, false, t); });
            t += 16;
            run(60, true);
            t_push = t;
            all([&](Pages& w) { w.flow.input(PadAction::Accept, false, t); });
            run(60, true);
            bt = bt && a.flow.nav.top() == Screen::Bluetooth;
            all([&](Pages& w) { w.flow.input(PadAction::Back, false, t); });
            run(60, false);
            all([&](Pages& w) { w.flow.input(PadAction::Back, false, t); });
            run(60, false);
            trusted = trusted && a.flow.page_engine() && a.flow.page_engine()->trusted_frames() > 0 &&
                      a.flow.page_engine()->segment_frames() > 0 && b.flow.page_engine() &&
                      b.flow.page_engine()->trusted_frames() == 0;
            check(same, (name + " R8 pages: a fast HomeFlow composes every frame of its page folds equal to the pixel "
                                "path into a persistent target with the toast under and drawn around each frame, with "
                                "redraws of a changed page every fourth frame of a fold")
                            .c_str());
            check(trusted && after && redraws > 0,
                  (name + " R8 pages: the page engine trusted and on segments on the fast path, never on the pixel "
                          "path; the frame after each redraw untrusted")
                      .c_str());
            const eng::Instance* pe = a.flow.page_engine();
            bool timed = pe && pe->times().begin > 0 && pe->times().starts > 0 && pe->times().passes > 0 &&
                         pe->times().pass > 0 && pe->times().start_marks > 0 && pe->times().start_fills > 0 &&
                         pe->times().start_build > 0;
            check(timed, (name + " R8 pages: the fast flow's frame 0 timers ran on the page engine (begin, frame "
                                 "passes, and the start pass of the push whose first frame lands one frame after its "
                                 "input, split into marks, fills and seg_build)")
                             .c_str());
            check(late > 0, (name + " R8 pages: a redraw lands on a frame whose predecessor is also at or past "
                                    "form_ms from its push, while the fold still runs after it (the pose held since "
                                    "form_ms)")
                                .c_str());
            check(differ, (name + " R8 pages: a fast twin without the toast's under() differs from the pixel path")
                              .c_str());
            check(reversed && bt && a.flow.nav.top() == Screen::Home,
                  (name + " R8 pages: a home to settings push reversed by Back, a push landed, settings to Bluetooth, "
                          "both pops back home")
                      .c_str());
        }
        {
            std::string name = "step";
            FlowOpts fo;
            fo.lang = true;
            FlowOpts po = fo;
            fo.fast = true;
            Pages a(fonts, fo, true), b(fonts, po, true);
            bool same = true, planes = true, owed = true, used = true;
            int64_t t = 0;
            auto run = [&](int frames) {
                for (int n = 0; n < frames; n++, t += 16) {
                    a.frame(fonts, t);
                    b.frame(fonts, t);
                    same = same && a.scr.pix == b.scr.pix;
                    const eng::Instance* pa = a.flow.page_engine();
                    const eng::Instance* pb = b.flow.page_engine();
                    if (a.flow.ctl.active())
                        planes = planes && pa && pb && pa->e() == pb->e() && pa->ids() == pb->ids() &&
                                 pa->age() == pb->age() && pa->held() == pb->held();
                }
            };
            auto press = [&](PadAction k) {
                a.flow.input(k, false, t);
                b.flow.input(k, false, t);
                t += 16;
            };
            auto land = [&]() {
                for (int n = 0; n < 100 && a.flow.ctl.active(); n++)
                    run(1);
            };
            Rect gr = glow_rect_of(a.flow.band());
            int cap = 4 * ((gr.y1 - gr.y0 + kScreen.y1 - kScreen.y0) / 180 + 2);
            bool hung = false;
            auto prep_all = [&]() {
                for (int k = 0; k < cap && a.flow.prepare_owed(); k++)
                    a.flow.prepare(180);
                hung = hung || a.flow.prepare_owed();
            };
            run(30);
            owed = owed && a.flow.page_owed();
            prep_all();
            press(PadAction::Accept);
            run(1);
            used = used && a.flow.page_owed();
            land();
            run(30);
            a.flow.prepare(100);
            owed = owed && a.flow.page_owed();
            press(PadAction::Back);
            land();
            run(3);
            press(PadAction::Accept);
            land();
            run(30);
            prep_all();
            press(PadAction::Back);
            run(1);
            used = used && a.flow.page_owed();
            land();
            run(30);
            check(same && planes && owed && used && !hung && a.flow.nav.top() == Screen::Home,
                  (name + " R8 prepare: the page engine's begin prepared at rest in full, in part, or not at all (a "
                          "press before idle, a push 48 ms after a pop landed) composes every frame equal to an "
                          "unprepared pixel twin with the same planes during each fold, and begin consumes the "
                          "prepare" +
                   std::string(hung ? " (a prepare was still owed after its cap of " + std::to_string(cap) +
                                          " slices)"
                                    : ""))
                      .c_str());
        }
        {
            std::string name = "step";
            FlowOpts fo;
            fo.lang = true;
            FlowOpts po = fo;
            fo.fast = true;
            Pages a(fonts, fo, true), b(fonts, po, true);
            int calls = 0;
            auto rows_of = a.flow.page_rows;
            a.flow.page_rows = [&calls, rows_of](Screen s) {
                calls++;
                return rows_of(s);
            };
            const eng::Instance* pe = a.flow.page_engine();
            bool same = true, planes = true, ready = true, heads = true;
            a.pivot = b.pivot = 1;
            std::string got;
            int64_t t = 0;
            auto run = [&](int frames) {
                for (int n = 0; n < frames; n++, t += 16) {
                    a.frame(fonts, t);
                    b.frame(fonts, t);
                    same = same && a.scr.pix == b.scr.pix;
                    const eng::Instance* pb = b.flow.page_engine();
                    if (a.flow.ctl.active())
                        planes = planes && pe && pb && pe->e() == pb->e() && pe->ids() == pb->ids() &&
                                 pe->age() == pb->age() && pe->held() == pb->held();
                }
            };
            auto press = [&](PadAction k) {
                a.flow.input(k, false, t);
                b.flow.input(k, false, t);
                t += 16;
            };
            auto land = [&]() {
                for (int n = 0; n < 100 && a.flow.ctl.active(); n++)
                    run(1);
            };
            auto theme = [&](bool light) {
                set_theme_light(light);
                for (Pages* w : {&a, &b}) {
                    w->flow.theme_changed();
                    w->flow.redraw();
                }
                run(30);
            };
            auto prep_all = [&]() {
                for (int k = 0; k < 400 && a.flow.prepare_owed(); k++)
                    a.flow.prepare(16);
                ready = ready && pe && pe->start_ready() && !a.flow.prepare_owed();
            };
            auto push = [&]() {
                press(PadAction::Accept);
                run(1);
                got += pe ? pe->ps() : '?';
                const PageRun* pr = a.flow.page_run();
                bool under = false;
                heads = heads && pr && pr->src.size() == 4 && head_side(pr->form, under) && !under &&
                        pr->form.over_head.letters.size() == 4 && pr->form.over_head.tabs.size() == 3 && pr->src[3].px;
                if (pr) {
                    const PageForm& pf = pr->form;
                    size_t nu = pf.under_els.size(), no = pf.over_els.size();
                    bool rows = !pf.over_els.empty() && pf.over_els[0].r.y0 == 0 && pr->rows.size() == nu + no + 7 &&
                                pr->rows[nu].name == pf.over_els[0].name;
                    for (size_t k = 0; rows && k < 4; k++)
                        rows = pr->rows[nu + no + k].name == pf.over_head.letters[k].name;
                    for (size_t j = 0; rows && j < 3; j++)
                        rows = pr->rows[nu + no + 4 + j].name == pf.over_head.tabs[j].name;
                    heads = heads && rows;
                }
                land();
                run(30);
                press(PadAction::Back);
                land();
                run(30);
            };
            run(30);
            prep_all();
            int built = calls;
            a.flow.prepare_pass();
            a.flow.prepare_owed();
            bool once = built == 1 && calls == 2;
            push();
            for (int k = 0; k < 400 && pe && !pe->start_busy(); k++)
                a.flow.prepare(16);
            a.flow.prepare(16);
            push();
            prep_all();
            theme(true);
            push();
            prep_all();
            theme(false);
            prep_all();
            push();
            check(once, (name + " seed: the start frame is built once per idle pass, its settings rows read once over "
                                "every prepare and owed call of the pass, and again after the next pass begins")
                            .c_str());
            check(same && planes && ready && heads && got == "apga" && a.flow.nav.top() == Screen::Home,
                  (name + " seed: a push from home whose start was prepared at idle adopts it (a), a push after one "
                          "row slice of it resets it (p), a push after a theme switch at rest discards it (g), and a "
                          "seed the theme made stale at rest is dropped and prepared again at idle (a), every frame "
                          "equal to the pixel flow with the same planes during each fold, with the settings "
                          "letters and tabs built and the word layer as source 3, the rows keeping the settings chrome "
                          "row before the letters and the tabs, "
                          "the idle seed cut from the view the "
                          "page opens with though the last visit left another pivot (got " +
                   got + ")")
                      .c_str());
        }
        {
            FlowOpts fo;
            fo.lang = true;
            fo.fast = true;
            Pages on(fonts, fo, true), plain(fonts, fo, true);
            plain.flow.page_head = nullptr;
            for (Pages* w : {&on, &plain}) {
                w->frame(fonts, 0);
                w->flow.input(PadAction::Accept, false, 16);
                w->frame(fonts, 16);
            }
            const PageRun* rn = on.flow.page_run();
            const PageRun* rp = plain.flow.page_run();
            bool under = false, words = false, chrome = false;
            eng::Frame f0 = rp ? page_frame(rp->form, 0) : eng::Frame{};
            for (const eng::Block& b : f0.blocks) {
                words = words || b.source >= 2;
                chrome = chrome || (b.source == 1 && b.src.y0 == 0);
            }
            check(rn && rp && head_side(rn->form, under) && !head_side(rp->form, under) && !words && chrome &&
                      rp->src.size() == 4 && !rp->src[2].px && !rp->src[3].px,
                  "a flow with no page head builds no letters or tabs and no word layer source on home to settings, "
                  "and keeps the settings chrome as one block");
        }
    }

    {
        std::string name = "step";
        FlowOpts po;
        po.lang = true;
        FlowOpts fo = po;
        fo.fast = true;
        HomeFlow a(fonts, fo), b(fonts, po);
        Buffer sa, sb;
        int64_t clock = 0;
        for (HomeFlow* f : {&a, &b}) {
            f->model.systems = {{"GB", 3, ""}, {"GBA", 3, ""}, {"PS1", 3, ""}};
            f->kinds = {MenuKind::System, MenuKind::System, MenuKind::System};
            f->games = [](int sys) {
                std::vector<HomeGame> list;
                for (int i = 0; i < 3; i++)
                    list.push_back({1000 * (sys + 1) + i, "Synth Clock Game " + std::to_string(i), "GB"});
                return list;
            };
            Buffer& scr = f == &a ? sa : sb;
            f->last_frame = [&scr](const Surface& s) { copy(scr.surface(), s); };
            f->setup();
            f->home.deal_recent(f->model, {{901, "Synth Clock A", "GB"}, {902, "Synth Clock B", "GBA"}});
        }
        a.set_clock([&clock]() { return clock += 7; });
        int builds0 = a.glow().times.runs_builds;
        bool pixels = true, planes = true, nobuild = true;
        int64_t t = 0;
        auto run = [&](int frames) {
            for (int n = 0; n < frames; n++, t += 16) {
                a.frame(t, sa.surface());
                b.frame(t, sb.surface());
                pixels = pixels && sa.pix == sb.pix;
                nobuild = nobuild && a.glow().times.runs_builds == builds0;
                if (a.pane() && b.pane())
                    planes = planes && a.pane()->e() == b.pane()->e() && a.pane()->ids() == b.pane()->ids() &&
                             a.pane()->age() == b.pane()->age();
                planes = planes && a.glow().e == b.glow().e && a.glow().age == b.glow().age &&
                         a.glow().ids == b.glow().ids;
            }
        };
        run(60);
        for (PadAction k : {PadAction::Left, PadAction::Back, PadAction::Left, PadAction::Back}) {
            a.input(k, false, t);
            b.input(k, false, t);
            run(50);
        }
        bool seeded = false, why = true;
        for (PadAction k : {PadAction::Left, PadAction::Back}) {
            int64_t s0 = a.glow().times.seed, p0 = a.glow().times.seed_past;
            a.input(k, false, t);
            b.input(k, false, t);
            t += 16;
            run(1);
            bool ran = a.glow().times.seed > s0, marks = a.glow().seed_frames > 0;
            seeded = seeded || ran;
            why = why && (!ran || (marks == (a.glow().times.seed_why == 0) &&
                                   (!marks || a.glow().times.seed_past == p0)));
            run(49);
        }
        const Glow::Times& gt = a.glow().times;
        bool split = seeded && why && a.pane() && a.pane()->times().starts > 0 &&
                     a.pane()->times().start_marks > 0 && a.pane()->times().start_fills > 0 &&
                     a.pane()->times().start_build > 0 && gt.seed_raster > 0 && gt.seed_glow > 0 &&
                     gt.frame_raster > 0 && gt.frame_glow > 0 && b.glow().times.seed_raster == 0;
        bool timed = a.pane() && a.pane()->times().begin > 0 && a.pane()->times().pass > 0 &&
                     a.pane()->times().passes > 0 && a.glow().times.frame > 0 && b.pane() &&
                     b.pane()->times().passes == 0 && b.glow().times.frame == 0;
        check(pixels && planes && timed,
              (name + " R8 timers: with the frame 0 timers on (a clock hook on the fast flow), every frame and the "
                      "pane's and the grid's planes equal the pixel path over Recent expands and collapses; the "
                      "timers ran on the fast flow only")
                  .c_str());
        check(split, (name + " R8 timers: an expand and a collapse whose first frame lands one frame after the input "
                             "run the pane's start pass and the grid seed; the start pass split (marks, fills, "
                             "seg_build) and the grid paint split (raster, glow_frame, for the seed and the frame) ran "
                             "on the fast flow only, and a seed's reason is empty exactly when it was state only, with "
                             "no pixel past w then")
                         .c_str());
        check(nobuild, (name + " R8 timers: the fast flow's grid never builds its row runs from the planes, the folds "
                               "whose input leads their first frame (the seed and frame 0 on runs) included")
                           .c_str());
    }

    {
        auto tiles = [](const std::vector<Rect>& v, int y0, int y1) {
            int x = 0;
            for (const Rect& r : v) {
                if (r.x0 != x || r.x1 <= r.x0 || r.y0 != y0 || r.y1 != y1)
                    return false;
                x = r.x1;
            }
            return !v.empty() && x == kPortraitW;
        };
        std::vector<Rect> ls = page_letter_cuts(fonts, settings_word()), lb = page_letter_cuts(fonts, bluetooth_word());
        SettingsView v;
        v.pivots = {"display", "sound", "controls", "games", "network", "power", "system"};
        std::vector<Rect> td = settings_tab_cuts(fonts, v);
        v.pivot = 4;
        std::vector<Rect> tn = settings_tab_cuts(fonts, v);
        std::string got = std::to_string(ls.size()) + " " + std::to_string(lb.size()) + " " +
                          std::to_string(td.size()) + " " + std::to_string(tn.size());
        check(ls.size() == 8 && lb.size() == 9 && tiles(ls, 0, 140) && tiles(lb, 0, 140),
              ("letter cuts: 8 on settings and 9 on bluetooth, from each glyph's ink at its pen, tiling 0 to 960 x 0 "
               "to 140 (got " + got + ")")
                  .c_str());
        v.pivot = 0;
        PageHead sh = settings_page_head(fonts, v), bh = bluetooth_page_head(fonts);
        bool names = sh.word == "settings" && sh.layer && sh.letters.size() == ls.size() &&
                     sh.tabs.size() == td.size() && bh.word == "bluetooth" && !bh.layer && bh.tabs.empty() &&
                     bh.letters.size() == lb.size();
        for (size_t k = 0; names && k < sh.letters.size(); k++)
            names = sh.letters[k].name == "settings-header-l" + std::to_string(k);
        for (size_t j = 0; names && j < sh.tabs.size(); j++)
            names = sh.tabs[j].name == "settings-pivots-t" + std::to_string(j);
        for (size_t k = 0; names && k < bh.letters.size(); k++)
            names = bh.letters[k].name == "bluetooth-header-l" + std::to_string(k);
        auto band_of = [&](const char* word, Buffer& band) {
            Canvas bc(band.surface());
            draw_page_ground(bc, theme());
            draw_page_chrome(bc, fonts, word, nullptr, theme());
        };
        auto ink_col = [](const Surface& bs, int x) {
            for (int y = 0; y < bs.h; y++)
                if (bs.row(y)[x] != LiveF::pack(theme().bg))
                    return true;
            return false;
        };
        bool ground = true;
        for (const char* word : {settings_word(), bluetooth_word()}) {
            Buffer band(kPortraitW, 140);
            band_of(word, band);
            std::vector<Rect> cuts = page_letter_cuts(fonts, word);
            Surface bs = band.surface();
            bool inked = false;
            for (int x = 0; x < bs.w; x++)
                inked = inked || ink_col(bs, x);
            ground = ground && inked;
            for (size_t k = 1; k < cuts.size(); k++)
                ground = ground && !ink_col(bs, cuts[k].x0);
        }
        check(ground, "letter cuts: every interior cut column is ground in rows 0 to 140 of the chrome alone, for "
                      "settings and bluetooth, each band holding ink");
        auto gap_mid = [&](const Surface& bs, int x) {
            int g0 = x, g1 = x;
            while (g0 > 0 && !ink_col(bs, g0 - 1))
                g0--;
            while (g1 < bs.w && !ink_col(bs, g1))
                g1++;
            return std::make_pair((g0 + g1) / 2, g1 - g0);
        };
        struct Box {
            int a, b;
            char first, last;
        };
        auto u64 = [](fx u) { return (to_px(u) + 512) >> 10; };
        auto boxes_of = [&](const std::string& w) {
            TextStyle st = role::hubHeader.style();
            int x0 = u64(tok::hubEdge);
            std::vector<Box> runs;
            for (size_t i = 0; i < w.size(); i++) {
                std::string glyph = w.substr(i, 1);
                int pen = x0 + fonts.width64(st, w.substr(0, i + 1).c_str()) - fonts.width64(st, glyph.c_str());
                int l = 0, r = 0, p = (pen + 32) >> 6;
                fonts.ink64(st, glyph.c_str(), l, r);
                if (r <= l)
                    continue;
                Box k{p + (l >> 6), p + ((r + 63) >> 6), w[i], w[i]};
                if (!runs.empty() && k.a <= runs.back().b) {
                    runs.back().b = std::max(runs.back().b, k.b);
                    runs.back().last = w[i];
                } else {
                    runs.push_back(k);
                }
            }
            return runs;
        };
        int worst = 0, inside = 0, over = 0, narrow = kPortraitW, wide = 0, gaps = 0;
        char over_glyph = '-';
        bool centred = true;
        std::string off;
        for (const char* word : {settings_word(), bluetooth_word()}) {
            Buffer band(kPortraitW, 140);
            band_of(word, band);
            Surface bs = band.surface();
            std::vector<Rect> cuts = page_letter_cuts(fonts, word);
            std::vector<Box> runs = boxes_of(word);
            for (size_t k = 1; k < cuts.size() && k < runs.size(); k++) {
                int cut = cuts[k].x0, br = runs[k - 1].b, bl = runs[k].a;
                int g0 = cut, g1 = cut;
                while (g0 > 0 && !ink_col(bs, g0 - 1))
                    g0--;
                while (g1 < bs.w && !ink_col(bs, g1))
                    g1++;
                int in = std::max(g0 - br, bl - g1);
                if (br - g0 > over) {
                    over = br - g0;
                    over_glyph = runs[k - 1].last;
                }
                if (g1 - bl > over) {
                    over = g1 - bl;
                    over_glyph = runs[k].first;
                }
                worst = std::max(worst, std::abs(cut - (g0 + g1) / 2));
                inside = std::max(inside, in);
                narrow = std::min(narrow, g1 - g0);
                wide = std::max(wide, g1 - g0);
                gaps++;
                centred = centred && !ink_col(bs, cut) && cut == (br + bl) / 2 && in <= 1;
                if (std::abs(cut - (g0 + g1) / 2) > 1)
                    off += std::string(off.empty() ? "" : "; ") + word + " gap " + std::to_string(k) + " cut " +
                           std::to_string(cut) + " box " + std::to_string(br) + "-" + std::to_string(bl) + " drawn " +
                           std::to_string(g0) + "-" + std::to_string(g1);
            }
        }
        check(centred && gaps == 15,
              ("letter cuts against the drawn word: every interior cut of settings and bluetooth is the middle of its "
               "glyph boxes' gap and ground, and each glyph box holds the ink Fonts::draw leaves, an edge at most 1 "
               "px inside it, so a pen error, which moves every box against its ink, puts an exact edged glyph's box "
               "edge 2 px or more inside and fails (" +
               std::to_string(gaps) + " gaps, deepest box edge inside the ink " + std::to_string(inside) +
               " px, largest overhang " + std::to_string(over) + " px at " + std::string(1, over_glyph) +
               ", worst offset from the drawn middle " + std::to_string(worst) + " px, narrowest gap " +
               std::to_string(narrow) + " px, widest " + std::to_string(wide) + " px" +
               (wide < 4 ? "; every gap is under 4 px, so the centring cannot be judged" : "") +
               (off.empty() ? std::string("") : "; off the drawn middle by more than 1 px: " + off) + ")")
                  .c_str());
        std::string pair, most;
        int top = 0;
        TextStyle hs = role::hubHeader.style();
        for (char a = 'A'; a <= 'z'; a++)
            for (char b = 'A'; b <= 'z'; b++) {
                if (!std::isalpha((unsigned char)a) || !std::isalpha((unsigned char)b))
                    continue;
                std::string p1(1, a), p2(1, b), ab = p1 + p2;
                int w2 = fonts.width64(hs, p2.c_str());
                int kern = fonts.width64(hs, ab.c_str()) - fonts.width64(hs, p1.c_str()) - w2;
                if (std::abs(kern) > std::abs(top)) {
                    top = kern;
                    most = ab;
                }
                int l1 = 0, r1 = 0, l2 = 0, r2 = 0;
                fonts.ink64(hs, p1.c_str(), l1, r1);
                fonts.ink64(hs, p2.c_str(), l2, r2);
                int pen2 = fonts.width64(hs, ab.c_str()) - w2;
                if (pair.empty() && std::abs(kern) >= 4 * 64 && pen2 + l2 - r1 >= 4 * 64)
                    pair = ab;
            }
        bool pinned = true;
        int cut = -1, mid = -1;
        if (!pair.empty()) {
            Buffer band(kPortraitW, 140);
            band_of(pair.c_str(), band);
            Surface bs = band.surface();
            std::vector<Rect> cuts = page_letter_cuts(fonts, pair.c_str());
            cut = cuts.size() == 2 ? cuts[1].x0 : -1;
            mid = cut >= 0 ? gap_mid(bs, cut).first : -1;
            pinned = cut >= 0 && std::abs(cut - mid) <= 1;
        }
        char kerned[48];
        snprintf(kerned, sizeof kerned, "%.2f", top / 64.0);
        check(pinned,
              (pair.empty() ? "letter cuts with a pair kern: no hubHeader letter pair is kerned 4 px or more with ink "
                              "apart, so the gap check above holds the pens (largest kern " +
                                  std::string(kerned) + " px, " + (most.empty() ? std::string("none") : most) + ")"
                            : "letter cuts with a pair kern: " + pair + ", kerned 4 px or more with ink apart, cuts at "
                                  "the drawn gap's middle within 1 px (cut " + std::to_string(cut) + ", middle " +
                                  std::to_string(mid) + "; largest kern " + std::string(kerned) + " px, " + most +
                                  ")")
                  .c_str());
        check(names,
              "page heads: settings reads its word layer, its letters settings-header-l<k> and its tabs "
              "settings-pivots-t<j>; bluetooth reads its rest, letters bluetooth-header-l<k>, no tabs");
        check(td.size() == 5 && tn.size() == 3 && tiles(td, 116, 172) && tiles(tn, 116, 172),
              ("tab cuts: 5 on the display pivot and 3 on network, from the Chrome's label positions, tiling 0 to 960 "
               "x 116 to 172 (got " + got + ")")
                  .c_str());
    }

    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
