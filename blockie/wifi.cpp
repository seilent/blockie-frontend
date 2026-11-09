#include "wifi.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>

#include <dbus/dbus.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fd {

namespace {

const char* const kNm = "org.freedesktop.NetworkManager";
const char* const kNmPath = "/org/freedesktop/NetworkManager";
const char* const kSettingsPath = "/org/freedesktop/NetworkManager/Settings";
const char* const kSettingsIface = "org.freedesktop.NetworkManager.Settings";
const char* const kConnIface = "org.freedesktop.NetworkManager.Settings.Connection";
const char* const kDeviceIface = "org.freedesktop.NetworkManager.Device";
const char* const kWirelessIface = "org.freedesktop.NetworkManager.Device.Wireless";
const char* const kApIface = "org.freedesktop.NetworkManager.AccessPoint";
const char* const kProps = "org.freedesktop.DBus.Properties";
const char* const kSecurity = "802-11-wireless-security";
constexpr dbus_uint32_t kToDisk = 0x1;

const char* const kMatches[] = {
    "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',member='NameOwnerChanged',"
    "arg0='org.freedesktop.NetworkManager'",
    "type='signal',sender='org.freedesktop.NetworkManager',path='/org/freedesktop/NetworkManager',"
    "interface='org.freedesktop.DBus.Properties',member='PropertiesChanged'",
    "type='signal',sender='org.freedesktop.NetworkManager',interface='org.freedesktop.NetworkManager.Device',"
    "member='StateChanged'",
};

const char* const kPageMatches[] = {
    "type='signal',sender='org.freedesktop.NetworkManager',interface='org.freedesktop.NetworkManager.Device.Wireless'",
    "type='signal',sender='org.freedesktop.NetworkManager',interface='org.freedesktop.DBus.Properties',"
    "member='PropertiesChanged',arg0='org.freedesktop.NetworkManager.Device.Wireless'",
    "type='signal',sender='org.freedesktop.NetworkManager',interface='org.freedesktop.DBus.Properties',"
    "member='PropertiesChanged',arg0='org.freedesktop.NetworkManager.AccessPoint'",
};

int64_t clock_ms()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

struct NmValue {
    enum Kind { None, Bool, Int, Str, Bytes, List };
    Kind kind = None;
    bool b = false;
    int64_t i = 0;
    std::string s;
    std::vector<std::string> list;
};

using NmProps = std::map<std::string, NmValue>;

bool ok_reply(DBusMessage* r) { return r && dbus_message_get_type(r) == DBUS_MESSAGE_TYPE_METHOD_RETURN; }

std::string error_of(DBusMessage* r)
{
    if (!r)
        return DBUS_ERROR_NO_REPLY;
    const char* e = dbus_message_get_error_name(r);
    return e ? e : "error";
}

void log_failed(const char* what, DBusMessage* r)
{
    std::fprintf(stderr, "blockie-frontend: wifi %s failed: %s\n", what, error_of(r).c_str());
}

void read_value(DBusMessageIter* it, NmValue& out)
{
    int t = dbus_message_iter_get_arg_type(it);
    if (t == DBUS_TYPE_VARIANT) {
        DBusMessageIter v;
        dbus_message_iter_recurse(it, &v);
        read_value(&v, out);
        return;
    }
    if (t == DBUS_TYPE_BOOLEAN) {
        dbus_bool_t b = FALSE;
        dbus_message_iter_get_basic(it, &b);
        out.kind = NmValue::Bool;
        out.b = b != FALSE;
        return;
    }
    if (t == DBUS_TYPE_STRING || t == DBUS_TYPE_OBJECT_PATH) {
        const char* s = nullptr;
        dbus_message_iter_get_basic(it, &s);
        out.kind = NmValue::Str;
        out.s = s ? s : "";
        return;
    }
    if (t == DBUS_TYPE_BYTE || t == DBUS_TYPE_INT16 || t == DBUS_TYPE_UINT16 || t == DBUS_TYPE_INT32 ||
        t == DBUS_TYPE_UINT32 || t == DBUS_TYPE_INT64 || t == DBUS_TYPE_UINT64) {
        DBusBasicValue b;
        std::memset(&b, 0, sizeof b);
        dbus_message_iter_get_basic(it, &b);
        switch (t) {
        case DBUS_TYPE_BYTE: out.i = b.byt; break;
        case DBUS_TYPE_INT16: out.i = b.i16; break;
        case DBUS_TYPE_UINT16: out.i = b.u16; break;
        case DBUS_TYPE_INT32: out.i = b.i32; break;
        case DBUS_TYPE_UINT32: out.i = b.u32; break;
        case DBUS_TYPE_INT64: out.i = b.i64; break;
        default: out.i = (int64_t)b.u64; break;
        }
        out.kind = NmValue::Int;
        return;
    }
    if (t != DBUS_TYPE_ARRAY)
        return;
    int e = dbus_message_iter_get_element_type(it);
    DBusMessageIter a;
    dbus_message_iter_recurse(it, &a);
    if (e == DBUS_TYPE_BYTE) {
        const unsigned char* p = nullptr;
        int n = 0;
        dbus_message_iter_get_fixed_array(&a, &p, &n);
        out.kind = NmValue::Bytes;
        out.s.assign(p ? (const char*)p : "", p ? (size_t)n : 0);
        return;
    }
    if (e != DBUS_TYPE_STRING && e != DBUS_TYPE_OBJECT_PATH)
        return;
    out.kind = NmValue::List;
    while (dbus_message_iter_get_arg_type(&a) == e) {
        const char* s = nullptr;
        dbus_message_iter_get_basic(&a, &s);
        out.list.push_back(s ? s : "");
        dbus_message_iter_next(&a);
    }
}

void read_props(DBusMessageIter* arr, NmProps& out)
{
    if (dbus_message_iter_get_arg_type(arr) != DBUS_TYPE_ARRAY)
        return;
    DBusMessageIter e;
    dbus_message_iter_recurse(arr, &e);
    while (dbus_message_iter_get_arg_type(&e) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter kv;
        dbus_message_iter_recurse(&e, &kv);
        if (dbus_message_iter_get_arg_type(&kv) == DBUS_TYPE_STRING) {
            const char* k = nullptr;
            dbus_message_iter_get_basic(&kv, &k);
            dbus_message_iter_next(&kv);
            NmValue v;
            read_value(&kv, v);
            if (k && v.kind != NmValue::None)
                out[k] = v;
        }
        dbus_message_iter_next(&e);
    }
}

bool flag(const NmProps& p, const char* k, bool& out)
{
    auto it = p.find(k);
    if (it == p.end() || it->second.kind != NmValue::Bool)
        return false;
    out = it->second.b;
    return true;
}

bool num(const NmProps& p, const char* k, int64_t& out)
{
    auto it = p.find(k);
    if (it == p.end() || it->second.kind != NmValue::Int)
        return false;
    out = it->second.i;
    return true;
}

bool text(const NmProps& p, const char* k, std::string& out)
{
    auto it = p.find(k);
    if (it == p.end() || (it->second.kind != NmValue::Str && it->second.kind != NmValue::Bytes))
        return false;
    out = it->second.s;
    return true;
}

bool reply_value(DBusMessage* r, NmValue& out)
{
    DBusMessageIter it;
    if (!ok_reply(r) || !dbus_message_has_signature(r, "v") || !dbus_message_iter_init(r, &it))
        return false;
    read_value(&it, out);
    return true;
}

bool reply_props(DBusMessage* r, NmProps& out)
{
    DBusMessageIter it;
    if (!ok_reply(r) || !dbus_message_has_signature(r, "a{sv}") || !dbus_message_iter_init(r, &it))
        return false;
    read_props(&it, out);
    return true;
}

std::vector<std::string> reply_paths(DBusMessage* r)
{
    std::vector<std::string> v;
    DBusMessageIter it, a;
    if (!ok_reply(r) || !dbus_message_has_signature(r, "ao") || !dbus_message_iter_init(r, &it))
        return v;
    dbus_message_iter_recurse(&it, &a);
    while (dbus_message_iter_get_arg_type(&a) == DBUS_TYPE_OBJECT_PATH) {
        const char* s = nullptr;
        dbus_message_iter_get_basic(&a, &s);
        v.push_back(s ? s : "");
        dbus_message_iter_next(&a);
    }
    return v;
}

DBusMessage* method(const std::string& path, const char* iface, const char* member)
{
    return dbus_message_new_method_call(kNm, path.c_str(), iface, member);
}

DBusMessage* prop_get(const std::string& path, const char* iface, const char* name)
{
    DBusMessage* m = dbus_message_new_method_call(kNm, path.c_str(), kProps, "Get");
    if (m)
        dbus_message_append_args(m, DBUS_TYPE_STRING, &iface, DBUS_TYPE_STRING, &name, DBUS_TYPE_INVALID);
    return m;
}

DBusMessage* prop_all(const std::string& path, const char* iface)
{
    DBusMessage* m = dbus_message_new_method_call(kNm, path.c_str(), kProps, "GetAll");
    if (m)
        dbus_message_append_args(m, DBUS_TYPE_STRING, &iface, DBUS_TYPE_INVALID);
    return m;
}

void put_str(DBusMessageIter* dict, const char* key, const char* v)
{
    DBusMessageIter e, var;
    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, nullptr, &e);
    dbus_message_iter_append_basic(&e, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&e, DBUS_TYPE_VARIANT, DBUS_TYPE_STRING_AS_STRING, &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &v);
    dbus_message_iter_close_container(&e, &var);
    dbus_message_iter_close_container(dict, &e);
}

