#include <algorithm>
#include <array>
#include <chrono>
#include <climits>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../core/curves.h"
#include "../gfx/canvas.h"
#include "../gfx/font.h"
#include "../gfx/glyph.h"
#include "../gfx/pixel.h"
#include "../gfx/png.h"
#include "../gfx/wayland.h"
#include "../ui/roles.h"
#include "../ui/size.h"
#include "bt_model.h"
#include "dumpz.h"
#include "home.h"
#include "homeflow.h"
#include "decodeq.h"
#include "image.h"
#include "ink.h"
#include "jpeg.h"
#include "launch.h"
#include "launchflow.h"
#include "layer_mem.h"
#include "library.h"
#include "motion.h"
#include "nav.h"
#include "pageform.h"
#include "pagelog.h"
#include "pagerows.h"
#include "pops.h"
#include "portmsg.h"
#include "powermenu.h"
#include "repeat.h"
#include "screen_bt.h"
#include "screen_selector.h"
#include "screen_settings.h"
#include "screen_store.h"
#include "sysinfo.h"
#include "store.h"
#include "store_flow.h"
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

const char* page_word(Screen s)
{
    return s == Screen::Settings ? settings_word() : s == Screen::Bluetooth ? bluetooth_word() : nullptr;
}

bool head_tiles(const PageRun* run, int64_t tf)
{
    bool under = false;
    if (!run || !head_side(run->form, under))
        return true;
    const PageHead& h = form_head(run->form);
    auto tiles = [](const std::vector<PageRow>& v, int y0, int y1) {
        int x = 0;
        for (const PageRow& e : v) {
            if (e.r.x0 != x || e.r.x1 <= e.r.x0 || e.r.y0 != y0 || e.r.y1 != y1)
                return false;
            x = e.r.x1;
        }
        return v.empty() || x == kPortraitW;
    };
    int band = px(tok::headerBand), s0 = px(tok::pivotTop), s1 = px(tok::pivotBottom);
    bool ok = !h.letters.empty() && tiles(h.letters, 0, band) && tiles(h.tabs, s0, s1);
    fprintf(stderr,
            "blockie-shot: %lld ms page tiling check %s, %zu letters over 0 to %d x 0 to %d, %zu tabs over 0 to %d "
            "x %d to %d\n",
            (long long)tf, ok ? "PASS" : "FAILED", h.letters.size(), kPortraitW, band, h.tabs.size(), kPortraitW, s0,
            s1);
    return ok;
}

bool word_check(Fonts& fonts, const char* word, const Surface& rest, int64_t tf = -1, bool strip = true)
{
    Buffer chrome;
    Canvas c(chrome.surface());
    draw_page_ground(c, theme());
    draw_page_chrome(c, fonts, word, nullptr, theme());
    Surface w = chrome.surface();
    int rows = px(strip ? tok::pivotTop : tok::headerBand), bad = -1;
    for (int y = 0; y < rows && bad < 0; y++)
        if (!std::equal(rest.row(y), rest.row(y) + rest.w, w.row(y)))
            bad = y;
    char at[32] = "";
    if (tf >= 0)
        snprintf(at, sizeof at, "%lld ms ", (long long)tf);
    if (bad < 0)
        fprintf(stderr, "blockie-shot: %s%s word layer check PASS, the rows above y %d equal the page chrome alone\n",
                at, word, rows);
    else
        fprintf(stderr, "blockie-shot: %s%s word layer check FAILED, row %d differs from the page chrome alone\n", at,
                word, bad);
    return bad < 0;
}

bool cut_check(const Surface& s, const std::vector<PageRow>& letters, const char* word, int64_t tf)
{
    LiveF::T g = LiveF::pack(theme().bg);
    int band = px(tok::headerBand), bad = -1;
    for (size_t k = 1; k < letters.size() && bad < 0; k++)
        for (int y = 0; y < band && bad < 0; y++)
            if (s.row(y)[letters[k].r.x0] != g)
                bad = letters[k].r.x0;
    char at[32] = "";
    if (tf >= 0)
        snprintf(at, sizeof at, "%lld ms ", (long long)tf);
    if (bad < 0)
        fprintf(stderr, "blockie-shot: %s%s word cut check PASS, every interior letter cut column is ground in rows 0 "
                        "to %d\n",
                at, word, band);
    else
        fprintf(stderr, "blockie-shot: %s%s word cut check FAILED, cut column %d carries ink\n", at, word, bad);
    return bad < 0;
}

bool clip_check(Fonts& fonts, const Surface& word)
{
    Buffer whole;
    Canvas c(whole.surface());
    draw_page_ground(c, theme());
    draw_page_chrome(c, fonts, settings_word(), nullptr, theme(), false);
    Surface k = whole.surface();
    LiveF::T g = LiveF::pack(theme().bg);
    int y0 = px(tok::pivotTop), y1 = px(tok::headerBand), below = 0, bx = -1, by = -1;
    for (int y = y0; y < y1; y++)
        for (int x = 0; x < word.w; x++) {
            below += k.row(y)[x] != g;
            if (by < 0 && word.row(y)[x] != g) {
                bx = x;
                by = y;
            }
        }
    if (by < 0 && below > 0)
        fprintf(stderr, "blockie-shot: settings clipped word check PASS, the word layer is ground in rows %d to %d, "
                        "where the whole word draws %d pixels\n",
                y0, y1, below);
    else if (by < 0)
        fprintf(stderr, "blockie-shot: settings clipped word check FAILED, the whole word draws nothing below y %d\n",
                y0);
    else
        fprintf(stderr, "blockie-shot: settings clipped word check FAILED, pixel %d,%d of the word layer carries ink "
                        "below y %d\n",
                bx, by, y0);
    return by < 0 && below > 0;
}

int shot_bt(const std::string& out, const std::string& state, Fonts& fonts, bool hp)
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
    if (!hp || state == "sheet" || state == "confirm")
        return 0;
    return word_check(fonts, bluetooth_word(), buf.surface(), -1, false) ? 0 : 1;
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
            st.key != "term" && st.key != "power" && st.key != "net" && !action_of(st.key, pa))
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
    std::string steam;
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
    struct stat sb;
    if (!o.steam.empty() && (stat(o.steam.c_str(), &sb) != 0 || !S_ISREG(sb.st_mode))) {
        fprintf(stderr, "blockie-shot: --steam takes a steam.tsv file, not %s\n", o.steam.c_str());
        return false;
    }
    if (!o.steam.empty() && read_steam(o.steam).signed_in)
        sp.set_row(kSteamRow, steam_row(true), 0);
    sp.on_action = [](const std::string& id) { fprintf(stderr, "blockie-shot: action %s\n", id.c_str()); };
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
    int recent_in = -1;
    int sys_games = 12;
    bool noart = false;
    std::string title0;
    std::string titles;
    std::string recent_titles;
    std::string recent_system = "Synth";
    bool big = false;
    SlotMap slots = SlotMap::RowMajor;
    bool fast = true;
    bool hp = false;
    std::string word_dir;
    bool t9_edge = false;
    bool bar_slot = false;
    bool grid_band = true;
    bool startup = false;
    int64_t startup_at = -1;
    int64_t input_lead = 0;
    int64_t arrive = -1;
    bool late_sys = false;
    std::vector<int> artless;
    std::vector<int> artless_sys;
    std::string blocks;
    std::string dump;
    std::string pane_blocks;
    std::string pane_dump;
    std::string script;
    int64_t from = 0;
    int64_t at = -1;
    int tail = 0;
    double cadence = 0;
    bool repeat_covers = false;
    std::string exit_class = "quit";
    bool quit_stamp = false;
    bool port = false;
    HelperOp helper = HelperOp::None;
    std::string ports_dir;
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
constexpr int64_t kMsgShotCapMs = 120000;
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
    } kExits[] = {{"quit", 0},   {"crash", 70}, {"oom", 1},    {"busy", 75},
                  {"config", 64}, {"exec", 127}, {"core", 69}, {"audio", 71}};
    for (const auto& e : kExits)
        if (name == e.name) {
            status = W_EXITCODE(e.code, 0);
            return true;
        }
    return false;
}

bool all_px(const Surface& s, uint32_t v)
{
    for (int y = 0; y < s.h; y++)
        for (int x = 0; x < s.w; x++)
            if (s.at(x, y) != v)
                return false;
    return true;
}

bool all_black(const Surface& s) { return all_px(s, LiveF::pack(launch_ground())); }

bool same_px(const Surface& a, const Surface& b)
{
    for (int y = 0; y < a.h; y++)
        if (!std::equal(a.row(y), a.row(y) + a.w, b.row(y)))
            return false;
    return true;
}

bool same_span(const Surface& a, const Surface& b, int x0, int x1)
{
    x0 = std::max(0, x0);
    x1 = std::min(x1, a.w);
    for (int y = 0; y < a.h && x0 < x1; y++)
        if (!std::equal(a.row(y) + x0, a.row(y) + x1, b.row(y) + x0))
            return false;
    return true;
}

void copy_surface(const Surface& src, const Surface& dst)
{
    for (int y = 0; y < dst.h; y++)
        copy_px(dst.row(y), src.row(y), dst.w);
}

bool listed(const std::vector<int>& v, int i) { return std::find(v.begin(), v.end(), i) != v.end(); }

FlowOpts flow_opts(const DriveOpts& o)
{
    FlowOpts f;
    f.big = o.big;
    f.slots = o.slots;
    f.lang = true;
    f.fast = o.fast;
    f.t9_edge = o.t9_edge;
    f.bar_slot = o.bar_slot;
    f.grid_band = o.grid_band;
    return f;
}

const char* left_name(LeftMove m)
{
    switch (m) {
    case LeftMove::Fade: return "l0";
    case LeftMove::T5: return "t5";
    case LeftMove::Stepper: return "stepper";
    }
    return "?";
}

bool in_rect(Rect r, int x, int y) { return x >= r.x0 && x < r.x1 && y >= r.y0 && y < r.y1; }

bool covered_by(const std::vector<Rect>& rs, int x, int y)
{
    for (const Rect& r : rs)
        if (in_rect(r, x, y))
            return true;
    return false;
}

uint32_t shaded(uint32_t src, uint32_t ground, int e, int sigma)
{
    double f = std::min(1.0, e / 65280.0 * 256.0 / sigma);
    uint32_t out = 0;
    for (int sh = 0; sh < 24; sh += 8) {
        double g = ground >> sh & 255, v = src >> sh & 255;
        out |= (uint32_t)std::lround(g + f * (v - g)) << sh;
    }
    return out;
}

bool near_px(uint32_t a, uint32_t b, int tol)
{
    for (int sh = 0; sh < 24; sh += 8)
        if (std::abs((int)(a >> sh & 255) - (int)(b >> sh & 255)) > tol)
            return false;
    return true;
}

struct PaneView {
    const eng::Instance* in = nullptr;
    const eng::Frame* frame = nullptr;
};

