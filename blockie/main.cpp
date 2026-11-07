#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <mutex>
#include <string>
#include <thread>
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
    Game game;
    std::string libretro_folder;
};

struct ArtDrop {
    int index;
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
    std::vector<ScrapeJob> jobs;
    if (lib.open("")) {
        int n = lib.scan();
        std::fprintf(stderr, "blockie-frontend: %d games\n", n);
        for (const auto& sw : lib.systems_with_games())
            model.systems.push_back({sw.first.name, sw.second});
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
                jobs.push_back({(int)i, g, folder});
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
    Launcher::Child child;
    int64_t child_game_id = -1;
    auto redraw = [&]() {
        int64_t tt = now_ms();
        p.begin_frame(tt, p.show_at(tt));
        p.begin_full();
        Canvas c(p.draw_target());
        fonts.begin_frame(-1);
        model.tile_focus = pane == Pane::Details ? tile_focus : -1;
        draw_selector(c, fonts, model, pane);
        p.present(kScreen);
        p.flip();
    };
    redraw();

    auto try_launch = [&]() {
        if (child.pid > 0)
            return;
        if (tile_focus < 0 || tile_focus >= (int)recent.size())
            return;
        const Game& g = recent[tile_focus];
        std::string core;
        for (const auto& s : lib.config())
            if (s.id == g.system_id) {
                core = s.core;
                break;
            }
        if (!launcher.core_installed(core)) {
            std::fprintf(stderr, "blockie-frontend: core %s absent for %s\n", core.c_str(), g.system_id.c_str());
            return;
        }
        LaunchSpec spec;
        spec.system = g.system_id;
        spec.rompath_abs = g.rompath_abs;
        spec.core = core;
        Launcher::Child ch = launcher.spawn(spec);
        if (ch.pid < 0) {
            std::fprintf(stderr, "blockie-frontend: spawn failed for %s\n", g.system_id.c_str());
            return;
        }
        child = ch;
        child_game_id = g.id;
        std::fprintf(stderr, "blockie-frontend: launch %s pid %d\n", g.system_id.c_str(), (int)ch.pid);
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
            try_launch();
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

    std::mutex art_mutex;
    std::vector<ArtDrop> art_drops;
    std::atomic<bool> scrape_stop{false};
    int art_evfd = jobs.empty() ? -1 : eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    std::thread scrape_thread;
    if (!jobs.empty() && art_evfd < 0)
        std::fprintf(stderr, "blockie-frontend: eventfd failed, art scrape disabled\n");
    if (art_evfd >= 0) {
        scrape_thread = std::thread([data_root, jobs = std::move(jobs), art_evfd, &art_mutex, &art_drops,
                                     &scrape_stop]() {
            Library wlib;
            if (!wlib.open(data_root))
                return;
            Scraper sc(wlib, data_root);
            for (const ScrapeJob& j : jobs) {
                if (scrape_stop.load())
                    break;
                ArtOutcome o = sc.art(j.game.id, j.game.system_id, j.libretro_folder, j.game.name, ArtType::Covers);
                if (o.cached && !o.path.empty()) {
                    {
                        std::lock_guard<std::mutex> lk(art_mutex);
                        art_drops.push_back({j.index, o.path});
                    }
                    uint64_t one = 1;
                    ssize_t w = write(art_evfd, &one, sizeof one);
                    (void)w;
                }
            }
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
            for (PadAction a : pad.poll()) {
                std::fprintf(stderr, "blockie-frontend: pad %s\n", action_name(a));
                if (p.activated())
                    apply(a);
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
                if (drp.index < 0 || drp.index >= (int)model.tiles.size())
                    continue;
                DecodedImage dec;
                if (load_png(drp.path, dec) && dec.ok()) {
                    model.tiles[drp.index] = Image::from_portrait(dec.rgb.data(), dec.w, dec.h);
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
    if (scrape_thread.joinable())
        scrape_thread.join();
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
