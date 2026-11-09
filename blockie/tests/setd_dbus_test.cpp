#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <dbus/dbus.h>
#include <poll.h>

#include "../bus.h"
#include "../setd_key.h"
#include "../theme_setting.h"

using namespace fd;

namespace {

int g_fail = 0;
int g_total = 0;
DBusConnection* g_ctl = nullptr;

const char* const kTest = "net.seilent.Test.FakeSetd";
const std::string kZone = "system.timezone";
const std::string kTheme = "frontend.theme";
const std::string kLevel = "display.brightness";
const std::string kShader = "games.shader";
const std::string kInvalid = std::string(SetdKey::kErr) + "Invalid";

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

DBusMessage* test_msg(const char* member) { return dbus_message_new_method_call(kTest, "/test", kTest, member); }

void test_call(const char* member)
{
    if (DBusMessage* r = ctl(test_msg(member)))
        dbus_message_unref(r);
}

void test_str(const char* member, const std::string& a)
{
    DBusMessage* m = test_msg(member);
    const char* s = a.c_str();
    dbus_message_append_args(m, DBUS_TYPE_STRING, &s, DBUS_TYPE_INVALID);
    if (DBusMessage* r = ctl(m))
        dbus_message_unref(r);
}

void test_str2(const char* member, const std::string& a, const std::string& b)
{
    DBusMessage* m = test_msg(member);
    const char* s = a.c_str();
    const char* t = b.c_str();
    dbus_message_append_args(m, DBUS_TYPE_STRING, &s, DBUS_TYPE_STRING, &t, DBUS_TYPE_INVALID);
    if (DBusMessage* r = ctl(m))
        dbus_message_unref(r);
}

void test_u32(const char* member, uint32_t v)
{
    DBusMessage* m = test_msg(member);
    dbus_uint32_t u = v;
    dbus_message_append_args(m, DBUS_TYPE_UINT32, &u, DBUS_TYPE_INVALID);
    if (DBusMessage* r = ctl(m))
        dbus_message_unref(r);
}

void own(bool want)
{
    DBusMessage* m = test_msg("Own");
    dbus_bool_t b = want ? TRUE : FALSE;
    dbus_message_append_args(m, DBUS_TYPE_BOOLEAN, &b, DBUS_TYPE_INVALID);
    if (DBusMessage* r = ctl(m))
        dbus_message_unref(r);
}

void burst(const std::string& key, uint32_t n)
{
    DBusMessage* m = test_msg("Burst");
    const char* k = key.c_str();
    dbus_uint32_t u = n;
    dbus_message_append_args(m, DBUS_TYPE_STRING, &k, DBUS_TYPE_UINT32, &u, DBUS_TYPE_INVALID);
    if (DBusMessage* r = ctl(m))
        dbus_message_unref(r);
}

std::vector<std::string> fake_log()
{
    std::vector<std::string> v;
    DBusMessage* r = ctl(test_msg("Log"));
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

int count(const std::vector<std::string>& log, const std::string& line)
{
    int n = 0;
    for (const std::string& l : log)
        n += l == line;
    return n;
}

int find(const std::vector<std::string>& log, const std::string& line, size_t from = 0)
{
    for (size_t i = from; i < log.size(); i++)
        if (log[i] == line)
            return (int)i;
    return -1;
}

void spoof_changed(const char* dest, const std::string& key, const std::string& value)
{
    DBusMessage* s = dbus_message_new_signal(SetdKey::kPath, SetdKey::kIface, "Changed");
    dbus_message_set_destination(s, dest);
    DBusMessageIter it, v;
    dbus_message_iter_init_append(s, &it);
    const char* k = key.c_str();
    const char* val = value.c_str();
    dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &k);
    dbus_message_iter_open_container(&it, DBUS_TYPE_VARIANT, "s", &v);
    dbus_message_iter_append_basic(&v, DBUS_TYPE_STRING, &val);
    dbus_message_iter_close_container(&it, &v);
    dbus_connection_send(g_ctl, s, nullptr);
    dbus_connection_flush(g_ctl);
    dbus_message_unref(s);
}

struct Pump {
    Bus bus;

