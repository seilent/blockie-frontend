#include <algorithm>
#include <array>
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
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../core/curves.h"
#include "../gfx/glyph.h"
#include "../gfx/pixel.h"
#include "../gfx/wayland.h"
#include "../ui/roles.h"
#include "backlight.h"
#include "bench.h"
#include "bt.h"
#include "bus.h"
#include "children.h"
#include "decodeq.h"
#include "frame_gate.h"
#include "framestats.h"
#include "homeflow.h"
#include "image.h"
#include "ink.h"
#include "input_pad.h"
#include "jpeg.h"
#include "launch.h"
#include "launchflow.h"
#include "library.h"
#include "nav.h"
#include "pagerows.h"
#include "ports.h"
#include "ready.h"
#include "repeat.h"
#include "screen_bt.h"
#include "scraper.h"
#include "screen_selector.h"
#include "screen_settings.h"
#include "screen_store.h"
#include "setd_key.h"
#include "store.h"
#include "store_flow.h"
#include "sysinfo.h"
#include "theme.h"
#include "theme_setting.h"
#include "tile_grid.h"
#include "toast_strip.h"
#include "volume.h"
#include "wifi.h"
#include "wl_keys.h"
#include "zones.h"

#ifndef BLOCKIE_FE_STEAM_FILES
#define BLOCKIE_FE_STEAM_FILES 0
#endif

