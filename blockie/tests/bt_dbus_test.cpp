#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

#include <dbus/dbus.h>
#include <poll.h>

#include "../bt.h"
#include "../bus.h"

using namespace fd;

namespace {

int g_fail = 0;
int g_total = 0;
DBusConnection* g_ctl = nullptr;

const char* const kTest = "net.seilent.Test.FakeBluez";
const char* const kAdapter = "/org/bluez/hci0";
const std::string kHeadset = "/org/bluez/hci0/dev_02_00_00_00_00_01";
const std::string kPad = "/org/bluez/hci0/dev_02_00_00_00_00_02";
const std::string kSpeaker = "/org/bluez/hci0/dev_02_00_00_00_00_03";
const std::string kAgent = "/net/seilent/Blockie/agent";

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

int64_t now_ms()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

DBusMessage* ctl(DBusMessage* m)
{
    DBusError err;
    dbus_error_init(&err);
    DBusMessage* r = dbus_connection_send_with_reply_and_block(g_ctl, m, 5000, &err);
    dbus_message_unref(m);
    if (!r) {
        std::printf("ctl call failed: %s\n", err.message ? err.message : "?");
        dbus_error_free(&err);
    }
    return r;
}

void test_call(const char* member, const char* s = nullptr)
{
    DBusMessage* m = dbus_message_new_method_call("org.bluez", "/test", kTest, member);
    if (s)
        dbus_message_append_args(m, DBUS_TYPE_STRING, &s, DBUS_TYPE_INVALID);
    if (DBusMessage* r = ctl(m))
        dbus_message_unref(r);
}

void powered(bool on)
{
    DBusMessage* m = dbus_message_new_method_call("org.bluez", "/test", kTest, "SetPowered");
    dbus_bool_t b = on ? TRUE : FALSE;
    dbus_message_append_args(m, DBUS_TYPE_BOOLEAN, &b, DBUS_TYPE_INVALID);
    if (DBusMessage* r = ctl(m))
        dbus_message_unref(r);
}

void ask(const char* method, const std::string& dev = "/", const char* arg = "")
{
    DBusMessage* m = dbus_message_new_method_call("org.bluez", "/test", kTest, "Ask");
    const char* d = dev.c_str();
    dbus_message_append_args(m, DBUS_TYPE_STRING, &method, DBUS_TYPE_OBJECT_PATH, &d, DBUS_TYPE_STRING, &arg,
                             DBUS_TYPE_INVALID);
    if (DBusMessage* r = ctl(m))
        dbus_message_unref(r);
}

std::vector<std::string> fake_log()
{
    std::vector<std::string> v;
    DBusMessage* r = ctl(dbus_message_new_method_call("org.bluez", "/test", kTest, "Log"));
    if (!r)
        return v;
    DBusMessageIter it, a;
    if (dbus_message_iter_init(r, &it) && dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_ARRAY) {
        dbus_message_iter_recurse(&it, &a);
        while (dbus_message_iter_get_arg_type(&a) == DBUS_TYPE_STRING) {
            const char* s = nullptr;
            dbus_message_iter_get_basic(&a, &s);
            v.push_back(s ? s : "");
            dbus_message_iter_next(&a);
        }
    }
    dbus_message_unref(r);
    return v;
}

int find(const std::vector<std::string>& log, const std::string& line, size_t from = 0)
{
    for (size_t i = from; i < log.size(); i++)
        if (log[i] == line)
            return (int)i;
    return -1;
}

int count(const std::vector<std::string>& log, const std::string& line)
{
    int n = 0;
    for (const std::string& l : log)
        n += l == line;
    return n;
}

bool logged(const std::string& line, size_t from = 0) { return find(fake_log(), line, from) >= 0; }

bool prop(const char* path, const char* iface, const char* name, DBusMessageIter& out, DBusMessage*& keep)
{
    DBusMessage* m = dbus_message_new_method_call("org.bluez", path, "org.freedesktop.DBus.Properties", "Get");
    dbus_message_append_args(m, DBUS_TYPE_STRING, &iface, DBUS_TYPE_STRING, &name, DBUS_TYPE_INVALID);
    keep = ctl(m);
    DBusMessageIter it;
    if (!keep || !dbus_message_iter_init(keep, &it) || dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_VARIANT)
        return false;
    dbus_message_iter_recurse(&it, &out);
    return true;
}

bool adapter_bool(const char* name)
{
    DBusMessageIter v;
    DBusMessage* keep = nullptr;
    dbus_bool_t b = FALSE;
    if (prop(kAdapter, "org.bluez.Adapter1", name, v, keep) && dbus_message_iter_get_arg_type(&v) == DBUS_TYPE_BOOLEAN)
        dbus_message_iter_get_basic(&v, &b);
    if (keep)
        dbus_message_unref(keep);
    return b != FALSE;
}

uint32_t adapter_u32(const char* name)
{
    DBusMessageIter v;
    DBusMessage* keep = nullptr;
    dbus_uint32_t u = 0xFFFFFFFFu;
    if (prop(kAdapter, "org.bluez.Adapter1", name, v, keep) && dbus_message_iter_get_arg_type(&v) == DBUS_TYPE_UINT32)
        dbus_message_iter_get_basic(&v, &u);
    if (keep)
        dbus_message_unref(keep);
    return u;
}

bool restored() { return !adapter_bool("Pairable") && adapter_u32("PairableTimeout") == 0 && !adapter_bool("Discovering"); }

struct Rig {
    Bus bus;
    Bt bt{bus};

