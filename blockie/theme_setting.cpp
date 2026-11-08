#include "theme_setting.h"

#include <cstdio>
#include <cstring>

#include <dbus/dbus.h>

namespace fd {

namespace {

constexpr const char* kDest = "net.seilent.Blockie.Settings";
constexpr const char* kPath = "/net/seilent/Blockie/Settings";
constexpr const char* kIface = "net.seilent.Blockie.Settings1";

bool variant_string(DBusMessageIter* it, std::string& out)
{
    if (dbus_message_iter_get_arg_type(it) != DBUS_TYPE_VARIANT)
        return false;
    DBusMessageIter v;
    dbus_message_iter_recurse(it, &v);
    if (dbus_message_iter_get_arg_type(&v) != DBUS_TYPE_STRING)
        return false;
    const char* s = nullptr;
    dbus_message_iter_get_basic(&v, &s);
    out = s ? s : "";
    return true;
}

}

bool ThemeSetting::parse_value(DBusMessage* m, std::string& out)
{
    if (!m || dbus_message_get_type(m) != DBUS_MESSAGE_TYPE_METHOD_RETURN)
        return false;
    DBusMessageIter it;
    if (!dbus_message_iter_init(m, &it))
        return false;
    return variant_string(&it, out);
}

bool ThemeSetting::parse_changed(DBusMessage* m, std::string& key, std::string& value)
{
    if (!m || !dbus_message_is_signal(m, kIface, "Changed"))
        return false;
    DBusMessageIter it;
    if (!dbus_message_iter_init(m, &it) || dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_STRING)
        return false;
    const char* k = nullptr;
    dbus_message_iter_get_basic(&it, &k);
    key = k ? k : "";
    if (!dbus_message_iter_next(&it))
        return false;
    return variant_string(&it, value);
}

void ThemeSetting::attach(Bus& bus)
{
    bus_ = &bus;
    bus.add_match(std::string("type='signal',sender='") + kDest + "',path='" + kPath + "',interface='" + kIface +
                  "',member='Changed'");
    bus.add_filter([this](DBusMessage* m) { return on_signal(m); });
    bus.on_connect([this](int64_t now) { fetch(now); });
}

void ThemeSetting::fetch(int64_t now)
{
    now_ = now;
    if (!bus_)
        return;
    DBusMessage* m = dbus_message_new_method_call(kDest, kPath, kIface, "Get");
    if (m) {
        const char* key = kKey;
        dbus_message_append_args(m, DBUS_TYPE_STRING, &key, DBUS_TYPE_INVALID);
    }
    bus_->call(m, [this](DBusMessage* r) {
        answered_ = true;
        std::string v;
        if (parse_value(r, v)) {
            apply(v);
            return;
        }
        const char* err = r && dbus_message_get_type(r) == DBUS_MESSAGE_TYPE_ERROR ? dbus_message_get_error_name(r) : nullptr;
        std::fprintf(stderr, "blockie-frontend: theme get failed (%s), dark\n", err ? err : "no reply");
    }, now, kTimeoutMs);
}

void ThemeSetting::set(bool light, int64_t now)
{
    now_ = now;
    if (!bus_)
        return;
    DBusMessage* m = dbus_message_new_method_call(kDest, kPath, kIface, "Set");
    if (m) {
        DBusMessageIter it, v;
        dbus_message_iter_init_append(m, &it);
        const char* key = kKey;
        const char* value = light ? "light" : "dark";
        dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &key);
        dbus_message_iter_open_container(&it, DBUS_TYPE_VARIANT, "s", &v);
        dbus_message_iter_append_basic(&v, DBUS_TYPE_STRING, &value);
        dbus_message_iter_close_container(&it, &v);
    }
    std::string want = light ? "light" : "dark";
    bus_->call(m, [this, want](DBusMessage* r) {
        if (r && dbus_message_get_type(r) == DBUS_MESSAGE_TYPE_METHOD_RETURN) {
            apply(want);
            return;
        }
        const char* err = r && dbus_message_get_type(r) == DBUS_MESSAGE_TYPE_ERROR ? dbus_message_get_error_name(r) : nullptr;
        std::fprintf(stderr, "blockie-frontend: theme set failed (%s)\n", err ? err : "no reply");
        if (on_fail)
            on_fail();
    }, now, kTimeoutMs);
}

bool ThemeSetting::on_signal(DBusMessage* m)
{
    std::string key, value;
    if (parse_changed(m, key, value) && key == kKey)
        apply(value);
    return false;
}

void ThemeSetting::apply(const std::string& v)
{
    if (v != "light" && v != "dark")
        return;
    if (on_value)
        on_value(v == "light");
}

}