void put_u32(DBusMessageIter* dict, const char* key, dbus_uint32_t v)
{
    DBusMessageIter e, var;
    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, nullptr, &e);
    dbus_message_iter_append_basic(&e, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&e, DBUS_TYPE_VARIANT, DBUS_TYPE_UINT32_AS_STRING, &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_UINT32, &v);
    dbus_message_iter_close_container(&e, &var);
    dbus_message_iter_close_container(dict, &e);
}

void put_bytes(DBusMessageIter* dict, const char* key, const std::string& v)
{
    DBusMessageIter e, var, arr;
    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, nullptr, &e);
    dbus_message_iter_append_basic(&e, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&e, DBUS_TYPE_VARIANT, "ay", &var);
    dbus_message_iter_open_container(&var, DBUS_TYPE_ARRAY, DBUS_TYPE_BYTE_AS_STRING, &arr);
    const unsigned char* p = (const unsigned char*)v.data();
    dbus_message_iter_append_fixed_array(&arr, DBUS_TYPE_BYTE, &p, (int)v.size());
    dbus_message_iter_close_container(&var, &arr);
    dbus_message_iter_close_container(&e, &var);
    dbus_message_iter_close_container(dict, &e);
}

void open_section(DBusMessageIter* outer, const char* name, DBusMessageIter* entry, DBusMessageIter* props)
{
    dbus_message_iter_open_container(outer, DBUS_TYPE_DICT_ENTRY, nullptr, entry);
    dbus_message_iter_append_basic(entry, DBUS_TYPE_STRING, &name);
    dbus_message_iter_open_container(entry, DBUS_TYPE_ARRAY, "{sv}", props);
}

void close_section(DBusMessageIter* outer, DBusMessageIter* entry, DBusMessageIter* props)
{
    dbus_message_iter_close_container(entry, props);
    dbus_message_iter_close_container(outer, entry);
}

void put_psk(DBusMessageIter* props, const std::string& psk)
{
    put_str(props, "key-mgmt", "wpa-psk");
    put_str(props, "psk", psk.c_str());
    put_u32(props, "psk-flags", 0);
}

void append_wifi(DBusMessageIter* it, const std::string& ssid, const std::string* psk, const std::string& id,
                 const std::string& uuid)
{
    DBusMessageIter outer, e, p;
    dbus_message_iter_open_container(it, DBUS_TYPE_ARRAY, "{sa{sv}}", &outer);
    if (!id.empty()) {
        open_section(&outer, "connection", &e, &p);
        put_str(&p, "id", id.c_str());
        if (!uuid.empty())
            put_str(&p, "uuid", uuid.c_str());
        put_str(&p, "type", "802-11-wireless");
        close_section(&outer, &e, &p);
    }
    open_section(&outer, "802-11-wireless", &e, &p);
    put_bytes(&p, "ssid", ssid);
    put_str(&p, "mode", "infrastructure");
    close_section(&outer, &e, &p);
    if (psk) {
        open_section(&outer, kSecurity, &e, &p);
        put_psk(&p, *psk);
        close_section(&outer, &e, &p);
    }
    dbus_message_iter_close_container(it, &outer);
}