    void step(int64_t now)
    {
        bus.pump();
        bus.tick(now);
        bt.tick(now);
    }

    bool until(const std::function<bool()>& cond, int ms = 3000)
    {
        int64_t end = now_ms() + ms;
        while (now_ms() < end) {
            step(now_ms());
            if (cond())
                return true;
            pollfd p{bus.fd(), POLLIN, 0};
            poll(&p, p.fd >= 0 ? 1 : 0, 10);
        }
        step(now_ms());
        return cond();
    }

    void run(BtCmd::Type t, const std::string& path = "", bool yes = false)
    {
        BtCmd c;
        c.type = t;
        c.path = path;
        c.yes = yes;
        bt.run(c, now_ms());
    }

    const BtDevice* dev(const std::string& path) const { return bt_device(bt.state(), path); }
};

}

int main()
{
    const char* addr = std::getenv("DBUS_SYSTEM_BUS_ADDRESS");
    if (!addr || !*addr) {
        std::printf("FAIL no DBUS_SYSTEM_BUS_ADDRESS: run through tests/fake_bluez.py\n");
        return 1;
    }
    DBusError err;
    dbus_error_init(&err);
    g_ctl = dbus_connection_open_private(addr, &err);
    if (!g_ctl || !dbus_bus_register(g_ctl, &err)) {
        std::printf("FAIL control connection: %s\n", err.message ? err.message : "?");
        return 1;
    }

    Rig rig;
    Bt& bt = rig.bt;
    const BtState& s = bt.state();
    bt.start();
    rig.bus.open(now_ms());

    check(rig.until([&] { return s.up && s.devices.size() == 2; }), "objects fetched at start");
    check(s.adapter.path == kAdapter && s.adapter.powered && !s.adapter.pairable, "adapter state from the objects");
    std::vector<BtRow> rows = bt_rows(s);
    check(rows.size() == 3 && rows[1].name == "Synth Headset" && rows[1].kind == BtRow::Paired &&
              rows[2].name == "Synth Pad" && rows[2].kind == BtRow::Found,
          "rows from the fetched objects");
    check(rig.until([&] {
        std::vector<std::string> log = fake_log();
        int reg = find(log, "RegisterAgent " + kAgent + " DisplayYesNo");
        return reg >= 0 && find(log, "RequestDefaultAgent " + kAgent, (size_t)reg) >= 0;
    }),
          "agent registered as DisplayYesNo and made the default");

    ask("RequestConfirmation", kPad);
    check(rig.until([&] { return logged("Ask RequestConfirmation org.bluez.Error.Rejected"); }),
          "confirmation with the page closed is rejected");
    ask("AuthorizeService", kHeadset, "0000110d-0000-1000-8000-00805f9b34fb");
    check(rig.until([&] { return logged("Ask AuthorizeService ok"); }), "a2dp service from a paired device is accepted");
    size_t mark = fake_log().size();
    ask("AuthorizeService", kHeadset, "00001101-0000-1000-8000-00805f9b34fb");
    check(rig.until([&] { return logged("Ask AuthorizeService org.bluez.Error.Rejected", mark); }),
          "a service off the allow list is rejected");
    mark = fake_log().size();
    ask("AuthorizeService", kPad, "00001124-0000-1000-8000-00805f9b34fb");
    check(rig.until([&] { return logged("Ask AuthorizeService org.bluez.Error.Rejected", mark); }),
          "hid service from an unpaired device is rejected");
    ask("RequestPasskey", kHeadset);
    check(rig.until([&] { return logged("Ask RequestPasskey org.bluez.Error.Rejected"); }), "passkey entry is rejected");
    ask("RequestAuthorization", kPad);
    check(rig.until([&] { return logged("Ask RequestAuthorization org.bluez.Error.Rejected"); }),
          "an incoming pair with no pair from the page is rejected");
    ask("Bogus");
    check(rig.until([&] { return logged("Ask Bogus org.freedesktop.DBus.Error.UnknownMethod"); }),
          "an unknown agent method gets an error reply");

    bt.set_page_open(true, now_ms());
    mark = fake_log().size();
    int64_t t0 = now_ms();
    rig.run(BtCmd::Search);
    check(s.searching && s.search_end >= t0 + 120000 && bt.deadline() >= t0 + 120000 - 1,
          "search runs a 120 s window");
    check(rig.until([&] { return adapter_bool("Pairable") && adapter_u32("PairableTimeout") == 120 &&
                                 adapter_bool("Discovering"); }),
          "search sets PairableTimeout 120, Pairable and starts discovery");
    {
        std::vector<std::string> log = fake_log();
        int a = find(log, "Set Adapter1 PairableTimeout 120", mark), b = find(log, "Set Adapter1 Pairable True", mark);
        int c = find(log, "StartDiscovery", mark);
        check(a >= 0 && b > a && c > b, "PairableTimeout, then Pairable, then StartDiscovery");
    }
    check(rig.until([&] { return rig.dev(kSpeaker) != nullptr; }), "a device found while searching joins the model");
    check(bt_rows(s).back().path == kSpeaker && bt_rows(s).back().state == "not paired", "found speaker row");
    rig.run(BtCmd::StopSearch);
    check(!s.searching && rig.until(restored), "stopping restores Pairable and PairableTimeout and stops discovery");

    rig.run(BtCmd::Search);
    check(rig.until([&] { return adapter_bool("Pairable"); }), "second search opens");
    rig.run(BtCmd::StopSearch);
    rig.run(BtCmd::Search);
    rig.run(BtCmd::StopSearch);
    check(rig.until([&] {
        std::vector<std::string> log = fake_log();
        return restored() && log.back() == "StopDiscovery";
    }),
          "a search restarted before the restore replies still restores the first saved values");

    rig.run(BtCmd::Search);
    check(rig.until([&] { return adapter_bool("Pairable"); }), "search before the page closes");
    bt.set_page_open(false, now_ms());
    check(!s.searching && rig.until(restored), "closing the page stops the search and restores");
    bt.set_page_open(true, now_ms());

    rig.run(BtCmd::Search);
    check(rig.until([&] { return adapter_bool("Pairable"); }), "search before the window ends");
    rig.step(now_ms() + 121000);
    check(!s.searching && rig.until(restored), "the 120 s window ends the search and restores");

    test_call("Mode", "confirm");
    mark = fake_log().size();
    rig.run(BtCmd::Pair, kSpeaker);
    check(s.pair_target == kSpeaker, "pair marks the target");
    check(rig.until([&] { return s.prompt.kind == BtPrompt::Confirm; }), "confirmation for the paired device shows");
    check(s.prompt.path == kSpeaker && s.prompt.passkey == 123456, "prompt carries the device and passkey");
    rig.run(BtCmd::Answer, kSpeaker, true);
    check(s.prompt.kind == BtPrompt::None, "answering clears the prompt");
    check(rig.until([&] {
        const BtDevice* d = rig.dev(kSpeaker);
        return d && d->paired && d->connected && s.connect_target.empty();
    }),
          "yes pairs, then the device connects");
    {
        std::vector<std::string> log = fake_log();
        int a = find(log, "RequestConfirmation ok", mark), b = find(log, "Set Device1 Trusted True", mark);
        int c = find(log, "Connect " + kSpeaker, mark);
        check(a >= 0 && b > a && c > b, "confirmation, then Trusted, then Connect");
        check(s.pair_target.empty() && s.pair_failed.empty(), "pair target cleared after success");
    }

    rig.run(BtCmd::Pair, kPad);
    check(rig.until([&] { return s.prompt.kind == BtPrompt::Confirm; }), "second confirmation shows");
    rig.run(BtCmd::Answer, kPad, false);
    check(rig.until([&] { return s.pair_failed == kPad && s.pair_target.empty(); }), "no fails the pair");
    check(logged("RequestConfirmation org.bluez.Error.Rejected"), "no is sent as a rejection");
    rows = bt_rows(s);
    check(rows.back().path == kPad && rows.back().state == "pair failed", "the pad row says pair failed");

    test_call("Mode", "cancel");
    rig.run(BtCmd::Pair, kPad);
    check(s.pair_failed.empty() && rig.until([&] { return s.prompt.kind == BtPrompt::Confirm; }),
          "a new pair clears the failure and asks");
    check(rig.until([&] { return s.prompt.kind == BtPrompt::None && s.pair_failed == kPad; }),
          "agent Cancel closes the prompt");
    check(rig.until([&] { return logged("Cancel ok"); }), "Cancel gets a reply");

    test_call("Mode", "display");
    rig.run(BtCmd::Pair, kPad);
    check(rig.until([&] { return s.prompt.kind == BtPrompt::Passkey; }) && s.prompt.passkey == 654321,
          "DisplayPasskey shows the code");
    check(rig.until([&] { return logged("DisplayPasskey ok"); }), "DisplayPasskey gets a reply");
    rig.run(BtCmd::Dismiss, kPad);
    check(s.prompt.kind == BtPrompt::None, "dismiss closes the shown code");
    check(rig.until([&] { return s.pair_failed == kPad; }), "the failed display pair is marked");

    test_call("Mode", "confirm");
    bt.set_game_running(true, now_ms());
    mark = fake_log().size();
    rig.run(BtCmd::Pair, kPad);
    check(rig.until([&] { return s.pair_failed == kPad; }), "a pair during a game fails");
    check(s.prompt.kind == BtPrompt::None && logged("RequestConfirmation org.bluez.Error.Rejected", mark),
          "no dialog over a game, the confirmation is rejected");
    bt.set_game_running(false, now_ms());

    test_call("Mode", "hold");
    rig.run(BtCmd::Pair, kPad);
    check(rig.until([&] { return logged("Pair hold " + kPad); }), "pair in flight");
    bt.set_page_open(false, now_ms());
    check(rig.until([&] { return logged("CancelPairing " + kPad) && s.pair_failed == kPad; }),
          "closing the page cancels the pair in flight");
    bt.set_page_open(true, now_ms());

    test_call("Mode", "pin");
    rig.run(BtCmd::Pair, kPad);
    check(rig.until([&] {
        const BtDevice* d = rig.dev(kPad);
        return d && d->paired && d->connected;
    }),
          "pin pair connects");
    check(logged("RequestPinCode 0000"), "pin code 0000 for the device paired from the page");

    rig.run(BtCmd::Disconnect, kPad);
    check(rig.until([&] { return !rig.dev(kPad)->connected; }), "disconnect");
    rig.run(BtCmd::Connect, kPad);
    check(rig.until([&] { return rig.dev(kPad)->connected; }), "connect");

    mark = fake_log().size();
    rig.run(BtCmd::Forget, kPad);
    check(rig.until([&] { return rig.dev(kPad) == nullptr; }), "forget removes a connected device");
    {
        std::vector<std::string> log = fake_log();
        int a = find(log, "Disconnect " + kPad, mark), b = find(log, "RemoveDevice " + kPad, mark);
        check(a >= 0 && b > a, "forget disconnects first, then removes");
    }
    mark = fake_log().size();
    rig.run(BtCmd::Forget, kHeadset);
    check(rig.until([&] { return rig.dev(kHeadset) == nullptr; }), "forget removes an idle device");
    check(find(fake_log(), "Disconnect " + kHeadset, mark) < 0, "an idle device is not disconnected first");

    powered(false);
    check(rig.until([&] { return !s.adapter.powered; }) && bt_rows(s)[0].kind == BtRow::Power, "power off shows the power row");
    rig.run(BtCmd::Search);
    check(!s.searching, "no search while powered off");
    rig.run(BtCmd::PowerOn);
    check(rig.until([&] { return s.adapter.powered; }) && logged("Set Adapter1 Powered True"), "power on");

    int regs = count(fake_log(), "RegisterAgent " + kAgent + " DisplayYesNo");
    uint32_t serial = bt.serial();
    test_call("Restart");
    check(rig.until([&] { return !s.up; }, 1000) || bt.serial() != serial, "bluetoothd leaving drops the model");
    check(rig.until([&] {
        return s.up && count(fake_log(), "RegisterAgent " + kAgent + " DisplayYesNo") == regs + 1;
    }),
          "bluetoothd back: objects fetched and the agent registered again");
    check(s.devices.size() == 1 && rig.dev(kSpeaker) && rig.dev(kSpeaker)->paired, "model rebuilt from the new owner");

    rig.run(BtCmd::Search);
    check(rig.until([&] { return adapter_bool("Pairable"); }), "search before shutdown");
    bt.shutdown(now_ms());
    check(rig.until(restored), "shutdown stops the search and restores");
    check(bt.deadline() == -1, "no deadline when idle");

    dbus_connection_close(g_ctl);
    dbus_connection_unref(g_ctl);
    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
