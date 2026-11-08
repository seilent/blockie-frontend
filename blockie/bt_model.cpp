#include "bt_model.h"

#include <algorithm>
#include <cctype>

namespace fd {

namespace {

const char* const kSinkUuid = "0000110b-0000-1000-8000-00805f9b34fb";
const char* const kHidUuid = "00001124-0000-1000-8000-00805f9b34fb";
const char* const kHogpUuid = "00001812-0000-1000-8000-00805f9b34fb";
constexpr uint32_t kMajorAudio = 0x04;
constexpr uint32_t kMajorPeripheral = 0x05;

std::string lower(std::string s)
{
    for (char& ch : s)
        ch = (char)std::tolower((unsigned char)ch);
    return s;
}

bool has_uuid(const BtDevice& d, const char* uuid)
{
    for (const std::string& u : d.uuids)
        if (lower(u) == uuid)
            return true;
    return false;
}

uint32_t major_class(const BtDevice& d) { return (d.cls >> 8) & 0x1F; }

uint64_t hash_path(const std::string& s)
{
    uint64_t h = 1469598103934665603ull;
    for (unsigned char ch : s)
        h = (h ^ ch) * 1099511628211ull;
    return h | 1;
}

void str(const BtProps& p, const char* k, std::string& out)
{
    auto it = p.find(k);
    if (it != p.end() && it->second.kind == BtValue::Str)
        out = it->second.s;
}

void flag(const BtProps& p, const char* k, bool& out)
{
    auto it = p.find(k);
    if (it != p.end() && it->second.kind == BtValue::Bool)
        out = it->second.b;
}

void num(const BtProps& p, const char* k, uint32_t& out)
{
    auto it = p.find(k);
    if (it != p.end() && it->second.kind == BtValue::Int)
        out = (uint32_t)it->second.i;
}

void list(const BtProps& p, const char* k, std::vector<std::string>& out)
{
    auto it = p.find(k);
    if (it != p.end() && it->second.kind == BtValue::List)
        out = it->second.list;
}

std::string paired_state(const BtState& s, const BtDevice& d)
{
    if (d.path == s.connect_target)
        return "connecting";
    if (d.path == s.connect_failed)
        return "connect failed";
    return d.connected ? "connected" : "";
}

std::string found_state(const BtState& s, const BtDevice& d)
{
    if (d.path == s.pair_target)
        return "pairing";
    return d.path == s.pair_failed ? "pair failed" : "not paired";
}

}

BtValue BtValue::of_bool(bool v)
{
    BtValue x;
    x.kind = Bool;
    x.b = v;
    return x;
}

BtValue BtValue::of_int(int64_t v)
{
    BtValue x;
    x.kind = Int;
    x.i = v;
    return x;
}

BtValue BtValue::of_str(const std::string& v)
{
    BtValue x;
    x.kind = Str;
    x.s = v;
    return x;
}

BtValue BtValue::of_list(const std::vector<std::string>& v)
{
    BtValue x;
    x.kind = List;
    x.list = v;
    return x;
}

bool BtDevice::operator==(const BtDevice& o) const
{
    return path == o.path && adapter == o.adapter && address == o.address && alias == o.alias && name == o.name &&
           paired == o.paired && trusted == o.trusted && connected == o.connected && cls == o.cls && uuids == o.uuids;
}

bool BtAdapter::operator==(const BtAdapter& o) const
{
    return path == o.path && powered == o.powered && discovering == o.discovering && pairable == o.pairable &&
           pairable_timeout == o.pairable_timeout;
}

bool BtPrompt::operator==(const BtPrompt& o) const
{
    return kind == o.kind && path == o.path && pin == o.pin && passkey == o.passkey;
}

bool BtRow::operator==(const BtRow& o) const
{
    return kind == o.kind && path == o.path && name == o.name && state == o.state && connected == o.connected;
}

void bt_apply_device(BtDevice& d, const BtProps& p)
{
    str(p, "Adapter", d.adapter);
    str(p, "Address", d.address);
    str(p, "Alias", d.alias);
    str(p, "Name", d.name);
    flag(p, "Paired", d.paired);
    flag(p, "Trusted", d.trusted);
    flag(p, "Connected", d.connected);
    num(p, "Class", d.cls);
    list(p, "UUIDs", d.uuids);
}

void bt_apply_adapter(BtAdapter& a, const BtProps& p)
{
    flag(p, "Powered", a.powered);
    flag(p, "Discovering", a.discovering);
    flag(p, "Pairable", a.pairable);
    num(p, "PairableTimeout", a.pairable_timeout);
}

const BtDevice* bt_device(const BtState& s, const std::string& path)
{
    for (const BtDevice& d : s.devices)
        if (d.path == path)
            return &d;
    return nullptr;
}

std::string bt_name(const BtDevice& d)
{
    if (!d.alias.empty())
        return d.alias;
    return d.name.empty() ? d.address : d.name;
}

bool bt_audio_sink(const BtDevice& d) { return has_uuid(d, kSinkUuid) || major_class(d) == kMajorAudio; }

bool bt_gamepad(const BtDevice& d)
{
    return has_uuid(d, kHidUuid) || has_uuid(d, kHogpUuid) || major_class(d) == kMajorPeripheral;
}

bool bt_listed(const BtDevice& d) { return bt_audio_sink(d) || bt_gamepad(d); }

bool bt_available(const BtState& s) { return s.up && !s.adapter.path.empty(); }

int bt_seconds(const BtState& s, int64_t now)
{
    if (!s.searching)
        return 0;
    return (int)std::max<int64_t>(0, (s.search_end - now + 999) / 1000);
}

int64_t bt_tick_at(const BtState& s, int64_t now)
{
    if (!s.searching)
        return -1;
    int secs = bt_seconds(s, now);
    return secs <= 1 ? s.search_end : s.search_end - (int64_t)(secs - 1) * 1000;
}

std::string bt_subtitle(const BtState& s, int64_t now)
{
    if (!bt_available(s) || !s.adapter.powered)
        return "unavailable";
    for (const BtRow& r : bt_rows(s))
        if (r.kind == BtRow::Paired && r.connected)
            return r.name;
    if (s.searching)
        return "searching " + std::to_string(bt_seconds(s, now));
    return "not connected";
}

std::vector<BtRow> bt_rows(const BtState& s)
{
    std::vector<BtRow> v;
    bool avail = bt_available(s);
    BtRow top;
    if (avail && !s.adapter.powered) {
        top.kind = BtRow::Power;
        top.name = "bluetooth";
        top.state = "off";
    } else {
        top.kind = BtRow::Search;
        top.name = "search";
        top.state = !avail ? "unavailable" : s.searching ? "searching" : "";
    }
    v.push_back(top);
    if (!avail)
        return v;
    std::vector<const BtDevice*> paired, found;
    for (const BtDevice& d : s.devices)
        if (bt_listed(d))
            (d.paired ? paired : found).push_back(&d);
    auto order = [](const BtDevice* a, const BtDevice* b) {
        std::string la = lower(bt_name(*a)), lb = lower(bt_name(*b));
        return la != lb ? la < lb : a->address < b->address;
    };
    std::stable_sort(paired.begin(), paired.end(), order);
    std::stable_sort(found.begin(), found.end(), order);
    for (const BtDevice* d : paired) {
        BtRow r;
        r.kind = BtRow::Paired;
        r.path = d->path;
        r.name = bt_name(*d);
        r.connected = d->connected;
        r.state = paired_state(s, *d);
        v.push_back(r);
    }
    for (const BtDevice* d : found) {
        BtRow r;
        r.kind = BtRow::Found;
        r.path = d->path;
        r.name = bt_name(*d);
        r.state = found_state(s, *d);
        v.push_back(r);
    }
    return v;
}

uint64_t bt_row_key(const BtRow& r)
{
    if (r.kind == BtRow::Search || r.kind == BtRow::Power)
        return 1;
    return hash_path(r.path) + 1;
}

int bt_refocus(const std::vector<BtRow>& was, int focus, const std::vector<BtRow>& rows)
{
    if (rows.empty())
        return 0;
    if (focus >= 0 && focus < (int)was.size()) {
        uint64_t key = bt_row_key(was[(size_t)focus]);
        for (size_t i = 0; i < rows.size(); i++)
            if (bt_row_key(rows[i]) == key)
                return (int)i;
    }
    return std::clamp(focus, 0, (int)rows.size() - 1);
}

}