bool blocks_hold(const LeftPlan& lp, const std::vector<BlockPose>& poses, const Surface& a, const Surface& b,
                 const Surface& frame, int x_end, uint32_t ground, int& bad_x, int& bad_y, PaneView pane = PaneView{})
{
    int w = std::min(x_end, frame.w), h = frame.h;
    Rect all{0, 0, w, h};
    std::vector<int> owner((size_t)w * h, -1);
    std::vector<uint32_t> want((size_t)w * h, 0);
    auto mark = [&](int i, int x, int y, uint32_t v) {
        owner[(size_t)y * w + x] = i;
        want[(size_t)y * w + x] = v;
    };
    auto fail = [&](int x, int y) {
        bad_x = x;
        bad_y = y;
        return false;
    };
    std::vector<Rect> covers;
    for (const BlockPose& q : poses)
        if (q.top)
            covers.push_back(block_cover(lp, q));
    for (size_t i = 0; i < poses.size(); i++) {
        const BlockPose& q = poses[i];
        const Block& k = lp.blocks[(size_t)q.block];
        const Surface& src = k.wide ? b : a;
        if (q.a256 != 256)
            return fail(q.rect.x0, q.rect.y0);
        Rect vis = q.rect.intersect(q.clip).intersect(all);
        if (vis.empty())
            continue;
        Rect box = block_box(lp, q).intersect(all);
        int dx = q.rect.x0 - k.cell.x0, dy = q.rect.y0 - k.cell.y0;
        bool under = !q.top;
        for (int y = box.y0; y < box.y1; y++)
            for (int x = box.x0; x < box.x1; x++) {
                bool tile = in_rect(q.rect, x, y);
                if (tile ? !(k.art && in_rect(vis, x, y)) : !k.focus)
                    continue;
                if (under && covered_by(covers, x, y))
                    continue;
                mark((int)i, x, y, src.at(x - dx, y - dy));
            }
    }
    Rect gr = block_glow_rect(lp);
    const Glow& gv = glow_view(lp);
    bool glow = lp.glow_on && gv.inited && gv.e.size() == (size_t)(gr.x1 - gr.x0) * (gr.y1 - gr.y0);
    bool panes = pane.in && pane.frame && pane.in->cover_on();
    Rect pr = panes ? pane.in->rect() : Rect{};
    int pw = pr.x1 - pr.x0;
    std::vector<int> pose_of;
    for (size_t j = 0; panes && j < pane.frame->poses.size(); j++) {
        size_t id = (size_t)(pane.frame->blocks[(size_t)pane.frame->poses[j].block].id & eng::kIdMask);
        if (pose_of.size() <= id)
            pose_of.resize(id + 1, -1);
        pose_of[id] = (int)j;
    }
    Rect band = lp.band.intersect(all);
    for (int y = band.y0; y < band.y1; y++)
        for (int x = band.x0; x < band.x1; x++) {
            if (in_rect(lp.header, x, y))
                continue;
            size_t pi = panes && in_rect(pr, x, y) ? (size_t)(y - pr.y0) * pw + (size_t)(x - pr.x0) : 0;
            if (panes && in_rect(pr, x, y) && pane.in->cover()[pi]) {
                size_t id = (size_t)(pane.in->ids()[pi] & eng::kIdMask);
                uint32_t pv = ground;
                if (id) {
                    int j = id < pose_of.size() ? pose_of[id] : -1;
                    if (j < 0)
                        return fail(x, y);
                    const eng::Pose& q = pane.frame->poses[(size_t)j];
                    const eng::Block& k = pane.frame->blocks[(size_t)q.block];
                    const Surface& src = k.source == 0 ? a : b;
                    int sx = x - q.rect.x0 + k.src.x0, sy = y - q.rect.y0 + k.src.y0;
                    if (!in_rect(q.clip, x, y) || !in_rect(src.rect(), sx, sy))
                        return fail(x, y);
                    const std::vector<eng::Shade>& sh = pane.frame->shades;
                    int sigma = (size_t)q.block < sh.size() ? sh[(size_t)q.block].sigma : 256;
                    pv = shaded(src.at(sx, sy), ground, pane.in->e()[pi], sigma);
                }
                if (!near_px(frame.at(x, y), pv, id ? 2 : 0))
                    return fail(x, y);
                continue;
            }
            size_t o = (size_t)y * w + x;
            uint32_t v = ground;
            int tol = 0;
            if (owner[o] >= 0) {
                v = want[o];
                if (glow && in_rect(gr, x, y)) {
                    const Block& k = lp.blocks[(size_t)poses[(size_t)owner[o]].block];
                    int e = gv.e[(size_t)(y - gr.y0) * (gr.x1 - gr.x0) + (x - gr.x0)];
                    v = shaded(v, ground, e, block_sigma(lp, k));
                    tol = 2;
                }
            }
            if (!near_px(frame.at(x, y), v, tol))
                return fail(x, y);
        }
    return true;
}

bool shows_art(const Surface& s, Rect r, uint32_t ground)
{
    r = r.intersect(s.rect());
    for (int y = r.y0; y < r.y1; y++)
        for (int x = r.x0; x < r.x1; x++)
            if (s.at(x, y) != ground)
                return true;
    return false;
}

void write_pose(FILE* f, int n, const LeftPlan& lp, const BlockPose& q)
{
    const Block& k = lp.blocks[(size_t)q.block];
    Rect box = block_box(lp, q);
    fprintf(f, "pose %d %d %d %d %d %d %d %d %d %d 0 0 %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n", n, q.block,
            q.rect.x0, q.rect.y0, q.rect.x1, q.rect.y1, q.clip.x0, q.clip.y0, q.clip.x1, q.clip.y1, q.top ? 1 : 0,
            k.art ? 1 : 0, k.focus ? 1 : 0, box.x0, box.y0, box.x1, box.y1, q.a256, (int)kOne, q.ring.x0, q.ring.y0,
            q.ring.x1, q.ring.y1, q.beneath ? 1 : 0);
}

std::vector<uint32_t> band_px(const Surface& s, Rect r)
{
    std::vector<uint32_t> out;
    out.reserve((size_t)(r.x1 - r.x0) * (r.y1 - r.y0));
    for (int y = r.y0; y < r.y1; y++)
        for (int x = r.x0; x < r.x1; x++)
            out.push_back(s.at(x, y));
    return out;
}

class Drive {
public:
    Drive(Fonts& fonts, const DriveOpts& o) : flow(fonts, flow_opts(o)), fonts_(fonts), o_(o) {}
    ~Drive()
    {
        if (blocks_)
            fclose(blocks_);
        if (inputs_)
            fclose(inputs_);
    }

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
            bool port = o_.port || g.system == kPortsSystem;
            std::string core = port ? std::string() : core_of(g.system);
            LaunchCheck gate = Launcher(".", o_.libretro).check(port ? kPortsSystem : g.system, core, o_.helper);
            if (gate == LaunchCheck::HelperBusy) {
                LaunchOutcome r = launch_helper_busy(o_.helper);
                toast_.show(now_, r.line1, r.line2, flow.nav.top() == Screen::Home);
                refused_ = r.line1;
                return LaunchStart::Refused;
            }
            bool absent = !o_.libretro.empty() && gate == LaunchCheck::CoreAbsent;
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
            launch_.start(true, active_, port);
            toast_.hide();
            fprintf(stderr, "blockie-shot: %lld ms launch on %s\n", (long long)now_, core.c_str());
            return LaunchStart::Leave;
        };
        bt_ = std::make_unique<BtPage>(fonts_, glyphs_);
        flow.draw_page = [this](Canvas& c, Screen s, int64_t now) {
            if (s == Screen::Bluetooth) {
                bt_->draw(c, ink_, now);
                return;
            }
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
        flow.page_rows = [this](Screen s) {
            if (s == Screen::Bluetooth)
                return bluetooth_page_rows(*bt_);
            return s == Screen::Settings ? settings_page_rows(settings_view()) : std::vector<PageRow>();
        };
        flow.page_head = [this](Screen s) {
            if (s == Screen::Bluetooth)
                return bluetooth_page_head(fonts_);
            return s == Screen::Settings ? settings_page_head(fonts_, settings_view()) : PageHead{};
        };
        flow.draw_word = [this](Canvas& c, const PageHead& h) {
            draw_page_ground(c, theme());
            draw_page_chrome(c, fonts_, h.word.c_str(), nullptr, theme(), h.word == settings_word());
            if (o_.hp && !cut_check(c.surface(), h.letters, h.word.c_str(), now_))
                hp_failed_ = true;
            if (o_.word_dir.empty())
                return;
            Buffer full;
            Canvas fc(full.surface());
            draw_page_ground(fc, theme());
            Surface band = c.surface(), w = full.surface();
            for (int y = 0; y < band.h && y < w.h; y++)
                copy_px(w.row(y), band.row(y), std::min(band.w, w.w));
            std::string path = o_.word_dir + "/" + h.word + "-word-" + (theme().light ? "light" : "dark") + ".png";
            if (write_png(path, w)) {
                fprintf(stderr, "blockie-shot: %lld ms word layer %s\n", (long long)now_, path.c_str());
            } else {
                fprintf(stderr, "blockie-shot: cannot write %s\n", path.c_str());
                hp_failed_ = true;
            }
        };
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
            flow.input(a, rep, now - o_.input_lead);
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
            if (late_.count(g.id) && (!arrived_ || (o_.recent_in >= 0 && g.id == kRecentIds + o_.recent_in)))
                return nullptr;
            auto it = art.find(g.id);
            return it == art.end() ? nullptr : it->second;
        };
        std::vector<HomeGame> list(recent_games.begin(),
                                   recent_games.begin() + std::min(recent_games.size(), (size_t)o_.recent_n));
        if (o_.arrive >= 0 && !o_.late_sys)
            for (const HomeGame& g : list)
                late_.insert(g.id);
        int focus = flow.nav.focus;
        if (o_.arrive >= 0 && o_.late_sys && focus >= 0 && focus < (int)games.size())
            for (const HomeGame& g : games[(size_t)focus])
                late_.insert(g.id);
        flow.home.deal_recent(flow.model, list);
        return true;
    }

    int run(const std::string& out, const std::string& frames_dir)
    {
        set_now(0);
        bool waiting = o_.startup_at > 0;
        if (waiting && frame_time(1) >= o_.startup_at) {
            fprintf(stderr, "blockie-shot: --startup-at %lld leaves fewer than 2 still frames before the startup\n",
                    (long long)o_.startup_at);
            return 1;
        }
        if (o_.startup && !waiting)
            flow.startup(0);
        if (waiting) {
            Canvas gc(last_.surface());
            gc.fill(kScreen, theme().bg);
        } else {
            flow.draw(last_.surface(), flow.model, flow.nav, false, flow.nav.pane, kScreen);
        }
        copy_surface(last_.surface(), shown_.surface());
        FILE* side = nullptr;
        if (!frames_dir.empty() && !(side = fopen((frames_dir + "/frames.txt").c_str(), "w"))) {
            fprintf(stderr, "blockie-shot: cannot write %s/frames.txt\n", frames_dir.c_str());
            return 1;
        }
        if (!o_.pane_blocks.empty() && !pane_log_.open(o_.pane_blocks, o_.pane_dump, "pane-")) {
            fprintf(stderr, "blockie-shot: cannot write %s\n", o_.pane_blocks.c_str());
            if (side)
                fclose(side);
            return 1;
        }
        if (!o_.blocks.empty() && !(blocks_ = fopen(o_.blocks.c_str(), "w"))) {
            fprintf(stderr, "blockie-shot: cannot write %s\n", o_.blocks.c_str());
            if (side)
                fclose(side);
            return 1;
        }
        if (!frames_dir.empty() && !(inputs_ = fopen((frames_dir + "/inputs.txt").c_str(), "w"))) {
            fprintf(stderr, "blockie-shot: cannot write %s/inputs.txt\n", frames_dir.c_str());
            fclose(side);
            return 1;
        }
        int written = 0;
        int tail = o_.tail;
        bool was_moving = false;
        bool was_home_moving = false;
        bool moved = false;
        for (int n = 0;; n++) {
            int64_t tf = frame_time(n);
            bool stop = o_.at >= 0 && tf >= o_.at;
            if (stop)
                tf = o_.at;
            advance(tf);
            set_now(tf);
            fonts_.next_frame();
            bool wait = waiting && tf < o_.startup_at;
            if (waiting && !wait) {
                waiting = false;
                flow.startup(tf);
            }
            MotionCtl& ctl = flow.ctl;
            bool was_fold = ctl.active();
            bool was_details = ctl.details();
            FoldKind was_kind = ctl.kind();
            bool step_in = flow.take_step_in();
            bool still = !was_home_moving;
            bool motion = !wait && (was_moving || flow.animating(tf) || page_moving(tf) || toast_.animating(tf));
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
            } else if (kind == LaunchDraw::Home && !wait) {
                more = flow.frame(tf, last_.surface());
            }
            more = more || page_moving(tf);
            in_frame_ = false;
            was_home_moving = more;
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
            bool paging = ctl.active() && (ctl.kind() == FoldKind::Push || ctl.kind() == FoldKind::Pop);
            if (o_.hp && paging && !ctl.reversed() && ctl.t0() != page_t0_) {
                page_t0_ = ctl.t0();
                bool judged = still && o_.input_lead == 0;
                bool held = judged && same_px(prev_.surface(), last_.surface());
                bool tiled = head_tiles(flow.page_run(), tf);
                ok = ok && (held || !judged) && tiled;
                hp_failed_ = hp_failed_ || (judged && !held) || !tiled;
                fprintf(stderr, "blockie-shot: %lld ms page frame 0 check %s\n", (long long)tf,
                        !judged ? "not judged, the input leads the frame or the screen was moving before it"
                        : held  ? "the frame is the frame shown before the input"
                                : "FAILED");
            }
            if (step_in) {
                step = 1;
                bool split = block_details();
                bool rows = lang_details();
                bool judged = !rows || still;
                bool held = false;
                if (rows) {
                    held = still && same_px(prev_.surface(), last_.surface());
                } else {
                    Buffer ref;
                    flow.draw(ref.surface(), flow.kept(), flow.kept_focus(), true, Pane::Details, kScreen);
                    if (split) {
                        Buffer sel;
                        flow.draw(sel.surface(), flow.kept(), flow.kept_nav(), true, Pane::Selector, kScreen);
                        held = same_span(sel.surface(), last_.surface(), 0, kPaneLeftEdge) &&
                               same_span(ref.surface(), last_.surface(), kPaneLeftEdge, ref.surface().w);
                    } else {
                        held = same_px(ref.surface(), last_.surface());
                    }
                }
                ok = ok && (held || !judged);
                fprintf(stderr, "blockie-shot: %lld ms step in check %s\n", (long long)tf,
                        !judged ? "not judged, the home was moving on the frame before the input"
                        : !held ? "FAILED"
                        : rows  ? "the frame is the frame shown before the input, the pane keeping the menu focus"
                        : split ? "left of 436 the menu focus rest before the input, right of it the state before the "
                                  "input with Details focus, the designed right pane focus step"
                                : "the frame is the state before the input with Details focus");
            }
            if (was_fold && !ctl.active() && was_kind != FoldKind::Startup) {
                bool collapse_step = was_details && was_kind == FoldKind::Collapse;
                bool split = collapse_step && block_details();
                bool rows = collapse_step && lang_details();
                Buffer rest;
                if (collapse_step) {
                    step = 1;
                    details_rest(rest.surface(), split);
                    ok = same_px(rest.surface(), prev_.surface());
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
                    flow.draw(rest.surface(), flow.model, flow.nav, false, flow.nav.pane, kScreen);
                }
                bool waits = was_kind == FoldKind::Launch && launch_.phase() != LaunchPhase::Idle;
                fprintf(stderr, "blockie-shot: %lld ms %s end check %s\n", (long long)tf, fold_kind_name(was_kind),
                        !ok             ? "FAILED"
                        : rows          ? "the last composed frame equals the destination at menu focus, the pane "
                                          "keeping the menu focus"
                        : split         ? "the last composed frame is the menu focus rest left of 436 and the "
                                          "destination with Details focus right of it, the designed right pane focus "
                                          "step"
                        : collapse_step ? "the last composed frame equals the destination with Details focus"
                        : waits         ? "the leave lands on the wait's black"
                                        : "the composite at T equals a fresh render");
                if (was_kind != FoldKind::Launch && was_kind != FoldKind::Return) {
                    unsigned long long hf = frame_hash(prev_.surface()), hr = frame_hash(rest.surface());
                    const char* parts =
                        rows    ? ", the destination at menu focus across the frame, the pane keeping the menu focus"
                        : split ? ", menu focus left of 436, the designed right pane focus step right of it"
                                : "";
                    fprintf(stderr,
                            "blockie-shot: %lld ms %s land check %s, last fold frame %016llx, static rest render "
                            "%016llx%s\n",
                            (long long)tf, fold_kind_name(was_kind), hf == hr ? "PASS" : "FAILED", hf, hr, parts);
                }
                const char* word = page_word(flow.nav.top());
                if (o_.hp && word && (was_kind == FoldKind::Push || was_kind == FoldKind::Pop)) {
                    bool held = word_check(fonts_, word, last_.surface(), tf, flow.nav.top() == Screen::Settings);
                    if (flow.nav.top() == Screen::Bluetooth)
                        held = cut_check(last_.surface(), bluetooth_page_head(fonts_).letters, word, tf) && held;
                    ok = ok && held;
                    hp_failed_ = hp_failed_ || !held;
                }
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
                        (long long)ctl.back_e());
            }
            if (ctl.active() && ctl.t0() != start_logged_) {
                start_logged_ = ctl.t0();
                fprintf(stderr, "blockie-shot: %lld ms %s start from %d us to %d us\n", (long long)ctl.t0(),
                        fold_kind_name(ctl.kind()), ctl.from_us(), ctl.to_us());
            }
            const SlidePlan* sp = flow.plan();
            bool swap = sp && sp->left == LeftMove::T5 && ctl.active() &&
                        (ctl.kind() == FoldKind::Expand || ctl.kind() == FoldKind::Collapse);
            int64_t ft = ctl.forward_time(tf);
            int64_t k = ctl.kind() == FoldKind::Collapse && !ctl.reversed() ? slide_ms() - ft : ft;
            int leave_ms = fx_round(tok::motion_swapOut);
            bool same = swap_seen_ && (ctl.t0() == swap_t0_ ||
                                       (ctl.reversed() && !swap_back_ && ctl.t0() - ctl.back_e() == swap_t0_));
            bool crossed = same && std::min(swap_k_, k) < leave_ms && std::max(swap_k_, k) > 0;
            if (!step && swap && ((k > 0 && k <= leave_ms) || crossed)) {
                Buffer ref;
                const char* what = k <= 0 ? "landing" : k < leave_ms ? "leave" : k == leave_ms ? "hold" : "arrival";
                int64_t gap = n > 0 ? tf - frame_time(n - 1) : 0;
                bool short_back = ctl.reversed() && tf == ctl.t0() && ctl.back_e() <= gap;
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
            if (wait) {
                bool held = all_px(shown_.surface(), LiveF::pack(theme().bg));
                ok = ok && held;
                cause = " the plain ground before the frontend's first frame";
                fprintf(stderr, "blockie-shot: %lld ms startup wait check %s\n", (long long)tf,
                        held ? "the frame is plain ground with motion 0" : "FAILED");
            }
            if (o_.startup_at > 0 && motion && !moved) {
                moved = true;
                bool late = tf >= o_.startup_at;
                ok = ok && late;
                fprintf(stderr, "blockie-shot: %lld ms startup motion check %s %lld ms\n", (long long)tf,
                        late ? "the first motion frame is at or past" : "FAILED, the first motion frame is before",
                        (long long)o_.startup_at);
            }
            if (!block_frame(n, !frames_dir.empty() && tf >= o_.from ? written : -1, tf)) {
                ok = 0;
                cause = " the frame is not every block at its logged pose over ground";
            }
            if (dump_failed_ && !dump_logged_) {
                dump_logged_ = true;
                ok = 0;
                cause = " the e and block id dump could not be written";
            }
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
            bool pending = waiting || next_ < steps_.size() || repeat_.active() || (o_.arrive >= 0 && !arrived_);
            if (pending || more)
                tail = o_.tail;
            else if (o_.at < 0 && tail-- <= 0)
                break;
        }
        if (side && fclose(side) != 0) {
            fprintf(stderr, "blockie-shot: cannot close %s/frames.txt\n", frames_dir.c_str());
            return 1;
        }
        if (!pane_log_.close()) {
            fprintf(stderr, "blockie-shot: cannot close %s\n", o_.pane_blocks.c_str());
            return 1;
        }
        FILE* bl = blocks_;
        blocks_ = nullptr;
        if (bl && fclose(bl) != 0) {
            fprintf(stderr, "blockie-shot: cannot close %s\n", o_.blocks.c_str());
            return 1;
        }
        FILE* in = inputs_;
        inputs_ = nullptr;
        if (in && fclose(in) != 0) {
            fprintf(stderr, "blockie-shot: cannot close %s/inputs.txt\n", frames_dir.c_str());
            return 1;
        }
        if (!out.empty() && !write_png(out, shown_.surface())) {
            fprintf(stderr, "blockie-shot: cannot write %s\n", out.c_str());
            return 1;
        }
        fprintf(stderr, "blockie-shot: drive %d frames to %lld ms\n", written, (long long)now_);
        return dump_failed_ || hp_failed_ ? 1 : 0;
    }

