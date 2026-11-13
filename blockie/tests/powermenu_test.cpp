#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <sys/eventfd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../layer_mem.h"
#include "../layer_present.h"
#include "../pm_input.h"
#include "../pm_run.h"
#include "../powermenu.h"
#include "../theme.h"

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

const char* const kDefault = "state=default\nandroid=bootable\nrecovery=no\nfastboot=no\nreturn=keys\n";

struct Rig {
    PowerMenu m;
    int64_t t = 1000000;
    BootGates now_gates;
    std::vector<std::string> lines;

    explicit Rig(const std::string& gates = kDefault, bool dark = false)
    {
        now_gates = parse_gates(gates);
        m.reread = [this]() { return now_gates; };
        m.open(parse_gates(gates), dark, t);
    }

    void at(int64_t ms)
    {
        t += ms;
        m.tick(t);
        std::string s;
        while (m.take_line(s))
            lines.push_back(s);
    }
    void act(PadAction a, bool rep = false)
    {
        m.action(a, rep, t);
        at(0);
    }
    PmView v() const { return m.view(t); }
    bool said(const std::string& s) const
    {
        for (const std::string& l : lines)
            if (l == s)
                return true;
        return false;
    }
    std::string items() const
    {
        PmView w = v();
        std::string s;
        if (w.surface == PmSurface::Sheet) {
            for (int i = 0; i < w.sheet.count; i++)
                s += std::string(i ? "," : "") + w.sheet.items[i];
            return s;
        }
        for (size_t i = 0; i < w.items.size(); i++)
            s += std::string(i ? "," : "") + w.items[i];
        return s;
    }
    void to_sheet()
    {
        at(400);
        act(PadAction::Down);
        act(PadAction::Accept);
        at(400);
    }
    void to_confirm()
    {
        to_sheet();
        act(PadAction::Down);
        act(PadAction::Accept);
        at(400);
        at(400);
    }
};

void gate_cases()
{
    {
        BootGates none;
        PowerMenu m;
        m.open(none, false, 0);
        PmView v = m.view(0);
        check(v.items.size() == 1 && std::string(v.items[0]) == "power off", "no gate file: one row, power off");
    }
    for (const char* st : {"state=pending\n", "state=error\nreason=io\n"}) {
        Rig r(st);
        check(r.items() == "power off", st[6] == 'p' ? "state=pending hides reboot" : "state=error hides reboot");
    }
    {
        Rig r("state=default\n");
        check(r.items() == "power off,reboot", "state=default: two rows, power off then reboot");
        r.to_sheet();
        check(r.v().surface == PmSurface::Sheet && r.items() == "reboot", "sheet holds reboot alone");
    }
    {
        Rig r("state=default\nandroid=bootable\nreturn=keys\n");
        r.to_sheet();
        check(r.items() == "reboot,android", "android=bootable with return=keys adds android");
    }
    {
        Rig r("state=default\nandroid=bootable\nrecovery=yes\nfastboot=yes\nreturn=keys\n");
        r.to_sheet();
        check(r.items() == "reboot,android,recovery", "recovery=yes adds recovery, fastboot=yes still hides fastboot");
    }
    const char* const rets[][2] = {{"return=host\n", "return=host hides android"},
                                   {"", "no return= line hides android"},
                                   {"return=other\n", "return=other hides android"}};
    for (const auto& c : rets) {
        Rig r(std::string("state=default\nandroid=bootable\nrecovery=yes\n") + c[0]);
        r.to_sheet();
        check(r.items() == "reboot,recovery", c[1]);
    }
    {
        Rig r;
        r.to_confirm();
        check(r.v().surface == PmSurface::Confirm && r.v().items.size() == 2,
              "return=keys shows android and its confirm");
    }
}

void focus_cases()
{
    Rig r;
    check(r.v().surface == PmSurface::Power && r.v().focus == 0 && std::string(r.v().items[0]) == "power off",
          "power dialog opens on power off");
    check(std::string(r.v().title) == "power", "power dialog title");
    r.to_sheet();
    check(r.v().surface == PmSurface::Sheet && r.v().sheet.focus == 0 && std::string(r.v().sheet.title) == "reboot",
          "reboot sheet opens on reboot");
    Rig c;
    c.to_confirm();
    PmView v = c.v();
    check(v.surface == PmSurface::Confirm && v.focus == 1 && std::string(v.title) == "switch to android" &&
              std::string(v.items[0]) == "boot android" && std::string(v.items[1]) == "cancel",
          "confirm opens on cancel");
}

void key_cases()
{
    {
        Rig r;
        r.at(400);
        r.act(PadAction::Up);
        check(r.v().focus == 0, "dialog Up stops at the top");
        r.act(PadAction::Down);
        check(r.v().focus == 1, "dialog Down moves to the last row");
        r.act(PadAction::Down);
        check(r.v().focus == 1, "dialog fresh Down at the end stops, no wrap");
        r.act(PadAction::Down, true);
        check(r.v().focus == 1, "dialog held Down at the end stops");
        r.act(PadAction::Left);
        r.act(PadAction::Right);
        r.act(PadAction::Context);
        check(r.v().focus == 1 && r.lines.empty() && r.v().surface == PmSurface::Power,
              "Left, Right and Context do nothing");
        r.at(300);
        r.act(PadAction::Up);
        r.at(100);
        check(r.v().alpha[0] < tok::type_dialogItem_alpha && r.v().alpha[0] > tok::type_dialogItem_alphaDim,
              "dialog row alpha changes over 200");
        r.at(100);
        check(r.v().alpha[0] == tok::type_dialogItem_alpha && r.v().alpha[1] == tok::type_dialogItem_alphaDim,
              "dialog row alpha lands after 200");
    }
    {
        Rig r;
        r.at(400);
        r.act(PadAction::Accept);
        check(r.lines.size() == 1 && r.lines[0] == "poweroff" && r.m.done(), "POWER OFF says poweroff and ends");
    }
    {
        Rig r;
        r.at(50);
        r.act(PadAction::Accept);
        check(r.lines.size() == 1 && r.lines[0] == "poweroff" && r.m.done(),
              "an Accept during the open lands it and says poweroff");
    }
    {
        Rig r;
        r.at(400);
        r.act(PadAction::Back);
        r.at(149);
        check(r.lines.empty(), "dialog Back closes over 150");
        r.at(1);
        check(r.lines.size() == 1 && r.lines[0] == "cancel" && r.m.done(), "dialog Back says cancel once closed");
    }
    {
        Rig r;
        r.at(100);
        fx o = r.v().o;
        r.act(PadAction::Back);
        r.at(16);
        check(r.v().o < o && r.v().s < kOne && r.lines.empty(), "Back during the open reverses it in place");
        r.at(300);
        check(r.lines.size() == 1 && r.lines[0] == "cancel", "the reversed open ends in cancel");
    }
    {
        Rig r;
        r.at(50);
        r.act(PadAction::Down);
        check(r.v().focus == 1 && r.v().o == kOne && r.v().s == kOne, "input during the open lands it, then moves");
    }
    {
        Rig r;
        r.to_sheet();
        r.act(PadAction::Up);
        check(r.v().sheet.focus == 1, "sheet Up on the first row wraps on a fresh press");
        r.act(PadAction::Down);
        check(r.v().sheet.focus == 0, "sheet Down on the last row wraps on a fresh press");
        r.act(PadAction::Down);
        r.act(PadAction::Down, true);
        check(r.v().sheet.focus == 1, "sheet held Down stops at the end");
        r.act(PadAction::Left);
        check(r.v().sheet.focus == 1 && r.lines.empty(), "sheet Left does nothing");
        r.act(PadAction::Back);
        r.at(379);
        check(r.lines.empty() && r.v().surface == PmSurface::Sheet, "sheet Back closes over 380");
        r.at(1);
        check(r.lines.size() == 1 && r.lines[0] == "cancel", "sheet Back says cancel once closed");
    }
    {
        Rig r;
        r.at(400);
        r.act(PadAction::Down);
        r.act(PadAction::Accept);
        r.at(190);
        fx p = r.v().sheet.progress;
        r.act(PadAction::Back);
        r.at(16);
        check(r.v().sheet.progress < p && r.lines.empty(), "Back during the sheet rise reverses it");
        r.at(400);
        check(r.lines.size() == 1 && r.lines[0] == "cancel", "the reversed rise ends in cancel");
    }
    {
        Rig r;
        r.to_sheet();
        r.act(PadAction::Down);
        r.act(PadAction::Accept);
        r.at(379);
        check(r.v().surface == PmSurface::Sheet, "ANDROID closes the sheet first");
        r.at(1);
        check(r.v().surface == PmSurface::Confirm && r.v().o < kOne, "the confirm opens once the sheet landed");
    }
    {
        Rig r;
        r.to_confirm();
        r.act(PadAction::Accept);
        check(r.lines.size() == 1 && r.lines[0] == "cancel", "CANCEL on the confirm says cancel");
    }
    {
        Rig r;
        r.to_confirm();
        r.act(PadAction::Back);
        r.at(150);
        check(r.lines.size() == 1 && r.lines[0] == "cancel", "Back on the confirm says cancel");
    }
    {
        Rig r;
        r.to_confirm();
        r.act(PadAction::Up);
        r.act(PadAction::Accept);
        check(r.v().surface == PmSurface::Toast && r.v().line1 == "restarting" && r.said("boot android"),
              "BOOT ANDROID shows restarting and says boot android");
        r.act(PadAction::Back);
        r.act(PadAction::Accept);
        r.at(60000);
        check(r.lines.size() == 1 && !r.m.done(), "restarting takes no keys and holds");
    }
    {
        Rig r("state=default\nrecovery=yes\n");
        r.to_sheet();
        r.act(PadAction::Down);
        r.act(PadAction::Accept);
        check(r.lines.empty(), "RECOVERY waits for the sheet close");
        r.at(380);
        check(r.said("boot recovery") && r.v().line1 == "restarting", "RECOVERY says boot recovery after the close");
    }
    {
        Rig r;
        r.to_sheet();
        r.act(PadAction::Accept);
        r.at(380);
        check(r.said("boot reboot"), "REBOOT says boot reboot after the close");
    }
}

UnitProbe probe(const char* load, const char* active, const char* result, int64_t changed_us)
{
    UnitProbe u;
    u.read = true;
    u.load = load;
    u.active = active;
    u.result = result;
    u.changed_us = changed_us;
    return u;
}

std::string why_of(const PowerMenu& m, const UnitProbe& u)
{
    UnitVerdict v = m.check(u);
    return v.failed ? v.why : "";
}

std::string reason_of(const PowerMenu& m, const UnitProbe& u)
{
    UnitVerdict v = m.check(u);
    return v.failed ? v.reason : "";
}

void booted(Rig& r)
{
    r.to_sheet();
    r.act(PadAction::Accept);
    r.at(380);
}

void more_key_cases()
{
    {
        Rig r;
        r.to_confirm();
        r.act(PadAction::Left);
        r.act(PadAction::Right);
        r.act(PadAction::Context);
        check(r.v().surface == PmSurface::Confirm && r.v().focus == 1 && r.lines.empty(),
              "Left, Right and Context on the confirm do nothing");
    }
    {
        Rig r;
        r.to_sheet();
        r.act(PadAction::Down);
        r.act(PadAction::Accept);
        r.at(380);
        r.at(100);
        fx o = r.v().o;
        r.act(PadAction::Back);
        r.at(16);
        check(r.v().surface == PmSurface::Confirm && r.v().o < o && r.lines.empty(),
              "Back during the confirm open reverses it");
        r.at(300);
        check(r.lines.size() == 1 && r.lines[0] == "cancel", "the reversed confirm ends in cancel");
    }
    {
        Rig r;
        booted(r);
        r.act(PadAction::Up);
        r.act(PadAction::Down);
        r.act(PadAction::Down, true);
        check(r.lines.size() == 1 && r.v().line1 == "restarting" && !r.m.done(),
              "Up and Down during restarting do nothing");
    }
    {
        Rig r;
        r.at(400);
        r.act(PadAction::Down);
        r.at(50);
        r.act(PadAction::Up);
        fx half = fx_lerp(tok::type_dialogItem_alphaDim, tok::type_dialogItem_alpha, kOne / 2);
        check(r.v().focus == 0 && r.v().alpha[0] > half && r.v().alpha[1] > tok::type_dialogItem_alphaDim,
              "a quick Down then Up starts each row alpha from where it was");
    }
}

void pick_cases()
{
    {
        Rig r;
        r.to_sheet();
        r.now_gates = parse_gates("state=pending\nreason=Lock Busy\n");
        r.act(PadAction::Accept);
        r.at(380);
        PmView v = r.v();
        check(v.surface == PmSurface::Toast && v.line1 == "reboot failed" && v.line2 == "lock busy" && r.lines.empty(),
              "a gate dropped before the pick: failure toast, no boot line");
        r.at(350 + 3500 + 249);
        check(r.lines.empty(), "failure toast holds 3500, then leaves over 250");
        r.at(1);
        check(r.lines.size() == 1 && r.lines[0] == "failed gate" && r.m.done(), "failed gate after the toast");
    }
    {
        Rig r;
        r.to_confirm();
        r.now_gates = parse_gates("state=default\nandroid=missing\nreason=no image\n");
        r.act(PadAction::Up);
        r.act(PadAction::Accept);
        check(r.v().line1 == "boot android failed" && r.v().line2 == "no image" && !r.said("boot android"),
              "ANDROID with its gate gone: boot android failed");
    }
    {
        Rig r;
        r.to_confirm();
        r.now_gates = parse_gates("state=default\nandroid=bootable\nreturn=host\nreason=synthetic reason\n");
        r.act(PadAction::Up);
        r.act(PadAction::Accept);
        PmView v = r.v();
        check(v.surface == PmSurface::Toast && v.line1 == "boot android failed" && v.line2.empty() &&
                  !r.said("boot android"),
              "a pre-read without return=keys: boot android failed with no second line");
        r.at(350 + 3500 + 250);
        check(r.lines.size() == 1 && r.lines[0] == "failed gate" && r.m.done(), "then failed gate");
    }
    {
        Rig r("state=default\nrecovery=yes\n");
        r.to_sheet();
        r.now_gates = parse_gates("state=default\nrecovery=no\nreason=synthetic reason\n");
        r.act(PadAction::Down);
        r.act(PadAction::Accept);
        r.at(380);
        PmView v = r.v();
        check(v.line1 == "recovery failed" && v.line2.empty(), "a recovery= refusal has no second line");
    }
}

