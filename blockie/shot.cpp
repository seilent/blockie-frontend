#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <dirent.h>

#include "../gfx/canvas.h"
#include "../gfx/font.h"
#include "../gfx/glyph.h"
#include "../gfx/pixel.h"
#include "../gfx/png.h"
#include "bt_model.h"
#include "fold.h"
#include "home.h"
#include "homeflow.h"
#include "image.h"
#include "ink.h"
#include "motion.h"
#include "nav.h"
#include "pops.h"
#include "repeat.h"
#include "screen_bt.h"
#include "screen_selector.h"
#include "screen_settings.h"
#include "theme.h"
#include "tile_grid.h"

using namespace fd;

namespace {

int64_t frame_ms(int n) { return (int64_t)n * 1000 / 60; }

Art load_tile(const std::string& path, int side)
{
    DecodedImage d;
    if (!load_png(path, d) || !d.ok())
        return nullptr;
    return std::make_shared<const Image>(cover_tile(d, side));
}

std::vector<std::string> pngs_in(const std::string& dir)
{
    std::vector<std::string> names;
    DIR* dp = opendir(dir.c_str());
    if (!dp)
        return names;
    for (struct dirent* e = readdir(dp); e; e = readdir(dp)) {
        std::string n = e->d_name;
        if (n.size() > 4 && n.compare(n.size() - 4, 4, ".png") == 0)
            names.push_back(n);
    }
    closedir(dp);
    std::sort(names.begin(), names.end());
    for (std::string& n : names)
        n = dir + "/" + n;
    return names;
}

bool load_tiles(const std::string& dir, std::vector<Art>& tiles, size_t cap = 8, int side = kTileSide)
{
    tiles.clear();
    if (dir.empty())
        return true;
    for (const std::string& p : pngs_in(dir)) {
        if (tiles.size() >= cap)
            break;
        Art img = load_tile(p, side);
        if (!img) {
            fprintf(stderr, "blockie-shot: cannot decode %s\n", p.c_str());
            return false;
        }
        tiles.push_back(std::move(img));
    }
    if (tiles.empty()) {
        fprintf(stderr, "blockie-shot: no covers in %s\n", dir.c_str());
        return false;
    }
    return true;
}

BtDevice shot_device(const char* mac, const char* alias, bool paired, bool connected, const char* uuid)
{
    BtDevice d;
    std::string tail = mac;
    std::replace(tail.begin(), tail.end(), ':', '_');
    d.path = "/org/bluez/hci0/dev_" + tail;
    d.adapter = "/org/bluez/hci0";
    d.address = mac;
    d.alias = alias;
    d.paired = paired;
    d.connected = connected;
    d.uuids = {uuid};
    return d;
}

int shot_bt(const std::string& out, const std::string& state, Fonts& fonts)
{
    const char* sink = "0000110b-0000-1000-8000-00805f9b34fb";
    const char* hid = "00001124-0000-1000-8000-00805f9b34fb";
    int64_t now = 1000000;
    BtState s;
    if (state != "unavailable") {
        s.up = true;
        s.adapter.path = "/org/bluez/hci0";
        s.adapter.powered = state != "off";
    }
    if (state != "unavailable" && state != "empty")
        s.devices = {shot_device("02:00:00:00:00:01", "Synth Headset", true, true, sink),
                     shot_device("02:00:00:00:00:02", "Synth Buds", true, false, sink),
                     shot_device("02:00:00:00:00:03", "Synth Pad", false, false, hid),
                     shot_device("02:00:00:00:00:04", "Synth Speaker", false, false, sink)};
    if (state == "searching") {
        s.searching = true;
        s.search_end = now + 88000;
    }
    if (state == "confirm") {
        s.pair_target = s.devices[3].path;
        s.prompt = BtPrompt{BtPrompt::Confirm, s.devices[3].path, "", 123456};
    }
    GlyphCache glyphs;
    BtPage page(fonts, glyphs);
    page.open(now);
    page.set_model(s, now);
    if (state == "sheet") {
        page.action(PadAction::Down, false, now);
        page.action(PadAction::Context, false, now);
    }
    page.land(now);
    Buffer buf;
    Canvas c(buf.surface());
    InkLayer ink;
    page.draw(c, ink, now + 1000);
    if (!write_png(out, buf.surface())) {
        fprintf(stderr, "blockie-shot: cannot write %s\n", out.c_str());
        return 1;
    }
    return 0;
}

std::string frame_path(const std::string& dir, int i)
{
    char name[32];
    snprintf(name, sizeof name, "/f%04d.png", i);
    return dir + name;
}

bool read_lines(const std::string& path, std::vector<std::string>& lines)
{
    lines.clear();
    if (path.empty())
        return true;
    std::ifstream in(path);
    if (!in) {
        fprintf(stderr, "blockie-shot: cannot read %s\n", path.c_str());
        return false;
    }
    std::string line;
    while (std::getline(in, line))
        if (!line.empty())
            lines.push_back(line);
    return true;
}

bool action_of(const std::string& k, PadAction& a)
{
    static const struct {
        const char* name;
        PadAction a;
    } kKeys[] = {{"up", PadAction::Up},         {"down", PadAction::Down},     {"left", PadAction::Left},
                 {"right", PadAction::Right},   {"accept", PadAction::Accept}, {"back", PadAction::Back}};
    for (const auto& e : kKeys)
        if (k == e.name) {
            a = e.a;
            return true;
        }
    return false;
}

struct Step {
    std::string key;
    bool down = true;
    int64_t ms = 0;
    int pct = -1;
};

bool parse_script(const std::string& s, std::vector<Step>& out)
{
    size_t i = 0;
    while (i < s.size()) {
        size_t j = s.find(',', i);
        if (j == std::string::npos)
            j = s.size();
        std::string tok = s.substr(i, j - i);
        i = j + 1;
        if (tok.empty())
            continue;
        size_t a = tok.find(':');
        if (a == std::string::npos)
            return false;
        size_t b = tok.find(':', a + 1);
        if (b == std::string::npos)
            return false;
        Step st;
        st.key = tok.substr(0, a);
        PadAction pa;
        if (st.key != "ret" && st.key != "theme" && !action_of(st.key, pa))
            return false;
        std::string ud = tok.substr(a + 1, b - a - 1);
        if (ud != "d" && ud != "u")
            return false;
        st.down = ud == "d";
        std::string t = tok.substr(b + 1);
        if (!t.empty() && t.back() == '%')
            st.pct = atoi(t.c_str());
        else
            st.ms = atoll(t.c_str());
        out.push_back(st);
    }
    return true;
}

struct DriveOpts {
    bool on = false;
    int recent_n = 0;
    int sys_games = 12;
    bool noart = false;
    std::string title0;
    std::string titles;
    std::string recent_titles;
    std::string recent_system = "Synth";
    bool big = false;
    SlotMap slots = SlotMap::RowMajor;
    LeftMove left = LeftMove::Cells;
    int unit = 5;
    bool t9_edge = false;
    bool bar_slot = false;
    bool grid_band = false;
    bool startup = false;
    int64_t arrive = -1;
    std::string script;
    int64_t from = 0;
    int64_t at = -1;
    int tail = 0;
    double cadence = 0;
    bool repeat_covers = false;
};

constexpr const char* kNames[] = {"GB", "GBC", "NES", "SNES", "Genesis", "GBA", "PS1", "N64", "PSP", "DC"};
constexpr int kCounts[] = {28, 19, 63, 47, 35, 52, 31, 22, 14, 9};
constexpr int kRecentIds = 900000;
constexpr int kSystemIds = 1000;
constexpr int kDriveGamesMax = 99;
constexpr int kCoverCap = 40;
constexpr int64_t kDriveCapMs = 20000;
constexpr int64_t kNever = std::numeric_limits<int64_t>::max();

bool same_px(const Surface& a, const Surface& b)
{
    for (int y = 0; y < a.h; y++)
        if (!std::equal(a.row(y), a.row(y) + a.w, b.row(y)))
            return false;
    return true;
}

void copy_surface(const Surface& src, const Surface& dst)
{
    for (int y = 0; y < dst.h; y++)
        copy_px(dst.row(y), src.row(y), dst.w);
}

FlowOpts flow_opts(const DriveOpts& o)
{
    FlowOpts f;
    f.big = o.big;
    f.slots = o.slots;
    f.left = o.left;
    f.unit = o.unit;
    f.t9_edge = o.t9_edge;
    f.bar_slot = o.bar_slot;
    f.grid_band = o.grid_band;
    return f;
}

class Drive {
public:
    Drive(Fonts& fonts, const DriveOpts& o) : flow(fonts, flow_opts(o)), fonts_(fonts), o_(o) {}

