#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
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

#include "../gfx/pixel.h"
#include "image.h"
#include "input_pad.h"
#include "launch.h"
#include "library.h"
#include "present_wl.h"
#include "scraper.h"
#include "screen_selector.h"

namespace fd {

namespace {

int64_t now_ms()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
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
    }
    return "?";
}

struct ScrapeJob {
    int index;
    int64_t epoch;
    Game game;
    std::string libretro_folder;
};

struct ArtDrop {
    int64_t game_id;
    int index;
    int64_t epoch;
    std::string path;
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

    int64_t t = now_ms();
    p.begin_frame(t, p.show_at(t));
    p.begin_full();
    WaylandPresenter::S s = p.draw_target();
    for (int y = 0; y < kPortraitH; y++)
        fill_px(s.row(y), 0x101418u, kPortraitW);
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

    Library lib;
    SelectorModel model;
    model.focus = 0;
    std::vector<Game> recent;
    std::vector<std::string> sys_ids;
    std::vector<ScrapeJob> jobs;
    if (lib.open("")) {
        int n = lib.scan();
        std::fprintf(stderr, "blockie-frontend: %d games\n", n);
        for (const auto& sw : lib.systems_with_games()) {
            model.systems.push_back({sw.first.name, sw.second});
            sys_ids.push_back(sw.first.id);
        }
        recent = lib.recent_games(8);
        for (size_t i = 0; i < recent.size(); i++) {
            const Game& g = recent[i];
            Image img;
            bool had_art = false;
            for (const auto& m : lib.media(g.id))
                if (m.type == "covers") {
                    DecodedImage d;
                    if (load_png(m.path, d) && d.ok()) {
                        img = Image::from_portrait(d.rgb.data(), d.w, d.h);
                        had_art = true;
                    }
                    break;
                }
            model.tiles.push_back(std::move(img));
            if (!had_art) {
                std::string folder;
                for (const auto& sc : lib.config())
                    if (sc.id == g.system_id) {
                        folder = sc.libretro_folder;
                        break;
                    }
                jobs.push_back({(int)i, 0, g, folder});
            }
        }
    }

    const char* denv = std::getenv("BLOCKIE_DATA");
    std::string data_root = denv && *denv ? denv : "/var/lib/blockie/data";
    while (data_root.size() > 1 && data_root.back() == '/')
        data_root.pop_back();
    Launcher launcher(data_root);

    PadReader pad;
    bool pad_ok = pad.open();
    std::fprintf(stderr, pad_ok ? "blockie-frontend: pad fd %d\n" : "blockie-frontend: no pad\n", pad.fd());

    bool need_redraw = false;
    Pane pane = Pane::Selector;
    int tile_focus = 0;
    bool system_view = false;
    std::vector<Game> sysgames;
    std::vector<Image> recent_hold;
    int sys_focus = 0;
    int sys_scroll = 0;
    std::string sys_name;
    std::unordered_map<int64_t, Image> cover_cache;
    Launcher::Child child;
    int64_t child_game_id = -1;

