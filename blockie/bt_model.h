#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace fd {

struct BtValue {
    enum Kind { None, Bool, Int, Str, List };
    Kind kind = None;
    bool b = false;
    int64_t i = 0;
    std::string s;
    std::vector<std::string> list;

    static BtValue of_bool(bool v);
    static BtValue of_int(int64_t v);
    static BtValue of_str(const std::string& v);
    static BtValue of_list(const std::vector<std::string>& v);
};

using BtProps = std::map<std::string, BtValue>;

struct BtDevice {
    std::string path, adapter, address, alias, name;
    bool paired = false, trusted = false, connected = false;
    uint32_t cls = 0;
    std::vector<std::string> uuids;

    bool operator==(const BtDevice& o) const;
    bool operator!=(const BtDevice& o) const { return !(*this == o); }
};

struct BtAdapter {
    std::string path;
    bool powered = false, discovering = false, pairable = false;
    uint32_t pairable_timeout = 0;

    bool operator==(const BtAdapter& o) const;
    bool operator!=(const BtAdapter& o) const { return !(*this == o); }
};

struct BtPrompt {
    enum Kind { None, Confirm, Passkey, Pin };
    Kind kind = None;
    std::string path, pin;
    uint32_t passkey = 0;

    bool operator==(const BtPrompt& o) const;
    bool operator!=(const BtPrompt& o) const { return !(*this == o); }
};

struct BtState {
    bool up = false;
    BtAdapter adapter;
    std::vector<BtDevice> devices;
    bool searching = false;
    int64_t search_end = 0;
    std::string pair_target, pair_failed, connect_target, connect_failed;
    BtPrompt prompt;
};

struct BtCmd {
    enum Type { Search, StopSearch, Pair, Connect, Disconnect, Forget, PowerOn, Answer, Dismiss };
    Type type = Search;
    std::string path;
    bool yes = false;
};

struct BtRow {
    enum Kind { Search, Power, Paired, Found };
    Kind kind = Search;
    std::string path, name, state;
    bool connected = false;

    bool operator==(const BtRow& o) const;
    bool operator!=(const BtRow& o) const { return !(*this == o); }
};

constexpr int kBtSearchS = 120;

void bt_apply_device(BtDevice& d, const BtProps& p);
void bt_apply_adapter(BtAdapter& a, const BtProps& p);
const BtDevice* bt_device(const BtState& s, const std::string& path);
std::string bt_name(const BtDevice& d);
bool bt_audio_sink(const BtDevice& d);
bool bt_gamepad(const BtDevice& d);
bool bt_listed(const BtDevice& d);
bool bt_available(const BtState& s);
int bt_seconds(const BtState& s, int64_t now);
int64_t bt_tick_at(const BtState& s, int64_t now);
std::string bt_subtitle(const BtState& s, int64_t now);
std::vector<BtRow> bt_rows(const BtState& s);
uint64_t bt_row_key(const BtRow& r);
int bt_refocus(const std::vector<BtRow>& was, int focus, const std::vector<BtRow>& rows);

}