    HomeFlow flow;
    std::vector<std::vector<HomeGame>> games;
    std::vector<HomeGame> recent_games;
    std::vector<Art> sys_covers;
    std::vector<Art> recent_covers;
    std::unordered_map<int64_t, Art> art;

    bool setup()
    {
        if (!parse_script(o_.script, steps_)) {
            fprintf(stderr, "blockie-shot: bad --script %s\n", o_.script.c_str());
            return false;
        }
        flow.games = [this](int sys) {
            return sys >= 0 && sys < (int)games.size() ? games[sys] : std::vector<HomeGame>();
        };
        flow.launch_game = [this](const HomeGame& g) {
            played_ = g;
            playing_ = true;
            flow.land_all(now_, "child");
            return true;
        };
        flow.draw_page = [this](Canvas& c, Screen, int64_t) {
            SettingsView sv;
            sv.light = theme().light;
            sv.sheet_focus = theme().light ? 1 : 0;
            draw_settings(c, fonts_, ink_, sv, theme());
        };
        flow.rearm = [this](int64_t now) { repeat_.rearm(now); };
        flow.last_frame = [this](const Surface& s) { copy_surface(last_.surface(), s); };
        flow.setup();
        flow.ctl.can_animate = []() { return true; };
        flow.ctl.landed = [this](const char* what, const char* why) {
            fprintf(stderr, "blockie-shot: %lld ms %s landed (%s)\n", (long long)now_, what, why);
            bool by_input = !in_frame_ && !flow.ctl.navigating();
            if (by_input && std::strcmp(why, "end") == 0 && std::strcmp(what, "startup") != 0)
                end_check_input();
        };
        flow.home.load = [this](const HomeGame& g) -> Art {
            if (late_.count(g.id) && !arrived_)
                return nullptr;
            auto it = art.find(g.id);
            return it == art.end() ? nullptr : it->second;
        };
        std::vector<HomeGame> list(recent_games.begin(),
                                   recent_games.begin() + std::min(recent_games.size(), (size_t)o_.recent_n));
        if (o_.arrive >= 0)
            for (const HomeGame& g : list)
                late_.insert(g.id);
        flow.home.deal_recent(flow.model, list);
        return true;
    }

