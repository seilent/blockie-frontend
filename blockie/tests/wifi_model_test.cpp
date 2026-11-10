#include <cstdio>
#include <string>
#include <vector>

#include "../wifi_model.h"

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

const std::string kHome = "blockie-test-home";
const std::string kCafe = "blockie-test-cafe";
const std::string kWep = "blockie-test-wep";
const std::string kOffice = "blockie-test-office";
const std::string kGone = "blockie-test-gone";
const std::string kLate = "blockie-test-late";
constexpr uint32_t kPskCcmp = 0x188;

WifiAp ap(const std::string& n, const std::string& ssid, int strength, uint32_t flags, uint32_t rsn)
{
    WifiAp a;
    a.path = "/org/freedesktop/NetworkManager/AccessPoint/" + n;
    a.ssid = ssid;
    a.strength = strength;
    a.flags = flags;
    a.rsn = rsn;
    return a;
}

WifiState base()
{
    WifiState s;
    s.up = true;
    s.radio = true;
    s.device = "/org/freedesktop/NetworkManager/Devices/3";
    s.dev_state = kNmDeviceDisconnected;
    s.aps = {ap("1", kHome, 80, 1, kPskCcmp), ap("2", kCafe, 60, 0, 0), ap("3", kWep, 50, 1, 0),
             ap("4", kHome, 40, 1, kPskCcmp), ap("5", "", 70, 1, kPskCcmp), ap("6", kOffice, 30, 1, kPskCcmp)};
    s.saved = {{"/org/freedesktop/NetworkManager/Settings/1", kOffice, WifiSec::Psk, false},
               {"/org/freedesktop/NetworkManager/Settings/2", kCafe, WifiSec::Open, true}};
    return s;
}

std::string names(const std::vector<WifiRow>& rows)
{
    std::string out;
    for (const WifiRow& r : rows)
        out += (r.kind == WifiRow::Scan ? std::string("scan") : r.ssid) + "=" + r.state + ";";
    return out;
}

const WifiRow* row(const std::vector<WifiRow>& rows, const std::string& ssid)
{
    for (const WifiRow& r : rows)
        if (r.kind == WifiRow::Network && r.ssid == ssid)
            return &r;
    return nullptr;
}

}

