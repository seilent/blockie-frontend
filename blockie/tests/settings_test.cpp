#include <algorithm>
#include <cstdio>
#include <iterator>
#include <string>
#include <vector>

#include "../../ui/roles.h"
#include "../../ui/rows.h"
#include "../bt_model.h"
#include "../leftmove.h"
#include "../screen_settings.h"
#include "../setd_key.h"
#include "../theme.h"
#include "../zones.h"

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

const char* const kFixture =
    "# tzdb timezone descriptions\n"
    "#\n"
    "#country-code\tcoordinates\tTZ\tcomments\n"
    "AR\t-3436-05827\tAmerica/Argentina/Buenos_Aires\tBuenos Aires (BA, CF)\n"
    "HT\t+1832-07220\tAmerica/Port-au-Prince\n"
    "TT\t+1039-06131\tAmerica/Port_of_Spain\n"
    "US\t+470659-1011757\tAmerica/North_Dakota/New_Salem\tCentral - ND (Morton rural)\n"
    "AQ\t-6640+14001\tAntarctica/DumontDUrville\tDumont-d'Urville\n"
    "\n"
    "SJ\t+7800+01600\tArctic/Longyearbyen\n"
    "JP\t+353916+1394441\tAsia/Tokyo\n"
    "GB\t+513030-0000731\tEurope/London\n"
    "FR\t+4852+00220\tEurope/Paris\n"
    "JP\t+353916+1394441\tAsia/Tokyo\n";

constexpr int kAmerica = 0, kArctic = 2, kAsia = 3, kEurope = 4, kUtc = 5;

SettingsRowSpec spec(const char* pivot, const char* title, RowKind kind, const char* id,
                     std::vector<const char*> words = {}, bool bounded = false)
{
    SettingsRowSpec r;
    r.pivot = pivot;
    r.title = title;
    r.kind = kind;
    r.id = id;
    r.words = words;
    r.values = words;
    r.bounded = bounded;
    return r;
}

std::vector<SettingsRowSpec> kinds_table()
{
    std::vector<SettingsRowSpec> t = settings_rows();
    t.push_back(spec("display", "level", RowKind::Live, "test.live", {"100", "50", "10"}, true));
    t.push_back(spec("sound", "first", RowKind::Sheet, "test.first", {"10", "5", "2"}));
    t.push_back(spec("sound", "step", RowKind::Sheet, "test.sheet", {"10", "5", "2"}));
    t.push_back(spec("controls", "reset", RowKind::Action, "test.action"));
    t.push_back(spec("network", "link", RowKind::Push, "test.push"));
    t.push_back(spec("power", "charge", RowKind::Info, "test.info"));
    t.push_back(spec("system", "versions", RowKind::RowSheet, "test.rowsheet"));
    return t;
}

SettingsRowState row_state(const std::string& value, const std::string& text = "")
{
    SettingsRowState st;
    st.shown = true;
    st.value = value;
    st.text = text;
    return st;
}

struct Kinds {
    SettingsPage p;
    int64_t t = 1000000;
    std::vector<std::string> log;

    Kinds()
    {
        p.on_pick = [this](const std::string& r, const std::string& v) { log.push_back("pick " + r + " " + v); };
        p.on_preview = [this](const std::string& r, const std::string& v) { log.push_back("preview " + r + " " + v); };
        p.on_restore = [this](const std::string& r) { log.push_back("restore " + r); };
        p.on_open = [this](const std::string& r) { log.push_back("open " + r); };
        p.on_close = [this](const std::string& r) { log.push_back("close " + r); };
        p.on_push = [this](const std::string& r) { log.push_back("push " + r); };
        p.on_action = [this](const std::string& r) { log.push_back("action " + r); };
        p.on_theme = [this](bool light) { log.push_back(light ? "theme light" : "theme dark"); };
        p.set_sheet_rows(3);
        p.set_table(kinds_table());
        p.reset(false);
    }

    void act(PadAction a, bool rep = false)
    {
        p.action(a, rep, t);
        t += 1000;
    }

    SettingsView v() const { return p.view(t); }
    std::string last() const { return log.empty() ? std::string() : log.back(); }
    std::string pivot() const { return p.pivot(); }

    void go(const char* word)
    {
        for (int i = 0; i < 8 && pivot() != word; i++)
            act(PadAction::Prev);
        for (int i = 0; i < 8 && pivot() != word; i++)
            act(PadAction::Next);
    }
};

struct Rig {
    SettingsPage p;
    int64_t t = 1000000;
    std::vector<std::string> zones;
    std::vector<bool> themes;

    Rig()
    {
        p.on_zone = [this](const std::string& id) { zones.push_back(id); };
        p.on_theme = [this](bool light) { themes.push_back(light); };
        p.set_sheet_rows(3);
        p.set_zones(parse_zones(kFixture));
        p.reset(false);
    }

    bool act(PadAction a, bool rep = false)
    {
        bool r = p.action(a, rep, t);
        t += 1000;
        return r;
    }

    SettingsView v() const { return p.view(t); }
    SheetView s() const { return p.view(t).sheet; }
    bool open() const { return p.view(t).sheet.progress > 0; }
    std::string item(int k) const { return s().items[k]; }

    void zone_sheet()
    {
        p.reset(false);
        act(PadAction::Next);
        act(PadAction::Accept);
    }
};

}

