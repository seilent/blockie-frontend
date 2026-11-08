#include <cstdio>
#include <string>
#include <vector>

#include "../screen_settings.h"
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
        act(PadAction::Down);
        act(PadAction::Accept);
    }
};

}

int main(int argc, char** argv)
{
    {
        Rig r;
        check(!r.v().zone_row, "one row until the zone has a value");
        r.act(PadAction::Down);
        check(r.v().focus == 0, "Down on one row stays");
        r.p.set_zone("Asia/Tokyo");
        check(r.v().zone_row && r.v().zone == "tokyo", "two rows once the zone has a value, the row shows its words");
        r.act(PadAction::Down);
        check(r.v().focus == 1, "Down moves to the time zone row");
        r.act(PadAction::Down);
        r.act(PadAction::Down, true);
        check(r.v().focus == 1, "page Down stops at the end, held or not");
        r.act(PadAction::Up);
        r.act(PadAction::Up);
        r.act(PadAction::Up, true);
        check(r.v().focus == 0, "page Up stops at the end, held or not");
        r.act(PadAction::Down);
        r.p.set_zone_missing(r.t);
        check(!r.v().zone_row && r.v().focus == 0 && r.v().bar == fx_int(0),
              "one row again on missing, the focus and the bar back on theme at once");
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
        r.p.action(PadAction::Down, false, r.t);
        int64_t t0 = r.t + 1000;
        r.p.action(PadAction::Accept, false, t0);
        r.p.set_zone_missing(t0 + 100);
        check(r.p.view(t0 + 100).sheet.progress == kOne && r.p.view(t0 + 290).sheet.progress > 0 &&
                  r.p.view(t0 + 290).sheet.progress < kOne && r.p.view(t0 + 480).sheet.progress == 0,
              "zone missing during a rise: the rise lands, then the close runs");
        r.p.set_zone("Asia/Tokyo");
        int64_t t1 = t0 + 2000;
        r.p.action(PadAction::Down, false, t1);
        r.p.set_zone_missing(t1 + 50);
        check(r.p.view(t1 + 50).focus == 0 && r.p.view(t1 + 50).bar == fx_int(0),
              "zone missing during a page bar move: the bar lands on row 0 at once");
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
        r.p.set_zone("Asia/Tokyo");
        int64_t t0 = r.t;
        r.p.action(PadAction::Down, false, t0);
        check(r.p.view(t0).bar == fx_int(0) && r.p.view(t0 + 100).bar == fx_int(1) &&
                  r.p.view(t0 + 50).bar > fx_int(0) && r.p.view(t0 + 50).bar < fx_int(1),
              "the page focus bar moves over 100 ms");
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
        r.p.action(PadAction::Down, false, r.t);
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
        r.act(PadAction::Down);
        r.p.reset(true);
        check(r.v().zone_row && r.v().zone == "tokyo" && r.v().focus == 0 && r.v().light, "the value survives reset()");
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
        p.action(PadAction::Down, false, t);
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
        p.action(PadAction::Down, false, 0);
        p.action(PadAction::Accept, false, 1000);
        SheetView s = p.view(2000).sheet;
        check(s.count == 1 && std::string(s.items[0]) == "utc" && s.focus == 0 && s.mark == -1,
              "with no zone.tab level 1 lists only utc");
    }

    std::string dir = argc > 1 ? argv[1] : "fonts";
    Fonts fonts;
    if (!fonts.open(dir, {})) {
        std::printf("FAIL fonts not found in %s\n", dir.c_str());
        return 1;
    }
    fonts.begin_frame(-1);
    check(value_sheet_rows(fonts) == SettingsPage::kSheetRows, "the value sheet fits 7 rows at 720 px");
    for (int rows : {7, 6}) {
        SettingsPage p;
        p.set_sheet_rows(rows);
        p.set_zones(parse_zones(std::string(kFixture) + "NZ\t-3652+17446\tPacific/Auckland\n"));
        p.set_zone("Asia/Tokyo");
        p.reset(false);
        p.action(PadAction::Down, false, 0);
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

    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
