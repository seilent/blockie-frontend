#include "backlight.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

#include <dbus/dbus.h>
#include <unistd.h>

#include "wifi_model.h"

namespace fd {

namespace {

const char* const kLogin = "org.freedesktop.login1";
const char* const kLoginPath = "/org/freedesktop/login1";
const char* const kManager = "org.freedesktop.login1.Manager";
const char* const kSession = "org.freedesktop.login1.Session";
const char* const kProps = "org.freedesktop.DBus.Properties";

int64_t clock_ms()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

bool ok_reply(DBusMessage* r) { return r && dbus_message_get_type(r) == DBUS_MESSAGE_TYPE_METHOD_RETURN; }

std::string error_of(DBusMessage* r)
{
    if (!r)
        return DBUS_ERROR_NO_REPLY;
    const char* e = dbus_message_get_error_name(r);
    return e ? e : "error";
}

std::vector<std::string> seat_sessions(DBusMessage* r, uint32_t uid)
{
    std::vector<std::string> out;
    DBusMessageIter it, a, st;
    if (!dbus_message_has_signature(r, "a(susso)") || !dbus_message_iter_init(r, &it))
        return out;
    dbus_message_iter_recurse(&it, &a);
    while (dbus_message_iter_get_arg_type(&a) == DBUS_TYPE_STRUCT) {
        dbus_message_iter_recurse(&a, &st);
        const char *id = nullptr, *user = nullptr, *seat = nullptr, *path = nullptr;
        dbus_uint32_t u = 0;
        dbus_message_iter_get_basic(&st, &id);
        dbus_message_iter_next(&st);
        dbus_message_iter_get_basic(&st, &u);
        dbus_message_iter_next(&st);
        dbus_message_iter_get_basic(&st, &user);
        dbus_message_iter_next(&st);
        dbus_message_iter_get_basic(&st, &seat);
        dbus_message_iter_next(&st);
        dbus_message_iter_get_basic(&st, &path);
        if (u == uid && seat && std::strcmp(seat, "seat0") == 0 && path)
            out.push_back(path);
        dbus_message_iter_next(&a);
    }
    return out;
}

bool active_reply(DBusMessage* r)
{
    DBusMessageIter it, v;
    if (!ok_reply(r) || !dbus_message_iter_init(r, &it) || dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_VARIANT)
        return false;
    dbus_message_iter_recurse(&it, &v);
    if (dbus_message_iter_get_arg_type(&v) != DBUS_TYPE_BOOLEAN)
        return false;
    dbus_bool_t b = FALSE;
    dbus_message_iter_get_basic(&v, &b);
    return b;
}

}

int backlight_raw(int pct, int max) { return std::max(1, (pct * max + 50) / 100); }

Bus::Reply Backlight::guard(std::function<void(DBusMessage*)> f)
{
    std::weak_ptr<int> alive = life_;
    return [alive, f](DBusMessage* r) {
        if (!alive.expired())
            f(r);
    };
}

int Backlight::node(const char* name) const
{
    FILE* f = std::fopen((led_ + "/" + name).c_str(), "rb");
    if (!f)
        return -1;
    int v = -1;
    if (std::fscanf(f, "%d", &v) != 1)
        v = -1;
    std::fclose(f);
    return v;
}

void Backlight::open()
{
    opened_ = node("brightness");
    queued_ = -1;
    max_told_ = false;
}

void Backlight::preview(int pct, int64_t now)
{
    int max = node("max_brightness");
    if (max <= 0) {
        std::fprintf(stderr, "blockie-frontend: brightness max unreadable in %s\n", led_.c_str());
        if (!max_told_ && on_fail)
            on_fail("failed");
        max_told_ = true;
        return;
    }
    want(backlight_raw(pct, max), now);
}

void Backlight::restore(int64_t now)
{
    if (opened_ > 0)
        want(opened_, now);
}

void Backlight::want(int raw, int64_t now)
{
    if (busy_)
        queued_ = raw;
    else
        send(raw, now);
}

void Backlight::send(int raw, int64_t now)
{
    queued_ = -1;
    if (node("brightness") == 0) {
        if (on_blank)
            on_blank();
        return;
    }
    busy_ = true;
    if (!session_.empty()) {
        call(raw, now);
        return;
    }
    bus_.call(dbus_message_new_method_call(kLogin, kLoginPath, kManager, "ListSessions"),
              guard([this, raw](DBusMessage* r) { listed(r, raw); }), now, kCallMs);
}

void Backlight::listed(DBusMessage* r, int raw)
{
    if (!ok_reply(r)) {
        fail(wifi_error_word(error_of(r)));
        return;
    }
    std::vector<std::string> paths = seat_sessions(r, (uint32_t)getuid());
    if (paths.empty()) {
        std::fprintf(stderr, "blockie-frontend: brightness: no seat0 session of uid %u\n", (unsigned)getuid());
        fail("unavailable");
        return;
    }
    if (paths.size() == 1) {
        session_ = paths[0];
        call(raw, clock_ms());
        return;
    }
    active(std::move(paths), 0, raw);
}

void Backlight::active(std::vector<std::string> paths, size_t i, int raw)
{
    if (i == paths.size()) {
        session_ = paths[0];
        call(raw, clock_ms());
        return;
    }
    DBusMessage* m = dbus_message_new_method_call(kLogin, paths[i].c_str(), kProps, "Get");
    if (m) {
        const char* iface = kSession;
        const char* name = "Active";
        dbus_message_append_args(m, DBUS_TYPE_STRING, &iface, DBUS_TYPE_STRING, &name, DBUS_TYPE_INVALID);
    }
    bus_.call(m, guard([this, paths, i, raw](DBusMessage* r) {
                  if (active_reply(r)) {
                      session_ = paths[i];
                      call(raw, clock_ms());
                  } else {
                      active(paths, i + 1, raw);
                  }
              }),
              clock_ms(), kCallMs);
}

void Backlight::call(int raw, int64_t now)
{
    DBusMessage* m = dbus_message_new_method_call(kLogin, session_.c_str(), kSession, "SetBrightness");
    if (m) {
        const char* sub = "leds";
        const char* name = "lcd-backlight";
        dbus_uint32_t v = (dbus_uint32_t)raw;
        dbus_message_append_args(m, DBUS_TYPE_STRING, &sub, DBUS_TYPE_STRING, &name, DBUS_TYPE_UINT32, &v,
                                 DBUS_TYPE_INVALID);
    }
    bus_.call(m, guard([this](DBusMessage* r) { called(r); }), now, kCallMs);
}

void Backlight::called(DBusMessage* r)
{
    if (!ok_reply(r)) {
        std::fprintf(stderr, "blockie-frontend: brightness SetBrightness on %s failed: %s\n", session_.c_str(),
                     error_of(r).c_str());
        session_.clear();
        fail(wifi_error_word(error_of(r)));
        return;
    }
    busy_ = false;
    if (queued_ >= 0)
        send(queued_, clock_ms());
}

void Backlight::fail(const std::string& word)
{
    busy_ = false;
    if (on_fail)
        on_fail(word);
    if (queued_ >= 0)
        send(queued_, clock_ms());
}

}