int main(int argc, char** argv)
{
    {
        Rig r;
        check(!r.v().zone_row && r.v().pivots.size() == 1 && std::string(r.v().pivots[0]) == "display" &&
                  r.v().rows.size() == 1 && r.v().rows[0].title == "theme" && r.v().rows[0].value == "dark",
              "one pivot with the theme row until the zone has a value");
        r.act(PadAction::Down);
        check(r.v().focus == 0, "Down on one row stays");
        r.act(PadAction::Next);
        check(r.v().pivot == 0 && r.v().from_pivot < 0, "Next with one pivot shown stays");
        r.p.set_zone("Asia/Tokyo");
        check(r.v().zone_row && r.v().pivots.size() == 2 && std::string(r.v().pivots[1]) == "system" &&
                  r.v().pivot == 0,
              "the system pivot appears once the zone has a value, display stays active");
        r.act(PadAction::Next);
        check(r.v().pivot == 1 && r.v().rows.size() == 1 && r.v().rows[0].title == "time zone" &&
                  r.v().rows[0].value == "tokyo" && std::string(r.p.pivot()) == "system",
              "Next moves to system, the time zone row shows its words");
        r.act(PadAction::Next);
        r.act(PadAction::Down);
        r.act(PadAction::Down, true);
        check(r.v().pivot == 1 && r.v().focus == 0, "Next at the last pivot stays, Down on its one row stays");
        r.act(PadAction::Prev);
        r.act(PadAction::Prev);
        check(r.v().pivot == 0, "Prev at the first pivot stays");
        r.act(PadAction::Next);
        r.p.set_zone_missing(r.t);
        check(!r.v().zone_row && r.v().pivots.size() == 1 && r.v().pivot == 0 && r.v().focus == 0 &&
                  r.v().bar == fx_int(0) && r.v().from_pivot < 0,
              "the zone going takes its pivot, the page is on display at once with no switch");
    }

    for (int level : {1, 2}) {
        Rig r;
        r.p.set_zone("Asia/Tokyo");
        r.zone_sheet();
        if (level == 2)
            r.act(PadAction::Accept);
        int64_t t0 = r.t;
        r.p.set_zone_missing(t0);
        SettingsView v = r.p.view(t0);
        bool ok = !v.sheet.open && v.sheet.progress == kOne && r.p.view(t0 + 190).sheet.progress > 0 &&
                  r.p.view(t0 + 190).sheet.progress < kOne && r.p.view(t0 + 380).sheet.progress == 0 && !v.zone_row &&
                  v.focus == 0 && v.bar == fx_int(0) && r.zones.empty();
        check(ok, level == 1 ? "zone missing at level 1: T12 close, focus and bar on row 0 at once, nothing applied"
                             : "zone missing at level 2: T12 close, focus and bar on row 0 at once, nothing applied");
    }

    {
        Rig r;
        r.p.set_zone("Asia/Tokyo");
        r.p.reset(false);
        r.p.action(PadAction::Next, false, r.t);
        int64_t t0 = r.t + 1000;
        r.p.action(PadAction::Accept, false, t0);
        r.p.set_zone_missing(t0 + 100);
        check(r.p.view(t0 + 100).sheet.progress == kOne && r.p.view(t0 + 290).sheet.progress > 0 &&
                  r.p.view(t0 + 290).sheet.progress < kOne && r.p.view(t0 + 480).sheet.progress == 0,
              "zone missing during a rise: the rise lands, then the close runs");
        r.p.set_zone("Asia/Tokyo");
        int64_t t1 = t0 + 2000;
        r.p.action(PadAction::Next, false, t1);
        r.p.set_zone_missing(t1 + 50);
        check(r.p.view(t1 + 50).pivot == 0 && r.p.view(t1 + 50).from_pivot < 0 && r.p.view(t1 + 50).focus == 0 &&
                  r.p.view(t1 + 50).bar == fx_int(0),
              "zone missing during a switch to its pivot: the page lands on display at once");
    }

    {
        Rig r;
        r.p.set_zone("Asia/Tokyo");
        r.act(PadAction::Accept);
        int64_t t0 = r.t;
        r.p.set_zone_missing(t0);
        check(r.p.view(t0).sheet.open && r.p.view(t0).sheet.level == 0 && r.p.view(t0 + 1000).sheet.progress == kOne &&
                  !r.p.view(t0).zone_row,
              "zone missing with the theme sheet open: the sheet stays");
    }

    {
        Rig r;
        r.act(PadAction::Accept);
        check(r.open() && std::string(r.s().title) == "theme" && r.s().count == 2 && r.s().focus == 0 &&
                  r.s().mark == 0,
              "theme sheet opens on the live theme, marked");
        r.act(PadAction::Down);
        check(r.s().focus == 1, "theme sheet Down");
        r.act(PadAction::Down, true);
        check(r.s().focus == 1, "theme sheet held Down stops at the end (A17)");
        r.act(PadAction::Down);
        check(r.s().focus == 0, "theme sheet fresh Down wraps");
        r.act(PadAction::Up);
        check(r.s().focus == 1, "theme sheet fresh Up wraps");
        r.act(PadAction::Accept);
        check(!r.open() && r.themes.size() == 1 && r.themes[0] && r.v().light, "Accept on light applies it and closes");
        r.act(PadAction::Accept);
        check(r.s().focus == 1 && r.s().mark == 1, "the mark follows the live theme");
        r.act(PadAction::Back);
        check(!r.open() && r.themes.size() == 1, "Back closes the theme sheet with nothing applied");
    }

    {
        Rig r;
        r.p.set_zone("Asia/Tokyo");
        r.zone_sheet();
        check(r.open() && std::string(r.s().title) == "time zone" && r.s().count == 6 && r.item(0) == "america" &&
                  r.item(kUtc) == "utc",
              "level 1 lists the regions, then utc");
        check(r.s().focus == kAsia && r.s().mark == kAsia, "level 1 opens on the live region, marked");
        check(r.s().first == 1, "a focus past the shown rows sits on the bottom row at open");
        r.act(PadAction::Down);
        check(r.s().focus == kEurope && r.s().first == 2, "the window moves by the smallest step");
        r.act(PadAction::Down);
        r.act(PadAction::Down, true);
        check(r.s().focus == kUtc && r.s().first == 3, "held Down stops at the end");
        r.act(PadAction::Down);
        check(r.s().focus == 0 && r.s().first == 0, "fresh Down past the end wraps");
        r.act(PadAction::Up);
        check(r.s().focus == kUtc && r.s().first == 3, "fresh Up past the top wraps");
        r.act(PadAction::Up);
        check(r.s().focus == kEurope && r.s().first == 3, "a move inside the window keeps it");
        r.act(PadAction::Accept);
        check(r.s().count == 2 && r.item(0) == "london" && r.item(1) == "paris" && r.s().focus == 0 &&
                  r.s().mark == -1,
              "push europe from Asia/Tokyo: row 0, no mark");
        r.act(PadAction::Back);
        check(r.open() && r.s().count == 6 && r.s().focus == kEurope && r.s().first == 3,
              "Back pops to level 1 with its focus and window");
        r.act(PadAction::Up);
        r.act(PadAction::Accept);
        check(r.s().count == 1 && r.item(0) == "tokyo" && r.s().focus == 0 && r.s().mark == 0,
              "push asia: focus and mark on the live zone");
        r.act(PadAction::Accept);
        check(!r.open() && r.zones.empty(), "Accept on the live zone closes with no call");
        r.zone_sheet();
        r.act(PadAction::Back);
        check(!r.open() && r.zones.empty(), "Back on level 1 closes with nothing applied");
    }

    {
        Rig r;
        r.p.set_zone("Asia/Tokyo");
        r.zone_sheet();
        r.act(PadAction::Down);
        r.act(PadAction::Accept);
        r.act(PadAction::Down);
        int64_t at = r.t;
        r.act(PadAction::Accept);
        check(r.zones.size() == 1 && r.zones[0] == "Europe/Paris", "Accept on a zone calls on_zone once");
        check(r.v().zone == "paris" && r.p.view(at).sheet.progress == kOne && r.p.view(at + 380).sheet.progress == 0,
              "the row value swaps at Accept and the sheet closes over 380 ms");
        r.zone_sheet();
        check(r.s().focus == kEurope, "level 1 opens on the picked region");
        r.act(PadAction::Down);
        r.act(PadAction::Accept);
        check(r.zones.size() == 2 && r.zones[1] == "UTC" && r.v().zone == "utc", "utc applies UTC");
    }

    {
        Rig r;
        r.p.set_zone("Etc/UTC");
        check(r.v().zone == "utc", "Etc/UTC reads utc on the row");
        r.zone_sheet();
        check(r.s().focus == kUtc && r.s().mark == kUtc && r.s().first == 3, "Etc/UTC opens on utc, marked");
        r.act(PadAction::Accept);
        check(!r.open() && r.zones.empty(), "Accept on utc with Etc/UTC live makes no call");
        r.p.set_zone("America/Argentina/ComodRivadavia");
        check(r.v().zone == "america / argentina / comodrivadavia", "an unlisted zone keeps its first part on the row");
        r.zone_sheet();
        check(r.s().focus == kAmerica && r.s().mark == -1, "an unlisted zone opens on row 0 with no mark");
        r.act(PadAction::Accept);
        check(r.s().count == 4 && r.s().focus == 0 && r.s().mark == -1, "push america: row 0, no mark");
        r.act(PadAction::Down);
        r.act(PadAction::Down, true);
        r.act(PadAction::Down, true);
        check(r.s().focus == 3 && r.s().first == 1, "level 2 held Down reaches the end");
        r.act(PadAction::Down, true);
        check(r.s().focus == 3, "level 2 held Down stops at the end");
        r.act(PadAction::Down);
        check(r.s().focus == 0 && r.s().first == 0, "level 2 fresh Down wraps");
    }

    {
        Rig r;
        r.p.set_zone("Asia/Tokyo");
        r.zone_sheet();
        r.act(PadAction::Down);
        int focus = r.s().focus, first = r.s().first;
        r.p.set_zone("Europe/London");
        check(r.s().mark == kEurope && r.s().focus == focus && r.s().first == first && r.v().zone == "london",
              "a Changed while open moves only the mark and the row behind");
        r.act(PadAction::Accept);
        check(r.s().focus == 0 && r.s().mark == 0, "push europe after the Changed: focus and mark on london");
    }

    {
        Rig r;
        r.p.set_zone("Asia/Tokyo");
        r.p.reset(false);
        r.p.action(PadAction::Next, false, r.t);
        int64_t t0 = r.t + 1000;
        r.p.action(PadAction::Accept, false, t0);
        check(r.p.view(t0).sheet.progress == 0 && r.p.view(t0 + 190).sheet.progress > 0 &&
                  r.p.view(t0 + 190).sheet.progress < kOne && r.p.view(t0 + 380).sheet.progress == kOne,
              "the sheet rises over 380 ms");
        r.p.action(PadAction::Down, false, t0 + 100);
        check(r.p.view(t0 + 100).sheet.progress == kOne && r.p.view(t0 + 100).sheet.focus == kEurope,
              "input during a rise lands it, then acts");
        check(r.p.view(t0 + 100).sheet.bar == fx_int(kAsia) && r.p.view(t0 + 100).sheet.first == 2 &&
                  r.p.view(t0 + 200).sheet.bar == fx_int(kEurope),
              "the sheet bar moves in list coordinates over 100 ms");
        int64_t t1 = t0 + 1000;
        r.p.action(PadAction::Up, false, t1);
        r.p.action(PadAction::Accept, false, t1 + 1000);
        check(r.p.view(t1 + 1000).sheet.progress == 0 && r.p.view(t1 + 1380).sheet.progress == kOne,
              "a push rises again from 0");
        r.p.action(PadAction::Back, false, t1 + 2000);
        check(r.p.view(t1 + 2000).sheet.progress == 0 && r.p.view(t1 + 2380).sheet.progress == kOne,
              "a pop rises again from 0");
    }

    {
        Rig r;
        r.p.set_zone("Asia/Tokyo");
        r.act(PadAction::Next);
        r.p.reset(true);
        check(r.v().zone_row && r.v().zone == "tokyo" && r.v().pivot == 0 && r.v().focus == 0 && r.v().light,
              "the value survives reset(), which opens on display");
        r.p.set_zone("Arctic/Longyearbyen");
        r.zone_sheet();
        check(r.s().focus == kArctic, "level 1 opens on arctic");
        r.act(PadAction::Accept);
        check(r.s().count == 1 && r.s().focus == 0 && r.s().mark == 0 && r.item(0) == "longyearbyen" &&
                  r.p.view(r.t).sheet.title == std::string("time zone"),
              "push arctic: one row, the title kept");
    }

    {
        std::string text;
        char line[64];
        for (int i = 0; i < 144; i++) {
            std::snprintf(line, sizeof line, "XX\t+0000+00000\tSynth/Z%03d\n", i);
            text += line;
        }
        for (int i = 0; i < 8; i++) {
            std::snprintf(line, sizeof line, "XX\t+0000+00000\tEight/E%d\n", i);
            text += line;
        }
        SettingsPage p;
        p.set_sheet_rows(7);
        p.set_zones(parse_zones(text));
        p.set_zone("Synth/Z143");
        p.reset(false);
        int64_t t = 1000;
        p.action(PadAction::Next, false, t);
        p.action(PadAction::Accept, false, t += 1000);
        p.action(PadAction::Accept, false, t += 1000);
        SheetView s = p.view(t += 1000).sheet;
        check(s.count == 144 && s.focus == 143 && s.first == 137, "Wrap bar: 144 rows open on the last row");
        p.action(PadAction::Down, false, t += 1000);
        s = p.view(t).sheet;
        fx mid = p.view(t + 50).sheet.bar;
        check(s.first == 0 && s.focus == 0 && s.bar == fx_int(6) && p.view(t + 100).sheet.bar == fx_int(0) &&
                  mid > fx_int(0) && mid < fx_int(6),
              "Wrap bar: a fresh Down at the end of 144 rows: first row 0, bar 6 at the press, 0 at 100 ms");
        p.action(PadAction::Up, false, t += 1000);
        s = p.view(t).sheet;
        mid = p.view(t + 50).sheet.bar;
        check(s.first == 137 && s.focus == 143 && s.bar == fx_int(137) && p.view(t + 100).sheet.bar == fx_int(143) &&
                  mid > fx_int(137) && mid < fx_int(143),
              "Wrap bar: a fresh Up from row 0: first row 137, bar 137 at the press, 143 at 100 ms");
        p.action(PadAction::Back, false, t += 1000);
        p.action(PadAction::Up, false, t += 1000);
        p.action(PadAction::Accept, false, t += 1000);
        for (int i = 0; i < 7; i++)
            p.action(PadAction::Down, false, t += 1000);
        s = p.view(t).sheet;
        check(s.count == 8 && s.focus == 7 && s.first == 1, "Wrap bar: 8 rows at 7, the last row focused");
        p.action(PadAction::Down, false, t += 1000);
        s = p.view(t).sheet;
        check(s.first == 0 && s.focus == 0 && s.bar == fx_int(6) && p.view(t + 100).sheet.bar == fx_int(0),
              "Wrap bar: 8 rows at 7 rows: a fresh Down at the end starts the bar at slot 6");
        p.action(PadAction::Down, true, t += 1000);
        p.action(PadAction::Up, true, t += 1000);
        p.action(PadAction::Up, true, t += 1000);
        check(p.view(t).sheet.focus == 0 && p.view(t).sheet.first == 0, "Wrap bar: a held Up at row 0 does not wrap");
    }

    {
        Rig r;
        r.act(PadAction::Accept);
        r.act(PadAction::Down);
        int64_t t0 = r.t;
        r.p.action(PadAction::Down, false, t0);
        fx mid = r.p.view(t0 + 50).sheet.bar;
        check(r.p.view(t0).sheet.focus == 0 && r.p.view(t0).sheet.bar == fx_int(1) &&
                  r.p.view(t0 + 100).sheet.bar == fx_int(0) && mid > 0 && mid < fx_int(1),
              "Wrap bar: the theme sheet's wrap glides from 1 to 0 as before");
    }

    {
        SettingsPage p;
        p.set_zones(Zones());
        p.set_zone("Asia/Tokyo");
        p.reset(false);
        p.action(PadAction::Next, false, 0);
        p.action(PadAction::Accept, false, 1000);
        SheetView s = p.view(2000).sheet;
        check(s.count == 1 && std::string(s.items[0]) == "utc" && s.focus == 0 && s.mark == -1,
              "with no zone.tab level 1 lists only utc");
    }

    {
        Kinds k;
        check(k.v().pivots.size() == 1 && k.v().rows.size() == 1, "a table row shows only once it is set shown");
        k.p.set_row("test.live", row_state("50"), k.t);
        k.p.set_row("test.sheet", row_state("5"), k.t);
        k.p.set_row("test.action", row_state(""), k.t);
        k.p.set_row("test.push", row_state("", "not connected"), k.t);
        k.p.set_row("test.info", row_state("", "62 % charging"), k.t);
        k.p.set_row("test.rowsheet", row_state(""), k.t);
        SettingsView v = k.v();
        std::string words;
        for (const char* w : v.pivots)
            words += std::string(w) + " ";
        check(words == "display sound controls network power system ",
              "pivots in table order, games hidden with no row shown, system shown by its row sheet row");
        check(v.rows.size() == 2 && v.rows[1].title == "level" && v.rows[1].value == "50" &&
                  v.rows[1].kind == RowKind::Live,
              "display holds theme and the live row with its value word");

        int64_t t0 = k.t;
        k.p.action(PadAction::Down, false, t0);
        check(k.p.view(t0).bar == fx_int(0) && k.p.view(t0 + 100).bar == fx_int(1) &&
                  k.p.view(t0 + 50).bar > fx_int(0) && k.p.view(t0 + 50).bar < fx_int(1),
              "the page focus bar moves over 100 ms");
        k.t += 1000;
        k.act(PadAction::Accept);
        check(k.v().sheet.open && std::string(k.v().sheet.title) == "level" && k.v().sheet.count == 3 &&
                  k.v().sheet.focus == 1 && k.v().sheet.mark == 1,
              "live: Accept opens its sheet on the marked value");
        k.act(PadAction::Down);
        check(k.last() == "preview test.live 10" && k.v().sheet.focus == 2, "live: each focus step previews");
        size_t n = k.log.size();
        k.act(PadAction::Down);
        check(k.log.size() == n && k.v().sheet.focus == 2, "live: a bounded sheet stops at the end, no wrap");
        k.act(PadAction::Back);
        check(!k.v().sheet.open && k.last() == "restore test.live", "live: Back closes and restores");
        k.act(PadAction::Accept);
        k.act(PadAction::Up);
        k.act(PadAction::Down);
        n = k.log.size();
        k.act(PadAction::Accept);
        check(!k.v().sheet.open && k.log.size() == n + 2 && k.log[n] == "close test.live" &&
                  k.last() == "pick test.live 50",
              "live: Accept back on the opening value after a move picks it");
        k.act(PadAction::Accept);
        n = k.log.size();
        k.act(PadAction::Accept);
        check(!k.v().sheet.open && k.log.size() == n + 1 && k.last() == "close test.live",
              "live: Accept with no move sends nothing");
        k.act(PadAction::Accept);
        k.act(PadAction::Up);
        k.act(PadAction::Accept);
        check(k.last() == "pick test.live 100", "live: Accept on another value picks it");

        k.act(PadAction::Next);
        check(k.pivot() == "sound" && k.v().rows.size() == 1 && k.v().focus == 0, "Next: sound with its one row");
        k.act(PadAction::Prev);
        check(k.pivot() == "display" && k.v().focus == 1, "each pivot keeps its focus row");
        k.p.reset(false);
        check(k.pivot() == "display" && k.v().focus == 0, "reset opens on display with every focus at row 0");

        k.go("sound");
        k.act(PadAction::Accept);
        check(k.v().sheet.focus == 1 && k.v().sheet.mark == 1, "sheet: opens on its value, marked");
        k.act(PadAction::Down);
        k.act(PadAction::Down);
        check(k.v().sheet.focus == 0, "sheet: a fresh Down past the end wraps");
        n = k.log.size();
        k.act(PadAction::Accept);
        check(k.log.size() == n + 2 && k.log[n] == "close test.sheet" && k.last() == "pick test.sheet 10" &&
                  !k.v().sheet.open,
              "sheet: Accept closes, then picks, no preview on the way");
        k.act(PadAction::Accept);
        n = k.log.size();
        k.act(PadAction::Accept);
        check(k.log.size() == n + 1 && k.last() == "close test.sheet", "sheet: Accept on the live value closes with no pick");

        k.p.set_row("test.first", row_state("2"), k.t);
        check(k.v().rows.size() == 2 && k.v().rows[0].title == "first" && k.v().focus == 1,
              "a row appearing above the focus keeps the focus on its row, in place");
        k.p.set_row("test.sheet", SettingsRowState{}, k.t);
        check(k.v().rows.size() == 1 && k.v().focus == 0 && k.v().bar == fx_int(0),
              "the focused row going moves the focus to the row above at once");
        k.p.set_row("test.first", SettingsRowState{}, k.t);
        check(k.pivot() == "display" && k.v().from_pivot < 0,
              "the active pivot's last row going lands on the pivot before");
        k.p.set_row("test.sheet", row_state("5"), k.t);

        k.go("controls");
        k.act(PadAction::Accept);
        check(k.last() == "action test.action" && !k.v().sheet.open, "action: Accept runs its action");
        k.go("network");
        k.act(PadAction::Accept);
        check(k.last() == "push test.push" && k.v().rows[0].value == "not connected",
              "push: Accept pushes, its text shown as the value");
        k.go("power");
        n = k.log.size();
        k.act(PadAction::Accept);
        check(k.v().rows[0].kind == RowKind::Info && k.v().rows[0].value == "62 % charging" && k.log.size() == n &&
                  !k.v().sheet.open && !k.v().rowsheet.open,
              "info: takes focus, Accept does nothing");

        SettingsRowState lines = row_state("");
        lines.lines = {{"blockie", "20261109.abc1234"}, {"kernel", "5.10.0-test"}};
        k.p.set_row("test.rowsheet", lines, k.t);
        k.go("system");
        int64_t t1 = k.t;
        k.p.action(PadAction::Accept, false, t1);
        RowSheetView rs = k.p.view(t1 + 380).rowsheet;
        check(rs.open && rs.progress == kOne && k.p.view(t1).rowsheet.progress == 0 &&
                  std::string(rs.title) == "versions" && rs.lines.size() == 2 && rs.lines[1].value == "5.10.0-test",
              "row sheet: Accept rises it with its lines over 380 ms");
        k.t = t1 + 1000;
        k.act(PadAction::Down);
        k.act(PadAction::Accept);
        k.act(PadAction::Next);
        check(k.v().rowsheet.open && k.pivot() == "system", "row sheet: Down, Accept and Next do nothing");
        int64_t t2 = k.t;
        k.p.action(PadAction::Back, false, t2);
        check(!k.p.view(t2).rowsheet.open && k.p.view(t2 + 380).rowsheet.progress == 0, "row sheet: Back closes it");

        k.p.reset(false);
        k.act(PadAction::Down);
        k.act(PadAction::Accept);
        n = k.log.size();
        k.p.set_row("test.live", SettingsRowState{}, k.t);
        check(!k.v().sheet.open && k.v().focus == 0 && k.log.size() == n + 1 && k.last() == "close test.live",
              "a live row going closes its open sheet, nothing applied");
    }

    {
        Kinds k;
        k.p.set_row("test.sheet", row_state("5"), k.t);
        int64_t t0 = k.t;
        k.p.action(PadAction::Next, false, t0);
        SettingsView a = k.p.view(t0 + 10);
        SettingsView b = k.p.view(t0 + settings_switch_ms());
        check(a.pivot == 1 && a.from_pivot == 0 && a.dir == 1 && a.from_rows.size() == 1 &&
                  a.from_rows[0].title == "theme" && a.rows[0].title == "step" && b.from_pivot < 0,
              "a switch keeps the rows it leaves until it ends");
        check(settings_switch_ms() == swap_ms() && swap_ms() == 250, "the switch lasts the T5 swap, 50 out and 200 in");
        k.p.action(PadAction::Prev, false, t0 + 100);
        SettingsView c = k.p.view(t0 + 110);
        check(c.pivot == 0 && c.from_pivot == 1 && c.dir == -1, "input during a switch lands it, then switches back");
        check(k.p.animating(t0 + 110) && !k.p.animating(t0 + 100 + settings_switch_ms()), "animating while it runs");
    }

    {
        Buffer from, to, dst, gb;
        int w = from.surface().w;
        Rect area{0, 200, w, 300};
        Canvas fc(from.surface()), tc(to.surface()), gc(gb.surface());
        uint32_t ground = 0x101010;
        fc.fill(area, 0x808080);
        tc.fill(area, 0x404040);
        gc.fill(area, ground);
        Surface ds = dst.surface();
        auto at = [&](int64_t t, int dir, int x) {
            settings_rows_switch(from.surface(), to.surface(), ds, area, t, dir, ground);
            return ds.at(x, 250);
        };
        auto gp = gb.surface().at(w / 2, 250);
        auto fp = from.surface().at(w / 2, 250);
        int out = fx_round(tok::motion_swapOut);
        check(at(0, 1, w / 2) == fp && at(swap_ms(), 1, w / 2) == to.surface().at(w / 2, 250),
              "T5: the leaving rows at 0, the arriving rows at the end");
        auto mid = at(out / 2, 1, w / 2);
        check(at(out, 1, w / 2) == gp && mid != gp && mid != fp, "T5: a fade out, then bare ground between out and in");
        check(at(out / 2, 1, w - 1) == gp && at(out / 2, 1, 0) != gp, "T5: leaving with dir 1 moves the rows left");
        check(at(out / 2, -1, 0) == gp && at(out / 2, -1, w - 1) != gp, "T5: leaving with dir -1 moves them right");
    }

    {
        SettingsPage p;
        int64_t t0 = 5000;
        p.toast(t0, "time zone", "denied");
        int in = fx_round(tok::motion_toastIn), hold = fx_round(tok::motion_toastHold);
        int out = fx_round(tok::motion_toastOut);
        check(p.view(t0).toast.e == 0 && p.view(t0 + in / 2).toast.e > 0 && p.view(t0 + in).toast.e == kOne &&
                  p.view(t0 + in).toast.line1 == "time zone" && p.view(t0 + in).toast.line2 == "denied",
              "toast: rises with its two lines");
        check(p.deadline(t0 + in) == t0 + in + hold && p.deadline(t0 + in + hold) == t0 + in + hold + out &&
                  p.view(t0 + in + hold + out).toast.e == 0 && !p.animating(t0 + in + 10),
              "toast: holds, then leaves, with a wake at each edge");
        p.toast(t0 + in + 100, "theme", "busy");
        int d = fx_round(tok::motion_valueChange);
        int64_t sw = t0 + in + 100;
        check(p.view(sw + d - 1).toast.line2 == "denied" && p.view(sw + d).toast.line2 == "busy" &&
                  p.view(sw + d).toast.text < kOne,
              "toast: a second one while shown swaps the text through a dip");
    }

    {
        std::vector<SettingsRowSpec> t = settings_rows();
        std::string order;
        bool pairs = true, unique = true;
        for (size_t i = 0; i < t.size(); i++) {
            order += std::string(t[i].pivot) + ":" + t[i].title + ",";
            pairs = pairs && t[i].words.size() == t[i].values.size();
            for (size_t j = 0; j < i; j++)
                unique = unique && std::string(t[i].id) != t[j].id;
        }
        check(order == "display:theme,display:brightness,sound:volume,sound:volume step,controls:vibration,"
                       "games:shaders,games:integer scale,games:resume,games:rewind,network:wi-fi,network:bluetooth,"
                       "power:battery,power:screen off after,system:time zone,system:storage,system:android gpu,"
                       "system:android performance,system:about,",
              "rows: the item 3 table in pivot order");
        check(pairs && unique, "rows: every row has a value per word, ids are unique");
        auto row = [&](const char* id) {
            for (const SettingsRowSpec& r : t)
                if (std::string(r.id) == id)
                    return r;
            return SettingsRowSpec{};
        };
        SettingsRowSpec b = row(kBrightnessRow), v = row(kVolumeRow);
        check(b.kind == RowKind::Live && b.bounded && b.setd && b.type == SetdType::U && b.values.size() == 19 &&
                  std::string(b.values.front()) == "100" && std::string(b.values.back()) == "10",
              "rows: brightness is a bounded live u row, 100 to 10 step 5");
        check(v.kind == RowKind::Live && v.bounded && !v.setd && v.values.size() == 21 &&
                  std::string(v.values.back()) == "0",
              "rows: volume is a bounded live row, 100 to 0 step 5, not a setd key");
        bool no_lead = true;
        for (const SettingsRowSpec& r : t)
            no_lead = no_lead && std::string(r.lead).empty();
        check(no_lead && row("games.shader").type == SetdType::B &&
                  row("power.screen_off").type == SetdType::U && row("android.perf").type == SetdType::S &&
                  !row(kRadioRow).setd && row(kBluetoothRow).kind == RowKind::Push &&
                  row(kBatteryRow).kind == RowKind::Info && row(kStorageRow).kind == RowKind::RowSheet &&
                  row(kAboutRow).kind == RowKind::RowSheet && !row(kThemeRow).setd && !row(kZoneRow).setd,
              "rows: no row carries a lead line; the key types and the kinds");

        SettingsPage p;
        p.reset(false);
        p.set_zone("Asia/Tokyo");
        for (const SettingsRowSpec& r : t) {
            SettingsRowState st;
            st.shown = true;
            st.value = r.values.empty() ? "" : r.values.back();
            p.set_row(r.id, st, 0);
        }
        p.set_row("games.shader", row_state("true"), 0);
        p.set_row("power.screen_off", row_state("300"), 0);
        p.set_row("android.perf", row_state("policy"), 0);
        SettingsView pv = p.view(0);
        std::string words;
        for (const char* w : pv.pivots)
            words += std::string(w) + " ";
        check(words == "display sound controls games network power system ", "rows: every pivot shows with its rows");
        int most = 0;
        std::string values;
        for (int i = 0; i < kSettingsPivots; i++) {
            SettingsView w = p.view(0);
            most = std::max(most, (int)w.rows.size());
            for (const SettingsRowView& r : w.rows)
                values += r.value + ",";
            p.action(PadAction::Next, false, 1000 * (i + 1));
            p.land();
        }
        check(most <= 6, "rows: no pivot holds more than 6 rows");
        check(values == "dark,10,0,2,off,on,off,off,off,off,,,5 min,asia / tokyo,,soft,on,,",
              "rows: values read as their words (true on, 0 off, 300 5 min, policy on)");
    }

    {
        SettingsPage p;
        p.reset(false);
        p.set_row("sound.volume_step", row_state("5"), 0);
        p.set_row(kVolumeRow, volume_row(true, 35, false), 0);
        p.action(PadAction::Next, false, 1000);
        p.land();
        bool shifted = p.view(2000).focus == 1;
        p.reset(false);
        p.action(PadAction::Next, false, 3000);
        p.land();
        check(shifted && p.view(4000).focus == 0 && p.view(4000).rows[0].title == "volume",
              "a row set above the focus keeps the focus on its row, and reset at page open puts every focus on row 0");
    }

    {
        Kinds k;
        k.p.set_row("test.live", row_state("30"), k.t);
        k.act(PadAction::Down);
        k.act(PadAction::Accept);
        check(k.v().sheet.focus == 1 && k.v().sheet.mark == 1 && k.last() == "open test.live",
              "nearest: 30 between 50 and 10 opens on 50 (ties up), marked, on_open told");
        size_t n = k.log.size();
        k.act(PadAction::Accept);
        check(k.log.size() == n + 1 && k.last() == "close test.live" && !k.v().sheet.open,
              "nearest: Accept on the opening step closes with no pick and no preview");
        k.p.set_row("test.live", row_state("75"), k.t);
        k.act(PadAction::Accept);
        check(k.v().sheet.focus == 0 && k.v().sheet.mark == 0, "nearest: 75 between 100 and 50 opens on 100");
        k.act(PadAction::Down);
        n = k.log.size();
        k.act(PadAction::Back);
        check(k.log.size() == n + 2 && k.log[n] == "close test.live" && k.log[n + 1] == "restore test.live",
              "live: Back tells close, then restore");
        k.p.set_row("test.live", row_state("77x"), k.t);
        k.act(PadAction::Accept);
        check(k.v().sheet.focus == 0 && k.v().sheet.mark == -1, "nearest: a value that is not a number marks nothing");
        k.act(PadAction::Back);

        k.p.set_row("test.live", row_state("50"), k.t);
        k.act(PadAction::Accept);
        k.act(PadAction::Down);
        n = k.log.size();
        k.p.close_row("test.live", k.t);
        check(!k.v().sheet.open && k.log.size() == n + 1 && k.last() == "close test.live",
              "close_row: closes the open sheet, nothing restored");
        k.act(PadAction::Accept);
        n = k.log.size();
        k.p.set_row("test.live", volume_row(false, 0, false), k.t);
        check(!k.v().sheet.open && k.log.size() == n + 1 && k.last() == "close test.live" && k.v().rows.size() == 1 &&
                  k.v().focus == 0,
              "hidden: a backend going hides its row and closes its sheet");
        k.p.set_row("test.live", volume_row(true, 50, false), k.t);
        k.act(PadAction::Down);
        k.act(PadAction::Accept);
        check(k.v().rows.size() == 2 && k.v().sheet.open, "hidden: the row comes back with its backend and opens");
    }

    {
        SettingsRowState a = volume_row(true, 35, false), b = volume_row(true, 35, true), c = volume_row(false, 0, false);
        check(a.shown && a.value == "35" && a.text.empty() && b.shown && b.value == "35" && b.text == "muted" &&
                  !c.shown && c.text.empty(),
              "volume row: the level, muted while muted, hidden when get fails");
        SettingsRowState on = radio_row(true, true), off = radio_row(true, false), gone = radio_row(false, true);
        check(on.shown && on.value == "on" && off.value == "off" && !gone.shown && gone.text.empty(),
              "wi-fi row: on or off, hidden with NM gone or no wlan0");
        SettingsRowState inf = info_row("62 % charging", {{"data", "82 of 106 gb free"}});
        check(inf.shown && inf.text == "62 % charging" && inf.lines.size() == 1 && inf.lines[0].title == "data",
              "info row: text and lines");
        SettingsPage p;
        p.reset(false);
        p.set_row("android.gpu", row_state("mali"), 0);
        p.set_row(kAboutRow, info_row(""), 0);
        p.action(PadAction::Next, false, 1000);
        p.land();
        p.action(PadAction::Accept, false, 2000);
        SheetView sv = p.view(3000).sheet;
        check(sv.open && std::string(sv.title) == "android gpu" && std::string(sv.lead).empty() && sv.count == 2 &&
                  sv.mark == 0,
              "android gpu: its sheet opens on mali with no lead line");
    }

    std::string err = SetdKey::kErr;
    check(setd_fail_text(err + "Invalid", "time zone is one of utc") == "time zone is one of utc" &&
              setd_fail_text(err + "Denied", "x") == "denied" &&
              setd_fail_text(err + "ReadOnly", "x") == "denied" &&
              setd_fail_text(err + "Busy", "x") == "busy" &&
              setd_fail_text(err + "Timeout", "x") == "timed out" &&
              setd_fail_text("org.freedesktop.DBus.Error.NoReply", "no reply") == "timed out" &&
              setd_fail_text(err + "Failed", "x") == "failed" &&
              setd_fail_text(err + "UnknownKey", "x") == "unavailable" &&
              setd_fail_text("org.freedesktop.DBus.Error.ServiceUnknown", "x") == "unavailable",
          "toast words: setd's own text only for Invalid, one word for every other kind");

    std::string dir = argc > 1 ? argv[1] : "fonts";
    Fonts fonts;
    if (!fonts.open(dir, {})) {
        std::printf("FAIL fonts not found in %s\n", dir.c_str());
        return 1;
    }
    fonts.begin_frame(-1);
    check(value_sheet_rows(fonts) == 7, "the value sheet fits 7 rows at 720 px");
    check(sheet_rows(fonts) == 6 && sheet_rows(fonts, 7) == 7 && sheet_rows(fonts, 9) == 7,
          "the app and the shot take 6 rows by default, a wider ask is capped at the 7 that fit");
    {
        SettingsPage p;
        p.set_zones(parse_zones(std::string(kFixture) + "NZ\t-3652+17446\tPacific/Auckland\n"));
        p.set_zone("Asia/Tokyo");
        p.reset(false);
        p.action(PadAction::Next, false, 0);
        p.action(PadAction::Accept, false, 1000);
        SheetView s = p.view(2000).sheet;
        check(SettingsPage::kSheetRows == 6 && s.rows == 6 && s.count == 7 && sheet_geom(fonts, s, kScreen).shown == 6,
              "a long sheet shows 6 rows by default");
        Buffer b;
        Canvas c(b.surface());
        InkLayer ink;
        draw_settings(c, fonts, ink, p.view(2000), theme_dark());
        Surface sf = b.surface();
        check(sf.at(500, 40) != sf.at(500, kPortraitH - 10) && sf.at(500, 87) == sf.at(500, kPortraitH - 10),
              "by default the page top shows under the scrim above the 6 rows");
    }
    {
        Kinds k;
        k.p.set_row("test.sheet", row_state("5"), k.t);
        int64_t t0 = k.t;
        k.p.action(PadAction::Next, false, t0);
        Buffer a, b;
        Canvas ca(a.surface()), cb(b.surface());
        InkLayer ink;
        draw_settings(ca, fonts, ink, k.p.view(t0 + fx_round(tok::motion_swapOut)), theme_dark());
        draw_settings(cb, fonts, ink, k.p.view(t0 + settings_switch_ms()), theme_dark());
        Surface sa = a.surface(), sb = b.surface();
        auto ground = sa.at(sa.w - 1, sa.h - 1);
        bool bare = true, drawn = false;
        for (int y = settings_row_y(0); y < settings_row_y(1); y++)
            for (int x = 0; x < sa.w; x++) {
                bare = bare && sa.at(x, y) == ground;
                drawn = drawn || sb.at(x, y) != ground;
            }
        check(bare && drawn, "draw: the rows band is bare ground between out and in, the new row drawn at the end");
        SettingsRowState lines = row_state("");
        lines.lines = {{"kernel", "5.10.0-test"}};
        k.p.set_row("test.rowsheet", lines, k.t);
        k.go("system");
        k.p.action(PadAction::Accept, false, k.t);
        draw_settings(ca, fonts, ink, k.p.view(k.t + 1000), theme_dark());
        check(sa.at(sa.w / 2, sa.h - 2) != ground, "draw: the row sheet panel covers the page bottom");
        k.p.toast(k.t, "theme", "denied");
        draw_settings(ca, fonts, ink, k.p.view(k.t + 2000), theme_dark());
        draw_settings(cb, fonts, ink, k.p.view(k.t + 20000), theme_dark());
        bool strip = false;
        for (int x = 0; x < sa.w; x++)
            strip = strip || sa.at(x, 2) != sb.at(x, 2);
        check(strip, "draw: the toast strip shows at the top while it holds, gone after");
    }
    {
        SheetView a;
        a.count = 2;
        a.progress = kOne;
        SheetView b = a;
        b.lead = "blockie test lead";
        SheetGeom ga = sheet_geom(fonts, a, kScreen), gb = sheet_geom(fonts, b, kScreen);
        check(gb.rows_top == ga.rows_top && gb.top == ga.top - ga.pitch && gb.shown == 2,
              "lead: the panel grows by one row above the items, the items stay at the bottom");
        b.count = 30;
        b.rows = 9;
        a.count = 30;
        a.rows = 9;
        check(sheet_geom(fonts, b, kScreen).shown == sheet_geom(fonts, a, kScreen).shown - 1,
              "lead: a full sheet fits one item fewer");
        std::vector<SettingsRowSpec> lt = settings_rows();
        SettingsRowSpec lr = spec("system", "test lead", RowKind::Sheet, "test.lead", {"a", "b"});
        lr.lead = "blockie test lead";
        lt.push_back(lr);
        SettingsPage p;
        p.set_table(lt);
        p.reset(false);
        p.set_row("test.lead", row_state("a"), 0);
        p.action(PadAction::Next, false, 1000);
        p.land();
        p.action(PadAction::Accept, false, 2000);
        Buffer x;
        Canvas cx(x.surface());
        InkLayer ink;
        draw_settings(cx, fonts, ink, p.view(3000), theme_dark());
        Surface sx = x.surface();
        SheetGeom g = sheet_geom(fonts, p.view(3000).sheet, kScreen);
        bool ink_in_lead = false;
        for (int yy = g.rows_top - g.pitch; yy < g.rows_top; yy++)
            for (int xx = 0; xx < sx.w; xx++)
                ink_in_lead = ink_in_lead || sx.at(xx, yy) != sx.at(sx.w - 1, g.rows_top - g.pitch);
        check(ink_in_lead, "lead: the line draws in its row");
    }
    {
        TextStyle is = role::dialogItem.style();
        int w64 = fonts.width64(is, "blockie test");
        std::vector<std::string> one = row_sheet_wrap(fonts, is, "blockie test", w64, w64);
        std::vector<std::string> two = row_sheet_wrap(fonts, is, "blockie test words", w64, w64 * 2);
        std::vector<std::string> cut = row_sheet_wrap(fonts, is, "blockietestwords", w64, w64);
        std::vector<std::string> down = row_sheet_wrap(fonts, is, "blockietestwords", w64, w64 * 2);
        std::vector<std::string> far =
            row_sheet_wrap(fonts, is, "blockie testwordsthatrunpastthesecondline", w64, w64);
        check(one.size() == 1 && two.size() == 2 && two[0] == "blockie test" && two[1] == "words",
              "SX2 wrap: a line that fits stays, a longer one breaks at the last space that fits");
        check(cut.size() == 2 && !cut[0].empty() && cut[0] + cut[1] == "blockietestwords" &&
                  fonts.width64(is, cut[0].c_str()) <= w64,
              "SX2 wrap: no space and too long for line 2 breaks at the last code point that fits");
        check(down.size() == 2 && down[0].empty() && down[1] == "blockietestwords",
              "SX2 wrap: no space but room on line 2 moves the whole title to line 2");
        check(far.size() == 2 && far[0] != "blockie" && far[0].rfind("blockie ", 0) == 0,
              "SX2 wrap: a rest too long for line 2 breaks at the last code point instead");
        SettingsRowState shortl = row_state(""), longl = row_state("");
        shortl.lines = {{"blockie", "20261110.0a1b2c3"}};
        longl.lines = {{"blockie test line that is long enough to wrap onto a second line here", "20261110.0a1b2c3"}};
        Buffer x, y;
        Canvas cx(x.surface()), cy(y.surface());
        InkLayer ink;
        RowSheetView rs;
        rs.title = "about";
        rs.progress = kOne;
        rs.open = true;
        rs.lines = shortl.lines;
        draw_row_sheet(cx, fonts, rs, theme_dark());
        rs.lines = longl.lines;
        draw_row_sheet(cy, fonts, rs, theme_dark());
        SheetView sv;
        sv.count = 1;
        sv.progress = kOne;
        int top = sheet_geom(fonts, sv, kScreen).top;
        Surface sx = x.surface(), sy = y.surface();
        check(sx.at(sx.w - 2, top) == sy.at(sy.w - 2, top) && sx.at(sx.w - 2, top - 1) != sy.at(sy.w - 2, top - 1),
              "SX2 wrap: a wrapped line grows the panel upward by one line box");
    }
    {
        auto to64 = [](fx u) { return (to_px(u) + 512) >> 10; };
        std::vector<const char*> words(std::begin(kSettingsPivotWords), std::end(kSettingsPivotWords));
        int n = 0;
        double sum[2] = {0, 0};
        int count[2] = {0, 0};
        for (int k = 0; k + 1 < kSettingsPivots; k++)
            for (int act : {0, 1}) {
                if (act == 0 && k == 0)
                    continue;
                Chrome ch;
                ch.measure(fonts, "settings", words);
                ch.set_active(act ? k : 0);
                const Role& r = act ? role::pivotActive : role::pivotInactive;
                int adv = fonts.width64(r.style(), words[(size_t)k]);
                int ink = ch.pivot_x64(k + 1) - ch.pivot_x64(k);
                int gap = to64(act ? tok::pivotActiveGap : tok::pivotGap);
                int md = adv + to64(16 * kOne);
                int diff = md - (ink - gap + to64(16 * kOne));
                std::printf("INFO pivot gap %s %s>%s: advance + 16 u %.2f px, ink + 16 u %.2f px, tighter by %.2f px\n",
                            act ? "active" : "inactive", words[(size_t)k], words[(size_t)k + 1], md / 64.0,
                            (ink - gap + to64(16 * kOne)) / 64.0, diff / 64.0);
                sum[act] += diff / 64.0;
                count[act]++;
                n++;
            }
        std::printf("INFO pivot gap mean tighter by: inactive %.2f px over %d pairs, active %.2f px over %d pairs\n",
                    sum[0] / count[0], count[0], sum[1] / count[1], count[1]);
        check(n == 11, "pivot gaps: the six word pairs measured (five inactive, six with the left word active)");
    }
    for (int rows : {7, 6}) {
        SettingsPage p;
        p.set_sheet_rows(rows);
        p.set_zones(parse_zones(std::string(kFixture) + "NZ\t-3652+17446\tPacific/Auckland\n"));
        p.set_zone("Asia/Tokyo");
        p.reset(false);
        p.action(PadAction::Next, false, 0);
        p.action(PadAction::Accept, false, 1000);
        Buffer b;
        Canvas c(b.surface());
        InkLayer ink;
        draw_settings(c, fonts, ink, p.view(2000), theme_dark());
        Surface sf = b.surface();
        bool band = sf.at(500, 40) != sf.at(500, kPortraitH - 10);
        bool panel = sf.at(500, 87) == sf.at(500, kPortraitH - 10);
        check(rows == 7 ? !band : band && panel,
              rows == 7 ? "7 rows: the panel fills the screen" : "6 rows: the page top shows under the scrim");
    }
    {
        SettingsPage p;
        std::vector<std::string> pushes;
        p.on_push = [&](const std::string& r) { pushes.push_back(r); };
        BtState bs;
        bs.up = true;
        bs.adapter.path = "/org/bluez/hci0";
        bs.adapter.powered = true;
        std::string last = bt_subtitle(bs, 0);
        p.set_row(kRadioRow, radio_row(true, true), 0);
        p.set_row(kBluetoothRow, info_row(last), 0);
        p.reset(false);
        int64_t t = 1000000;
        p.action(PadAction::Next, false, t);
        p.action(PadAction::Down, false, t += 1000);
        SettingsView v = p.view(t += 1000);
        bool on_row = std::string(p.pivot()) == "network" && v.focus == 1 && v.rows.size() == 2 &&
                      v.rows[1].title == "bluetooth" && v.rows[1].value == "not connected";
        BtDevice pad;
        pad.path = "/org/bluez/hci0/dev_02_00_00_00_00_01";
        pad.adapter = bs.adapter.path;
        pad.address = "02:00:00:00:00:01";
        pad.alias = "blockie-test-pad";
        pad.paired = true;
        pad.connected = true;
        pad.uuids = {"00001124-0000-1000-8000-00805f9b34fb"};
        bs.devices.push_back(pad);
        bool changed = sync_bt_row(p, last, bt_subtitle(bs, t), t);
        SettingsView w = p.view(t);
        bool shown = w.rows.size() == 2 && w.rows[1].value == "blockie-test-pad";
        bool again = sync_bt_row(p, last, bt_subtitle(bs, t + 1000), t + 1000);
        check(on_row && changed && shown && !again && last == "blockie-test-pad",
              "a BtState change updates the settings bluetooth row with no home entry, the same subtitle leaves it");
        p.action(PadAction::Accept, false, t += 2000);
        SettingsView after = p.view(t += 1000);
        check(pushes.size() == 1 && pushes[0] == kBluetoothRow && std::string(p.pivot()) == "network" &&
                  after.focus == 1 && after.rows.size() == 2 && after.rows[1].title == "bluetooth",
              "accept on the bluetooth row asks for the push and keeps network with the row focused for the pop");
    }

    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