    int run(const std::string& out, const std::string& frames_dir)
    {
        set_now(0);
        if (o_.startup)
            flow.startup(0);
        flow.draw(last_.surface(), flow.model, flow.nav, false, flow.nav.pane, kScreen);
        FILE* side = nullptr;
        if (!frames_dir.empty() && !(side = fopen((frames_dir + "/frames.txt").c_str(), "w"))) {
            fprintf(stderr, "blockie-shot: cannot write %s/frames.txt\n", frames_dir.c_str());
            return 1;
        }
        int written = 0;
        int tail = o_.tail;
        bool was_moving = false;
        for (int n = 0;; n++) {
            int64_t tf = frame_time(n);
            bool stop = o_.at >= 0 && tf >= o_.at;
            if (stop)
                tf = o_.at;
            advance(tf);
            set_now(tf);
            fonts_.next_frame();
            MotionCtl& ctl = flow.ctl;
            bool was_fold = ctl.active();
            bool was_details = ctl.details();
            FoldKind was_kind = ctl.kind();
            bool step_in = flow.take_step_in();
            bool motion = was_moving || flow.animating(tf);
            Rect art_r = flow.take_art_rect();
            copy_surface(last_.surface(), prev_.surface());
            in_frame_ = true;
            bool more = flow.frame(tf, last_.surface());
            in_frame_ = false;
            was_moving = more;
            int step = 0, ok = input_ok_;
            input_ok_ = 1;
            if (step_in) {
                step = 1;
                Buffer ref;
                flow.draw(ref.surface(), flow.kept(), flow.kept_focus(), true, Pane::Details, kScreen);
                ok = same_px(ref.surface(), last_.surface());
            }
            if (was_fold && !ctl.active() && was_kind != FoldKind::Startup) {
                bool collapse_step = was_details && was_kind == FoldKind::Collapse;
                if (collapse_step) {
                    step = 1;
                    Buffer ref;
                    flow.draw(ref.surface(), flow.model, flow.nav, false, Pane::Details, kScreen);
                    ok = same_px(ref.surface(), prev_.surface());
                } else {
                    ok = same_px(prev_.surface(), last_.surface());
                }
                fprintf(stderr, "blockie-shot: %lld ms %s end check %s\n", (long long)tf, fold_kind_name(was_kind),
                        ok ? (collapse_step ? "the last composed frame equals the destination with Details focus"
                                            : "the composite at T equals a fresh render")
                           : "FAILED");
            }
            if (!frames_dir.empty() && tf >= o_.from) {
                std::string png = frame_path(frames_dir, written);
                if (!write_png(png, last_.surface())) {
                    fprintf(stderr, "blockie-shot: cannot write %s\n", png.c_str());
                    fclose(side);
                    return 1;
                }
                if (fprintf(side, "%d %lld %d %d %d %d %d %d %d %d\n", written, (long long)tf, flow.boundary(tf),
                            motion ? 1 : 0, step, ok, art_r.x0, art_r.y0, art_r.x1, art_r.y1) < 0) {
                    fprintf(stderr, "blockie-shot: cannot write %s/frames.txt\n", frames_dir.c_str());
                    fclose(side);
                    return 1;
                }
                written++;
            }
            if (stop)
                break;
            if (tf > kDriveCapMs) {
                fprintf(stderr, "blockie-shot: the drive passed its %lld ms cap before it settled\n",
                        (long long)kDriveCapMs);
                if (side)
                    fclose(side);
                return 1;
            }
            bool pending = next_ < steps_.size() || repeat_.active() || (o_.arrive >= 0 && !arrived_);
            if (!pending && !more && o_.at < 0) {
                if (tail-- > 0)
                    continue;
                break;
            }
        }
        if (side && fclose(side) != 0) {
            fprintf(stderr, "blockie-shot: cannot close %s/frames.txt\n", frames_dir.c_str());
            return 1;
        }
        if (!out.empty() && !write_png(out, last_.surface())) {
            fprintf(stderr, "blockie-shot: cannot write %s\n", out.c_str());
            return 1;
        }
        fprintf(stderr, "blockie-shot: drive %d frames to %lld ms\n", written, (long long)now_);
        return 0;
    }

private:
    void end_check_input()
    {
        MotionCtl& ctl = flow.ctl;
        Buffer end, ref;
        if (!ctl.compose_end(end.surface()))
            return;
        bool collapse_step = ctl.details_play() && ctl.kind() == FoldKind::Collapse;
        flow.draw(ref.surface(), flow.model, flow.nav, false, collapse_step ? Pane::Details : flow.nav.pane, kScreen);
        bool ok = same_px(end.surface(), ref.surface());
        if (!ok)
            input_ok_ = 0;
        fprintf(stderr, "blockie-shot: %lld ms %s end check on an input %s\n", (long long)now_,
                fold_kind_name(ctl.kind()),
                ok ? (collapse_step ? "the end pose equals the destination with Details focus"
                                    : "the end pose equals a fresh render")
                   : "FAILED");
    }

