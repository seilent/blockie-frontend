#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fd {

enum class WifiSec : uint8_t { Open, Psk, Unsupported };
enum class WifiLevel : uint8_t { Low, Middle, Full };

constexpr uint32_t kNmDeviceUnavailable = 20;
constexpr uint32_t kNmDeviceDisconnected = 30;
constexpr uint32_t kNmDevicePrepare = 40;
constexpr uint32_t kNmDeviceIpConfig = 70;
constexpr uint32_t kNmDeviceActivated = 100;
constexpr uint32_t kNmDeviceFailed = 120;
constexpr uint32_t kNmReasonNoSecrets = 7;
constexpr uint32_t kNmReasonSupplicantDisconnect = 8;
constexpr uint32_t kNmApPrivacy = 0x1;
constexpr uint32_t kNmKeyPsk = 0x100;
constexpr uint32_t kNmKeyOweTm = 0x1000;
constexpr uint32_t kNmConnVolatile = 0x4;
constexpr int kWifiLevelFull = 67;
constexpr int kWifiLevelMiddle = 50;
constexpr int64_t kWifiNoAddressMs = 15000;
constexpr size_t kWifiTxtMax = 65536;

struct WifiAp {
    std::string path, ssid;
    uint32_t flags = 0, wpa = 0, rsn = 0, freq = 0;
    int strength = 0;
};

struct WifiSaved {
    std::string path, ssid;
    WifiSec sec = WifiSec::Open;
    bool temp = false;
};

struct WifiJoin {
    uint32_t id = 0;
    std::string ssid, conn, active;
    WifiSec sec = WifiSec::Open;
    bool fresh = false, replied = false, began = false, landed = false, retried = false;
    int64_t until = 0;
};

struct WifiNote {
    uint32_t seq = 0;
    std::string title, text;
};

struct WifiState {
    bool up = false, radio = false, scanning = false, scan_failed = false, scanned = false;
    std::string device, active_ap;
    uint32_t dev_state = 0;
    int64_t ip_since = -1;
    std::vector<WifiAp> aps;
    std::vector<WifiSaved> saved;
    std::vector<std::string> order;
    bool joining = false;
    WifiJoin join;
    std::string failed;
    bool wrong = false;
    WifiNote note;
};

struct WifiCmd {
    enum Type { Scan, Join, Connect, Disconnect, Forget, Radio };
    Type type = Scan;
    std::string ssid, psk;
    WifiSec sec = WifiSec::Open;
    bool on = false;
};

struct WifiRow {
    enum Kind { Scan, Network };
    Kind kind = Scan;
    std::string ssid, ap, saved, state;
    WifiSec sec = WifiSec::Open;
    int strength = 0;
    bool connected = false;
};

struct WifiTxtNet {
    std::string ssid, psk;
};

WifiSec wifi_ap_sec(uint32_t flags, uint32_t wpa, uint32_t rsn);
WifiSec wifi_key_mgmt(bool has_security, const std::string& key_mgmt);
std::string wifi_key(const std::string& ssid, WifiSec sec);
bool wifi_hidden(const std::string& ssid);
WifiLevel wifi_level(int strength);
std::string wifi_shown(const std::string& ssid);
const WifiAp* wifi_ap(const WifiState& s, const std::string& path);
const WifiSaved* wifi_saved(const WifiState& s, const std::string& ssid, WifiSec sec);
int wifi_saved_count(const WifiState& s);
std::vector<WifiRow> wifi_rows(const WifiState& s, int64_t now);
std::vector<WifiRow> wifi_saved_rows(const WifiState& s);
std::vector<std::string> wifi_order(const WifiState& s, int64_t now);
int64_t wifi_tick_at(const WifiState& s, int64_t now);
bool wifi_psk_ok(const std::string& psk);
int wifi_txt(const std::string& text, std::vector<WifiTxtNet>& out);
std::vector<WifiTxtNet> wifi_txt_unique(const std::vector<WifiTxtNet>& nets);
std::string wifi_error_word(const std::string& name);
std::string wifi_networks_saved(int n);
void wifi_wipe(std::string& secret);

}
