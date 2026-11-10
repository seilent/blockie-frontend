#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

#include <dbus/dbus.h>
#include <ftw.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../backlight.h"
#include "../bus.h"

using namespace fd;

namespace {

int g_fail = 0;
int g_total = 0;
DBusConnection* g_ctl = nullptr;

const char* const kTest = "net.seilent.Test.FakeLogind";

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

void test_str(const char* member, const std::string& a)
{
    DBusMessage* m = dbus_message_new_method_call(kTest, "/test", kTest, member);
    const char* s = a.c_str();
    dbus_message_append_args(m, DBUS_TYPE_STRING, &s, DBUS_TYPE_INVALID);
    if (DBusMessage* r = ctl(m))
        dbus_message_unref(r);
}

std::vector<std::string> fake_log()
{
    std::vector<std::string> v;
    DBusMessage* r = ctl(dbus_message_new_method_call(kTest, "/test", kTest, "Log"));
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
    std::vector<std::string> log = fake_log();
    return mark < log.size() ? std::vector<std::string>(log.begin() + (long)mark, log.end())
                             : std::vector<std::string>();
}

void write_node(const std::string& dir, const char* name, int v)
{
    FILE* f = std::fopen((dir + "/" + name).c_str(), "wb");
    if (f) {
        std::fprintf(f, "%d\n", v);
        std::fclose(f);
    }
}

int rm_cb(const char* path, const struct stat*, int, struct FTW*)
{
    remove(path);
    return 0;
}

std::string set_line(int session, int raw)
{
    return "SetBrightness /org/freedesktop/login1/session/_3" + std::to_string(session) + " leds lcd-backlight " +
           std::to_string(raw);
}

struct Rig {
    Bus bus;
    Backlight light;
    int blanks = 0;
    std::vector<std::string> fails;

    explicit Rig(const std::string& led) : light(bus, led)
    {
        light.on_blank = [this]() { blanks++; };
        light.on_fail = [this](const std::string& e) { fails.push_back(e); };
        bus.open(now_ms());
    }

    bool until(const std::function<bool()>& cond, int ms = 3000)
    {
        int64_t end = now_ms() + ms;
        while (now_ms() < end) {
            bus.pump();
            bus.tick(now_ms());
            if (cond())
                return true;
            pollfd p{bus.fd(), POLLIN, 0};
            poll(&p, p.fd >= 0 ? 1 : 0, 10);
        }
        return cond();
    }

    void settle(int ms)
    {
        until([] { return false; }, ms);
    }
};

}

