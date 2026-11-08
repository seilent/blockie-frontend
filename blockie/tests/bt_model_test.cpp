#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "../bt_model.h"

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

const char* const kSink = "0000110b-0000-1000-8000-00805f9b34fb";
const char* const kSource = "0000110a-0000-1000-8000-00805f9b34fb";
const char* const kHid = "00001124-0000-1000-8000-00805f9b34fb";
const char* const kHogp = "00001812-0000-1000-8000-00805f9b34fb";

BtDevice dev(const std::string& mac, const std::string& alias, bool paired, uint32_t cls,
             std::vector<std::string> uuids)
{
    BtDevice d;
    std::string tail = mac;
    for (char& ch : tail)
        if (ch == ':')
            ch = '_';
    d.path = "/org/bluez/hci0/dev_" + tail;
    d.adapter = "/org/bluez/hci0";
    d.address = mac;
    d.alias = alias;
    d.paired = paired;
    d.cls = cls;
    d.uuids = std::move(uuids);
    return d;
}

BtState base()
{
    BtState s;
    s.up = true;
    s.adapter.path = "/org/bluez/hci0";
    s.adapter.powered = true;
    return s;
}

std::vector<std::string> names(const std::vector<BtRow>& rows)
{
    std::vector<std::string> v;
    for (const BtRow& r : rows)
        v.push_back(r.name);
    return v;
}

int index_of(const std::vector<BtRow>& rows, const std::string& path)
{
    for (size_t i = 0; i < rows.size(); i++)
        if (rows[i].path == path)
            return (int)i;
    return -1;
}

}