void unit_cases()
{
    Rig r;
    booted(r);
    int64_t b = r.m.boot_at();
    int64_t after = b * 1000 + 10;
    check(r.m.probing() && b > 0, "after boot the menu polls the unit");
    check(why_of(r.m, probe("loaded", "inactive", "success", after)).empty(),
          "inactive with success: the reboot is under way");
    check(why_of(r.m, probe("loaded", "failed", "exit-code", b * 1000 - 5000)).empty(),
          "a failed state from before the boot line keeps polling");
    for (const char* st : {"activating", "active", "deactivating", "reloading"}) {
        std::string what = std::string("a running state keeps polling: ") + st;
        check(why_of(r.m, probe("loaded", st, "success", after)).empty(), what.c_str());
    }
    UnitProbe none = parse_unit_show("");
    check(!none.read && why_of(r.m, none).empty(), "empty output keeps polling");
    UnitProbe blank = parse_unit_show(" \n");
    check(!blank.read && why_of(r.m, blank).empty(), "blank output keeps polling");
    UnitProbe bad = probe("not-found", "inactive", "success", 0);
    bad.read = false;
    check(why_of(r.m, bad).empty(), "a nonzero systemctl show keeps polling");
    check(why_of(r.m, probe("not-found", "inactive", "success", 0)) == "not-found",
          "not-found with timestamp 0 is a failure");
    UnitProbe noload = parse_unit_show("ActiveState=inactive\nResult=success\nStateChangeTimestampMonotonic=0\n");
    check(noload.read && why_of(r.m, noload) == "unknown", "a missing LoadState line reads unknown, a failure");
    check(why_of(r.m, probe("loaded", "inactive", "exit-code", after)) == "inactive exit-code",
          "inactive with a non-success result is a failure");
    check(why_of(r.m, probe("loaded", "failed", "exit-code", after)) == "failed exit-code",
          "failed with exit-code is a failure, the why is the state and the result");
    check(why_of(r.m, probe("loaded", "failed", "success", after)) == "failed success",
          "any state that is not inactive with success is a failure");
    check(why_of(r.m, probe("loaded", "", "", after)) == "unknown ", "an empty state reads unknown");
    check(reason_of(r.m, probe("loaded", "inactive", "exit-code", after)) == "exit-code" &&
              reason_of(r.m, probe("loaded", "failed", "timeout", after)) == "timeout",
          "the toast reason is the unit's Result");
    check(reason_of(r.m, probe("not-found", "inactive", "success", 0)) == "not-found" &&
              reason_of(r.m, noload) == "unknown",
          "the toast reason is the LoadState word for a LoadState failure");
    check(reason_of(r.m, probe("loaded", "failed", "", after)) == "failed", "an empty Result gives the state");
    UnitProbe full = parse_unit_show(
        "LoadState=loaded\nActiveState=failed\nResult=timeout\nStateChangeTimestampMonotonic=99999999999999999\n");
    check(full.read && full.load == "loaded" && full.active == "failed" && full.result == "timeout" &&
              why_of(r.m, full) == "failed timeout",
          "systemctl show output parses to its fields");
    r.at(30000);
    check(!r.m.done() && r.v().line1 == "restarting", "polling holds restarting");
    r.m.fail("exit-code", r.t);
    r.at(200);
    r.at(1);
    PmView v = r.v();
    check(v.line1 == "reboot failed" && v.line2 == "exit-code" && !r.m.probing(),
          "a counted failure shows reboot failed");
    check(why_of(r.m, probe("loaded", "failed", "exit-code", after)).empty(), "no check once the failure is shown");
    r.at(3500 + 250);
    check(r.m.done() && r.lines.size() == 1, "the failure toast ends the child with no line");
}

void load_cases()
{
    for (const char* load : {"not-found", "masked", "bad-setting", "error"}) {
        Rig r;
        booted(r);
        int64_t in = r.m.boot_at();
        std::string what = std::string("LoadState=") + load + ": the failure lands inside the restarting toast in";
        check(in == r.t, what.c_str());
        std::string why = reason_of(r.m, probe(load, "inactive", "success", 0));
        what = std::string("LoadState=") + load + ": counted as a failure with that word";
        check(why == load, what.c_str());
        r.m.fail(why, r.t);
        r.at(201);
        what = std::string("LoadState=") + load + ": reboot failed with that word on line 2";
        check(r.v().line1 == "reboot failed" && r.v().line2 == load && !r.m.probing(), what.c_str());
        int64_t end = in + fx_round(tok::motion_toastIn) + fx_round(tok::motion_toastHold) +
                      fx_round(tok::motion_toastOut);
        r.at(end - 1 - r.t);
        what = std::string("LoadState=") + load + ": the toast still runs 1 ms before its end";
        check(!r.m.done() && r.lines.size() == 1, what.c_str());
        r.at(1);
        what = std::string("LoadState=") + load + ": the child ends at the toast end with no line";
        check(r.m.done() && r.lines.size() == 1, what.c_str());
    }
    {
        Rig r;
        booted(r);
        int64_t b = r.m.boot_at();
        bool polling = true;
        for (int i = 0; i < 4; i++) {
            polling = polling && why_of(r.m, probe("loaded", "inactive", "success", b * 1000 + 10)).empty();
            r.at(PowerMenu::kProbeMs);
        }
        check(polling && r.m.probing() && r.v().line1 == "restarting" && !r.m.done(),
              "loaded, inactive with success keeps polling past 3 probes");
    }
}

void term_cases()
{
    {
        Rig r;
        r.at(400);
        r.m.term(r.t);
        r.at(0);
        check(r.m.done() && r.lines.empty() && r.v().surface == PmSurface::None,
              "SIGTERM over the dialog closes at once with no line");
    }
    {
        Rig r;
        booted(r);
        r.at(1000);
        r.m.term(r.t);
        r.at(0);
        check(r.m.done() && r.lines.size() == 1 && r.lines[0] == "boot reboot",
              "SIGTERM over restarting closes at once with no line");
    }
    for (int after : {100, 1000, 3900}) {
        Rig r;
        r.to_sheet();
        r.now_gates = parse_gates("state=pending\nreason=lock busy\n");
        r.act(PadAction::Accept);
        r.at(380);
        r.at(after);
        fx e0 = r.v().e;
        r.m.term(r.t);
        fx e1 = r.v().e;
        r.at(16);
        fx e2 = r.v().e;
        bool from = std::abs(e1 - e0) <= kOne / 20 && e2 < e1 && !r.m.done();
        r.at(250 - 16);
        std::string what = "SIGTERM " + std::to_string(after) + " ms into a failure toast runs the out from its value";
        check(from, what.c_str());
        what = "SIGTERM " + std::to_string(after) + " ms into a failure toast ends within 250 ms with no line";
        check(r.m.done() && r.lines.empty(), what.c_str());
    }
    {
        Rig r;
        r.to_sheet();
        r.now_gates = parse_gates("state=pending\nreason=lock busy\n");
        r.act(PadAction::Accept);
        r.at(380);
        r.at(500);
        r.m.term(r.t);
        r.m.term(r.t + 100);
        r.at(250);
        check(r.m.done() && r.lines.empty(), "a second SIGTERM does not restart the out");
    }
    {
        std::string ln;
        int64_t at = 0;
        bool closing = false;
        Rig q;
        q.at(400);
        q.m.action(PadAction::Accept, false, q.t);
        bool peeked = q.m.peek_line(ln, at, closing);
        check(peeked && ln == "poweroff" && closing && at == q.t, "a closing line keeps its pick time");
        Rig c;
        c.to_sheet();
        c.m.action(PadAction::Accept, false, c.t);
        c.m.tick(c.t + 380);
        peeked = c.m.peek_line(ln, at, closing);
        check(peeked && ln == "boot reboot" && !closing && at == c.t, "boot is no closing line, anchored at the pick");
    }
    {
        Rig r;
        r.at(400);
        r.m.abort("failed grab", r.t);
        r.at(0);
        check(r.m.done() && r.lines.size() == 1 && r.lines[0] == "failed grab",
              "a grab failure closes with failed grab");
    }
}

struct PauseRig : Rig {
    PauseRig() { m.open_pause(parse_gates(kDefault), t); }
    bool noted(const char* word)
    {
        std::string n;
        return m.take_note(n) && n == word;
    }
};

void pause_cases()
{
    {
        PauseRig r;
        PmView v = r.v();
        check(v.surface == PmSurface::Pause && std::string(v.title) == "paused" && r.items() == "resume,quit" &&
                  v.focus == 0,
              "pause: title paused, rows resume and quit, focus on resume at open");
        r.at(400);
        r.act(PadAction::Accept);
        check(r.m.done() && r.said("resume") && r.v().surface == PmSurface::None,
              "pause: Accept on resume closes at once with resume");
    }
    {
        PauseRig r;
        r.at(400);
        r.act(PadAction::Back);
        r.at(100);
        bool early = !r.said("resume") && r.v().surface == PmSurface::Pause;
        r.at(60);
        check(early && r.said("resume") && r.m.done(), "pause: Back closes over 150 ms, then resume");
    }
    {
        PauseRig r;
        r.at(100);
        fx o0 = r.v().o;
        r.act(PadAction::Back);
        r.at(16);
        fx o1 = r.v().o;
        r.at(400);
        check(o1 < o0 && r.said("resume") && !r.said("cancel"),
              "pause: Back during the open reverses from where it is, then resume");
    }
    {
        PauseRig r;
        r.at(400);
        r.act(PadAction::Down);
        r.at(300);
        r.act(PadAction::Accept);
        check(r.said("quit") && r.m.done(), "pause: Accept on quit says quit and closes at once");
    }
    {
        PauseRig r;
        r.at(400);
        r.act(PadAction::Down);
        r.at(300);
        r.m.power(r.t);
        r.at(100);
        PmView v = r.v();
        bool mid = v.surface == PmSurface::Power && v.other.items.size() == 2 &&
                   std::string(v.other.title) == "paused" && v.mix > kOne / 4 && v.mix < kOne * 3 / 4 && v.o == kOne &&
                   v.s == kOne && r.m.power_drawn(r.t);
        r.at(100);
        v = r.v();
        bool rest = v.surface == PmSurface::Power && v.other.items.empty() && r.items() == "power off,reboot" &&
                    v.focus == 0;
        check(mid && rest,
              "pause: power crosses to the power dialog over 200 ms, scrim held, no scale, the pause body fading");
        r.act(PadAction::Back);
        bool back = r.noted("cancel") && r.v().surface == PmSurface::Pause && r.v().focus == 1;
        r.at(100);
        v = r.v();
        bool midb = v.other.items.size() == 2 && std::string(v.other.title) == "power" && v.mix > kOne / 4 &&
                    v.mix < kOne * 3 / 4;
        r.at(120);
        check(back && midb && r.v().other.items.empty() && !r.m.done() && r.lines.empty(),
              "pause: Back on the power dialog crosses back to the pause, focus kept, a cancel note and no result");
    }
    {
        PauseRig r;
        r.at(400);
        r.m.power(r.t);
        r.at(100);
        fx pause_o = r.v().mix;
        r.act(PadAction::Back);
        PmView v = r.v();
        bool from = v.surface == PmSurface::Pause && std::abs(v.mix - (kOne - pause_o)) <= kOne / 50;
        r.at(50);
        bool going = r.v().mix < kOne - pause_o;
        r.at(60);
        check(from && going && r.v().other.items.empty() && r.noted("cancel"),
              "pause: Back during the cross reverses it from where it is");
    }
    {
        PauseRig r;
        r.at(400);
        r.m.power(r.t);
        r.at(50);
        r.act(PadAction::Down);
        check(r.v().surface == PmSurface::Power && r.v().other.items.empty() && r.v().focus == 1,
              "pause: Down during the cross lands it, then moves (rule 4, as the power dialog's open)");
    }
    {
        PauseRig r;
        r.at(400);
        r.act(PadAction::Back);
        r.m.power(r.t);
        r.at(0);
        check(r.said("resume") && r.m.done(), "pause: power during the Back close lands it, resume");
    }
    {
        PauseRig r;
        r.at(400);
        r.m.power(r.t);
        r.at(300);
        r.act(PadAction::Down);
        r.at(300);
        r.act(PadAction::Back);
        r.at(50);
        r.m.power(r.t);
        PmView v = r.v();
        bool fresh = v.surface == PmSurface::Power && v.mix == kOne && v.focus == 0 && v.other.items.size() == 2;
        r.at(100);
        bool mid = r.v().mix > kOne / 4 && r.v().mix < kOne * 3 / 4;
        r.at(110);
        bool rest = r.v().surface == PmSurface::Power && r.v().other.items.empty() && r.v().focus == 0;
        check(fresh && mid && rest,
              "pause: power during the cross back lands it, then a fresh 200 ms cross opening on power off");
    }
    {
        PauseRig r;
        r.at(400);
        r.m.power(r.t);
        r.at(300);
        r.act(PadAction::Back);
        r.at(50);
        r.act(PadAction::Back);
        bool landed = r.v().surface == PmSurface::Pause && r.v().other.items.empty() && !r.m.done();
        r.at(160);
        check(landed && r.said("resume"),
              "pause: Back during the cross back lands it, then closes the pause with resume");
    }
    {
        PauseRig r;
        r.at(400);
        r.m.power(r.t);
        r.at(300);
        r.act(PadAction::Down);
        r.at(300);
        r.act(PadAction::Accept);
        r.at(400);
        r.now_gates = parse_gates("state=pending\nreason=io\n");
        r.act(PadAction::Accept);
        r.at(400);
        PmView v = r.v();
        check(v.surface == PmSurface::Toast && v.under && std::string(v.other.title) == "paused",
              "pause: the failed gate toast from the pause draws over the pause");
        bool never_empty = true;
        for (int k = 0; k < 400 && r.v().surface == PmSurface::Toast; k++) {
            r.at(16);
            never_empty = never_empty && r.v().surface != PmSurface::None;
        }
        check(r.v().surface == PmSurface::Pause && never_empty && r.noted("failed gate") && r.lines.empty() &&
                  !r.m.done(),
              "pause: the failed gate toast ends on the pause at rest: a failed gate note, no result, no empty frame");
    }
    {
        PauseRig r;
        r.at(400);
        r.m.power(r.t);
        r.at(300);
        r.act(PadAction::Down);
        r.at(300);
        r.act(PadAction::Accept);
        r.at(400);
        r.act(PadAction::Accept);
        r.at(400);
        bool boot = r.said("boot reboot") && r.v().surface == PmSurface::Toast && r.v().under;
        r.m.fail("exit-code", r.t);
        bool never_empty = true;
        for (int k = 0; k < 400 && r.v().surface == PmSurface::Toast; k++) {
            r.at(16);
            never_empty = never_empty && r.v().surface != PmSurface::None;
        }
        check(boot && r.v().surface == PmSurface::Pause && never_empty && r.noted("failed boot") && !r.m.done(),
              "pause: a boot that ran and failed shows its toast over the pause, then the pause, failed boot noted");
    }
    {
        PauseRig r;
        r.at(400);
        r.m.power(r.t);
        r.at(300);
        r.act(PadAction::Down);
        r.at(300);
        r.act(PadAction::Accept);
        r.at(100);
        r.act(PadAction::Back);
        r.at(30);
        r.act(PadAction::Accept);
        r.at(400);
        check(r.v().surface == PmSurface::Pause && r.v().focus == 0 && !r.m.done() && r.lines.empty(),
              "pause: Accept during the sheet's reversal lands it on the pause and is dropped, no quit or resume");
    }
    {
        PauseRig r;
        r.at(400);
        r.m.power(r.t);
        r.at(300);
        r.act(PadAction::Down);
        r.at(300);
        r.act(PadAction::Accept);
        r.at(400);
        r.act(PadAction::Back);
        r.at(100);
        r.act(PadAction::Down);
        check(r.v().surface == PmSurface::Pause && r.v().focus == 0 && !r.m.done(),
              "pause: Down during the sheet's close lands it on the pause and is dropped");
    }
    {
        PauseRig r;
        r.at(400);
        r.m.power(r.t);
        r.at(300);
        r.act(PadAction::Down);
        r.at(300);
        r.act(PadAction::Accept);
        PmView v = r.v();
        bool under = v.surface == PmSurface::Sheet && v.under && std::string(v.other.title) == "paused";
        r.at(400);
        r.act(PadAction::Back);
        r.at(400);
        check(under && r.v().surface == PmSurface::Pause && !r.v().under && r.noted("cancel") && !r.m.done(),
              "pause: the reboot sheet rises over the pause and its Back ends on the pause");
    }
    {
        PauseRig r;
        r.at(400);
        r.m.power(r.t);
        r.at(300);
        r.to_confirm();
        bool confirm = r.v().surface == PmSurface::Confirm && r.v().under;
        r.act(PadAction::Accept);
        check(confirm && r.v().surface == PmSurface::Pause && !r.m.done() && r.lines.empty(),
              "pause: the confirm's cancel ends on the pause, uncovered, with no result");
    }
    {
        PauseRig r;
        r.at(29999);
        bool before = !r.m.done();
        r.at(1);
        check(before && r.said("screen-off"), "pause: 30 s with no input gives screen-off");
    }
    {
        Rig r;
        r.m.power(r.t);
        check(r.v().surface == PmSurface::Power && r.v().other.items.empty() && !r.m.power_drawn(r.t),
              "power mode: power does nothing");
    }
}