    void set_now(int64_t t)
    {
        now_ = t;
        flow.at(t);
    }

    int64_t frame_time(int n) const
    {
        return o_.cadence > 0 ? (int64_t)(n * o_.cadence) : frame_ms(n);
    }

    int64_t snap(int64_t ms) const
    {
        int n = std::max(0, (int)(o_.cadence > 0 ? ms / o_.cadence : ms * 60 / 1000) - 1);
        while (frame_time(n) < ms)
            n++;
        return frame_time(n);
    }

    void game_returned()
    {
        if (!playing_)
            return;
        playing_ = false;
        flow.game_ran(played_first(flow.home.recent, played_, flow.home.recent_cap));
    }

    void input(PadAction a, bool rep) { flow.input(a, rep, now_); }

    int64_t step_time(size_t i)
    {
        Step& st = steps_[i];
        if (st.pct >= 0) {
            st.ms = flow.ctl.t0() + flow.ctl.run_length() * st.pct / 100;
            st.pct = -1;
        }
        if (i > 0)
            st.ms = std::max(st.ms, steps_[i - 1].ms);
        st.ms = snap(st.ms);
        return st.ms;
    }

    void apply(const Step& st)
    {
        if (st.key == "ret") {
            if (st.down)
                game_returned();
            return;
        }
        if (st.key == "theme") {
            if (st.down) {
                set_theme_light(!theme().light);
                flow.theme_changed();
            }
            return;
        }
        PadAction a;
        if (!action_of(st.key, a))
            return;
        if (st.down) {
            input(a, false);
            if (is_direction(a) && !playing_)
                repeat_.press(a, kFromKeys, now_);
        } else if (is_direction(a)) {
            repeat_.release(a, kFromKeys, now_);
        }
    }

    void arrive()
    {
        arrived_ = true;
        for (int64_t id : late_) {
            auto it = art.find(id);
            if (it != art.end())
                flow.art(id, 0, it->second);
        }
    }

    void advance(int64_t tf)
    {
        for (;;) {
            int64_t te = next_ < steps_.size() ? step_time(next_) : kNever;
            int64_t tr = repeat_.active() ? repeat_.deadline() : kNever;
            int64_t ta = o_.arrive >= 0 && !arrived_ ? o_.arrive : kNever;
            int64_t t = std::min({te, tr, ta});
            if (t > tf || t == kNever)
                return;
            set_now(t);
            if (t == ta) {
                arrive();
                continue;
            }
            if (te <= tr) {
                apply(steps_[next_++]);
                continue;
            }
            PadAction a = PadAction::Up;
            if (repeat_.due(t, a))
                input(a, true);
        }
    }

