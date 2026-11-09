#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <poll.h>
#include <signal.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wayland-client.h>

#ifdef BLOCKIE_HAVE_XKB
#include <xkbcommon/xkbcommon-keysyms.h>
#else
#include <linux/input-event-codes.h>
#endif

#include "../core/curves.h"
#include "../gfx/glyph.h"
#include "../gfx/pixel.h"
#include "../ui/roles.h"
#include "bench.h"
#include "bt.h"
#include "bus.h"
#include "decodeq.h"
#include "framestats.h"
#include "homeflow.h"
#include "image.h"
#include "ink.h"
#include "input_pad.h"
#include "launch.h"
#include "library.h"
#include "nav.h"
#include "present_wl.h"
#include "repeat.h"
#include "screen_bt.h"
#include "scraper.h"
#include "screen_selector.h"
#include "screen_settings.h"
#include "setd_key.h"
#include "theme.h"
#include "theme_setting.h"
#include "tile_grid.h"
#include "zones.h"

namespace fd {

namespace {

using Art = std::shared_ptr<const Image>;

constexpr int kFrameWatchdogMs = 100;
constexpr int kThemeWaitMs = 300;
constexpr int kCoverWaitMs = 300;
constexpr int kBenchIdleMs = 5000;
constexpr int kBenchPollMs = 50;
constexpr const char* kZoneKey = "system.timezone";

void copy_surface(const Surface& src, const Surface& dst)
{
    Rect r = src.rect().intersect(dst.rect());
    if constexpr (std::is_same_v<LiveL, Rows>) {
        for (int y = r.y0; y < r.y1; y++)
            std::memcpy(dst.row(y) + r.x0, src.row(y) + r.x0, (size_t)(r.x1 - r.x0) * sizeof(Pix));
    } else {
        for (int y = r.y0; y < r.y1; y++)
            for (int x = r.x0; x < r.x1; x++)
                dst.at(x, y) = src.at(x, y);
    }
}

int64_t now_ms()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

int64_t now_us()
{
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

const char* action_name(PadAction a)
{
    switch (a) {
    case PadAction::Up: return "up";
    case PadAction::Down: return "down";
    case PadAction::Left: return "left";
    case PadAction::Right: return "right";
    case PadAction::Accept: return "accept";
    case PadAction::Back: return "back";
    case PadAction::Context: return "context";
    }
    return "?";
}

struct ScrapeJob {
    int64_t epoch;
    Game game;
    std::string libretro_folder;
};

struct ArtDrop {
    int64_t game_id;
    std::string path;
    Art img;
    bool decoded = false;
    int side = kTileSide;
    bool last = true;
};

}

static int run()
{
    std::fprintf(stderr, "blockie-frontend: started\n");
    WaylandPresenter p;
    if (!p.open("", true)) {
        std::fprintf(stderr, "blockie-frontend: no wayland display, exiting\n");
        return 1;
    }

    Bus bus;
    ThemeSetting theme_set;
    theme_set.on_value = [](bool light) { set_theme_light(light); };
    theme_set.attach(bus);
    SetdKey zone_key(kZoneKey);
    zone_key.attach(bus);
    int64_t t = now_ms();
    if (bus.open(t)) {
        int64_t until = t + kThemeWaitMs;
        while (!theme_set.answered() && now_ms() < until && bus.connected()) {
            struct pollfd bf;
            bf.fd = bus.fd();
            bf.events = POLLIN | (bus.wants_write() ? POLLOUT : 0);
            bf.revents = 0;
            if (!bus.data_remains())
                poll(&bf, 1, (int)std::max<int64_t>(0, until - now_ms()));
            bus.pump();
        }
    }
    std::fprintf(stderr, "blockie-frontend: theme %s\n", theme().light ? "light" : "dark");
    Bt bt(bus);
    bt.start();
    t = now_ms();
    p.begin_frame(t, p.show_at(t));
    p.begin_full();
    WaylandPresenter::S s = p.draw_target();
    for (int y = 0; y < kPortraitH; y++)
        fill_px(s.row(y), theme().bg, kPortraitW);
    p.present(kScreen);
    p.flip();

    std::string font_dir;
    if (const char* env = std::getenv("BLOCKIE_FONTS"))
        font_dir = env;
    if (font_dir.empty())
        font_dir = "/usr/share/blockie/fonts";
    Fonts fonts;
    if (!fonts.open(font_dir, {}))
        std::fprintf(stderr, "blockie-frontend: fonts not found in %s\n", font_dir.c_str());

    GlyphCache glyphs;
    BtPage btp(fonts, glyphs);
    btp.set_send([&](const BtCmd& cmd) { bt.run(cmd, now_ms()); });
    uint32_t bt_seen = bt.serial();

    const char* denv = std::getenv("BLOCKIE_DATA");
    std::string data_root = denv && *denv ? denv : "/var/lib/blockie/data";
    while (data_root.size() > 1 && data_root.back() == '/')
        data_root.pop_back();
    Library lib;
    FlowOpts fopts;
    HomeFlow flow(fonts, fopts);
    SelectorModel& model = flow.model;
    std::vector<std::string> sys_ids;
    std::vector<MenuKind>& kinds = flow.kinds;
    if (lib.open(data_root)) {
        int n = lib.scan();
        std::fprintf(stderr, "blockie-frontend: %d games\n", n);
        Scraper local(lib, data_root);
        std::fprintf(stderr, "blockie-frontend: local covers recorded %d\n", local.sweep_local(ArtType::Covers));
        for (const auto& sw : lib.systems_with_games()) {
            model.systems.push_back({sw.first.name, sw.second, ""});
            sys_ids.push_back(sw.first.id);
            kinds.push_back(MenuKind::System);
        }
    }

    int bt_item = (int)model.systems.size();
    model.systems.push_back({"bluetooth", 0, bt_subtitle(bt.state(), now_ms())});
    kinds.push_back(MenuKind::Bluetooth);
    model.systems.push_back({"settings", 0, "display"});
    kinds.push_back(MenuKind::Settings);

    Launcher launcher(data_root);

    PadReader pad;
    bool pad_ok = pad.open();
    std::fprintf(stderr, pad_ok ? "blockie-frontend: pad fd %d\n" : "blockie-frontend: no pad\n", pad.fd());

    bool need_redraw = false;
    Nav& nav = flow.nav;
    HomeState& home = flow.home;
    Repeat repeat;
    SettingsPage settings_page;
    settings_page.set_sheet_rows(sheet_rows(fonts));
    settings_page.reset(theme().light);
    auto sync_zone = [&]() {
        if (zone_key.has_value())
            settings_page.set_zone(zone_key.value());
        else
            settings_page.set_zone_missing(now_ms());
    };
    sync_zone();
    bool zones_loaded = false;
    InkLayer ink;
    int64_t frame_req_at = -1;
    bool drop_budget = false;
    std::unordered_map<int64_t, Game> by_id;
    std::unordered_map<int64_t, Art> held;
    Launcher::Child child;
    int64_t child_game_id = -1;

    std::deque<ScrapeJob> squeue;
    std::mutex qmutex;
    std::unordered_set<int64_t> requested;
    std::mutex art_mutex;
    std::vector<ArtDrop> art_drops;
    std::atomic<bool> scrape_stop{false};
    std::atomic<bool> scrape_pause{false};
    std::atomic<bool> scrape_busy{false};
    int art_evfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    int work_evfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    std::vector<ArtDrop> pending_drops;
    DecodeQueue dq;
    std::mutex dmutex;
    std::condition_variable dcv;
    bool decode_stop = false;
    bool decode_pause = false;
    int64_t win_gen = kRecentGen;
    FrameStats fstats;
    const char* fsenv = std::getenv("BLOCKIE_FRAMESTATS");
    fstats.on = fsenv && std::strcmp(fsenv, "1") == 0;
    const char* benv = std::getenv("BLOCKIE_BENCH");
    bool bench = benv && std::strcmp(benv, "grow") == 0;

    auto enqueue_scrape = [&](const Game& g, int64_t epoch, const std::string& folder) {
        bool added = false;
        {
            std::lock_guard<std::mutex> lk(qmutex);
            if (requested.insert(g.id).second) {
                squeue.push_back({epoch, g, folder});
                added = true;
            }
        }
        if (added && work_evfd >= 0) {
            uint64_t one = 1;
            ssize_t w = write(work_evfd, &one, sizeof one);
            (void)w;
        }
    };
    std::unordered_set<int64_t> failed_covers;
    auto queue_decode = [&](int64_t id, const std::string& path, int64_t gen) {
        bool added;
        {
            std::lock_guard<std::mutex> lk(dmutex);
            added = dq.push({id, path, gen, now_ms(), {kTileSide}});
        }
        if (added)
            dcv.notify_one();
    };
    auto request_cover = [&](const Game& g, int64_t gen) {
        if (home.cache.count(g.id))
            return true;
        if (failed_covers.count(g.id))
            return false;
        for (const auto& m : lib.media(g.id))
            if (m.type == "covers") {
                queue_decode(g.id, m.path, gen);
                return true;
            }
        return false;
    };
    auto system_cfg = [&](const std::string& system_id) -> const SystemCfg* {
        for (const auto& sc : lib.config())
            if (sc.id == system_id)
                return &sc;
        return nullptr;
    };
    auto folder_of = [&](const std::string& system_id) {
        const SystemCfg* sc = system_cfg(system_id);
        return sc ? sc->libretro_folder : std::string();
    };
    auto home_game = [&](const Game& g) {
        by_id[g.id] = g;
        const SystemCfg* sc = system_cfg(g.system_id);
        return HomeGame{g.id, g.name, sc ? sc->name : std::string()};
    };
    flow.setup();
    home.load = [&](const HomeGame& g) -> Art {
        auto it = held.find(g.id);
        return it != held.end() ? it->second : Art();
    };
    auto deal_recent = [&](bool first) {
        std::vector<Art>& grid = home.system_view ? home.recent_hold : model.tiles;
        held.clear();
        for (size_t k = 0; k < home.recent.size() && k < grid.size(); k++)
            if (grid[k])
                held[home.recent[k].id] = grid[k];
        std::vector<HomeGame> list;
        for (const Game& g : lib.recent_games(home.recent_cap))
            list.push_back(home_game(g));
        std::vector<HomeGame> want = first ? home.deal_recent(model, list) : flow.game_ran(list);
        held.clear();
        std::unordered_set<int64_t> ids;
        for (const HomeGame& g : home.recent)
            ids.insert(g.id);
        {
            std::lock_guard<std::mutex> lk(dmutex);
            dq.set_recent(std::move(ids));
        }
        int queued = 0;
        for (const HomeGame& hg : want) {
            const Game& g = by_id[hg.id];
            if (!request_cover(g, kRecentGen)) {
                enqueue_scrape(g, 0, folder_of(g.system_id));
                queued++;
            }
        }
        return queued;
    };
    int recent_jobs = deal_recent(true);
    auto render_page = [&](Canvas& c, Screen sc, int64_t now) {
        if (sc == Screen::Bluetooth)
            btp.draw(c, ink, now);
        else if (sc == Screen::Settings)
            draw_settings(c, fonts, ink, settings_page.view(now), theme());
    };
    auto page_animating = [&](int64_t now) {
        if (nav.top() == Screen::Settings && settings_page.animating(now))
            return true;
        return nav.top() == Screen::Bluetooth && btp.animating(now);
    };
    auto animating = [&](int64_t now) { return flow.animating(now) || page_animating(now); };
    auto frame = [&](int64_t now) -> bool {
        int64_t t0 = now_us();
        p.begin_frame(now, p.show_at(now));
        p.begin_full();
        Surface dst = p.draw_target();
        fonts.begin_frame(-1);
        bool folding = flow.ctl.active();
        bool more = flow.frame(now, dst);
        more = more || page_animating(now);
        p.present(kScreen);
        if (more)
            p.request_frame();
        bool ok = p.flip();
        if (ok && folding)
            fstats.frame(t0, now_us());
        if (ok && more)
            frame_req_at = now;
        if (ok && folding)
            drop_budget = true;
        if (!ok && more)
            p.cancel_frame();
        return ok;
    };
    auto can_animate = [&]() { return p.activated() && child.pid <= 0; };
    auto land_all = [&](int64_t now, const char* why) {
        if (!flow.animating(now))
            return;
        flow.land_all(now, why);
        if (p.frame_pending())
            p.cancel_frame();
        frame_req_at = -1;
        need_redraw = true;
    };

    auto try_launch = [&](const Game& g) {
        if (child.pid > 0)
            return false;
        const SystemCfg* sc = system_cfg(g.system_id);
        std::string core = sc ? sc->core : std::string();
        std::string sysname = sc ? sc->name : std::string();
        for (char& ch : sysname)
            ch = (char)std::tolower((unsigned char)ch);
        if (!launcher.core_installed(core)) {
            std::fprintf(stderr, "blockie-frontend: core %s absent for %s\n", core.c_str(), g.system_id.c_str());
            return false;
        }
        LaunchSpec spec;
        spec.system = sysname;
        spec.rompath_abs = g.rompath_abs;
        spec.core = core;
        Launcher::Child ch = launcher.spawn(spec);
        if (ch.pid < 0) {
            std::fprintf(stderr, "blockie-frontend: spawn failed for %s\n", g.system_id.c_str());
            return false;
        }
        child = ch;
        child_game_id = g.id;
        repeat.release_all();
        bt.set_game_running(true, now_ms());
        scrape_pause.store(true);
        {
            std::lock_guard<std::mutex> lk(dmutex);
            decode_pause = true;
        }
        std::fprintf(stderr, "blockie-frontend: launch %s pid %d\n", sysname.c_str(), (int)ch.pid);
        return true;
    };

    auto reap_children = [&]() {
        int status = 0;
        pid_t r;
        while ((r = waitpid(-1, &status, WNOHANG)) > 0) {
            if (child.pid <= 0 || r != child.pid)
                continue;
            LaunchExit ex = Launcher::classify(status);
            bool ran = ex != LaunchExit::ConfigError && ex != LaunchExit::CoreAbsent &&
                       ex != LaunchExit::AudioGate && ex != LaunchExit::Busy;
            if (ran && child_game_id >= 0) {
                lib.mark_played(child_game_id, (int64_t)time(nullptr));
                deal_recent(false);
            }
            if (!child.cfg_path.empty())
                unlink(child.cfg_path.c_str());
            std::fprintf(stderr, "blockie-frontend: game exited (%s)\n", ran ? "ran" : "not launched");
            child = {};
            child_game_id = -1;
            bt.set_game_running(false, now_ms());
            scrape_pause.store(false);
            {
                std::lock_guard<std::mutex> lk(dmutex);
                decode_pause = false;
            }
            dcv.notify_one();
            if (work_evfd >= 0) {
                uint64_t one = 1;
                ssize_t w = write(work_evfd, &one, sizeof one);
                (void)w;
            }
            if (pad_ok) {
                pad.poll();
                if (!pad.is_open())
                    pad_ok = false;
            }
            need_redraw = true;
        }
    };

    auto take_drops = [&]() {
        uint64_t cnt;
        while (read(art_evfd, &cnt, sizeof cnt) == (ssize_t)sizeof cnt) {
        }
        std::lock_guard<std::mutex> lk(art_mutex);
        for (ArtDrop& a : art_drops)
            pending_drops.push_back(std::move(a));
        art_drops.clear();
    };
    auto apply_drop = [&](const ArtDrop& drp) {
        bool want;
        {
            std::lock_guard<std::mutex> lk(dmutex);
            if (drp.decoded && drp.last)
                dq.done(drp.game_id);
            want = dq.accept(drp.game_id);
        }
        if (!drp.decoded)
            failed_covers.erase(drp.game_id);
        if (!want)
            return;
        bool in_recent = false;
        for (const HomeGame& g : home.recent)
            if (g.id == drp.game_id)
                in_recent = true;
        if (drp.decoded) {
            if (drp.img) {
                if (flow.art(drp.game_id, drp.side, home.epoch, drp.img) && !flow.ctl.active() && child.pid <= 0)
                    need_redraw = true;
                return;
            }
            failed_covers.insert(drp.game_id);
            bool in_system = false;
            for (const HomeGame& g : home.sysgames)
                if (g.id == drp.game_id)
                    in_system = true;
            auto it = by_id.find(drp.game_id);
            if (it != by_id.end() && (in_recent || in_system))
                enqueue_scrape(it->second, in_recent ? 0 : home.epoch, folder_of(it->second.system_id));
            return;
        }
        queue_decode(drp.game_id, drp.path, in_recent ? kRecentGen : win_gen);
    };
    flow.games = [&](int sys) {
        std::vector<HomeGame> list;
        if (sys < 0 || sys >= (int)sys_ids.size())
            return list;
        for (const Game& g : lib.games(sys_ids[sys]))
            list.push_back(home_game(g));
        return list;
    };
    flow.covers_wanted = [&](const std::vector<HomeGame>& want, int scroll) {
        {
            std::lock_guard<std::mutex> lk(dmutex);
            dq.set_window(++win_gen, home.keep_set(scroll));
        }
        for (const HomeGame& hg : want) {
            const Game& g = by_id[hg.id];
            if (!request_cover(g, win_gen))
                enqueue_scrape(g, home.epoch, folder_of(g.system_id));
        }
    };
    flow.system_left = [&]() {
        {
            std::lock_guard<std::mutex> lk(qmutex);
            std::deque<ScrapeJob> keep;
            requested.clear();
            for (auto& j : squeue) {
                bool in_recent = j.epoch == 0;
                for (const HomeGame& g : home.recent)
                    if (g.id == j.game.id)
                        in_recent = true;
                if (in_recent) {
                    requested.insert(j.game.id);
                    keep.push_back(std::move(j));
                }
            }
            squeue.swap(keep);
        }
        std::lock_guard<std::mutex> lk(dmutex);
        dq.set_window(kRecentGen, {});
    };
    flow.launch_game = [&](const HomeGame& g) {
        auto it = by_id.find(g.id);
        return it != by_id.end() && try_launch(it->second);
    };
    flow.draw_page = [&](Canvas& c, Screen sc, int64_t now) { render_page(c, sc, now); };
    flow.page_open = [&](Screen sc, int64_t now) {
        if (sc == Screen::Settings) {
            if (!zones_loaded) {
                settings_page.set_zones(load_zones(kZoneTab));
                zones_loaded = true;
            }
            settings_page.reset(theme().light);
            sync_zone();
            if (!zone_key.has_value())
                zone_key.fetch(now);
        }
        if (sc == Screen::Bluetooth) {
            btp.open(now);
            bt.set_page_open(true, now);
        }
    };
    flow.page_input = [&](Screen sc, PadAction a, bool rep) {
        int64_t now = now_ms();
        if (sc == Screen::Settings)
            return settings_page.action(a, rep, now);
        if (btp.action(a, rep, now) != BtPage::Act::Pop) {
            need_redraw = true;
            return false;
        }
        bt.set_page_open(false, now);
        btp.close();
        return true;
    };
    flow.rearm = [&](int64_t now) { repeat.rearm(now); };
    flow.last_frame = [&](const Surface& s) { copy_surface(p.draw_target(), s); };
    flow.ctl.can_animate = can_animate;
    flow.ctl.clock_us = now_us;
    flow.ctl.landed = [&](const char* what, const char* why) {
        std::fprintf(stderr, "blockie-frontend: %s landed (%s)\n", what, why);
        if (std::strcmp(what, "expand") == 0 || std::strcmp(what, "collapse") == 0 || std::strcmp(what, "push") == 0 ||
            std::strcmp(what, "pop") == 0 || std::strcmp(what, "startup") == 0)
            fstats.end(why);
    };
    auto apply = [&](PadAction a, bool rep) {
        bool changed = flow.input(a, rep, now_ms());
        bool started = flow.ctl.started() && flow.ctl.active();
        if (changed && !started)
            need_redraw = true;
        if (started) {
            if (flow.ctl.reversed())
                fstats.end("reversed");
            fstats.begin(fold_kind_name(flow.ctl.kind()));
            fstats.from_us = flow.ctl.from_us();
            fstats.to(flow.ctl.to_us());
        }
    };
    theme_set.on_value = [&](bool light) {
        settings_page.set_light(light);
        if (theme().light != light) {
            set_theme_light(light);
            flow.theme_changed();
        }
        need_redraw = true;
    };
    theme_set.on_fail = [&]() {
        settings_page.set_light(theme().light);
        need_redraw = true;
    };
    settings_page.on_theme = [&](bool light) { theme_set.set(light, now_ms()); };
    zone_key.on_value = [&](const std::string& v) {
        settings_page.set_zone(v);
        if (nav.top() == Screen::Settings)
            need_redraw = true;
    };
    zone_key.on_fail = [&]() {
        sync_zone();
        if (nav.top() == Screen::Settings)
            need_redraw = true;
    };
    zone_key.on_missing = [&]() {
        settings_page.set_zone_missing(now_ms());
        if (nav.top() == Screen::Settings)
            need_redraw = true;
    };
    settings_page.on_zone = [&](const std::string& id) { zone_key.set(id, now_ms()); };
    auto press = [&](PadAction a, uint8_t src, int64_t now) {
        apply(a, false);
        if (is_direction(a) && child.pid <= 0)
            repeat.press(a, src, now);
    };

    p.set_key_handler([&](uint32_t sym, bool pressed) {
        bool have = true;
        PadAction a = PadAction::Up;
#ifdef BLOCKIE_HAVE_XKB
        if (sym == XKB_KEY_Up) a = PadAction::Up;
        else if (sym == XKB_KEY_Down) a = PadAction::Down;
        else if (sym == XKB_KEY_Left) a = PadAction::Left;
        else if (sym == XKB_KEY_Right) a = PadAction::Right;
        else if (sym == XKB_KEY_Return) a = PadAction::Accept;
        else if (sym == XKB_KEY_Escape) a = PadAction::Back;
        else if (sym == XKB_KEY_x) a = PadAction::Context;
        else have = false;
#else
        if (sym == KEY_UP) a = PadAction::Up;
        else if (sym == KEY_DOWN) a = PadAction::Down;
        else if (sym == KEY_LEFT) a = PadAction::Left;
        else if (sym == KEY_RIGHT) a = PadAction::Right;
        else if (sym == KEY_ENTER) a = PadAction::Accept;
        else if (sym == KEY_ESC) a = PadAction::Back;
        else if (sym == KEY_X) a = PadAction::Context;
        else have = false;
#endif
        if (!have)
            return;
        int64_t now = now_ms();
        if (!pressed) {
            repeat.release(a, kFromKeys, now);
            return;
        }
        std::fprintf(stderr, "blockie-frontend: key %s\n", action_name(a));
        press(a, kFromKeys, now);
    });
    p.set_leave_handler([&]() { repeat.release_source(kFromKeys, now_ms()); });

    int sfd = -1;
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGCHLD);
    if (sigprocmask(SIG_BLOCK, &mask, nullptr) == 0) {
        sfd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
        if (sfd < 0)
            sigprocmask(SIG_UNBLOCK, &mask, nullptr);
    }