    std::deque<ScrapeJob> squeue;
    std::mutex qmutex;
    std::unordered_set<int64_t> requested;
    int64_t sys_epoch = 0;
    std::mutex art_mutex;
    std::vector<ArtDrop> art_drops;
    std::atomic<bool> scrape_stop{false};
    int art_evfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    int work_evfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);

    auto enqueue_scrape = [&](const Game& g, int64_t epoch, int index, const std::string& folder) {
        bool added = false;
        {
            std::lock_guard<std::mutex> lk(qmutex);
            if (requested.insert(g.id).second) {
                squeue.push_back({index, epoch, g, folder});
                added = true;
            }
        }
        if (added && work_evfd >= 0) {
            uint64_t one = 1;
            ssize_t w = write(work_evfd, &one, sizeof one);
            (void)w;
        }
    };
    for (const ScrapeJob& j : jobs)
        enqueue_scrape(j.game, j.epoch, j.index, j.libretro_folder);

    auto load_cover = [&](const Game& g) -> Image {
        auto it = cover_cache.find(g.id);
        if (it != cover_cache.end())
            return it->second;
        Image img;
        for (const auto& m : lib.media(g.id))
            if (m.type == "covers") {
                DecodedImage d;
                if (load_png(m.path, d) && d.ok())
                    img = Image::from_portrait(d.rgb.data(), d.w, d.h);
                break;
            }
        if (img.w > 0)
            cover_cache[g.id] = img;
        return img;
    };
    auto redraw = [&]() {
        int64_t tt = now_ms();
        p.begin_frame(tt, p.show_at(tt));
        p.begin_full();
        Canvas c(p.draw_target());
        fonts.begin_frame(-1);
        if (!system_view)
            model.tile_focus = pane == Pane::Details ? tile_focus : -1;
        draw_selector(c, fonts, model, pane, ArtFit::Cover, false, system_view);
        p.present(kScreen);
        p.flip();
    };
    redraw();

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
            if (ran && child_game_id >= 0)
                lib.mark_played(child_game_id, (int64_t)time(nullptr));
            if (!child.cfg_path.empty())
                unlink(child.cfg_path.c_str());
            std::fprintf(stderr, "blockie-frontend: game exited (%s)\n", ran ? "ran" : "not launched");
            child = {};
            child_game_id = -1;
            if (pad_ok) {
                pad.poll();
                if (!pad.is_open())
                    pad_ok = false;
            }
            need_redraw = true;
        }
    };

    const int kCols = 2;
    const int kRows = 4;
    auto refresh_system = [&](bool rebuild) {
        int start = sys_scroll * kCols;
        if (rebuild) {
            model.tiles.clear();
            int end = start + kRows * kCols;
            if (end > (int)sysgames.size())
                end = (int)sysgames.size();
            for (int i = start; i < end; i++)
                model.tiles.push_back(load_cover(sysgames[i]));
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
            if (start < (int)sysgames.size()) {
                std::string folder;
                for (const auto& sc : lib.config())
                    if (sc.id == sysgames[start].system_id) {
                        folder = sc.libretro_folder;
                        break;
                    }
                for (int i = start; i < hi; i++) {
                    const Game& g = sysgames[i];
                    if (cover_cache.count(g.id))
                        continue;
                    if (i >= end && load_cover(g).w > 0)
                        continue;
                    enqueue_scrape(g, sys_epoch, -1, folder);
                }
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
        requested.clear();
        model.section_label.clear();
        model.game_system.clear();
        model.game_title.clear();
        pane = Pane::Selector;
        need_redraw = true;
    };
    auto apply = [&](PadAction a) {
        int cnt = (int)model.systems.size();
        int tiles = (int)model.tiles.size();
        if (pane == Pane::Selector) {
            if (a == PadAction::Up && model.focus > 0) {
                model.focus--;
                need_redraw = true;
            } else if (a == PadAction::Down && cnt > 0 && model.focus < cnt - 1) {
                model.focus++;
                need_redraw = true;
            } else if (a == PadAction::Left && tiles > 0) {
                pane = Pane::Details;
                tile_focus = 0;
                need_redraw = true;
            } else if (a == PadAction::Accept && model.focus >= 0 && model.focus < (int)sys_ids.size()) {
                sysgames = lib.games(sys_ids[model.focus]);
                if (!sysgames.empty()) {
                    sys_name = model.systems[model.focus].name;
                    sys_focus = 0;
                    sys_scroll = 0;
                    system_view = true;
                    sys_epoch++;
                    model.tiles.swap(recent_hold);
                    refresh_system(true);
                    pane = Pane::Details;
                    need_redraw = true;
                }
            }
            return;
        }
        if (system_view) {
            int total = (int)sysgames.size();
            if (a == PadAction::Up && sys_focus - kCols >= 0) {
                sys_focus -= kCols;
                bool scrolled = sys_focus < sys_scroll * kCols;
                if (scrolled)
                    sys_scroll--;
                refresh_system(scrolled);
                need_redraw = true;
            } else if (a == PadAction::Down && sys_focus + kCols < total) {
                sys_focus += kCols;
                bool scrolled = sys_focus >= (sys_scroll + kRows) * kCols;
                if (scrolled)
                    sys_scroll++;
                refresh_system(scrolled);
                need_redraw = true;
            } else if (a == PadAction::Right && sys_focus % kCols < kCols - 1 && sys_focus + 1 < total) {
                sys_focus++;
                refresh_system(false);
                need_redraw = true;
            } else if (a == PadAction::Left) {
                if (sys_focus % kCols > 0) {
                    sys_focus--;
                    refresh_system(false);
                    need_redraw = true;
                } else {
                    exit_system();
                }
            } else if (a == PadAction::Accept && sys_focus >= 0 && sys_focus < total) {
                try_launch(sysgames[sys_focus]);
            } else if (a == PadAction::Back) {
                exit_system();
            }
            return;
        }
        if (a == PadAction::Up && tile_focus - kCols >= 0) {
            tile_focus -= kCols;
            need_redraw = true;
        } else if (a == PadAction::Down && tile_focus + kCols < tiles) {
            tile_focus += kCols;
            need_redraw = true;
        } else if (a == PadAction::Right && tile_focus % kCols < kCols - 1 && tile_focus + 1 < tiles) {
            tile_focus++;
            need_redraw = true;
        } else if (a == PadAction::Left) {
            if (tile_focus % kCols > 0)
                tile_focus--;
            else
                pane = Pane::Selector;
            need_redraw = true;
        } else if (a == PadAction::Accept) {
            if (tile_focus >= 0 && tile_focus < (int)recent.size())
                try_launch(recent[tile_focus]);
        } else if (a == PadAction::Back) {
            pane = Pane::Selector;
            need_redraw = true;
        }
    };

    p.set_key_handler([&](uint32_t sym, bool pressed) {
        if (!pressed)
            return;
        bool have = true;
        PadAction a = PadAction::Up;
#ifdef BLOCKIE_HAVE_XKB
        if (sym == XKB_KEY_Up) a = PadAction::Up;
        else if (sym == XKB_KEY_Down) a = PadAction::Down;
        else if (sym == XKB_KEY_Left) a = PadAction::Left;
        else if (sym == XKB_KEY_Right) a = PadAction::Right;
        else if (sym == XKB_KEY_Return) a = PadAction::Accept;
        else if (sym == XKB_KEY_Escape) a = PadAction::Back;
        else have = false;
#else
        if (sym == KEY_UP) a = PadAction::Up;
        else if (sym == KEY_DOWN) a = PadAction::Down;
        else if (sym == KEY_LEFT) a = PadAction::Left;
        else if (sym == KEY_RIGHT) a = PadAction::Right;
        else if (sym == KEY_ENTER) a = PadAction::Accept;
        else if (sym == KEY_ESC) a = PadAction::Back;
        else have = false;
#endif
        if (!have)
            return;
        std::fprintf(stderr, "blockie-frontend: key %s\n", action_name(a));
        apply(a);
    });

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

    std::fprintf(stderr, "blockie-frontend: scrape worklist %zu\n", jobs.size());
    std::thread scrape_thread;
    if (art_evfd < 0 || work_evfd < 0)
        std::fprintf(stderr, "blockie-frontend: eventfd failed, art scrape disabled\n");
    else {
        scrape_thread = std::thread([data_root, art_evfd, work_evfd, &squeue, &qmutex, &art_mutex, &art_drops,
                                     &scrape_stop]() {
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
                    std::string eff = sc.resolve_name(j.game.system_id, j.game.rompath_abs);
                    if (eff.empty())
                        eff = j.game.name;
                    if (scrape_stop.load())
                        return;
                    ArtOutcome o = sc.art(j.game.id, j.game.system_id, j.libretro_folder, eff, ArtType::Covers);
                    const char* r = o.cached ? "ok" : o.not_found ? "404" : o.transient ? "err" : "miss";
                    std::fprintf(stderr, "blockie-frontend: scrape %s \"%s\" %s %s\n", j.game.system_id.c_str(),
                                 j.game.name.c_str(), r, o.path.c_str());
                    if (o.cached && !o.path.empty()) {
                        {
                            std::lock_guard<std::mutex> lk(art_mutex);
                            art_drops.push_back({j.game.id, j.index, j.epoch, o.path});
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

    wl_display* d = p.display();
    int wl_fd = p.event_fd();
    while (p.ready()) {
        while (wl_display_prepare_read(d) != 0)
            wl_display_dispatch_pending(d);
        wl_display_flush(d);

        struct pollfd fds[4];
        fds[0].fd = wl_fd;
        fds[0].events = POLLIN;
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
        int pr = poll(fds, nfds, -1);
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
            std::vector<PadAction> acts = pad.poll();
            if (p.activated()) {
                for (PadAction a : acts) {
                    std::fprintf(stderr, "blockie-frontend: pad %s\n", action_name(a));
                    apply(a);
                }
            }
            if (!pad.is_open()) {
                pad_ok = false;
                std::fprintf(stderr, "blockie-frontend: pad disconnected\n");
            }
        }

        if (ai >= 0 && (fds[ai].revents & POLLIN)) {
            uint64_t cnt;
            while (read(art_evfd, &cnt, sizeof cnt) == (ssize_t)sizeof cnt) {
            }
            std::vector<ArtDrop> drops;
            {
                std::lock_guard<std::mutex> lk(art_mutex);
                drops.swap(art_drops);
            }
            for (const ArtDrop& drp : drops) {
                if (drp.epoch == 0) {
                    std::vector<Image>& grid = system_view ? recent_hold : model.tiles;
                    if (drp.index < 0 || drp.index >= (int)grid.size())
                        continue;
                    DecodedImage dec;
                    if (load_png(drp.path, dec) && dec.ok()) {
                        grid[drp.index] = Image::from_portrait(dec.rgb.data(), dec.w, dec.h);
                        if (!system_view && child.pid <= 0)
                            need_redraw = true;
                    }
                    continue;
                }
                if (!system_view || drp.epoch != sys_epoch)
                    continue;
                DecodedImage dec;
                if (!(load_png(drp.path, dec) && dec.ok()))
                    continue;
                Image img = Image::from_portrait(dec.rgb.data(), dec.w, dec.h);
                int start = sys_scroll * kCols;
                int end = start + kRows * kCols;
                if (end > (int)sysgames.size())
                    end = (int)sysgames.size();
                int pos = -1;
                for (int i = start; i < end; i++)
                    if (sysgames[i].id == drp.game_id) {
                        pos = i - start;
                        break;
                    }
                cover_cache[drp.game_id] = img;
                if (pos >= 0 && pos < (int)model.tiles.size()) {
                    model.tiles[pos] = img;
                    if (child.pid <= 0)
                        need_redraw = true;
                }
            }
        }

        if (need_redraw) {
            redraw();
            need_redraw = false;
        }
    }
    if (child.pid > 0)
        kill(child.pid, SIGTERM);
    scrape_stop.store(true);
    if (work_evfd >= 0) {
        uint64_t one = 1;
        ssize_t w = write(work_evfd, &one, sizeof one);
        (void)w;
    }
    if (scrape_thread.joinable())
        scrape_thread.join();
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