    Fonts& fonts_;
    DriveOpts o_;
    Repeat repeat_;
    InkLayer ink_;
    Buffer last_;
    Buffer prev_;
    int64_t now_ = 0;
    bool playing_ = false;
    HomeGame played_;
    std::vector<Step> steps_;
    size_t next_ = 0;
    std::unordered_set<int64_t> late_;
    bool arrived_ = false;
    bool in_frame_ = false;
    int input_ok_ = 1;
};

int run_drive(Fonts& fonts, const DriveOpts& o, const std::string& out, const std::string& frames_dir,
              int systems, bool items, const std::string& bt_sub, int sys_focus, const std::string& tiles_dir,
              const std::string& recent_dir)
{
    auto d = std::make_unique<Drive>(fonts, o);
    if (!o.noart && !load_tiles(tiles_dir, d->sys_covers, kCoverCap, o.big ? kBigTileSide : kTileSide))
        return 1;
    std::vector<std::string> titles, rtitles;
    if (!load_tiles(recent_dir, d->recent_covers, kCoverCap) || !read_lines(o.titles, titles) ||
        !read_lines(o.recent_titles, rtitles))
        return 1;
    systems = std::max(1, std::min(systems, 10));
    int focus = std::clamp(sys_focus, 0, systems + (items ? 2 : 0) - 1);
    if (!o.repeat_covers) {
        bool enters = o.script.find("accept") != std::string::npos;
        int games_n = focus < systems ? std::min(kDriveGamesMax, o.sys_games > 0 ? o.sys_games : kCounts[focus]) : 0;
        int shown = enters ? std::min(games_n, kCoverCap) : 0;
        int played = std::min(o.recent_n, kGridColsWide * Nav::kRows);
        if (!d->sys_covers.empty() && (int)d->sys_covers.size() < shown) {
            fprintf(stderr, "blockie-shot: %zu covers for %d games, pass --repeat-covers to reuse them\n",
                    d->sys_covers.size(), shown);
            return 1;
        }
        if (!d->recent_covers.empty() && (int)d->recent_covers.size() < played) {
            fprintf(stderr, "blockie-shot: %zu recent covers for %d played, pass --repeat-covers to reuse them\n",
                    d->recent_covers.size(), played);
            return 1;
        }
    }
    for (int s = 0; s < systems; s++) {
        int count = std::min(kDriveGamesMax, s == focus && o.sys_games > 0 ? o.sys_games : kCounts[s]);
        std::vector<HomeGame> list;
        for (int i = 0; i < count; i++) {
            HomeGame g;
            g.id = 1 + (int64_t)(s + 1) * kSystemIds + i;
            g.name = titles.empty() ? "Synth Game " + std::to_string(i + 1) : titles[(size_t)i % titles.size()];
            if (i == 0 && !o.title0.empty())
                g.name = o.title0;
            g.system = kNames[s];
            if (s == focus && !d->sys_covers.empty())
                d->art[g.id] = d->sys_covers[(size_t)i % d->sys_covers.size()];
            list.push_back(g);
        }
        d->games.push_back(list);
        d->flow.model.systems.push_back({kNames[s], count, ""});
        d->flow.kinds.push_back(MenuKind::System);
    }
    if (items) {
        d->flow.model.systems.push_back({"bluetooth", 0, bt_sub.empty() ? "not connected" : bt_sub});
        d->flow.kinds.push_back(MenuKind::Bluetooth);
        d->flow.model.systems.push_back({"settings", 0, "display"});
        d->flow.kinds.push_back(MenuKind::Settings);
    }
    for (int k = 0; k < kGridColsWide * Nav::kRows; k++) {
        HomeGame g;
        g.id = kRecentIds + k;
        g.name = rtitles.empty() ? "Synth Recent " + std::to_string(k + 1) : rtitles[(size_t)k % rtitles.size()];
        g.system = o.recent_system;
        if (!d->recent_covers.empty())
            d->art[g.id] = d->recent_covers[(size_t)k % d->recent_covers.size()];
        d->recent_games.push_back(g);
    }
    d->flow.nav.focus = focus;
    if (!d->setup())
        return 2;
    return d->run(out, frames_dir);
}

bool load_frame(const std::string& dir, int n, DecodedImage& out)
{
    std::string p = frame_path(dir, n);
    if (load_png(p, out) && out.ok())
        return true;
    printf("FAIL cannot read %s\n", p.c_str());
    return false;
}

int run_pops(const std::string& dir, bool lead, const std::string& summary, const std::string& name)
{
    std::vector<PopRow> rows;
    {
        std::ifstream in(dir + "/frames.txt");
        PopRow r;
        long long t = 0;
        int motion = 0, step = 0, ok = 1;
        while (in >> r.n >> t >> r.b >> motion >> step >> ok >> r.art.x0 >> r.art.y0 >> r.art.x1 >> r.art.y1) {
            r.t = t;
            r.motion = motion != 0;
            r.step = step != 0;
            r.ok = ok != 0;
            rows.push_back(r);
        }
    }
    if (rows.empty()) {
        printf("FAIL no frames in %s\n", dir.c_str());
        return 1;
    }
    std::vector<PopDiff> diffs(rows.size());
    DecodedImage f0, f1, f2;
    if (!load_frame(dir, rows[0].n, f0) || (rows.size() > 1 && !load_frame(dir, rows[1].n, f1)))
        return 1;
    for (size_t i = 1; i < rows.size(); i++) {
        if (f1.w != f0.w || f1.h != f0.h) {
            printf("FAIL frame %d changes size\n", rows[i].n);
            return 1;
        }
        pop_count(f0.rgb.data(), f1.rgb.data(), f0.w, f0.h, rows[i].b, rows[i].art, diffs[i].d, diffs[i].e,
                  &diffs[i].same);
        if (i + 1 < rows.size()) {
            if (!load_frame(dir, rows[i + 1].n, f2))
                return 1;
            if (f2.w != f0.w || f2.h != f0.h) {
                printf("FAIL frame %d changes size\n", rows[i + 1].n);
                return 1;
            }
            pop_count(f0.rgb.data(), f2.rgb.data(), f0.w, f0.h, rows[i].b, rows[i].art, diffs[i].skip,
                      diffs[i].e_skip, nullptr);
        }
        std::swap(f0, f1);
        std::swap(f1, f2);
    }
    std::vector<std::string> lines;
    int fails = pop_judge(rows, diffs, lead, lines);
    for (const std::string& l : lines)
        printf("%s\n", l.c_str());
    if (!summary.empty()) {
        FILE* fs = fopen(summary.c_str(), "a");
        if (!fs) {
            printf("FAIL cannot open %s\n", summary.c_str());
            return 1;
        }
        for (const std::string& l : lines)
            if (l.find("FAIL") != std::string::npos || &l == &lines.back())
                fprintf(fs, "%s: %s\n", name.c_str(), l.c_str());
        if (fclose(fs) != 0) {
            printf("FAIL cannot write %s\n", summary.c_str());
            return 1;
        }
    }
    return fails ? 1 : 0;
}

}

