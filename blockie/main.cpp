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
#include "bt.h"
#include "bus.h"
#include "decodeq.h"
#include "fold.h"
#include "framestats.h"
#include "image.h"
#include "ink.h"
#include "input_pad.h"
#include "launch.h"
#include "library.h"
#include "nav.h"
#include "pagemove.h"
#include "present_wl.h"
#include "repeat.h"
#include "screen_bt.h"
#include "scraper.h"
#include "screen_selector.h"
#include "screen_settings.h"
#include "theme.h"
#include "theme_setting.h"
#include "tile_grid.h"

namespace fd {

namespace {

enum class Motion { None, Fold, Page };

using Art = std::shared_ptr<const Image>;

constexpr int kFrameWatchdogMs = 100;
constexpr int kThemeWaitMs = 300;
constexpr int kCoverWaitMs = 300;

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

const char* fold_name(FoldDir d)
{
    switch (d) {
    case FoldDir::ToDetails: return "enter";
    case FoldDir::ToSelector: return "exit";
    case FoldDir::PagePush: return "push";
    case FoldDir::PagePop: return "pop";
    }
    return "?";
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
};

class FnHost : public NavHost {
public:
    std::function<int()> count;
    std::function<MenuKind(int)> kind;
    std::function<void(Screen)> push_fn;
    std::function<void(Screen)> pop_fn;
    std::function<bool(Screen, PadAction, bool)> page_fn;
    std::function<int()> recent;
    std::function<int(int)> enter;
    std::function<void()> leave;
    std::function<void(bool)> moved;
    std::function<void(int)> play_recent;
    std::function<void(int)> play_game;

    int menu_count() const override { return count(); }
    MenuKind menu_kind(int i) const override { return kind(i); }
    int recent_count() const override { return recent(); }
    int enter_system(int sys) override { return enter(sys); }
    void exit_system() override { leave(); }
    void system_moved(bool scrolled) override { moved(scrolled); }
    void launch_recent(int i) override { play_recent(i); }
    void launch_system_game(int i) override { play_game(i); }
    void push(Screen sc) override { push_fn(sc); }
    void pop(Screen sc) override { pop_fn(sc); }
    bool page_action(Screen sc, PadAction a, bool repeat) override { return page_fn(sc, a, repeat); }
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
    SelectorModel model;
    model.focus = 0;
    std::vector<Game> recent;
    std::vector<std::string> sys_ids;
    std::vector<MenuKind> kinds;
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
    Nav nav;
    Pane& pane = nav.pane;
    int& tile_focus = nav.tile_focus;
    bool& system_view = nav.system_view;
    std::vector<Game> sysgames;
    std::vector<Art> recent_hold;
    int& sys_focus = nav.sys_focus;
    int& sys_scroll = nav.sys_scroll;
    Repeat repeat;
    SettingsPage settings_page;
    settings_page.reset(theme().light);
    InkLayer ink;
    Buffer from_buf;
    Buffer to_buf;
    Motion motion = Motion::None;
    FoldPlan fold;
    int64_t fold_t0 = 0;
    bool to_dirty = false;
    bool end_ready = false;
    PageMove pm;
    int64_t frame_req_at = -1;
    bool drop_budget = false;
    int scroll_from = 0;
    int scroll_to = 0;
    int64_t scroll_t0 = -1000000000;
    std::string sys_name;
    std::unordered_map<int64_t, Art> cover_cache;
    Launcher::Child child;
    int64_t child_game_id = -1;