    void step(int64_t now)
    {
        bus.pump();
        bus.tick(now);
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
};

struct Rig : Pump {
    ThemeSetting theme;
    SetdKey zone{kZone};
    std::string shown;
    std::vector<std::string> values;
    int fails = 0;
    int missing = 0;
    std::vector<bool> themes;
    int theme_fails = 0;

    Rig()
    {
        theme.on_value = [this](bool light) { themes.push_back(light); };
        theme.on_fail = [this](const std::string&, const std::string&) { theme_fails++; };
        theme.attach(bus);
        zone.on_value = [this](const std::string& v) {
            values.push_back(v);
            shown = v;
        };
        zone.on_fail = [this](const std::string&, const std::string&) {
            fails++;
            shown = zone.value();
        };
        zone.on_missing = [this]() { missing++; };
        zone.attach(bus);
    }

    void pick(const std::string& v)
    {
        shown = v;
        zone.set(v, now_ms());
    }
};

struct Typed : Pump {
    SetdKey level{kLevel, SetdType::U};
    SetdKey shader{kShader, SetdType::B};
    SetdKey wrong{kLevel};
    std::vector<std::string> levels, shaders, errors, texts;

    Typed()
    {
        level.on_value = [this](const std::string& v) { levels.push_back(v); };
        shader.on_value = [this](const std::string& v) { shaders.push_back(v); };
        auto fail = [this](const std::string& e, const std::string& t) {
            errors.push_back(e);
            texts.push_back(t);
        };
        level.on_fail = fail;
        shader.on_fail = fail;
        wrong.on_fail = fail;
        level.attach(bus);
        shader.attach(bus);
        wrong.attach(bus);
    }
};

int sets_in(const std::vector<std::string>& log)
{
    int n = 0;
    for (const std::string& l : log)
        n += l.compare(0, 4, "Set ") == 0;
    return n;
}

}

int main()
{
    const char* addr = std::getenv("DBUS_SYSTEM_BUS_ADDRESS");
    if (!addr || !*addr) {
        std::printf("FAIL no DBUS_SYSTEM_BUS_ADDRESS: run through tests/fake_setd.py\n");
        return 1;
    }
    DBusError err;
    dbus_error_init(&err);
    g_ctl = dbus_connection_open_private(addr, &err);
    if (!g_ctl || !dbus_bus_register(g_ctl, &err)) {
        std::printf("FAIL control connection: %s\n", err.message ? err.message : "?");
        return 1;
    }

    {
        auto rig = std::make_unique<Rig>();
        rig->bus.open(now_ms());
        check(rig->until([&] { return rig->zone.answered() && rig->theme.answered(); }),
              "with no setd both Gets at connect answer");
        check(!rig->zone.has_value() && !rig->zone.missing() && rig->themes.empty(),
              "a failed Get leaves no value and the key is not missing");
        test_str("GetMode", "fail");
        size_t mark = fake_log().size();
        own(true);
        check(rig->until([&] {
            std::vector<std::string> log = since(mark);
            return count(log, "Get " + kZone) == 1 && count(log, "Get " + kTheme) == 1;
        }),
              "a new setd owner starts one Get per key");
        rig->settle(100);
        check(!rig->zone.has_value(), "a Get that fails with setd up leaves no value");
        test_str("GetMode", "ok");
        rig->zone.fetch(now_ms());
        check(rig->until([&] { return rig->zone.has_value(); }) && rig->zone.value() == "Asia/Tokyo" &&
                  rig->shown == "Asia/Tokyo",
              "a fetch after the failed Gets gets the value");
    }

    Rig rig;
    size_t mark = fake_log().size();
    rig.bus.open(now_ms());
    check(rig.until([&] { return rig.zone.has_value() && !rig.themes.empty(); }), "both values at connect");
    {
        std::vector<std::string> log = since(mark);
        check(log.size() == 2 && log[0] == "Get " + kTheme && log[1] == "Get " + kZone,
              "Get at connect for both keys, the theme first");
    }
    check(rig.zone.value() == "Asia/Tokyo" && rig.shown == "Asia/Tokyo" && rig.themes.back() == false,
          "values from setd");

    mark = fake_log().size();
    rig.pick("Europe/London");
    check(rig.until([&] { return rig.zone.value() == "Europe/London" && rig.values.back() == "Europe/London"; }),
          "Set ok confirms the value");
    rig.settle(300);
    {
        std::vector<std::string> log = since(mark);
        int s = find(log, "Set " + kZone + " Europe/London");
        check(s >= 0 && count(log, "Get " + kZone) == 1 && find(log, "Get " + kZone, (size_t)s) > s,
              "Set ok, then Changed, then one coalesced Get");
    }
    check(rig.shown == "Europe/London", "the shown value after a Set ok");

    test_u32("GetDelay", 200);
    mark = fake_log().size();
    burst(kZone, 6);
    rig.settle(900);
    check(count(since(mark), "Get " + kZone) == 2, "a Changed burst keeps one Get in flight plus one again");
    test_u32("GetDelay", 0);

    const char* const modes[] = {"invalid", "busy", "failed"};
    for (const char* m : modes) {
        test_str("Mode", m);
        mark = fake_log().size();
        int fails = rig.fails;
        rig.pick("Europe/Paris");
        bool back = rig.until([&] { return rig.fails == fails + 1; }) && rig.shown == "Europe/London";
        bool got = rig.until([&] {
            std::vector<std::string> log = since(mark);
            int s = find(log, "Set " + kZone + " Europe/Paris");
            return s >= 0 && find(log, "Get " + kZone, (size_t)s) > s;
        });
        std::string what = std::string(m) + ": back to the confirmed value, then a Get";
        check(back && got, what.c_str());
    }
    rig.settle(100);
    check(rig.shown == "Europe/London" && rig.zone.value() == "Europe/London", "the confirmed value after the errors");

    test_str("Mode", "timeout");
    test_u32("GetDelay", 300);
    mark = fake_log().size();
    int fails = rig.fails;
    rig.pick("Europe/Paris");
    check(rig.until([&] { return find(since(mark), "Set " + kZone + " Europe/Paris") >= 0; }), "a Set that times out");
    test_str2("Write", kZone, "Arctic/Longyearbyen");
    check(rig.until([&] { return rig.zone.value() == "Arctic/Longyearbyen"; }) && rig.zone.setting() &&
              rig.shown == "Europe/Paris",
          "the link changes during the Set: value() follows the Get, the pick stays shown");
    check(rig.until([&] { return count(since(mark), "Get " + kZone) == 2; }) && !rig.zone.setting(),
          "Timeout starts a Get");
    check(rig.shown == "Europe/Paris" && rig.fails == fails, "Timeout keeps the pick shown until the Get answers");
    check(rig.until([&] { return rig.shown == "Arctic/Longyearbyen"; }) && rig.fails == fails,
          "then the shown value is the Get value, the link written during the Set");
    test_u32("GetDelay", 0);
    test_str("GetMode", "fail");
    rig.pick("Europe/London");
    check(rig.until([&] { return rig.fails == fails + 1; }) && rig.shown == "Arctic/Longyearbyen",
          "Timeout and the Get fails too: back to the confirmed value");
    test_str("GetMode", "ok");
    test_str2("Write", kZone, "Europe/Paris");
    check(rig.until([&] { return rig.shown == "Europe/Paris"; }), "a late write after Timeout shows through Changed");

    fails = rig.fails;
    test_str("Mode", "hang");
    mark = fake_log().size();
    rig.pick("Europe/London");
    check(rig.until([&] { return find(since(mark), "Set " + kZone + " Europe/London") >= 0; }), "Set with no reply");
    mark = fake_log().size();
    rig.step(now_ms() + SetdKey::kTimeoutMs + 1000);
    check(rig.until([&] { return count(since(mark), "Get " + kZone) == 1; }), "no reply starts a Get");
    check(rig.until([&] { return rig.shown == "Europe/Paris"; }) && rig.fails == fails,
          "no reply, the Get answers: the shown value is the Get value");
    test_str("GetMode", "fail");
    mark = fake_log().size();
    rig.pick("Arctic/Longyearbyen");
    check(rig.until([&] { return find(since(mark), "Set " + kZone + " Arctic/Longyearbyen") >= 0; }),
          "a second Set with no reply");
    rig.step(now_ms() + SetdKey::kTimeoutMs + 1000);
    check(rig.until([&] { return rig.fails == fails + 1; }) && rig.shown == "Europe/Paris",
          "no reply and the Get fails too: back to the confirmed value");
    test_str("GetMode", "ok");
    test_str("Mode", "ok");

    mark = fake_log().size();
    size_t seen = rig.values.size();
    const char* unique = dbus_bus_get_unique_name(rig.bus.conn());
    spoof_changed(unique ? unique : "", kZone, "Mars/Olympus");
    check(rig.until([&] { return count(since(mark), "Get " + kZone) == 1; }), "a spoofed Changed only starts a Get");
    rig.settle(100);
    {
        bool shown_spoof = false;
        for (size_t i = seen; i < rig.values.size(); i++)
            shown_spoof = shown_spoof || rig.values[i] == "Mars/Olympus";
        check(!shown_spoof && rig.shown == "Europe/Paris", "the spoofed payload is never shown, the Get value is");
    }

    test_str("Mode", "delay");
    mark = fake_log().size();
    rig.pick("Europe/London");
    rig.pick("Asia/Tokyo");
    rig.pick("Arctic/Longyearbyen");
    seen = rig.values.size();
    check(rig.zone.setting(), "one Set in flight, the newer ones wait");
    check(rig.until([&] { return rig.zone.value() == "Europe/London"; }) && rig.zone.setting() &&
              rig.shown == "Arctic/Longyearbyen" && rig.values.size() == seen,
          "while the newest is in flight value() moves to London, the pick stays shown");
    check(rig.until([&] { return rig.zone.value() == "Arctic/Longyearbyen" && !rig.zone.setting(); }),
          "the newest value lands");
    rig.settle(300);
    {
        std::vector<std::string> log = since(mark);
        check(count(log, "Set " + kZone + " Europe/London") == 1 && count(log, "Set " + kZone + " Asia/Tokyo") == 0 &&
                  find(log, "Set " + kZone + " Arctic/Longyearbyen") > find(log, "Set " + kZone + " Europe/London"),
              "two quick sets: the newest is sent after the one in flight");
        bool superseded = false;
        for (size_t i = seen; i < rig.values.size(); i++)
            superseded = superseded || rig.values[i] != "Arctic/Longyearbyen";
        check(!superseded && rig.shown == "Arctic/Longyearbyen",
              "the superseded reply is neither applied nor reverted");
    }

    mark = fake_log().size();
    seen = rig.values.size();
    rig.pick("Europe/London");
    check(rig.until([&] { return find(since(mark), "Set " + kZone + " Europe/London") >= 0; }), "a delayed Set");
    test_str2("Write", kZone, "Asia/Tokyo");
    check(rig.until([&] { return rig.zone.value() == "Asia/Tokyo"; }) && rig.zone.setting() &&
              rig.shown == "Europe/London" && rig.values.size() == seen,
          "Get deferral: a Get answered during a Set changes value(), not the shown value");
    check(rig.until([&] { return !rig.zone.setting() && rig.zone.value() == "Europe/London"; }) &&
              rig.shown == "Europe/London",
          "then the Set's success shows the pick");
    test_str("Mode", "slowfail");
    mark = fake_log().size();
    fails = rig.fails;
    rig.pick("Europe/Paris");
    check(rig.until([&] { return find(since(mark), "Set " + kZone + " Europe/Paris") >= 0; }), "a Set that fails late");
    test_str2("Write", kZone, "Asia/Tokyo");
    check(rig.until([&] { return rig.zone.value() == "Asia/Tokyo"; }) && rig.shown == "Europe/Paris",
          "a Get during the failing Set is deferred");
    check(rig.until([&] { return rig.fails == fails + 1; }) && rig.shown == "Asia/Tokyo",
          "a failing Set after a deferred Get shows the Get's value");
    test_str("Mode", "ok");

    size_t themes = rig.themes.size();
    mark = fake_log().size();
    test_str2("Write", kTheme, "blue");
    check(rig.until([&] { return count(since(mark), "Get " + kTheme) == 1; }), "theme Changed starts a Get");
    rig.settle(100);
    check(rig.themes.size() == themes, "the theme adapter takes only light and dark");
    test_str2("Write", kTheme, "light");
    check(rig.until([&] { return rig.themes.size() == themes + 1 && rig.themes.back(); }), "theme light from setd");

    test_str("Mode", "failed");
    mark = fake_log().size();
    themes = rig.themes.size();
    rig.theme.set(false, now_ms());
    check(rig.until([&] { return rig.theme_fails == 1; }), "a failed theme Set fails");
    check(rig.until([&] {
        std::vector<std::string> log = since(mark);
        int s = find(log, "Set " + kTheme + " dark");
        return s >= 0 && find(log, "Get " + kTheme, (size_t)s) > s;
    }),
          "a failed theme Set starts a Get");
    check(rig.until([&] { return rig.themes.size() == themes + 1 && rig.themes.back(); }),
          "the theme follows the Get");
    test_str("Mode", "ok");

    mark = fake_log().size();
    test_call("Restart");
    check(rig.until([&] {
        std::vector<std::string> log = since(mark);
        return count(log, "Get " + kTheme) == 1 && count(log, "Get " + kZone) == 1;
    }),
          "an owner change refetches the theme and the zone");
    rig.settle(200);
    {
        std::vector<std::string> log = since(mark);
        check(count(log, "Get " + kTheme) == 1 && count(log, "Get " + kZone) == 1, "owner change: one Get per key");
    }

    test_str("Mode", "hang");
    mark = fake_log().size();
    rig.pick("Europe/London");
    check(rig.until([&] { return find(since(mark), "Set " + kZone + " Europe/London") >= 0; }), "a Set in flight");
    test_str("Forget", kZone);
    rig.zone.fetch(now_ms());
    check(rig.until([&] { return rig.zone.missing(); }) && rig.missing == 1 && !rig.zone.has_value() &&
              rig.zone.setting(),
          "UnknownKey during a Set: the key is missing at once");

    test_str("Mode", "ok");
    Typed ty;
    mark = fake_log().size();
    ty.bus.open(now_ms());
    check(ty.until([&] { return ty.level.has_value() && ty.shader.has_value() && ty.wrong.answered(); }),
          "u and b keys answer at connect");
    check(ty.level.value() == "40" && !ty.levels.empty() && ty.levels.back() == "40", "a u value reads as its decimal");
    check(ty.shader.value() == "true" && !ty.shaders.empty() && ty.shaders.back() == "true",
          "a b value reads as true");
    check(!ty.wrong.has_value() && !ty.wrong.missing(), "an s key reading a u value gets no value and is not missing");
    {
        std::vector<std::string> log = since(mark);
        check(count(log, "Get " + kLevel) == 2 && count(log, "Get " + kShader) == 1, "one Get per key at connect");
    }

    mark = fake_log().size();
    test_str2("Write", kLevel, "60");
    check(ty.until([&] { return ty.level.value() == "60" && ty.levels.back() == "60"; }),
          "a Changed with a u payload starts a Get");
    check(ty.until([&] { return count(since(mark), "Get " + kLevel) == 2; }) && !ty.wrong.has_value(),
          "both keys of that name Get on it");
    test_str2("Write", kShader, "false");
    check(ty.until([&] { return ty.shader.value() == "false" && ty.shaders.back() == "false"; }),
          "a Changed with a b payload starts a Get, false reads as false");

    mark = fake_log().size();
    ty.level.set("55", now_ms());
    check(ty.until([&] { return !ty.level.setting() && ty.level.value() == "55"; }) &&
              find(since(mark), "Set " + kLevel + " 55") >= 0 && ty.errors.empty(),
          "a u Set goes as a u variant");
    ty.shader.set("true", now_ms());
    check(ty.until([&] { return !ty.shader.setting() && ty.shader.value() == "true"; }) &&
              find(since(mark), "Set " + kShader + " true") >= 0 && ty.errors.empty(),
          "a b Set goes as a b variant");
    ty.shader.set("false", now_ms());
    check(ty.until([&] { return !ty.shader.setting() && ty.shader.value() == "false"; }) &&
              find(since(mark), "Set " + kShader + " false") >= 0 && ty.errors.empty(),
          "a b Set of false goes as false");
    ty.shader.set("true", now_ms());
    check(ty.until([&] { return !ty.shader.setting() && ty.shader.value() == "true"; }) && ty.errors.empty(),
          "and back to true");
    mark = fake_log().size();
    ty.level.set("0070", now_ms());
    check(ty.until([&] { return !ty.level.setting() && ty.level.value() == "70"; }) &&
              find(since(mark), "Set " + kLevel + " 70") >= 0 && ty.levels.back() == "70" &&
              std::find(ty.levels.begin(), ty.levels.end(), "0070") == ty.levels.end(),
          "a u Set sends the decimal without its leading zeros, and 0070 is never shown");
    ty.level.set("4294967295", now_ms());
    check(ty.until([&] { return !ty.level.setting() && ty.level.value() == "4294967295"; }) && ty.errors.empty(),
          "2^32-1 is sent");
    ty.settle(200);

    mark = fake_log().size();
    size_t errs = ty.errors.size();
    for (const char* bad : {"", "-1", "4294967296", "12a", " 5", "+5", "99999999999999999999"})
        ty.level.set(bad, now_ms());
    {
        bool all = ty.errors.size() == errs + 7;
        for (size_t i = errs; i < ty.errors.size(); i++)
            all = all && ty.errors[i] == kInvalid && ty.texts[i] == kLevel + " takes u";
        check(all && !ty.level.setting(), "a u value that is not a decimal in 0..2^32-1 is refused as Invalid at once");
    }
    errs = ty.errors.size();
    for (const char* bad : {"", "yes", "1", "True"})
        ty.shader.set(bad, now_ms());
    {
        bool all = ty.errors.size() == errs + 4;
        for (size_t i = errs; i < ty.errors.size(); i++)
            all = all && ty.errors[i] == kInvalid && ty.texts[i] == kShader + " takes b";
        check(all && !ty.shader.setting(), "a b value other than true or false is refused as Invalid at once");
    }
    ty.settle(200);
    check(sets_in(since(mark)) == 0 && ty.level.value() == "4294967295" && ty.shader.value() == "true",
          "a refused value is never sent");

    errs = ty.errors.size();
    mark = fake_log().size();
    ty.wrong.set("80", now_ms());
    check(ty.until([&] { return ty.errors.size() == errs + 1; }) && ty.errors.back() == kInvalid &&
              ty.texts.back() == kLevel + " takes u, not s" && find(since(mark), "Set " + kLevel + " 80") >= 0,
          "setd refuses a Set of another type with Invalid and its text");
    ty.settle(200);
    check(ty.level.value() == "4294967295" && !ty.wrong.has_value(), "the refused type leaves the value");

    const char* const kinds[][2] = {{"invalid", "Invalid"}, {"busy", "Busy"}, {"failed", "Failed"}};
    for (const auto& k : kinds) {
        test_str("Mode", k[0]);
        errs = ty.errors.size();
        ty.level.set("50", now_ms());
        bool got = ty.until([&] { return ty.errors.size() == errs + 1; }) &&
                   ty.errors.back() == std::string(SetdKey::kErr) + k[1] &&
                   ty.texts.back() == std::string(k[0]) + " for the test";
        std::string what = std::string(k[0]) + ": on_fail gets setd's error name and text";
        check(got, what.c_str());
        ty.settle(100);
    }
    test_str("Mode", "hang");
    test_str("GetMode", "fail");
    errs = ty.errors.size();
    mark = fake_log().size();
    ty.level.set("45", now_ms());
    check(ty.until([&] { return find(since(mark), "Set " + kLevel + " 45") >= 0; }), "a u Set with no reply");
    ty.step(now_ms() + SetdKey::kTimeoutMs + 1000);
    check(ty.until([&] { return ty.errors.size() == errs + 1; }) && ty.errors.back() == DBUS_ERROR_NO_REPLY &&
              ty.texts.back() == "no reply",
          "no reply and the Get fails too: on_fail gets NoReply");
    test_str("GetMode", "ok");
    test_str("Mode", "ok");

    dbus_connection_close(g_ctl);
    dbus_connection_unref(g_ctl);
    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