void copy_value(DBusMessageIter* from, DBusMessageIter* to)
{
    int t = dbus_message_iter_get_arg_type(from);
    if (dbus_type_is_basic(t)) {
        DBusBasicValue v;
        std::memset(&v, 0, sizeof v);
        dbus_message_iter_get_basic(from, &v);
        dbus_message_iter_append_basic(to, t, &v);
        return;
    }
    DBusMessageIter sub, out;
    dbus_message_iter_recurse(from, &sub);
    char* sig = nullptr;
    const char* contained = nullptr;
    if (t == DBUS_TYPE_VARIANT) {
        sig = dbus_message_iter_get_signature(&sub);
        contained = sig;
    } else if (t == DBUS_TYPE_ARRAY) {
        sig = dbus_message_iter_get_signature(from);
        contained = sig ? sig + 1 : nullptr;
    }
    dbus_message_iter_open_container(to, t, contained, &out);
    while (dbus_message_iter_get_arg_type(&sub) != DBUS_TYPE_INVALID) {
        copy_value(&sub, &out);
        dbus_message_iter_next(&sub);
    }
    dbus_message_iter_close_container(to, &out);
    if (sig)
        dbus_free(sig);
}

void append_rekeyed(DBusMessageIter* from, DBusMessageIter* to, const std::string& psk)
{
    DBusMessageIter in, outer;
    dbus_message_iter_recurse(from, &in);
    dbus_message_iter_open_container(to, DBUS_TYPE_ARRAY, "{sa{sv}}", &outer);
    bool done = false;
    while (dbus_message_iter_get_arg_type(&in) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter kv;
        dbus_message_iter_recurse(&in, &kv);
        const char* name = nullptr;
        dbus_message_iter_get_basic(&kv, &name);
        if (!name || std::strcmp(name, kSecurity) != 0) {
            copy_value(&in, &outer);
            dbus_message_iter_next(&in);
            continue;
        }
        dbus_message_iter_next(&kv);
        DBusMessageIter e, p, props;
        open_section(&outer, kSecurity, &e, &p);
        dbus_message_iter_recurse(&kv, &props);
        while (dbus_message_iter_get_arg_type(&props) == DBUS_TYPE_DICT_ENTRY) {
            DBusMessageIter pk;
            dbus_message_iter_recurse(&props, &pk);
            const char* k = nullptr;
            dbus_message_iter_get_basic(&pk, &k);
            if (k && std::strcmp(k, "psk") != 0 && std::strcmp(k, "psk-flags") != 0 && std::strcmp(k, "key-mgmt") != 0)
                copy_value(&props, &p);
            dbus_message_iter_next(&props);
        }
        put_psk(&p, psk);
        close_section(&outer, &e, &p);
        done = true;
        dbus_message_iter_next(&in);
    }
    if (!done) {
        DBusMessageIter e, p;
        open_section(&outer, kSecurity, &e, &p);
        put_psk(&p, psk);
        close_section(&outer, &e, &p);
    }
    dbus_message_iter_close_container(to, &outer);
}

bool read_settings(DBusMessage* r, WifiSaved& out, bool& wifi)
{
    DBusMessageIter it, a;
    if (!ok_reply(r) || !dbus_message_has_signature(r, "a{sa{sv}}") || !dbus_message_iter_init(r, &it))
        return false;
    dbus_message_iter_recurse(&it, &a);
    std::string type, key_mgmt;
    bool security = false;
    while (dbus_message_iter_get_arg_type(&a) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter e;
        dbus_message_iter_recurse(&a, &e);
        const char* name = nullptr;
        dbus_message_iter_get_basic(&e, &name);
        dbus_message_iter_next(&e);
        NmProps p;
        read_props(&e, p);
        std::string n = name ? name : "";
        if (n == "connection")
            text(p, "type", type);
        else if (n == "802-11-wireless")
            text(p, "ssid", out.ssid);
        else if (n == kSecurity) {
            security = true;
            text(p, "key-mgmt", key_mgmt);
        }
        dbus_message_iter_next(&a);
    }
    wifi = type == "802-11-wireless";
    out.sec = wifi_key_mgmt(security, key_mgmt);
    return true;
}

void ap_apply(WifiAp& a, const NmProps& p)
{
    int64_t v = 0;
    text(p, "Ssid", a.ssid);
    if (num(p, "Strength", v))
        a.strength = (int)v;
    if (num(p, "Flags", v))
        a.flags = (uint32_t)v;
    if (num(p, "WpaFlags", v))
        a.wpa = (uint32_t)v;
    if (num(p, "RsnFlags", v))
        a.rsn = (uint32_t)v;
    if (num(p, "Frequency", v))
        a.freq = (uint32_t)v;
}

std::string make_uuid()
{
    std::string u;
    FILE* f = std::fopen("/proc/sys/kernel/random/uuid", "r");
    if (!f)
        return u;
    char buf[64];
    if (std::fgets(buf, sizeof buf, f))
        u = buf;
    std::fclose(f);
    while (!u.empty() && (u.back() == '\n' || u.back() == ' '))
        u.pop_back();
    return u;
}

void fsync_dir(const std::string& file)
{
    size_t slash = file.rfind('/');
    std::string dir = slash == std::string::npos ? "." : slash == 0 ? "/" : file.substr(0, slash);
    int d = open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (d >= 0) {
        fsync(d);
        close(d);
    }
}

bool wrong_reason(uint32_t reason) { return reason == kNmReasonNoSecrets || reason == kNmReasonSupplicantDisconnect; }

}

Bus::Reply Wifi::guard(std::function<void(DBusMessage*)> f)
{
    std::weak_ptr<int> alive = life_;
    return [alive, f](DBusMessage* r) {
        if (!alive.expired())
            f(r);
    };
}

void Wifi::start()
{
    if (started_)
        return;
    started_ = true;
    std::weak_ptr<int> alive = life_;
    bus_.add_filter([this, alive](DBusMessage* m) { return !alive.expired() && on_message(m); });
    for (const char* rule : kMatches)
        bus_.add_match(rule);
    bus_.on_connect([this, alive](int64_t now) {
        if (alive.expired())
            return;
        drop();
        owner_.clear();
        fetch_due_ = false;
        fetch(now);
    });
    if (bus_.connected())
        fetch_due_ = true;
}