    std::deque<ScrapeJob> squeue;
    std::mutex qmutex;
    std::unordered_set<int64_t> requested;
    int64_t sys_epoch = 0;
    std::mutex art_mutex;
    std::vector<ArtDrop> art_drops;
    std::atomic<bool> scrape_stop{false};
    std::atomic<bool> scrape_pause{false};
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
            added = dq.push({id, path, gen, now_ms()});
        }
        if (added)
            dcv.notify_one();
    };
    auto cached = [&](int64_t id) {
        auto it = cover_cache.find(id);
        return it != cover_cache.end() ? it->second : Art();
    };
    auto request_cover = [&](const Game& g, int64_t gen) {
        if (cover_cache.count(g.id))
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
    auto folder_of = [&](const std::string& system_id) {
        for (const auto& sc : lib.config())
            if (sc.id == system_id)
                return sc.libretro_folder;
        return std::string();
    };
    auto deal_recent = [&]() {
        std::vector<Art>& grid = system_view ? recent_hold : model.tiles;
        std::unordered_map<int64_t, Art> had;
        for (size_t k = 0; k < recent.size() && k < grid.size(); k++)
            if (grid[k])
                had[recent[k].id] = grid[k];
        recent = lib.recent_games(8);
        grid.clear();
        std::unordered_set<int64_t> ids;
        int queued = 0;
        for (const Game& g : recent) {
            ids.insert(g.id);
            auto it = had.find(g.id);
            Art art = it != had.end() ? it->second : cached(g.id);
            if (!art && !request_cover(g, kRecentGen)) {
                enqueue_scrape(g, 0, folder_of(g.system_id));
                queued++;
            }
            grid.push_back(std::move(art));
        }
        {
            std::lock_guard<std::mutex> lk(dmutex);
            dq.set_recent(std::move(ids));
        }
        return queued;
    };
    int recent_jobs = deal_recent();
    auto scroll_px = [&](int64_t now) {
        int d = fx_round(tok::motion_scroll);
        int64_t e = now - scroll_t0;
        if (e >= d || e < 0)
            return scroll_to;
        fx k = curve(Curve::EaseIn, (fx)(e * kOne / d));
        return scroll_from + (int)(((int64_t)(scroll_to - scroll_from) * k) >> kFxBits);
    };
    auto scrolling = [&](int64_t now) { return now - scroll_t0 < fx_round(tok::motion_scroll); };
    auto visible_rows = [&]() {
        return std::min(kMenuVisibleRows, std::max(0, (int)model.systems.size() - nav.menu_top));
    };
    auto sync_model = [&](int64_t now) {
        model.focus = nav.focus;
        if (!system_view)
            model.tile_focus = pane == Pane::Details ? tile_focus : -1;
        int target = nav.menu_top * kMenuPitchPx;
        if (target != scroll_to) {
            scroll_from = scroll_px(now);
            scroll_to = target;
            scroll_t0 = p.activated() && child.pid <= 0 ? now : -1000000000;
        }
        model.menu_top_px = scroll_px(now);
        model.poses.clear();
        model.grid_a256 = 256;
        model.bar_a256 = 256;
        if (motion == Motion::Page && pm.running(now)) {
            model.poses.resize(model.systems.size());
            for (int i = 0; i < (int)model.systems.size(); i++) {
                ItemPose ip = pm.item(now, i - nav.menu_top);
                model.poses[i] = {ip.dx, ip.a256};
            }
            model.grid_a256 = pm.grid_a256(now);
            model.bar_a256 = pm.bar_a256(now);
        }
    };
    auto render_home = [&](Canvas& c) { draw_selector(c, fonts, model, pane, ArtFit::Cover, false, system_view, theme()); };
    auto render_page = [&](Canvas& c, Screen sc, int64_t now) {
        if (sc == Screen::Bluetooth)
            btp.draw(c, ink, now);
        else if (sc == Screen::Settings)
            draw_settings(c, fonts, ink, settings_page.view(now), theme());
    };
    auto animating = [&](int64_t now) {
        if (motion == Motion::Fold && now - fold_t0 < fold_duration(fold))
            return true;
        if (motion == Motion::Page && pm.running(now))
            return true;
        if (nav.top() == Screen::Settings && settings_page.animating(now))
            return true;
        if (nav.top() == Screen::Bluetooth && btp.animating(now))
            return true;
        return nav.top() == Screen::Home && scrolling(now);
    };
    auto land = [&](const char* why) {
        if (motion == Motion::None)
            return;
        std::fprintf(stderr, "blockie-frontend: motion landed (%s)\n", why);
        motion = Motion::None;
        fstats.end(why);
        pm.land();
        if (p.frame_pending())
            p.cancel_frame();
        frame_req_at = -1;
        need_redraw = true;
    };
    auto render_end = [&](int64_t now) {
        sync_model(now);
        Canvas tc(to_buf.surface());
        if (nav.top() == Screen::Home)
            render_home(tc);
        else
            render_page(tc, nav.top(), now);
        to_dirty = false;
        end_ready = true;
    };
    auto frame = [&](int64_t now) -> bool {
        int64_t t0 = now_us();
        p.begin_frame(now, p.show_at(now));
        p.begin_full();
        Surface dst = p.draw_target();
        Canvas c(dst);
        fonts.begin_frame(-1);
        sync_model(now);
        if (motion == Motion::Fold) {
            if (to_dirty) {
                int64_t r0 = now_us();
                Canvas tc(to_buf.surface());
                if (nav.top() == Screen::Home)
                    render_home(tc);
                else
                    render_page(tc, nav.top(), now);
                to_dirty = false;
                fstats.to((int)(now_us() - r0));
            }
            fold_compose(from_buf.surface(), to_buf.surface(), dst, fold, now - fold_t0);
        } else if (nav.top() == Screen::Home) {
            render_home(c);
        } else {
            render_page(c, nav.top(), now);
        }
        p.present(kScreen);
        bool more = animating(now);
        if (more)
            p.request_frame();
        bool ok = p.flip();
        if (ok && motion != Motion::None)
            fstats.frame(t0, now_us());
        if (ok && more)
            frame_req_at = now;
        if (ok && motion != Motion::None)
            drop_budget = true;
        if (!ok && more)
            p.cancel_frame();
        return ok;
    };
    auto can_animate = [&]() { return p.activated() && child.pid <= 0; };
    auto start_fold = [&](FoldDir dir, int title_bottom, int64_t now) {
        if (!can_animate())
            return;
        int64_t c0 = now_us();
        copy_surface(end_ready ? to_buf.surface() : p.draw_target(), from_buf.surface());
        int from_us = (int)(now_us() - c0);
        end_ready = false;
        fold = fold_plan(dir, nav.focus - nav.menu_top, title_bottom);
        fold_t0 = now;
        to_dirty = true;
        motion = Motion::Fold;
        fstats.begin(fold_name(dir));
        fstats.from_us = from_us;
    };

    auto try_launch = [&](const Game& g) {
        if (child.pid > 0)
            return;
        std::string core, sysname;
        for (const auto& s : lib.config())
            if (s.id == g.system_id) {
                core = s.core;
                sysname = s.name;
                break;
            }
        for (char& ch : sysname)
            ch = (char)std::tolower((unsigned char)ch);
        if (!launcher.core_installed(core)) {
            std::fprintf(stderr, "blockie-frontend: core %s absent for %s\n", core.c_str(), g.system_id.c_str());
            return;
        }
        LaunchSpec spec;
        spec.system = sysname;
        spec.rompath_abs = g.rompath_abs;
        spec.core = core;
        Launcher::Child ch = launcher.spawn(spec);
        if (ch.pid < 0) {
            std::fprintf(stderr, "blockie-frontend: spawn failed for %s\n", g.system_id.c_str());
            return;
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
                deal_recent();
                if (!system_view && pane == Pane::Details)
                    tile_focus = 0;
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

    const int kCols = Nav::kCols;
    const int kRows = Nav::kRows;
    auto refresh_system = [&](bool rebuild) {
        int start = sys_scroll * kCols;
        if (rebuild) {
            model.tiles.clear();
            int end = start + kRows * kCols;
            if (end > (int)sysgames.size())
                end = (int)sysgames.size();
            for (int i = start; i < end; i++)
                model.tiles.push_back(cached(sysgames[i].id));
            model.section_label = sys_name;
            int lo = (sys_scroll - 1) * kCols;
            if (lo < 0)
                lo = 0;
            int hi = (sys_scroll + kRows + 1) * kCols;
            if (hi > (int)sysgames.size())
                hi = (int)sysgames.size();
            std::unordered_set<int64_t> keep;
            for (int i = lo; i < hi; i++)
                keep.insert(sysgames[i].id);
            for (auto it = cover_cache.begin(); it != cover_cache.end();) {
                if (keep.count(it->first))
                    ++it;
                else
                    it = cover_cache.erase(it);
            }
            {
                std::lock_guard<std::mutex> lk(dmutex);
                dq.set_window(++win_gen, std::move(keep));
            }
            if (start < (int)sysgames.size()) {
                std::string folder = folder_of(sysgames[start].system_id);
                for (int i = start; i < hi; i++)
                    if (!request_cover(sysgames[i], win_gen))
                        enqueue_scrape(sysgames[i], sys_epoch, folder);
            }
        }
        model.game_system = sys_name;
        model.game_title = (sys_focus >= 0 && sys_focus < (int)sysgames.size()) ? sysgames[sys_focus].name : std::string();
        model.tile_focus = sys_focus - start;
    };
    auto exit_system = [&]() {
        system_view = false;
        model.tiles.swap(recent_hold);
        recent_hold.clear();
        cover_cache.clear();
        {
            std::lock_guard<std::mutex> lk(qmutex);
            std::deque<ScrapeJob> keep;
            requested.clear();
            for (auto& j : squeue) {
                bool in_recent = j.epoch == 0;
                for (const Game& g : recent)
                    if (g.id == j.game.id)
                        in_recent = true;
                if (in_recent) {
                    requested.insert(j.game.id);
                    keep.push_back(std::move(j));
                }
            }
            squeue.swap(keep);
        }
        {
            std::lock_guard<std::mutex> lk(dmutex);
            dq.set_window(kRecentGen, {});
        }
        model.section_label.clear();
        model.game_system.clear();
        model.game_title.clear();
        pane = Pane::Selector;
        need_redraw = true;
    };
    auto show_tile = [&](int pos) {
        if (child.pid > 0)
            return;
        if (motion != Motion::Fold) {
            need_redraw = true;
            return;
        }
        if (nav.top() != Screen::Home || to_dirty)
            return;
        Canvas tc(to_buf.surface());
        tc.set_bound(grid_tile_rect(pos).grow(kTileFocusBorder));
        render_home(tc);
    };
    auto place_art = [&](int64_t id, const Art& img) {
        cover_cache[id] = img;
        std::vector<Art>& grid = system_view ? recent_hold : model.tiles;
        for (int k = 0; k < (int)recent.size() && k < (int)grid.size(); k++)
            if (recent[k].id == id) {
                grid[k] = img;
                if (!system_view)
                    show_tile(k);
                break;
            }
        if (!system_view)
            return;
        int start = sys_scroll * kCols;
        for (int i = 0; i < (int)model.tiles.size() && start + i < (int)sysgames.size(); i++)
            if (sysgames[start + i].id == id) {
                model.tiles[i] = img;
                show_tile(i);
                break;
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
            if (drp.decoded)
                dq.done(drp.game_id);
            want = dq.accept(drp.game_id);
        }
        if (!drp.decoded)
            failed_covers.erase(drp.game_id);
        if (!want)
            return;
        if (drp.decoded) {
            if (drp.img) {
                place_art(drp.game_id, drp.img);
                return;
            }
            failed_covers.insert(drp.game_id);
            for (const Game& g : recent)
                if (g.id == drp.game_id) {
                    enqueue_scrape(g, 0, folder_of(g.system_id));
                    return;
                }
            for (const Game& g : sysgames)
                if (g.id == drp.game_id) {
                    enqueue_scrape(g, sys_epoch, folder_of(g.system_id));
                    return;
                }
            return;
        }
        bool in_recent = false;
        for (const Game& g : recent)
            if (g.id == drp.game_id)
                in_recent = true;
        queue_decode(drp.game_id, drp.path, in_recent ? kRecentGen : win_gen);
    };
    FnHost host;
    host.count = [&]() { return (int)model.systems.size(); };
    host.kind = [&](int i) { return i >= 0 && i < (int)kinds.size() ? kinds[i] : MenuKind::System; };
    host.push_fn = [&](Screen sc) {
        int64_t now = now_ms();
        if (sc == Screen::Settings)
            settings_page.reset(theme().light);
        if (sc == Screen::Bluetooth) {
            btp.open(now);
            bt.set_page_open(true, now);
        }
        repeat.rearm(now);
        start_fold(FoldDir::PagePush, 0, now);
    };
    host.pop_fn = [&](Screen) {
        int64_t now = now_ms();
        repeat.rearm(now);
        start_fold(FoldDir::PagePop, 0, now);
    };
    host.page_fn = [&](Screen sc, PadAction a, bool rep) {
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
    host.recent = [&]() { return (int)recent.size(); };
    host.enter = [&](int sys) -> int {
        if (sys < 0 || sys >= (int)sys_ids.size())
            return 0;
        sysgames = lib.games(sys_ids[sys]);
        if (sysgames.empty())
            return 0;
        sys_name = model.systems[sys].name;
        sys_focus = 0;
        sys_scroll = 0;
        system_view = true;
        sys_epoch++;
        model.tiles.swap(recent_hold);
        refresh_system(true);
        int64_t now = now_ms();
        repeat.rearm(now);
        start_fold(FoldDir::ToDetails, detail_title_bottom(fonts, model), now);
        return (int)sysgames.size();
    };
    host.leave = [&]() {
        int title_bottom = detail_title_bottom(fonts, model);
        exit_system();
        int64_t now = now_ms();
        repeat.rearm(now);
        start_fold(FoldDir::ToSelector, title_bottom, now);
    };
    host.moved = [&](bool scrolled) { refresh_system(scrolled); };
    host.play_recent = [&](int i) {
        if (i >= 0 && i < (int)recent.size())
            try_launch(recent[i]);
    };
    host.play_game = [&](int i) {
        if (i >= 0 && i < (int)sysgames.size())
            try_launch(sysgames[i]);
    };
    auto apply = [&](PadAction a, bool rep) {
        bool cut = motion != Motion::None;
        land("input");
        if (cut)
            render_end(now_ms());
        if (nav_apply(nav, host, a, rep))
            need_redraw = true;
        end_ready = false;
    };
    theme_set.on_value = [&](bool light) {
        settings_page.set_light(light);
        if (theme().light != light) {
            set_theme_light(light);
            to_dirty = true;
        }
        need_redraw = true;
    };
    theme_set.on_fail = [&]() {
        settings_page.set_light(theme().light);
        need_redraw = true;
    };
    settings_page.on_theme = [&](bool light) { theme_set.set(light, now_ms()); };
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
                                     &scrape_stop, &scrape_pause]() {
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
                        if (squeue.empty())
                            break;
                        j = std::move(squeue.front());
                        squeue.pop_front();
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
                Art img;
                if (ok)
                    img = std::make_shared<const Image>(cover_tile(dec, kTileSide));
                int64_t t2 = now_us();
                std::fprintf(stderr, "blockie-frontend: decode %lld %s %dx%d wait %lld png %.1f tile %.1f ms\n",
                             (long long)j.game_id, ok ? "ok" : "fail", dec.w, dec.h,
                             (long long)(t0 / 1000 - j.queued_ms), (t1 - t0) / 1000.0, (t2 - t1) / 1000.0);
                {
                    std::lock_guard<std::mutex> ak(art_mutex);
                    art_drops.push_back({j.game_id, std::string(), img, true});
                }
                uint64_t one = 1;
                ssize_t w = write(art_evfd, &one, sizeof one);
                (void)w;
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
                for (size_t k = 0; k < recent.size() && k < model.tiles.size(); k++)
                    if (!model.tiles[k] && dq.pending(recent[k].id))
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
    if (can_animate()) {
        pm.enter(now_ms(), visible_rows(), fx_round(tok::motion_homeEnterStagger));
        motion = Motion::Page;
        fstats.begin("t1");
    }
    need_redraw = true;

    wl_display* d = p.display();
    int wl_fd = p.event_fd();
    while (p.ready()) {
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
                if (nav.top() == Screen::Home)
                    need_redraw = true;
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
        if ((want_frame && p.claim()) || bus.data_remains() || (motion == Motion::None && !pending_drops.empty()))
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
        if (!pending_drops.empty() && (motion == Motion::None || drop_budget)) {
            std::vector<ArtDrop> drops;
            if (motion == Motion::None) {
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
            land(child.pid > 0 ? "child" : "hidden");
            settings_page.land();
            scroll_t0 = -1000000000;
        }
        if (frame_req_at >= 0 && fnow - frame_req_at > kFrameWatchdogMs) {
            p.cancel_frame();
            frame_req_at = -1;
            land("watchdog");
            settings_page.land();
            scroll_t0 = -1000000000;
            need_redraw = true;
        }
        if (motion == Motion::Fold && fnow - fold_t0 >= fold_duration(fold)) {
            motion = Motion::None;
            fstats.end("done");
            need_redraw = true;
        }
        if (motion == Motion::Page && !pm.running(fnow)) {
            motion = Motion::None;
            fstats.end("done");
            need_redraw = true;
        }
        if (motion == Motion::Fold && need_redraw) {
            to_dirty = true;
            need_redraw = false;
        }
        if (motion != Motion::None && need_redraw)
            need_redraw = false;
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