void idle_cases()
{
    {
        Rig r;
        r.at(29999);
        check(r.lines.empty(), "awake: nothing before 30 s");
        r.at(1);
        check(r.lines.size() == 1 && r.lines[0] == "screen-off", "awake: screen-off at 30 s with no input");
    }
    {
        Rig r(kDefault, true);
        r.at(9000);
        r.act(PadAction::Down);
        r.at(1000);
        check(r.lines.empty(), "dark: input resets the timer");
        r.at(8999);
        check(r.lines.empty(), "dark: nothing before 10 s from the last input");
        r.at(1);
        check(r.lines.size() == 1 && r.lines[0] == "screen-off", "dark: screen-off 10 s after the last input");
    }
    {
        Rig r;
        r.to_sheet();
        r.act(PadAction::Accept);
        r.at(380);
        r.at(120000);
        check(!r.said("screen-off") && !r.m.done(), "no idle timeout after boot");
    }
}

uint8_t a_of(uint32_t p) { return (uint8_t)(p >> 24); }
int ch(uint32_t p, int s) { return (int)((p >> s) & 0xFF); }

bool premultiplied(const MemPresenter& mp)
{
    for (uint32_t p : mp.argb())
        if (ch(p, 16) > a_of(p) || ch(p, 8) > a_of(p) || ch(p, 0) > a_of(p))
            return false;
    return true;
}

bool near_a(uint8_t a, double want) { return std::abs((double)a - want * 255.0) <= 1.5; }

void alpha_cases(Fonts& f)
{
    InkLayer ink;
    MemPresenter mp;
    std::vector<AlphaRegion> regions;
    auto shot = [&](const PmView& v) {
        Canvas c(mp.frame());
        draw_powermenu(c, f, ink, v, theme(), regions);
        mp.present(regions);
    };
    {
        Rig r;
        shot(r.v());
        check(!regions_visible(regions), "the first open frame carries no visible region");
        r.at(60);
        shot(r.v());
        check(regions_visible(regions) && near_a(a_of(mp.argb_at(4, 4)), 0.96 * r.v().o / 65536.0),
              "dialog scrim alpha follows the open");
        r.at(400);
        shot(r.v());
        uint32_t px = mp.argb_at(4, 4);
        check(a_of(px) == 245 && ch(px, 16) == 16 && ch(px, 8) == 16 && ch(px, 0) == 16,
              "dialog scrim 0.96, #111111 premultiplied");
        bool ink_ok = true, ink_seen = false;
        for (int y = 120; y < 170; y++)
            for (int x = 32; x < 300; x++) {
                uint32_t q = mp.argb_at(x, y);
                if (ch(q, 16) > 40) {
                    ink_seen = true;
                    ink_ok = ink_ok && a_of(q) == 245;
                }
            }
        check(ink_seen && ink_ok, "dialog ink takes the scrim alpha");
        check(premultiplied(mp), "dialog frame RGB premultiplied");
        r.act(PadAction::Back);
        r.at(50);
        shot(r.v());
        check(a_of(mp.argb_at(4, 4)) < 245 && a_of(mp.argb_at(4, 4)) > 0, "dialog close scales the scrim alpha");
    }
    {
        Rig r;
        r.at(400);
        r.act(PadAction::Down);
        r.act(PadAction::Accept);
        r.at(100);
        shot(r.v());
        fx p = r.v().sheet.progress;
        check(near_a(a_of(mp.argb_at(4, 4)), 0.55 * p / 65536.0) && near_a(a_of(mp.argb_at(4, 719)), p / 65536.0),
              "sheet scrim and panel alpha follow the rise");
        r.at(400);
        shot(r.v());
        uint32_t s = mp.argb_at(4, 4), pnl = mp.argb_at(950, 715);
        check(a_of(s) == 140 && ch(s, 16) == 9, "sheet scrim 0.55, #111111 premultiplied");
        check(a_of(pnl) == 255 && ch(pnl, 16) == 0x18, "sheet panel 1.0, bgElevated");
        check(premultiplied(mp), "sheet frame RGB premultiplied");
        r.act(PadAction::Accept);
        r.at(380);
        r.at(350);
        shot(r.v());
        uint32_t t = mp.argb_at(950, 4), out = mp.argb_at(4, 400);
        check(a_of(t) == 204 && ch(t, 16) == 0, "toast strip 0.8 black");
        check(out == 0, "outside the regions alpha 0");
        check(premultiplied(mp), "toast frame RGB premultiplied");
    }
    {
        Rig r;
        r.at(400);
        r.act(PadAction::Down);
        r.act(PadAction::Accept);
        r.at(400);
        r.act(PadAction::Accept);
        r.at(380);
        r.at(100);
        shot(r.v());
        int y = -1;
        for (int k = 0; k < 80 && y < 0; k++)
            if (a_of(mp.argb_at(950, k)) == 0)
                y = k;
        check(y > 0 && y < 80 && near_a(a_of(mp.argb_at(950, 0)), 0.8 * r.v().e / 65536.0),
              "toast in moves the strip and scales its alpha");
    }
    {
        Rig r;
        r.to_sheet();
        r.act(PadAction::Back);
        r.at(100);
        shot(r.v());
        fx p = r.v().sheet.progress;
        check(p > 0 && p < kOne && near_a(a_of(mp.argb_at(4, 4)), 0.55 * p / 65536.0) &&
                  near_a(a_of(mp.argb_at(4, 719)), p / 65536.0),
              "sheet close scales the scrim and panel alpha");
    }
    {
        Rig r;
        r.to_sheet();
        r.now_gates = parse_gates("state=pending\nreason=lock busy\n");
        r.act(PadAction::Accept);
        r.at(380);
        r.at(350 + 3500 + 100);
        shot(r.v());
        fx e = r.v().e;
        check(e > 0 && e < kOne && near_a(a_of(mp.argb_at(950, 0)), 0.8 * e / 65536.0),
              "toast out scales the strip alpha");
        check(premultiplied(mp), "toast out frame RGB premultiplied");
    }
    set_theme_light(true);
    {
        Rig r;
        r.at(400);
        shot(r.v());
        uint32_t px = mp.argb_at(4, 4);
        check(a_of(px) == 245 && ch(px, 16) == 245, "light dialog scrim #FFFFFF at 0.96");
        r.act(PadAction::Down);
        r.act(PadAction::Accept);
        r.at(400);
        shot(r.v());
        uint32_t s = mp.argb_at(4, 4), pnl = mp.argb_at(950, 715);
        check(a_of(s) == 140 && ch(s, 16) == 140 && a_of(pnl) == 255 && ch(pnl, 16) == 0xF8,
              "light sheet scrim #FFFFFF at 0.55, panel #F8F8F8");
        r.act(PadAction::Accept);
        r.at(380);
        r.at(350);
        shot(r.v());
        uint32_t t = mp.argb_at(950, 4);
        check(a_of(t) == 204 && ch(t, 16) == 204, "light toast strip #FFFFFF at 0.8");
    }
    set_theme_light(false);
}

void cross_cases(Fonts& f)
{
    set_theme_light(false);
    InkLayer ink;
    Buffer b;
    std::vector<AlphaRegion> regions;
    auto shot = [&](const PmView& v, std::vector<uint8_t>& out) {
        Canvas c(b.surface());
        draw_powermenu(c, f, ink, v, theme(), regions);
        Surface sf = b.surface();
        out.resize((size_t)sf.w * sf.h);
        for (int y = 0; y < sf.h; y++)
            for (int x = 0; x < sf.w; x++)
                out[(size_t)y * sf.w + x] = (uint8_t)((LiveF::unpack(sf.at(x, y)) >> 8) & 0xFF);
    };
    std::vector<uint8_t> a, m, z;
    int rest = -1;
    {
        PauseRig e;
        e.at(400);
        shot(e.v(), a);
        rest = regions.empty() ? -1 : regions[0].a256;
        e.m.power(e.t);
        e.at(300);
        shot(e.v(), z);
    }
    bool held = true, between = true;
    int shared = 0, full = 0, frames = 0;
    auto frame = [&](const PmView& v) {
        shot(v, m);
        frames++;
        held = held && regions.size() == 1 && regions[0].a256 == rest;
        for (size_t i = 0; i < a.size(); i++) {
            between = between && m[i] + 1 >= std::min(a[i], z[i]);
            if (a[i] == 255 && z[i] == 255) {
                shared++;
                full += m[i] == 255;
            }
        }
    };
    PauseRig r;
    r.at(400);
    r.m.power(r.t);
    for (int k = 1; k * 16 < 200; k++) {
        r.at(16);
        frame(r.v());
    }
    r.at(300);
    r.act(PadAction::Back);
    for (int k = 1; k * 16 < 200; k++) {
        r.at(16);
        frame(r.v());
    }
    check(frames == 24 && held, "the cross: the scrim holds at the rest alpha in every frame, both ways");
    check(between, "the cross: every pixel of every 16 ms frame, both ways, holds at least the ink of the nearer end");
    check(shared > 0 && full == shared,
          "the cross: pixels full in both bodies (the shared p, the bar) stay full in every frame");
    r.at(300);
    r.m.power(r.t);
    r.at(300);
    r.act(PadAction::Down);
    r.at(300);
    r.act(PadAction::Accept);
    r.at(400);
    std::vector<uint8_t> sh;
    shot(r.v(), sh);
    bool stacked = regions.size() >= 2 && regions[0].a256 == rest;
    for (const AlphaRegion& g : regions)
        stacked = stacked && g.a256 >= rest;
    check(stacked, "the reboot sheet over the pause: the pause's scrim first, no region below it");
    auto alpha_at = [](const std::vector<AlphaRegion>& rg, int x, int y) {
        for (size_t i = rg.size(); i-- > 0;)
            if (x >= rg[i].r.x0 && x < rg[i].r.x1 && y >= rg[i].r.y0 && y < rg[i].r.y1)
                return std::clamp(rg[i].a256, 0, 256);
        return 0;
    };
    auto over_ok = [&](const PmView& v) {
        Buffer cb, ub, tb;
        std::vector<AlphaRegion> cr, ur, tr;
        PmView top = v;
        top.under = false;
        top.other = PmBody{};
        PmView u;
        u.surface = PmSurface::Pause;
        u.title = v.other.title;
        u.items = v.other.items;
        u.alpha = v.other.alpha;
        u.bar = v.other.bar;
        u.o = kOne;
        Canvas cc(cb.surface()), uc(ub.surface()), tc(tb.surface());
        draw_powermenu(cc, f, ink, v, theme(), cr);
        draw_powermenu(uc, f, ink, u, theme(), ur);
        draw_powermenu(tc, f, ink, top, theme(), tr);
        Surface cs = cb.surface(), us = ub.surface(), ts = tb.surface();
        int worst = 0;
        for (int y = 0; y < cs.h; y++)
            for (int x = 0; x < cs.w; x++) {
                double au = alpha_at(ur, x, y) / 256.0, at = alpha_at(tr, x, y) / 256.0;
                double all = at + au * (1 - at);
                worst = std::max(worst, std::abs((int)region_a8(alpha_at(cr, x, y)) - (int)std::lround(all * 255)));
                if (all <= 0)
                    continue;
                uint32_t pc = LiveF::unpack(cs.at(x, y)), pu = LiveF::unpack(us.at(x, y));
                uint32_t pt = LiveF::unpack(ts.at(x, y));
                for (int sh = 0; sh <= 16; sh += 8) {
                    double want = (((pt >> sh) & 0xFF) * at + ((pu >> sh) & 0xFF) * au * (1 - at)) / all;
                    worst = std::max(worst, std::abs((int)((pc >> sh) & 0xFF) - (int)std::lround(want)));
                }
            }
        return worst <= 1;
    };
    {
        PauseRig q;
        q.at(400);
        q.m.power(q.t);
        q.at(300);
        q.act(PadAction::Down);
        q.at(300);
        q.act(PadAction::Accept);
        q.at(40);
        PmView sv = q.v();
        bool sheet = sv.surface == PmSurface::Sheet && sv.under && sv.sheet.progress > 0 && sv.sheet.progress < kOne;
        check(sheet && over_ok(sv),
              "the composite: the reboot sheet at mid rise over the pause equals a straight alpha over within 1");
        q.at(400);
        q.act(PadAction::Down);
        q.act(PadAction::Accept);
        q.at(425);
        PmView cv = q.v();
        bool confirm = cv.surface == PmSurface::Confirm && cv.under && cv.o > 0 && cv.o < kOne;
        check(confirm && over_ok(cv),
              "the composite: the confirm at mid open over the pause equals a straight alpha over within 1");
    }
    {
        PauseRig g;
        g.at(400);
        g.m.power(g.t);
        g.at(300);
        g.act(PadAction::Down);
        g.at(300);
        g.act(PadAction::Accept);
        g.at(400);
        g.now_gates = parse_gates("state=pending\nreason=io\n");
        g.act(PadAction::Accept);
        g.at(400);
        std::vector<uint8_t> last, first;
        std::vector<AlphaRegion> last_r, first_r;
        bool toast = g.v().surface == PmSurface::Toast;
        for (int k = 0; k < 400 && g.v().surface == PmSurface::Toast; k++) {
            shot(g.v(), last);
            last_r = regions;
            g.at(16);
        }
        shot(g.v(), first);
        first_r = regions;
        bool paused = g.v().surface == PmSurface::Pause && !last_r.empty() && !first_r.empty();
        Rect strip = last_r.empty() ? Rect{} : last_r.back().r;
        bool covered = true, same = true;
        Surface sf = b.surface();
        for (int y = 0; y < sf.h; y++)
            for (int x = 0; x < sf.w; x++) {
                covered = covered && alpha_at(last_r, x, y) >= rest && alpha_at(first_r, x, y) >= rest;
                bool in = x >= strip.x0 && x < strip.x1 && y >= strip.y0 && y < strip.y1;
                size_t i = (size_t)y * sf.w + x;
                same = same && (in || std::abs((int)last[i] - (int)first[i]) <= 1);
            }
        check(toast && paused && covered && same,
              "the failed gate seam: the last toast frame and the first pause frame both hold the pause's scrim, "
              "equal outside the strip");
    }
}