void Wifi::drop()
{
    WifiNote keep = s_.note;
    s_ = WifiState{};
    s_.note = keep;
    if (importing_)
        txt_due_ = true;
    importing_ = false;
    saved_ok_ = false;
    scan_until_ = -1;
    later_.clear();
    gen_++;
    changed();
}

void Wifi::owned(DBusMessage* r)
{
    const char* s = ok_reply(r) ? dbus_message_get_sender(r) : nullptr;
    if (s && owner_.empty())
        owner_ = s;
}

void Wifi::fetch(int64_t now)
{
    uint32_t g = gen_;
    find_device(now);
    bus_.call(prop_get(kNmPath, kNm, "WirelessEnabled"), guard([this, g](DBusMessage* r) {
                  NmValue v;
                  if (g != gen_)
                      return;
                  if (!reply_value(r, v) || v.kind != NmValue::Bool) {
                      log_failed("WirelessEnabled", r);
                      return;
                  }
                  owned(r);
                  s_.radio = v.b;
                  changed();
              }),
              now, kCallMs);
    fetch_saved(now);
}

void Wifi::find_device(int64_t now)
{
    uint32_t g = gen_;
    DBusMessage* m = method(kNmPath, kNm, "GetDeviceByIpIface");
    const char* name = kIface;
    if (m)
        dbus_message_append_args(m, DBUS_TYPE_STRING, &name, DBUS_TYPE_INVALID);
    bus_.call(m, guard([this, g](DBusMessage* r) {
                  const char* path = nullptr;
                  if (g != gen_)
                      return;
                  if (!ok_reply(r) ||
                      !dbus_message_get_args(r, nullptr, DBUS_TYPE_OBJECT_PATH, &path, DBUS_TYPE_INVALID)) {
                      log_failed("GetDeviceByIpIface", r);
                      return;
                  }
                  owned(r);
                  s_.device = path;
                  s_.up = true;
                  changed();
                  int64_t t = clock_ms();
                  fetch_state(t);
                  fetch_active(t);
                  if (page_)
                      fetch_aps(t, false);
              }),
              now, kCallMs);
}

void Wifi::fetch_state(int64_t now)
{
    uint32_t g = gen_;
    bus_.call(prop_get(s_.device, kDeviceIface, "State"), guard([this, g](DBusMessage* r) {
                  NmValue v;
                  if (g != gen_ || !reply_value(r, v) || v.kind != NmValue::Int)
                      return;
                  s_.dev_state = (uint32_t)v.i;
                  s_.ip_since = s_.dev_state == kNmDeviceIpConfig ? clock_ms() : -1;
                  changed();
              }),
              now, kCallMs);
}

void Wifi::fetch_active(int64_t now)
{
    uint32_t g = gen_;
    bus_.call(prop_get(s_.device, kWirelessIface, "ActiveAccessPoint"), guard([this, g](DBusMessage* r) {
                  NmValue v;
                  if (g != gen_ || !reply_value(r, v) || v.kind != NmValue::Str)
                      return;
                  std::string ap = v.s == "/" ? std::string() : v.s;
                  if (ap != s_.active_ap) {
                      s_.active_ap = ap;
                      changed();
                  }
              }),
              now, kCallMs);
}

void Wifi::fetch_saved(int64_t now)
{
    uint32_t g = gen_, sg = ++saved_gen_;
    bus_.call(method(kSettingsPath, kSettingsIface, "ListConnections"), guard([this, g, sg](DBusMessage* r) {
                  if (g != gen_ || sg != saved_gen_)
                      return;
                  if (!ok_reply(r) || !dbus_message_has_signature(r, "ao")) {
                      log_failed("ListConnections", r);
                      return;
                  }
                  owned(r);
                  std::vector<std::string> paths = reply_paths(r);
                  struct Acc {
                      std::vector<WifiSaved> items;
                      std::vector<bool> wifi;
                      int left = 0;
                  };
                  auto acc = std::make_shared<Acc>();
                  acc->items.resize(paths.size());
                  acc->wifi.assign(paths.size(), false);
                  acc->left = 2 * (int)paths.size() + 1;
                  auto done = [this, g, sg, acc]() {
                      if (--acc->left > 0 || g != gen_ || sg != saved_gen_)
                          return;
                      s_.saved.clear();
                      for (size_t i = 0; i < acc->items.size(); i++)
                          if (acc->wifi[i])
                              s_.saved.push_back(acc->items[i]);
                      saved_ok_ = true;
                      changed();
                  };
                  int64_t t = clock_ms();
                  for (size_t i = 0; i < paths.size(); i++) {
                      acc->items[i].path = paths[i];
                      bus_.call(method(paths[i], kConnIface, "GetSettings"), guard([acc, i, done](DBusMessage* rr) {
                                    bool wifi = false;
                                    if (read_settings(rr, acc->items[i], wifi))
                                        acc->wifi[i] = wifi;
                                    done();
                                }),
                                t, kCallMs);
                      bus_.call(prop_get(paths[i], kConnIface, "Flags"), guard([acc, i, done](DBusMessage* rr) {
                                    NmValue v;
                                    if (reply_value(rr, v) && v.kind == NmValue::Int)
                                        acc->items[i].temp = (v.i & kNmConnVolatile) != 0;
                                    done();
                                }),
                                t, kCallMs);
                  }
                  done();
              }),
              now, kCallMs);
}

void Wifi::fetch_aps(int64_t now, bool order)
{
    if (s_.device.empty())
        return;
    uint32_t g = gen_, ag = ++ap_gen_;
    bus_.call(method(s_.device, kWirelessIface, "GetAllAccessPoints"), guard([this, g, ag, order](DBusMessage* r) {
                  if (g != gen_ || ag != ap_gen_)
                      return;
                  if (!ok_reply(r) || !dbus_message_has_signature(r, "ao")) {
                      log_failed("GetAllAccessPoints", r);
                      return;
                  }
                  std::vector<std::string> paths = reply_paths(r);
                  struct Acc {
                      std::vector<WifiAp> aps;
                      std::vector<bool> ok;
                      int left = 0;
                  };
                  auto acc = std::make_shared<Acc>();
                  acc->aps.resize(paths.size());
                  acc->ok.assign(paths.size(), false);
                  acc->left = (int)paths.size() + 1;
                  auto done = [this, g, ag, order, acc]() {
                      if (--acc->left > 0 || g != gen_ || ag != ap_gen_)
                          return;
                      s_.aps.clear();
                      for (size_t i = 0; i < acc->aps.size(); i++)
                          if (acc->ok[i])
                              s_.aps.push_back(acc->aps[i]);
                      if (order)
                          s_.order = wifi_order(s_, clock_ms());
                      changed();
                  };
                  int64_t t = clock_ms();
                  for (size_t i = 0; i < paths.size(); i++) {
                      std::string path = paths[i];
                      bus_.call(prop_all(path, kApIface), guard([acc, i, path, done](DBusMessage* rr) {
                                    NmProps p;
                                    if (reply_props(rr, p)) {
                                        acc->aps[i].path = path;
                                        ap_apply(acc->aps[i], p);
                                        acc->ok[i] = true;
                                    }
                                    done();
                                }),
                                t, kCallMs);
                  }
                  done();
              }),
              now, kCallMs);
}

