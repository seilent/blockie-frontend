#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

#include <dbus/dbus.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../bus.h"
#include "../wifi.h"

using namespace fd;

namespace {

int g_fail = 0;
int g_total = 0;
DBusConnection* g_ctl = nullptr;

const char* const kNmName = "org.freedesktop.NetworkManager";
const char* const kTest = "net.seilent.Test.FakeNm";
const std::string kHome = "blockie-test-home";
const std::string kCafe = "blockie-test-cafe";
const std::string kOffice = "blockie-test-office";
const std::string kLate = "blockie-test-late";
const std::string kTxtNew = "blockie-test-txt-new";
const std::string kTwice = "blockie-test-twice";
const std::string kSettings = "/org/freedesktop/NetworkManager/Settings/";
const std::string kAp = "/org/freedesktop/NetworkManager/AccessPoint/";

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

void test_call(const char* member, const char* a = nullptr, const char* b = nullptr)
{
    DBusMessage* m = dbus_message_new_method_call(kNmName, "/test", kTest, member);
    if (a && b)
        dbus_message_append_args(m, DBUS_TYPE_STRING, &a, DBUS_TYPE_STRING, &b, DBUS_TYPE_INVALID);
    else if (a)
        dbus_message_append_args(m, DBUS_TYPE_STRING, &a, DBUS_TYPE_INVALID);
    if (DBusMessage* r = ctl(m))
        dbus_message_unref(r);
}

void hold(bool on)
{
    DBusMessage* m = dbus_message_new_method_call(kNmName, "/test", kTest, "Hold");
    dbus_bool_t b = on ? TRUE : FALSE;
    dbus_message_append_args(m, DBUS_TYPE_BOOLEAN, &b, DBUS_TYPE_INVALID);
    if (DBusMessage* r = ctl(m))
        dbus_message_unref(r);
}

std::vector<std::string> strings(const char* member)
{
    std::vector<std::string> v;
    DBusMessage* r = ctl(dbus_message_new_method_call(kNmName, "/test", kTest, member));
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

std::vector<std::string> since(size_t mark)
{
    std::vector<std::string> log = strings("Log");
    return mark < log.size() ? std::vector<std::string>(log.begin() + (long)mark, log.end())
                             : std::vector<std::string>();
}

size_t log_size() { return strings("Log").size(); }

int find_prefix(const std::vector<std::string>& log, const std::string& prefix, size_t from = 0)
{
    for (size_t i = from; i < log.size(); i++)
        if (log[i].compare(0, prefix.size(), prefix) == 0)
            return (int)i;
    return -1;
}

int count_prefix(const std::vector<std::string>& log, const std::string& prefix)
{
    int n = 0;
    for (const std::string& l : log)
        n += l.compare(0, prefix.size(), prefix) == 0;
    return n;
}

int profiles(const char* member, const std::string& ssid, const std::string& psk = "")
{
    int n = 0;
    for (const std::string& l : strings(member))
        if (l.compare(0, ssid.size() + 1, ssid + "|") == 0 &&
            (psk.empty() || l.find("|" + psk) != std::string::npos))
            n++;
    return n;
}

int disk_count(const std::string& ssid, const std::string& psk = "") { return profiles("Disk", ssid, psk); }

int conn_count(const std::string& ssid) { return profiles("Conns", ssid); }

bool write_file(const std::string& path, const std::string& body)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f)
        return false;
    bool ok = std::fwrite(body.data(), 1, body.size(), f) == body.size();
    return std::fclose(f) == 0 && ok;
}

std::string read_file(const std::string& path)
{
    std::string out;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f)
        return out;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
        out.append(buf, n);
    std::fclose(f);
    return out;
}

struct Rig {
    Bus bus;
    Wifi wifi;

    explicit Rig(const std::string& txt) : wifi(bus, txt) {}

    void step(int64_t now)
    {
        bus.pump();
        bus.tick(now);
        wifi.tick(now);
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

    void settle(int ms)
    {
        until([] { return false; }, ms);
    }

    void run(WifiCmd::Type t, const std::string& ssid = "", WifiSec sec = WifiSec::Open, const std::string& psk = "",
             bool on = false)
    {
        WifiCmd c;
        c.type = t;
        c.ssid = ssid;
        c.sec = sec;
        c.psk = psk;
        c.on = on;
        wifi.run(c, now_ms());
    }

    std::vector<WifiRow> rows() const { return wifi_rows(wifi.state(), now_ms()); }

    std::string state_of(const std::string& ssid) const
    {
        for (const WifiRow& r : rows())
            if (r.kind == WifiRow::Network && r.ssid == ssid)
                return r.state;
        return "-";
    }

    std::string names() const
    {
        std::string out;
        for (const WifiRow& r : rows())
            out += (r.kind == WifiRow::Scan ? std::string("scan") : r.ssid) + "=" + r.state + ";";
        return out;
    }

    bool saved(const std::string& ssid, WifiSec sec) const { return wifi_saved(wifi.state(), ssid, sec) != nullptr; }
};

}

