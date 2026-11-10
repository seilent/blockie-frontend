#include "wifi_model.h"

#include <algorithm>
#include <cstring>

namespace fd {

namespace {

int hexv(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

bool printable(const std::string& s)
{
    for (unsigned char c : s)
        if (c < 0x20 || c > 0x7e)
            return false;
    return true;
}

std::vector<std::string> fields(const std::string& s, char sep)
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

bool blank(const std::string& s) { return s.find_first_not_of(" \t") == std::string::npos; }

std::string unquote(const std::string& v)
{
    return v.size() >= 2 && v.front() == '"' && v.back() == '"' ? v.substr(1, v.size() - 2) : v;
}

bool valid_text(const std::string& s)
{
    static const uint32_t kMin[] = {0, 0, 0x80, 0x800, 0x10000};
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = (unsigned char)s[i];
        uint32_t cp = 0;
        size_t n = 0;
        if (c < 0x80) {
            cp = c;
            n = 1;
        } else if ((c & 0xE0) == 0xC0) {
            cp = c & 0x1F;
            n = 2;
        } else if ((c & 0xF0) == 0xE0) {
            cp = c & 0x0F;
            n = 3;
        } else if ((c & 0xF8) == 0xF0) {
            cp = c & 0x07;
            n = 4;
        } else {
            return false;
        }
        if (i + n > s.size())
            return false;
        for (size_t k = 1; k < n; k++) {
            unsigned char d = (unsigned char)s[i + k];
            if ((d & 0xC0) != 0x80)
                return false;
            cp = (cp << 6) | (d & 0x3F);
        }
        if (cp < kMin[n] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
            return false;
        if (cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp < 0xA0))
            return false;
        i += n;
    }
    return true;
}

WifiSec ap_sec(const WifiAp& a) { return wifi_ap_sec(a.flags, a.wpa, a.rsn); }

int group(const WifiRow& r) { return r.connected ? 0 : !r.saved.empty() ? 1 : 2; }

bool activating(uint32_t st) { return st >= kNmDevicePrepare && st < kNmDeviceActivated; }

std::string sec_words(WifiSec sec)
{
    switch (sec) {
    case WifiSec::Open: return "open";
    case WifiSec::Psk: return "secured";
    case WifiSec::Unsupported: return "not supported";
    }
    return "";
}

std::vector<WifiRow> natural_rows(const WifiState& s, int64_t now)
{
    std::vector<WifiRow> nets;
    const WifiAp* active = wifi_ap(s, s.active_ap);
    std::string active_key = active ? wifi_key(active->ssid, ap_sec(*active)) : std::string();
    std::string join_key = s.joining ? wifi_key(s.join.ssid, s.join.sec) : std::string();
    for (const WifiAp& a : s.aps) {
        if (wifi_hidden(a.ssid))
            continue;
        WifiSec sec = ap_sec(a);
        auto it = std::find_if(nets.begin(), nets.end(),
                               [&](const WifiRow& r) { return r.ssid == a.ssid && r.sec == sec; });
        if (it != nets.end()) {
            if (a.strength > it->strength) {
                it->strength = a.strength;
                it->ap = a.path;
            }
            continue;
        }
        WifiRow r;
        r.kind = WifiRow::Network;
        r.ssid = a.ssid;
        r.sec = sec;
        r.strength = a.strength;
        r.ap = a.path;
        nets.push_back(r);
    }
    for (WifiRow& r : nets) {
        std::string key = wifi_key(r.ssid, r.sec);
        if (const WifiSaved* sv = wifi_saved(s, r.ssid, r.sec))
            r.saved = sv->path;
        bool here = !active_key.empty() && key == active_key;
        r.connected = here && s.dev_state == kNmDeviceActivated;
        bool connecting = !r.connected && (key == join_key || (here && activating(s.dev_state)));
        if (r.connected)
            r.state = "connected";
        else if (connecting && s.dev_state == kNmDeviceIpConfig && s.ip_since >= 0 &&
                 now - s.ip_since >= kWifiNoAddressMs)
            r.state = "no address";
        else if (connecting)
            r.state = "connecting";
        else if (key == s.failed)
            r.state = s.wrong ? "wrong password" : "not connected";
        else if (!r.saved.empty())
            r.state = "saved";
        else
            r.state = sec_words(r.sec);
    }
    std::stable_sort(nets.begin(), nets.end(), [](const WifiRow& a, const WifiRow& b) {
        if (group(a) != group(b))
            return group(a) < group(b);
        if (a.strength != b.strength)
            return a.strength > b.strength;
        return a.ssid < b.ssid;
    });
    return nets;
}

}

WifiSec wifi_ap_sec(uint32_t flags, uint32_t wpa, uint32_t rsn)
{
    uint32_t km = (wpa | rsn) & ~kNmKeyOweTm;
    if (!(flags & kNmApPrivacy) && km == 0)
        return WifiSec::Open;
    if (km & kNmKeyPsk)
        return WifiSec::Psk;
    return WifiSec::Unsupported;
}

WifiSec wifi_key_mgmt(bool has_security, const std::string& key_mgmt)
{
    if (!has_security)
        return WifiSec::Open;
    return key_mgmt == "wpa-psk" ? WifiSec::Psk : WifiSec::Unsupported;
}

std::string wifi_key(const std::string& ssid, WifiSec sec)
{
    return ssid + std::string(1, '\0') + std::to_string((int)sec);
}

bool wifi_hidden(const std::string& ssid)
{
    for (char c : ssid)
        if (c != '\0')
            return false;
    return true;
}

WifiLevel wifi_level(int strength)
{
    if (strength >= kWifiLevelFull)
        return WifiLevel::Full;
    return strength >= kWifiLevelMiddle ? WifiLevel::Middle : WifiLevel::Low;
}

std::string wifi_shown(const std::string& ssid)
{
    if (valid_text(ssid))
        return ssid;
    static const char kHex[] = "0123456789abcdef";
    std::string out;
    for (unsigned char c : ssid) {
        out += kHex[c >> 4];
        out += kHex[c & 0xF];
    }
    return out;
}

const WifiAp* wifi_ap(const WifiState& s, const std::string& path)
{
    if (path.empty())
        return nullptr;
    for (const WifiAp& a : s.aps)
        if (a.path == path)
            return &a;
    return nullptr;
}

const WifiSaved* wifi_saved(const WifiState& s, const std::string& ssid, WifiSec sec)
{
    for (const WifiSaved& sv : s.saved)
        if (!sv.temp && sv.ssid == ssid && sv.sec == sec)
            return &sv;
    return nullptr;
}

int wifi_saved_count(const WifiState& s) { return (int)wifi_saved_rows(s).size(); }

std::vector<WifiRow> wifi_rows(const WifiState& s, int64_t now)
{
    std::vector<WifiRow> v;
    bool avail = s.up && s.radio;
    std::vector<WifiRow> nets = avail ? natural_rows(s, now) : std::vector<WifiRow>();
    WifiRow top;
    if (!avail)
        top.state = "unavailable";
    else if (s.scanning)
        top.state = "scanning";
    else if (s.scan_failed)
        top.state = "scan failed";
    else if (s.scanned && nets.empty())
        top.state = "no networks";
    v.push_back(top);
    if (!s.order.empty()) {
        auto pos = [&](const WifiRow& r) {
            return (size_t)(std::find(s.order.begin(), s.order.end(), wifi_key(r.ssid, r.sec)) - s.order.begin());
        };
        std::stable_sort(nets.begin(), nets.end(), [&](const WifiRow& a, const WifiRow& b) { return pos(a) < pos(b); });
    }
    v.insert(v.end(), nets.begin(), nets.end());
    return v;
}

std::vector<WifiRow> wifi_saved_rows(const WifiState& s)
{
    std::vector<WifiRow> v;
    const WifiAp* active = wifi_ap(s, s.active_ap);
    for (const WifiSaved& sv : s.saved) {
        if (sv.temp)
            continue;
        bool dup = std::any_of(v.begin(), v.end(),
                               [&](const WifiRow& r) { return r.ssid == sv.ssid && r.sec == sv.sec; });
        if (dup)
            continue;
        WifiRow r;
        r.kind = WifiRow::Network;
        r.ssid = sv.ssid;
        r.sec = sv.sec;
        r.saved = sv.path;
        int best = -1;
        for (const WifiAp& a : s.aps)
            if (a.ssid == sv.ssid && ap_sec(a) == sv.sec && a.strength > best) {
                best = a.strength;
                r.ap = a.path;
            }
        r.strength = std::max(best, 0);
        r.state = best >= 0 ? "in range" : "not in range";
        r.connected = active && active->ssid == sv.ssid && ap_sec(*active) == sv.sec &&
                      s.dev_state == kNmDeviceActivated;
        v.push_back(r);
    }
    std::stable_sort(v.begin(), v.end(), [](const WifiRow& a, const WifiRow& b) { return a.ssid < b.ssid; });
    return v;
}

std::vector<std::string> wifi_order(const WifiState& s, int64_t now)
{
    std::vector<std::string> keys;
    for (const WifiRow& r : natural_rows(s, now))
        keys.push_back(wifi_key(r.ssid, r.sec));
    return keys;
}

int64_t wifi_tick_at(const WifiState& s, int64_t now)
{
    if (s.dev_state == kNmDeviceIpConfig && s.ip_since >= 0 && now < s.ip_since + kWifiNoAddressMs)
        return s.ip_since + kWifiNoAddressMs;
    return -1;
}

bool wifi_psk_ok(const std::string& psk)
{
    if (psk.size() == 64) {
        for (char c : psk)
            if (hexv(c) < 0)
                return false;
        return true;
    }
    return psk.size() >= 8 && psk.size() <= 63 && printable(psk);
}

int wifi_txt(const std::string& text, std::vector<WifiTxtNet>& out)
{
    out.clear();
    std::string t = text.compare(0, 3, "\xef\xbb\xbf") == 0 ? text.substr(3) : text;
    std::vector<std::string> lines = fields(t, '\n');
    bool open = false, pass = false;
    for (size_t i = 0; i < lines.size(); i++) {
        std::string l = lines[i];
        if (!l.empty() && l.back() == '\r')
            l.pop_back();
        int n = (int)i + 1;
        if (blank(l)) {
            open = false;
            continue;
        }
        if (l.compare(0, 5, "ssid=") == 0) {
            std::string v = unquote(l.substr(5));
            if (v.empty() || v.size() > 32)
                return n;
            out.push_back({v, ""});
            open = true;
            pass = false;
        } else if (l.compare(0, 9, "password=") == 0) {
            std::string v = unquote(l.substr(9));
            if (!open || pass || (!v.empty() && !wifi_psk_ok(v)))
                return n;
            out.back().psk = v;
            pass = true;
        } else {
            return n;
        }
    }
    return out.empty() ? 1 : 0;
}

std::vector<WifiTxtNet> wifi_txt_unique(const std::vector<WifiTxtNet>& nets)
{
    std::vector<WifiTxtNet> out;
    out.reserve(nets.size());
    for (const WifiTxtNet& n : nets) {
        auto it = std::find_if(out.begin(), out.end(), [&](const WifiTxtNet& o) {
            return o.ssid == n.ssid && o.psk.empty() == n.psk.empty();
        });
        if (it == out.end()) {
            out.push_back(n);
            continue;
        }
        wifi_wipe(it->psk);
        it->psk = n.psk;
    }
    return out;
}

std::string wifi_error_word(const std::string& name)
{
    auto ends = [&](const char* tail) {
        size_t n = std::strlen(tail);
        return name.size() >= n && name.compare(name.size() - n, n, tail) == 0;
    };
    if (ends(".PermissionDenied") || ends(".AccessDenied") || ends(".NotAuthorized"))
        return "denied";
    if (ends(".NoReply") || ends(".Timeout") || ends(".TimedOut"))
        return "timed out";
    if (name.empty() || ends(".ServiceUnknown") || ends(".NameHasNoOwner") || ends(".Disconnected"))
        return "unavailable";
    return "failed";
}

std::string wifi_networks_saved(int n) { return n == 1 ? "1 network saved" : std::to_string(n) + " networks saved"; }

bool wifi_radio_shown(const WifiState& s) { return s.up && s.radio_ok; }

void wifi_wipe(std::string& secret)
{
    std::fill(secret.begin(), secret.end(), '\0');
    secret.clear();
}

}