void Wifi::fetch_ap(const std::string& path, int64_t now)
{
    uint32_t g = gen_;
    bus_.call(prop_all(path, kApIface), guard([this, g, path](DBusMessage* r) {
                  NmProps p;
                  if (g != gen_ || !page_ || !reply_props(r, p))
                      return;
                  auto it = std::find_if(s_.aps.begin(), s_.aps.end(), [&](const WifiAp& a) { return a.path == path; });
                  if (it == s_.aps.end()) {
                      s_.aps.push_back(WifiAp{});
                      it = s_.aps.end() - 1;
                      it->path = path;
                  }
                  ap_apply(*it, p);
                  changed();
              }),
              now, kCallMs);
}

bool Wifi::on_message(DBusMessage* m)
{
    if (dbus_message_get_type(m) != DBUS_MESSAGE_TYPE_SIGNAL)
        return false;
    int64_t now = clock_ms();
    if (dbus_message_is_signal(m, DBUS_INTERFACE_DBUS, "NameOwnerChanged")) {
        const char* from = dbus_message_get_sender(m);
        const char *n = nullptr, *was = nullptr, *now_owner = nullptr;
        if (!from || std::strcmp(from, DBUS_SERVICE_DBUS) != 0 ||
            !dbus_message_get_args(m, nullptr, DBUS_TYPE_STRING, &n, DBUS_TYPE_STRING, &was, DBUS_TYPE_STRING,
                                   &now_owner, DBUS_TYPE_INVALID) ||
            std::strcmp(n, kNm) != 0)
            return false;
        std::fprintf(stderr, "blockie-frontend: wifi %s owner %s\n", kNm, *now_owner ? now_owner : "gone");
        drop();
        owner_ = now_owner;
        fetch_due_ = !owner_.empty();
        return false;
    }
    const char* sender = dbus_message_get_sender(m);
    const char* path = dbus_message_get_path(m);
    if (owner_.empty() || !sender || owner_ != sender || !path)
        return false;
    if (dbus_message_is_signal(m, kDeviceIface, "StateChanged")) {
        dbus_uint32_t st = 0, old = 0, reason = 0;
        if (s_.device == path && dbus_message_get_args(m, nullptr, DBUS_TYPE_UINT32, &st, DBUS_TYPE_UINT32, &old,
                                                       DBUS_TYPE_UINT32, &reason, DBUS_TYPE_INVALID))
            on_state(st, reason, now);
        return false;
    }
    if (dbus_message_is_signal(m, kProps, "PropertiesChanged")) {
        DBusMessageIter it;
        if (!dbus_message_has_signature(m, "sa{sv}as") || !dbus_message_iter_init(m, &it))
            return false;
        const char* iface = nullptr;
        dbus_message_iter_get_basic(&it, &iface);
        dbus_message_iter_next(&it);
        NmProps p;
        read_props(&it, p);
        std::string in = iface ? iface : "";
        bool b = false;
        std::string s;
        if (in == kNm && std::strcmp(path, kNmPath) == 0) {
            if (flag(p, "WirelessEnabled", b) && b != s_.radio) {
                s_.radio = b;
                if (!b) {
                    s_.scanning = false;
                    scan_until_ = -1;
                }
                changed();
                if (b && page_)
                    later_.push_back([this](int64_t t) { scan(t); });
            }
            if (p.count("Devices") && s_.device.empty())
                find_device(now);
        } else if (page_ && in == kWirelessIface && s_.device == path) {
            if (text(p, "ActiveAccessPoint", s)) {
                s_.active_ap = s == "/" ? std::string() : s;
                changed();
            }
            if (p.count("LastScan"))
                scan_done(now);
        } else if (page_ && in == kApIface) {
            for (WifiAp& a : s_.aps)
                if (a.path == path) {
                    ap_apply(a, p);
                    changed();
                }
        }
        return false;
    }
    if (!page_ || s_.device != path)
        return false;
    const char* ap = nullptr;
    if (dbus_message_is_signal(m, kWirelessIface, "AccessPointAdded") &&
        dbus_message_get_args(m, nullptr, DBUS_TYPE_OBJECT_PATH, &ap, DBUS_TYPE_INVALID)) {
        fetch_ap(ap, now);
    } else if (dbus_message_is_signal(m, kWirelessIface, "AccessPointRemoved") &&
               dbus_message_get_args(m, nullptr, DBUS_TYPE_OBJECT_PATH, &ap, DBUS_TYPE_INVALID)) {
        std::string gone = ap;
        auto it = std::remove_if(s_.aps.begin(), s_.aps.end(), [&](const WifiAp& a) { return a.path == gone; });
        if (it != s_.aps.end()) {
            s_.aps.erase(it, s_.aps.end());
            changed();
        }
    }
    return false;
}

void Wifi::on_state(uint32_t st, uint32_t reason, int64_t now)
{
    uint32_t was = s_.dev_state;
    s_.dev_state = st;
    if (st != kNmDeviceIpConfig)
        s_.ip_since = -1;
    else if (was != kNmDeviceIpConfig)
        s_.ip_since = now;
    changed();
    fetch_active(now);
    if (!s_.joining)
        return;
    if (s_.join.replied && st >= kNmDevicePrepare && st < kNmDeviceActivated)
        s_.join.began = true;
    if (st == kNmDeviceFailed || (st == kNmDeviceDisconnected && s_.join.began)) {
        join_end(false, wrong_reason(reason));
        return;
    }
    if (st < kNmDeviceDisconnected) {
        join_end(false, false);
        return;
    }
    if (st == kNmDeviceActivated && s_.join.replied && !s_.join.landed)
        check_active(now);
}