int main()
{
    BtDevice headset = dev("02:00:00:00:00:01", "Synth Headset", false, 0, {kSink});
    BtDevice classed = dev("02:00:00:00:00:02", "Synth Speaker", false, 0x240414, {});
    BtDevice pad = dev("02:00:00:00:00:03", "Synth Pad", false, 0, {kHid});
    BtDevice lepad = dev("02:00:00:00:00:04", "Synth Le Pad", false, 0, {kHogp});
    BtDevice classpad = dev("02:00:00:00:00:05", "Synth Class Pad", false, 0x002508, {});
    BtDevice phone = dev("02:00:00:00:00:06", "Synth Phone", false, 0x5a020c, {kSource});
    BtDevice upper = dev("02:00:00:00:00:07", "Synth Upper", false, 0, {"0000110B-0000-1000-8000-00805F9B34FB"});

    check(bt_audio_sink(headset) && !bt_gamepad(headset), "a2dp sink uuid is an audio sink");
    check(bt_audio_sink(classed), "class major 0x04 is an audio sink without uuids");
    check(bt_audio_sink(upper), "uuid match ignores case");
    check(bt_gamepad(pad) && bt_gamepad(lepad) && bt_gamepad(classpad), "hid, hogp and class major 0x05 are gamepads");
    check(!bt_audio_sink(pad), "a gamepad is not an audio sink");
    check(!bt_listed(phone), "a phone (a2dp source, class major 0x02) is filtered out");

    BtDevice named = dev("02:00:00:00:00:08", "", false, 0, {kSink});
    check(bt_name(named) == "02:00:00:00:00:08", "no alias and no name falls back to the address");
    named.name = "Synth Name";
    check(bt_name(named) == "Synth Name", "no alias falls back to the name");

    BtState s = base();
    BtDevice alpha = dev("02:00:00:00:00:11", "alpha", true, 0, {kSink});
    BtDevice bravo = dev("02:00:00:00:00:12", "Bravo", true, 0, {kSink});
    BtDevice bravo2 = dev("02:00:00:00:00:10", "bravo", true, 0, {kHid});
    BtDevice charlie = dev("02:00:00:00:00:13", "Charlie", false, 0, {kSink});
    BtDevice able = dev("02:00:00:00:00:14", "able", false, 0, {kHid});
    s.devices = {charlie, bravo, phone, able, alpha, bravo2};
    std::vector<BtRow> rows = bt_rows(s);
    check(names(rows) == std::vector<std::string>{"search", "alpha", "bravo", "Bravo", "able", "Charlie"},
          "search first, paired by lowercase alias with address tie break, then found");
    check(rows[0].kind == BtRow::Search && rows[1].kind == BtRow::Paired && rows[4].kind == BtRow::Found,
          "row kinds follow the sections");
    check(rows[2].path == bravo2.path && rows[3].path == bravo.path, "equal lowercase alias sorts by address");
    check(rows[0].state.empty(), "search row has no state when idle");

    check(bt_row_key(rows[0]) == 1, "search row key is 1");
    check(bt_row_key(rows[1]) != 1 && bt_row_key(rows[1]) != bt_row_key(rows[2]), "device keys differ from search");
    check(bt_row_key(rows[1]) == bt_row_key(bt_rows(s)[1]), "a device key is stable for its path");

    s.pair_target = charlie.path;
    s.pair_failed = able.path;
    rows = bt_rows(s);
    check(rows[index_of(rows, charlie.path)].state == "pairing", "found row being paired says pairing");
    check(rows[index_of(rows, able.path)].state == "pair failed", "found row after a failed pair says pair failed");
    s.pair_failed.clear();
    rows = bt_rows(s);
    check(rows[index_of(rows, able.path)].state == "not paired", "found row says not paired");

    s.connect_target = alpha.path;
    s.connect_failed = bravo.path;
    s.devices[5].connected = true;
    rows = bt_rows(s);
    check(rows[index_of(rows, alpha.path)].state == "connecting", "paired row being connected says connecting");
    check(rows[index_of(rows, bravo.path)].state == "connect failed", "paired row after a failed connect");
    check(rows[index_of(rows, bravo2.path)].state == "connected" && rows[index_of(rows, bravo2.path)].connected,
          "connected paired row says connected");
    s.connect_target.clear();
    s.connect_failed.clear();
    rows = bt_rows(s);
    check(rows[index_of(rows, alpha.path)].state.empty(), "idle paired row has no state");

    int focus = index_of(rows, charlie.path);
    std::vector<BtRow> was = rows;
    BtState t = s;
    BtProps rssi;
    rssi["RSSI"] = BtValue::of_int(-40);
    for (BtDevice& d : t.devices) {
        BtDevice before = d;
        bt_apply_device(d, rssi);
        check(d == before, "an rssi change leaves the device as it was");
    }
    check(bt_rows(t) == was, "an rssi change leaves the rows as they were");
    BtProps rename;
    rename["Alias"] = BtValue::of_str("Zulu");
    for (BtDevice& d : t.devices)
        if (d.path == able.path)
            bt_apply_device(d, rename);
    rows = bt_rows(t);
    int moved = bt_refocus(was, focus, rows);
    check(rows[(size_t)moved].path == charlie.path && moved != focus, "focus follows its key when a rename reorders");

    focus = index_of(rows, alpha.path);
    was = rows;
    t.devices.erase(t.devices.begin() + 4);
    rows = bt_rows(t);
    check(index_of(rows, alpha.path) < 0 && bt_refocus(was, focus, rows) == focus,
          "a vanished key keeps the same index");
    focus = (int)rows.size() - 1;
    was = rows;
    t.devices.pop_back();
    t.devices.pop_back();
    rows = bt_rows(t);
    check(bt_refocus(was, focus, rows) == (int)rows.size() - 1, "a vanished last row clamps to the new last row");
    check(bt_refocus(was, 0, rows) == 0, "search focus stays on search");

    BtState found = base();
    found.devices = {charlie};
    found.pair_target = charlie.path;
    std::vector<BtRow> before = bt_rows(found);
    found.devices[0].paired = true;
    found.pair_target.clear();
    std::vector<BtRow> after = bt_rows(found);
    check(after[(size_t)bt_refocus(before, 1, after)].path == charlie.path && after[1].kind == BtRow::Paired,
          "focus follows a device from found to paired");

    BtState down;
    rows = bt_rows(down);
    check(rows.size() == 1 && rows[0].kind == BtRow::Search && rows[0].state == "unavailable",
          "bluetoothd down shows only the search row, unavailable");
    BtState noadapter;
    noadapter.up = true;
    noadapter.devices = {headset};
    rows = bt_rows(noadapter);
    check(rows.size() == 1 && rows[0].state == "unavailable", "no adapter shows only the search row, unavailable");
    check(bt_subtitle(down, 0) == "unavailable" && bt_subtitle(noadapter, 0) == "unavailable",
          "subtitle unavailable without bluetoothd or adapter");

    BtState off = base();
    off.adapter.powered = false;
    off.devices = {alpha};
    rows = bt_rows(off);
    check(rows[0].kind == BtRow::Power && rows[0].name == "bluetooth" && rows[0].state == "off" &&
              bt_row_key(rows[0]) == 1,
          "powered off shows the power row in the search slot");
    check(rows.size() == 2 && rows[1].path == alpha.path, "paired devices stay listed while powered off");

    BtState empty = base();
    rows = bt_rows(empty);
    check(rows.size() == 1 && rows[0].kind == BtRow::Search, "zero devices shows the search row alone");
    check(bt_subtitle(empty, 0) == "not connected", "subtitle not connected");
    BtState unpowered = empty;
    unpowered.adapter.powered = false;
    check(bt_subtitle(unpowered, 0) == "unavailable", "subtitle unavailable while the adapter is powered off");

    BtState search = base();
    search.searching = true;
    search.search_end = (int64_t)kBtSearchS * 1000;
    check(bt_rows(search)[0].state == "searching", "search row state while searching");
    check(bt_seconds(search, 0) == 120 && bt_tick_at(search, 0) == 1000, "120 s left at the start, next tick 1 s");
    check(bt_seconds(search, 999) == 120 && bt_tick_at(search, 999) == 1000, "still 120 just before the first tick");
    check(bt_seconds(search, 1000) == 119 && bt_tick_at(search, 1000) == 2000, "119 at 1 s");
    check(bt_seconds(search, 119500) == 1 && bt_tick_at(search, 119500) == 120000, "last second ticks at the end");
    check(bt_subtitle(search, 0) == "searching 120" && bt_subtitle(search, 60000) == "searching 60",
          "subtitle counts the search down");
    search.devices = {alpha};
    search.devices[0].connected = true;
    check(bt_subtitle(search, 0) == "alpha", "subtitle names the connected device first");
    BtState idle = base();
    check(bt_seconds(idle, 0) == 0 && bt_tick_at(idle, 0) == -1, "no countdown when not searching");

    BtAdapter a;
    BtProps ap;
    ap["Powered"] = BtValue::of_bool(true);
    ap["Pairable"] = BtValue::of_bool(true);
    ap["PairableTimeout"] = BtValue::of_int(120);
    ap["Discovering"] = BtValue::of_bool(true);
    ap["Alias"] = BtValue::of_str("Synth Adapter");
    bt_apply_adapter(a, ap);
    check(a.powered && a.pairable && a.pairable_timeout == 120 && a.discovering, "adapter properties apply");
    BtProps bad;
    bad["Powered"] = BtValue::of_str("yes");
    bt_apply_adapter(a, bad);
    check(a.powered, "a property of the wrong type is ignored");

    BtDevice d;
    BtProps dp;
    dp["Address"] = BtValue::of_str("02:00:00:00:00:21");
    dp["Alias"] = BtValue::of_str("Synth Buds");
    dp["Paired"] = BtValue::of_bool(true);
    dp["Trusted"] = BtValue::of_bool(true);
    dp["Connected"] = BtValue::of_bool(true);
    dp["Class"] = BtValue::of_int(0x240404);
    dp["UUIDs"] = BtValue::of_list({kSink});
    dp["Adapter"] = BtValue::of_str("/org/bluez/hci0");
    bt_apply_device(d, dp);
    check(d.address == "02:00:00:00:00:21" && d.alias == "Synth Buds" && d.paired && d.trusted && d.connected &&
              d.cls == 0x240404 && d.uuids.size() == 1 && d.adapter == "/org/bluez/hci0",
          "device properties apply");
    BtProps part;
    part["Connected"] = BtValue::of_bool(false);
    bt_apply_device(d, part);
    check(!d.connected && d.paired && d.alias == "Synth Buds", "a partial change keeps the other properties");

    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