int main(int argc, char** argv)
{
    std::string out;
    std::string font_dir;
    std::string tiles_dir;
    std::string recent_dir;
    std::string header;
    std::string page;
    std::string page_flip;
    std::string frames_dir;
    std::string bt_sub;
    std::string bt_state;
    int tile_focus = -1;
    int sys_focus = 0;
    int systems = 6;
    int menu_top = 0;
    int sheet_focus = -1;
    bool items = false;
    bool sheet = false;
    bool light = false;
    Pane focus = Pane::Selector;
    bool scatter = false;
    bool detail = false;
    std::string game_system;
    std::string game_title;
    std::vector<std::string> fallback;
    DriveOpts drive;
    std::string pops_dir;
    bool pop_lead = true;
    std::string pop_summary;
    std::string pop_case;
    bool menu_top_set = false;
    if (const char* env = getenv("BLOCKIE_FONTS"))
        font_dir = env;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--focus" && i + 1 < argc)
            focus = std::string(argv[++i]) == "details" ? Pane::Details : Pane::Selector;
        else if (a == "--fonts" && i + 1 < argc)
            font_dir = argv[++i];
        else if (a == "--tiles" && i + 1 < argc)
            tiles_dir = argv[++i];
        else if (a == "--recent" && i + 1 < argc)
            recent_dir = argv[++i];
        else if (a == "--header" && i + 1 < argc)
            header = argv[++i];
        else if (a == "--tilefocus" && i + 1 < argc)
            tile_focus = atoi(argv[++i]);
        else if (a == "--sysfocus" && i + 1 < argc)
            sys_focus = atoi(argv[++i]);
        else if (a == "--systems" && i + 1 < argc)
            systems = atoi(argv[++i]);
        else if (a == "--menutop" && i + 1 < argc) {
            menu_top = atoi(argv[++i]);
            menu_top_set = true;
        }
        else if (a == "--items")
            items = true;
        else if (a == "--btsub" && i + 1 < argc)
            bt_sub = argv[++i];
        else if (a == "--bt" && i + 1 < argc)
            bt_state = argv[++i];
        else if (a == "--light")
            light = true;
        else if (a == "--page" && i + 1 < argc)
            page = argv[++i];
        else if (a == "--sheet")
            sheet = true;
        else if (a == "--sheetfocus" && i + 1 < argc)
            sheet_focus = atoi(argv[++i]);
        else if (a == "--pageflip" && i + 1 < argc)
            page_flip = argv[++i];
        else if (a == "--frames" && i + 1 < argc)
            frames_dir = argv[++i];
        else if (a == "--scatter")
            scatter = true;
        else if (a == "--fallback" && i + 1 < argc)
            fallback.push_back(argv[++i]);
        else if (a == "--drive")
            drive.on = true;
        else if (a == "--recentn" && i + 1 < argc)
            drive.recent_n = atoi(argv[++i]);
        else if (a == "--sysgames" && i + 1 < argc)
            drive.sys_games = atoi(argv[++i]);
        else if (a == "--noart")
            drive.noart = true;
        else if (a == "--title0" && i + 1 < argc)
            drive.title0 = argv[++i];
        else if (a == "--titles" && i + 1 < argc)
            drive.titles = argv[++i];
        else if (a == "--recenttitles" && i + 1 < argc)
            drive.recent_titles = argv[++i];
        else if (a == "--recentsys" && i + 1 < argc)
            drive.recent_system = argv[++i];
        else if (a == "--grid" && i + 1 < argc)
            drive.big = std::string(argv[++i]) == "g2";
        else if (a == "--slots" && i + 1 < argc)
            drive.slots = std::string(argv[++i]) == "c" ? SlotMap::RecentC : SlotMap::RowMajor;
        else if (a == "--left" && i + 1 < argc)
            drive.left = std::string(argv[++i]) == "l0" ? LeftMove::Fade : LeftMove::Cells;
        else if (a == "--unit" && i + 1 < argc)
            drive.unit = atoi(argv[++i]) == 3 ? 3 : 5;
        else if (a == "--t9clip" && i + 1 < argc)
            drive.t9_edge = std::string(argv[++i]) == "edge";
        else if (a == "--tail" && i + 1 < argc)
            drive.tail = std::max(0, atoi(argv[++i]));
        else if (a == "--cadence" && i + 1 < argc)
            drive.cadence = atof(argv[++i]);
        else if (a == "--repeat-covers")
            drive.repeat_covers = true;
        else if (a == "--pops" && i + 1 < argc)
            pops_dir = argv[++i];
        else if (a == "--no-lead")
            pop_lead = false;
        else if (a == "--summary" && i + 1 < argc)
            pop_summary = argv[++i];
        else if (a == "--case" && i + 1 < argc)
            pop_case = argv[++i];
        else if (a == "--bar" && i + 1 < argc)
            drive.bar_slot = std::string(argv[++i]) == "slot";
        else if (a == "--gridscroll" && i + 1 < argc) {
            if (std::string(argv[++i]) != "band") {
                fprintf(stderr, "blockie-shot: --gridscroll takes band\n");
                return 2;
            }
            drive.grid_band = true;
        }
        else if (a == "--startup")
            drive.startup = true;
        else if (a == "--arrive" && i + 1 < argc)
            drive.arrive = atoll(argv[++i]);
        else if (a == "--script" && i + 1 < argc)
            drive.script = argv[++i];
        else if (a == "--from" && i + 1 < argc)
            drive.from = atoll(argv[++i]);
        else if (a == "--at" && i + 1 < argc)
            drive.at = atoll(argv[++i]);
        else if (a == "--gamedetail" && i + 1 < argc) {
            std::string v = argv[++i];
            size_t bar = v.find('|');
            game_system = bar == std::string::npos ? std::string() : v.substr(0, bar);
            game_title = bar == std::string::npos ? v : v.substr(bar + 1);
            detail = true;
            focus = Pane::Details;
        }
        else if (out.empty())
            out = a;
        else {
            fprintf(stderr, "blockie-shot: unexpected argument %s\n", a.c_str());
            return 2;
        }
    }
    if (!pops_dir.empty())
        return run_pops(pops_dir, pop_lead, pop_summary, pop_case.empty() ? pops_dir : pop_case);
    if (drive.on && menu_top_set) {
        fprintf(stderr, "blockie-shot: --menutop is not a --drive option, the drive keeps the focus in view\n");
        return 2;
    }
    bool sequence = !page_flip.empty();
    bool driven = drive.on && (!out.empty() || !frames_dir.empty());
    if (!driven && ((out.empty() && !sequence) || (sequence && frames_dir.empty()))) {
        fprintf(stderr,
                "usage: blockie-shot OUT.png [--focus selector|details] [--fonts DIR] [--tiles DIR] [--header TEXT]"
                " [--tilefocus N] [--sysfocus N] [--systems N] [--menutop N] [--items] [--btsub TEXT]"
                " [--light] [--page settings] [--sheet] [--sheetfocus N] [--scatter] [--gamedetail SYSTEM|TITLE]"
                " [--bt unavailable|off|empty|list|searching|sheet|confirm]\n"
                "       blockie-shot --pageflip push|pop --frames DIR [same options]\n"
                "       blockie-shot --drive [OUT.png] [--frames DIR] [--script KEY:d|u:MS|PCT%%,...] [--at MS] [--from MS]"
                " [--recentn N] [--sysgames N] [--noart] [--title0 TEXT] [--titles FILE] [--recenttitles FILE]"
                " [--recentsys NAME] [--grid g1|g2] [--slots a|c] [--left l1|l0] [--unit 5|3] [--t9clip pane|edge]"
                " [--bar content|slot] [--gridscroll band] [--startup] [--arrive MS]"
                " [--tail N] [--cadence MS] [--repeat-covers] [--fallback FONT] [same options]\n"
                "       blockie-shot --pops FRAMES_DIR [--no-lead] [--summary FILE] [--case NAME]\n");
        return 2;
    }
    if (font_dir.empty())
        font_dir = "fonts";
    set_theme_light(light);

    Fonts fonts;
    if (!fonts.open(font_dir, fallback)) {
        fprintf(stderr, "blockie-shot: fonts not found in %s\n", font_dir.c_str());
        return 1;
    }
    fonts.begin_frame(-1);
    if (!bt_state.empty())
        return shot_bt(out, bt_state, fonts);
    if (drive.on)
        return run_drive(fonts, drive, out, frames_dir, systems, items, bt_sub, sys_focus, tiles_dir, recent_dir);

    SelectorModel model;
    systems = std::max(1, std::min(systems, 10));
    for (int i = 0; i < systems; i++)
        model.systems.push_back({kNames[i], kCounts[i], ""});
    if (items) {
        model.systems.push_back({"bluetooth", 0, bt_sub.empty() ? "not connected" : bt_sub});
        model.systems.push_back({"settings", 0, "display"});
    }
    int count = (int)model.systems.size();
    model.focus = sys_focus < 0 || sys_focus >= count ? 0 : sys_focus;
    model.menu_top_px = menu_top * kMenuPitchPx;

    if (!load_tiles(tiles_dir, model.tiles))
        return 1;

    model.section_label = header;
    if (focus == Pane::Details && tile_focus < 0)
        tile_focus = 0;
    model.tile_focus = tile_focus;
    model.game_system = game_system;
    model.game_title = game_title;

    InkLayer ink;
    SettingsView sv;
    sv.light = light;
    sv.sheet = sheet ? kOne : 0;
    sv.sheet_focus = sheet_focus >= 0 ? sheet_focus : (light ? 1 : 0);

    if (!page_flip.empty()) {
        bool push = page_flip == "push";
        Buffer menub;
        Buffer pageb;
        Canvas mc(menub.surface());
        draw_selector(mc, fonts, model, Pane::Selector, scatter, false);
        Canvas pc(pageb.surface());
        draw_settings(pc, fonts, ink, sv, theme());
        auto plan = std::make_shared<const FoldPlan>(fold_page(model.focus - menu_top));
        int64_t dur = fold_duration(*plan);
        Surface shown = push ? menub.surface() : pageb.surface();
        Surface dest = push ? pageb.surface() : menub.surface();
        MotionCtl ctl;
        ctl.render = [&](const Surface& s, Rect) { copy_surface(dest, s); };
        ctl.last = [&](const Surface& s) { copy_surface(shown, s); };
        ctl.render_kept = ctl.last;
        FoldPlay play;
        play.duration = dur;
        play.forward = [plan](const Surface& a, const Surface& b, const Surface& dst, int64_t t, int64_t) {
            fold_compose(a, b, dst, *plan, t);
            return dst.rect();
        };
        ctl.start(push ? FoldKind::Push : FoldKind::Pop, play, 0);
        Buffer outb;
        int n = 0;
        for (;; n++) {
            int64_t t = frame_ms(n);
            ctl.frame(t, outb.surface());
            if (!write_png(frame_path(frames_dir, n), outb.surface()))
                return 1;
            if (t >= dur)
                break;
        }
        fprintf(stderr, "blockie-shot: page %s %lld ms %d frames\n", page_flip.c_str(), (long long)dur, n + 1);
        return 0;
    }

    Buffer buf;
    Canvas c(buf.surface());
    if (page == "settings")
        draw_settings(c, fonts, ink, sv, theme());
    else
        draw_selector(c, fonts, model, focus, scatter, detail);

    if (!write_png(out, buf.surface())) {
        fprintf(stderr, "blockie-shot: cannot write %s\n", out.c_str());
        return 1;
    }
    return 0;
}