void region_cap_cases(Fonts& f)
{
    Rig r;
    InkLayer ink;
    Buffer buf;
    std::vector<AlphaRegion> regions;
    size_t most = 0;
    std::vector<PmSurface> seen;
    auto walk = [&](int ms) {
        for (int e = 0; e < ms; e += 16) {
            r.at(16);
            Canvas c(buf.surface());
            draw_powermenu(c, f, ink, r.v(), theme(), regions);
            most = std::max(most, regions.size());
            if (seen.empty() || seen.back() != r.v().surface)
                seen.push_back(r.v().surface);
        }
    };
    walk(400);
    r.act(PadAction::Down);
    walk(200);
    r.act(PadAction::Accept);
    walk(500);
    r.act(PadAction::Down);
    r.act(PadAction::Accept);
    walk(800);
    r.act(PadAction::Up);
    r.act(PadAction::Accept);
    walk(600);
    r.m.fail("exit-code", r.t);
    walk(4500);
    bool all = seen.size() == 5 && seen[0] == PmSurface::Power && seen[1] == PmSurface::Sheet &&
               seen[2] == PmSurface::Confirm && seen[3] == PmSurface::Toast && seen[4] == PmSurface::None;
    check(all && most > 0 && most <= 16, "every phase draws at most 16 regions");
}

struct FakeDev {
    int rfd = -1, wfd = -1;
    DevInfo info;
    std::vector<int> grabs;
    int keys = 0;
    int hat = 0;
    std::string real;
};

void push(int fd, uint16_t type, uint16_t code, int32_t value)
{
    input_event ev = {};
    ev.type = type;
    ev.code = code;
    ev.value = value;
    if (write(fd, &ev, sizeof ev) != (ssize_t)sizeof ev)
        std::printf("FAIL could not write event\n");
}

void grab_cases()
{
    std::map<std::string, FakeDev> devs;
    std::vector<std::string> paths;
    auto add = [&](const std::string& path, const std::string& name, bool joy, bool touch, std::vector<int> grabs) {
        FakeDev d;
        int p[2];
        if (pipe(p) != 0)
            return;
        fcntl(p[0], F_SETFL, O_NONBLOCK);
        d.rfd = p[0];
        d.wfd = p[1];
        d.info.id.name = name;
        d.info.joy = joy;
        d.info.touch = touch;
        d.grabs = std::move(grabs);
        devs[path] = d;
        paths.push_back(path);
    };
    std::map<int, std::string> by_fd;
    std::map<std::string, int> opens;
    EvdevOps ops;
    ops.list = [&]() { return paths; };
    ops.open = [&](const std::string& path) {
        int fd = dup(devs[path].rfd);
        by_fd[fd] = path;
        opens[path]++;
        return fd;
    };
    ops.info = [&](int fd, DevInfo& d) {
        d = devs[by_fd[fd]].info;
        return true;
    };
    ops.grab = [&](int fd) {
        FakeDev& d = devs[by_fd[fd]];
        if (d.grabs.empty())
            return 0;
        int rc = d.grabs.front();
        d.grabs.erase(d.grabs.begin());
        return rc;
    };
    ops.keys_down = [&](int fd) {
        FakeDev& d = devs[by_fd[fd]];
        return input_held(
            [&d](unsigned long* bits, size_t) {
                bits[0] = d.keys > 0 ? 1UL : 0UL;
                return true;
            },
            [&d](int code, int& value) {
                if (code != ABS_HAT0Y)
                    return false;
                value = d.hat;
                return true;
            });
    };
    ops.real = [&](const std::string& path) { return devs[path].real; };

    add("/dev/input/event0", "mtk-pmic-keys", false, false, {});
    add("/dev/input/event1", "odm:gpio_keys", false, false, {});
    add("/dev/input/event2", "AYANEO Controller", true, false, {EBUSY, EBUSY, 0});
    add("/dev/input/event3", "hyn_ts", false, true, {});
    add("/dev/input/event4", "mtk-kpd", false, false, {});
    add("/dev/input/event7", "gpio-keys-extra", false, false, {});
    devs["/dev/input/event7"].real = "/sys/devices/virtual/input/input7/event7";
    {
        MenuGrab g(ops);
        g.start(0);
        check(g.held() == 1 && g.busy() == 1, "key nodes skipped, touch grabbed, a busy pad waits");
        check(g.tick(249) && g.busy() == 1, "no retry before 250 ms");
        check(g.tick(250) && g.busy() == 1, "first retry at 250 ms still busy");
        check(g.tick(500) && g.held() == 2 && g.busy() == 0, "the pad grabbed on the next retry");
        add("/dev/input/event5", "Pad BT", true, false, {});
        g.tick(1999);
        check(g.held() == 2, "no rescan before 2 s");
        g.tick(2000);
        check(g.held() == 3, "the rescan at 2 s grabs a new pad node");
        push(devs["/dev/input/event2"].wfd, EV_KEY, BTN_EAST, 1);
        push(devs["/dev/input/event2"].wfd, EV_SYN, SYN_REPORT, 0);
        std::vector<PadEdge> e;
        g.read(e);
        check(e.size() == 1 && e[0].action == PadAction::Accept && e[0].down, "the grabbed pad reads Accept");
        push(devs["/dev/input/event2"].wfd, EV_KEY, BTN_SOUTH, 0);
        push(devs["/dev/input/event2"].wfd, EV_KEY, BTN_EAST, 2);
        push(devs["/dev/input/event2"].wfd, EV_SYN, SYN_REPORT, 0);
        e.clear();
        g.read(e);
        bool acted = false;
        for (const PadEdge& x : e)
            acted = acted || x.down;
        check(!acted, "a key down before the grab acts on neither its release nor its autorepeat");
        add("/dev/input/event6", "Pad two", true, false, {EBUSY});
        g.tick(4000);
        check(g.held() == 3 && g.busy() == 0, "a node busy at a rescan is skipped, not waited on");
        check(g.tick(6000) && g.held() == 4, "the next rescan grabs it once free");
        check(opens["/dev/input/event0"] == 1 && opens["/dev/input/event4"] == 1,
              "nodes that are no pad or touch are opened once, not at every rescan");
        devs["/dev/input/event7"].info.joy = true;
        devs["/dev/input/event7"].real = "/sys/devices/virtual/input/input9/event7";
        check(g.tick(8000) && g.held() == 5 && opens["/dev/input/event7"] == 2,
              "a pad reusing an ignored event path under a new sysfs node is grabbed at the next rescan");
        devs["/dev/input/event2"].keys = 1;
        g.begin_release(7000);
        check(g.releasing(7000) && g.held() == 1, "release keeps the node whose key is still down");
        check(g.releasing(7500) && g.held() == 1, "and keeps it while the key stays down");
        devs["/dev/input/event2"].keys = 0;
        check(!g.releasing(7600) && g.held() == 0, "released once no key is down");
    }
    {
        devs["/dev/input/event2"].grabs = {EBUSY, EBUSY, EBUSY, EBUSY, EBUSY, EBUSY};
        devs["/dev/input/event5"].keys = 1;
        MenuGrab g(ops);
        g.start(0);
        bool ok = g.tick(250) && g.tick(500) && g.tick(750);
        check(ok && !g.tick(1000), "a pad busy for 1000 ms fails the grab");
        check(g.busy() == 0 && g.tick(1250), "the grab fails once, the busy node is let go");
        g.begin_release(1000);
        g.releasing(1000);
        check(g.held() > 0, "release waits for keys up");
        check(!g.releasing(2000) && g.held() == 0, "release gives up after 1000 ms");
    }
    for (auto& kv : devs) {
        kv.second.grabs.clear();
        kv.second.keys = 0;
    }
    {
        devs["/dev/input/event2"].keys = 1;
        MenuGrab g(ops, true);
        g.start(0);
        int others = g.held();
        check(g.waiting() == 1 && g.deadline() == 1000,
              "pause mode: a pad with a key down waits, the other pads and touch are grabbed at start");
        push(devs["/dev/input/event2"].wfd, EV_KEY, BTN_EAST, 0);
        push(devs["/dev/input/event2"].wfd, EV_SYN, SYN_REPORT, 0);
        std::vector<PadEdge> e;
        g.read(e);
        check(e.empty() && g.waiting() == 1, "pause mode: the waiting node's events are read and act on nothing");
        g.tick(300);
        check(g.waiting() == 1, "pause mode: no grab while EVIOCGKEY still shows a key down");
        push(devs["/dev/input/event2"].wfd, EV_KEY, BTN_SOUTH, 1);
        push(devs["/dev/input/event2"].wfd, EV_SYN, SYN_REPORT, 0);
        devs["/dev/input/event2"].keys = 0;
        g.tick(400);
        check(g.waiting() == 0 && g.held() == others + 1, "pause mode: grabbed at the first tick after the release");
        push(devs["/dev/input/event2"].wfd, EV_KEY, BTN_EAST, 1);
        push(devs["/dev/input/event2"].wfd, EV_SYN, SYN_REPORT, 0);
        e.clear();
        g.read(e);
        check(e.size() == 1 && e[0].action == PadAction::Accept && e[0].down,
              "pause mode: a press queued before the grab is dropped, a fresh press after it reads");
    }
    {
        devs["/dev/input/event2"].keys = 1;
        MenuGrab g(ops, true);
        g.start(0);
        check(g.tick(999) && g.waiting() == 1, "pause mode: still waiting at 999 ms with the key down");
        int before = g.held();
        check(g.tick(1000) && g.waiting() == 0 && g.held() == before + 1, "pause mode: grabbed anyway at 1000 ms");
        g.begin_release(1000);
        devs["/dev/input/event2"].keys = 0;
        check(!g.releasing(1000) && g.held() == 0, "pause mode: the release wait as in power mode");
    }
    {
        devs["/dev/input/event2"].keys = 1;
        MenuGrab g(ops);
        g.start(0);
        check(g.waiting() == 0 && g.held() == 5, "power mode: a pad with a key down is grabbed at start as built");
        devs["/dev/input/event2"].keys = 0;
    }
    {
        devs["/dev/input/event2"].hat = 1;
        MenuGrab g(ops, true);
        g.start(0);
        bool waits = g.waiting() == 1;
        g.tick(300);
        waits = waits && g.waiting() == 1;
        devs["/dev/input/event2"].hat = 0;
        g.tick(400);
        check(waits && g.waiting() == 0, "pause mode: a held d-pad (ABS_HAT0Y) waits for its release");
    }
    {
        MenuGrab g(ops);
        g.start(0);
        int all = g.held();
        devs["/dev/input/event2"].hat = 1;
        g.begin_release(100);
        bool holds = g.releasing(100) && g.held() == 1 && all > 1;
        check(holds && g.releasing(600) && g.held() == 1, "release: a held d-pad (ABS_HAT0Y) keeps its node grabbed");
        devs["/dev/input/event2"].hat = 0;
        check(!g.releasing(700) && g.held() == 0, "release: the node drops once the hat is back at 0");
    }
    {
        MenuGrab g(ops, true);
        g.start(0);
        int before = g.held();
        add("/dev/input/event8", "Pad three", true, false, {EBUSY, EBUSY, EBUSY, EBUSY, EBUSY, EBUSY});
        devs["/dev/input/event8"].keys = 1;
        g.tick(2000);
        bool waits = g.waiting() == 1;
        devs["/dev/input/event8"].keys = 0;
        bool ok = g.tick(2100);
        check(waits && ok && g.waiting() == 0 && g.busy() == 0 && g.held() == before,
              "pause mode: a node found at a rescan waits for its key, then EBUSY skips it as a rescan does");
        check(g.tick(2350) && g.tick(2600) && g.tick(3200), "pause mode: the skipped node fails no grab");
    }
    {
        MenuGrab g(ops, true);
        g.start(0);
        push(devs["/dev/input/event2"].wfd, EV_KEY, BTN_MODE, 1);
        push(devs["/dev/input/event2"].wfd, EV_SYN, SYN_REPORT, 0);
        std::vector<PadEdge> e;
        std::vector<bool> modes;
        g.read(e, &modes);
        bool down = e.empty() && modes.size() == 1 && modes[0];
        push(devs["/dev/input/event2"].wfd, EV_KEY, BTN_MODE, 0);
        push(devs["/dev/input/event2"].wfd, EV_SYN, SYN_REPORT, 0);
        modes.clear();
        g.read(e, &modes);
        bool up = e.empty() && modes.size() == 1 && !modes[0];
        push(devs["/dev/input/event2"].wfd, EV_KEY, BTN_MODE, 1);
        push(devs["/dev/input/event2"].wfd, EV_SYN, SYN_REPORT, 0);
        g.read(e);
        check(down && up && e.empty(),
              "BTN_MODE on a grabbed pad comes out as a mode edge only when asked, never as a pad action");
    }
    {
        std::map<int, int> abs;
        auto held = [&abs](unsigned long first) {
            return input_held(
                [first](unsigned long* bits, size_t) {
                    bits[0] = first;
                    return true;
                },
                [&abs](int code, int& value) {
                    auto it = abs.find(code);
                    if (it == abs.end())
                        return false;
                    value = it->second;
                    return true;
                });
        };
        bool none = !held(0);
        abs = {{ABS_HAT0X, 0}, {ABS_HAT0Y, 0}, {ABS_GAS, 0}, {ABS_BRAKE, 0}};
        bool rest = !held(0);
        bool key = held(1);
        abs[ABS_HAT0X] = -1;
        bool left = held(0);
        abs[ABS_HAT0X] = 0;
        abs[ABS_BRAKE] = 40;
        bool trigger = held(0);
        check(none && rest && key && left && trigger,
              "input_held: a key bit, a hat off zero or a trigger off zero is held; no axes or all at zero is not");
    }
    for (auto& kv : devs) {
        close(kv.second.rfd);
        close(kv.second.wfd);
    }
}