void Wifi::scan(int64_t now)
{
    if (!usable() || s_.scanning)
        return;
    uint32_t g = gen_;
    DBusMessage* m = method(s_.device, kWirelessIface, "RequestScan");
    if (m) {
        DBusMessageIter it, opts;
        dbus_message_iter_init_append(m, &it);
        dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &opts);
        dbus_message_iter_close_container(&it, &opts);
    }
    s_.scanning = true;
    s_.scan_failed = false;
    scan_until_ = now + kScanMs;
    changed();
    bus_.call(m, guard([this, g](DBusMessage* r) {
                  if (g != gen_ || ok_reply(r))
                      return;
                  log_failed("RequestScan", r);
                  s_.scanning = false;
                  s_.scan_failed = true;
                  scan_until_ = -1;
                  changed();
              }),
              now, kCallMs);
}

void Wifi::scan_done(int64_t now)
{
    s_.scanning = false;
    s_.scan_failed = false;
    s_.scanned = true;
    scan_until_ = -1;
    changed();
    fetch_aps(now, true);
}

void Wifi::start_join(const std::string& ssid, WifiSec sec, bool fresh, int64_t now)
{
    s_.joining = true;
    s_.join = WifiJoin{};
    s_.join.id = ++join_seq_;
    s_.join.ssid = ssid;
    s_.join.sec = sec;
    s_.join.fresh = fresh;
    s_.join.until = now + kJoinMs;
    if (s_.failed == wifi_key(ssid, sec)) {
        s_.failed.clear();
        s_.wrong = false;
    }
    changed();
}

void Wifi::join(WifiCmd& c, int64_t now)
{
    if (!usable() || s_.joining || c.sec == WifiSec::Unsupported || (c.sec == WifiSec::Psk && !wifi_psk_ok(c.psk)))
        return;
    const WifiAp* best = nullptr;
    for (const WifiAp& a : s_.aps)
        if (a.ssid == c.ssid && wifi_ap_sec(a.flags, a.wpa, a.rsn) == c.sec && (!best || a.strength > best->strength))
            best = &a;
    if (!best)
        return;
    std::string ap = best->path;
    DBusMessage* m = method(kNmPath, kNm, "AddAndActivateConnection2");
    if (m) {
        DBusMessageIter it, opts;
        dbus_message_iter_init_append(m, &it);
        append_wifi(&it, c.ssid, c.sec == WifiSec::Psk ? &c.psk : nullptr, "", "");
        const char* dev = s_.device.c_str();
        const char* spec = ap.c_str();
        dbus_message_iter_append_basic(&it, DBUS_TYPE_OBJECT_PATH, &dev);
        dbus_message_iter_append_basic(&it, DBUS_TYPE_OBJECT_PATH, &spec);
        dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &opts);
        put_str(&opts, "persist", "volatile");
        dbus_message_iter_close_container(&it, &opts);
    }
    wifi_wipe(c.psk);
    start_join(c.ssid, c.sec, true, now);
    std::fprintf(stderr, "blockie-frontend: wifi join %s\n", ap.c_str());
    uint32_t g = gen_, id = s_.join.id;
    bus_.call(m, guard([this, g, id](DBusMessage* r) {
                  const char *conn = nullptr, *active = nullptr;
                  if (g != gen_ || !s_.joining || s_.join.id != id)
                      return;
                  if (!ok_reply(r) || !dbus_message_get_args(r, nullptr, DBUS_TYPE_OBJECT_PATH, &conn,
                                                             DBUS_TYPE_OBJECT_PATH, &active, DBUS_TYPE_INVALID)) {
                      log_failed("AddAndActivateConnection2", r);
                      join_end(false, false);
                      return;
                  }
                  s_.join.conn = conn;
                  s_.join.active = active;
                  s_.join.replied = true;
                  if (s_.dev_state == kNmDeviceActivated)
                      later_.push_back([this](int64_t t) { check_active(t); });
              }),
              now, kCallMs);
}

void Wifi::connect(const std::string& ssid, WifiSec sec, int64_t now)
{
    const WifiSaved* sv = wifi_saved(s_, ssid, sec);
    if (!usable() || s_.joining || !sv)
        return;
    std::string conn = sv->path;
    DBusMessage* m = method(kNmPath, kNm, "ActivateConnection");
    const char* c = conn.c_str();
    const char* dev = s_.device.c_str();
    const char* spec = "/";
    if (m)
        dbus_message_append_args(m, DBUS_TYPE_OBJECT_PATH, &c, DBUS_TYPE_OBJECT_PATH, &dev, DBUS_TYPE_OBJECT_PATH,
                                 &spec, DBUS_TYPE_INVALID);
    start_join(ssid, sec, false, now);
    s_.join.conn = conn;
    std::fprintf(stderr, "blockie-frontend: wifi connect %s\n", conn.c_str());
    uint32_t g = gen_, id = s_.join.id;
    bus_.call(m, guard([this, g, id](DBusMessage* r) {
                  const char* active = nullptr;
                  if (g != gen_ || !s_.joining || s_.join.id != id)
                      return;
                  if (!ok_reply(r) ||
                      !dbus_message_get_args(r, nullptr, DBUS_TYPE_OBJECT_PATH, &active, DBUS_TYPE_INVALID)) {
                      log_failed("ActivateConnection", r);
                      join_end(false, false);
                      return;
                  }
                  s_.join.active = active;
                  s_.join.replied = true;
                  if (s_.dev_state == kNmDeviceActivated)
                      later_.push_back([this](int64_t t) { check_active(t); });
              }),
              now, kCallMs);
}

void Wifi::check_active(int64_t now)
{
    if (!s_.joining || s_.join.landed || s_.device.empty())
        return;
    s_.join.landed = true;
    uint32_t g = gen_, id = s_.join.id;
    bus_.call(prop_get(s_.device, kDeviceIface, "ActiveConnection"), guard([this, g, id](DBusMessage* r) {
                  NmValue v;
                  if (g != gen_ || !s_.joining || s_.join.id != id)
                      return;
                  if (!reply_value(r, v) || v.kind != NmValue::Str) {
                      log_failed("ActiveConnection", r);
                      s_.join.landed = false;
                      if (!s_.join.retried && s_.dev_state == kNmDeviceActivated) {
                          s_.join.retried = true;
                          later_.push_back([this](int64_t t) { check_active(t); });
                      }
                      return;
                  }
                  if (v.s != s_.join.active) {
                      s_.join.landed = false;
                      return;
                  }
                  if (s_.join.fresh)
                      persist(clock_ms());
                  else
                      join_end(true, false);
              }),
              now, kCallMs);
}