namespace fd {

namespace {

using Art = std::shared_ptr<const Image>;

constexpr bool kSteamFiles = BLOCKIE_FE_STEAM_FILES != 0;

constexpr int kThemeWaitMs = 300;
constexpr int kPrepareRows = 16;
constexpr int64_t kPrepareUs = 2000;
constexpr int kBenchIdleMs = 5000;
constexpr int kBenchPollMs = 50;
constexpr const char* kZoneKey = "system.timezone";
constexpr const char* kQuitStamp = "/blockie-game.quit";

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

int64_t thread_cpu_us()
{
    struct timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

int64_t now_us()
{
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

bool quit_stamped()
{
    const char* run = std::getenv("XDG_RUNTIME_DIR");
    return run && *run && access((std::string(run) + kQuitStamp).c_str(), F_OK) == 0;
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
    case PadAction::Prev: return "prev";
    case PadAction::Next: return "next";
    case PadAction::Start: return "start";
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
    int64_t index_gen = 0;
};

}

static int run()
{
    std::fprintf(stderr, "blockie-frontend: started\n");
    std::fprintf(stderr, "blockie-frontend: commit %s\n", *frontend_commit() ? frontend_commit() : "unknown");
    std::fprintf(stderr, "blockie-frontend: %s\n", frontend_steam_files());

    Bus bus;
    ThemeSetting theme_set;
    theme_set.on_value = [](bool light) { set_theme_light(light); };
    theme_set.attach(bus);
    SetdKey zone_key(kZoneKey);
    zone_key.attach(bus);
    std::vector<SettingsRowSpec> row_specs = settings_rows();
    std::vector<std::unique_ptr<SetdKey>> row_keys;
    for (const SettingsRowSpec& r : row_specs)
        if (r.setd) {
            row_keys.push_back(std::make_unique<SetdKey>(r.id, r.type));
            row_keys.back()->attach(bus);
        }
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
    WlKeys wl_keys;
    std::vector<PadEdge> key_edges;
    int leave_at = -1;
    WaylandPresenter p;
    p.set_app_id("net.seilent.Blockie.Frontend");
    p.set_title("Blockie");
    p.set_output(kPortraitW, kPortraitH);
    p.set_keyboard(true);
    p.set_keymap_handler([&](int fd, uint32_t size) { wl_keys.keymap(fd, size); });
    p.set_key_handler([&](uint32_t code, bool pressed) {
        PadAction a = PadAction::Up;
        if (wl_keys.action(code, a))
            key_edges.push_back({a, pressed});
    });
    p.set_focus_handler([&](bool in) {
        if (!in)
            leave_at = (int)key_edges.size();
    });
    const char* cenv = std::getenv("BLOCKIE_PRESENT_COPY");
    if (!p.open(cenv && std::strcmp(cenv, "1") == 0 ? "copy" : "direct", true)) {
        std::fprintf(stderr, "blockie-frontend: no wayland display, exiting\n");
        return 1;
    }
    Bt bt(bus);
    bt.start();
    Wifi wifi(bus);
    wifi.start();
    uint32_t wifi_seen = 0, wifi_note = 0;
    Backlight backlight(bus);
    std::vector<WlTouch> touch_scratch;
    auto wl_wait = [&](int ms) {
        struct pollfd wf;
        wf.fd = p.display_fd();
        wf.events = POLLIN;
        wf.revents = 0;
        if (poll(&wf, 1, ms) > 0 && gate_pump(wf.revents))
            return pump_presenter(p, touch_scratch);
        return true;
    };
    fill_when_ready(p, now_ms, wl_wait, [&]() {
        int64_t ft = now_ms();
        p.begin_frame(ft, p.show_at(ft));
        p.begin_full();
        WaylandPresenter::S s = p.draw_target();
        for (int y = 0; y < kPortraitH; y++)
            fill_px(s.row(y), theme().bg, kPortraitW);
        p.present(kScreen);
    });

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
    fopts.lang = true;
    const char* eenv = std::getenv("BLOCKIE_ENGINE");
    fopts.fast = !(eenv && std::strcmp(eenv, "pixel") == 0);
    const char* venv = std::getenv("BLOCKIE_ENGINE_VERIFY");
    fopts.verify = venv && std::strcmp(venv, "1") == 0;
    HomeFlow flow(fonts, fopts);
    SelectorModel& model = flow.model;
    std::vector<std::string> sys_ids;
    std::vector<MenuKind>& kinds = flow.kinds;
    if (lib.open(data_root)) {
        int n = lib.scan();
        std::fprintf(stderr, "blockie-frontend: %d games\n", n);
        Scraper local(lib, data_root);
        std::fprintf(stderr, "blockie-frontend: local covers recorded %d\n", local.sweep_local(ArtType::Covers));
        for (const auto& sw : lib.systems_with_games(kPortsSystem)) {
            model.systems.push_back({sw.first.name, sw.second, ""});
            sys_ids.push_back(sw.first.id);
            kinds.push_back(MenuKind::System);
        }
    }

    add_menu_items(model, kinds);

    Launcher launcher(data_root);
    Children kids;
    ReadyReport ready_report(kids, now_ms);
    Volume volume(kids);
    BatteryFeed battery;
    Battery battery_now;

    PadReader pad;
    bool pad_ok = pad.open();
    std::fprintf(stderr, pad_ok ? "blockie-frontend: pad fd %d\n" : "blockie-frontend: no pad\n", pad.fd());

    bool need_redraw = false;
    bool posted_last = false;
    int64_t glow_done_us = -1, landed_us = -1;
    bool more_last = false;
    bool drew_last = false;
    int glide_at[2] = {};
    uint32_t redraw_why = 0;
    auto want_redraw = [&](char c) {
        need_redraw = true;
        redraw_why |= 1u << (c - 'a');
    };
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
    FrameGate gate;
    int64_t exit_at = -1;
    bool drop_budget = false;
    std::unordered_map<int64_t, Game> by_id;
    std::unordered_map<int64_t, Art> held;
    Launcher::Child child;
    PortsHelper ports;
    int64_t ports_gen = 0;
    std::string ports_dir = data_root + "/roms/" + kPortsSystem;
    PortPage portp(fonts, glyphs);
    StoreFlow sf(flow, portp, ports, kids);
    bool was_online = false;
    std::unique_ptr<Catalog> cat_fresh;
    bool cat_reading = false, cat_again = false;
    std::mutex cat_mutex;
    std::thread cat_thread;
    int cat_evfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    int64_t shot_id = -1;
    std::string shot_path;
    Art shot_art;
    int64_t child_game_id = -1;
    std::string child_core;
    LaunchFlow launch;
    ToastStrip toast;
    bool was_active = p.activated();
    const char* lenv = std::getenv("BLOCKIE_LAUNCH");
    bool launch_ab = lenv && std::strcmp(lenv, "ab") == 0;
    bool arm_b = false;

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
    const char* probe_env = std::getenv("BLOCKIE_PROBE");
    bool probe = fstats.on && probe_env && std::strcmp(probe_env, "1") == 0;
    const char* benv = std::getenv("BLOCKIE_BENCH");
    bool bench = benv && std::strcmp(benv, "grow") == 0;

    auto enqueue_scrape = [&](const Game& g, int64_t epoch, const std::string& folder) {
        if (g.system_id == kPortsSystem)
            return;
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
    auto queue_decode = [&](int64_t id, const std::string& path, int64_t gen, int64_t index_gen,
                            int side = kTileSide) {
        bool added;
        {
            std::lock_guard<std::mutex> lk(dmutex);
            added = dq.push({id, path, gen, now_ms(), {side}, index_gen});
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
                queue_decode(g.id, m.path, gen, g.system_id == kPortsSystem ? ports_gen : 0);
                return true;
            }
        return false;
    };
    auto store_cover = [&](int64_t id, int64_t gen) {
        const StoreItem* it = sf.item(id);
        if (!it || failed_covers.count(id))
            return;
        const std::string& path = !it->cover.empty() ? it->cover : it->shot;
        if (!path.empty())
            queue_decode(id, path, gen, sf.catalog.gen);
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
        return HomeGame{g.id, g.title.empty() ? g.name : g.title, sc ? sc->name : std::string()};
    };
    flow.setup();
    home.load = [&](const HomeGame& g) -> Art {
        auto it = held.find(g.id);
        return it != held.end() ? it->second : Art();
    };
    auto deal_recent = [&](bool first, bool keep = false) {
        if (flow.nav.store)
            return 0;
        std::vector<Art>& grid = home.system_view ? home.recent_hold : model.tiles;
        held.clear();
        for (size_t k = 0; k < home.recent.size() && k < grid.size(); k++)
            if (grid[k])
                held[home.recent[k].id] = grid[k];
        std::vector<HomeGame> list;
        for (const Game& g : lib.recent_games(home.recent_cap))
            list.push_back(home_game(g));
        std::vector<HomeGame> want = first  ? home.deal_recent(model, list)
                                     : keep ? flow.recent_changed(list)
                                            : flow.game_ran(list);
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
        if (sc == Screen::Port)
            portp.draw(c, ink, now);
        else if (sc == Screen::SteamLogin)
            portp.draw_login(c, ink, now);
        else if (sc == Screen::Bluetooth)
            btp.draw(c, ink, now);
        else if (sc == Screen::Settings)
            draw_settings(c, fonts, ink, settings_page.view(now), theme());
    };
    auto page_animating = [&](int64_t now) {
        if (nav.top() == Screen::Settings && settings_page.animating(now))
            return true;
        if (nav.top() == Screen::Port && portp.animating(now))
            return true;
        if (nav.top() == Screen::SteamLogin && portp.login_animating(now))
            return true;
        return nav.top() == Screen::Bluetooth && btp.animating(now);
    };
    auto animating = [&](int64_t now) { return flow.animating(now) || page_animating(now) || toast.animating(now); };
    auto note_return = [&](bool ok) {
        if (ok && exit_at >= 0) {
            std::fprintf(stderr, "blockie-frontend: frame after return %lld ms\n", (long long)(now_ms() - exit_at));
            exit_at = -1;
        }
    };
    struct EngSnap {
        eng::Instance::Times pane, page;
        Glow::Times grid;
        int seeds = 0, trusts = 0;
    };
    auto eng_snap = [&]() {
        EngSnap s;
        for (const eng::Instance* in : {flow.pane(), flow.page_engine()})
            if (in) {
                (in == flow.pane() ? s.pane : s.page) = in->times();
                s.seeds += in->seed_frames();
                s.trusts += in->trusted_frames();
            }
        s.grid = flow.glow().times;
        return s;
    };
    auto eng_parts = [](const EngSnap& a, const EngSnap& b, int forward) {
        auto ms = [](int64_t us) {
            char t[24];
            std::snprintf(t, sizeof t, "%.2f", us / 1000.0);
            return std::string(t);
        };
        auto letters = [](uint32_t m) {
            std::string s;
            for (int i = 0; i < 26; i++)
                if (m & (1u << i))
                    s += (char)('a' + i);
            return s.empty() ? std::string("-") : s;
        };
        auto d = [](int64_t x, int64_t y) { return y - x; };
        int64_t begin = d(a.pane.begin, b.pane.begin) + d(a.page.begin, b.page.begin);
        int64_t build = d(a.pane.build, b.pane.build) + d(a.page.build, b.page.build);
        int64_t start = d(a.pane.start, b.pane.start) + d(a.page.start, b.page.start);
        int64_t pass = d(a.pane.pass, b.pane.pass) + d(a.page.pass, b.page.pass);
        bool pane_start = b.pane.starts > a.pane.starts, page_start = b.page.starts > a.page.starts;
        bool pane_pass = b.pane.passes > a.pane.passes, page_pass = b.page.passes > a.page.passes;
        uint32_t start_why = pane_start ? b.pane.start_why : page_start ? b.page.start_why : 0;
        uint32_t trust_why = pane_pass ? b.pane.trust_why : page_pass ? b.page.trust_why : 0;
        int64_t greset = d(a.grid.reset, b.grid.reset), gseed = d(a.grid.seed, b.grid.seed);
        int64_t gframe = d(a.grid.frame, b.grid.frame);
        std::string s = "begin " + ms(begin) + " build " + ms(build) + " start ";
        if (pane_start || page_start)
            s += ms(start) + (b.seeds > a.seeds ? " m" : " d:" + letters(start_why));
        else
            s += "- -";
        s += " pass ";
        if (pane_pass || page_pass)
            s += ms(pass) + (b.trusts > a.trusts ? " t" : " u:" + letters(trust_why));
        else
            s += "- -";
        s += " greset " + ms(greset) + " gseed " + ms(gseed) +
             (b.grid.seed_marks > a.grid.seed_marks ? " m" : gseed > 0 ? " d" : " -");
        s += " gframe " + ms(gframe) + " other ";
        s += forward < 0 ? std::string("-") : ms(forward - (begin + build + start + pass + greset + gseed + gframe));
        s += " l4 ";
        s += b.grid.guard_seeds > a.grid.guard_seeds ? (b.grid.guard_pass > a.grid.guard_pass ? "p" : "f") : "-";
        int64_t sm = d(a.pane.start_marks, b.pane.start_marks) + d(a.page.start_marks, b.page.start_marks);
        int64_t sf = d(a.pane.start_fills, b.pane.start_fills) + d(a.page.start_fills, b.page.start_fills);
        int64_t sb = d(a.pane.start_build, b.pane.start_build) + d(a.page.start_build, b.page.start_build);
        bool s1 = b.pane.s1_starts > a.pane.s1_starts || b.page.s1_starts > a.page.s1_starts;
        if (pane_start || page_start)
            s += " sm " + ms(sm) + " sf " + ms(sf) + " sb " + ms(sb) + (s1 ? " s1 1" : " s1 0");
        else
            s += " sm - sf - sb - s1 -";
        if (gseed > 0)
            s += " sw " + letters(b.grid.seed_why) + " sp " + std::to_string(d(a.grid.seed_past, b.grid.seed_past)) +
                 " sr " + ms(d(a.grid.seed_raster, b.grid.seed_raster)) + " sg " +
                 ms(d(a.grid.seed_glow, b.grid.seed_glow));
        else
            s += " sw - sp - sr - sg -";
        s += " gr " + ms(d(a.grid.frame_raster, b.grid.frame_raster)) + " gg " +
             ms(d(a.grid.frame_glow, b.grid.frame_glow)) + " gb " +
             std::to_string(b.grid.runs_builds - a.grid.runs_builds);
        return s;
    };
    auto frame = [&](int64_t now) -> bool {
        int64_t t0 = now_us();
        p.begin_frame(now, p.show_at(now));
        p.begin_full();
        Surface dst = p.draw_target();
        fonts.begin_frame(-1);
        bool timed = fstats.on;
        int64_t u0 = timed ? now_us() : 0;
        toast.under(dst);
        int toast_us = timed ? (int)(now_us() - u0) : 0;
        bool folding = flow.ctl.active();
        size_t nth = fstats.work_us.size();
        bool parts = timed && folding && fstats.kind && nth < 2;
        EngSnap before = parts ? eng_snap() : EngSnap{};
        int gb0 = flow.glow().times.runs_builds;
        auto structs = [&]() {
            return (flow.pane() ? flow.pane()->adopt_struct() : 0) +
                   (flow.page_engine() ? flow.page_engine()->adopt_struct() : 0);
        };
        int st0 = structs();
        const eng::Instance* pe = flow.page_engine();
        int64_t sc0 = pe ? pe->times().start : 0, sw0 = pe ? pe->times().start_warm : 0;
        bool more = flow.frame(now, dst);
        int64_t warm = pe ? pe->times().start_warm - sw0 : 0;
        fstats.build(flow.glow().times.runs_builds - gb0);
        std::string part = parts ? eng_parts(before, eng_snap(), flow.ctl.forward_us() - (int)warm) : std::string();
        if (parts && nth == 0) {
            part += std::string(" l5 ") + (structs() > st0 ? 'i' : flow.ctl.l5());
            int64_t t_us = now_us();
            char extra[160];
            std::snprintf(extra, sizeof extra, " l2 %c %.0f rest %.0f", flow.glow().l2,
                          glow_done_us < 0 ? -1.0 : (t_us - glow_done_us) / 1000.0,
                          landed_us < 0 ? -1.0 : (t_us - landed_us) / 1000.0);
            part += extra;
            bool page = flow.ctl.kind() == FoldKind::Push || flow.ctl.kind() == FoldKind::Pop;
            part += std::string(" ps ") + (page && pe ? pe->ps() : '-');
            if (pe && pe->times().start_warm > sw0) {
                std::snprintf(extra, sizeof extra, " start cold %.2f warm %.2f", (pe->times().start - sc0) / 1000.0,
                              (pe->times().start_warm - sw0) / 1000.0);
                part += extra;
            }
        }
        more = more || page_animating(now);
        bool at_rest = !more;
        int64_t d0 = timed ? now_us() : 0;
        toast.draw(fonts, ink, dst, now);
        if (timed)
            toast_us += (int)(now_us() - d0);
        more = more || toast.animating(now);
        int64_t p0 = timed ? now_us() : 0;
        p.present(kScreen);
        p.flip();
        int present_us = timed ? (int)(now_us() - p0) : 0;
        bool ok = p.posted();
        posted_last = ok;
        if (ok) {
            more_last = !at_rest;
            glide_at[0] = flow.scroll_glide().at(now);
            glide_at[1] = flow.bar_glide().at(now);
        }
        if (ok)
            ready_report.posted(pad_ok, p.activated());
        if (ok && folding) {
            fstats.frame(t0 + warm, now_us(), flow.ctl.forward_us() - (int)warm, flow.ctl.render_us(), toast_us,
                         present_us);
            if (parts && nth == 0) {
                int khz = 0, cpu = sched_getcpu();
                char path[96];
                std::snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", cpu);
                if (FILE* f = cpu >= 0 ? std::fopen(path, "r") : nullptr) {
                    if (std::fscanf(f, "%d", &khz) != 1)
                        khz = 0;
                    std::fclose(f);
                }
                part += " mhz " + std::to_string(khz / 1000) + " cpu " + std::to_string(cpu);
            }
            if (parts)
                fstats.part(nth, part);
        }
        gate.posted(ok, more, now);
        if (ok && folding)
            drop_budget = true;
        note_return(ok);
        return ok;
    };
    auto black_frame = [&](int64_t now) {
        posted_last = false;
        p.begin_frame(now, p.show_at(now));
        p.begin_full();
        Surface dst = p.draw_target();
        Canvas c(dst);
        c.fill(dst.rect(), launch_ground());
        p.present(kScreen);
        p.flip();
        bool ok = p.posted();
        if (ok)
            launch.black_shown();
        note_return(ok);
        std::fprintf(stderr, "blockie-frontend: black frame in %s posted %d at %lld us\n",
                     launch_phase_name(launch.phase()), ok ? 1 : 0, (long long)now_us());
    };
    auto can_animate = [&]() { return launch.can_animate(p.activated(), child.pid > 0); };
    auto gate_in = [&](int64_t now, int timeout) {
        GateIn in;
        in.now = now;
        in.ready = p.ready();
        in.claim = p.claim();
        in.activated = p.activated();
        in.child = child.pid > 0;
        in.need_redraw = need_redraw;
        in.animating = animating(now);
        in.timeout = timeout;
        return launch_gate(launch, in, child.pid > 0);
    };
    auto land_all = [&](int64_t now, const char* why) {
        if (!flow.animating(now))
            return;
        flow.land_all(now, why);
        want_redraw('o');
    };

    auto refuse_with = [&](const LaunchOutcome& o) {
        toast.show(now_ms(), o.line1, o.line2, nav.top() == Screen::Home);
        want_redraw('o');
        return LaunchStart::Refused;
    };
    auto refuse = [&](bool core_absent, const std::string& core) {
        return refuse_with(launch_refused(core_absent, core));
    };
    auto try_launch = [&](const Game& g) {
        if (child.pid > 0)
            return LaunchStart::Refused;
        const SystemCfg* sc = system_cfg(g.system_id);
        std::string core = sc ? sc->core : std::string();
        std::string sysname = sc ? sc->name : std::string();
        for (char& ch : sysname)
            ch = (char)std::tolower((unsigned char)ch);
        LaunchCheck gate_check = launcher.check(g.system_id, core, ports.op);
        if (gate_check == LaunchCheck::HelperBusy)
            return refuse_with(launch_helper_busy(ports.op));
        if (gate_check == LaunchCheck::CoreAbsent) {
            std::fprintf(stderr, "blockie-frontend: core %s absent for %s\n", core.c_str(), g.system_id.c_str());
            return refuse(true, core);
        }
        LaunchSpec spec;
        spec.system = sysname;
        spec.rompath_abs = g.rompath_abs;
        spec.core = core;
        spec.theme = theme().light ? "light" : "dark";
        Launcher::Child ch = launcher.spawn(spec);
        if (ch.pid < 0) {
            std::fprintf(stderr, "blockie-frontend: spawn failed for %s\n", g.system_id.c_str());
            return refuse(false, core);
        }
        bool leave = !launch_ab || arm_b;
        if (launch_ab)
            arm_b = !arm_b;
        child = ch;
        child_game_id = g.id;
        child_core = core;
        launch.start(leave, p.activated(), g.system_id == kPortsSystem);
        toast.hide();
        repeat.release_all();
        bt.set_game_running(true, now_ms());
        scrape_pause.store(true);
        {
            std::lock_guard<std::mutex> lk(dmutex);
            decode_pause = true;
        }
        std::fprintf(stderr, "blockie-frontend: launch %s pid %d arm %c press at %lld us ready %d\n", sysname.c_str(),
                     (int)ch.pid, leave ? 'B' : 'A', (long long)now_us(), p.ready() ? 1 : 0);
        return leave ? LaunchStart::Leave : LaunchStart::Cut;
    };

    auto reap_children = [&]() {
        int status = 0;
        pid_t r;
        while ((r = waitpid(-1, &status, WNOHANG)) > 0) {
            if (kids.reaped(r, status))
                continue;
            if (child.pid <= 0 || r != child.pid)
                continue;
            int64_t now = now_ms();
            LaunchPhase at = launch.phase();
            LaunchOutcome out = launch.reap(status, quit_stamped(), child_core, now, now + kCoverWaitMs);
            if (!child.cfg_path.empty())
                unlink(child.cfg_path.c_str());
            std::fprintf(stderr, "blockie-frontend: game exited (%s) in %s at %lld us ready %d\n",
                         out.played ? "played" : "not played", launch_phase_name(at), (long long)now_us(),
                         p.ready() ? 1 : 0);
            exit_at = now_ms();
            child = {};
            child_game_id = -1;
            child_core.clear();
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
            want_redraw('p');
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
        if (want && drp.decoded) {
            std::lock_guard<std::mutex> lk(dmutex);
            want = dq.current(drp.game_id, drp.path, drp.index_gen);
        }
        if (!drp.decoded)
            failed_covers.erase(drp.game_id);
        if (!want)
            return;
        bool in_recent = false;
        for (const HomeGame& g : home.recent)
            if (g.id == drp.game_id)
                in_recent = true;
        if (drp.decoded && drp.game_id >= kStoreShotBase) {
            if (drp.game_id != shot_id)
                return;
            shot_art = drp.img;
            if (nav.top() == Screen::Port || nav.top() == Screen::SteamLogin)
                portp.set_art(shot_art);
            want_redraw('a');
            return;
        }
        if (drp.decoded) {
            if (drp.img) {
                if (flow.art(drp.game_id, drp.side, home.epoch, drp.img) && !flow.ctl.active() && child.pid <= 0)
                    want_redraw('a');
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
        queue_decode(drp.game_id, drp.path, in_recent ? kRecentGen : win_gen, 0);
    };
    flow.games = [&](int sys) {
        std::vector<HomeGame> list;
        if (flow.nav.store)
            return sf.games(sys);
        if (sys < 0 || sys >= (int)sys_ids.size())
            return list;
        for (const Game& g : lib.games(sys_ids[sys]))
            if (g.present || g.system_id != kPortsSystem)
                list.push_back(home_game(g));
        return list;
    };
    flow.covers_wanted = [&](const std::vector<HomeGame>& want, int scroll) {
        {
            std::lock_guard<std::mutex> lk(dmutex);
            dq.set_window(++win_gen, home.keep_set(scroll));
        }
        for (const HomeGame& hg : want) {
            if (hg.id >= kStoreIdBase) {
                store_cover(hg.id, win_gen);
                continue;
            }
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
    auto rescan_ports = [&]() {
        lib.rescan(kPortsSystem);
        ports_gen++;
        int sys = -1;
        for (int i = 0; i < (int)sys_ids.size(); i++)
            if (sys_ids[i] == kPortsSystem)
                sys = i;
        for (const Game& g : lib.games(kPortsSystem))
            failed_covers.erase(g.id);
        if (sys < 0)
            return;
        int count = 0;
        for (const auto& sw : lib.systems_with_games(kPortsSystem))
            if (sw.first.id == kPortsSystem)
                count = sw.second;
        flow.home_model().systems[(size_t)sys].games = count;
        if (flow.nav.store)
            return;
        std::vector<int64_t> gone = flow.regame(sys, flow.games(sys));
        {
            std::lock_guard<std::mutex> lk(dmutex);
            dq.forget(std::unordered_set<int64_t>(gone.begin(), gone.end()));
        }
        if (nav.system_view && nav.focus == sys)
            for (int64_t id : home.keep_set(nav.sys_scroll))
                for (const auto& m : lib.media(id))
                    if (m.type == "covers")
                        queue_decode(id, m.path, win_gen, ports_gen);
        deal_recent(false, true);
        want_redraw('l');
    };
    auto online = [&]() { return wifi.state().dev_state == kNmDeviceActivated; };
    auto want_shot = [&](const StoreItem* it) {
        int64_t id = it ? it->id + kStoreIdBase : -1;
        const std::string path = !it ? std::string() : !it->shot.empty() ? it->shot : it->cover;
        if (id == shot_id && path == shot_path) {
            if (shot_art && (nav.top() == Screen::Port || nav.top() == Screen::SteamLogin))
                portp.set_art(shot_art);
            return;
        }
        {
            std::lock_guard<std::mutex> lk(dmutex);
            if (shot_id >= 0)
                dq.forget({shot_id});
            std::unordered_set<int64_t> keep =
                nav.system_view ? home.keep_set(nav.sys_scroll) : std::unordered_set<int64_t>();
            if (id >= 0)
                keep.insert(id);
            dq.set_window(win_gen, std::move(keep));
        }
        if (id != shot_id || path.empty()) {
            shot_art.reset();
            if (nav.top() == Screen::Port || nav.top() == Screen::SteamLogin)
                portp.set_art(nullptr);
        }
        shot_id = id;
        shot_path = path;
        if (id >= 0 && !path.empty())
            queue_decode(id, path, win_gen, sf.catalog.gen, kStoreShotSide);
    };
    auto read_catalog_async = [&]() {
        if (cat_evfd < 0)
            return;
        if (cat_reading) {
            cat_again = true;
            return;
        }
        cat_reading = true;
        if (cat_thread.joinable())
            cat_thread.join();
        cat_thread = std::thread([&, path = ports_dir + kCatalogDb]() {
            auto c = std::make_unique<Catalog>();
            bool ok = read_catalog(path, ports_dir, *c);
            {
                std::lock_guard<std::mutex> lk(cat_mutex);
                cat_fresh = ok ? std::move(c) : nullptr;
            }
            uint64_t one = 1;
            ssize_t w = write(cat_evfd, &one, sizeof one);
            (void)w;
        });
    };
    auto take_catalog = [&]() {
        uint64_t cnt;
        while (read(cat_evfd, &cnt, sizeof cnt) == (ssize_t)sizeof cnt) {
        }
        if (!cat_reading)
            return;
        if (cat_thread.joinable())
            cat_thread.join();
        cat_reading = false;
        std::unique_ptr<Catalog> fresh;
        {
            std::lock_guard<std::mutex> lk(cat_mutex);
            fresh = std::move(cat_fresh);
        }
        for (auto it = failed_covers.begin(); it != failed_covers.end();)
            it = *it >= kStoreIdBase ? failed_covers.erase(it) : std::next(it);
        sf.set_catalog(fresh != nullptr, fresh ? std::move(*fresh) : Catalog{}, now_ms());
        want_redraw('c');
        if (cat_again) {
            cat_again = false;
            read_catalog_async();
        }
    };
    auto sync_steam = [&]() {
        settings_page.set_row(kSteamRow, steam_row(read_steam(ports_dir + kSteamTsv).signed_in), now_ms());
        if (nav.top() == Screen::Settings)
            want_redraw('s');
    };
    sf.host.now = now_ms;
    sf.host.wall = []() { return (int64_t)time(nullptr); };
    sf.host.is_ports = [&](int sys) {
        return sys >= 0 && sys < (int)sys_ids.size() && sys_ids[(size_t)sys] == kPortsSystem;
    };
    sf.host.online = online;
    sf.host.stale = [&]() {
        struct stat sb;
        std::string path = ports_dir + kCatalogDb;
        return stat(path.c_str(), &sb) != 0 || time(nullptr) - sb.st_mtime > kCatalogStaleS;
    };
    sf.host.index = [&]() { return index_ports(lib); };
    sf.host.steam = [&]() {
        SteamState st = read_steam(ports_dir + kSteamTsv);
        if (!kSteamFiles)
            st.ports.clear();
        return st;
    };
    sf.host.rescan = rescan_ports;
    sf.host.reread = read_catalog_async;
    sf.host.preview_art = [&](const std::vector<HomeGame>& shown, const std::vector<HomeGame>& want) {
        std::unordered_set<int64_t> ids;
        for (const HomeGame& g : shown)
            ids.insert(g.id);
        {
            std::lock_guard<std::mutex> lk(dmutex);
            dq.set_recent(std::move(ids));
        }
        for (const HomeGame& g : want)
            store_cover(g.id, kRecentGen);
    };
    sf.host.shot = want_shot;
    sf.host.toast = [&](const std::string& l1, const std::string& l2) { toast.show(now_ms(), l1, l2, true); };
    sf.host.closed = [&]() { deal_recent(false, true); };
    sf.host.redraw = [&]() { want_redraw('h'); };
    sf.host.result = [&](const HelperResult& r) {
        sync_steam();
        StoreToast t = r.cmd == HelperCmd::SteamLogout ? store_toast(r) : StoreToast{};
        if (t.show && nav.top() == Screen::Settings)
            settings_page.toast(now_ms(), t.line1, t.line2);
        else if (t.show)
            toast.show(now_ms(), t.line1, t.line2, nav.top() == Screen::Home);
    };
    sf.attach();
    was_online = online();
    flow.launch_game = [&](const HomeGame& g) {
        auto it = by_id.find(g.id);
        if (it != by_id.end())
            return try_launch(it->second);
        for (const Game& x : lib.games(kPortsSystem))
            if (x.id == g.id)
                return try_launch(x);
        return LaunchStart::Refused;
    };
    flow.draw_page = [&](Canvas& c, Screen sc, int64_t now) { render_page(c, sc, now); };
    flow.page_open = [&](Screen sc, int64_t now) {
        if (sc == Screen::Settings) {
            if (!zones_loaded) {
                settings_page.set_zones(load_zones(kZoneTab));
                zones_loaded = true;
            }
            sync_zone();
            if (!zone_key.has_value())
                zone_key.fetch(now);
            for (auto& k : row_keys)
                if (!k->has_value())
                    k->fetch(now);
            sync_steam();
            Storage st = read_storage(data_root, kVarDir, kSystemDir);
            settings_page.set_row(kStorageRow, info_row(storage_value(st), storage_lines(st)), now);
            settings_page.set_row(kAboutRow, info_row("", about_lines(kOsRelease, frontend_commit(), kernel_release())),
                                  now);
            settings_page.reset(theme().light);
            volume.get();
            battery.restart();
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
        if (sc == Screen::Port || sc == Screen::SteamLogin) {
            want_redraw('i');
            return sf.page_input(sc, a, rep, now);
        }
        if (btp.action(a, rep, now) != BtPage::Act::Pop) {
            want_redraw('b');
            return false;
        }
        bt.set_page_open(false, now);
        btp.close();
        return true;
    };
    flow.rearm = [&](int64_t now) { repeat.rearm(now); };
    flow.page_seed_y = [&](Screen s) { return s == Screen::Settings ? settings_page.focus_y() : sf.seed_y(s); };
    auto settings_view = [&]() {
        return flow.opening() ? settings_page.open_view(now_ms()) : settings_page.view(now_ms());
    };
    flow.page_rows = [&](Screen s) {
        if (s == Screen::Bluetooth)
            return bluetooth_page_rows(btp);
        return s == Screen::Settings ? settings_page_rows(settings_view()) : std::vector<PageRow>();
    };
    flow.page_head = [&](Screen s) {
        if (s == Screen::Bluetooth)
            return bluetooth_page_head(fonts);
        return s == Screen::Settings ? settings_page_head(fonts, settings_view()) : PageHead{};
    };
    flow.draw_word = [&](Canvas& c, const PageHead& h) {
        draw_page_ground(c, theme());
        draw_page_chrome(c, fonts, h.word.c_str(), nullptr, theme(), h.word == settings_word());
    };
    flow.last_frame = [&](const Surface& s) {
        copy_surface(p.draw_target(), s);
        if (launch.phase() != LaunchPhase::Leaving)
            toast.under(s);
    };
    flow.ctl.can_animate = can_animate;
    if (fstats.on) {
        flow.ctl.clock_us = now_us;
        flow.set_clock(now_us);
        flow.set_probe(probe);
    }
    int verified[6] = {};
    int set_skips = 0, set_fails = 0, set_seen[2] = {};
    std::string idle_said;
    flow.ctl.target_current = [&](int64_t now) -> char {
        if (more_last)
            return 'e';
        if (need_redraw)
            return 'n';
        if (!posted_last)
            return 'u';
        if (page_animating(now))
            return 'q';
        return 0;
    };
    flow.ctl.landed = [&](const char* what, const char* why) {
        landed_us = now_us();
        std::fprintf(stderr, "blockie-frontend: %s landed (%s)\n", what, why);
        if (std::strcmp(what, "expand") == 0 || std::strcmp(what, "collapse") == 0 || std::strcmp(what, "push") == 0 ||
            std::strcmp(what, "pop") == 0 || std::strcmp(what, "startup") == 0 || std::strcmp(what, "launch") == 0 ||
            std::strcmp(what, "return") == 0)
            fstats.end(why);
        if (fopts.verify) {
            int now_v[6] = {flow.pane() ? flow.pane()->verify_scans() : 0,
                            flow.pane() ? flow.pane()->verify_fails() : 0,
                            flow.page_engine() ? flow.page_engine()->verify_scans() : 0,
                            flow.page_engine() ? flow.page_engine()->verify_fails() : 0,
                            (flow.pane() ? flow.pane()->adopts() : 0) +
                                (flow.page_engine() ? flow.page_engine()->adopts() : 0),
                            (flow.pane() ? flow.pane()->adopt_fails() : 0) +
                                (flow.page_engine() ? flow.page_engine()->adopt_fails() : 0)};
            char tail[48] = "";
            if (std::strcmp(what, "expand") == 0 || std::strcmp(what, "collapse") == 0) {
                std::snprintf(tail, sizeof tail, " grid scans %d fails %d", flow.glow().verify_scans,
                              flow.glow().verify_fails);
            } else if (std::strcmp(what, "push") == 0 || std::strcmp(what, "pop") == 0) {
                std::snprintf(tail, sizeof tail, " settings skips %d fails %d", set_skips - set_seen[0],
                              set_fails - set_seen[1]);
                set_seen[0] = set_skips;
                set_seen[1] = set_fails;
            }
            std::fprintf(stderr,
                         "blockie-frontend: engine verify %s pane scans %d fails %d page scans %d fails %d l5 %d "
                         "fails %d%s\n",
                         what, now_v[0] - verified[0], now_v[1] - verified[1], now_v[2] - verified[2],
                         now_v[3] - verified[3], now_v[4] - verified[4], now_v[5] - verified[5], tail);
            std::copy(now_v, now_v + 6, verified);
        }
        if (std::strcmp(what, "return") == 0)
            want_redraw('o');
        launch.landed(what);
    };
    auto apply = [&](PadAction a, bool rep) {
        bool changed = flow.input(a, rep, now_ms());
        bool started = flow.ctl.started() && flow.ctl.active();
        if (changed && !started)
            want_redraw('o');
        if (started) {
            if (flow.ctl.reversed())
                fstats.end("reversed");
            fstats.begin(fold_kind_name(flow.ctl.kind()));
            fstats.tag = flow.ctl.start_tag();
            fstats.from_us = flow.ctl.from_us();
            fstats.to(flow.ctl.to_us());
        }
    };
    auto apply_theme = [&](bool light) {
        settings_page.set_light(light);
        if (theme().light != light) {
            set_theme_light(light);
            flow.theme_changed();
        }
        want_redraw('t');
    };
    theme_set.on_value = [&](bool light) { launch.theme(light); };
    theme_set.on_fail = [&](const std::string& error, const std::string& text) {
        settings_page.set_light(theme().light);
        settings_page.refused(kThemeRow, error, text, now_ms());
        want_redraw('t');
    };
    settings_page.on_theme = [&](bool light) { theme_set.set(light, now_ms()); };
    zone_key.on_value = [&](const std::string& v) {
        settings_page.set_zone(v);
        if (nav.top() == Screen::Settings)
            want_redraw('z');
    };
    zone_key.on_fail = [&](const std::string& error, const std::string& text) {
        sync_zone();
        settings_page.refused(kZoneRow, error, text, now_ms());
        if (nav.top() == Screen::Settings)
            want_redraw('z');
    };
    zone_key.on_missing = [&]() {
        settings_page.set_zone_missing(now_ms());
        if (nav.top() == Screen::Settings)
            want_redraw('z');
    };
    settings_page.on_zone = [&](const std::string& id) { zone_key.set(id, now_ms()); };
    auto settings_dirty = [&]() {
        if (nav.top() == Screen::Settings)
            want_redraw('s');
    };
    auto page_pix = [&](int64_t now) {
        Buffer b;
        Canvas c(b.surface());
        InkLayer scratch;
        draw_settings(c, fonts, scratch, settings_page.view(now), theme());
        return b.pix;
    };
    auto settings_set = [&](const std::function<bool()>& set) {
        bool check = fopts.verify && nav.top() == Screen::Settings;
        int64_t now = now_ms();
        std::vector<Buffer::T> before = check ? page_pix(now) : std::vector<Buffer::T>();
        if (set()) {
            settings_dirty();
        } else if (check) {
            set_skips++;
            set_fails += page_pix(now) != before;
        }
    };
    auto sync_key = [&](const SetdKey& k) {
        SettingsRowState st;
        st.shown = k.has_value();
        st.value = k.value();
        return settings_page.set_row(k.key(), st, now_ms());
    };
    for (auto& k : row_keys) {
        SetdKey* kp = k.get();
        kp->on_value = [&, kp](const std::string&) { settings_set([&]() { return sync_key(*kp); }); };
        kp->on_missing = [&, kp]() { settings_set([&]() { return sync_key(*kp); }); };
        kp->on_fail = [&, kp](const std::string& error, const std::string& text) {
            sync_key(*kp);
            settings_page.refused(kp->key(), error, text, now_ms());
            settings_dirty();
        };
        sync_key(*kp);
    }
    VolumeLevel vol_now, vol_open;
    bool vol_restore = false;
    int vol_last_set = -1;
    volume.on_value = [&](const VolumeLevel& v) {
        vol_now = v;
        if (vol_restore) {
            vol_restore = false;
            if (v.pct == vol_last_set)
                volume.set(vol_open.pct, vol_open.muted);
            vol_last_set = -1;
        }
        settings_set([&]() { return settings_page.set_row(kVolumeRow, volume_row(true, v.pct, v.muted), now_ms()); });
    };
    volume.on_fail = [&]() {
        vol_restore = false;
        settings_set([&]() { return settings_page.set_row(kVolumeRow, volume_row(false, 0, false), now_ms()); });
    };
    volume.on_set = [&](bool) { volume.get(); };
    backlight.on_blank = [&]() { settings_set([&]() { return settings_page.close_row(kBrightnessRow, now_ms()); }); };
    backlight.on_fail = [&](const std::string& word) {
        settings_page.toast(now_ms(), settings_page.title(kBrightnessRow), word);
        settings_dirty();
    };
    settings_page.on_open = [&](const std::string& id) {
        if (id == kBrightnessRow)
            backlight.open();
        if (id == kVolumeRow) {
            vol_open = vol_now;
            vol_last_set = -1;
            vol_restore = false;
        }
    };
    settings_page.on_close = [&](const std::string& id) {
        if (id == kBrightnessRow)
            backlight.drop();
    };
    settings_page.on_preview = [&](const std::string& id, const std::string& v) {
        int pct = std::atoi(v.c_str());
        if (id == kBrightnessRow)
            backlight.preview(pct, now_ms());
        if (id == kVolumeRow && volume.set(pct))
            vol_last_set = pct;
    };
    settings_page.on_restore = [&](const std::string& id) {
        if (id == kBrightnessRow)
            backlight.restore(now_ms());
        if (id == kVolumeRow && vol_last_set >= 0) {
            vol_restore = true;
            volume.get();
        }
    };
    settings_page.on_pick = [&](const std::string& id, const std::string& v) {
        int64_t now = now_ms();
        for (auto& k : row_keys)
            if (k->key() == id) {
                SettingsRowState st;
                st.shown = true;
                st.value = v;
                settings_page.set_row(id, st, now);
                k->set(v, now);
                return;
            }
        if (id == kRadioRow) {
            WifiCmd c;
            c.type = WifiCmd::Radio;
            c.on = v == "on";
            wifi.run(c, now);
        }
    };
    settings_page.on_push = [&](const std::string& id) {
        if (id != kBluetoothRow)
            return;
        nav.stack.push_back(Screen::Bluetooth);
        flow.push(Screen::Bluetooth);
    };
    settings_page.on_action = [&](const std::string& id) {
        HelperCmd c = HelperCmd::Refresh;
        if (id == kSteamRow && !(ports.holds(std::string(), c) && c == HelperCmd::SteamLogout))
            ports.ask(kids, HelperCmd::SteamLogout, std::string());
    };
    std::string bt_sub = bt_subtitle(bt.state(), now_ms());
    settings_page.set_row(kBluetoothRow, info_row(bt_sub), now_ms());
    auto launch_edges = [&]() {
        bool active = p.activated();
        if (active != was_active) {
            was_active = active;
            int64_t at = now_us();
            if (active && launch.phase() != LaunchPhase::Idle)
                std::fprintf(stderr, "blockie-frontend: activation 1 in %s at %lld us, last frame hash %016llx\n",
                             launch_phase_name(launch.phase()), (long long)at, frame_hash(p.draw_target()));
            else
                std::fprintf(stderr, "blockie-frontend: activation %d in %s at %lld us\n", active ? 1 : 0,
                             launch_phase_name(launch.phase()), (long long)at);
        }
        launch.edges(active, child.pid > 0, now_ms());
    };
    launch.hooks.land = land_all;
    launch.hooks.reverse = [&](int64_t now) { return flow.launch_back(now); };
    launch.hooks.fold_end = [&]() { return flow.ctl.active() ? flow.ctl.t0() + flow.ctl.run_length() : (int64_t)-1; };
    launch.hooks.finish = [&](int64_t now) { flow.ctl.finish(now); };
    launch.hooks.come_back = [&](int64_t now) {
        std::fprintf(stderr, "blockie-frontend: return after a cover wait of %lld ms\n",
                     (long long)(now - launch.cover_until() + kCoverWaitMs));
        flow.come_back(now);
        fstats.begin("return");
        fstats.tag = flow.ctl.start_tag();
        fstats.from_us = flow.ctl.from_us();
        fstats.to(flow.ctl.to_us());
    };
    launch.hooks.covers_pending = [&]() {
        std::lock_guard<std::mutex> lk(dmutex);
        return covers_pending(home, model, [&](int64_t id) { return dq.pending(id); });
    };
    launch.hooks.played = [&]() {
        if (child_game_id < 0)
            return;
        lib.mark_played(child_game_id, (int64_t)time(nullptr));
        deal_recent(false);
    };
    launch.hooks.theme = apply_theme;
    launch.hooks.drops = [&]() {
        if (pending_drops.empty() || (flow.ctl.active() && !drop_budget))
            return;
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
    };
    launch.hooks.toast = [&](const std::string& line1, const std::string& line2) {
        toast.show(now_ms(), line1, line2, nav.top() == Screen::Home);
        want_redraw('o');
    };
    LaunchKeys keys(launch, repeat);
    keys.apply = [&](PadAction a, bool rep, int64_t) { apply(a, rep); };
    auto press = [&](PadAction a, uint8_t src, int64_t now) { keys.press(a, src, child.pid <= 0, now); };
    launch.hooks.act = [&](PadAction a, int64_t now) { keys.act(a, now); };

    auto pump = [&]() {
        bool ok = pump_presenter(p, touch_scratch);
        std::vector<PadEdge> edges;
        edges.swap(key_edges);
        int leave = leave_at;
        leave_at = -1;
        int64_t now = now_ms();
        for (int i = 0; i < (int)edges.size(); i++) {
            if (i == leave)
                repeat.release_source(kFromKeys, now);
            const PadEdge& e = edges[i];
            if (!e.down) {
                repeat.release(e.action, kFromKeys, now);
                continue;
            }
            std::fprintf(stderr, "blockie-frontend: key %s\n", action_name(e.action));
            press(e.action, kFromKeys, now);
        }
        if (leave == (int)edges.size())
            repeat.release_source(kFromKeys, now);
        return ok;
    };

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
                                     &art_drops, &battery]() {
            JpegDecoder jpeg;
            std::unique_lock<std::mutex> lk(dmutex);
            for (;;) {
                dcv.wait(lk, [&]() { return decode_stop || battery.wanted() || (!decode_pause && dq.size() > 0); });
                if (decode_stop)
                    break;
                if (battery.wanted()) {
                    lk.unlock();
                    battery.run();
                    uint64_t one = 1;
                    ssize_t w = write(art_evfd, &one, sizeof one);
                    (void)w;
                    lk.lock();
                    continue;
                }
                DecodeJob j;
                if (!dq.pop(j))
                    continue;
                lk.unlock();
                int64_t t0 = now_us();
                DecodedImage dec;
                int cover_side = kTileSide;
                for (int sd : j.sides)
                    cover_side = std::max(cover_side, sd);
                bool ok = load_cover(jpeg, j.path, cover_side, dec) && dec.ok();
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
                        art_drops.push_back({j.game_id, j.path, img, true, side, s + 1 == n, j.index_gen});
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
    want_redraw('o');
    key_edges.clear();
    leave_at = -1;
    volume.get();

    read_catalog_async();
    while (!bench && gate_runs(p.lost(), p.closed())) {
        p.flush();
        bus.pump();
        bus.tick(now_ms());
        {
            int64_t bnow = now_ms();
            bt.tick(bnow);
            if (bt.serial() != bt_seen) {
                bt_seen = bt.serial();
                btp.set_model(bt.state(), bnow);
                if (nav.top() == Screen::Bluetooth)
                    want_redraw('b');
            }
            if (sync_bt_row(settings_page, bt_sub, bt_subtitle(bt.state(), bnow), bnow))
                settings_dirty();
            wifi.tick(bnow);
            if (wifi.serial() != wifi_seen) {
                wifi_seen = wifi.serial();
                const WifiState& ws = wifi.state();
                settings_set([&]() {
                    bool drawn = settings_page.set_row(kRadioRow, radio_row(wifi_radio_shown(ws), ws.radio), bnow);
                    if (ws.note.seq != wifi_note) {
                        wifi_note = ws.note.seq;
                        if (nav.top() == Screen::Settings) {
                            settings_page.toast(bnow, ws.note.title, ws.note.text);
                            drawn = true;
                        }
                    }
                    return drawn;
                });
                if (online() != was_online) {
                    was_online = online();
                    sf.online_changed(bnow);
                    want_redraw('n');
                }
            }
            if (nav.top() == Screen::Bluetooth && btp.dirty(bnow))
                want_redraw('b');
        }

        std::array<struct pollfd, 10> fds;
        size_t nfds = 0;
        auto watch = [&](int f, short events) {
            if (nfds == fds.size()) {
                std::fprintf(stderr, "blockie-frontend: poll set full\n");
                std::abort();
            }
            fds[nfds].fd = f;
            fds[nfds].events = events;
            fds[nfds].revents = 0;
            return (int)nfds++;
        };
        GateFds gf = gate_fds(p.display_fd(), p.event_fd(), p.blocked());
        watch(gf.display.fd, gf.display.events);
        watch(sfd, POLLIN);
        int pi = pad_ok ? watch(pad.fd(), POLLIN) : -1;
        int ai = art_evfd >= 0 ? watch(art_evfd, POLLIN) : -1;
        int bi = bus.fd() >= 0 ? watch(bus.fd(), POLLIN | (bus.wants_write() ? POLLOUT : 0)) : -1;
        int vi = volume.fd() >= 0 ? watch(volume.fd(), POLLIN) : -1;
        int ei = watch(gf.event.fd, gf.event.events);
        int hi = ports.fd() >= 0 ? watch(ports.fd(), POLLIN) : -1;
        int ci = cat_evfd >= 0 ? watch(cat_evfd, POLLIN) : -1;
        int64_t pnow = now_ms();
        ports.tick(pnow);
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
        until(wifi.deadline());
        if (nav.top() == Screen::Bluetooth)
            until(btp.deadline(pnow));
        if (nav.top() == Screen::Settings)
            until(settings_page.deadline(pnow));
        if ((nav.top() == Screen::Settings || battery.reads() == 0) && art_evfd >= 0) {
            if (battery.due(pnow)) {
                {
                    std::lock_guard<std::mutex> lk(dmutex);
                    battery.request(pnow);
                }
                dcv.notify_one();
            }
            until(battery.deadline());
        }
        until(toast.deadline(pnow));
        until(ports.deadline());
        until(sf.deadline());
        bool port_top = nav.top() == Screen::Port || nav.top() == Screen::SteamLogin;
        int64_t port_due = port_top ? portp.deadline(pnow, nav.top() == Screen::Port) : -1;
        until(port_due);
        until(launch.deadline());
        until(ready_report.deadline());
        timeout = gate.timeout(gate_in(pnow, timeout));
        if (bus.data_remains() || (!flow.ctl.active() && !pending_drops.empty() && !launch.holds()))
            timeout = 0;
        flow.prepare_pass();
        if (flow.prepare_owed()) {
            int64_t prep0 = now_ms();
            const char* why = timeout == 0                           ? "timeout"
                              : flow.ctl.active()                    ? "fold"
                              : animating(pnow)                      ? "animating"
                              : need_redraw                          ? "redraw"
                              : gate.armed()                         ? "armed"
                              : flow.ctl.stale()                     ? "stale"
                              : !p.activated()                       ? "inactive"
                              : child.pid > 0                        ? "child"
                              : launch.phase() != LaunchPhase::Idle ? "launch"
                                                                     : nullptr;
            int64_t first[3] = {}, worst[3] = {}, cpu_first[3] = {}, cpu_worst[3] = {}, total[3] = {}, cpu_all[3] = {};
            int phases[3] = {};
            while (!why && flow.prepare_owed()) {
                int ready = poll(fds.data(), (nfds_t)nfds, 0);
                if (ready != 0) {
                    why = ready < 0 ? "poll" : "fd";
                    for (size_t k = 0; ready > 0 && k < nfds; k++)
                        if (fds[k].revents) {
                            int ik = (int)k;
                            why = k == 0    ? "fd display"
                                  : k == 1  ? "fd signal"
                                  : ik == pi ? "fd pad"
                                  : ik == ai ? "fd art"
                                  : ik == bi ? "fd bus"
                                  : ik == vi ? "fd volume"
                                  : ik == ei ? "fd event"
                                             : "fd";
                            break;
                        }
                    break;
                }
                int64_t s0 = now_us(), part[3] = {}, cpart[3] = {};
                do {
                    int64_t a0 = fstats.on ? now_us() : 0, ac = fstats.on ? thread_cpu_us() : 0;
                    char c = flow.prepare(kPrepareRows);
                    int k = c == 'g' ? 0 : c == 'p' ? 1 : 2;
                    if (fstats.on) {
                        part[k] += now_us() - a0;
                        cpart[k] += thread_cpu_us() - ac;
                    }
                    if (k == 0 && !flow.glow_owed())
                        glow_done_us = now_us();
                } while (flow.prepare_owed() && now_us() - s0 < kPrepareUs);
                for (int k = 0; fstats.on && k < 3; k++) {
                    if (part[k] == 0)
                        continue;
                    if (phases[k]++ == 0) {
                        first[k] = part[k];
                        cpu_first[k] = cpart[k];
                    }
                    worst[k] = std::max(worst[k], part[k]);
                    cpu_worst[k] = std::max(cpu_worst[k], cpart[k]);
                    total[k] += part[k];
                    cpu_all[k] += cpart[k];
                }
            }
            for (int k = 0; fstats.on && k < 3; k++) {
                if (phases[k] == 0)
                    continue;
                bool owed = k == 0 ? flow.glow_owed() : k == 1 ? flow.page_owed() : flow.start_owed();
                std::fprintf(stderr,
                             "blockie-frontend: idle prepare %s phases %d first %.2f cpu %.2f max %.2f cpu %.2f "
                             "total %.2f cpu %.2f ms %s%s\n",
                             k == 0 ? "glow" : k == 1 ? "page" : "start", phases[k], first[k] / 1000.0,
                             cpu_first[k] / 1000.0, worst[k] / 1000.0, cpu_worst[k] / 1000.0, total[k] / 1000.0,
                             cpu_all[k] / 1000.0, owed ? "stopped by " : "done", owed && why ? why : "");
            }
            if (fstats.on && (phases[0] > 0 || phases[1] > 0 || phases[2] > 0)) {
                idle_said.clear();
            } else if (fstats.on && why && idle_said != why) {
                std::fprintf(stderr, "blockie-frontend: idle prepare blocked %s\n", why);
                idle_said = why;
            }
            if (timeout > 0)
                timeout = (int)std::max<int64_t>(0, timeout - (now_ms() - prep0));
        }
        int pr = poll(fds.data(), (nfds_t)nfds, timeout);
        if (pr < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (hi >= 0 && (fds[hi].revents & (POLLIN | POLLHUP | POLLERR)))
            ports.readable();
        if (gate_pump(fds[0].revents) && !pump())
            break;
        if (fds[ei].revents & POLLIN)
            drain_event(gf.event.fd);
        if (bi >= 0 && (fds[bi].revents & (POLLIN | POLLOUT | POLLHUP | POLLERR)))
            bus.pump();
        if (ci >= 0 && (fds[ci].revents & POLLIN))
            take_catalog();
        if (port_due >= 0 && now_ms() >= port_due)
            want_redraw('d');
        sf.tick(now_ms());

        launch_edges();

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

        if (p.ready())
            ready_report.shown(pad_ok, p.activated());
        ready_report.tick();
        if (ready_report.wants_frame(pad_ok, p.activated()))
            want_redraw('r');

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

        if (ai >= 0 && (fds[ai].revents & POLLIN)) {
            take_drops();
            if (battery.take(battery_now))
                settings_set([&]() {
                    return settings_page.set_row(kBatteryRow, info_row(battery_words(battery_now)), now_ms());
                });
        }
        if (vi >= 0 && (fds[vi].revents & (POLLIN | POLLHUP | POLLERR)))
            volume.readable();

        if (!p.activated() || child.pid > 0)
            repeat.release_all();
        PadAction ra = PadAction::Up;
        int64_t tn = now_ms();
        if (repeat.due(tn, ra)) {
            if (repeat.sources(ra) == kFromPad && pad_ok && pad.held(ra) == Held::No)
                repeat.release(ra, kFromPad, tn);
            else
                keys.repeat(ra, tn);
        }

        int64_t fnow = now_ms();
        launch.step(fnow);
        GateStep g = gate.step(gate_in(fnow, -1));
        if (g.report_done)
            fstats.done(now_us());
        if (g.land_reason)
            land_all(fnow, g.land_reason);
        if (g.land_reason || !can_animate())
            settings_page.land();
        if (g.redraw)
            want_redraw('g');
        if (flow.ctl.active() && need_redraw) {
            fstats.redraw(redraw_why);
            flow.redraw();
            need_redraw = false;
        }
        if (!need_redraw)
            redraw_why = 0;
        if (fstats.on && drew_last && !g.draw)
            std::fprintf(stderr, "blockie-frontend: glide rest scroll %d %d bar %d %d tails %d\n", glide_at[0],
                         flow.scroll_glide().target(), glide_at[1], flow.bar_glide().target(), gate.tails());
        drew_last = g.draw;
        LaunchDraw kind = launch.draw(g.draw);
        if (kind == LaunchDraw::Black)
            black_frame(fnow);
        else if (kind == LaunchDraw::Home && frame(fnow))
            need_redraw = false;
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
    if (cat_thread.joinable())
        cat_thread.join();
    if (cat_evfd >= 0)
        close(cat_evfd);
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