int64_t steady_ms()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void spawn_cases()
{
    std::fflush(stdout);
    int p[2];
    if (pipe(p) != 0)
        return;
    fcntl(p[0], F_SETFL, O_NONBLOCK);
    int saved = dup(1);
    dup2(p[1], 1);
    Spawned s = spawn_capture({"sh", "-c", "echo out"});
    std::string got;
    if (s.fd >= 0) {
        char buf[64];
        for (;;) {
            pollfd pf{s.fd, POLLIN, 0};
            poll(&pf, 1, 2000);
            ssize_t r = read(s.fd, buf, sizeof buf);
            if (r <= 0)
                break;
            got.append(buf, (size_t)r);
        }
        close(s.fd);
        waitpid(s.pid, nullptr, 0);
    }
    char c;
    ssize_t leaked = read(p[0], &c, 1);
    int leak_errno = errno;
    dup2(saved, 1);
    close(saved);
    close(p[0]);
    close(p[1]);
    check(got == "out\n", "a subprocess writes to its capture pipe");
    Spawned t = spawn_capture({"true"});
    if (t.fd >= 0)
        close(t.fd);
    int st = 0;
    waitpid(t.pid, &st, 0);
    int64_t t0 = steady_ms();
    bool reaped = reap(t.pid, st);
    check(t.pid > 0 && !reaped && steady_ms() - t0 < 50, "reap stops at once on ECHILD, no kill");
    check(leaked < 0 && leak_errno == EAGAIN, "a subprocess never writes to the result fd");
}

struct Present {
    unsigned seq = 0;
    size_t n = 0;
    int visible = 0, most = 0;
};

bool as_present(const std::string& s, Present& p)
{
    return std::sscanf(s.c_str(), "present %u %zu %d %d", &p.seq, &p.n, &p.visible, &p.most) == 4;
}

bool meta(const std::string& s) { return s.rfind("present ", 0) == 0 || s.rfind("note ", 0) == 0; }

struct Loop {
    pid_t pid = -1;
    int out = -1;
    std::string buf;
    bool ended = false;
    int64_t ended_at = -1;
    std::vector<std::string> log;
    std::vector<int64_t> at;

    bool next(std::string& s, int64_t until)
    {
        for (;;) {
            size_t i = buf.find('\n');
            if (i != std::string::npos) {
                s = buf.substr(0, i);
                buf.erase(0, i + 1);
                log.push_back(s);
                at.push_back(steady_ms());
                return true;
            }
            if (ended)
                return false;
            pollfd pf{out, POLLIN, 0};
            if (poll(&pf, 1, (int)std::max<int64_t>(0, until - steady_ms())) <= 0)
                return false;
            char b[512];
            ssize_t r = read(out, b, sizeof b);
            if (r <= 0) {
                ended = true;
                ended_at = steady_ms();
                return false;
            }
            buf.append(b, (size_t)r);
        }
    }
    bool line(std::string& s, int ms)
    {
        int64_t until = steady_ms() + ms;
        while (next(s, until))
            if (!meta(s))
                return true;
        return false;
    }
    bool eof(int ms)
    {
        int64_t until = steady_ms() + ms;
        std::string s;
        while (next(s, until)) {
        }
        return ended;
    }
    int find(const std::string& s, size_t from = 0) const
    {
        for (size_t i = from; i < log.size(); i++)
            if (log[i] == s)
                return (int)i;
        return -1;
    }
    bool closed_by(const std::string& s) const
    {
        int k = find(s);
        if (k < 0)
            return false;
        Present p;
        int last = -1;
        for (int i = k - 1; i >= 0 && last < 0; i--)
            if (as_present(log[(size_t)i], p))
                last = i;
        if (last < 0 || p.n != 0)
            return false;
        bool noted = false;
        for (int i = last + 1; i < k; i++)
            noted = noted || log[(size_t)i].rfind("note " + std::to_string(p.seq) + " ", 0) == 0;
        return noted && !visible_after((size_t)k);
    }
    bool visible_after(size_t from) const
    {
        Present p;
        for (size_t i = from; i < log.size(); i++)
            if (as_present(log[i], p) && p.visible)
                return true;
        return false;
    }
    int empties_after(size_t from) const
    {
        Present p;
        int n = 0;
        for (size_t i = from; i < log.size(); i++)
            n += as_present(log[i], p) && p.n == 0;
        return n;
    }
    bool line_after(size_t from) const
    {
        for (size_t i = from; i < log.size(); i++)
            if (!meta(log[i]))
                return true;
        return false;
    }
    int end()
    {
        int status = -1;
        if (pid > 0) {
            kill(pid, SIGTERM);
            bool gone = false;
            for (int i = 0; i < 300 && !gone; i++) {
                gone = waitpid(pid, &status, WNOHANG) == pid;
                if (!gone)
                    usleep(10000);
            }
            if (!gone) {
                std::printf("FAIL the child ignored SIGTERM for 3 s, killed\n");
                kill(pid, SIGKILL);
                waitpid(pid, &status, 0);
            }
            pid = -1;
        }
        if (out >= 0)
            close(out);
        out = -1;
        return status >= 0 && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }
};

Loop start_loop(Fonts& f, MemPresenter& mp, const EvdevOps& ops, const PmRunOpts& base, bool chld_ign = false,
                int drop = -1)
{
    Loop l;
    int p[2];
    if (pipe(p) != 0)
        return l;
    std::fflush(stdout);
    l.pid = fork();
    if (l.pid == 0) {
        close(p[0]);
        PmRunOpts o = base;
        o.out = p[1];
        mp.log_to(p[1]);
        if (chld_ign)
            signal(SIGCHLD, SIG_IGN);
        if (drop >= 0)
            close(drop);
        MenuGrab g(ops);
        g.start(steady_ms());
        _exit(run_powermenu(mp, f, g, o));
    }
    close(p[1]);
    l.out = p[0];
    return l;
}

void write_file(const std::string& path, const std::string& text, mode_t mode = 0644)
{
    std::ofstream(path) << text;
    chmod(path.c_str(), mode);
}