private:
    SettingsView settings_view() const { return flow.opening() ? page.open_view(now_) : page.view(now_); }

    void end_check_input()
    {
        MotionCtl& ctl = flow.ctl;
        Buffer end, ref;
        if (!ctl.compose_end(end.surface()))
            return;
        bool collapse_step = ctl.details_play() && ctl.kind() == FoldKind::Collapse;
        bool split = collapse_step && block_details();
        bool rows = collapse_step && lang_details();
        if (collapse_step)
            details_rest(ref.surface(), split);
        else
            flow.draw(ref.surface(), flow.model, flow.nav, false, flow.nav.pane, kScreen);
        bool ok = same_px(end.surface(), ref.surface());
        if (!ok)
            input_ok_ = 0;
        fprintf(stderr, "blockie-shot: %lld ms %s end check on an input %s\n", (long long)now_,
                fold_kind_name(ctl.kind()),
                !ok             ? "FAILED"
                : rows          ? "the end pose equals the destination at menu focus, the pane keeping the menu focus"
                : split         ? "the end pose is the menu focus rest left of 436 and the destination with Details "
                                  "focus right of it, the designed right pane focus step"
                : collapse_step ? "the end pose equals the destination with Details focus"
                                : "the end pose equals a fresh render");
    }

    bool block_details() const
    {
        const SlidePlan* sp = flow.plan();
        return sp && block_form(sp->left) && sp->moves.glow_on;
    }

    bool lang_details() const { return block_details() && flow.plan()->lang_on; }

    void details_rest(const Surface& dst, bool split)
    {
        if (lang_details()) {
            flow.draw(dst, flow.model, flow.nav, false, Pane::Selector, kScreen);
            return;
        }
        flow.draw(dst, flow.model, flow.nav, false, Pane::Details, kScreen);
        if (!split)
            return;
        Buffer sel;
        flow.draw(sel.surface(), flow.model, flow.nav, false, Pane::Selector, kScreen);
        Surface s = sel.surface();
        for (int y = 0; y < dst.h; y++)
            copy_px(dst.row(y), s.row(y), std::min(kPaneLeftEdge, dst.w));
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
            int64_t span = flow.ctl.reversed() ? flow.ctl.back_e() : flow.ctl.run_length();
            st.ms = flow.ctl.t0() + span * st.pct / 100;
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
        if (inputs_)
            fprintf(inputs_, "%lld %s %d\n", (long long)now_, st.key.c_str(), st.down ? 1 : 0);
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

    void pane_frame(int n, int png_n, int64_t tf)
    {
        MotionCtl& ctl = flow.ctl;
        const SlidePlan* sp = flow.plan();
        bool fold = ctl.active() && (ctl.kind() == FoldKind::Expand || ctl.kind() == FoldKind::Collapse);
        if (!pane_log_.on() || !fold || !sp || !sp->pane || !sp->pane_frame)
            return;
        int64_t ft = ctl.forward_time(tf);
        int rest = pane_rest(*sp, ft, ctl.reversed(), sp->pane->settled());
        if (ctl.t0() != pane_t0_ || ctl.reversed() != pane_back_) {
            if (pane_t0_ < 0)
                pane_log_.surface(eng::kPaneSid, "pane", "step", sp->pane->rect());
            pane_t0_ = ctl.t0();
            pane_back_ = ctl.reversed();
            pane_log_.fold(ctl.t0(), "step", fold_kind_name(ctl.kind()), ctl.reversed(), 3 * sp->pane_stop,
                           *sp->pane_frame, sp->moves.collapse, sp->moves.redeal, eng::kPaneSid);
        }
        if (!pane_log_.frame(n, png_n, tf, ft, rest, *sp->pane_frame, *sp->pane, -1, eng::kPaneSid))
            dump_failed_ = true;
    }

    void page_frame_log(int n, int png_n, int64_t tf)
    {
        const PageRun* run = flow.page_run();
        if (run && !page_log(pane_log_, page_log_, flow.ctl, *run, n, png_n, tf))
            dump_failed_ = true;
    }

    bool block_frame(int n, int png_n, int64_t tf)
    {
        pane_frame(n, png_n, tf);
        page_frame_log(n, png_n, tf);
        MotionCtl& ctl = flow.ctl;
        const SlidePlan* sp = flow.plan();
        bool fold = ctl.active() && (ctl.kind() == FoldKind::Expand || ctl.kind() == FoldKind::Collapse);
        if (!blocks_ || !fold || !sp || !block_form(sp->left))
            return true;
        const LeftPlan& lp = sp->moves;
        bool fw = ctl.kind() == FoldKind::Expand && !ctl.reversed();
        Surface f = ctl.from(), t = ctl.to();
        Surface a = fw ? f : t, b = fw ? t : f;
        Surface frame = last_.surface();
        uint32_t ground = LiveF::pack(theme().bg);
        auto boundary = [&](int64_t at) { return std::clamp(slide_pose(*sp, at).boundary, kPaneSplit, frame.w); };
        int64_t ft = ctl.forward_time(tf);
        int64_t tl = block_time(lp, ft);
        int bx = boundary(ft);
        int rest = block_rest(lp, ft, tl);
        if (ctl.t0() != fold_t0_ || ctl.reversed() != fold_back_) {
            fold_t0_ = ctl.t0();
            fold_back_ = ctl.reversed();
            int64_t d3 = block_thirds(lp);
            fprintf(blocks_, "fold %lld %s %s %d %d %d %lld %zu\n", (long long)ctl.t0(), left_name(sp->left),
                    fold_kind_name(ctl.kind()), ctl.reversed() ? 1 : 0, lp.collapse ? 1 : 0, lp.redeal ? 1 : 0,
                    (long long)d3, lp.blocks.size());
            for (size_t i = 0; i < lp.blocks.size(); i++) {
                const Block& k = lp.blocks[i];
                fprintf(blocks_, "block %zu %lld %d %d %d %d %d %d %d %d %d %d %d %d %d\n", i, (long long)k.id,
                        k.wide ? 1 : 0, k.game, k.col, k.row, k.cell.x0, k.cell.y0, k.cell.x1, k.cell.y1,
                        k.art ? 1 : 0, k.focus ? 1 : 0, block_key(lp, (int)i) + 1, block_sigma(lp, k),
                        block_departs(lp, (int)i) ? 1 : 0);
            }
            int64_t dm = (d3 + 2) / 3, tail = kBlockTailMs;
            int64_t ends[2][2] = {{0, lp.collapse ? dm + tail : 0}, {d3, lp.collapse ? 0 : dm}};
            for (int e = 0; e < 2; e++) {
                int lb = boundary(ends[e][1]);
                std::vector<BlockPose> legs = block_legs(lp, ends[e][0], lb);
                fprintf(blocks_, "leg %d %lld %d %zu\n", e, (long long)ends[e][0], lb, legs.size());
                for (const BlockPose& q : legs)
                    write_pose(blocks_, -1, lp, q);
            }
        }
        std::vector<BlockPose> poses = block_poses(lp, ft, tl, bx);
        bool copied = lp.glow_on ? glow_view(lp).copied : rest != 0;
        fprintf(blocks_, "frame %d %d %lld %lld %lld %d %d %zu %d %d %d %d\n", n, png_n, (long long)tf,
                (long long)ft, (long long)tl, bx, rest, poses.size(), copied ? 1 : 0, lp.glow_on ? 1 : 0,
                block_settled(lp) ? 1 : 0, lp.glow_on && glow_view(lp).last == tf ? 1 : 0);
        if (!o_.dump.empty() && !dump_frame(n, lp, a, b))
            dump_failed_ = true;
        int arrived = 0, dark = 0;
        for (const BlockPose& q : poses) {
            write_pose(blocks_, n, lp, q);
            const Block& k = lp.blocks[(size_t)q.block];
            if (arrived_at_ < ctl.t0() || k.wide != fw || !late_.count(k.id) || !art.count(k.id))
                continue;
            arrived++;
            Rect vis = q.rect.intersect(q.clip);
            bool hidden = k.col == kGridColsWide - 1 && !lp.redeal && vis.empty() &&
                          !q.rect.intersect(block_clip(lp, bx)).empty();
            if (!k.art || hidden || !shows_art(k.wide ? b : a, k.cell.intersect(lp.band), ground) ||
                (!vis.empty() && !shows_art(frame, vis, ground)))
                dark++;
        }
        if (arrived && (arrive_logged_ != ctl.t0() || dark)) {
            arrive_logged_ = ctl.t0();
            fprintf(stderr, "blockie-shot: %lld ms arrive check %s, %d arrived blocks on the destination side\n",
                    (long long)tf,
                    dark ? "FAILED, an arrived block is clipped away inside the pane or shows ground"
                         : "every arrived block draws its art from this frame",
                    arrived);
        }
        if (dark)
            return false;
        if (copied)
            return true;
        int bad_x = -1, bad_y = -1;
        bool held = blocks_hold(lp, poses, a, b, frame, std::max(bx, kPaneLeftEdge), ground, bad_x, bad_y,
                                PaneView{sp->pane.get(), sp->pane_frame.get()});
        if (held)
            fprintf(stderr, "blockie-shot: %lld ms blocks check the frame is every block at its logged pose%s\n",
                    (long long)tf, ctl.reversed() ? ", reversed at e - s" : "");
        else
            fprintf(stderr, "blockie-shot: %lld ms blocks check FAILED at x %d y %d\n", (long long)tf, bad_x, bad_y);
        return held;
    }

    bool dump_frame(int n, const LeftPlan& lp, const Surface& a, const Surface& b)
    {
        const Glow& gl = glow_view(lp);
        Rect gr = block_glow_rect(lp);
        size_t px = (size_t)(gr.x1 - gr.x0) * (size_t)(gr.y1 - gr.y0);
        const Surface* src[2] = {&a, &b};
        const char* side[2] = {"a", "b"};
        char name[32];
        for (int i = 0; i < 2; i++) {
            unsigned long long h = frame_hash(*src[i]);
            if (h == dump_hash_[i])
                continue;
            std::vector<uint32_t> v = band_px(*src[i], gr);
            snprintf(name, sizeof name, "/%s-%05d.z", side[i], n);
            if (!dump_z(o_.dump + name, v.data(), v.size() * sizeof(uint32_t))) {
                fprintf(stderr, "blockie-shot: cannot write %s%s\n", o_.dump.c_str(), name);
                return false;
            }
            dump_hash_[i] = h;
            fprintf(blocks_, "surf %d %s %016llx %d\n", n, side[i], h, i == 0 && !lp.redeal ? kSwapSigma : 256);
        }
        bool have = gl.e.size() == px && gl.ids.size() == px && gl.age.size() == px;
        std::vector<uint16_t> e = have ? gl.e : std::vector<uint16_t>(px, 0);
        std::vector<uint8_t> ids = have ? gl.ids : std::vector<uint8_t>(px, 0);
        std::vector<uint8_t> age = have ? gl.age : std::vector<uint8_t>(px, (uint8_t)kGlowAgeMs);
        snprintf(name, sizeof name, "/e-%05d.z", n);
        bool ok = dump_z(o_.dump + name, e.data(), e.size() * sizeof(uint16_t));
        if (ok) {
            snprintf(name, sizeof name, "/id-%05d.z", n);
            ok = dump_z(o_.dump + name, ids.data(), ids.size());
        }
        if (ok) {
            snprintf(name, sizeof name, "/g-%05d.z", n);
            ok = dump_z(o_.dump + name, age.data(), age.size());
        }
        if (!ok)
            fprintf(stderr, "blockie-shot: cannot write %s%s\n", o_.dump.c_str(), name);
        else
            fprintf(blocks_, "dump %d %d %d %d %d\n", n, gr.x0, gr.y0, gr.x1, gr.y1);
        return ok;
    }

    void arrive()
    {
        arrived_ = true;
        arrive_held_ = false;
        arrived_at_ = now_;
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
            if (repeat_.due(t, a)) {
                if (inputs_)
                    fprintf(inputs_, "%lld repeat 1 %s\n", (long long)t, key_name(a));
                keys_.repeat(a, t);
            }
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
    int64_t start_logged_ = -1;
    Buffer rest_sel_;
    Buffer rest_det_;
    FILE* blocks_ = nullptr;
    FILE* inputs_ = nullptr;
    unsigned long long dump_hash_[2] = {0, 0};
    bool dump_failed_ = false;
    bool hp_failed_ = false;
    int64_t page_t0_ = -1;
    bool dump_logged_ = false;
    int64_t fold_t0_ = -1;
    bool fold_back_ = false;
    eng::PoseLog pane_log_;
    int64_t pane_t0_ = -1;
    bool pane_back_ = false;
    PageLogState page_log_;
    int64_t arrived_at_ = -1;
    int64_t arrive_logged_ = -1;
};

bool ports_library(const std::string& dir, Library& lib)
{
    const std::string tail = "/roms/ports";
    std::string d = dir;
    while (d.size() > 1 && d.back() == '/')
        d.pop_back();
    if (d.size() <= tail.size() || d.compare(d.size() - tail.size(), tail.size(), tail) != 0) {
        fprintf(stderr, "blockie-shot: --portsdir takes a DATA/roms/ports dir\n");
        return false;
    }
    if (!lib.open(d.substr(0, d.size() - tail.size()))) {
        fprintf(stderr, "blockie-shot: cannot open the library under %s\n", d.c_str());
        return false;
    }
    lib.rescan(kPortsSystem);
    return true;
}

bool ports_fixture(const std::string& dir, int side, std::vector<HomeGame>& out, SelectorItem& row,
                   std::unordered_map<int64_t, Art>& art)
{
    Library lib;
    if (!ports_library(dir, lib))
        return false;
    for (const SystemCfg& sc : lib.config())
        if (sc.id == kPortsSystem)
            row.name = sc.name;
    for (const auto& sw : lib.systems_with_games())
        if (sw.first.id == kPortsSystem)
            row.games = sw.second;
    DecodeQueue dq;
    JpegDecoder jpeg;
    std::unordered_set<int64_t> keep;
    for (const Game& g : lib.games(kPortsSystem)) {
        if (!g.present)
            continue;
        out.push_back({g.id, g.title.empty() ? g.name : g.title, row.name});
        keep.insert(g.id);
        for (const MediaRef& m : lib.media(g.id))
            if (m.type == "covers")
                dq.push({g.id, m.path, 1, 0, {side}, 1});
    }
    dq.set_window(1, keep);
    DecodeJob j;
    while (dq.pop(j)) {
        DecodedImage dec;
        bool ok = load_cover(jpeg, j.path, side, dec) && dec.ok();
        dq.done(j.game_id);
        if (ok && dq.accept(j.game_id) && dq.current(j.game_id, j.path, j.index_gen))
            art[j.game_id] = std::make_shared<const Image>(cover_tile(dec, side));
        fprintf(stderr, "blockie-shot: port %lld cover %s %s\n", (long long)j.game_id, j.path.c_str(),
                ok ? "decoded" : "none");
    }
    fprintf(stderr, "blockie-shot: %zu ports\n", out.size());
    return true;
}

int run_drive(Fonts& fonts, const DriveOpts& o, const std::string& out, const std::string& frames_dir,
              int systems, bool items, int sys_focus, const std::string& tiles_dir, const std::string& recent_dir,
              const Zones& zones)
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
    std::vector<HomeGame> ports;
    SelectorItem ports_row;
    if (!o.ports_dir.empty() &&
        !ports_fixture(o.ports_dir, o.big ? kBigTileSide : kTileSide, ports, ports_row, d->art))
        return 1;
    int with_ports = systems + (ports.empty() ? 0 : 1);
    int focus = std::clamp(sys_focus, 0, with_ports + (items ? 1 : 0) - 1);
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
            if (s == focus && !d->sys_covers.empty() && !listed(o.artless, i) && !listed(o.artless_sys, i))
                d->art[g.id] = d->sys_covers[(size_t)i % d->sys_covers.size()];
            list.push_back(g);
        }
        d->games.push_back(list);
        d->flow.model.systems.push_back({kNames[s], count, ""});
        d->flow.kinds.push_back(MenuKind::System);
    }
    if (!ports.empty()) {
        d->games.push_back(ports);
        d->flow.model.systems.push_back(ports_row);
        d->flow.kinds.push_back(MenuKind::System);
    }
    if (items)
        add_menu_items(d->flow.model, d->flow.kinds);
    for (int k = 0; k < kGridColsWide * Nav::kRows; k++) {
        HomeGame g;
        g.id = kRecentIds + k;
        g.name = rtitles.empty() ? "Synth Recent " + std::to_string(k + 1) : rtitles[(size_t)k % rtitles.size()];
        g.system = rsys[(size_t)k % rsys.size()];
        if (!d->recent_covers.empty() && !listed(o.artless, k))
            d->art[g.id] = d->recent_covers[(size_t)k % d->recent_covers.size()];
        d->recent_games.push_back(g);
    }
    if (o.recent_in >= 0) {
        if (o.arrive < 0 || focus >= systems || o.recent_in >= std::min(o.recent_n, (int)d->recent_games.size()) ||
            o.recent_in >= (int)d->games[(size_t)focus].size()) {
            fprintf(stderr, "blockie-shot: --recentin K needs --arrive MS, K under --recentn and the focused"
                            " system's game count, and a game system in focus\n");
            return 2;
        }
        HomeGame& g = d->recent_games[(size_t)o.recent_in];
        g.system = kNames[focus];
        d->games[(size_t)focus][(size_t)o.recent_in] = g;
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
    case PmSurface::Pause: return "pause";
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

struct MsgEvent {
    int64_t at = 0;
    std::vector<std::string> argv;
    std::vector<std::string> lines;
    PadAction key = PadAction::Up;
    int edge = 0;
};

bool msg_words(const std::string& line, std::vector<std::string>& out)
{
    out.clear();
    std::string cur;
    bool quoted = false, any = false;
    for (size_t i = 0; i < line.size(); i++) {
        char ch = line[i];
        if (ch == '"') {
            quoted = !quoted;
            any = true;
        } else if (ch == '\\' && quoted && i + 1 < line.size()) {
            char n = line[++i];
            cur += n == 'n' ? '\n' : n == 't' ? '\t' : n;
        } else if (ch == ' ' && !quoted) {
            if (any)
                out.push_back(cur);
            cur.clear();
            any = false;
        } else {
            cur += ch;
            any = true;
        }
    }
    if (any)
        out.push_back(cur);
    return !quoted;
}

const char* msg_mode_name(MsgMode m)
{
    switch (m) {
    case MsgMode::Text: return "text";
    case MsgMode::Progress: return "progress";
    case MsgMode::Choose: return "choose";
    case MsgMode::None: break;
    }
    return "none";
}

bool read_calls(const std::string& path, std::vector<MsgEvent>& events)
{
    std::vector<std::string> lines;
    if (!read_lines(path, lines)) {
        fprintf(stderr, "blockie-shot: cannot read %s\n", path.c_str());
        return false;
    }
    for (const std::string& l : lines) {
        std::vector<std::string> w;
        if (!msg_words(l, w)) {
            fprintf(stderr, "blockie-shot: an open quote in %s\n", l.c_str());
            return false;
        }
        if (w.empty())
            continue;
        char* end = nullptr;
        long long at = std::strtoll(w[0].c_str(), &end, 10);
        if (end == w[0].c_str() || *end || at < 0 || w.size() < 2) {
            fprintf(stderr, "blockie-shot: bad call line %s\n", l.c_str());
            return false;
        }
        MsgEvent e;
        e.at = at;
        size_t i = 1;
        for (; i < w.size() && w[i] != "--"; i++)
            e.argv.push_back(w[i]);
        if (i < w.size())
            e.lines.assign(w.begin() + (long)i + 1, w.end());
        const std::string& a0 = e.argv.empty() ? std::string() : e.argv[0];
        if (a0.compare(0, 4, "key:") == 0) {
            std::string k = a0.substr(4);
            size_t colon = k.find(':');
            std::string edge = colon == std::string::npos ? std::string() : k.substr(colon + 1);
            k = k.substr(0, colon);
            e.edge = edge == "d" ? 1 : edge == "u" ? -1 : 0;
            if (!action_of(k, e.key) || (!edge.empty() && e.edge == 0) || e.argv.size() != 1) {
                fprintf(stderr, "blockie-shot: unknown key %s\n", a0.c_str());
                return false;
            }
        }
        events.push_back(e);
    }
    std::stable_sort(events.begin(), events.end(), [](const MsgEvent& a, const MsgEvent& b) { return a.at < b.at; });
    return true;
}

bool load_plate(const std::string& path, Buffer& plate)
{
    DecodedImage d;
    if (!load_png(path, d) || !d.ok() || d.w != kPortraitW || d.h != kPortraitH)
        return false;
    for (int y = 0; y < d.h; y++)
        for (int x = 0; x < d.w; x++)
            plate.surface().at(x, y) = LiveF::pack(d.rgb[(size_t)y * d.w + x]);
    return true;
}

int run_portmsg_shot(Fonts& fonts, const std::string& calls, const std::string& plate_arg, const DriveOpts& o,
                     const std::string& frames_dir, const std::string& out)
{
    std::vector<MsgEvent> events;
    if (!read_calls(calls, events))
        return 2;
    Buffer plate;
    if (plate_arg == "black") {
        Canvas pc(plate.surface());
        pc.fill(kScreen, launch_ground());
    } else if (!load_plate(plate_arg, plate)) {
        fprintf(stderr, "blockie-shot: --plate takes black or a %dx%d png\n", kPortraitW, kPortraitH);
        return 2;
    }
    int64_t clock = 0, progress = 0;
    std::set<int64_t> pending;
    auto say = [&](int64_t id, int code, const std::string& value) {
        fprintf(stderr, "blockie-shot: %lld ms call %lld answer %d%s%s\n", (long long)clock, (long long)id, code,
                value.empty() ? "" : " ", value.c_str());
        pending.erase(id);
    };
    auto fresh = [&]() {
        auto c = std::make_unique<MsgCore>();
        c->answer = say;
        return c;
    };
    std::unique_ptr<MsgCore> core = fresh();
    Repeat rep;
    bool server = false;
    uint64_t opened = 0;
    auto press = [&](PadAction a, int64_t t) {
        core->key(a, t);
        if (is_direction(a) && core->live())
            rep.press(a, kFromPad, t);
    };
    auto run_event = [&](size_t idx) {
        const MsgEvent& e = events[idx];
        int64_t id = (int64_t)idx + 1;
        const std::string a0 = e.argv.empty() ? std::string() : e.argv[0];
        if (a0 == "end") {
            std::vector<int64_t> left(pending.begin(), pending.end());
            for (int64_t p : left)
                say(p, kMsgCancel, std::string());
            core = fresh();
            rep.release_all();
            server = false;
            progress = 0;
            return;
        }
        if (a0.compare(0, 4, "key:") == 0) {
            if (e.edge >= 0)
                press(e.key, e.at);
            if (e.edge <= 0)
                rep.release(e.key, kFromPad, e.at);
            return;
        }
        if (a0 == "line") {
            for (const std::string& l : e.lines)
                core->line(progress, l, e.at);
            return;
        }
        if (a0 == "eof") {
            core->done(progress);
            return;
        }
        MsgCall c;
        std::vector<std::string> opts = e.lines;
        if (!parse_call(e.argv, c) || !msg_options(c, opts)) {
            say(id, kMsgUsage, std::string());
            return;
        }
        if (!server && msg_quiet(c.kind)) {
            say(id, kMsgDone, std::string());
            return;
        }
        server = true;
        pending.insert(id);
        core->call(id, c, opts, e.at);
        if (core->chooses() != opened) {
            opened = core->chooses();
            rep.release_all();
        }
        if (c.kind == CallKind::Progress) {
            progress = id;
            for (const std::string& l : e.lines)
                core->line(id, l, e.at);
        }
    };
    size_t next = 0;
    auto advance = [&](int64_t tf) {
        for (;;) {
            int64_t ev = next < events.size() ? events[next].at : kNever;
            int64_t td = core->deadline() >= 0 ? core->deadline() : kNever;
            int64_t rd = rep.deadline() >= 0 ? rep.deadline() : kNever;
            int64_t t = std::min({ev, td, rd});
            if (t > tf)
                break;
            clock = t;
            if (t == td) {
                core->tick(t);
            } else if (t == rd) {
                PadAction a = PadAction::Up;
                if (rep.due(t, a))
                    core->key(a, t);
                else
                    rep.release_all();
            } else {
                run_event(next++);
            }
        }
        clock = tf;
        core->tick(tf);
        if (server && core->ended(tf) && pending.empty())
            server = false;
    };
    InkLayer ink;
    GlyphCache glyphs;
    Buffer layer, comp;
    std::vector<uint32_t> argb((size_t)kPortraitW * kPortraitH);
    std::vector<AlphaRegion> regions;
    FILE* side = frames_dir.empty() ? nullptr : fopen((frames_dir + "/frames.txt").c_str(), "w");
    if (!frames_dir.empty() && !side) {
        fprintf(stderr, "blockie-shot: cannot write %s/frames.txt\n", frames_dir.c_str());
        return 1;
    }
    int64_t last = events.empty() ? 0 : events.back().at;
    int written = 0, tail = o.tail;
    bool composed = false;
    for (int n = 0;; n++) {
        int64_t tf = o.at >= 0 && frames_dir.empty() ? o.at : frame_ms(n);
        advance(tf);
        regions.clear();
        Canvas c(layer.surface());
        c.fill(kScreen, 0);
        if (server)
            core->view.draw(c, fonts, glyphs, ink, theme(), tf, regions);
        bool refused = regions.size() > (size_t)kWlAlphaMax;
        if (!refused || !composed) {
            if (refused)
                regions.clear();
            write_argb(layer.surface(), regions, argb.data(), kPortraitW);
            composite_argb(argb.data(), kPortraitW, plate.surface(), comp.surface());
            composed = true;
        }
        if (side && tf >= o.from) {
            if (!write_png(frame_path(frames_dir, written), comp.surface())) {
                fprintf(stderr, "blockie-shot: cannot write %s\n", frame_path(frames_dir, written).c_str());
                fclose(side);
                return 1;
            }
            std::string line = std::to_string(written) + " " + std::to_string(tf) + " " +
                               (server ? msg_mode_name(core->view.content().mode) : "gone");
            for (const AlphaRegion& r : regions)
                line += " " + std::to_string(r.r.x0) + "," + std::to_string(r.r.y0) + "," + std::to_string(r.r.x1) +
                        "," + std::to_string(r.r.y1) + ":" + std::to_string(region_a8(r.a256));
            line += " focus=" + std::to_string(server ? core->view.focus() : -1);
            if (refused)
                line += " refused";
            if (fprintf(side, "%s\n", line.c_str()) < 0) {
                fprintf(stderr, "blockie-shot: cannot write %s/frames.txt\n", frames_dir.c_str());
                fclose(side);
                return 1;
            }
            written++;
        }
        if (o.at >= 0 && tf >= o.at)
            break;
        if (tf > kMsgShotCapMs) {
            fprintf(stderr, "blockie-shot: the portmsg frames passed the %lld ms cap\n", (long long)kMsgShotCapMs);
            if (side)
                fclose(side);
            return 1;
        }
        bool idle = !server || (!core->view.animating(tf) && core->deadline() < 0 && !rep.active());
        if (next >= events.size() && tf >= last && idle) {
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
    fprintf(stderr, "blockie-shot: portmsg %s, %d frames\n", calls.c_str(), written);
    return 0;
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
    } else if (po.surface != "power" && po.surface != "pause") {
        fprintf(stderr, "blockie-shot: --powermenu takes power, reboot, confirm or pause\n");
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
        } else if (key == "power") {
            if (down)
                m.power(t);
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
                if (po.surface == "pause")
                    m.open_pause(g, t);
                else
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
            while (m.take_note(ln))
                fprintf(stderr, "blockie-shot: %lld ms note %s\n", (long long)t, ln.c_str());
        }
        if (opened)
            m.tick(tf);
        std::string ln;
        while (m.take_line(ln))
            fprintf(stderr, "blockie-shot: %lld ms line %s\n", (long long)tf, ln.c_str());
        while (m.take_note(ln))
            fprintf(stderr, "blockie-shot: %lld ms note %s\n", (long long)tf, ln.c_str());
    };
    InkLayer ink;
    Buffer layer, comp;
    std::vector<uint32_t> argb((size_t)kPortraitW * kPortraitH);
    std::vector<AlphaRegion> regions;
    int64_t draw_us = 0;
    auto render = [&](int64_t tf) {
        regions.clear();
        Canvas c(layer.surface());
        auto t0 = std::chrono::steady_clock::now();
        if (opened && !m.done())
            draw_powermenu(c, fonts, ink, m.view(tf), theme(), regions);
        draw_us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
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
            bool dlg =
                pv.surface == PmSurface::Power || pv.surface == PmSurface::Confirm || pv.surface == PmSurface::Pause;
            int focus = pv.surface == PmSurface::Sheet ? pv.sheet.focus : dlg ? pv.focus : -1;
            line += " focus=" + std::to_string(focus);
            fprintf(stderr, "blockie-shot: frame %d draw_us %lld\n", written, (long long)draw_us);
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
        bool menu_up =
            ps == PmSurface::Power || ps == PmSurface::Sheet || ps == PmSurface::Confirm || ps == PmSurface::Pause;
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

constexpr int kMenuStripPx = 37;

struct StoreShotOpts {
    bool on = false;
    std::string port, helper, steam;
    bool offline = false;
    bool steampage = false;
    int64_t cap = -1;
    int64_t art_delay = 0;
};

struct HelperLine {
    std::string cmd;
    int64_t ms = 0;
    std::string line;
};

constexpr size_t kHelperLinesMax = 256;

std::string real_path(const std::string& p)
{
    char buf[PATH_MAX];
    return realpath(p.c_str(), buf) ? std::string(buf) : std::string();
}

std::string port_stem(const std::string& name)
{
    size_t n = name.size();
    return n > 4 && name.compare(n - 4, 4, ".zip") == 0 ? name.substr(0, n - 4) : name;
}

std::vector<std::string> store_rows(const HomeFlow& flow, const PortPage& pp, Screen top, int64_t tf, std::string& kind,
                                    bool landed = false)
{
    std::vector<std::string> out;
    auto row = [&out](const std::string& name, Rect r, bool art) {
        out.push_back(name + " " + std::to_string(r.x0) + " " + std::to_string(r.y0) + " " + std::to_string(r.x1) +
                      " " + std::to_string(r.y1) + (art ? " art" : " text"));
    };
    kind = top == Screen::Port         ? "port"
           : top == Screen::SteamLogin ? "steam"
           : flow.nav.store            ? "s-a"
                                       : "home";
    if (top == Screen::SteamLogin) {
        row("steam-box", login_box(), true);
    } else if (top == Screen::Port) {
        row("port-art", Rect{kPortArtX, kPortArtTop, kPortArtX + kStoreShotSide, kPortArtTop + kStoreShotSide}, true);
        int pitch = px(sz().rowHeight);
        for (int k = 0; k < pp.action_count(tf); k++) {
            int y0 = kPortRowsTop + k * pitch;
            row("port-row" + std::to_string(k), Rect{0, y0, kPaneSplit, y0 + pitch}, false);
        }
        std::vector<Rect> lines = pp.line_rects(tf);
        for (size_t k = 0; k < lines.size(); k++)
            row("port-line" + std::to_string(k), lines[k], false);
    } else {
        bool wide = flow.nav.pane == Pane::Details && top == Screen::Home;
        int split = (landed ? (wide ? kPaneSplitWide : kPaneSplit) : flow.boundary(tf)) + kTileFocusBorder;
        bool big = flow.nav.system_view && flow.nav.cols == kBigCols;
        int pitch = big ? kBigPitch : kGridRowPitch, count = big ? kBigRows : kGridRows;
        row("home-head", Rect{0, 0, split, kBandTop}, false);
        for (int k = 0; k < count; k++) {
            int y0 = kBandTop + k * pitch;
            row("home-tile" + std::to_string(k), Rect{0, y0, split, k + 1 < count ? y0 + pitch : kPortraitH}, true);
        }
        if (flow.nav.pane == Pane::Details) {
            row("home-detail", Rect{split, 0, kPortraitW, kPortraitH}, false);
        } else {
            row("home-strip", Rect{split, 0, kPortraitW, kMenuStripPx}, false);
            int shown = std::min(kMenuVisibleRows, (int)flow.model.systems.size() - flow.nav.menu_top);
            for (int k = 0; k < shown; k++) {
                int y0 = kMenuStripPx + k * kMenuPitchPx;
                row("home-menu" + std::to_string(k), Rect{split, y0, kPortraitW, y0 + kMenuPitchPx}, false);
            }
            int wm = kMenuStripPx + kMenuVisibleRows * kMenuPitchPx;
            row("home-watermark", Rect{split, wm, kPortraitW, kPortraitH}, false);
        }
    }
    return out;
}

int run_store_shot(Fonts& fonts, const StoreShotOpts& so, const DriveOpts& o, const std::string& frames_dir,
                   const std::string& out)
{
    std::vector<Step> steps;
    if (!parse_script(o.script, steps)) {
        fprintf(stderr, "blockie-shot: bad --script %s\n", o.script.c_str());
        return 2;
    }
    Library lib;
    if (!ports_library(o.ports_dir, lib))
        return 1;
    std::string ports = o.ports_dir;
    while (ports.size() > 1 && ports.back() == '/')
        ports.pop_back();
    std::string catalog_path = ports + kCatalogDb;
    std::string steam_path = so.steam.empty() ? ports + kSteamTsv : so.steam;
    std::vector<HelperLine> hlines;
    if (!so.helper.empty()) {
        std::vector<std::string> raw;
        if (!read_lines(so.helper, raw))
            return 1;
        if (raw.size() > kHelperLinesMax) {
            fprintf(stderr, "blockie-shot: --helperlines takes at most %zu lines\n", kHelperLinesMax);
            return 2;
        }
        std::string root = real_path(ports);
        for (const std::string& l : raw) {
            size_t a = l.find(' '), b = a == std::string::npos ? a : l.find(' ', a + 1);
            std::string cmd = l.substr(0, a), ms = a == std::string::npos ? std::string() : l.substr(a + 1, b - a - 1);
            std::string rest = b == std::string::npos ? std::string() : l.substr(b + 1);
            bool known = cmd == "refresh" || cmd == "install" || cmd == "update" || cmd == "uninstall" ||
                         cmd == "steam-get" || cmd == "steam-logout";
            bool digits = !ms.empty() && ms.size() <= 7 && ms.find_first_not_of("0123456789") == std::string::npos;
            bool shape = rest == "ignore-term" || rest.compare(0, 9, "progress\t") == 0 ||
                         rest.compare(0, 5, "done\t") == 0 || rest.compare(0, 5, "fail\t") == 0 ||
                         (cmd == "steam-get" && rest.compare(0, 3, "qr\t") == 0) ||
                         (cmd == "refresh" && rest.compare(0, 8, "catalog ") == 0) || rest.compare(0, 6, "steam ") == 0;
            for (const char* key : {"catalog ", "steam "}) {
                size_t n = std::strlen(key);
                if (rest.compare(0, n, key) != 0)
                    continue;
                std::string path = real_path(rest.substr(n));
                shape = shape && !root.empty() && path.compare(0, root.size() + 1, root + "/") == 0;
                rest = key + path;
            }
            if (!known || !digits || !shape || l.size() > kHelperLineCap) {
                fprintf(stderr, "blockie-shot: --helperlines takes CMD MS LINE, refused a %zu byte %s line\n", l.size(),
                        known ? cmd.c_str() : "unknown");
                return 2;
            }
            hlines.push_back({cmd, std::atoll(ms.c_str()), rest});
        }
        std::stable_sort(hlines.begin(), hlines.end(),
                         [](const HelperLine& x, const HelperLine& y) { return x.ms < y.ms; });
    }
    int64_t now = 0;
    bool online = !so.offline;
    GlyphCache glyphs;
    PortPage portp(fonts, glyphs);
    HomeFlow flow(fonts, flow_opts(o));
    Nav& nav = flow.nav;
    Children kids;
    PortsHelper helper;
    StoreFlow sf(flow, portp, helper, kids);
    sf.l2 = so.steampage;
    ToastStrip toast;
    Repeat rep;
    std::unordered_map<int64_t, Art> arts;
    JpegDecoder jpeg;
    auto art_at = [&](int64_t key, const std::string& path, int side) {
        auto f = arts.find(key);
        if (f != arts.end())
            return f->second;
        DecodedImage dec;
        Art img;
        if (!path.empty() && load_cover(jpeg, path, side, dec) && dec.ok())
            img = std::make_shared<const Image>(cover_tile(dec, side));
        arts[key] = img;
        return img;
    };
    auto art_of = [&](const StoreItem& it, bool shot) {
        const std::string& a = shot ? it.shot : it.cover;
        const std::string& b = shot ? it.cover : it.shot;
        return art_at(shot ? it.id + kStoreIdBase : it.id, !a.empty() ? a : b, shot ? kStoreShotSide : kTileSide);
    };
    std::vector<std::pair<int64_t, std::function<void()>>> later;
    int64_t shot_want = -1;
    Art shot_art;
    auto deliver = [&](int64_t id, Art a) {
        if (!a)
            return;
        if (so.art_delay <= 0) {
            flow.art(id, kTileSide, flow.home.epoch, a);
            return;
        }
        later.push_back({now + so.art_delay, [&, id, a]() {
                             bool keep = flow.nav.system_view && flow.home.keep_set(flow.nav.sys_scroll).count(id);
                             for (const HomeGame& g : flow.home.recent)
                                 keep = keep || g.id == id;
                             if (keep)
                                 flow.art(id, kTileSide, flow.home.epoch, a);
                         }});
    };
    std::vector<IndexPort> added;
    std::unordered_set<std::string> gone;
    int64_t fake_id = int64_t(1) << 30;
    auto index = [&]() {
        std::vector<IndexPort> idx;
        for (IndexPort& ip : index_ports(lib))
            if (!gone.count(ip.dir.empty() ? ip.base : ip.dir))
                idx.push_back(ip);
        idx.insert(idx.end(), added.begin(), added.end());
        return idx;
    };
    std::string run_cmd, run_name;
    int64_t run_at = -1;
    size_t hnext = 0;
    bool run_cancel = false, run_killed = false, run_ignore = false, playing = false, returning = false;
    bool kept = false;
    PadAction kept_key = PadAction::Up;
    helper.spawn = [&](const std::vector<std::string>& argv, int* fd_out) {
        *fd_out = -1;
        run_cmd = argv.size() > 1 ? argv[1] : std::string();
        run_name = argv.size() > 2 ? argv[2] : run_cmd;
        run_at = now;
        hnext = 0;
        run_cancel = run_killed = run_ignore = false;
        for (const HelperLine& h : hlines)
            run_ignore = run_ignore || (h.cmd == run_cmd && h.line == "ignore-term");
        fprintf(stderr, "blockie-shot: %lld ms helper %s %s\n", (long long)now, run_cmd.c_str(), run_name.c_str());
        return (pid_t)1;
    };
    helper.signal_child = [&](pid_t, int sig) {
        if (sig == SIGKILL)
            run_killed = true;
        else if (!run_ignore)
            run_cancel = true;
    };
    sf.host.result = [&](const HelperResult& r) {
        if (!r.done)
            return;
        std::string dir = port_stem(r.name);
        if (r.cmd == HelperCmd::Remove) {
            gone.insert(dir);
            added.erase(std::remove_if(added.begin(), added.end(), [&](const IndexPort& ip) { return ip.dir == dir; }),
                        added.end());
            return;
        }
        if (r.cmd != HelperCmd::Install)
            return;
        gone.erase(dir);
        bool listed = false;
        for (const IndexPort& ip : index_ports(lib))
            listed = listed || ip.dir == dir;
        for (const IndexPort& ip : added)
            listed = listed || ip.dir == dir;
        for (const StoreItem& it : sf.items)
            if (!listed && it.name == r.name)
                added.push_back({fake_id++, dir, it.title, dir, it.cover});
    };
    sf.host.now = [&]() { return now; };
    sf.host.wall = [&]() { return 1700000000 + now / 1000; };
    sf.host.is_ports = [](int sys) { return sys == 0; };
    sf.host.online = [&]() { return online; };
    sf.host.stale = [&]() {
        for (const HelperLine& h : hlines)
            if (h.cmd == "refresh")
                return true;
        return false;
    };
    sf.host.index = index;
    sf.host.steam = [&]() { return read_steam(steam_path); };
    sf.host.rescan = [&]() {
        flow.home_model().systems[0].games = (int)index().size();
        if (!nav.store && nav.system_view && nav.focus == 0 && nav.stack.empty())
            flow.regame(0, flow.games(0));
    };
    sf.host.reread = [&]() {
        Catalog c;
        bool ok = read_catalog(catalog_path, ports, c);
        sf.set_catalog(ok, std::move(c), now);
    };
    sf.host.preview_art = [&](const std::vector<HomeGame>&, const std::vector<HomeGame>& want) {
        for (const HomeGame& g : want)
            if (const StoreItem* it = sf.item(g.id))
                deliver(g.id, art_of(*it, false));
    };
    sf.host.shot = [&](const StoreItem* it) {
        Art a = it ? art_of(*it, true) : nullptr;
        if (so.art_delay > 0) {
            int64_t key = it ? it->id + kStoreIdBase : -1;
            auto show = [&](const Art& art) {
                if (nav.top() == Screen::Port || nav.top() == Screen::SteamLogin)
                    portp.set_art(art);
            };
            if (key == shot_want) {
                if (shot_art)
                    show(shot_art);
                return;
            }
            shot_want = key;
            shot_art.reset();
            show(nullptr);
            later.push_back({now + so.art_delay, [&, a, key, show]() {
                                 if (shot_want != key)
                                     return;
                                 shot_art = a;
                                 show(a);
                             }});
            return;
        }
        portp.set_art(a);
    };
    sf.host.toast = [&](const std::string& l1, const std::string& l2) { toast.show(now, l1, l2, true); };
    sf.host.closed = []() {};
    sf.host.redraw = []() {};
    sf.attach();
    {
        Catalog c;
        bool ok = read_catalog(catalog_path, ports, c);
        sf.set_catalog(ok, std::move(c), now);
    }
    std::string entry = kPortsSystem;
    for (const SystemCfg& sc : lib.config())
        if (sc.id == kPortsSystem)
            entry = sc.name;
    flow.model.systems.push_back({entry, (int)index().size(), ""});
    flow.kinds.push_back(MenuKind::System);
    auto cover_of = [&](int64_t id) {
        for (const IndexPort& ip : index())
            if (ip.game_id == id)
                return ip.cover;
        return std::string();
    };
    flow.home.load = [&](const HomeGame& g) -> Art {
        if (so.art_delay > 0)
            return nullptr;
        if (const StoreItem* it = sf.item(g.id))
            return art_of(*it, false);
        return art_at(g.id, cover_of(g.id), kTileSide);
    };
    Buffer frame;
    InkLayer ink;
    flow.last_frame = [&](const Surface& s) {
        copy_surface(frame.surface(), s);
        toast.under(s);
    };
    flow.ctl.can_animate = []() { return true; };
    flow.ctl.clock_us = [&]() { return now * 1000; };
    flow.rearm = [&](int64_t t) { rep.rearm(t); };
    flow.games = [&](int sys) {
        if (nav.store)
            return sf.games(sys);
        std::vector<HomeGame> list;
        if (sys == 0)
            for (const IndexPort& ip : index())
                list.push_back({ip.game_id, ip.title, entry});
        return list;
    };
    flow.covers_wanted = [&](const std::vector<HomeGame>& want, int) {
        for (const HomeGame& g : want) {
            const StoreItem* it = sf.item(g.id);
            deliver(g.id, it ? art_of(*it, false) : art_at(g.id, cover_of(g.id), kTileSide));
        }
    };
    flow.launch_game = [&](const HomeGame& g) {
        if (helper.op != HelperOp::None) {
            LaunchOutcome lo = launch_helper_busy(helper.op);
            toast.show(now, lo.line1, lo.line2, nav.top() == Screen::Home);
            return LaunchStart::Refused;
        }
        fprintf(stderr, "blockie-shot: %lld ms launch %lld\n", (long long)now, (long long)g.id);
        rep.release_all();
        playing = true;
        return LaunchStart::Leave;
    };
    flow.draw_page = [&](Canvas& c, Screen s, int64_t t) {
        if (s == Screen::Port)
            portp.draw(c, ink, t);
        else if (s == Screen::SteamLogin)
            portp.draw_login(c, ink, t);
    };
    flow.page_input = [&](Screen s, PadAction a, bool r) { return sf.page_input(s, a, r, now); };
    flow.page_seed_y = [&](Screen s) { return sf.seed_y(s); };
    flow.setup();
    nav.focus = 0;
    if (!so.port.empty()) {
        const StoreItem* found = nullptr;
        for (const StoreItem& it : sf.items)
            if (it.name == so.port)
                found = &it;
        if (!found) {
            fprintf(stderr, "blockie-shot: --storeport %s is not in the store\n", so.port.c_str());
            return 2;
        }
        StoreItem it = *found;
        flow.input(PadAction::Context, false, now);
        flow.ctl.land_all(now, "scene");
        if (nav.store) {
            flow.input(PadAction::Accept, false, now);
            flow.ctl.land_all(now, "scene");
        }
        flow.open_port(store_game(it));
        flow.ctl.land_all(now, "scene");
    }
    FILE* side = nullptr;
    FILE* rows = nullptr;
    if (!frames_dir.empty()) {
        side = fopen((frames_dir + "/frames.txt").c_str(), "w");
        rows = side ? fopen((frames_dir + "/rows.txt").c_str(), "w") : nullptr;
        if (!side || !rows) {
            fprintf(stderr, "blockie-shot: cannot write in %s\n", frames_dir.c_str());
            if (side)
                fclose(side);
            return 1;
        }
    }
    bool wrote = true;
    int sid = 0;
    std::vector<std::string> logged;
    std::string kind;
    auto put_surface = [&](const std::vector<std::string>& set) {
        ++sid;
        wrote = fprintf(rows, "surface %d page step 0 0 %d %d\n", sid, kPortraitW, kPortraitH) > 0 && wrote;
        for (const std::string& r : set)
            wrote = fprintf(rows, "row %d %s\n", sid, r.c_str()) > 0 && wrote;
        logged = set;
    };
    int64_t fold_t0 = -1;
    const char* fold_kind = "";
    int fold_rev = -1;
    std::vector<std::string> before;
    Screen before_top = Screen::Home;
    auto mark = [&]() {
        before_top = nav.top();
        before = store_rows(flow, portp, before_top, now, kind);
    };
    auto folded = [&]() {
        if (!rows || !flow.ctl.active())
            return;
        const char* fk = fold_kind_name(flow.ctl.kind());
        int rev = flow.ctl.reversed() ? 1 : 0;
        if (flow.ctl.t0() == fold_t0 && fk == fold_kind && rev == fold_rev)
            return;
        fold_t0 = flow.ctl.t0();
        fold_kind = fk;
        fold_rev = rev;
        put_surface(before);
        wrote = fprintf(rows, "fold %lld step %s %d 0 0 0 0 %d from\n", (long long)fold_t0, fk, rev, sid) > 0 && wrote;
        put_surface(store_rows(flow, portp, nav.top(), now, kind, true));
        wrote = fprintf(rows, "fold %lld step %s %d 0 0 0 0 %d to\n", (long long)fold_t0, fk, rev, sid) > 0 && wrote;
        fprintf(stderr, "blockie-shot: %lld ms %s start from %d us to %d us\n", (long long)fold_t0, fk,
                flow.ctl.from_us(), flow.ctl.to_us());
    };
    enum class Gate { Drop, Keep, Act };
    auto input = [&](PadAction a, bool r) {
        if (playing)
            return Gate::Drop;
        if (returning && flow.ctl.active() && flow.ctl.kind() == FoldKind::Return) {
            kept = true;
            kept_key = a;
            return Gate::Keep;
        }
        returning = false;
        mark();
        flow.input(a, r, now);
        folded();
        return Gate::Act;
    };
    auto next_line = [&]() -> int64_t {
        if (!helper.running())
            return kNever;
        if (run_cancel || run_killed)
            return now;
        size_t k = 0;
        bool any = false;
        for (const HelperLine& h : hlines) {
            if (h.cmd != run_cmd)
                continue;
            any = true;
            if (k++ == hnext)
                return run_at + h.ms;
        }
        return any ? kNever : now;
    };
    auto feed_one = [&]() {
        if (run_killed) {
            mark();
            helper.finish();
            folded();
            return;
        }
        if (run_cancel) {
            helper.line("fail\t" + run_name + "\tcancel");
            helper.finish();
            return;
        }
        size_t k = 0;
        for (const HelperLine& h : hlines) {
            if (h.cmd != run_cmd || k++ != hnext)
                continue;
            hnext++;
            if (h.line.compare(0, 8, "catalog ") == 0) {
                catalog_path = h.line.substr(8);
                return;
            }
            if (h.line.compare(0, 6, "steam ") == 0) {
                steam_path = h.line.substr(6);
                return;
            }
            if (h.line == "ignore-term")
                return;
            std::string l = h.line;
            size_t p = l.find("%s");
            if (p != std::string::npos)
                l.replace(p, 2, run_name);
            helper.line(l);
            if (l.compare(0, 5, "done\t") == 0 || l.compare(0, 5, "fail\t") == 0) {
                mark();
                helper.finish();
                folded();
            }
            return;
        }
        helper.line("done\t" + run_name);
        mark();
        helper.finish();
        folded();
    };
    size_t next = 0;
    int written = 0, tail = o.tail, rc = 0;
    int64_t cap = so.cap > 0 ? so.cap : kDriveCapMs;
    std::string keys;
    auto log_key = [&keys](const std::string& k) { keys += (keys.empty() ? "" : ",") + k; };
    for (int n = 0;; n++) {
        int64_t tf = o.at >= 0 && frames_dir.empty() ? o.at : frame_ms(n);
        for (;;) {
            int64_t te = next < steps.size() ? steps[next].ms : kNever;
            int64_t tr = rep.active() ? rep.deadline() : kNever;
            int64_t th = next_line();
            int64_t ts = sf.deadline() >= 0 ? sf.deadline() : kNever;
            int64_t tk = helper.deadline() >= 0 ? helper.deadline() : kNever;
            int64_t ta = kNever;
            for (const auto& l : later)
                ta = std::min(ta, l.first);
            int64_t t = std::min({te, tr, th, ts, tk, ta});
            if (t > tf || t == kNever)
                break;
            now = std::max(now, t);
            if (t == th) {
                feed_one();
            } else if (t == ta) {
                std::vector<std::function<void()>> due;
                for (size_t i = 0; i < later.size();)
                    if (later[i].first <= now) {
                        due.push_back(std::move(later[i].second));
                        later.erase(later.begin() + (long)i);
                    } else {
                        i++;
                    }
                for (auto& fn : due)
                    fn();
            } else if (t == tk) {
                helper.tick(now);
            } else if (t == te) {
                const Step& st = steps[next++];
                PadAction a = PadAction::Up;
                log_key(st.key + (st.down ? ".d" : ".u"));
                if (st.key == "ret") {
                    if (st.down && playing) {
                        playing = false;
                        returning = true;
                        mark();
                        flow.come_back(now);
                        folded();
                    }
                } else if (st.key == "net") {
                    online = !st.down;
                    mark();
                    sf.online_changed(now);
                    folded();
                } else if (action_of(st.key, a)) {
                    Gate g = st.down ? input(a, false) : Gate::Drop;
                    if (is_direction(a)) {
                        if (st.down && g != Gate::Drop)
                            rep.press(a, kFromKeys, now);
                        else if (!st.down)
                            rep.release(a, kFromKeys, now);
                    }
                } else {
                    fprintf(stderr, "blockie-shot: --store ignores script key %s\n", st.key.c_str());
                }
            } else if (t == tr) {
                PadAction a = PadAction::Up;
                if (returning || kept) {
                    rep.rearm(now);
                } else if (rep.due(now, a)) {
                    log_key(std::string(key_name(a)) + ".r");
                    input(a, true);
                }
            } else {
                mark();
                sf.tick(now);
                folded();
            }
        }
        now = std::max(now, tf);
        helper.tick(now);
        mark();
        sf.tick(now);
        folded();
        toast.under(frame.surface());
        flow.frame(tf, frame.surface());
        if (returning && !(flow.ctl.active() && flow.ctl.kind() == FoldKind::Return)) {
            returning = false;
            if (kept) {
                kept = false;
                input(kept_key, false);
                rep.rearm(now);
            }
        }
        toast.draw(fonts, ink, frame.surface(), tf);
        if (side && tf >= o.from) {
            if (!write_png(frame_path(frames_dir, written), frame.surface())) {
                fprintf(stderr, "blockie-shot: cannot write %s\n", frame_path(frames_dir, written).c_str());
                rc = 1;
                break;
            }
            Screen top = nav.top();
            std::vector<std::string> set = store_rows(flow, portp, top, tf, kind);
            if (set != logged)
                put_surface(set);
            bool moving = flow.animating(tf) || (top == Screen::Port && portp.animating(tf)) ||
                          (top == Screen::SteamLogin && portp.login_animating(tf)) || toast.animating(tf);
            std::string focus = top == Screen::Port ? std::to_string(portp.focus())
                                : nav.system_view   ? std::to_string(nav.sys_focus)
                                                    : std::to_string(nav.focus);
            bool toasting = toast.visible(tf) || (top == Screen::Port && portp.toasting(tf));
            bool dialog = (top == Screen::Port && portp.dialog_shown()) ||
                          (top == Screen::SteamLogin && portp.login_dialog_shown());
            wrote = fprintf(side, "%d %lld %d %d 0 1 0 0 0 0 kind=%s rows=%d focus=%s pivot=- working=%s dialog=%d "
                                  "toast=%d online=%d qr=%d%s%s\n",
                            written, (long long)tf, flow.boundary(tf), moving ? 1 : 0, kind.c_str(), sid,
                            focus.c_str(), helper.running() ? run_cmd.c_str() : "none", dialog ? 1 : 0,
                            toasting ? 1 : 0, online ? 1 : 0,
                            (top == Screen::Port || top == Screen::SteamLogin) && portp.qr_shown() ? 1 : 0,
                            keys.empty() ? "" : " keys=", keys.c_str()) > 0 &&
                    wrote;
            written++;
        }
        keys.clear();
        if (o.at >= 0 && tf >= o.at)
            break;
        if (tf > cap) {
            fprintf(stderr, "blockie-shot: the store frames passed the %lld ms cap\n", (long long)cap);
            rc = 1;
            break;
        }
        bool login_top = nav.top() == Screen::SteamLogin;
        bool page_moving = (nav.top() == Screen::Port && (portp.animating(tf) || portp.deadline(tf) >= 0)) ||
                           (login_top && (portp.login_animating(tf) || portp.deadline(tf, false) >= 0));
        bool pending = helper.running() || sf.deadline() >= 0 || toast.animating(tf) || toast.visible(tf) ||
                       !later.empty();
        if (next >= steps.size() && !rep.active() && !pending && !flow.animating(tf) && !page_moving && tail-- <= 0)
            break;
    }
    bool ok = wrote && rc == 0;
    if (side && fclose(side) != 0)
        ok = false;
    if (rows && fclose(rows) != 0)
        ok = false;
    if (!out.empty() && !write_png(out, frame.surface()))
        ok = false;
    if (!ok)
        fprintf(stderr, "blockie-shot: store frames incomplete in %s\n", frames_dir.c_str());
    fprintf(stderr, "blockie-shot: store s-a, %d frames\n", written);
    return ok ? 0 : (rc ? rc : 1);
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
    std::string word_out;
    int tile_focus = -1;
    int sys_focus = 0;
    int systems = 6;
    int menu_top = 0;
    bool items = false;
    bool light = false;
    std::string msg_calls;
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
    StoreShotOpts store_shot;
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
        else if (a == "--word" && i + 1 < argc)
            word_out = argv[++i];
        else if (a == "--hp")
            drive.hp = true;
        else if (a == "--word-dir" && i + 1 < argc)
            drive.word_dir = argv[++i];
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
        else if (a == "--recentin" && i + 1 < argc)
            drive.recent_in = atoi(argv[++i]);
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
        else if (a == "--engine" && i + 1 < argc) {
            std::string e = argv[++i];
            if (e != "fast" && e != "pixel") {
                fprintf(stderr, "blockie-shot: --engine takes fast or pixel\n");
                return 2;
            }
            drive.fast = e == "fast";
        }
        else if (a == "--blocks" && i + 1 < argc)
            drive.blocks = argv[++i];
        else if (a == "--pane-blocks" && i + 1 < argc)
            drive.pane_blocks = argv[++i];
        else if (a == "--pane-dump" && i + 1 < argc)
            drive.pane_dump = argv[++i];
        else if (a == "--dump" && i + 1 < argc)
            drive.dump = argv[++i];
        else if (a == "--late" && i + 1 < argc) {
            std::string v = argv[++i];
            if (v != "recent" && v != "sys") {
                fprintf(stderr, "blockie-shot: --late takes recent or sys\n");
                return 2;
            }
            drive.late_sys = v == "sys";
        }
        else if (a == "--artless" && i + 1 < argc) {
            for (const std::string& v : comma_list(argv[++i]))
                drive.artless.push_back(atoi(v.c_str()));
        }
        else if (a == "--artless-sys" && i + 1 < argc) {
            for (const std::string& v : comma_list(argv[++i]))
                drive.artless_sys.push_back(atoi(v.c_str()));
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
        else if (a == "--input-lead" && i + 1 < argc)
            drive.input_lead = std::max<int64_t>(0, atoll(argv[++i]));
        else if (a == "--startup-at" && i + 1 < argc) {
            drive.startup = true;
            drive.startup_at = std::max<int64_t>(0, atoll(argv[++i]));
        }
        else if (a == "--exit" && i + 1 < argc) {
            drive.exit_class = argv[++i];
            int status = 0;
            if (drive.exit_class != "spawn" && !exit_status(drive.exit_class, status)) {
                fprintf(stderr, "blockie-shot: --exit takes quit, crash, oom, busy, config, exec, core, audio"
                                " or spawn\n");
                return 2;
            }
        }
        else if (a == "--quitstamp")
            drive.quit_stamp = true;
        else if (a == "--port")
            drive.port = true;
        else if (a == "--portsdir" && i + 1 < argc)
            drive.ports_dir = argv[++i];
        else if (a == "--portmsg" && i + 1 < argc)
            msg_calls = argv[++i];
        else if (a == "--theme" && i + 1 < argc)
            light = std::string(argv[++i]) == "light";
        else if (a == "--helper" && i + 1 < argc) {
            std::string h = argv[++i];
            drive.helper = h == "install" ? HelperOp::Install
                           : h == "update" ? HelperOp::Update
                           : h == "remove" ? HelperOp::Remove
                           : h == "steam-get" ? HelperOp::SteamGet
                                              : HelperOp::None;
            if (drive.helper == HelperOp::None) {
                fprintf(stderr, "blockie-shot: --helper takes install, update, remove or steam-get\n");
                return 2;
            }
        }
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
        else if (a == "--store")
            store_shot.on = true;
        else if (a == "--cap" && i + 1 < argc)
            store_shot.cap = atoll(argv[++i]);
        else if (a == "--art-delay" && i + 1 < argc)
            store_shot.art_delay = std::max<int64_t>(0, atoll(argv[++i]));
        else if (a == "--storeport" && i + 1 < argc)
            store_shot.port = argv[++i];
        else if (a == "--helperlines" && i + 1 < argc)
            store_shot.helper = argv[++i];
        else if (a == "--steam" && i + 1 < argc)
            store_shot.steam = page_opts.steam = argv[++i];
        else if (a == "--offline")
            store_shot.offline = true;
        else if (a == "--steampage")
            store_shot.steampage = true;
        else if (out.empty())
            out = a;
        else {
            fprintf(stderr, "blockie-shot: unexpected argument %s\n", a.c_str());
            return 2;
        }
    }
    if (!pops_dir.empty())
        return run_pops(pops_dir, pop_lead, pop_summary, pop_case.empty() ? pops_dir : pop_case);
    if (!drive.dump.empty() && drive.blocks.empty()) {
        fprintf(stderr, "blockie-shot: --dump needs --blocks FILE\n");
        return 2;
    }
    if (drive.on && menu_top_set) {
        fprintf(stderr, "blockie-shot: --menutop is not a --drive option, the drive keeps the focus in view\n");
        return 2;
    }
    bool sequence = !page_flip.empty();
    bool driven = drive.on && (!out.empty() || !frames_dir.empty());
    bool page_frames = !drive.on && !sequence && page == "settings" && !frames_dir.empty();
    bool pm_shot = !drive.on && !pm.surface.empty() && (!out.empty() || !frames_dir.empty());
    bool msg_shot = !msg_calls.empty() && (!out.empty() || !frames_dir.empty());
    if (!driven && !page_frames && !pm_shot && !msg_shot &&
        ((out.empty() && !sequence) || (sequence && frames_dir.empty()))) {
        fprintf(stderr,
                "usage: blockie-shot OUT.png [--focus selector|details] [--fonts DIR] [--tiles DIR] [--header TEXT]"
                " [--tilefocus N] [--sysfocus N] [--systems N] [--menutop N] [--items] [--btsub TEXT]"
                " [--light] [--page settings [--word FILE]] [--pivot WORD] [--pagefocus N]"
                " [--sheet [tz|tz:REGION]] [--sheetfocus N]"
                " [--sheetrows 6|7] [--zone ID] [--zone-missing] [--zonetab PATH] [--set ID=VALUE] [--hide ID]"
                " [--battery CAP:STATUS|unknown] [--volume PCT[:muted]|fail] [--radio on|off|gone]"
                " [--storage FREE:TOTAL:SYSFREE:SYSTOTAL|nocard:SYSFREE:SYSTOTAL] [--about OS_RELEASE|FRONTEND|KERNEL]"
                " [--toast LINE1|LINE2] [--steam STEAM.TSV] [--scatter]"
                " [--gamedetail SYSTEM|TITLE] [--bt unavailable|off|empty|list|searching|sheet|confirm [--hp]]\n"
                "       blockie-shot --pageflip push|pop --frames DIR [same options]\n"
                "       blockie-shot --page settings --frames DIR [--script KEY|fail|missing:d|u:MS,...] [--from MS]"
                " [--tail N]"
                " [same options]\n"
                "       blockie-shot --drive [OUT.png] [--frames DIR] [--script KEY|ret|cover|theme:d|u:MS|PCT%%,...]"
                " [--at MS] [--from MS]"
                " [--recentn N] [--recentin K] [--sysgames N] [--noart] [--title0 TEXT] [--titles FILE]"
                " [--recenttitles FILE] [--recentsys NAME[,NAME...]] [--grid g1|g2] [--slots a|c]"
                " [--engine fast|pixel] [--hp] [--word-dir DIR]"
                " [--blocks FILE [--dump DIR]]"
                " [--pane-blocks FILE [--pane-dump DIR]] [--t9clip pane|edge]"
                " [--bar content|slot]"
                " [--gridscroll band|full] [--startup] [--arrive MS] [--late recent|sys] [--artless N[,N...]]"
                " [--artless-sys N[,N...]]"
                " [--tail N] [--cadence MS] [--repeat-covers] [--fallback FONT] [--startup-at MS] [--input-lead MS]"
                " [--exit quit|crash|oom|busy|config|exec|core|audio|spawn] [--quitstamp] [--port]"
                " [--helper install|update|remove|steam-get] [--portsdir DATA/roms/ports] [--libretro DIR]"
                " [same options]\n"
                "       blockie-shot --portmsg CALLS [OUT.png] [--frames DIR] [--plate black|FILE.png] [--at MS]"
                " [--tail N] [--theme light|dark]\n"
                "         CALLS lines: AT ARGV... [-- LINE...]; ARGV is blockie-portmsg's argv, the words after --"
                " its stdin lines;\n"
                "         AT line -- LINE... feeds the open --progress, AT eof ends its stdin, AT key:K[:d|:u]"
                " with K up down left right accept back context prev next start, AT end stops the scope\n"
                "       blockie-shot --drive --store [OUT.png] [--frames DIR] --portsdir DATA/roms/ports"
                " [--storeport NAME] [--helperlines FILE] [--steam STEAM.TSV] [--steampage] [--offline]"
                " [--script KEY:d|u:MS,...] [--at MS] [--from MS] [--tail N] [--light] [--cap MS] [--art-delay MS]\n"
                "       blockie-shot --pops FRAMES_DIR [--no-lead] [--summary FILE] [--case NAME]\n"
                "       blockie-shot --powermenu power|reboot|confirm|pause [OUT.png] [--frames DIR] [--gates FILE]"
                " [--pickgates FILE] [--plate home|plain|FILE.png] [--idle MS]"
                " [--script KEY|fail|term|power:d|u:MS,...] [--arrive MS]"
                " [--at MS]"
                " [--from MS] [--tail N] [same options]\n");
        return 2;
    }
    bool still = page == "settings" && !drive.on && !sequence && !page_frames && !pm_shot && bt_state.empty();
    if (!word_out.empty() && !still) {
        fprintf(stderr, "blockie-shot: --word needs a settings still, OUT.png --page settings\n");
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
        return shot_bt(out, bt_state, fonts, drive.hp);
    if (!page_opts.zonetab.empty() && access(page_opts.zonetab.c_str(), R_OK) != 0)
        fprintf(stderr, "blockie-shot: cannot read %s, the sheet lists only utc\n", page_opts.zonetab.c_str());
    Zones zones = page_opts.zonetab.empty() ? Zones() : load_zones(page_opts.zonetab);
    if (!bt_sub.empty())
        page_opts.btsub = bt_sub;
    drive.page = page_opts;
    if (msg_shot && drive.on) {
        fprintf(stderr, "blockie-shot: --portmsg and --drive do not mix\n");
        return 2;
    }
    if (store_shot.on && !drive.on) {
        fprintf(stderr, "blockie-shot: --store needs --drive\n");
        return 2;
    }
    if (drive.on && store_shot.on)
        return run_store_shot(fonts, store_shot, drive, frames_dir, out);
    if (drive.on)
        return run_drive(fonts, drive, out, frames_dir, systems, items, sys_focus, tiles_dir, recent_dir, zones);

    SelectorModel model;
    systems = std::max(1, std::min(systems, 10));
    for (int i = 0; i < systems; i++)
        model.systems.push_back({kNames[i], kCounts[i], ""});
    if (items) {
        std::vector<MenuKind> kinds;
        add_menu_items(model, kinds);
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

    if (msg_shot)
        return run_portmsg_shot(fonts, msg_calls, pm.plate == "home" ? "black" : pm.plate, drive, frames_dir, out);
    if (pm_shot) {
        Buffer plate;
        Canvas pc(plate.surface());
        if (pm.plate == "plain") {
            pc.fill(kScreen, kPlainPlate);
        } else if (pm.plate == "home") {
            draw_selector(pc, fonts, model, focus, scatter, detail);
        } else if (!load_plate(pm.plate, plate)) {
            fprintf(stderr, "blockie-shot: --plate takes home, plain or a %dx%d png\n", kPortraitW, kPortraitH);
            return 2;
        }
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
        Surface shown = push ? menub.surface() : pageb.surface();
        Surface dest = push ? pageb.surface() : menub.surface();
        eng::Instance page_eng(kScreen);
        MotionCtl ctl;
        ctl.render = [&](const Surface& s, Rect) { copy_surface(dest, s); };
        ctl.last = [&](const Surface& s) { copy_surface(shown, s); };
        ctl.render_kept = ctl.last;
        auto run = std::make_shared<PageRun>();
        run->form.closing = !push;
        run->form.under = page_inks(page_art(kPaneSplit, true));
        run->form.over = page_inks(Rect{});
        run->form.under_els = home_elements(kPaneSplit, true, model.focus - menu_top);
        run->form.over_els = page_elements("settings", "chrome", settings_page_rows(sv));
        run->in = &page_eng;
        FoldPlay play = page_fold(run);
        int64_t dur = play.duration;
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

    if (!word_out.empty() && (sv.sheet.open || sv.rowsheet.open || sv.toast.e > 0)) {
        fprintf(stderr, "blockie-shot: --word needs a rest, with no sheet or toast open\n");
        return 2;
    }
    auto settings_still = [&](const Surface& s, const SettingsView& v) {
        Canvas pc(s);
        draw_settings(pc, fonts, ink, v, theme());
    };
    Buffer buf;
    if (page == "settings") {
        settings_still(buf.surface(), sv);
    } else {
        Canvas c(buf.surface());
        draw_selector(c, fonts, model, focus, scatter, detail);
    }

    if (!write_png(out, buf.surface())) {
        fprintf(stderr, "blockie-shot: cannot write %s\n", out.c_str());
        return 1;
    }
    if (word_out.empty())
        return 0;
    bool ok = word_check(fonts, settings_word(), buf.surface());
    SettingsView wv = sv;
    wv.pivots.clear();
    Buffer wb;
    settings_still(wb.surface(), wv);
    ok = clip_check(fonts, wb.surface()) && ok;
    Surface r = buf.surface(), w = wb.surface();
    int s0 = px(tok::pivotTop), s1 = px(tok::pivotBottom), bad = -1;
    for (int y = 0; y < r.h && bad < 0; y++)
        if ((y < s0 || y >= s1) && !std::equal(r.row(y), r.row(y) + r.w, w.row(y)))
            bad = y;
    if (!write_png(word_out, w)) {
        fprintf(stderr, "blockie-shot: cannot write %s\n", word_out.c_str());
        return 1;
    }
    if (bad < 0)
        fprintf(stderr, "blockie-shot: settings word layer %s written, equal to the rest outside rows %d to %d\n",
                word_out.c_str(), s0, s1);
    else
        fprintf(stderr, "blockie-shot: settings word layer %s FAILED, row %d differs from the rest outside the "
                        "pivot strip\n",
                word_out.c_str(), bad);
    return ok && bad < 0 ? 0 : 1;
}
