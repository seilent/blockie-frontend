#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <dirent.h>
#include <strings.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../core/curves.h"
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
#include "launch.h"
#include "launchflow.h"
#include "layer_mem.h"
#include "motion.h"
#include "nav.h"
#include "pops.h"
#include "powermenu.h"
#include "repeat.h"
#include "screen_bt.h"
#include "screen_selector.h"
#include "screen_settings.h"
#include "sysinfo.h"
#include "theme.h"
#include "tile_grid.h"
#include "toast_strip.h"
#include "zones.h"

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

BtState shot_bt_state(const std::string& state, int64_t now)
{
    const char* sink = "0000110b-0000-1000-8000-00805f9b34fb";
    const char* hid = "00001124-0000-1000-8000-00805f9b34fb";
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
    return s;
}

int shot_bt(const std::string& out, const std::string& state, Fonts& fonts)
{
    int64_t now = 1000000;
    BtState s = shot_bt_state(state, now);
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

std::vector<std::string> comma_list(const std::string& s)
{
    std::vector<std::string> out;
    std::istringstream in(s);
    std::string v;
    while (std::getline(in, v, ','))
        if (!v.empty())
            out.push_back(v);
    return out;
}

struct KeyName {
    const char* name;
    PadAction a;
};

constexpr KeyName kKeys[] = {{"up", PadAction::Up},           {"down", PadAction::Down},     {"left", PadAction::Left},
                             {"right", PadAction::Right},     {"accept", PadAction::Accept}, {"back", PadAction::Back},
                             {"context", PadAction::Context}, {"prev", PadAction::Prev},     {"next", PadAction::Next},
                             {"start", PadAction::Start}};

bool action_of(const std::string& k, PadAction& a)
{
    for (const KeyName& e : kKeys)
        if (k == e.name) {
            a = e.a;
            return true;
        }
    return false;
}

const char* key_name(PadAction a)
{
    for (const KeyName& e : kKeys)
        if (e.a == a)
            return e.name;
    return "?";
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
        if (st.key != "ret" && st.key != "cover" && st.key != "theme" && st.key != "fail" && st.key != "missing" &&
            st.key != "term" && !action_of(st.key, pa))
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
    if (std::none_of(out.begin(), out.end(), [](const Step& st) { return st.pct >= 0; }))
        std::stable_sort(out.begin(), out.end(), [](const Step& x, const Step& y) { return x.ms < y.ms; });
    return true;
}

struct PageOpts {
    std::string pivot;
    std::vector<std::string> sets;
    std::vector<std::string> hides;
    std::string battery = "62:Charging";
    std::string volume = "35";
    std::string radio = "on";
    std::string storage = "82:106:4.3:7.8";
    std::string about = "|0a1b2c3|5.10.233-blockie-test";
    std::string btsub = "not connected";
    std::string toast;
    std::string zone;
    bool zone_set = false;
    bool zone_missing = false;
    std::string zonetab;
    int rows = 0;
    int focus = 0;
    bool sheet = false;
    std::string sheet_arg;
    int sheet_focus = -1;
};

std::vector<std::string> split(const std::string& s, char sep)
{
    std::vector<std::string> out;
    size_t at = 0;
    for (;;) {
        size_t e = s.find(sep, at);
        out.push_back(s.substr(at, e == std::string::npos ? std::string::npos : e - at));
        if (e == std::string::npos)
            return out;
        at = e + 1;
    }
}

uint64_t gb_bytes(const std::string& s) { return (uint64_t)(atof(s.c_str()) * (double)(1ull << 30)); }

bool page_fixtures(SettingsPage& sp, const PageOpts& o)
{
    std::vector<std::pair<std::string, std::string>> keys = {
        {kBrightnessRow, "40"},      {"sound.volume_step", "5"},       {"controls.rumble", "100"},
        {"games.shader", "true"},    {"games.integer_scale", "false"}, {"games.resume", "false"},
        {"games.rewind", "false"},   {"power.screen_off", "300"},      {"android.gpu", "mali"},
        {"android.perf", "policy"}};
    for (const std::string& a : o.sets) {
        size_t eq = a.find('=');
        if (eq == std::string::npos || eq == 0) {
            fprintf(stderr, "blockie-shot: --set takes ID=VALUE, not %s\n", a.c_str());
            return false;
        }
        bool found = false;
        for (auto& k : keys)
            if (k.first == a.substr(0, eq)) {
                k.second = a.substr(eq + 1);
                found = true;
            }
        if (!found) {
            fprintf(stderr, "blockie-shot: --set %s is not a setd row\n", a.c_str());
            return false;
        }
    }
    for (const auto& k : keys) {
        SettingsRowState st;
        st.shown = true;
        st.value = k.second;
        sp.set_row(k.first, st, 0);
    }
    std::vector<std::string> v = split(o.volume, ':');
    if (o.volume == "fail")
        sp.set_row(kVolumeRow, volume_row(false, 0, false), 0);
    else
        sp.set_row(kVolumeRow, volume_row(true, atoi(v[0].c_str()), v.size() > 1 && v[1] == "muted"), 0);
    if (o.radio != "on" && o.radio != "off" && o.radio != "gone") {
        fprintf(stderr, "blockie-shot: --radio takes on, off or gone\n");
        return false;
    }
    sp.set_row(kRadioRow, radio_row(o.radio != "gone", o.radio == "on"), 0);
    sp.set_row(kBluetoothRow, info_row(o.btsub), 0);
    Battery b;
    std::vector<std::string> bv = split(o.battery, ':');
    if (o.battery != "unknown") {
        b.ok = true;
        b.capacity = atoi(bv[0].c_str());
        b.status = bv.size() > 1 ? bv[1] : "";
    }
    sp.set_row(kBatteryRow, info_row(battery_words(b)), 0);
    std::vector<std::string> sv = split(o.storage, ':');
    if (sv.size() != 4 && !(sv.size() == 3 && sv[0] == "nocard")) {
        fprintf(stderr, "blockie-shot: --storage takes FREE:TOTAL:SYSFREE:SYSTOTAL or nocard:SYSFREE:SYSTOTAL in GB\n");
        return false;
    }
    Storage st;
    st.mounted = sv.size() == 4;
    size_t k = 0;
    if (st.mounted) {
        st.data = {true, gb_bytes(sv[0]), gb_bytes(sv[1])};
        k = 2;
    } else {
        k = 1;
    }
    st.system = {true, gb_bytes(sv[k]), gb_bytes(sv[k + 1])};
    sp.set_row(kStorageRow, info_row(storage_value(st), storage_lines(st)), 0);
    std::vector<std::string> av = split(o.about, '|');
    if (av.size() != 3) {
        fprintf(stderr, "blockie-shot: --about takes OS_RELEASE|FRONTEND|KERNEL\n");
        return false;
    }
    sp.set_row(kAboutRow, info_row("", about_lines(av[0], av[1], av[2])), 0);
    for (const std::string& h : o.hides)
        sp.set_row(h, SettingsRowState{}, 0);
    if (!o.toast.empty()) {
        std::vector<std::string> tv = split(o.toast, '|');
        sp.toast(-fx_round(tok::motion_toastIn), tv[0], tv.size() > 1 ? tv[1] : "");
    }
    return true;
}

bool page_base(SettingsPage& sp, Fonts& fonts, const PageOpts& o, const Zones& z)
{
    sp.set_sheet_rows(sheet_rows(fonts, o.rows > 0 ? o.rows : SettingsPage::kSheetRows));
    sp.set_zones(z);
    if (o.zone_set && !o.zone_missing)
        sp.set_zone(o.zone);
    else
        sp.set_zone_missing(0);
    bool ok = page_fixtures(sp, o);
    sp.reset(theme().light);
    return ok;
}

bool page_pose(SettingsPage& sp, const PageOpts& o, const Zones& z)
{
    auto press = [&](PadAction a) {
        sp.action(a, false, 0);
        sp.land();
    };
    auto step_to = [&](int want) {
        int n = sp.view(0).sheet.count;
        if (want < 0 || want >= n) {
            fprintf(stderr, "blockie-shot: --sheetfocus %d is outside the %d rows\n", want, n);
            return false;
        }
        while (sp.view(0).sheet.focus != want)
            press(sp.view(0).sheet.focus < want ? PadAction::Down : PadAction::Up);
        return true;
    };
    bool tz = !o.sheet_arg.empty();
    if (tz && !sp.view(0).zone_row) {
        fprintf(stderr, "blockie-shot: --sheet %s needs --zone\n", o.sheet_arg.c_str());
        return false;
    }
    std::string pivot = o.pivot;
    for (const SettingsRowSpec& r : settings_rows())
        if (tz && std::strcmp(r.id, kZoneRow) == 0)
            pivot = r.pivot;
    if (!pivot.empty()) {
        SettingsView v = sp.view(0);
        int want = -1;
        for (size_t k = 0; k < v.pivots.size(); k++)
            if (pivot == v.pivots[k])
                want = (int)k;
        if (want < 0) {
            fprintf(stderr, "blockie-shot: --pivot %s is not shown\n", pivot.c_str());
            return false;
        }
        while (sp.view(0).pivot != want)
            press(sp.view(0).pivot < want ? PadAction::Next : PadAction::Prev);
    }
    for (int i = 0; i < (tz ? 0 : o.focus); i++)
        press(PadAction::Down);
    if (!o.sheet)
        return true;
    press(PadAction::Accept);
    if (o.sheet_arg.size() > 3) {
        std::string name = o.sheet_arg.substr(3);
        int r = -1;
        for (size_t k = 0; k < z.regions.size(); k++)
            if (z.regions[k].words == name || z.regions[k].name == name)
                r = (int)k;
        if (r < 0) {
            fprintf(stderr, "blockie-shot: no region %s in the zone.tab\n", name.c_str());
            return false;
        }
        if (!step_to(r))
            return false;
        press(PadAction::Accept);
    }
    return o.sheet_focus < 0 || step_to(o.sheet_focus);
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
    LeftMove left = LeftMove::T5;
    bool t9_edge = false;
    bool bar_slot = false;
    bool grid_band = true;
    bool startup = false;
    int64_t arrive = -1;
    std::string script;
    int64_t from = 0;
    int64_t at = -1;
    int tail = 0;
    double cadence = 0;
    bool repeat_covers = false;
    std::string exit_class = "quit";
    bool quit_stamp = false;
    std::string libretro;
    PageOpts page;
};

constexpr const char* kNames[] = {"GB", "GBC", "NES", "SNES", "Genesis", "GBA", "PS1", "N64", "PSP", "DC"};
constexpr const char* kCores[] = {"gambatte", "gambatte",     "nestopia",         "snes9x", "genesis_plus_gx",
                                  "mgba",     "pcsx_rearmed", "mupen64plus_next", "ppsspp", "flycast"};
constexpr int kCounts[] = {28, 19, 63, 47, 35, 52, 31, 22, 14, 9};
constexpr int kRecentIds = 900000;
constexpr int kSystemIds = 1000;
constexpr int kDriveGamesMax = 99;
constexpr int kCoverCap = 40;
constexpr int64_t kDriveCapMs = 20000;
constexpr int64_t kNever = std::numeric_limits<int64_t>::max();
constexpr uint32_t kPlainPlate = 0x808080;

const char* core_of(const std::string& system)
{
    for (size_t i = 0; i < sizeof kNames / sizeof kNames[0]; i++)
        if (strcasecmp(kNames[i], system.c_str()) == 0)
            return kCores[i];
    return "";
}

bool exit_status(const std::string& name, int& status)
{
    static const struct {
        const char* name;
        int code;
    } kExits[] = {{"quit", 0},   {"crash", 70}, {"busy", 75}, {"config", 64},
                  {"exec", 127}, {"core", 69},  {"audio", 71}};
    for (const auto& e : kExits)
        if (name == e.name) {
            status = W_EXITCODE(e.code, 0);
            return true;
        }
    return false;
}

bool all_black(const Surface& s)
{
    uint32_t v = LiveF::pack(launch_ground());
    for (int y = 0; y < s.h; y++)
        for (int x = 0; x < s.w; x++)
            if (s.at(x, y) != v)
                return false;
    return true;
}

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
    f.t9_edge = o.t9_edge;
    f.bar_slot = o.bar_slot;
    f.grid_band = o.grid_band;
    return f;
}

