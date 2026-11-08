#include "setd_key.h"

#include <chrono>
#include <cstdio>
#include <cstring>

#include <dbus/dbus.h>

namespace fd {

namespace {

int64_t clock_ms()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

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

const char* error_of(DBusMessage* r)
{
    return r && dbus_message_get_type(r) == DBUS_MESSAGE_TYPE_ERROR ? dbus_message_get_error_name(r) : nullptr;
}

bool setd_error(const char* err, const char* kind)
{
    size_t n = std::strlen(SetdKey::kErr);
    return err && std::strncmp(err, SetdKey::kErr, n) == 0 && std::strcmp(err + n, kind) == 0;
}

bool no_reply(const char* err)
{
    return !err || std::strcmp(err, DBUS_ERROR_NO_REPLY) == 0 || std::strcmp(err, DBUS_ERROR_DISCONNECTED) == 0 ||
           std::strcmp(err, DBUS_ERROR_SERVICE_UNKNOWN) == 0;
}

}

bool SetdKey::parse_value(DBusMessage* m, std::string& out)
{
    if (!m || dbus_message_get_type(m) != DBUS_MESSAGE_TYPE_METHOD_RETURN)
        return false;
    DBusMessageIter it;
    if (!dbus_message_iter_init(m, &it))
        return false;
    return variant_string(&it, out);
}

bool SetdKey::parse_changed(DBusMessage* m, std::string& key, std::string& value)
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

Bus::Reply SetdKey::guard(std::function<void(DBusMessage*)> f)
{
    std::weak_ptr<int> alive = life_;
    return [alive, f](DBusMessage* r) {
        if (!alive.expired())
            f(r);
    };
}

void SetdKey::attach(Bus& bus)
{
    bus_ = &bus;
    std::weak_ptr<int> alive = life_;
    bus.add_match(std::string("type='signal',sender='") + kDest + "',path='" + kPath + "',interface='" + kIface +
                  "',member='Changed'");
    bus.add_match(std::string("type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',"
                              "member='NameOwnerChanged',arg0='") +
                  kDest + "'");
    bus.add_filter([this, alive](DBusMessage* m) { return !alive.expired() && on_signal(m); });
    bus.on_connect([this, alive](int64_t now) {
        if (!alive.expired())
            fetch(now);
    });
}

void SetdKey::fetch(int64_t now)
{
    if (!bus_)
        return;
    if (getting_) {
        again_ = true;
        return;
    }
    getting_ = true;
    DBusMessage* m = dbus_message_new_method_call(kDest, kPath, kIface, "Get");
    if (m) {
        const char* key = key_.c_str();
        dbus_message_append_args(m, DBUS_TYPE_STRING, &key, DBUS_TYPE_INVALID);
    }
    bus_->call(m, guard([this](DBusMessage* r) { got(r); }), now, kTimeoutMs);
}

void SetdKey::got(DBusMessage* r)
{
    answered_ = true;
    getting_ = false;
    bool back = back_if_get_fails_;
    back_if_get_fails_ = false;
    std::string v;
    const char* err = error_of(r);
    if (parse_value(r, v)) {
        value_ = v;
        has_ = true;
        missing_ = false;
        if (!setting_ && on_value)
            on_value(v);
    } else if (setd_error(err, "UnknownKey")) {
        std::fprintf(stderr, "blockie-frontend: %s is not a setd key\n", key_.c_str());
        value_.clear();
        has_ = false;
        missing_ = true;
        if (on_missing)
            on_missing();
    } else {
        std::fprintf(stderr, "blockie-frontend: %s get failed (%s)\n", key_.c_str(), err ? err : "no reply");
        if (back && !setting_ && on_fail)
            on_fail();
    }
    if (again_) {
        again_ = false;
        fetch(clock_ms());
    }
}

void SetdKey::set(const std::string& value, int64_t now)
{
    if (!bus_)
        return;
    if (setting_) {
        next_ = value;
        queued_ = true;
        return;
    }
    send(value, now);
}

void SetdKey::send(const std::string& value, int64_t now)
{
    setting_ = true;
    DBusMessage* m = dbus_message_new_method_call(kDest, kPath, kIface, "Set");
    if (m) {
        DBusMessageIter it, v;
        dbus_message_iter_init_append(m, &it);
        const char* key = key_.c_str();
        const char* s = value.c_str();
        dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &key);
        dbus_message_iter_open_container(&it, DBUS_TYPE_VARIANT, "s", &v);
        dbus_message_iter_append_basic(&v, DBUS_TYPE_STRING, &s);
        dbus_message_iter_close_container(&it, &v);
    }
    bus_->call(m, guard([this, value](DBusMessage* r) { sent(value, r); }), now, kTimeoutMs);
}

void SetdKey::sent(const std::string& value, DBusMessage* r)
{
    setting_ = false;
    if (queued_) {
        queued_ = false;
        std::string next = std::move(next_);
        next_.clear();
        send(next, clock_ms());
        return;
    }
    if (r && dbus_message_get_type(r) == DBUS_MESSAGE_TYPE_METHOD_RETURN) {
        value_ = value;
        has_ = true;
        missing_ = false;
        if (on_value)
            on_value(value);
        return;
    }
    const char* err = error_of(r);
    std::fprintf(stderr, "blockie-frontend: %s set %s failed (%s)\n", key_.c_str(), value.c_str(),
                 err ? err : "no reply");
    if (setd_error(err, "Timeout") || no_reply(err)) {
        back_if_get_fails_ = true;
        fetch(clock_ms());
        return;
    }
    if (on_fail)
        on_fail();
    fetch(clock_ms());
}

bool SetdKey::on_signal(DBusMessage* m)
{
    if (dbus_message_is_signal(m, DBUS_INTERFACE_DBUS, "NameOwnerChanged")) {
        const char* from = dbus_message_get_sender(m);
        const char *n = nullptr, *was = nullptr, *now_owner = nullptr;
        if (from && std::strcmp(from, DBUS_SERVICE_DBUS) == 0 &&
            dbus_message_get_args(m, nullptr, DBUS_TYPE_STRING, &n, DBUS_TYPE_STRING, &was, DBUS_TYPE_STRING,
                                  &now_owner, DBUS_TYPE_INVALID) &&
            std::strcmp(n, kDest) == 0 && *now_owner)
            fetch(clock_ms());
        return false;
    }
    std::string key, value;
    if (parse_changed(m, key, value) && key == key_)
        fetch(clock_ms());
    return false;
}

}
