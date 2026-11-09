#include "setd_key.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <dbus/dbus.h>

namespace fd {

namespace {

int64_t clock_ms()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

bool variant_value(DBusMessageIter* it, SetdType type, std::string& out)
{
    if (dbus_message_iter_get_arg_type(it) != DBUS_TYPE_VARIANT)
        return false;
    DBusMessageIter v;
    dbus_message_iter_recurse(it, &v);
    int t = dbus_message_iter_get_arg_type(&v);
    if (type == SetdType::S && t == DBUS_TYPE_STRING) {
        const char* s = nullptr;
        dbus_message_iter_get_basic(&v, &s);
        out = s ? s : "";
        return true;
    }
    if (type == SetdType::U && t == DBUS_TYPE_UINT32) {
        dbus_uint32_t u = 0;
        dbus_message_iter_get_basic(&v, &u);
        out = std::to_string(u);
        return true;
    }
    if (type == SetdType::B && t == DBUS_TYPE_BOOLEAN) {
        dbus_bool_t b = FALSE;
        dbus_message_iter_get_basic(&v, &b);
        out = b ? "true" : "false";
        return true;
    }
    return false;
}

bool wire_value(SetdType type, const std::string& in, std::string& out)
{
    if (type == SetdType::S) {
        out = in;
        return true;
    }
    if (type == SetdType::B) {
        if (in != "true" && in != "false")
            return false;
        out = in;
        return true;
    }
    if (in.empty())
        return false;
    uint64_t n = 0;
    for (char c : in) {
        if (c < '0' || c > '9')
            return false;
        n = n * 10 + (uint64_t)(c - '0');
        if (n > 0xFFFFFFFFull)
            return false;
    }
    out = std::to_string(n);
    return true;
}

const char* error_of(DBusMessage* r)
{
    return r && dbus_message_get_type(r) == DBUS_MESSAGE_TYPE_ERROR ? dbus_message_get_error_name(r) : nullptr;
}

std::string error_text(DBusMessage* r)
{
    DBusMessageIter it;
    if (!r)
        return "no reply";
    if (!dbus_message_iter_init(r, &it) || dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_STRING)
        return "";
    const char* s = nullptr;
    dbus_message_iter_get_basic(&it, &s);
    return s ? s : "";
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

bool SetdKey::parse_value(DBusMessage* m, SetdType type, std::string& out)
{
    if (!m || dbus_message_get_type(m) != DBUS_MESSAGE_TYPE_METHOD_RETURN)
        return false;
    DBusMessageIter it;
    if (!dbus_message_iter_init(m, &it))
        return false;
    return variant_value(&it, type, out);
}

bool SetdKey::parse_changed(DBusMessage* m, std::string& key)
{
    if (!m || !dbus_message_is_signal(m, kIface, "Changed") || !dbus_message_has_signature(m, "sv"))
        return false;
    DBusMessageIter it;
    if (!dbus_message_iter_init(m, &it))
        return false;
    const char* k = nullptr;
    dbus_message_iter_get_basic(&it, &k);
    key = k ? k : "";
    return true;
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
    if (parse_value(r, type_, v)) {
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
        std::fprintf(stderr, "blockie-frontend: %s get failed (%s)\n", key_.c_str(),
                     err ? err : r ? "wrong type" : "no reply");
        if (back && !setting_ && on_fail)
            on_fail(fail_error_, fail_text_);
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
    std::string v;
    if (!wire_value(type_, value, v)) {
        std::string text = key_ + " takes " + (char)type_;
        std::fprintf(stderr, "blockie-frontend: %s set %s refused (%s)\n", key_.c_str(), value.c_str(), text.c_str());
        if (on_fail)
            on_fail(std::string(kErr) + "Invalid", text);
        return;
    }
    if (setting_) {
        next_ = v;
        queued_ = true;
        return;
    }
    send(v, now);
}

void SetdKey::send(const std::string& value, int64_t now)
{
    setting_ = true;
    DBusMessage* m = dbus_message_new_method_call(kDest, kPath, kIface, "Set");
    if (m) {
        DBusMessageIter it, v;
        dbus_message_iter_init_append(m, &it);
        const char* key = key_.c_str();
        dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &key);
        if (type_ == SetdType::U) {
            dbus_uint32_t u = (dbus_uint32_t)std::strtoul(value.c_str(), nullptr, 10);
            dbus_message_iter_open_container(&it, DBUS_TYPE_VARIANT, DBUS_TYPE_UINT32_AS_STRING, &v);
            dbus_message_iter_append_basic(&v, DBUS_TYPE_UINT32, &u);
        } else if (type_ == SetdType::B) {
            dbus_bool_t b = value == "true" ? TRUE : FALSE;
            dbus_message_iter_open_container(&it, DBUS_TYPE_VARIANT, DBUS_TYPE_BOOLEAN_AS_STRING, &v);
            dbus_message_iter_append_basic(&v, DBUS_TYPE_BOOLEAN, &b);
        } else {
            const char* s = value.c_str();
            dbus_message_iter_open_container(&it, DBUS_TYPE_VARIANT, DBUS_TYPE_STRING_AS_STRING, &v);
            dbus_message_iter_append_basic(&v, DBUS_TYPE_STRING, &s);
        }
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
        fail_error_ = err ? err : DBUS_ERROR_NO_REPLY;
        fail_text_ = error_text(r);
        back_if_get_fails_ = true;
        fetch(clock_ms());
        return;
    }
    if (on_fail)
        on_fail(err, error_text(r));
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
    std::string key;
    if (parse_changed(m, key) && key == key_)
        fetch(clock_ms());
    return false;
}

}