class Drive {
public:
    Drive(Fonts& fonts, const DriveOpts& o) : flow(fonts, flow_opts(o)), fonts_(fonts), o_(o) {}

    HomeFlow flow;
    SettingsPage page;
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
            std::string core = core_of(g.system);
            bool absent = !o_.libretro.empty() && !Launcher(".", o_.libretro).core_installed(core);
            if (absent || o_.exit_class == "spawn") {
                LaunchOutcome r = launch_refused(absent, core);
                toast_.show(now_, r.line1, r.line2, flow.nav.top() == Screen::Home);
                refused_ = r.line1;
                fprintf(stderr, "blockie-shot: %lld ms launch refused, %s\n", (long long)now_,
                        absent ? ("core " + core + " is not in the libretro dir").c_str() : "the spawn failed");
                return LaunchStart::Refused;
            }
            played_ = g;
            playing_ = true;
            core_ = core;
            repeat_.release_all();
            launch_.start(true, active_);
            toast_.hide();
            fprintf(stderr, "blockie-shot: %lld ms launch on %s\n", (long long)now_, core.c_str());
            return LaunchStart::Leave;
        };
        bt_ = std::make_unique<BtPage>(fonts_, glyphs_);
        flow.draw_page = [this](Canvas& c, Screen s, int64_t now) {
            if (s == Screen::Bluetooth)
                bt_->draw(c, ink_, now);
            else
                draw_settings(c, fonts_, ink_, page.view(now), theme());
        };
        flow.page_open = [this](Screen s, int64_t now) {
            if (s == Screen::Settings)
                page.reset(theme().light);
            if (s == Screen::Bluetooth) {
                bt_->open(now);
                bt_->set_model(shot_bt_state("list", now), now);
            }
        };
        flow.page_input = [this](Screen s, PadAction a, bool rep) {
            if (s == Screen::Settings)
                return page.action(a, rep, now_);
            if (s == Screen::Bluetooth)
                return bt_->action(a, rep, now_) == BtPage::Act::Pop;
            return a == PadAction::Back && !rep;
        };
        page.on_push = [this](const std::string& id) {
            if (id != kBluetoothRow)
                return;
            flow.nav.stack.push_back(Screen::Bluetooth);
            flow.push(Screen::Bluetooth);
        };
        flow.rearm = [this](int64_t now) { repeat_.rearm(now); };
        flow.page_seed_y = [this](Screen s) { return s == Screen::Settings ? page.focus_y() : -1; };
        flow.last_frame = [this](const Surface& s) {
            copy_surface(launch_.phase() == LaunchPhase::Leaving ? shown_.surface() : last_.surface(), s);
        };
        flow.setup();
        flow.ctl.can_animate = []() { return true; };
        LaunchHooks& h = launch_.hooks;
        h.land = [this](int64_t now, const char* why) { flow.land_all(now, why); };
        h.reverse = [this](int64_t now) { return flow.launch_back(now); };
        h.fold_end = [this]() { return flow.ctl.active() ? flow.ctl.t0() + flow.ctl.run_length() : (int64_t)-1; };
        h.finish = [this](int64_t now) { flow.ctl.finish(now); };
        h.come_back = [this](int64_t now) {
            fprintf(stderr, "blockie-shot: %lld ms return after a cover wait of %lld ms\n", (long long)now,
                    (long long)(now - launch_.cover_until() + kCoverWaitMs));
            flow.come_back(now);
        };
        h.covers_pending = [this]() {
            return covers_pending(flow.home, flow.model, [this](int64_t id) { return late_.count(id) && !arrived_; });
        };
        h.played = [this]() { flow.game_ran(played_first(flow.home.recent, played_, flow.home.recent_cap)); };
        h.theme = [this](bool light) {
            set_theme_light(light);
            page.set_light(light);
            flow.theme_changed();
        };
        h.drops = [this]() {
            if (!arrive_held_)
                return;
            fprintf(stderr, "blockie-shot: %lld ms held covers arrive\n", (long long)now_);
            arrive();
        };
        h.toast = [this](const std::string& line1, const std::string& line2) {
            toast_.show(now_, line1, line2, flow.nav.top() == Screen::Home);
        };
        h.act = [this](PadAction a, int64_t now) { keys_.act(a, now); };
        keys_.apply = [this](PadAction a, bool rep, int64_t now) {
            fprintf(stderr, "blockie-shot: %lld ms input %s%s\n", (long long)now, key_name(a), rep ? " repeat" : "");
            flow.input(a, rep, now);
        };
        flow.ctl.landed = [this](const char* what, const char* why) {
            fprintf(stderr, "blockie-shot: %lld ms %s landed (%s)\n", (long long)now_, what, why);
            launch_.landed(what);
            bool by_input = !in_frame_ && !flow.ctl.navigating();
            if (by_input && std::strcmp(why, "end") == 0 && std::strcmp(what, "startup") != 0)
                end_check_input();
            if (std::strcmp(why, "input") == 0 && std::strcmp(what, "collapse") == 0 && flow.ctl.navigating()) {
                collapse_landed_ = true;
                Nav det = flow.kept_nav();
                det.tile_focus = flow.nav.tile_focus;
                flow.draw(rest_sel_.surface(), flow.kept(), flow.kept_nav(), true, flow.kept_nav().pane, kScreen);
                flow.draw(rest_det_.surface(), flow.kept(), det, true, Pane::Details, kScreen);
            }
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
        copy_surface(last_.surface(), shown_.surface());
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
            bool motion = was_moving || flow.animating(tf) || page_moving(tf) || toast_.animating(tf);
            copy_surface(last_.surface(), prev_.surface());
            copy_surface(shown_.surface(), seen_frame_.surface());
            in_frame_ = true;
            launch_.step(tf);
            Rect art_r = flow.take_art_rect();
            bool more = false;
            LaunchDraw kind = launch_.draw(true);
            const char* black = kind == LaunchDraw::Black ? launch_phase_name(launch_.phase()) : nullptr;
            if (black) {
                Canvas bc(last_.surface());
                bc.fill(kScreen, launch_ground());
                launch_.black_shown();
                more = flow.animating(tf);
            } else if (kind == LaunchDraw::Home) {
                more = flow.frame(tf, last_.surface());
            }
            more = more || page_moving(tf);
            in_frame_ = false;
            more = more || toast_.visible(tf);
            was_moving = more;
            int step = 0, ok = input_ok_;
            input_ok_ = 1;
            if (black)
                fprintf(stderr, "blockie-shot: %lld ms black frame in %s\n", (long long)tf, black);
            if (!refused_.empty()) {
                bool held = !ctl.active() && launch_.phase() == LaunchPhase::Idle && toast_.visible(tf) &&
                            toast_.line1(tf) == refused_;
                ok = ok && held;
                fprintf(stderr, "blockie-shot: %lld ms precheck refusal check %s%s toast shows\n", (long long)tf,
                        held ? "no launch motion starts and the " : "FAILED, ", refused_.c_str());
                refused_.clear();
            }
            bool launching = ctl.active() && ctl.kind() == FoldKind::Launch;
            bool back = ctl.active() && ctl.kind() == FoldKind::Return;
            if (launching && !ctl.reversed() && tf == ctl.t0()) {
                bool held = same_px(last_.surface(), seen_frame_.surface());
                ok = ok && held;
                fprintf(stderr,
                        "blockie-shot: %lld ms launch frame 0 check %s, frame hash (FNV-1a over pixels) %016llx\n",
                        (long long)tf, held ? "the frame is the last presented frame" : "FAILED",
                        frame_hash(last_.surface()));
            } else if (launching || back) {
                Buffer ref;
                bool held = ctl.compose_at(ctl.forward_time(tf), ref.surface());
                held = held && same_px(ref.surface(), last_.surface());
                ok = ok && held;
                const char* what = back ? "return" : ctl.reversed() ? "launch reversal" : "launch leave";
                const char* res = back             ? "the home at rest over black"
                                  : ctl.reversed() ? "a fresh render of the home at rest over black"
                                                   : "the press frame over black";
                fprintf(stderr,
                        "blockie-shot: %lld ms %s check %s%s at its time, frame hash (FNV-1a over pixels) %016llx\n",
                        (long long)tf, what, held ? "the frame is " : "FAILED, the frame is not ", res,
                        frame_hash(last_.surface()));
            }
            if (step_in) {
                step = 1;
                Buffer ref;
                flow.draw(ref.surface(), flow.kept(), flow.kept_focus(), true, Pane::Details, kScreen);
                bool held = same_px(ref.surface(), last_.surface());
                ok = ok && held;
                fprintf(stderr, "blockie-shot: %lld ms step in check %s\n", (long long)tf,
                        held ? "the frame is the state before the input with Details focus" : "FAILED");
            }
            if (was_fold && !ctl.active() && was_kind != FoldKind::Startup) {
                bool collapse_step = was_details && was_kind == FoldKind::Collapse;
                if (collapse_step) {
                    step = 1;
                    Buffer ref;
                    flow.draw(ref.surface(), flow.model, flow.nav, false, Pane::Details, kScreen);
                    ok = same_px(ref.surface(), prev_.surface());
                } else if (was_kind == FoldKind::Launch && launch_.phase() != LaunchPhase::Idle) {
                    ok = all_black(last_.surface());
                } else if (was_kind == FoldKind::Launch) {
                    Buffer ref;
                    flow.draw(ref.surface(), flow.model, flow.nav, false, flow.nav.pane, kScreen);
                    ok = same_px(ref.surface(), last_.surface());
                } else if (was_kind == FoldKind::Return) {
                    Buffer ref;
                    ok = ctl.compose_end(ref.surface()) && same_px(ref.surface(), last_.surface());
                } else {
                    ok = same_px(prev_.surface(), last_.surface());
                }
                bool waits = was_kind == FoldKind::Launch && launch_.phase() != LaunchPhase::Idle;
                fprintf(stderr, "blockie-shot: %lld ms %s end check %s\n", (long long)tf, fold_kind_name(was_kind),
                        ok ? (collapse_step ? "the last composed frame equals the destination with Details focus"
                              : waits       ? "the leave lands on the wait's black"
                                            : "the composite at T equals a fresh render")
                           : "FAILED");
            }
            const T9& t9 = flow.t9();
            std::string sys = t9.shown(tf).system;
            int title_a = t9.pose(tf).a256;
            const char* cause = "";
            if (collapse_landed_ && ctl.active() && ctl.kind() == FoldKind::Expand && !ctl.reversed()) {
                Buffer ref;
                step = 1;
                Surface rest = ctl.details() ? rest_det_.surface() : rest_sel_.surface();
                bool held = same_px(rest, ctl.from()) && ctl.compose_at(ctl.forward_time(tf), ref.surface()) &&
                            same_px(ref.surface(), last_.surface());
                ok = ok && held;
                cause = " the collapse lands on the input and the expand starts from its rest";
                const char* res = held ? "the frame is the new expand composed fresh from the landed rest" : "FAILED";
                fprintf(stderr,
                        "blockie-shot: %lld ms rule 3 landing check %s, frame hash (FNV-1a over pixels) %016llx\n",
                        (long long)tf, res, frame_hash(last_.surface()));
            }
            collapse_landed_ = false;
            if (ctl.active() && ctl.reversed() && ctl.t0() != back_logged_) {
                back_logged_ = ctl.t0();
                fprintf(stderr, "blockie-shot: %lld ms reversal at e %lld ms\n", (long long)ctl.t0(),
                        (long long)ctl.run_length());
            }
            bool swap = o_.left == LeftMove::T5 && ctl.active() &&
                        (ctl.kind() == FoldKind::Expand || ctl.kind() == FoldKind::Collapse);
            int64_t ft = ctl.forward_time(tf);
            int64_t k = ctl.kind() == FoldKind::Collapse && !ctl.reversed() ? slide_ms() - ft : ft;
            int leave_ms = fx_round(tok::motion_swapOut);
            bool same = swap_seen_ && (ctl.t0() == swap_t0_ ||
                                       (ctl.reversed() && !swap_back_ && ctl.t0() - ctl.run_length() == swap_t0_));
            bool crossed = same && std::min(swap_k_, k) < leave_ms && std::max(swap_k_, k) > 0;
            if (!step && swap && ((k > 0 && k <= leave_ms) || crossed)) {
                Buffer ref;
                const char* what = k <= 0 ? "landing" : k < leave_ms ? "leave" : k == leave_ms ? "hold" : "arrival";
                int64_t gap = n > 0 ? tf - frame_time(n - 1) : 0;
                bool short_back = ctl.reversed() && tf == ctl.t0() && ctl.run_length() <= gap;
                step = k <= 0 ? 4 : short_back ? 3 : 2;
                bool held = ctl.compose_at(ft, ref.surface()) && same_px(ref.surface(), last_.surface());
                ok = ok && held;
                cause = short_back      ? " the T5 pose a reversal shorter than a frame starts from"
                        : k <= 0        ? " the reversed T5 leave lands on the origin"
                        : k < leave_ms  ? " the T5 rows leave on strip"
                        : k == leave_ms ? " the T5 ground hold before the arrival"
                                        : " the T5 rows arrive after the leave";
                const char* res = held ? "the frame is the row swap composed fresh at its time" : "FAILED";
                fprintf(stderr, "blockie-shot: %lld ms T5 %s check %s, frame hash (FNV-1a over pixels) %016llx\n",
                        (long long)tf, what, res, frame_hash(last_.surface()));
            }
            swap_seen_ = swap;
            swap_k_ = k;
            swap_t0_ = ctl.t0();
            swap_back_ = ctl.reversed();
            if (!step && seen_ && !was_fold && !ctl.active() && t9.running(tf) && sys != seen_sys_ &&
                flow.nav.pane == Pane::Details && flow.nav.top() == Screen::Home) {
                step = 1;
                cause = " the system line swaps with the title at the T9 out to in boundary";
                ok = ok && title_a == 0 && seen_title_a_ == 0 && swap_holds(seen_sys_, sys);
                fprintf(stderr, "blockie-shot: %lld ms system line swap check %s\n", (long long)tf,
                        ok ? "the frame is the one before with only the system line redrawn" : "FAILED");
            }
            seen_ = true;
            seen_sys_ = sys;
            seen_title_a_ = title_a;
            copy_surface(last_.surface(), shown_.surface());
            toast_.draw(fonts_, ink_, shown_.surface(), tf);
            fx toast_e = toast_.visible(tf) ? toast_.eased(tf) : 0;
            if (!step && toast_e != seen_toast_e_ && same_px(prev_.surface(), last_.surface())) {
                Buffer ref;
                copy_surface(last_.surface(), ref.surface());
                ToastStrip probe = toast_;
                probe.draw(fonts_, ink_, ref.surface(), tf);
                bool held = same_px(ref.surface(), shown_.surface());
                step = 1;
                ok = ok && held;
                cause = toast_e < seen_toast_e_ ? " the F11 toast leaves on expOut6"
                                                : " the F11 toast rises on expOut6";
                const char* res = held ? "the home is the frame before and the strip is the toast composed fresh at its"
                                         " time"
                                       : "FAILED";
                fprintf(stderr, "blockie-shot: %lld ms toast check %s, frame hash (FNV-1a over pixels) %016llx\n",
                        (long long)tf, res, frame_hash(shown_.surface()));
            }
            seen_toast_e_ = toast_e;
            if (!frames_dir.empty() && tf >= o_.from) {
                std::string png = frame_path(frames_dir, written);
                if (!write_png(png, shown_.surface())) {
                    fprintf(stderr, "blockie-shot: cannot write %s\n", png.c_str());
                    fclose(side);
                    return 1;
                }
                if (fprintf(side, "%d %lld %d %d %d %d %d %d %d %d%s\n", written, (long long)tf, flow.boundary(tf),
                            motion ? 1 : 0, step, ok, art_r.x0, art_r.y0, art_r.x1, art_r.y1, cause) < 0) {
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
            if (pending || more)
                tail = o_.tail;
            else if (o_.at < 0 && tail-- <= 0)
                break;
        }
        if (side && fclose(side) != 0) {
            fprintf(stderr, "blockie-shot: cannot close %s/frames.txt\n", frames_dir.c_str());
            return 1;
        }
        if (!out.empty() && !write_png(out, shown_.surface())) {
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

    void system_line(const Surface& s, const std::string& sys)
    {
        Canvas c(s);
        c.fill(s.rect(), theme().bg);
        SelectorModel m;
        m.game_system = sys;
        m.big = o_.big;
        draw_turned(c, fonts_, m, theme());
    }

    bool swap_holds(const std::string& from, const std::string& to)
    {
        Buffer a, b, blank;
        system_line(a.surface(), from);
        system_line(b.surface(), to);
        Canvas bc(blank.surface());
        bc.fill(blank.surface().rect(), theme().bg);
        Surface sa = a.surface(), sb = b.surface(), sz = blank.surface();
        Surface was = prev_.surface(), now = last_.surface();
        for (int y = 0; y < now.h; y++)
            for (int x = 0; x < now.w; x++) {
                bool line = sa.at(x, y) != sz.at(x, y) || sb.at(x, y) != sz.at(x, y);
                if (now.at(x, y) != (line ? sb.at(x, y) : was.at(x, y)))
                    return false;
            }
        return true;
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
        int status = 0;
        exit_status(o_.exit_class, status);
        LaunchPhase at = launch_.phase();
        LaunchOutcome out = launch_.reap(status, o_.quit_stamp, core_, now_, now_ + kCoverWaitMs);
        fprintf(stderr, "blockie-shot: %lld ms game exited %s in %s, %s\n", (long long)now_, o_.exit_class.c_str(),
                launch_phase_name(at), out.played ? "played" : "not played");
    }

    int64_t step_time(size_t i)
    {
        Step& st = steps_[i];
        if (st.pct >= 0) {
            st.ms = flow.ctl.t0() + flow.ctl.run_length() * st.pct / 100;
            st.pct = -1;
        }
        if (i > 0)
            st.ms = std::max(st.ms, steps_[i - 1].ms);
        if (st.key != "ret" && st.key != "cover")
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
        if (st.key == "cover") {
            active_ = !st.down;
            launch_.edges(active_, playing_, now_);
            return;
        }
        if (st.key == "theme") {
            if (st.down) {
                want_light_ = !want_light_;
                launch_.theme(want_light_);
            }
            return;
        }
        if (st.key == "fail") {
            if (st.down && o_.page.zone_set && !o_.page.zone_missing)
                page.set_zone(o_.page.zone);
            return;
        }
        if (st.key == "missing") {
            if (st.down)
                page.set_zone_missing(now_);
            return;
        }
        PadAction a;
        if (!action_of(st.key, a))
            return;
        if (st.down)
            keys_.press(a, kFromKeys, !playing_, now_);
        else if (is_direction(a))
            repeat_.release(a, kFromKeys, now_);
    }

    void arrive()
    {
        arrived_ = true;
        arrive_held_ = false;
        for (int64_t id : late_) {
            auto it = art.find(id);
            if (it != art.end())
                flow.art(id, kTileSide, 0, it->second);
        }
    }

    void advance(int64_t tf)
    {
        for (;;) {
            int64_t te = next_ < steps_.size() ? step_time(next_) : kNever;
            int64_t tr = repeat_.active() ? repeat_.deadline() : kNever;
            int64_t ta = o_.arrive >= 0 && !arrived_ && !arrive_held_ ? o_.arrive : kNever;
            int64_t t = std::min({te, tr, ta});
            if (t > tf || t == kNever)
                return;
            set_now(t);
            if (t == ta) {
                if (launch_.holds())
                    arrive_held_ = true;
                else
                    arrive();
                continue;
            }
            if (te <= tr) {
                apply(steps_[next_++]);
                continue;
            }
            PadAction a = PadAction::Up;
            if (repeat_.due(t, a))
                keys_.repeat(a, t);
        }
    }

    bool page_moving(int64_t t) const
    {
        Screen top = flow.nav.top();
        return (top == Screen::Settings && page.animating(t)) || (top == Screen::Bluetooth && bt_->animating(t));
    }

    Fonts& fonts_;
    DriveOpts o_;
    Repeat repeat_;
    InkLayer ink_;
    GlyphCache glyphs_;
    std::unique_ptr<BtPage> bt_;
    Buffer last_;
    Buffer prev_;
    Buffer shown_;
    Buffer seen_frame_;
    int64_t now_ = 0;
    bool playing_ = false;
    HomeGame played_;
    std::string core_;
    LaunchFlow launch_;
    LaunchKeys keys_{launch_, repeat_};
    ToastStrip toast_;
    bool active_ = true;
    std::string refused_;
    bool want_light_ = theme().light;
    std::vector<Step> steps_;
    size_t next_ = 0;
    std::unordered_set<int64_t> late_;
    bool arrived_ = false;
    bool arrive_held_ = false;
    bool in_frame_ = false;
    int input_ok_ = 1;
    bool seen_ = false;
    std::string seen_sys_;
    int seen_title_a_ = 256;
    bool swap_seen_ = false;
    int64_t swap_k_ = 0;
    int64_t swap_t0_ = 0;
    bool swap_back_ = false;
    fx seen_toast_e_ = 0;
    bool collapse_landed_ = false;
    int64_t back_logged_ = -1;
    Buffer rest_sel_;
    Buffer rest_det_;
};

int run_drive(Fonts& fonts, const DriveOpts& o, const std::string& out, const std::string& frames_dir,
              int systems, bool items, const std::string& bt_sub, int sys_focus, const std::string& tiles_dir,
              const std::string& recent_dir, const Zones& zones)
{
    auto d = std::make_unique<Drive>(fonts, o);
    if (!page_base(d->page, fonts, o.page, zones))
        return 2;
    if (!o.noart && !load_tiles(tiles_dir, d->sys_covers, kCoverCap, o.big ? kBigTileSide : kTileSide))
        return 1;
    std::vector<std::string> titles, rtitles;
    if (!load_tiles(recent_dir, d->recent_covers, kCoverCap) || !read_lines(o.titles, titles) ||
        !read_lines(o.recent_titles, rtitles))
        return 1;
    std::vector<std::string> rsys = comma_list(o.recent_system);
    if (rsys.empty()) {
        fprintf(stderr, "blockie-shot: --recentsys needs a system name\n");
        return 2;
    }
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
        g.system = rsys[(size_t)k % rsys.size()];
        if (!d->recent_covers.empty())
            d->art[g.id] = d->recent_covers[(size_t)k % d->recent_covers.size()];
        d->recent_games.push_back(g);
    }
    d->flow.nav.focus = focus;
    if (!d->setup())
        return 2;
    return d->run(out, frames_dir);
}

struct PageEvent {
    std::string key;
    int64_t t = 0;
    bool idle = false;
    SettingsView before;
    SettingsView after;
};

fx bar_glide(fx from, int to, int64_t e)
{
    int d = fx_round(tok::motion_focusCursor);
    if (e >= d || e < 0)
        return fx_int(to);
    return from + fx_mul(fx_int(to) - from, curve(Curve::Sine, (fx)(e * kOne / d)));
}

fx t12(int64_t e, bool open)
{
    int d = fx_round(tok::motion_sheet);
    fx p = curve(Curve::Decel, e >= d || e < 0 ? kOne : (fx)(e * kOne / d));
    return open ? p : kOne - p;
}

bool starts_t12(const PageEvent& e, bool& open)
{
    const SheetView& b = e.before.sheet;
    const SheetView& a = e.after.sheet;
    if (a.open && (!b.open || a.level != b.level)) {
        open = true;
        return true;
    }
    if (b.open && !a.open) {
        open = false;
        return true;
    }
    return false;
}

bool band_equal(const Surface& a, int ya, const Surface& b, int yb, int h, int x0)
{
    for (int y = 0; y < h; y++)
        if (!std::equal(a.row(ya + y) + x0, a.row(ya + y) + a.w, b.row(yb + y) + x0))
            return false;
    return true;
}

bool shifted_by_a_row(Fonts& fonts, const Surface& was, const Surface& now, const SheetView& b, const SheetView& a)
{
    SheetGeom g = sheet_geom(fonts, a, now.rect());
    if (!band_equal(now, 0, was, 0, g.rows_top, 0))
        return false;
    int lo = std::max(a.first, b.first), hi = std::min(a.first, b.first) + g.shown;
    for (int k = lo; k < hi; k++) {
        if (k == b.focus || k == a.focus)
            continue;
        if (!band_equal(now, g.rows_top + (k - a.first) * g.pitch, was, g.rows_top + (k - b.first) * g.pitch, g.pitch,
                        px(tok::edge)))
            return false;
    }
    int end = g.rows_top + g.shown * g.pitch;
    return band_equal(now, end, was, end, now.h - end, 0);
}

bool same_view(const SettingsView& x, const SettingsView& y)
{
    const SheetView& p = x.sheet;
    const SheetView& q = y.sheet;
    return x.focus == y.focus && x.bar == y.bar && x.light == y.light && x.zone_row == y.zone_row && x.zone == y.zone &&
           std::strcmp(p.title, q.title) == 0 && p.items == q.items && p.count == q.count && p.focus == q.focus &&
           p.bar == q.bar && p.progress == q.progress && p.first == q.first && p.mark == q.mark && p.rows == q.rows &&
           p.open == q.open && p.level == q.level;
}

bool panel_probe(Fonts& fonts, const SettingsView& v, fx p, const Surface& frame)
{
    Buffer page;
    Canvas pc(page.surface());
    InkLayer ink;
    SettingsView bare = v;
    bare.sheet.progress = 0;
    draw_settings(pc, fonts, ink, bare, theme());
    SheetView s = v.sheet;
    s.progress = p;
    Rect sr = frame.rect();
    SheetGeom g = sheet_geom(fonts, s, sr);
    int a = alpha256(p);
    const Theme& th = theme();
    auto row = [&](int y, bool panel) {
        if (y < sr.y0 || y >= sr.y1)
            return true;
        Rect r{sr.x0, y, sr.x1, y + 1};
        pc.blend(r, th.scrim_sheet, th.scrim_sheet_a256 * a >> 8);
        if (panel)
            pc.blend(r, th.bg_elevated, a);
        return band_equal(frame, y, page.surface(), y, 1, 0);
    };
    return row(g.top - 1, false) && row(g.top, true);
}

std::string designed(Fonts& fonts, const PageEvent& e, const SettingsView& v, int64_t tf, const Surface& was,
                     const Surface& now, const std::string& confirmed, bool& held)
{
    const SheetView& b = e.before.sheet;
    const SheetView& a = e.after.sheet;
    int64_t el = tf - e.t;
    bool open = false;
    bool t12_start = starts_t12(e, open);
    if (e.key == "fail" || e.key == "theme") {
        if (!e.idle)
            return "";
        SettingsView w = e.before;
        if (e.key == "fail") {
            w.zone = v.zone;
            w.sheet.mark = v.sheet.mark;
            held = same_view(w, v) && v.zone_row && v.zone == confirmed;
            return "a failed Set: the row reads the confirmed zone, nothing else moves";
        }
        w.light = v.light;
        held = same_view(w, v) && v.light != e.before.light && v.light == theme().light;
        return "a theme switch, nothing else moves";
    }
    if (e.key == "missing") {
        bool closes = b.open && b.level != 0;
        held = !v.zone_row && v.focus == 0 && v.bar == fx_int(0) &&
               (!closes || (!v.sheet.open && v.sheet.progress == t12(el, false) &&
                            panel_probe(fonts, v, t12(el, false), now)));
        return closes ? "the zone key goes: its row goes, the bar lands on row 0, the sheet closes from 1"
                      : "the zone key goes: its row goes, the bar lands on row 0";
    }
    if (b.progress > 0 && b.progress < kOne) {
        fx want = t12_start ? t12(el, open) : a.progress;
        if (t12_start)
            held = a.progress == (open ? 0 : kOne) && (!open || v.sheet.level == a.level);
        else
            held = a.progress == (b.open ? kOne : 0);
        held = held && v.sheet.progress == want && v.sheet.first == a.first && v.sheet.focus == a.focus &&
               panel_probe(fonts, v, want, now);
        std::string c = b.open ? "an input lands a running rise" : "an input lands a running close";
        return t12_start ? c + (open ? ", then the sheet rises from 0" : ", then the sheet closes from 1") : c;
    }
    if (t12_start) {
        bool level = b.open && a.open && a.level != b.level;
        held = v.sheet.progress == t12(el, open) && v.sheet.open == open && (!open || v.sheet.level == a.level) &&
               panel_probe(fonts, v, t12(el, open), now);
        if (level)
            return a.level > b.level ? "a level push: the new level rises from 0" : "a level pop: level 1 rises from 0";
        return open ? "the sheet rises from 0" : "the sheet closes from 1";
    }
    bool still = b.open && a.open && b.level == a.level && b.progress == kOne && v.sheet.progress == kOne;
    int d = a.first - b.first;
    if (still && (d == 1 || d == -1) && a.focus - b.focus == d) {
        held = v.sheet.first == a.first && shifted_by_a_row(fonts, was, now, b, v.sheet);
        return "a whole row window step";
    }
    int n = a.count;
    bool down_wrap = e.key == "down" && b.focus == n - 1 && a.focus == 0;
    bool up_wrap = e.key == "up" && b.focus == 0 && a.focus == n - 1;
    if (still && n > 1 && a.first != b.first && (down_wrap || up_wrap)) {
        int shown = sheet_geom(fonts, v.sheet, now.rect()).shown;
        held = v.sheet.first == (a.focus == 0 ? 0 : n - shown) &&
               v.sheet.bar == bar_glide(fx_int(b.focus - b.first + a.first), a.focus, el);
        return "a fresh press past the end wraps, the window jumps to the other end, the bar leaves the old slot";
    }
    return "";
}

int run_page_frames(SettingsPage& sp, Fonts& fonts, const DriveOpts& o, const Zones& zones,
                    const std::string& frames_dir, const std::string& out)
{
    std::vector<Step> steps;
    if (!parse_script(o.script, steps)) {
        fprintf(stderr, "blockie-shot: bad --script %s\n", o.script.c_str());
        return 2;
    }
    for (size_t i = 0; i < steps.size(); i++) {
        if (steps[i].pct >= 0) {
            fprintf(stderr, "blockie-shot: the settings frames take MS steps, not %d%%\n", steps[i].pct);
            return 2;
        }
        if (i > 0)
            steps[i].ms = std::max(steps[i].ms, steps[i - 1].ms);
    }
    FILE* side = fopen((frames_dir + "/frames.txt").c_str(), "w");
    if (!side) {
        fprintf(stderr, "blockie-shot: cannot write %s/frames.txt\n", frames_dir.c_str());
        return 1;
    }
    FILE* bars = fopen((frames_dir + "/bars.txt").c_str(), "w");
    if (!bars) {
        fprintf(stderr, "blockie-shot: cannot write %s/bars.txt\n", frames_dir.c_str());
        fclose(side);
        return 1;
    }
    auto shut = [&]() {
        fclose(side);
        fclose(bars);
    };
    std::string confirmed = zone_words(zones, o.page.zone);
    Repeat rep;
    InkLayer ink;
    Buffer buf;
    Buffer was;
    size_t next = 0;
    int written = 0;
    int tail = o.tail;
    bool was_moving = false;
    int64_t t12_at = -1;
    bool t12_open = false;
    for (int n = 0;; n++) {
        int64_t tf = frame_ms(n);
        std::vector<PageEvent> evs;
        auto act = [&](const std::string& key, int64_t t, const std::function<void()>& f) {
            PageEvent e;
            e.key = key;
            e.t = t;
            e.idle = !sp.animating(t);
            e.before = sp.view(t);
            f();
            e.after = sp.view(t);
            evs.push_back(std::move(e));
        };
        for (;;) {
            int64_t te = next < steps.size() ? steps[next].ms : kNever;
            int64_t tr = rep.active() ? rep.deadline() : kNever;
            int64_t t = std::min(te, tr);
            if (t > tf || t == kNever)
                break;
            if (te > tr) {
                PadAction a = PadAction::Up;
                if (rep.due(t, a))
                    act("repeat", t, [&]() { sp.action(a, true, t); });
                continue;
            }
            const Step& st = steps[next++];
            PadAction a = PadAction::Up;
            if (st.key == "fail") {
                if (st.down && o.page.zone_set && !o.page.zone_missing)
                    act("fail", t, [&]() { sp.set_zone(o.page.zone); });
            } else if (st.key == "missing") {
                if (st.down)
                    act("missing", t, [&]() { sp.set_zone_missing(t); });
            } else if (st.key == "theme") {
                if (st.down)
                    act("theme", t, [&]() {
                        set_theme_light(!theme().light);
                        sp.set_light(theme().light);
                    });
            } else if (action_of(st.key, a)) {
                if (st.down) {
                    act(st.key, t, [&]() { sp.action(a, false, t); });
                    if (is_direction(a))
                        rep.press(a, kFromKeys, t);
                } else if (is_direction(a)) {
                    rep.release(a, kFromKeys, t);
                }
            }
        }
        SettingsView v = sp.view(tf);
        bool moving = sp.animating(tf);
        copy_surface(buf.surface(), was.surface());
        Canvas c(buf.surface());
        draw_settings(c, fonts, ink, v, theme());
        std::string cause;
        bool held = true;
        bool open = false;
        if (n > 0 && evs.size() == 1)
            cause = designed(fonts, evs[0], v, tf, was.surface(), buf.surface(), confirmed, held);
        else if (n > 0 && evs.empty() && t12_at >= 0) {
            fx want = t12(tf - t12_at, t12_open);
            held = v.sheet.progress == want && panel_probe(fonts, v, want, buf.surface());
            cause = t12_open ? "the first T12 rise frame after the input" : "the first T12 close frame after the input";
        }
        t12_at = -1;
        if (evs.size() == 1 && starts_t12(evs[0], open)) {
            t12_at = evs[0].t;
            t12_open = open;
        }
        bool motion = was_moving || moving || !evs.empty();
        was_moving = moving;
        if (tf >= o.from) {
            if (!write_png(frame_path(frames_dir, written), buf.surface())) {
                fprintf(stderr, "blockie-shot: cannot write %s\n", frame_path(frames_dir, written).c_str());
                shut();
                return 1;
            }
            if (fprintf(side, "%d %lld %d %d %d %d 0 0 0 0%s%s\n", written, (long long)tf, kPaneSplit, motion ? 1 : 0,
                        cause.empty() ? 0 : 1, held ? 1 : 0, cause.empty() ? "" : " ", cause.c_str()) < 0) {
                fprintf(stderr, "blockie-shot: cannot write %s/frames.txt\n", frames_dir.c_str());
                shut();
                return 1;
            }
            SheetGeom g = sheet_geom(fonts, v.sheet, buf.surface().rect());
            int vis = alpha256(v.sheet.progress) > 0, rest = v.sheet.progress == kOne;
            int bar_rest = v.sheet.bar == fx_int(v.sheet.focus), page_rest = v.bar == fx_int(v.focus);
            if (fprintf(bars, "%d %d %d %d %d %d %d %d %d %d %d %d %d\n", written, vis, rest, v.sheet.focus,
                        v.sheet.first, g.rows_top, g.pitch, bar_rest, v.focus, settings_row_y(0),
                        settings_row_y(1) - settings_row_y(0), page_rest, evs.empty() ? 0 : 1) < 0) {
                fprintf(stderr, "blockie-shot: cannot write %s/bars.txt\n", frames_dir.c_str());
                shut();
                return 1;
            }
            written++;
        }
        if (tf > kDriveCapMs) {
            fprintf(stderr, "blockie-shot: the settings frames passed the %lld ms cap\n", (long long)kDriveCapMs);
            shut();
            return 1;
        }
        if (next >= steps.size() && !rep.active() && !moving) {
            if (tail-- > 0)
                continue;
            break;
        }
    }
    bool closed = fclose(side) == 0;
    closed = fclose(bars) == 0 && closed;
    if (!closed) {
        fprintf(stderr, "blockie-shot: cannot close %s/frames.txt or bars.txt\n", frames_dir.c_str());
        return 1;
    }
    if (!out.empty() && !write_png(out, buf.surface())) {
        fprintf(stderr, "blockie-shot: cannot write %s\n", out.c_str());
        return 1;
    }
    fprintf(stderr, "blockie-shot: settings %d frames\n", written);
    return 0;
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
        std::string text;
        while (std::getline(in, text)) {
            if (text.empty())
                continue;
            std::istringstream ls(text);
            PopRow r;
            long long t = 0;
            int motion = 0, step = 0, ok = 1;
            if (!(ls >> r.n >> t >> r.b >> motion >> step >> ok >> r.art.x0 >> r.art.y0 >> r.art.x1 >> r.art.y1))
                break;
            r.t = t;
            r.motion = motion != 0;
            r.step = step != 0;
            r.left_only = step >= 2;
            r.short_back = step == 3;
            r.landing = step == 4;
            r.ok = ok != 0;
            std::getline(ls >> std::ws, r.cause);
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
        pop_count(f0.rgb.data(), f1.rgb.data(), f0.w, f0.h, pop_edge(rows[i]), rows[i].art, diffs[i].d, diffs[i].e,
                  &diffs[i].same);
        if (i + 1 < rows.size()) {
            if (!load_frame(dir, rows[i + 1].n, f2))
                return 1;
            if (f2.w != f0.w || f2.h != f0.h) {
                printf("FAIL frame %d changes size\n", rows[i + 1].n);
                return 1;
            }
            pop_count(f0.rgb.data(), f2.rgb.data(), f0.w, f0.h, pop_edge(rows[i]), rows[i].art, diffs[i].skip,
                      diffs[i].e_skip, nullptr);
        }
        std::swap(f0, f1);
        std::swap(f1, f2);
    }
    std::vector<std::string> lines;
    int fails = pop_judge(rows, diffs, lead, lines);
    std::ifstream bin(dir + "/bars.txt");
    if (bin) {
        int sx = px(tok::edge - tok::focus_barOffset), pxb = px(tok::hubEdge - tok::focus_barOffset);
        int bw = px(tok::focus_barWidth);
        std::vector<BarRow> bars;
        std::string text;
        while (std::getline(bin, text)) {
            if (text.empty())
                continue;
            std::istringstream ls(text);
            BarRow b;
            int sv = 0, sr = 0, br = 0, pr = 0, ev = 0;
            if (!(ls >> b.n >> sv >> sr >> b.focus >> b.first >> b.rows_top >> b.pitch >> br >> b.page_focus >>
                  b.page_top >> b.page_pitch >> pr >> ev))
                break;
            b.sheet_vis = sv != 0;
            b.sheet_rest = sr != 0;
            b.bar_rest = br != 0;
            b.page_rest = pr != 0;
            b.event = ev != 0;
            DecodedImage f;
            if (!load_frame(dir, b.n, f))
                return 1;
            b.sheet_runs = bar_runs(f.rgb.data(), f.w, f.h, sx, bw, 0);
            b.page_runs = bar_runs(f.rgb.data(), f.w, f.h, pxb, bw, 0);
            bars.push_back(std::move(b));
        }
        std::vector<std::string> track;
        fails += bar_judge(bars, track);
        std::string last = lines.back();
        lines.pop_back();
        lines.insert(lines.end(), track.begin(), track.end());
        lines.push_back(last);
    }
    for (const std::string& l : lines)
        printf("%s\n", l.c_str());
    if (!summary.empty()) {
        FILE* fs = fopen(summary.c_str(), "a");
        if (!fs) {
            printf("FAIL cannot open %s\n", summary.c_str());
            return 1;
        }
        for (const std::string& l : lines)
            if (l.find("FAIL") != std::string::npos || &l == &lines.back() ||
                l.rfind("PASS the bar track", 0) == 0)
                fprintf(fs, "%s: %s\n", name.c_str(), l.c_str());
        if (fclose(fs) != 0) {
            printf("FAIL cannot write %s\n", summary.c_str());
            return 1;
        }
    }
    return fails ? 1 : 0;
}

struct PmShotOpts {
    std::string surface;
    std::string gates;
    std::string pick_gates;
    std::string plate = "home";
    int idle = 0;
};

const char* pm_surface_name(PmSurface s)
{
    switch (s) {
    case PmSurface::Power: return "power";
    case PmSurface::Sheet: return "sheet";
    case PmSurface::Confirm: return "confirm";
    case PmSurface::Toast: return "toast";
    case PmSurface::None: break;
    }
    return "none";
}

struct PmStep {
    int64_t t;
    std::string key;
    bool down;
};

int index_of(const std::vector<const char*>& v, const char* word)
{
    for (size_t i = 0; i < v.size(); i++)
        if (std::strcmp(v[i], word) == 0)
            return (int)i;
    return -1;
}

int run_pm_shot(Fonts& fonts, const PmShotOpts& po, const DriveOpts& o, const Surface& plate,
                const std::string& frames_dir, const std::string& out)
{
    std::vector<Step> steps;
    if (!parse_script(o.script, steps)) {
        fprintf(stderr, "blockie-shot: bad --script %s\n", o.script.c_str());
        return 2;
    }
    BootGates g;
    if (!po.gates.empty() && !(g = read_gates(po.gates)).read) {
        fprintf(stderr, "blockie-shot: cannot read %s\n", po.gates.c_str());
        return 1;
    }
    BootGates pick = g;
    if (!po.pick_gates.empty() && !(pick = read_gates(po.pick_gates)).read) {
        fprintf(stderr, "blockie-shot: cannot read %s\n", po.pick_gates.c_str());
        return 1;
    }
    PowerMenu m;
    m.reread = [&pick]() { return pick; };
    int64_t a0 = std::max<int64_t>(0, o.arrive);
    std::vector<PmStep> plan;
    int64_t open_at = a0;
    if (po.surface == "reboot" || po.surface == "confirm") {
        PowerMenu probe;
        probe.open(g, false, 0);
        int reboot = index_of(probe.view(0).items, "reboot");
        if (reboot < 0) {
            fprintf(stderr, "blockie-shot: --powermenu %s needs --gates with state=default\n", po.surface.c_str());
            return 2;
        }
        bool confirm = po.surface == "confirm";
        if (confirm && (g.android != "bootable" || g.ret != "keys")) {
            fprintf(stderr, "blockie-shot: --powermenu confirm needs --gates with android=bootable and return=keys\n");
            return 2;
        }
        open_at = a0 - (confirm ? 4000 : 2000);
        auto tap = [&plan](int64_t t, const char* key) {
            plan.push_back({t, key, true});
            plan.push_back({t + 60, key, false});
        };
        for (int i = 0; i < reboot; i++)
            tap(open_at + 500 + 100 * i, "down");
        if (confirm) {
            tap(a0 - 3000, "accept");
            tap(a0 - 2000, "down");
        }
        tap(a0, "accept");
    } else if (po.surface != "power") {
        fprintf(stderr, "blockie-shot: --powermenu takes power, reboot or confirm\n");
        return 2;
    }
    for (const Step& st : steps) {
        if (st.pct >= 0) {
            fprintf(stderr, "blockie-shot: the power menu frames take MS steps, not %d%%\n", st.pct);
            return 2;
        }
        plan.push_back({st.ms, st.key, st.down});
    }
    std::stable_sort(plan.begin(), plan.end(), [](const PmStep& x, const PmStep& y) { return x.t < y.t; });

    FILE* side = nullptr;
    if (!frames_dir.empty() && !(side = fopen((frames_dir + "/frames.txt").c_str(), "w"))) {
        fprintf(stderr, "blockie-shot: cannot write %s/frames.txt\n", frames_dir.c_str());
        return 1;
    }
    bool opened = false;
    size_t next = 0;
    Repeat rep;
    std::string keys;
    auto log_key = [&keys](const std::string& k) { keys += (keys.empty() ? "" : ",") + k; };
    auto apply = [&](int64_t t, const std::string& key, bool down) {
        log_key(key + (down ? ".d" : ".u"));
        PadAction a = PadAction::Up;
        if (key == "fail") {
            if (down)
                m.fail("exit-code", t);
        } else if (key == "term") {
            if (down)
                m.term(t);
        } else if (action_of(key, a)) {
            if (down) {
                m.action(a, false, t);
                if (a == PadAction::Up || a == PadAction::Down)
                    rep.press(a, kFromKeys, t);
            } else if (a == PadAction::Up || a == PadAction::Down) {
                rep.release(a, kFromKeys, t);
            }
        }
    };
    auto advance = [&](int64_t tf) {
        for (;;) {
            int64_t to = opened ? kNever : open_at;
            int64_t te = next < plan.size() ? plan[next].t : kNever;
            int64_t tr = rep.active() ? rep.deadline() : kNever;
            int64_t t = std::min({to, te, tr});
            if (t > tf || t == kNever)
                break;
            if (opened)
                m.tick(t);
            if (t == to) {
                m.open(g, false, t);
                if (po.idle > 0)
                    m.set_idle_ms(po.idle);
                opened = true;
            } else if (t == te) {
                const PmStep& st = plan[next++];
                if (opened)
                    apply(t, st.key, st.down);
            } else {
                PadAction a = PadAction::Up;
                if (rep.due(t, a)) {
                    log_key(a == PadAction::Down ? "down.r" : "up.r");
                    m.action(a, true, t);
                }
            }
            std::string ln;
            while (m.take_line(ln))
                fprintf(stderr, "blockie-shot: %lld ms line %s\n", (long long)t, ln.c_str());
        }
        if (opened)
            m.tick(tf);
        std::string ln;
        while (m.take_line(ln))
            fprintf(stderr, "blockie-shot: %lld ms line %s\n", (long long)tf, ln.c_str());
    };
    InkLayer ink;
    Buffer layer, comp;
    std::vector<uint32_t> argb((size_t)kPortraitW * kPortraitH);
    std::vector<AlphaRegion> regions;
    auto render = [&](int64_t tf) {
        regions.clear();
        Canvas c(layer.surface());
        if (opened && !m.done())
            draw_powermenu(c, fonts, ink, m.view(tf), theme(), regions);
        write_argb(layer.surface(), regions, argb.data(), kPortraitW);
        composite_argb(argb.data(), kPortraitW, plate, comp.surface());
    };
    int written = 0;
    int tail = o.tail;
    for (int n = 0;; n++) {
        int64_t tf = o.at >= 0 && frames_dir.empty() ? o.at : frame_ms(n);
        advance(tf);
        render(tf);
        if (side && tf >= o.from) {
            if (!write_png(frame_path(frames_dir, written), comp.surface())) {
                fprintf(stderr, "blockie-shot: cannot write %s\n", frame_path(frames_dir, written).c_str());
                fclose(side);
                return 1;
            }
            std::string line = std::to_string(written) + " " + std::to_string(tf) + " " +
                               pm_surface_name(opened && !m.done() ? m.surface() : PmSurface::None);
            for (const AlphaRegion& r : regions)
                line += " " + std::to_string(r.r.x0) + "," + std::to_string(r.r.y0) + "," + std::to_string(r.r.x1) +
                        "," + std::to_string(r.r.y1) + ":" + std::to_string(region_a8(r.a256));
            PmView pv = m.view(tf);
            int focus = pv.surface == PmSurface::Sheet                                       ? pv.sheet.focus
                        : pv.surface == PmSurface::Power || pv.surface == PmSurface::Confirm ? pv.focus
                                                                                              : -1;
            line += " focus=" + std::to_string(focus);
            if (!keys.empty())
                line += " keys=" + keys;
            if (fprintf(side, "%s\n", line.c_str()) < 0) {
                fprintf(stderr, "blockie-shot: cannot write %s/frames.txt\n", frames_dir.c_str());
                fclose(side);
                return 1;
            }
            written++;
        }
        if (o.at >= 0 && tf >= o.at)
            break;
        if (tf > kDriveCapMs) {
            fprintf(stderr, "blockie-shot: the power menu frames passed the %lld ms cap\n", (long long)kDriveCapMs);
            if (side)
                fclose(side);
            return 1;
        }
        keys.clear();
        PmSurface ps = m.surface();
        bool menu_up = ps == PmSurface::Power || ps == PmSurface::Sheet || ps == PmSurface::Confirm;
        bool pending = !m.done() && ((ps == PmSurface::Toast && !m.probing()) || (po.idle > 0 && menu_up));
        if (opened && next >= plan.size() && !rep.active() && !m.animating(tf) && !pending) {
            if (tail-- > 0)
                continue;
            break;
        }
    }
    if (side && fclose(side) != 0) {
        fprintf(stderr, "blockie-shot: cannot close %s/frames.txt\n", frames_dir.c_str());
        return 1;
    }
    if (!out.empty() && !write_png(out, comp.surface())) {
        fprintf(stderr, "blockie-shot: cannot write %s\n", out.c_str());
        return 1;
    }
    fprintf(stderr, "blockie-shot: power menu %s, %d frames\n", po.surface.c_str(), written);
    return 0;
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
    bool items = false;
    bool light = false;
    PageOpts page_opts;
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
    PmShotOpts pm;
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
        else if (a == "--sheet") {
            page_opts.sheet = true;
            if (i + 1 < argc && (std::string(argv[i + 1]) == "tz" || std::string(argv[i + 1]).rfind("tz:", 0) == 0))
                page_opts.sheet_arg = argv[++i];
        }
        else if (a == "--sheetfocus" && i + 1 < argc)
            page_opts.sheet_focus = atoi(argv[++i]);
        else if (a == "--sheetrows" && i + 1 < argc) {
            page_opts.rows = atoi(argv[++i]);
            if (page_opts.rows != 6 && page_opts.rows != 7) {
                fprintf(stderr, "blockie-shot: --sheetrows takes 6 or 7\n");
                return 2;
            }
        }
        else if (a == "--pivot" && i + 1 < argc)
            page_opts.pivot = argv[++i];
        else if (a == "--set" && i + 1 < argc)
            page_opts.sets.push_back(argv[++i]);
        else if (a == "--hide" && i + 1 < argc)
            page_opts.hides.push_back(argv[++i]);
        else if (a == "--battery" && i + 1 < argc)
            page_opts.battery = argv[++i];
        else if (a == "--volume" && i + 1 < argc)
            page_opts.volume = argv[++i];
        else if (a == "--radio" && i + 1 < argc)
            page_opts.radio = argv[++i];
        else if (a == "--storage" && i + 1 < argc)
            page_opts.storage = argv[++i];
        else if (a == "--about" && i + 1 < argc)
            page_opts.about = argv[++i];
        else if (a == "--toast" && i + 1 < argc)
            page_opts.toast = argv[++i];
        else if (a == "--pagefocus" && i + 1 < argc)
            page_opts.focus = std::max(0, atoi(argv[++i]));
        else if (a == "--zone" && i + 1 < argc) {
            page_opts.zone = argv[++i];
            page_opts.zone_set = true;
        }
        else if (a == "--zone-missing")
            page_opts.zone_missing = true;
        else if (a == "--zonetab" && i + 1 < argc)
            page_opts.zonetab = argv[++i];
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
        else if (a == "--left" && i + 1 < argc) {
            std::string v = argv[++i];
            if (v != "t5" && v != "l0") {
                fprintf(stderr, "blockie-shot: --left takes t5 or l0\n");
                return 2;
            }
            drive.left = v == "l0" ? LeftMove::Fade : LeftMove::T5;
        }
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
            std::string v = argv[++i];
            if (v != "band" && v != "full") {
                fprintf(stderr, "blockie-shot: --gridscroll takes band or full\n");
                return 2;
            }
            drive.grid_band = v == "band";
        }
        else if (a == "--startup")
            drive.startup = true;
        else if (a == "--exit" && i + 1 < argc) {
            drive.exit_class = argv[++i];
            int status = 0;
            if (drive.exit_class != "spawn" && !exit_status(drive.exit_class, status)) {
                fprintf(stderr, "blockie-shot: --exit takes quit, crash, busy, config, exec, core, audio or spawn\n");
                return 2;
            }
        }
        else if (a == "--quitstamp")
            drive.quit_stamp = true;
        else if (a == "--libretro" && i + 1 < argc)
            drive.libretro = argv[++i];
        else if (a == "--arrive" && i + 1 < argc)
            drive.arrive = atoll(argv[++i]);
        else if (a == "--script" && i + 1 < argc)
            drive.script = argv[++i];
        else if (a == "--powermenu" && i + 1 < argc)
            pm.surface = argv[++i];
        else if (a == "--gates" && i + 1 < argc)
            pm.gates = argv[++i];
        else if (a == "--pickgates" && i + 1 < argc)
            pm.pick_gates = argv[++i];
        else if (a == "--plate" && i + 1 < argc)
            pm.plate = argv[++i];
        else if (a == "--idle" && i + 1 < argc)
            pm.idle = atoi(argv[++i]);
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
    bool page_frames = !drive.on && !sequence && page == "settings" && !frames_dir.empty();
    bool pm_shot = !drive.on && !pm.surface.empty() && (!out.empty() || !frames_dir.empty());
    if (!driven && !page_frames && !pm_shot && ((out.empty() && !sequence) || (sequence && frames_dir.empty()))) {
        fprintf(stderr,
                "usage: blockie-shot OUT.png [--focus selector|details] [--fonts DIR] [--tiles DIR] [--header TEXT]"
                " [--tilefocus N] [--sysfocus N] [--systems N] [--menutop N] [--items] [--btsub TEXT]"
                " [--light] [--page settings] [--pivot WORD] [--pagefocus N] [--sheet [tz|tz:REGION]] [--sheetfocus N]"
                " [--sheetrows 6|7] [--zone ID] [--zone-missing] [--zonetab PATH] [--set ID=VALUE] [--hide ID]"
                " [--battery CAP:STATUS|unknown] [--volume PCT[:muted]|fail] [--radio on|off|gone]"
                " [--storage FREE:TOTAL:SYSFREE:SYSTOTAL|nocard:SYSFREE:SYSTOTAL] [--about OS_RELEASE|FRONTEND|KERNEL]"
                " [--toast LINE1|LINE2] [--scatter]"
                " [--gamedetail SYSTEM|TITLE] [--bt unavailable|off|empty|list|searching|sheet|confirm]\n"
                "       blockie-shot --pageflip push|pop --frames DIR [same options]\n"
                "       blockie-shot --page settings --frames DIR [--script KEY|fail|missing:d|u:MS,...] [--from MS]"
                " [--tail N]"
                " [same options]\n"
                "       blockie-shot --drive [OUT.png] [--frames DIR] [--script KEY|ret|cover|theme:d|u:MS|PCT%%,...]"
                " [--at MS] [--from MS]"
                " [--recentn N] [--sysgames N] [--noart] [--title0 TEXT] [--titles FILE] [--recenttitles FILE]"
                " [--recentsys NAME[,NAME...]] [--grid g1|g2] [--slots a|c] [--left t5|l0]"
                " [--t9clip pane|edge] [--bar content|slot] [--gridscroll band|full] [--startup] [--arrive MS]"
                " [--tail N] [--cadence MS] [--repeat-covers] [--fallback FONT]"
                " [--exit quit|crash|busy|config|exec|core|audio|spawn] [--quitstamp] [--libretro DIR] [same options]\n"
                "       blockie-shot --pops FRAMES_DIR [--no-lead] [--summary FILE] [--case NAME]\n"
                "       blockie-shot --powermenu power|reboot|confirm [OUT.png] [--frames DIR] [--gates FILE]"
                " [--pickgates FILE] [--plate home|plain] [--idle MS] [--script KEY|fail|term:d|u:MS,...] [--arrive MS]"
                " [--at MS]"
                " [--from MS] [--tail N] [same options]\n");
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
    if (!page_opts.zonetab.empty() && access(page_opts.zonetab.c_str(), R_OK) != 0)
        fprintf(stderr, "blockie-shot: cannot read %s, the sheet lists only utc\n", page_opts.zonetab.c_str());
    Zones zones = page_opts.zonetab.empty() ? Zones() : load_zones(page_opts.zonetab);
    if (!bt_sub.empty())
        page_opts.btsub = bt_sub;
    drive.page = page_opts;
    if (drive.on)
        return run_drive(fonts, drive, out, frames_dir, systems, items, bt_sub, sys_focus, tiles_dir, recent_dir,
                         zones);

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

    if (pm_shot) {
        if (pm.plate != "home" && pm.plate != "plain") {
            fprintf(stderr, "blockie-shot: --plate takes home or plain\n");
            return 2;
        }
        Buffer plate;
        Canvas pc(plate.surface());
        if (pm.plate == "plain")
            pc.fill(kScreen, kPlainPlate);
        else
            draw_selector(pc, fonts, model, focus, scatter, detail);
        return run_pm_shot(fonts, pm, drive, plate.surface(), frames_dir, out);
    }

    InkLayer ink;
    SettingsPage sp;
    if (!page_base(sp, fonts, page_opts, zones) || !page_pose(sp, page_opts, zones))
        return 2;
    if (page_opts.sheet) {
        SettingsView pv = sp.view(0);
        if (pv.sheet.open)
            fprintf(stderr, "blockie-shot: sheet %s rows %d\n", pv.sheet.title, pv.sheet.count);
        else if (pv.rowsheet.open)
            fprintf(stderr, "blockie-shot: row sheet %s lines %zu\n", pv.rowsheet.title, pv.rowsheet.lines.size());
    }
    if (page_frames)
        return run_page_frames(sp, fonts, drive, zones, frames_dir, out);
    SettingsView sv = sp.view(0);

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