    std::fprintf(stderr, "blockie-frontend: scrape worklist %d\n", recent_jobs);
    std::thread scrape_thread;
    if (art_evfd < 0 || work_evfd < 0)
        std::fprintf(stderr, "blockie-frontend: eventfd failed, art scrape and covers disabled\n");
    else {
        scrape_thread = std::thread([data_root, art_evfd, work_evfd, &squeue, &qmutex, &art_mutex, &art_drops,
                                     &scrape_stop, &scrape_pause, &scrape_busy]() {
            Library wlib;
            if (!wlib.open(data_root)) {
                std::fprintf(stderr, "blockie-frontend: scrape db open failed\n");
                return;
            }
            Scraper sc(wlib, data_root, {}, &scrape_stop);
            for (;;) {
                struct pollfd wf;
                wf.fd = work_evfd;
                wf.events = POLLIN;
                wf.revents = 0;
                int pr = poll(&wf, 1, -1);
                if (pr < 0) {
                    if (errno == EINTR)
                        continue;
                    break;
                }
                uint64_t cnt;
                while (read(work_evfd, &cnt, sizeof cnt) == (ssize_t)sizeof cnt) {
                }
                if (scrape_stop.load())
                    break;
                for (;;) {
                    if (scrape_pause.load())
                        break;
                    ScrapeJob j;
                    {
                        std::lock_guard<std::mutex> lk(qmutex);
                        if (squeue.empty() || scrape_pause.load())
                            break;
                        j = std::move(squeue.front());
                        squeue.pop_front();
                        scrape_busy.store(true);
                    }
                    if (scrape_stop.load())
                        return;
                    std::string hit = sc.record_local(j.game, ArtType::Covers);
                    ArtOutcome o{true, false, false, hit};
                    if (hit.empty()) {
                        std::string eff = sc.resolve_name(j.game.system_id, j.game.rompath_abs);
                        if (eff.empty())
                            eff = j.game.name;
                        if (scrape_stop.load())
                            return;
                        o = sc.art(j.game.id, j.game.system_id, j.libretro_folder, eff, ArtType::Covers);
                    }
                    const char* r = o.cached ? "ok" : o.not_found ? "404" : o.transient ? "err" : "miss";
                    std::fprintf(stderr, "blockie-frontend: scrape %s \"%s\" %s %s\n", j.game.system_id.c_str(),
                                 j.game.name.c_str(), r, o.path.c_str());
                    if (o.cached && !o.path.empty()) {
                        {
                            std::lock_guard<std::mutex> lk(art_mutex);
                            art_drops.push_back({j.game.id, o.path});
                        }
                        uint64_t one = 1;
                        ssize_t w = write(art_evfd, &one, sizeof one);
                        (void)w;
                    }
                    scrape_busy.store(false);
                }
            }
            std::fprintf(stderr, "blockie-frontend: scrape worker exit\n");
        });
    }
    std::thread decode_thread;
    if (art_evfd >= 0) {
        decode_thread = std::thread([art_evfd, &dq, &dmutex, &dcv, &decode_stop, &decode_pause, &art_mutex,
                                     &art_drops]() {
            std::unique_lock<std::mutex> lk(dmutex);
            for (;;) {
                dcv.wait(lk, [&]() { return decode_stop || (!decode_pause && dq.size() > 0); });
                if (decode_stop)
                    break;
                DecodeJob j;
                if (!dq.pop(j))
                    continue;
                lk.unlock();
                int64_t t0 = now_us();
                DecodedImage dec;
                bool ok = load_png(j.path, dec) && dec.ok();
                int64_t t1 = now_us();
                size_t n = ok ? std::max<size_t>(j.sides.size(), 1) : 1;
                for (size_t s = 0; s < n; s++) {
                    int side = s < j.sides.size() ? j.sides[s] : kTileSide;
                    Art img;
                    if (ok)
                        img = std::make_shared<const Image>(cover_tile(dec, side));
                    int64_t t2 = now_us();
                    if (s == 0)
                        std::fprintf(stderr,
                                     "blockie-frontend: decode %lld %s %dx%d wait %lld png %.1f tile %.1f ms\n",
                                     (long long)j.game_id, ok ? "ok" : "fail", dec.w, dec.h,
                                     (long long)(t0 / 1000 - j.queued_ms), (t1 - t0) / 1000.0, (t2 - t1) / 1000.0);
                    {
                        std::lock_guard<std::mutex> ak(art_mutex);
                        art_drops.push_back({j.game_id, std::string(), img, true, side, s + 1 == n});
                    }
                    uint64_t one = 1;
                    ssize_t w = write(art_evfd, &one, sizeof one);
                    (void)w;
                }
                lk.lock();
            }
        });
    }