void Wifi::persist(int64_t now)
{
    uint32_t g = gen_, id = s_.join.id;
    std::string conn = s_.join.conn;
    DBusMessage* m = method(conn, kConnIface, "Update2");
    if (m) {
        DBusMessageIter it, settings, args;
        dbus_uint32_t flags = kToDisk;
        dbus_message_iter_init_append(m, &it);
        dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sa{sv}}", &settings);
        dbus_message_iter_close_container(&it, &settings);
        dbus_message_iter_append_basic(&it, DBUS_TYPE_UINT32, &flags);
        dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &args);
        dbus_message_iter_close_container(&it, &args);
    }
    bus_.call(m, guard([this, g, id, conn](DBusMessage* r) {
                  if (g != gen_)
                      return;
                  std::vector<std::string> old;
                  if (!ok_reply(r)) {
                      log_failed("Update2", r);
                      note("wi-fi", wifi_error_word(error_of(r)));
                  } else if (s_.joining && s_.join.id == id) {
                      for (const WifiSaved& sv : s_.saved)
                          if (!sv.temp && sv.path != conn && sv.ssid == s_.join.ssid && sv.sec == s_.join.sec)
                              old.push_back(sv.path);
                  }
                  if (s_.joining && s_.join.id == id)
                      join_end(true, false);
                  int64_t t = clock_ms();
                  for (const std::string& p : old) {
                      std::fprintf(stderr, "blockie-frontend: wifi replaced %s\n", p.c_str());
                      bus_.call(method(p, kConnIface, "Delete"), guard([this, g](DBusMessage* rr) {
                                    if (g != gen_)
                                        return;
                                    if (!ok_reply(rr))
                                        log_failed("Delete", rr);
                                    fetch_saved(clock_ms());
                                }),
                                t, kCallMs);
                  }
              }),
              now, kCallMs);
}

void Wifi::join_end(bool ok, bool wrong)
{
    std::string key = wifi_key(s_.join.ssid, s_.join.sec);
    std::fprintf(stderr, "blockie-frontend: wifi join %s\n", ok ? "done" : wrong ? "wrong password" : "failed");
    s_.joining = false;
    if (ok) {
        if (s_.failed == key)
            s_.failed.clear();
        s_.wrong = false;
    } else {
        s_.failed = key;
        s_.wrong = wrong;
    }
    s_.join = WifiJoin{};
    changed();
    fetch_saved(clock_ms());
}

void Wifi::disconnect(int64_t now)
{
    if (!usable())
        return;
    uint32_t g = gen_;
    bus_.call(method(s_.device, kDeviceIface, "Disconnect"), guard([this, g](DBusMessage* r) {
                  if (g != gen_ || ok_reply(r))
                      return;
                  log_failed("Disconnect", r);
                  note("wi-fi", wifi_error_word(error_of(r)));
              }),
              now, kCallMs);
}

void Wifi::forget(const std::string& ssid, WifiSec sec, int64_t now)
{
    std::vector<std::string> paths;
    for (const WifiSaved& sv : s_.saved)
        if (!sv.temp && sv.ssid == ssid && sv.sec == sec)
            paths.push_back(sv.path);
    if (s_.failed == wifi_key(ssid, sec)) {
        s_.failed.clear();
        s_.wrong = false;
        changed();
    }
    uint32_t g = gen_;
    for (const std::string& p : paths) {
        std::fprintf(stderr, "blockie-frontend: wifi forget %s\n", p.c_str());
        bus_.call(method(p, kConnIface, "Delete"), guard([this, g](DBusMessage* r) {
                      if (g != gen_)
                          return;
                      if (!ok_reply(r)) {
                          log_failed("Delete", r);
                          note("wi-fi", wifi_error_word(error_of(r)));
                      }
                      fetch_saved(clock_ms());
                  }),
                  now, kCallMs);
    }
}

void Wifi::radio(bool on, int64_t now)
{
    if (owner_.empty())
        return;
    DBusMessage* m = dbus_message_new_method_call(kNm, kNmPath, kProps, "Set");
    if (m) {
        DBusMessageIter it, var;
        const char* iface = kNm;
        const char* name = "WirelessEnabled";
        dbus_bool_t b = on ? TRUE : FALSE;
        dbus_message_iter_init_append(m, &it);
        dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &iface);
        dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &name);
        dbus_message_iter_open_container(&it, DBUS_TYPE_VARIANT, DBUS_TYPE_BOOLEAN_AS_STRING, &var);
        dbus_message_iter_append_basic(&var, DBUS_TYPE_BOOLEAN, &b);
        dbus_message_iter_close_container(&it, &var);
    }
    uint32_t g = gen_;
    bus_.call(m, guard([this, g](DBusMessage* r) {
                  if (g != gen_ || ok_reply(r))
                      return;
                  log_failed("WirelessEnabled", r);
                  note("wi-fi", wifi_error_word(error_of(r)));
              }),
              now, kCallMs);
}

void Wifi::note(const std::string& title, const std::string& text)
{
    s_.note.seq++;
    s_.note.title = title;
    s_.note.text = text;
    changed();
}

void Wifi::run(WifiCmd& c, int64_t now)
{
    switch (c.type) {
    case WifiCmd::Scan:
        scan(now);
        break;
    case WifiCmd::Join:
        join(c, now);
        break;
    case WifiCmd::Connect:
        connect(c.ssid, c.sec, now);
        break;
    case WifiCmd::Disconnect:
        disconnect(now);
        break;
    case WifiCmd::Forget:
        forget(c.ssid, c.sec, now);
        break;
    case WifiCmd::Radio:
        radio(c.on, now);
        break;
    }
    wifi_wipe(c.psk);
}