int main()
{
    const char* addr = std::getenv("DBUS_SYSTEM_BUS_ADDRESS");
    const char* dir = std::getenv("BLOCKIE_TEST_DIR");
    if (!addr || !*addr || !dir || !*dir) {
        std::printf("FAIL no DBUS_SYSTEM_BUS_ADDRESS or BLOCKIE_TEST_DIR: run through tests/fake_nm.py\n");
        return 1;
    }
    DBusError err;
    dbus_error_init(&err);
    g_ctl = dbus_connection_open_private(addr, &err);
    if (!g_ctl || !dbus_bus_register(g_ctl, &err)) {
        std::printf("FAIL control connection: %s\n", err.message ? err.message : "?");
        return 1;
    }
    std::string work = dir;
    std::string txt = work + "/wifi.txt";
    std::string captured = work + "/stderr.txt";
    std::fflush(stderr);
    int saved_err = dup(2);
    int cap = open(captured.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (saved_err < 0 || cap < 0 || dup2(cap, 2) < 0) {
        std::printf("FAIL stderr capture\n");
        return 1;
    }
    close(cap);

    Rig rig(txt);
    const WifiState& s = rig.wifi.state();
    rig.wifi.start();
    rig.bus.open(now_ms());
    check(rig.until([&] { return s.up && s.radio && s.saved.size() == 1; }),
          "nm, wlan0, the radio and the saved list at start");
    check(s.device == "/org/freedesktop/NetworkManager/Devices/3" && s.saved[0].ssid == kOffice &&
              s.saved[0].sec == WifiSec::Psk && !s.saved[0].temp,
          "the saved wi-fi profile, the wired one left out");
    check(wifi_saved_count(s) == 1 && s.aps.empty() && count_prefix(strings("Log"), "RequestScan") == 0,
          "one saved, no scan and no access points before the page");

    size_t mark = log_size();
    rig.wifi.set_page_open(true, now_ms());
    check(s.scanning && rig.rows()[0].state == "scanning", "page entry starts a scan");
    check(rig.until([&] { return s.scanned && !s.scanning && rig.state_of(kLate) == "open"; }),
          "the scan finishes with the network found during it");
    check(count_prefix(since(mark), "RequestScan") == 1, "one RequestScan at entry");
    check(rig.names() == "scan=;" + kOffice + "=saved;" + kHome + "=secured;" + kCafe +
                             "=open;blockie-test-wep=not supported;" + kLate + "=open;",
          "groups: saved in range, then the others by signal; one row per ssid; hidden not listed");

    mark = log_size();
    test_call("FailOnce", "ActiveConnection");
    rig.run(WifiCmd::Join, kCafe, WifiSec::Open);
    check(s.joining && rig.state_of(kCafe) == "connecting", "an open network joins at once");
    check(rig.until([&] { return !s.joining && rig.state_of(kCafe) == "connected"; }), "the open network connects");
    check(find_prefix(since(mark), "GetFailed ActiveConnection") >= 0,
          "a failed ActiveConnection Get on ACTIVATED is tried once more");
    {
        std::vector<std::string> log = since(mark);
        int a =
            find_prefix(log, "AddAndActivateConnection2 persist=volatile ap=" + kAp + "2 security=open psk-flags=-");
        int u = find_prefix(log, "Update2 " + kSettings);
        check(a >= 0 && u > a, "AddAndActivateConnection2 volatile with no security setting, then Update2");
        check(u >= 0 && log[(size_t)u].find(" settings=0 flags=1") != std::string::npos,
              "Update2 with an empty settings dict and TO_DISK");
    }
    check(rig.until([&] { return disk_count(kCafe) == 1; }), "a join that reaches ACTIVATED leaves one disk profile");

    mark = log_size();
    rig.run(WifiCmd::Join, kHome, WifiSec::Psk, "blockie-test-wrong-1");
    check(rig.until([&] { return !s.joining && rig.state_of(kHome) == "wrong password"; }),
          "a wrong key reads wrong password");
    check(rig.until([&] { return count_prefix(since(mark), "Dropped ") == 1; }), "nm drops the volatile profile");
    {
        std::vector<std::string> log = since(mark);
        check(find_prefix(log, "AddAndActivateConnection2 persist=volatile ap=" + kAp +
                                   "1 security=wpa-psk psk-flags=0") >= 0,
              "the strongest ap, wpa-psk with psk-flags 0");
        check(count_prefix(log, "Update2 ") == 0 && count_prefix(log, "Delete ") == 0,
              "no Update2 and no Delete after a failed join");
    }
    check(disk_count(kHome) == 0 && conn_count(kHome) == 0, "no profile of it after a wrong password, disk or memory");

    mark = log_size();
    rig.run(WifiCmd::Join, kHome, WifiSec::Psk, "blockie-test-psk-01");
    check(s.failed.empty(), "a new join clears the failure");
    check(rig.until([&] { return !s.joining && rig.state_of(kHome) == "connected"; }), "the right key connects");
    check(rig.until([&] { return disk_count(kHome) == 1 && disk_count(kHome, "blockie-test-psk-01") == 1; }),
          "one disk profile with the key");

    check(rig.until([&] { return rig.saved(kHome, WifiSec::Psk); }), "the joined network is in the saved list");
    test_call("Key", kHome.c_str(), "blockie-test-psk-03");
    mark = log_size();
    rig.run(WifiCmd::Connect, kHome, WifiSec::Psk);
    check(rig.until([&] { return !s.joining && rig.state_of(kHome) == "wrong password"; }),
          "a saved network whose key changed fails as wrong password");
    check(find_prefix(since(mark), "ActivateConnection " + kSettings) >= 0,
          "a saved network connects with ActivateConnection");
    check(disk_count(kHome) == 1 && disk_count(kHome, "blockie-test-psk-01") == 1,
          "the saved profile stays on a failure");
    mark = log_size();
    rig.run(WifiCmd::Join, kHome, WifiSec::Psk, "blockie-test-psk-03");
    check(rig.until([&] { return !s.joining && rig.state_of(kHome) == "connected"; }), "the new key joins");
    check(rig.until([&] { return disk_count(kHome) == 1 && disk_count(kHome, "blockie-test-psk-03") == 1; }),
          "on ACTIVATED the old profile of the ssid goes, one profile left");
    {
        std::vector<std::string> log = since(mark);
        int u = find_prefix(log, "Update2 ");
        int d = find_prefix(log, "Delete " + kSettings);
        check(u >= 0 && d > u, "Update2, then Delete of the old profile");
    }

    check(rig.until([&] { return rig.saved(kCafe, WifiSec::Open); }), "the open network is saved");
    mark = log_size();
    rig.run(WifiCmd::Forget, kCafe, WifiSec::Open);
    check(rig.until([&] { return disk_count(kCafe) == 0 && !rig.saved(kCafe, WifiSec::Open); }),
          "forget deletes the saved profile");
    check(count_prefix(since(mark), "Delete ") == 1, "one Delete for forget");

    rig.run(WifiCmd::Connect, kOffice, WifiSec::Psk);
    check(rig.until([&] { return !s.joining && rig.state_of(kOffice) == "connected"; }), "a saved network connects");

    hold(true);
    mark = log_size();
    rig.run(WifiCmd::Join, kCafe, WifiSec::Open);
    check(rig.until([&] { return find_prefix(since(mark), "AddAndActivateConnection2 ") >= 0 && s.dev_state == 50; }),
          "a join held in config");
    rig.wifi.set_page_open(false, now_ms());
    rig.settle(200);
    hold(false);
    check(rig.until([&] { return !s.joining && disk_count(kCafe) == 1; }),
          "with the page closed the join finishes through StateChanged and goes to disk");
    {
        std::vector<std::string> log = since(mark);
        check(count_prefix(log, "Delete ") == 0 && find_prefix(log, "Update2 ") >= 0,
              "no Delete of the volatile profile at page exit, Update2 after it");
    }

    test_call("Mode", "refuse");
    uint32_t seq = s.note.seq;
    mark = log_size();
    rig.run(WifiCmd::Join, kLate, WifiSec::Open);
    check(rig.until([&] { return !s.joining && s.note.seq == seq + 1; }) && s.note.title == "wi-fi" &&
              s.note.text == "denied" && find_prefix(since(mark), "Update2 ") >= 0,
          "a refused Update2 shows denied");
    check(disk_count(kLate) == 0 && conn_count(kLate) == 1, "the refused profile stays volatile, nothing on disk");
    test_call("Mode", "ok");

    rig.run(WifiCmd::Radio, "", WifiSec::Open, "", false);
    check(rig.until([&] { return !s.radio; }) && rig.rows().size() == 1 && rig.rows()[0].state == "unavailable",
          "radio off reads unavailable");
    test_call("Mode", "deny");
    seq = s.note.seq;
    rig.run(WifiCmd::Radio, "", WifiSec::Open, "", true);
    check(rig.until([&] { return s.note.seq == seq + 1; }) && s.note.title == "wi-fi" && s.note.text == "denied" &&
              !s.radio,
          "a refused radio change shows denied");
    test_call("Mode", "ok");
    rig.run(WifiCmd::Radio, "", WifiSec::Open, "", true);
    check(rig.until([&] { return s.radio; }), "radio on");

    test_call("Mode", "scanfail");
    rig.wifi.set_page_open(true, now_ms());
    check(rig.until([&] { return s.scan_failed && !s.scanning; }) && rig.rows()[0].state == "scan failed",
          "a refused scan reads scan failed");
    test_call("Mode", "ok");
    rig.run(WifiCmd::Scan);
    check(rig.until([&] { return s.scanned && !s.scanning && !s.scan_failed; }), "a retry scans");
    rig.wifi.set_page_open(false, now_ms());

    uint32_t serial = rig.wifi.serial();
    test_call("Restart");
    check(rig.until([&] { return !s.up; }, 1000) || rig.wifi.serial() != serial, "nm leaving drops the model");
    check(rig.until([&] { return s.up && s.radio && rig.saved(kOffice, WifiSec::Psk); }), "nm back: fetched again");

    auto exists = [&]() { return access(txt.c_str(), F_OK) == 0; };
    auto present = [&]() {
        struct stat st;
        return lstat(txt.c_str(), &st) == 0;
    };
    check(symlink("wifi.txt", txt.c_str()) == 0, "a wifi.txt that is a link to itself");
    seq = s.note.seq;
    mark = log_size();
    rig.wifi.import_txt();
    check(rig.until([&] { return s.note.seq == seq + 1; }) && s.note.title == "wifi.txt" &&
              s.note.text == "line 1 not read" && present(),
          "a wifi.txt that does not open: line 1 not read, the file kept");
    unlink(txt.c_str());
    check(mkdir(txt.c_str(), 0700) == 0, "a wifi.txt that is a directory");
    seq = s.note.seq;
    rig.wifi.import_txt();
    check(rig.until([&] { return s.note.seq == seq + 1; }) && s.note.title == "wifi.txt" &&
              s.note.text == "line 1 not read" && present(),
          "a wifi.txt that opens but does not read: line 1 not read, kept");
    rmdir(txt.c_str());
    check(mkfifo(txt.c_str(), 0600) == 0, "a wifi.txt that is a fifo");
    seq = s.note.seq;
    rig.wifi.import_txt();
    check(rig.until([&] { return s.note.seq == seq + 1; }) && s.note.title == "wifi.txt" &&
              s.note.text == "line 1 not read" && present(),
          "a fifo is not read, nothing waits on it: line 1 not read, kept");
    unlink(txt.c_str());
    check(count_prefix(since(mark), "AddConnection") == 0 && count_prefix(since(mark), "Update ") == 0,
          "an unreadable wifi.txt saves nothing");

    std::string elsewhere = work + "/elsewhere.txt";
    std::string linked = "ssid=blockie-test-linked\npassword=blockie-test-psk-09\n";
    check(write_file(elsewhere, linked) && symlink(elsewhere.c_str(), txt.c_str()) == 0,
          "a wifi.txt that links to a valid file elsewhere");
    seq = s.note.seq;
    mark = log_size();
    rig.wifi.import_txt();
    {
        bool noted = rig.until([&] { return s.note.seq == seq + 1; });
        struct stat st;
        bool link = lstat(txt.c_str(), &st) == 0 && S_ISLNK(st.st_mode);
        check(noted && s.note.title == "wifi.txt" && s.note.text == "line 1 not read" && link,
              "a linked wifi.txt is not followed: line 1 not read, the link kept");
    }
    check(count_prefix(since(mark), "AddConnection") == 0 && count_prefix(since(mark), "Update ") == 0 &&
              disk_count("blockie-test-linked") == 0,
          "nothing from the link target is saved");
    check(read_file(elsewhere) == linked, "the link target is untouched");
    unlink(txt.c_str());
    unlink(elsewhere.c_str());
    write_file(txt, "ssid=" + kTxtNew + "\npassword=short\n");
    seq = s.note.seq;
    mark = log_size();
    rig.wifi.import_txt();
    check(rig.until([&] { return s.note.seq == seq + 1; }) && s.note.title == "wifi.txt" &&
              s.note.text == "line 2 not read",
          "wifi.txt with a bad line: line 2 not read");
    check(exists() && count_prefix(since(mark), "AddConnection") == 0, "the file stays and nothing is saved");

    std::string big = "ssid=blockie-test-big\npassword=blockie-test-psk-08\n" + std::string(70000, '\n');
    write_file(txt, big);
    seq = s.note.seq;
    rig.wifi.import_txt();
    check(rig.until([&] { return s.note.seq == seq + 1; }) && s.note.text == "line 65488 not read" && exists(),
          "a file past the 64 KiB read refuses at the line where the read stops");

    std::string good = "\xef\xbb\xbfssid=" + kTxtNew + "\r\npassword=\"blockie-test-psk-04\"\r\n\r\nssid=" + kOffice +
                       "\r\npassword=blockie-test-psk-05\r\n";
    write_file(txt, good);
    test_call("Mode", "refuse");
    seq = s.note.seq;
    rig.wifi.import_txt();
    check(rig.until([&] { return s.note.seq == seq + 1; }) && s.note.title == "wifi.txt" &&
              s.note.text == "networks not saved",
          "a refused save: networks not saved");
    check(exists() && disk_count(kTxtNew) == 0 && disk_count(kOffice, "blockie-test-psk-02") == 1,
          "a refused save keeps the file and changes nothing");
    test_call("Mode", "ok");
    seq = s.note.seq;
    mark = log_size();
    rig.wifi.import_txt();
    check(rig.until([&] { return s.note.seq == seq + 1; }) && s.note.title == "wi-fi" &&
              s.note.text == "2 networks saved",
          "wifi.txt saved: 2 networks saved");
    check(!exists(), "the file is deleted after every save succeeded");
    check(disk_count(kTxtNew) == 1 && disk_count(kTxtNew, "blockie-test-psk-04") == 1, "a new network is added");
    check(disk_count(kOffice) == 1 && disk_count(kOffice, "blockie-test-psk-05") == 1,
          "a saved network is re keyed in place, never added twice");
    {
        std::vector<std::string> log = since(mark);
        check(count_prefix(log, "AddConnection security=wpa-psk id=yes") == 1 &&
                  count_prefix(log, "AddConnection") == 1 && find_prefix(log, "Update " + kSettings) >= 0,
              "AddConnection for the new network, Update for the saved one");
    }

    check(rig.until([&] { return rig.saved(kCafe, WifiSec::Open); }), "the open network is saved again");
    write_file(txt, "ssid=" + kCafe + "\n");
    seq = s.note.seq;
    mark = log_size();
    rig.wifi.import_txt();
    check(rig.until([&] { return s.note.seq == seq + 1; }) && s.note.text == "1 network saved" && !exists(),
          "an open network already saved counts and the file goes");
    check(count_prefix(since(mark), "AddConnection") == 0 && count_prefix(since(mark), "Update ") == 0,
          "an open network already saved is kept as is");

    write_file(txt, "ssid=" + kTwice + "\npassword=blockie-test-psk-06\nssid=" + kTwice +
                        "\npassword=blockie-test-psk-07\n");
    seq = s.note.seq;
    rig.wifi.import_txt();
    check(rig.until([&] { return s.note.seq == seq + 1; }) && s.note.text == "1 network saved" &&
              disk_count(kTwice) == 1 && disk_count(kTwice, "blockie-test-psk-07") == 1,
          "a name twice keeps the last password");

    std::fflush(stderr);
    dup2(saved_err, 2);
    close(saved_err);
    std::string log = read_file(captured);
    check(log.find("blockie-frontend: wifi join") != std::string::npos &&
              log.find("blockie-frontend: wifi.txt line 1 not read") != std::string::npos,
          "the wifi client logged to stderr, the unreadable wifi.txt as its line only");
    check(log.find("blockie-test-") == std::string::npos && log.find("02:00:00:00:00:") == std::string::npos,
          "stderr holds no ssid, bssid or key");

    dbus_connection_close(g_ctl);
    dbus_connection_unref(g_ctl);
    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