    if (decode_thread.joinable()) {
        int64_t t0 = now_ms();
        int64_t until = t0 + kCoverWaitMs;
        for (;;) {
            take_drops();
            std::vector<ArtDrop> drops;
            drops.swap(pending_drops);
            for (const ArtDrop& drp : drops)
                apply_drop(drp);
            bool waiting = false;
            {
                std::lock_guard<std::mutex> lk(dmutex);
                for (size_t k = 0; k < home.recent.size() && k < model.tiles.size(); k++)
                    if (!model.tiles[k] && dq.pending(home.recent[k].id))
                        waiting = true;
            }
            int64_t left = until - now_ms();
            if (!waiting || left <= 0)
                break;
            struct pollfd af;
            af.fd = art_evfd;
            af.events = POLLIN;
            af.revents = 0;
            poll(&af, 1, (int)left);
        }
        std::fprintf(stderr, "blockie-frontend: recent covers waited %lld ms\n", (long long)(now_ms() - t0));
    }
    if (bench) {
        {
            std::lock_guard<std::mutex> lk(qmutex);
            scrape_pause.store(true);
        }
        {
            std::lock_guard<std::mutex> lk(dmutex);
            decode_pause = true;
        }
        int64_t w0 = now_ms();
        bool idle = false;
        for (;;) {
            take_drops();
            std::vector<ArtDrop> drops;
            drops.swap(pending_drops);
            for (const ArtDrop& drp : drops)
                apply_drop(drp);
            {
                std::lock_guard<std::mutex> lk(dmutex);
                idle = dq.busy() == 0 && !scrape_busy.load();
            }
            int64_t left = w0 + kBenchIdleMs - now_ms();
            if (idle || left <= 0)
                break;
            struct pollfd af;
            af.fd = art_evfd;
            af.events = POLLIN;
            af.revents = 0;
            poll(&af, 1, (int)std::min<int64_t>(left, kBenchPollMs));
        }
        bench_grow(flow, fonts, lib, sys_ids, idle, now_ms() - w0);
    } else if (can_animate()) {
        flow.startup(now_ms());
        fstats.begin("t1");
    }
    need_redraw = true;