void loop_cases(Fonts& f, const std::string& dir)
{
    EvdevOps none;
    none.list = []() { return std::vector<std::string>(); };
    none.open = [](const std::string&) { return -1; };
    none.info = [](int, DevInfo&) { return false; };
    none.grab = [](int) { return 0; };
    none.keys_down = [](int) { return false; };
    std::string fifo = dir + "/input";
    mkfifo(fifo.c_str(), 0600);
    std::string gates = dir + "/state";
    write_file(gates, "state=default\nandroid=bootable\nreturn=keys\n");
    std::string idle_unit = dir + "/systemctl-idle";
    write_file(idle_unit, "if [ \"$1\" = is-system-running ]; then echo running; exit 0; fi\n"
                          "printf 'LoadState=loaded\\nActiveState=inactive\\nResult=success\\n'\n"
                          "printf 'StateChangeTimestampMonotonic=9000000000000000\\n'\n");
    PmRunOpts o;
    o.gates = gates;
    o.fifo = fifo;
    o.systemctl = {"sh", idle_unit};
    auto send = [&](const char* text) {
        int fd = open(fifo.c_str(), O_WRONLY | O_NONBLOCK);
        if (fd >= 0) {
            if (write(fd, text, std::strlen(text)) < 0)
                std::printf("FAIL fifo write\n");
            close(fd);
        }
    };
    {
        MemPresenter mp;
        Loop l = start_loop(f, mp, none, o);
        std::string s;
        bool shown = l.line(s, 3000) && s == "shown";
        check(shown, "shown after the first presented frame with regions");
        send("accept\n");
        check(l.line(s, 3000) && s == "poweroff", "FIFO accept on the focused POWER OFF: poweroff");
        check(l.eof(3000), "the child exits after its result");
        check(l.end() == 0, "exit 0 after a result that is no failure");
    }
    {
        MemPresenter mp;
        mp.discard_next(1000000);
        Loop l = start_loop(f, mp, none, o);
        std::string s;
        check(!l.line(s, 600), "no shown while every frame is discarded");
        send("back\n");
        check(l.line(s, 3000) && s == "cancel", "FIFO back: cancel");
        l.end();
    }
    {
        MemPresenter mp;
        mp.ready_for(1);
        Loop l = start_loop(f, mp, none, o);
        std::string s;
        check(!l.line(s, 600), "no shown when only the alpha 0 first frame was presented");
        send("back\n");
        check(l.line(s, 3000) && s == "cancel", "Back still answers with no frame after the first");
        l.end();
    }
    {
        MemPresenter mp;
        mp.close();
        Loop l = start_loop(f, mp, none, o);
        std::string s;
        check(l.line(s, 3000) && s == "failed wayland", "a closed surface gives failed wayland");
        check(!l.line(s, 300), "nothing after failed wayland");
        check(l.end() == 1, "exit 1 after failed wayland");
    }
    {
        MemPresenter mp;
        mp.set_opens(false);
        Loop l = start_loop(f, mp, none, o);
        std::string s;
        check(l.line(s, 3000) && s == "failed wayland" && l.eof(3000), "no layer surface gives failed wayland");
        l.end();
    }
    struct UnitCase {
        const char* name;
        const char* load;
        const char* active;
        const char* result;
        const char* sys;
        int rc;
        int from;
        bool ends;
        const char* what;
    };
    const UnitCase cases[] = {
        {"running", "loaded", "failed", "exit-code", "running", 0, 1, true,
         "a failed reboot unit ends the child after its toast"},
        {"hold", "loaded", "failed", "exit-code", "running", 0, 3, true,
         "a reboot unit failing at the third probe ends the child after its toast"},
        {"stopping", "loaded", "failed", "exit-code", "stopping", 0, 1, false,
         "a failed unit while stopping keeps restarting"},
        {"notfound", "not-found", "inactive", "success", "running", 0, 1, true,
         "a reboot unit not found ends the child after its toast"},
        {"notfound-stopping", "not-found", "inactive", "success", "stopping", 0, 1, false,
         "a unit not found while stopping keeps restarting"},
        {"inactive", "loaded", "inactive", "success", "running", 0, 1, false, "inactive with success keeps restarting"},
        {"exit1", "loaded", "failed", "exit-code", "running", 1, 1, false,
         "a nonzero systemctl show keeps restarting"},
    };
    for (const UnitCase& c : cases) {
        std::string ctl = dir + "/systemctl-" + c.name, log = dir + "/calls-" + c.name;
        write_file(ctl, std::string("echo \"$1\" >> '") + log + "'\n" +
                            "if [ \"$1\" = is-system-running ]; then echo " + c.sys + "; exit 0; fi\n" +
                            "if [ \"$(grep -c -- --user '" + log + "')\" -lt " + std::to_string(c.from) + " ]; then\n" +
                            "printf 'LoadState=loaded\\nActiveState=inactive\\nResult=success\\n'\n" +
                            "printf 'StateChangeTimestampMonotonic=9000000000000000\\n'; exit 0; fi\n" +
                            "printf 'LoadState=" + c.load + "\\nActiveState=" + c.active + "\\nResult=" + c.result +
                            "\\nStateChangeTimestampMonotonic=9000000000000000\\n'\n" + "exit " +
                            std::to_string(c.rc) + "\n");
        PmRunOpts u = o;
        u.systemctl = {"sh", ctl};
        MemPresenter mp;
        Loop l = start_loop(f, mp, none, u);
        std::string s;
        l.line(s, 3000);
        send("down\naccept\n");
        usleep(500000);
        send("accept\n");
        bool boot = l.line(s, 3000) && s == "boot reboot";
        int64_t boot_at = boot ? l.at[(size_t)l.find("boot reboot")] : 0;
        int window = fx_round(tok::motion_toastIn) + fx_round(tok::motion_toastHold) + fx_round(tok::motion_toastOut) +
                     1500;
        check(boot && l.eof(c.ends ? window + 2500 : window) == c.ends, c.what);
        std::string name = c.name;
        if (name == "running")
            check(l.ended_at - boot_at >= 3500 && l.ended_at - boot_at <= 4900,
                  "a failure in the restarting in keeps the hold start: gone about 4.1 s after boot");
        if (name == "hold")
            check(l.ended_at - boot_at >= 5100 && l.ended_at - boot_at <= 7000,
                  "a failure in the restarting hold restarts the hold: gone about 3.75 s after the third probe");
        std::ifstream in(log);
        int shows = 0, systems = 0;
        for (std::string w; std::getline(in, w);)
            (w == "is-system-running" ? systems : shows)++;
        if (name == "notfound")
            check(shows >= 1 && systems >= 1, "a unit not loaded goes through the stopping filter too");
        if (name == "stopping" || name == "notfound-stopping")
            check(shows >= 3 && systems >= 3, "a failure while stopping is ignored and the polling goes on");
        if (name == "inactive")
            check(shows >= 4 && systems == 0, "inactive with success keeps polling past 3 probes");
        if (name == "exit1")
            check(shows >= 3 && systems == 0, "a nonzero systemctl show keeps polling, no failure");
        int rc = l.end();
        if (name == "running")
            check(rc == 0, "exit 0 after the failure toast, no failed line was sent");
    }
    struct CloseCase {
        const char* name;
        const char* input;
        const char* line;
        int idle;
        int rc;
    };
    const CloseCase closes[] = {
        {"POWER OFF", "accept\n", "poweroff", 0, 0},
        {"Back", "back\n", "cancel", 0, 0},
        {"the idle timeout", "", "screen-off", 400, 0},
        {"failed gate", "", "failed gate", 0, 1},
    };
    for (const CloseCase& c : closes) {
        write_file(gates, "state=default\nandroid=bootable\nreturn=keys\n");
        PmRunOpts u = o;
        u.idle_ms = c.idle;
        MemPresenter mp;
        Loop l = start_loop(f, mp, none, u);
        std::string s;
        bool shown = l.line(s, 3000) && s == "shown";
        if (std::string(c.line) == "failed gate") {
            send("down\naccept\n");
            usleep(500000);
            write_file(gates, "state=pending\nreason=lock busy\n");
            send("accept\n");
        } else {
            send(c.input);
        }
        bool said = l.line(s, 8000) && s == c.line;
        bool gone = l.eof(3000);
        std::string what = std::string(c.name) + ": the next present is empty and the line follows its note";
        check(shown && said && gone && l.closed_by(c.line), what.c_str());
        what = std::string(c.name) + ": nothing visible after the line, one line, exit " + std::to_string(c.rc);
        int k = l.find(c.line);
        check(k >= 0 && !l.visible_after((size_t)k) && !l.line_after((size_t)k + 1) && l.end() == c.rc, what.c_str());
    }
    write_file(gates, "state=default\nandroid=bootable\nreturn=keys\n");
    {
        MemPresenter mp;
        mp.hold_notes();
        Loop l = start_loop(f, mp, none, o);
        usleep(300000);
        int64_t sent_at = steady_ms();
        send("accept\n");
        std::string s;
        bool said = l.line(s, 3000) && s == "poweroff";
        int k = l.find("poweroff");
        bool late = k >= 0 && l.at[(size_t)k] - sent_at >= PowerMenu::kCloseMs - 5;
        bool empty_before = false;
        Present pr;
        for (int i = 0; i < k; i++)
            empty_before = empty_before || (as_present(l.log[(size_t)i], pr) && pr.n == 0);
        check(said && late && empty_before,
              "notes withheld: the line comes 50 ms after the pick, the empty frame first");
        check(l.eof(3000) && l.end() == 0, "notes withheld: the child exits");
    }
    {
        MemPresenter mp;
        mp.pause(2, 600);
        Loop l = start_loop(f, mp, none, o);
        std::string s;
        bool shown = l.line(s, 3000) && s == "shown";
        send("accept\n");
        bool said = l.line(s, 3000) && s == "poweroff";
        int k = l.find("poweroff");
        bool gone = l.eof(3000);
        Present pr;
        bool empty_before = false;
        for (int i = 0; i < k; i++)
            empty_before = empty_before || (as_present(l.log[(size_t)i], pr) && pr.n == 0);
        check(shown && said && !empty_before, "presenter not ready at the pick: the line still comes at 50 ms");
        check(gone && k >= 0 && l.empties_after((size_t)k) == 1 && !l.visible_after((size_t)k),
              "presenter not ready at the pick: the empty frame is committed before exit");
        l.end();
    }
    {
        MemPresenter mp;
        Loop l = start_loop(f, mp, none, o);
        std::string s;
        l.line(s, 3000);
        send("down\naccept\n");
        usleep(500000);
        send("accept\n");
        bool boot = l.line(s, 3000) && s == "boot reboot";
        int k = l.find("boot reboot");
        Present pr, last;
        bool any = false;
        for (int i = 0; i < k; i++)
            if (as_present(l.log[(size_t)i], pr)) {
                last = pr;
                any = true;
            }
        check(boot && any && last.n == 1 && last.visible,
              "boot comes with a visible restarting frame, never after an empty one");
        l.end();
    }
    {
        int pp[2];
        if (pipe(pp) == 0) {
            fcntl(pp[0], F_SETFL, O_NONBLOCK);
            EvdevOps pad;
            pad.list = []() { return std::vector<std::string>{"/dev/input/event9"}; };
            pad.open = [&pp](const std::string&) { return dup(pp[0]); };
            pad.info = [](int, DevInfo& d) {
                d.id = {0x0019, 0x4001, 0x0428, "ayaneo-gamepad/input0", "AYANEO Controller"};
                return true;
            };
            pad.grab = [](int) { return 0; };
            pad.keys_down = [](int) { return false; };
            pad.real = [](const std::string&) { return std::string("/sys/devices/virtual/input/input9/event9"); };
            for (int code : {BTN_DPAD_DOWN, BTN_EAST}) {
                push(pp[1], EV_KEY, (uint16_t)code, 1);
                push(pp[1], EV_SYN, SYN_REPORT, 0);
                push(pp[1], EV_KEY, (uint16_t)code, 0);
                push(pp[1], EV_SYN, SYN_REPORT, 0);
            }
            MemPresenter mp;
            Loop l = start_loop(f, mp, pad, o);
            std::string s;
            bool shown = l.line(s, 3000) && s == "shown";
            bool quiet = !l.line(s, 400);
            check(shown && quiet, "Down and Accept queued before shown pick nothing");
            push(pp[1], EV_KEY, BTN_EAST, 1);
            push(pp[1], EV_SYN, SYN_REPORT, 0);
            push(pp[1], EV_KEY, BTN_EAST, 0);
            push(pp[1], EV_SYN, SYN_REPORT, 0);
            check(l.line(s, 3000) && s == "poweroff",
                  "a fresh Accept after shown picks the row in focus, POWER OFF");
            l.end();
            close(pp[0]);
            close(pp[1]);
        }
    }
    {
        int pp[2];
        if (pipe(pp) == 0) {
            fcntl(pp[0], F_SETFL, O_NONBLOCK);
            EvdevOps pad;
            pad.list = []() { return std::vector<std::string>{"/dev/input/event9"}; };
            pad.open = [&pp](const std::string&) { return dup(pp[0]); };
            pad.info = [](int, DevInfo& d) {
                d.id = {0x0019, 0x4001, 0x0428, "ayaneo-gamepad/input0", "AYANEO Controller"};
                return true;
            };
            pad.grab = [](int) { return 0; };
            pad.keys_down = [](int) { return false; };
            pad.real = [](const std::string&) { return std::string("/sys/devices/virtual/input/input9/event9"); };
            push(pp[1], EV_KEY, BTN_EAST, 1);
            push(pp[1], EV_SYN, SYN_REPORT, 0);
            MemPresenter mp;
            Loop l = start_loop(f, mp, pad, o);
            std::string s;
            bool shown = l.line(s, 3000) && s == "shown";
            usleep(400000);
            push(pp[1], EV_KEY, BTN_EAST, 0);
            push(pp[1], EV_SYN, SYN_REPORT, 0);
            bool quiet = !l.line(s, 400);
            send("back\n");
            bool alive = l.line(s, 3000) && s == "cancel";
            check(shown && quiet && alive, "a pad key down at the grab and released after the open prints nothing");
            l.end();
            close(pp[0]);
            close(pp[1]);
        }
    }
    {
        std::string stall = dir + "/stall";
        unlink(stall.c_str());
        MemPresenter mp;
        mp.stall_while(stall);
        Loop l = start_loop(f, mp, none, o);
        std::string s;
        l.line(s, 3000);
        send("down\naccept\n");
        usleep(500000);
        while (l.next(s, steady_ms() + 50)) {
        }
        size_t k0 = l.log.size();
        write_file(stall, "");
        int64_t picked = steady_ms();
        send("accept\n");
        bool boot = l.line(s, PowerMenu::kBootMs + 3000) && s == "boot reboot";
        int k = l.find("boot reboot", k0);
        int64_t after = k >= 0 ? l.at[(size_t)k] - picked : -1;
        bool toast_before = false;
        Present pr;
        for (int i = (int)k0; i < k; i++)
            toast_before = toast_before || (as_present(l.log[(size_t)i], pr) && pr.n == 1);
        unlink(stall.c_str());
        bool pass = boot && !toast_before && after >= PowerMenu::kBootMs - 5 && after <= PowerMenu::kBootMs + 1000;
        check(pass, "with the presenter stalled from the pick, boot goes out 480 ms after the pick");
        if (!pass)
            std::printf("  boot %d, after %lld ms, toast frame before boot %d\n", boot ? 1 : 0, (long long)after,
                        toast_before ? 1 : 0);
        l.end();
    }
    {
        std::string ctl = dir + "/systemctl-chld";
        write_file(ctl, "if [ \"$1\" = is-system-running ]; then echo running; exit 0; fi\n"
                        "printf 'LoadState=loaded\\nActiveState=failed\\nResult=exit-code\\n'\n"
                        "printf 'StateChangeTimestampMonotonic=9000000000000000\\n'\n");
        PmRunOpts u = o;
        u.systemctl = {"sh", ctl};
        MemPresenter mp;
        Loop l = start_loop(f, mp, none, u, true);
        std::string s;
        l.line(s, 3000);
        send("down\naccept\n");
        usleep(500000);
        send("accept\n");
        bool boot = l.line(s, 3000) && s == "boot reboot";
        check(boot && l.eof(8000) && l.end() == 0,
              "SIGCHLD ignored by the parent: the child resets it and still reads the probe's exit status");
    }
    struct TermCase {
        const char* name;
        const char* input;
        int wait;
        bool boot;
    };
    const TermCase terms[] = {
        {"the dialog", "", 0, false},
        {"the sheet", "down\naccept\n", 500, false},
        {"the confirm", "down\naccept\ndown\naccept\n", 1200, false},
        {"restarting", "down\naccept\n", 500, true},
    };
    for (const TermCase& c : terms) {
        MemPresenter mp;
        Loop l = start_loop(f, mp, none, o);
        std::string s;
        bool ok = l.line(s, 3000) && s == "shown";
        if (*c.input) {
            send(c.input);
            usleep(c.wait * 1000);
        }
        if (c.boot) {
            send("accept\n");
            ok = ok && l.line(s, 3000) && s == "boot reboot";
        }
        std::string drained;
        while (l.next(drained, steady_ms() + 50)) {
        }
        size_t k = l.log.size();
        kill(l.pid, SIGTERM);
        bool gone = l.eof(3000);
        std::string what = std::string("SIGTERM over ") + c.name + ": one empty frame, no line, exit";
        check(ok && gone && l.empties_after(k) == 1 && !l.visible_after(k) && !l.line_after(k) && l.end() == 0,
              what.c_str());
    }
    struct OutCase {
        const char* name;
        int in_ms, out_ms, rise;
    };
    const OutCase outs[] = {{"in", 1500, 0, 12}, {"hold", 0, 0, 1}, {"out", 0, 1500, 1}};
    const int hold_a8 = region_a8(alpha256(tok::scrim_overlay_alpha));
    for (const OutCase& c : outs) {
        write_file(gates, "state=default\nandroid=bootable\nreturn=keys\n");
        PmRunOpts u = o;
        u.toast_in_ms = c.in_ms;
        u.toast_out_ms = c.out_ms;
        MemPresenter mp;
        Loop l = start_loop(f, mp, none, u);
        std::string s;
        bool ok = l.line(s, 3000) && s == "shown";
        send("down\naccept\n");
        usleep(500000);
        write_file(gates, "state=pending\nreason=lock busy\n");
        send("accept\n");
        std::string phase = c.name;
        Present pr;
        bool sheet = false, armed = false;
        int top = 0;
        int64_t until = steady_ms() + 8000;
        while (!armed && l.next(s, until)) {
            if (!as_present(s, pr))
                continue;
            if (pr.n == 2) {
                sheet = true;
                continue;
            }
            if (pr.n != 1 || !sheet)
                continue;
            top = std::max(top, pr.most);
            if (phase == "in")
                armed = pr.most >= hold_a8 * 9 / 10 && pr.most < hold_a8;
            else if (phase == "out")
                armed = top >= hold_a8 - 2 && pr.most < top - 1;
            else
                armed = pr.most >= hold_a8 - 2;
        }
        if (phase == "hold")
            usleep(1000000);
        while (l.next(s, steady_ms())) {
        }
        int before = -1;
        for (size_t i = 0; i < l.log.size(); i++)
            if (as_present(l.log[i], pr) && pr.n == 1)
                before = pr.most;
        size_t k = l.log.size();
        int64_t killed = steady_ms();
        kill(l.pid, SIGTERM);
        int out_ms = c.out_ms > 0 ? c.out_ms : fx_round(tok::motion_toastOut);
        bool gone = l.eof(out_ms + 3000);
        std::vector<int> post;
        for (size_t i = k; i < l.log.size(); i++)
            if (as_present(l.log[i], pr) && pr.n == 1)
                post.push_back(pr.most);
        size_t peak = 0;
        for (size_t i = 1; i < post.size(); i++)
            if (post[i] > post[peak])
                peak = i;
        bool falling = post.size() >= 2 && post.back() < post[peak];
        for (size_t i = peak + 1; i < post.size(); i++)
            falling = falling && post[i] <= post[i - 1];
        bool near = !post.empty() && post[peak] <= before + c.rise && post.front() >= before - hold_a8 / 4;
        std::string what = "SIGTERM in the failure toast " + phase + ": the out runs from its value";
        check(ok && armed && before > 0 && near && falling, what.c_str());
        what = "SIGTERM in the failure toast " + phase + ": one empty frame, no line, exit within the out plus 1050 ms";
        check(gone && l.ended_at - killed <= out_ms + 1050 && l.empties_after(k) == 1 && !l.line_after(k) &&
                  l.end() == 0,
              what.c_str());
    }
    unlink(fifo.c_str());
}