int main()
{
    check(wifi_ap_sec(0, 0, 0) == WifiSec::Open, "no privacy, no key management: open");
    check(wifi_ap_sec(1, 0, kPskCcmp) == WifiSec::Psk && wifi_ap_sec(1, kPskCcmp, 0) == WifiSec::Psk,
          "psk in wpa or rsn flags");
    check(wifi_ap_sec(1, 0, 0) == WifiSec::Unsupported, "privacy with no wpa: wep, not supported");
    check(wifi_ap_sec(1, 0, 0x288) == WifiSec::Unsupported, "802.1x: not supported");
    check(wifi_ap_sec(1, 0, 0x488) == WifiSec::Unsupported, "sae only: not supported");
    check(wifi_ap_sec(1, 0, 0x588) == WifiSec::Psk, "psk and sae mixed joins as psk");
    check(wifi_ap_sec(0, 0, 0x1000) == WifiSec::Open, "the open half of an owe transition pair is open");
    check(wifi_key_mgmt(false, "") == WifiSec::Open && wifi_key_mgmt(true, "wpa-psk") == WifiSec::Psk &&
              wifi_key_mgmt(true, "sae") == WifiSec::Unsupported && wifi_key_mgmt(true, "none") == WifiSec::Unsupported,
          "saved profile security from key-mgmt");
    check(wifi_level(0) == WifiLevel::Low && wifi_level(49) == WifiLevel::Low && wifi_level(50) == WifiLevel::Middle &&
              wifi_level(66) == WifiLevel::Middle && wifi_level(67) == WifiLevel::Full &&
              wifi_level(100) == WifiLevel::Full,
          "signal levels: full at 67 and above, middle 50 to 66, low under 50");
    check(wifi_hidden("") && wifi_hidden(std::string(3, '\0')) && !wifi_hidden("a"),
          "empty and all zero ssids are hidden");
    check(wifi_shown(kHome) == kHome && wifi_shown("\xe3\x81\x82") == "\xe3\x81\x82", "valid utf-8 draws as text");
    check(wifi_shown("\xff\xfe") == "fffe" && wifi_shown(std::string("\x01" "ab")) == "016162" &&
              wifi_shown("\xc0\xaf") == "c0af" && wifi_shown("\xed\xa0\x80") == "eda080",
          "invalid utf-8, controls, overlongs and surrogates draw as hex");

    WifiState off = base();
    off.radio = false;
    std::vector<WifiRow> rows = wifi_rows(off, 0);
    check(rows.size() == 1 && rows[0].kind == WifiRow::Scan && rows[0].state == "unavailable",
          "radio off: only the scan row, unavailable");
    off = base();
    off.up = false;
    check(wifi_rows(off, 0).size() == 1 && wifi_rows(off, 0)[0].state == "unavailable", "no nm: unavailable");

    WifiState s = base();
    rows = wifi_rows(s, 0);
    check(names(rows) == "scan=;" + kOffice + "=saved;" + kHome + "=secured;" + kCafe + "=open;" + kWep +
                             "=not supported;",
          "saved in range first, then the others by signal; hidden not listed; volatile not saved");
    const WifiRow* home = row(rows, kHome);
    check(home && home->strength == 80 && home->ap == "/org/freedesktop/NetworkManager/AccessPoint/1" &&
              home->sec == WifiSec::Psk,
          "one row per ssid and security, the strongest ap");
    check(row(rows, kOffice)->saved == "/org/freedesktop/NetworkManager/Settings/1" && row(rows, kCafe)->saved.empty(),
          "rows carry the saved profile");

    s.active_ap = "/org/freedesktop/NetworkManager/AccessPoint/2";
    s.dev_state = kNmDeviceActivated;
    rows = wifi_rows(s, 0);
    check(rows[1].ssid == kCafe && rows[1].state == "connected" && rows[1].connected, "connected first");
    s.dev_state = 50;
    rows = wifi_rows(s, 0);
    check(row(rows, kCafe)->state == "connecting" && !row(rows, kCafe)->connected, "activating: connecting");
    s.dev_state = kNmDeviceIpConfig;
    s.ip_since = 1000;
    rows = wifi_rows(s, 1000 + kWifiNoAddressMs - 1);
    check(row(rows, kCafe)->state == "connecting" && wifi_tick_at(s, 2000) == 1000 + kWifiNoAddressMs,
          "ip config under 15 s: connecting, a tick at 15 s");
    rows = wifi_rows(s, 1000 + kWifiNoAddressMs);
    check(row(rows, kCafe)->state == "no address" && wifi_tick_at(s, 1000 + kWifiNoAddressMs) == -1,
          "15 s associated with no lease: no address");

    s = base();
    s.joining = true;
    s.join.ssid = kHome;
    s.join.sec = WifiSec::Psk;
    check(row(wifi_rows(s, 0), kHome)->state == "connecting", "the join target reads connecting at once");
    s.joining = false;
    s.failed = wifi_key(kHome, WifiSec::Psk);
    s.wrong = true;
    check(row(wifi_rows(s, 0), kHome)->state == "wrong password", "a wrong key reads wrong password");
    s.wrong = false;
    check(row(wifi_rows(s, 0), kHome)->state == "not connected", "another failure reads not connected");
    s.failed = wifi_key(kHome, WifiSec::Open);
    check(row(wifi_rows(s, 0), kHome)->state == "secured", "a failure keys on ssid and security");

    s = base();
    s.order = wifi_order(s, 0);
    s.aps[1].strength = 95;
    s.aps.push_back(ap("7", kLate, 99, 0, 0));
    rows = wifi_rows(s, 0);
    check(names(rows) == "scan=;" + kOffice + "=saved;" + kHome + "=secured;" + kCafe + "=open;" + kWep +
                             "=not supported;" + kLate + "=open;",
          "between scans the order of the last scan holds, new rows go after");
    s.order = wifi_order(s, 0);
    rows = wifi_rows(s, 0);
    check(rows[2].ssid == kLate && rows[3].ssid == kCafe, "the next finished scan takes the new order");

    s = base();
    s.scanning = true;
    check(wifi_rows(s, 0)[0].state == "scanning", "scan row: scanning");
    s.scanning = false;
    s.scan_failed = true;
    check(wifi_rows(s, 0)[0].state == "scan failed", "scan row: scan failed");
    s.scan_failed = false;
    s.scanned = true;
    s.aps.clear();
    check(wifi_rows(s, 0).size() == 1 && wifi_rows(s, 0)[0].state == "no networks", "scan row: no networks");

    s = base();
    s.saved.push_back({"/org/freedesktop/NetworkManager/Settings/3", kGone, WifiSec::Psk, false});
    s.saved.push_back({"/org/freedesktop/NetworkManager/Settings/4", kOffice, WifiSec::Psk, false});
    std::vector<WifiRow> saved = wifi_saved_rows(s);
    check(saved.size() == 2 && saved[0].ssid == kGone && saved[0].state == "not in range" && saved[1].ssid == kOffice &&
              saved[1].state == "in range" && saved[1].ap == "/org/freedesktop/NetworkManager/AccessPoint/6",
          "saved rows: in range or not, a profile twice listed once, volatile left out");
    check(wifi_saved_count(s) == 2, "saved count");

    std::vector<WifiTxtNet> nets;
    check(wifi_txt("ssid=" + kHome + "\npassword=blockie-test-psk-01\n\nssid=" + kCafe + "\n", nets) == 0 &&
              nets.size() == 2 && nets[0].ssid == kHome && nets[0].psk == "blockie-test-psk-01" &&
              nets[1].ssid == kCafe && nets[1].psk.empty(),
          "wifi.txt: a secured and an open network");
    check(wifi_txt("\xef\xbb\xbfssid=\"" + kHome + "\"\r\npassword=\"blockie-test-psk-01\"\r\n", nets) == 0 &&
              nets.size() == 1 && nets[0].ssid == kHome && nets[0].psk == "blockie-test-psk-01",
          "wifi.txt: bom, crlf and one pair of quotes");
    check(wifi_txt("ssid=" + kHome + "\npassword=\n", nets) == 0 && nets[0].psk.empty(), "an empty password is open");
    check(wifi_txt("ssid=a=b\npassword=x=yyyyyyyy\n", nets) == 0 && nets[0].ssid == "a=b" &&
              nets[0].psk == "x=yyyyyyyy",
          "everything after the first = is the value");
    check(wifi_txt("ssid=" + kHome + "\nssid=" + kCafe + "\n", nets) == 0 && nets.size() == 2,
          "the next ssid ends a block");
    check(wifi_txt(std::string("ssid=") + kHome + "\npassword=" + std::string(64, 'a') + "\n", nets) == 0,
          "64 hex digits are a key");
    check(wifi_txt("password=blockie-test-psk-01\n", nets) == 1, "a password with no ssid refuses at its line");
    check(wifi_txt("ssid=" + kHome + "\npassword=blockie-test-psk-01\npassword=blockie-test-psk-02\n", nets) == 3,
          "a second password refuses");
    check(wifi_txt("ssid=" + kHome + "\n\npassword=blockie-test-psk-01\n", nets) == 3, "a blank line ends the block");
    check(wifi_txt("ssid=" + kHome + "\npassword=short\n", nets) == 2, "a 5 character password refuses");
    check(wifi_txt("ssid=" + kHome + "\npassword=" + std::string(64, 'g') + "\n", nets) == 2,
          "64 characters that are not hex refuse");
    check(wifi_txt("ssid=" + kHome + "\npassword=blockie-test-\x01psk\n", nets) == 2, "a control character refuses");
    check(wifi_txt("ssid=" + std::string(33, 'x') + "\n", nets) == 1 && wifi_txt("ssid=\n", nets) == 1,
          "an ssid of 0 or 33 bytes refuses");
    check(wifi_txt("ssid=" + kHome + "\nchannel=6\n", nets) == 2, "any other line refuses");
    check(wifi_txt("", nets) == 1 && wifi_txt("\n\n", nets) == 1, "no network refuses at line 1");

    std::vector<WifiTxtNet> twice = {{kHome, "blockie-test-psk-01"}, {kCafe, ""}, {kHome, "blockie-test-psk-02"},
                                     {kHome, ""}};
    std::vector<WifiTxtNet> uniq = wifi_txt_unique(twice);
    check(uniq.size() == 3 && uniq[0].ssid == kHome && uniq[0].psk == "blockie-test-psk-02" && uniq[1].ssid == kCafe &&
              uniq[2].ssid == kHome && uniq[2].psk.empty(),
          "a name twice with the same security keeps the last password");

    check(wifi_error_word("org.freedesktop.NetworkManager.PermissionDenied") == "denied" &&
              wifi_error_word("org.freedesktop.NetworkManager.Settings.PermissionDenied") == "denied" &&
              wifi_error_word("org.freedesktop.DBus.Error.NoReply") == "timed out" &&
              wifi_error_word("org.freedesktop.DBus.Error.ServiceUnknown") == "unavailable" &&
              wifi_error_word("org.freedesktop.NetworkManager.Failed") == "failed",
          "error words");
    check(wifi_networks_saved(1) == "1 network saved" && wifi_networks_saved(3) == "3 networks saved", "saved words");
    {
        WifiState w;
        bool none = !wifi_radio_shown(w);
        w.up = true;
        bool no_grant = !wifi_radio_shown(w);
        w.radio_ok = true;
        bool shown = wifi_radio_shown(w);
        w.up = false;
        check(none && no_grant && shown && !wifi_radio_shown(w),
              "the wi-fi row shows only with wlan0 from NM and enable-disable-wifi yes");
    }
    std::string secret = "blockie-test-psk-01";
    wifi_wipe(secret);
    check(secret.empty(), "wipe clears the secret");

    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