    wl_display* d = p.display();
    int wl_fd = p.event_fd();
    while (!bench && p.ready()) {
        while (wl_display_prepare_read(d) != 0)
            wl_display_dispatch_pending(d);
        bool wl_blocked = wl_display_flush(d) < 0 && errno == EAGAIN;
        bus.pump();
        bus.tick(now_ms());
        {
            int64_t bnow = now_ms();
            bt.tick(bnow);
            if (bt.serial() != bt_seen) {
                bt_seen = bt.serial();
                btp.set_model(bt.state(), bnow);
                if (nav.top() == Screen::Bluetooth)
                    need_redraw = true;
            }
            std::string sub = bt_subtitle(bt.state(), bnow);
            if (bt_item < (int)model.systems.size() && model.systems[bt_item].subtitle != sub) {
                model.systems[bt_item].subtitle = sub;
                if (nav.top() == Screen::Home) {
                    flow.redraw();
                    need_redraw = true;
                }
            }
            if (nav.top() == Screen::Bluetooth && btp.dirty(bnow))
                need_redraw = true;
        }

        struct pollfd fds[5];
        fds[0].fd = wl_fd;
        fds[0].events = POLLIN | (wl_blocked ? POLLOUT : 0);
        fds[0].revents = 0;
        fds[1].fd = sfd;
        fds[1].events = POLLIN;
        fds[1].revents = 0;
        int nfds = 2;
        int pi = -1;
        if (pad_ok) {
            pi = nfds;
            fds[pi].fd = pad.fd();
            fds[pi].events = POLLIN;
            fds[pi].revents = 0;
            nfds++;
        }
        int ai = -1;
        if (art_evfd >= 0) {
            ai = nfds;
            fds[ai].fd = art_evfd;
            fds[ai].events = POLLIN;
            fds[ai].revents = 0;
            nfds++;
        }
        int bi = -1;
        if (bus.fd() >= 0) {
            bi = nfds;
            fds[bi].fd = bus.fd();
            fds[bi].events = POLLIN | (bus.wants_write() ? POLLOUT : 0);
            fds[bi].revents = 0;
            nfds++;
        }
        int64_t pnow = now_ms();
        int timeout = -1;
        auto until = [&](int64_t when) {
            if (when < 0)
                return;
            int ms = (int)std::max<int64_t>(0, when - pnow);
            timeout = timeout < 0 ? ms : std::min(timeout, ms);
        };
        until(repeat.deadline());
        until(bus.deadline());
        until(bt.deadline());
        until(bt_tick_at(bt.state(), pnow));
        if (nav.top() == Screen::Bluetooth)
            until(btp.deadline(pnow));
        if (frame_req_at >= 0)
            until(frame_req_at + kFrameWatchdogMs);
        bool want_frame = child.pid <= 0 && (need_redraw || (animating(pnow) && !p.frame_pending()));
        if ((want_frame && p.claim()) || bus.data_remains() || (!flow.ctl.active() && !pending_drops.empty()))
            timeout = 0;
        int pr = poll(fds, nfds, timeout);
        if (pr < 0) {
            wl_display_cancel_read(d);
            if (errno == EINTR)
                continue;
            break;
        }
        if (fds[0].revents & (POLLIN | POLLHUP | POLLERR)) {
            if (wl_display_read_events(d) < 0)
                break;
        } else {
            wl_display_cancel_read(d);
        }
        if (bi >= 0 && (fds[bi].revents & (POLLIN | POLLOUT | POLLHUP | POLLERR)))
            bus.pump();
        if (wl_display_dispatch_pending(d) < 0)
            break;

        if (fds[1].revents & POLLIN) {
            bool quit = false;
            struct signalfd_siginfo si;
            while (read(sfd, &si, sizeof si) == (ssize_t)sizeof si) {
                if (si.ssi_signo == SIGCHLD)
                    reap_children();
                else
                    quit = true;
            }
            if (quit)
                break;
        }

        if (pi >= 0 && (fds[pi].revents & (POLLIN | POLLHUP | POLLERR))) {
            std::vector<PadEdge> edges = pad.poll_edges();
            int64_t now = now_ms();
            for (const PadEdge& e : edges) {
                if (!e.down) {
                    repeat.release(e.action, kFromPad, now);
                    continue;
                }
                if (!p.activated())
                    continue;
                std::fprintf(stderr, "blockie-frontend: pad %s\n", action_name(e.action));
                press(e.action, kFromPad, now);
            }
            if (!pad.is_open()) {
                pad_ok = false;
                repeat.release_source(kFromPad, now);
                std::fprintf(stderr, "blockie-frontend: pad disconnected\n");
            }
        }

        if (ai >= 0 && (fds[ai].revents & POLLIN))
            take_drops();
        if (!pending_drops.empty() && (!flow.ctl.active() || drop_budget)) {
            std::vector<ArtDrop> drops;
            if (!flow.ctl.active()) {
                drops.swap(pending_drops);
            } else {
                drop_budget = false;
                drops.push_back(std::move(pending_drops.front()));
                pending_drops.erase(pending_drops.begin());
            }
            for (const ArtDrop& drp : drops)
                apply_drop(drp);
        }

        if (!p.activated() || child.pid > 0)
            repeat.release_all();
        PadAction ra = PadAction::Up;
        int64_t tn = now_ms();
        if (repeat.due(tn, ra)) {
            if (repeat.sources(ra) == kFromPad && pad_ok && pad.held(ra) == Held::No)
                repeat.release(ra, kFromPad, tn);
            else
                apply(ra, true);
        }

        int64_t fnow = now_ms();
        if (p.take_frame_done()) {
            frame_req_at = -1;
            fstats.done(now_us());
        }
        if (!can_animate()) {
            land_all(fnow, child.pid > 0 ? "child" : "hidden");
            settings_page.land();
        }
        if (frame_req_at >= 0 && fnow - frame_req_at > kFrameWatchdogMs) {
            p.cancel_frame();
            frame_req_at = -1;
            land_all(fnow, "watchdog");
            settings_page.land();
            need_redraw = true;
        }
        if (flow.ctl.active() && need_redraw) {
            flow.redraw();
            need_redraw = false;
        }
        bool anim = animating(fnow);
        if ((need_redraw || (anim && !p.frame_pending())) && child.pid <= 0 && p.claim()) {
            if (frame(fnow))
                need_redraw = false;
        }
    }
    if (child.pid > 0)
        kill(child.pid, SIGTERM);
    bt.shutdown(now_ms());
    bus.pump();
    scrape_stop.store(true);
    if (work_evfd >= 0) {
        uint64_t one = 1;
        ssize_t w = write(work_evfd, &one, sizeof one);
        (void)w;
    }
    {
        std::lock_guard<std::mutex> lk(dmutex);
        decode_stop = true;
    }
    dcv.notify_all();
    if (scrape_thread.joinable())
        scrape_thread.join();
    if (decode_thread.joinable())
        decode_thread.join();
    if (work_evfd >= 0)
        close(work_evfd);
    if (art_evfd >= 0)
        close(art_evfd);
    if (sfd >= 0)
        close(sfd);
    p.stop();
    std::fprintf(stderr, "blockie-frontend: exiting\n");
    return 0;
}

}

int main() { return fd::run(); }