class StepPresenter : public MemPresenter {
public:
    StepPresenter() : wake_(eventfd(1, EFD_NONBLOCK | EFD_CLOEXEC)) {}
    ~StepPresenter() override
    {
        if (wake_ >= 0)
            ::close(wake_);
    }
    int fd() const override { return wake_; }
    uint32_t present(const std::vector<AlphaRegion>& regions) override
    {
        presents++;
        return MemPresenter::present(regions);
    }
    bool pump(std::vector<PresentNote>& out) override
    {
        if (on_pass)
            on_pass();
        return MemPresenter::pump(out);
    }
    int presents = 0;
    std::function<void()> on_pass;

private:
    int wake_ = -1;
};

struct RestRun {
    std::vector<int> same;
    size_t rested = 0;
    int rc = -1;
    bool capped = false;
    Loop log;
};

RestRun run_to_rest(Fonts& f, PmRunOpts o, const std::vector<std::vector<uint32_t>>& want,
                    const std::vector<std::function<void()>>& acts)
{
    RestRun r;
    r.same.assign(want.size(), -1);
    int p[2];
    if (pipe(p) != 0)
        return r;
    EvdevOps none;
    none.list = []() { return std::vector<std::string>(); };
    none.open = [](const std::string&) { return -1; };
    none.info = [](int, DevInfo&) { return false; };
    none.grab = [](int) { return 0; };
    none.keys_down = [](int) { return false; };
    StepPresenter mp;
    mp.log_to(p[1]);
    int64_t t = 1000000, cap = steady_ms() + 20000;
    int seen = 0, base = 0, idle = 0;
    std::vector<uint32_t> last;
    o.out = p[1];
    o.clock = [&t]() { return t; };
    mp.on_pass = [&]() {
        t += 18;
        if (!r.capped && steady_ms() > cap) {
            r.capped = true;
            raise(SIGTERM);
        }
        if (mp.presents != seen) {
            seen = mp.presents;
            idle = 0;
            return;
        }
        if (r.rested >= want.size() || seen == base || ++idle < 20)
            return;
        idle = 0;
        base = seen;
        if (mp.argb() == last)
            return;
        last = mp.argb();
        r.same[r.rested] = mp.argb() == want[r.rested];
        if (r.rested < acts.size())
            acts[r.rested]();
        r.rested++;
    };
    sigset_t mask, term;
    sigprocmask(SIG_SETMASK, nullptr, &mask);
    MenuGrab g(none);
    g.start(t);
    r.rc = run_powermenu(mp, f, g, o);
    sigemptyset(&term);
    sigaddset(&term, SIGTERM);
    timespec zero{0, 0};
    sigtimedwait(&term, nullptr, &zero);
    sigprocmask(SIG_SETMASK, &mask, nullptr);
    close(p[1]);
    r.log.out = p[0];
    r.log.eof(1000);
    return r;
}