int main()
{
    check(backlight_raw(40, 255) == 102 && backlight_raw(10, 255) == 26 && backlight_raw(100, 255) == 255 &&
              backlight_raw(50, 255) == 128 && backlight_raw(10, 5) == 1 && backlight_raw(0, 255) == 1,
          "raw is max(1, (pct x max + 50) / 100)");

    const char* addr = std::getenv("DBUS_SYSTEM_BUS_ADDRESS");
    if (!addr || !*addr) {
        std::printf("FAIL no DBUS_SYSTEM_BUS_ADDRESS: run through tests/fake_logind.py\n");
        return 1;
    }
    DBusError err;
    dbus_error_init(&err);
    g_ctl = dbus_connection_open_private(addr, &err);
    if (!g_ctl || !dbus_bus_register(g_ctl, &err)) {
        std::printf("FAIL control connection: %s\n", err.message ? err.message : "?");
        return 1;
    }
    const char* tdir = std::getenv("TMPDIR");
    std::string tmpl = std::string(tdir && *tdir ? tdir : "/tmp") + "/blockie-led-XXXXXX";
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    if (!mkdtemp(buf.data())) {
        std::printf("FAIL no temp dir\n");
        return 1;
    }
    std::string led = buf.data();
    write_node(led, "max_brightness", 255);
    write_node(led, "brightness", 102);

    {
        Rig r(led);
        size_t mark = fake_log().size();
        r.light.open();
        r.light.preview(50, now_ms());
        check(r.until([&] { return !r.light.busy(); }), "a preview ends");
        std::vector<std::string> log = since(mark);
        check(log.size() == 2 && log[0] == "ListSessions" && log[1] == set_line(3, 128) &&
                  r.light.session() == "/org/freedesktop/login1/session/_33",
              "the first preview finds the uid's seat0 session, skipping another uid and a seatless one");
        mark = fake_log().size();
        r.light.preview(45, now_ms());
        r.until([&] { return !r.light.busy(); });
        log = since(mark);
        check(log.size() == 1 && log[0] == set_line(3, 115), "a later preview reuses the session");

        test_str("Mode", "delay");
        mark = fake_log().size();
        r.light.preview(40, now_ms());
        r.light.preview(35, now_ms());
        r.light.preview(30, now_ms());
        check(r.until([&] { return since(mark).size() == 2 && !r.light.busy(); }), "a burst of three ends");
        log = since(mark);
        check(log.size() == 2 && log[0] == set_line(3, 102) && log[1] == set_line(3, 77),
              "one call in flight, only the newest queued");

        mark = fake_log().size();
        r.light.preview(25, now_ms());
        r.light.preview(20, now_ms());
        r.light.drop();
        r.until([&] { return !r.light.busy(); });
        r.settle(300);
        log = since(mark);
        check(log.size() == 1 && log[0] == set_line(3, 64), "a drop forgets the queued value");

        mark = fake_log().size();
        r.light.preview(60, now_ms());
        r.light.restore(now_ms());
        r.until([&] { return since(mark).size() == 2 && !r.light.busy(); });
        log = since(mark);
        check(log.size() == 2 && log[1] == set_line(3, 102), "restore sends the level the node held at open");
        test_str("Mode", "ok");

        write_node(led, "brightness", 0);
        mark = fake_log().size();
        r.light.preview(70, now_ms());
        r.settle(200);
        check(r.blanks == 1 && since(mark).empty() && !r.light.busy(), "a node at 0 sends nothing and reports a blank");
        r.light.open();
        r.light.restore(now_ms());
        r.settle(200);
        check(since(mark).empty(), "with the node at 0 at open there is nothing to restore");
        write_node(led, "brightness", 102);

        test_str("Mode", "deny");
        mark = fake_log().size();
        r.light.preview(80, now_ms());
        check(r.until([&] { return !r.fails.empty(); }) && r.fails[0] == "denied" && r.light.session().empty() &&
                  !r.light.busy(),
              "a refused call reports denied and forgets the session");
        test_str("Mode", "ok");
        mark = fake_log().size();
        r.light.preview(80, now_ms());
        r.until([&] { return !r.light.busy(); });
        log = since(mark);
        check(log.size() == 2 && log[0] == "ListSessions" && log[1] == set_line(3, 204),
              "the next preview looks the session up again");
    }

    {
        test_str("Seats", "two");
        Rig r(led);
        size_t mark = fake_log().size();
        r.light.preview(50, now_ms());
        r.until([&] { return !r.light.busy(); });
        std::vector<std::string> log = since(mark);
        check(log.size() == 4 && log[0] == "ListSessions" && log[1] == "Active /org/freedesktop/login1/session/_33" &&
                  log[2] == "Active /org/freedesktop/login1/session/_34" && log[3] == set_line(4, 128),
              "two seat0 sessions: the active one");
    }

    {
        test_str("Seats", "none");
        Rig r(led);
        r.light.preview(50, now_ms());
        check(r.until([&] { return !r.fails.empty(); }) && r.fails[0] == "unavailable" && !r.light.busy(),
              "no seat0 session reports unavailable");
        test_str("Seats", "one");
        test_str("Mode", "nolist");
        Rig q(led);
        q.light.preview(50, now_ms());
        check(q.until([&] { return !q.fails.empty(); }) && q.fails[0] == "unavailable",
              "a failed ListSessions with logind gone reports unavailable");
        test_str("Mode", "ok");
    }

    {
        write_node(led, "max_brightness", 0);
        Rig r(led);
        size_t mark = fake_log().size();
        r.light.open();
        r.light.preview(50, now_ms());
        r.light.preview(45, now_ms());
        r.settle(200);
        check(since(mark).empty() && !r.light.busy() && r.fails.size() == 1 && r.fails[0] == "failed",
              "an unreadable max sends nothing and reports failed once per open");
    }

    nftw(led.c_str(), rm_cb, 16, FTW_DEPTH | FTW_PHYS);
    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