void Wifi::set_page_open(bool open, int64_t now)
{
    if (open == page_)
        return;
    page_ = open;
    for (const char* rule : kPageMatches) {
        if (open)
            bus_.add_match(rule);
        else
            bus_.remove_match(rule);
    }
    if (!open) {
        s_.scanning = false;
        scan_until_ = -1;
        changed();
        return;
    }
    s_.scanned = false;
    s_.scan_failed = false;
    s_.order.clear();
    changed();
    fetch_aps(now, false);
    fetch_saved(now);
    scan(now);
    txt_due_ = true;
}

void Wifi::run_import(int64_t now)
{
    txt_due_ = false;
    if (txt_.empty())
        return;
    int f = open(txt_.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (f < 0 && (errno == ENOENT || errno == ENOTDIR))
        return;
    struct stat st = {};
    std::string text;
    bool ok = f >= 0 && fstat(f, &st) == 0 && S_ISREG(st.st_mode);
    char buf[4096];
    while (ok && text.size() < kWifiTxtMax) {
        ssize_t n = read(f, buf, std::min(sizeof buf, kWifiTxtMax - text.size()));
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0)
            ok = false;
        if (n <= 0)
            break;
        text.append(buf, (size_t)n);
    }
    std::fill(buf, buf + sizeof buf, '\0');
    if (f >= 0)
        close(f);
    std::vector<WifiTxtNet> nets;
    int bad = ok ? wifi_txt(text, nets) : 1;
    if (!bad && (off_t)text.size() != st.st_size)
        bad = (int)std::count(text.begin(), text.end(), '\n') + 1;
    wifi_wipe(text);
    if (bad) {
        for (WifiTxtNet& n : nets)
            wifi_wipe(n.psk);
        std::fprintf(stderr, "blockie-frontend: wifi.txt line %d not read\n", bad);
        note("wifi.txt", "line " + std::to_string(bad) + " not read");
        return;
    }
    std::vector<WifiTxtNet> uniq = wifi_txt_unique(nets);
    for (WifiTxtNet& n : nets)
        wifi_wipe(n.psk);
    importing_ = true;
    import_failed_ = false;
    import_n_ = (int)uniq.size();
    import_left_ = 1;
    for (WifiTxtNet& n : uniq) {
        WifiSec sec = n.psk.empty() ? WifiSec::Open : WifiSec::Psk;
        const WifiSaved* sv = wifi_saved(s_, n.ssid, sec);
        if (!sv) {
            import_left_++;
            import_add(n, now);
        } else if (sec == WifiSec::Psk) {
            import_left_++;
            import_rekey(sv->path, n.psk, now);
        }
        wifi_wipe(n.psk);
    }
    import_step(true);
}

void Wifi::import_add(const WifiTxtNet& n, int64_t now)
{
    DBusMessage* m = method(kSettingsPath, kSettingsIface, "AddConnection");
    if (m) {
        DBusMessageIter it;
        dbus_message_iter_init_append(m, &it);
        append_wifi(&it, n.ssid, n.psk.empty() ? nullptr : &n.psk, wifi_shown(n.ssid), make_uuid());
    }
    uint32_t g = gen_;
    bus_.call(m, guard([this, g](DBusMessage* r) {
                  if (g != gen_)
                      return;
                  if (!ok_reply(r))
                      log_failed("AddConnection", r);
                  import_step(ok_reply(r));
              }),
              now, kCallMs);
}

void Wifi::import_rekey(const std::string& path, const std::string& psk, int64_t now)
{
    auto key = std::make_shared<std::string>(psk);
    uint32_t g = gen_;
    bus_.call(method(path, kConnIface, "GetSettings"), guard([this, g, path, key](DBusMessage* r) {
                  if (g != gen_) {
                      wifi_wipe(*key);
                      return;
                  }
                  if (!ok_reply(r) || !dbus_message_has_signature(r, "a{sa{sv}}")) {
                      log_failed("GetSettings", r);
                      wifi_wipe(*key);
                      import_step(false);
                      return;
                  }
                  DBusMessage* m = method(path, kConnIface, "Update");
                  if (m) {
                      DBusMessageIter from, to;
                      dbus_message_iter_init(r, &from);
                      dbus_message_iter_init_append(m, &to);
                      append_rekeyed(&from, &to, *key);
                  }
                  wifi_wipe(*key);
                  bus_.call(m, guard([this, g](DBusMessage* rr) {
                                if (g != gen_)
                                    return;
                                if (!ok_reply(rr))
                                    log_failed("Update", rr);
                                import_step(ok_reply(rr));
                            }),
                            clock_ms(), kCallMs);
              }),
              now, kCallMs);
}

void Wifi::import_step(bool ok)
{
    if (!ok)
        import_failed_ = true;
    if (--import_left_ == 0)
        import_done();
}

void Wifi::import_done()
{
    importing_ = false;
    if (import_failed_) {
        std::fprintf(stderr, "blockie-frontend: wifi.txt not saved\n");
        note("wifi.txt", "networks not saved");
    } else {
        if (unlink(txt_.c_str()) == 0)
            fsync_dir(txt_);
        else
            std::fprintf(stderr, "blockie-frontend: wifi.txt not removed\n");
        std::fprintf(stderr, "blockie-frontend: wifi.txt %d networks saved\n", import_n_);
        note("wi-fi", wifi_networks_saved(import_n_));
    }
    fetch_saved(clock_ms());
}

void Wifi::tick(int64_t now)
{
    if (!bus_.connected()) {
        fetch_due_ = false;
        if (s_.up || !owner_.empty() || !later_.empty() || saved_ok_) {
            drop();
            owner_.clear();
        }
        return;
    }
    if (fetch_due_) {
        fetch_due_ = false;
        fetch(now);
    }
    if (!later_.empty()) {
        std::vector<Later> due;
        due.swap(later_);
        for (Later& l : due)
            l(now);
    }
    if (s_.joining && now >= s_.join.until)
        join_end(false, false);
    if (s_.scanning && scan_until_ >= 0 && now >= scan_until_)
        scan_done(now);
    if (txt_due_ && saved_ok_ && !importing_)
        run_import(now);
}

int64_t Wifi::deadline() const
{
    if (bus_.connected() && (fetch_due_ || !later_.empty() || (txt_due_ && saved_ok_ && !importing_)))
        return 0;
    int64_t d = -1;
    auto at = [&d](int64_t t) {
        if (t >= 0 && (d < 0 || t < d))
            d = t;
    };
    if (s_.joining)
        at(s_.join.until);
    if (s_.scanning)
        at(scan_until_);
    return d;
}

}