void rest_cases(Fonts& f, const std::string& dir)
{
    InkLayer ink;
    MemPresenter ref;
    std::vector<AlphaRegion> regions;
    auto rest_of = [&](const PmView& v) {
        Canvas c(ref.frame());
        f.begin_frame(-1);
        draw_powermenu(c, f, ink, v, theme(), regions);
        ref.present(regions);
        return ref.argb();
    };
    std::string gates = dir + "/rest-state", fifo = dir + "/rest-input", go = dir + "/rest-go";
    std::string ctl = dir + "/systemctl-rest";
    write_file(ctl, "if [ \"$1\" = is-system-running ]; then echo running; exit 0; fi\n"
                    "while [ ! -e '" + go + "' ]; do sleep 0.01; done\n"
                    "printf 'LoadState=not-found\\nActiveState=inactive\\nResult=\\n'\n");
    mkfifo(fifo.c_str(), 0600);
    int in = open(fifo.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (in < 0) {
        check(false, "rest cases: the input fifo opens");
        return;
    }
    auto send = [in](const char* w) {
        return std::function<void()>([in, w]() {
            if (write(in, w, std::strlen(w)) < 0)
                std::printf("FAIL fifo write\n");
        });
    };
    auto ends_empty = [](const Loop& l) {
        Present pr;
        for (size_t i = l.log.size(); i-- > 0;)
            if (as_present(l.log[i], pr))
                return pr.n == 0;
        return false;
    };
    auto checks = [&](const RestRun& run, const std::vector<const char*>& names, const char* label) {
        for (size_t k = 0; k < names.size(); k++) {
            std::string what = std::string(label) + ", " + names[k] + ": the last frame is the frame at rest";
            check(k < run.same.size() && run.same[k] == 1, what.c_str());
        }
        std::string what = std::string(label) + ": every motion reaches rest and nothing is drawn at rest";
        check(run.rested == names.size() && !run.capped, what.c_str());
    };
    PmRunOpts o;
    o.gates = gates;
    o.fifo = fifo;
    o.systemctl = {"sh", ctl};
    {
        Rig r;
        std::vector<std::vector<uint32_t>> want;
        auto rest = [&]() {
            r.at(1000);
            want.push_back(rest_of(r.v()));
        };
        rest();
        r.act(PadAction::Down);
        rest();
        r.act(PadAction::Accept);
        rest();
        r.act(PadAction::Down);
        rest();
        r.act(PadAction::Up);
        rest();
        r.act(PadAction::Accept);
        rest();
        r.at(10000);
        bool forever = !r.m.done() && r.v().surface == PmSurface::Toast && rest_of(r.v()) == want.back();
        r.m.fail("not-found", r.t);
        rest();
        write_file(gates, kDefault);
        unlink(go.c_str());
        RestRun run = run_to_rest(f, o, want,
                                  {send("down\n"), send("accept\n"), send("down\n"), send("up\n"), send("accept\n"),
                                   [&go]() { write_file(go, ""); }});
        checks(run,
               {"dialog open", "dialog bar and item alpha", "sheet rise", "sheet bar down", "sheet bar up",
                "restarting toast in", "swap to reboot failed"},
               "reboot");
        check(forever, "reboot: the restarting toast is unchanged 10 s after its in, as the swap case needs");
        check(run.rc == 0 && run.log.find("boot reboot") >= 0 && ends_empty(run.log),
              "reboot: the failure toast out still ends on one empty frame");
        run.log.end();
        unlink(go.c_str());
    }
    {
        Rig r;
        std::vector<std::vector<uint32_t>> want;
        auto rest = [&]() {
            r.at(1000);
            want.push_back(rest_of(r.v()));
        };
        rest();
        r.act(PadAction::Down);
        rest();
        r.act(PadAction::Accept);
        rest();
        r.act(PadAction::Down);
        rest();
        r.act(PadAction::Accept);
        rest();
        r.act(PadAction::Up);
        rest();
        r.now_gates = parse_gates("state=pending\nreason=lock busy\n");
        r.act(PadAction::Accept);
        rest();
        write_file(gates, kDefault);
        RestRun run = run_to_rest(f, o, want,
                                  {send("down\n"), send("accept\n"), send("down\n"), send("accept\n"), send("up\n"),
                                   [&]() {
                                       write_file(gates, "state=pending\nreason=lock busy\n");
                                       send("accept\n")();
                                   }});
        checks(run,
               {"dialog open", "dialog bar and item alpha", "sheet rise", "sheet bar down", "confirm open",
                "confirm bar and item alpha", "failure toast in"},
               "android");
        check(run.rc == 1 && run.log.closed_by("failed gate"),
              "android: the failure toast out still ends on one empty frame before failed gate");
        run.log.end();
    }
    close(in);
    unlink(fifo.c_str());
}

void pause_loop_cases(Fonts& f, const std::string& dir)
{
    EvdevOps none;
    none.list = []() { return std::vector<std::string>(); };
    none.open = [](const std::string&) { return -1; };
    none.info = [](int, DevInfo&) { return false; };
    none.grab = [](int) { return 0; };
    none.keys_down = [](int) { return false; };
    std::string gates = dir + "/pause-state", fifo = dir + "/pause-input";
    write_file(gates, kDefault);
    mkfifo(fifo.c_str(), 0600);
    auto send = [](int fd, const char* text) {
        if (write(fd, text, std::strlen(text)) < 0)
            std::printf("FAIL pause write\n");
    };
    {
        int in[2];
        if (pipe(in) != 0) {
            check(false, "pause loop: a stdin pipe");
            return;
        }
        PmRunOpts o;
        o.gates = gates;
        o.pause = true;
        o.in = in[0];
        MemPresenter mp;
        Loop l = start_loop(f, mp, none, o);
        close(in[0]);
        std::string s;
        check(l.line(s, 3000) && s == "shown", "pause loop: shown after the first presented frame");
        send(in[1], "power\n");
        check(l.line(s, 3000) && s == "shown power",
              "pause loop: stdin power, then shown power once a frame with the power body is presented");
        send(in[1], "back\n");
        check(l.line(s, 3000) && s == "cancel", "pause loop: stdin back on the power dialog says cancel");
        send(in[1], "bogus\n");
        send(in[1], "back\n");
        check(l.line(s, 3000) && s == "resume",
              "pause loop: stdin back on the pause dialog closes it with resume, an unknown word ignored");
        check(l.eof(3000) && l.end() == 0, "pause loop: the child exits 0 after resume");
        close(in[1]);
    }
    {
        int in[2];
        if (pipe(in) != 0) {
            check(false, "pause loop: a stdin pipe");
            return;
        }
        PmRunOpts o;
        o.gates = gates;
        o.fifo = fifo;
        o.pause = true;
        o.in = in[0];
        MemPresenter mp;
        Loop l = start_loop(f, mp, none, o);
        close(in[0]);
        std::string s;
        bool shown = l.line(s, 3000) && s == "shown";
        int fd = open(fifo.c_str(), O_WRONLY | O_NONBLOCK);
        if (fd >= 0) {
            send(fd, "down\naccept\n");
            close(fd);
        }
        check(shown && l.line(s, 3000) && s == "quit" && l.closed_by("quit"),
              "pause loop: Accept on quit says quit after the empty frame");
        l.end();
        close(in[1]);
    }
    {
        int in[2], pad[2];
        if (pipe(in) != 0 || pipe(pad) != 0) {
            check(false, "pause loop: the stdin and pad pipes");
            return;
        }
        fcntl(pad[0], F_SETFL, O_NONBLOCK);
        EvdevOps one = none;
        one.list = []() { return std::vector<std::string>{"/dev/input/event9"}; };
        one.open = [&pad](const std::string&) { return dup(pad[0]); };
        one.info = [](int, DevInfo& d) {
            d.id.name = "Pad invented";
            d.joy = true;
            return true;
        };
        PmRunOpts o;
        o.gates = gates;
        o.pause = true;
        o.in = in[0];
        MemPresenter mp;
        Loop l = start_loop(f, mp, one, o);
        close(in[0]);
        std::string s;
        bool shown = l.line(s, 3000) && s == "shown";
        send(in[1], "power\n");
        bool later = l.line(s, 3000) && s == "shown power";
        push(pad[1], EV_KEY, BTN_MODE, 1);
        push(pad[1], EV_SYN, SYN_REPORT, 0);
        bool down = l.line(s, 3000) && s == "key extra1 1";
        push(pad[1], EV_KEY, BTN_MODE, 0);
        push(pad[1], EV_SYN, SYN_REPORT, 0);
        bool up = l.line(s, 3000) && s == "key extra1 0";
        send(in[1], "back\n");
        bool cancel = l.line(s, 3000) && s == "cancel";
        send(in[1], "back\n");
        check(shown && later && down && up && cancel && l.line(s, 3000) && s == "resume",
              "pause loop: BTN_MODE on the grabbed pad goes to stdout as key extra1 1 and 0, no result");
        l.end();
        close(in[1]);
        close(pad[0]);
        close(pad[1]);
    }
    {
        int in[2], pad[2];
        if (pipe(in) != 0 || pipe(pad) != 0) {
            check(false, "pause loop: the stdin and pad pipes");
            return;
        }
        fcntl(pad[0], F_SETFL, O_NONBLOCK);
        EvdevOps one = none;
        one.list = []() { return std::vector<std::string>{"/dev/input/event9"}; };
        one.open = [&pad](const std::string&) { return dup(pad[0]); };
        one.info = [](int, DevInfo& d) {
            d.id.name = "Pad invented";
            d.joy = true;
            return true;
        };
        PmRunOpts o;
        o.gates = gates;
        o.pause = true;
        o.in = in[0];
        MemPresenter mp;
        Loop l = start_loop(f, mp, one, o);
        close(in[0]);
        std::string s;
        bool shown = l.line(s, 3000) && s == "shown";
        bool later = false;
        int64_t until = steady_ms() + 3000;
        while (!later && l.next(s, until))
            later = s.rfind("note ", 0) == 0;
        push(pad[1], EV_KEY, BTN_MODE, 0);
        push(pad[1], EV_SYN, SYN_REPORT, 0);
        push(pad[1], EV_KEY, BTN_MODE, 1);
        push(pad[1], EV_SYN, SYN_REPORT, 0);
        bool down = l.line(s, 3000) && s == "key extra1 1";
        push(pad[1], EV_KEY, BTN_MODE, 0);
        push(pad[1], EV_SYN, SYN_REPORT, 0);
        bool up = l.line(s, 3000) && s == "key extra1 0";
        send(in[1], "back\n");
        check(shown && later && down && up && l.line(s, 3000) && s == "resume",
              "pause loop: BTN_MODE on the pause dialog after a later note: a release with no press says nothing, then "
              "key extra1 1 and 0");
        l.end();
        close(in[1]);
        close(pad[0]);
        close(pad[1]);
    }
    {
        int in[2], pad[2];
        if (pipe(in) != 0 || pipe(pad) != 0) {
            check(false, "pause loop: the stdin and pad pipes");
            return;
        }
        fcntl(pad[0], F_SETFL, O_NONBLOCK);
        EvdevOps one = none;
        one.list = []() { return std::vector<std::string>{"/dev/input/event9"}; };
        one.open = [&pad](const std::string&) { return dup(pad[0]); };
        one.info = [](int, DevInfo& d) {
            d.id.name = "Pad invented";
            d.joy = true;
            return true;
        };
        std::string ctl = dir + "/systemctl-pause-failed";
        write_file(ctl, "if [ \"$1\" = is-system-running ]; then echo running; exit 0; fi\n"
                        "printf 'LoadState=loaded\\nActiveState=failed\\nResult=exit-code\\n'\n"
                        "printf 'StateChangeTimestampMonotonic=9000000000000000\\n'\n");
        PmRunOpts o;
        o.gates = gates;
        o.pause = true;
        o.in = in[0];
        o.systemctl = {"sh", ctl};
        MemPresenter mp;
        Loop l = start_loop(f, mp, one, o);
        close(in[0]);
        std::string s;
        bool shown = l.line(s, 3000) && s == "shown";
        send(in[1], "power\n");
        bool later = l.line(s, 3000) && s == "shown power";
        const uint16_t keys[][2] = {{EV_ABS, ABS_HAT0Y}, {EV_KEY, BTN_EAST}, {EV_KEY, BTN_EAST}};
        for (const auto& k : keys) {
            push(pad[1], k[0], k[1], 1);
            push(pad[1], EV_SYN, SYN_REPORT, 0);
            push(pad[1], k[0], k[1], 0);
            push(pad[1], EV_SYN, SYN_REPORT, 0);
            int got = 0;
            int64_t until = steady_ms() + 3000;
            std::string t;
            while (got < 2 && l.next(t, until))
                got += t.rfind("present ", 0) == 0;
        }
        bool boot = l.line(s, 8000) && s == "boot reboot";
        bool failed = l.line(s, 12000) && s == "failed boot";
        send(in[1], "back\n");
        check(shown && later && boot && failed && l.line(s, 3000) && s == "resume",
              "pause loop: a boot from the pause that fails says boot reboot, then failed boot back on the pause, then "
              "resume");
        l.end();
        close(in[1]);
        close(pad[0]);
        close(pad[1]);
    }
    {
        int in[2], pad[2];
        if (pipe(in) != 0 || pipe(pad) != 0) {
            check(false, "pause loop: the stdin and pad pipes");
            return;
        }
        fcntl(pad[0], F_SETFL, O_NONBLOCK);
        EvdevOps one = none;
        one.list = []() { return std::vector<std::string>{"/dev/input/event9"}; };
        one.open = [&pad](const std::string&) { return dup(pad[0]); };
        one.info = [](int, DevInfo& d) {
            d.id.name = "Pad invented";
            d.joy = true;
            return true;
        };
        std::string ctl = dir + "/systemctl-pause-retry", state = dir + "/pause-retry-state",
                    calls = dir + "/pause-retry-calls", stamp = dir + "/pause-retry-stamp";
        write_file(state, "failed\n");
        write_file(stamp, "9000000000000000\n");
        write_file(calls, "");
        write_file(ctl, "echo \"$1 $(date +%s%3N)\" >> '" + calls + "'\n" +
                            "if [ \"$1\" = is-system-running ]; then echo running; exit 0; fi\n" +
                            "printf 'LoadState=loaded\\nActiveState=%s\\nResult=exit-code\\n' \"$(cat '" + state +
                            "')\"\n" + "printf 'StateChangeTimestampMonotonic=%s\\n' \"$(cat '" + stamp + "')\"\n");
        std::string wakes = dir + "/pause-retry-wakes";
        write_file(wakes, "");
        PmRunOpts o;
        o.gates = gates;
        o.pause = true;
        o.in = in[0];
        o.systemctl = {"sh", ctl};
        o.on_wake = [wakes](int64_t, int64_t wake, int64_t due) {
            if (due < 0)
                return;
            FILE* w = std::fopen(wakes.c_str(), "a");
            if (!w)
                return;
            std::fprintf(w, "%lld %lld\n", (long long)wake, (long long)due);
            std::fclose(w);
        };
        MemPresenter mp;
        Loop l = start_loop(f, mp, one, o);
        close(in[0]);
        std::string s;
        auto pick_reboot = [&]() {
            send(in[1], "power\n");
            bool ok = l.line(s, 3000) && s == "shown power";
            const uint16_t keys[][2] = {{EV_ABS, ABS_HAT0Y}, {EV_KEY, BTN_EAST}, {EV_KEY, BTN_EAST}};
            for (const auto& k : keys) {
                push(pad[1], k[0], k[1], 1);
                push(pad[1], EV_SYN, SYN_REPORT, 0);
                push(pad[1], k[0], k[1], 0);
                push(pad[1], EV_SYN, SYN_REPORT, 0);
                int got = 0;
                int64_t until = steady_ms() + 3000;
                std::string t;
                while (got < 2 && l.next(t, until))
                    got += t.rfind("present ", 0) == 0;
            }
            return ok && l.line(s, 8000) && s == "boot reboot";
        };
        auto user_calls = [&calls]() {
            std::ifstream in(calls);
            std::string ln;
            std::vector<long long> at;
            while (std::getline(in, ln))
                if (ln.rfind("--user ", 0) == 0)
                    at.push_back(std::atoll(ln.c_str() + 7));
            return at;
        };
        bool shown = l.line(s, 3000) && s == "shown";
        bool first = pick_reboot();
        bool failed = l.line(s, 12000) && s == "failed boot";
        write_file(stamp, "1\n");
        bool again = pick_reboot();
        size_t before = user_calls().size();
        std::vector<long long> at = user_calls();
        for (int i = 0; i < 120 && at.size() < before + 4; i++) {
            usleep(50000);
            at = user_calls();
        }
        bool stale = !l.line(s, 1500);
        write_file(state, "active\n");
        write_file(stamp, "9000000000000000\n");
        bool quiet = !l.line(s, 1500);
        check(shown && first && failed && again && stale,
              "pause loop: a boot retried from the pause with the unit still failed under an old change stamp says no "
              "stale failed boot");
        check(again && quiet,
              "pause loop: a boot retried from the pause with the unit active says no stale failed boot");
        std::ifstream wf(wakes);
        long long wake = 0, due = 0;
        int waits = 0;
        bool early = true;
        while (wf >> wake >> due) {
            waits++;
            early = early && wake >= 0 && wake <= due;
        }
        check(again && at.size() >= before + 4 && waits > 0 && early,
              "pause loop: while a boot is probed, every poll wakes at or before the next probe time");
        l.end();
        close(in[1]);
        close(pad[0]);
        close(pad[1]);
    }
    {
        std::string hold = dir + "/pause-hold";
        struct Press {
            uint16_t type, code;
        };
        const Press kDown{EV_ABS, ABS_HAT0Y}, kAccept{EV_KEY, BTN_EAST}, kBack{EV_KEY, BTN_SOUTH};
        const std::vector<Press> sheet = {kDown, kAccept, kBack};
        const std::vector<Press> confirm = {kDown, kAccept, kDown, kAccept, kAccept};
        const std::pair<const std::vector<Press>*, const char*> runs[] = {{&sheet, "the reboot sheet's Back"},
                                                                           {&confirm, "the confirm's cancel"}};
        for (const auto& run : runs) {
            int in[2], pad[2];
            if (pipe(in) != 0 || pipe(pad) != 0) {
                check(false, "pause loop: the stdin and pad pipes");
                return;
            }
            fcntl(pad[0], F_SETFL, O_NONBLOCK);
            EvdevOps one = none;
            one.list = []() { return std::vector<std::string>{"/dev/input/event9"}; };
            one.open = [&pad](const std::string&) { return dup(pad[0]); };
            one.info = [](int, DevInfo& d) {
                d.id.name = "Pad invented";
                d.joy = true;
                return true;
            };
            PmRunOpts o;
            o.gates = gates;
            o.pause = true;
            o.in = in[0];
            MemPresenter mp;
            mp.hold_while(hold);
            Loop l = start_loop(f, mp, one, o);
            close(in[0]);
            std::string s;
            auto wait_for = [&l](const char* prefix, int n) {
                int got = 0;
                int64_t until = steady_ms() + 3000;
                std::string t;
                while (got < n && l.next(t, until))
                    got += t.rfind(prefix, 0) == 0;
                return got == n;
            };
            bool shown = l.line(s, 3000) && s == "shown";
            bool later = wait_for("note ", 1);
            write_file(hold, "");
            send(in[1], "power\n");
            bool drawn = wait_for("present ", 2);
            for (size_t i = 0; i < run.first->size(); i++) {
                const Press& k = (*run.first)[i];
                push(pad[1], k.type, k.code, 1);
                push(pad[1], EV_SYN, SYN_REPORT, 0);
                push(pad[1], k.type, k.code, 0);
                push(pad[1], EV_SYN, SYN_REPORT, 0);
                if (i + 1 < run.first->size())
                    drawn = wait_for("present ", 2) && drawn;
            }
            bool cancel = l.line(s, 3000) && s == "cancel";
            unlink(hold.c_str());
            send(in[1], "back\n");
            bool resume = l.line(s, 3000) && s == "resume";
            std::string what = std::string("pause loop, notes held: power then ") + run.second +
                               " says cancel and resume, no shown power from frames noted after the cancel";
            check(shown && later && drawn && cancel && resume && l.find("shown power") < 0, what.c_str());
            l.end();
            close(in[1]);
            close(pad[0]);
            close(pad[1]);
        }
        unlink(hold.c_str());
    }
    {
        int in[2];
        if (pipe(in) != 0) {
            check(false, "pause loop: a stdin pipe");
            return;
        }
        PmRunOpts o;
        o.gates = gates;
        o.pause = true;
        o.in = in[0];
        MemPresenter mp;
        Loop l = start_loop(f, mp, none, o, false, in[1]);
        close(in[0]);
        std::string s;
        bool shown = l.line(s, 3000) && s == "shown";
        close(in[1]);
        check(shown && !l.line(s, 1500) && l.eof(3000),
              "pause loop: stdin closed acts as SIGTERM: no line, the child exits");
        l.end();
    }
    {
        int pp[2];
        if (pipe(pp) != 0) {
            check(false, "line fifo: a pipe");
            return;
        }
        LineFifo lf;
        lf.adopt(pp[0]);
        std::string big(5000, 'x');
        send(pp[1], big.c_str());
        send(pp[1], "\nback\npow");
        std::vector<std::string> got;
        lf.read(got);
        bool mid = got.size() == 1 && got[0] == "back" && !lf.ended();
        send(pp[1], "er");
        close(pp[1]);
        got.clear();
        lf.read(got);
        check(mid && got.size() == 1 && got[0] == "power" && lf.ended() && lf.fd() < 0,
              "line fifo: an over long line is dropped, a last line with no newline is read at EOF, then ended");
    }
    {
        int pp[2];
        if (pipe(pp) != 0) {
            check(false, "line fifo: a pipe");
            return;
        }
        LineFifo lf;
        lf.adopt(pp[0]);
        std::string big(5000, 'x');
        send(pp[1], big.c_str());
        std::vector<std::string> got;
        lf.read(got);
        bool none = got.empty();
        send(pp[1], "back\npower\n");
        lf.read(got);
        close(pp[1]);
        check(none && got.size() == 1 && got[0] == "power",
              "line fifo: an over long line that spans reads is skipped up to its newline, its tail is no word");
    }
    unlink(fifo.c_str());
}

}

struct TempDir {
    std::string path;
    ~TempDir()
    {
        if (path.empty())
            return;
        std::string rm = "rm -rf '" + path + "'";
        if (std::system(rm.c_str()) != 0)
            std::printf("FAIL could not remove %s\n", path.c_str());
    }
};

int main(int argc, char** argv)
{
    const char* tmp = std::getenv("TMPDIR");
    std::string pattern = std::string(tmp && *tmp ? tmp : "/tmp") + "/powermenu-test-XXXXXX";
    std::vector<char> name(pattern.begin(), pattern.end());
    name.push_back(0);
    if (!mkdtemp(name.data())) {
        std::printf("FAIL no temp dir\n");
        return 1;
    }
    TempDir dir{name.data()};
    gate_cases();
    focus_cases();
    key_cases();
    more_key_cases();
    pick_cases();
    unit_cases();
    load_cases();
    idle_cases();
    term_cases();
    pause_cases();
    grab_cases();
    spawn_cases();
    Fonts f;
    if (argc < 2 || !f.open(argv[1], {})) {
        std::printf("FAIL fonts not found\n");
        return 1;
    }
    f.begin_frame(-1);
    alpha_cases(f);
    cross_cases(f);
    region_cap_cases(f);
    loop_cases(f, dir.path);
    pause_loop_cases(f, dir.path);
    rest_cases(f, dir.path);
    std::printf("%d/%d passed\n", g_total - g_fail, g_total);
    return g_fail ? 1 : 0;
}
