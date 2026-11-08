#include "bt.h"

#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <utility>

#include <dbus/dbus.h>

namespace fd {

namespace {

const char* const kBluez = "org.bluez";
const char* const kAdapterIface = "org.bluez.Adapter1";
const char* const kDeviceIface = "org.bluez.Device1";
const char* const kAgentIface = "org.bluez.Agent1";
const char* const kAgentManager = "org.bluez.AgentManager1";
const char* const kOm = "org.freedesktop.DBus.ObjectManager";
const char* const kProps = "org.freedesktop.DBus.Properties";
const char* const kRejected = "org.bluez.Error.Rejected";
const char* const kServiceAllow[] = {"0000110a", "0000110c", "0000110d", "0000110e", "00001124", "00001812"};

const char* const kMatches[] = {
    "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',member='NameOwnerChanged',"
    "arg0='org.bluez'",
    "type='signal',sender='org.bluez',interface='org.freedesktop.DBus.ObjectManager'",
    "type='signal',sender='org.bluez',interface='org.freedesktop.DBus.Properties',member='PropertiesChanged'",
};

bool ok_reply(DBusMessage* r) { return r && dbus_message_get_type(r) == DBUS_MESSAGE_TYPE_METHOD_RETURN; }

std::string error_of(DBusMessage* r)
{
    if (!r)
        return "no reply";
    const char* e = dbus_message_get_error_name(r);
    return e ? e : "error";
}

std::string lower(std::string s)
{
    for (char& ch : s)
        ch = (char)std::tolower((unsigned char)ch);
    return s;
}

void read_value(DBusMessageIter* it, BtValue& out)
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
        out = BtValue::of_bool(b != FALSE);
        return;
    }
    if (t == DBUS_TYPE_STRING || t == DBUS_TYPE_OBJECT_PATH) {
        const char* s = nullptr;
        dbus_message_iter_get_basic(it, &s);
        out = BtValue::of_str(s ? s : "");
        return;
    }
    if (t == DBUS_TYPE_BYTE || t == DBUS_TYPE_INT16 || t == DBUS_TYPE_UINT16 || t == DBUS_TYPE_INT32 ||
        t == DBUS_TYPE_UINT32 || t == DBUS_TYPE_INT64 || t == DBUS_TYPE_UINT64) {
        DBusBasicValue b;
        memset(&b, 0, sizeof b);
        dbus_message_iter_get_basic(it, &b);
        int64_t v = 0;
        switch (t) {
        case DBUS_TYPE_BYTE: v = b.byt; break;
        case DBUS_TYPE_INT16: v = b.i16; break;
        case DBUS_TYPE_UINT16: v = b.u16; break;
        case DBUS_TYPE_INT32: v = b.i32; break;
        case DBUS_TYPE_UINT32: v = b.u32; break;
        case DBUS_TYPE_INT64: v = b.i64; break;
        default: v = (int64_t)b.u64; break;
        }
        out = BtValue::of_int(v);
        return;
    }
    if (t != DBUS_TYPE_ARRAY)
        return;
    int e = dbus_message_iter_get_element_type(it);
    if (e != DBUS_TYPE_STRING && e != DBUS_TYPE_OBJECT_PATH)
        return;
    DBusMessageIter a;
    dbus_message_iter_recurse(it, &a);
    std::vector<std::string> v;
    while (dbus_message_iter_get_arg_type(&a) == e) {
        const char* s = nullptr;
        dbus_message_iter_get_basic(&a, &s);
        v.push_back(s ? s : "");
        dbus_message_iter_next(&a);
    }
    out = BtValue::of_list(v);
}

void read_props(DBusMessageIter* arr, BtProps& out)
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
            BtValue v;
            read_value(&kv, v);
            if (k && v.kind != BtValue::None)
                out[k] = v;
        }
        dbus_message_iter_next(&e);
    }
}

using Ifaces = std::vector<std::pair<std::string, BtProps>>;

void read_ifaces(DBusMessageIter* arr, Ifaces& out)
{
    DBusMessageIter a;
    dbus_message_iter_recurse(arr, &a);
    while (dbus_message_iter_get_arg_type(&a) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter e;
        dbus_message_iter_recurse(&a, &e);
        const char* iface = nullptr;
        dbus_message_iter_get_basic(&e, &iface);
        dbus_message_iter_next(&e);
        BtProps p;
        read_props(&e, p);
        out.push_back({iface ? iface : "", std::move(p)});
        dbus_message_iter_next(&a);
    }
}

void log_failed(const char* what, DBusMessage* r)
{
    std::fprintf(stderr, "blockie-frontend: bt %s failed: %s\n", what, error_of(r).c_str());
}

}

Bt::~Bt()
{
    if (held_)
        dbus_message_unref(held_);
}

Bus::Reply Bt::guard(std::function<void(DBusMessage*)> f)
{
    std::weak_ptr<int> alive = life_;
    return [alive, f](DBusMessage* r) {
        if (!alive.expired())
            f(r);
    };
}

void Bt::start()
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

void Bt::drop()
{
    if (held_) {
        dbus_message_unref(held_);
        held_ = nullptr;
    }
    s_ = BtState{};
    later_.clear();
    saved_ = false;
    restore_left_ = 0;
    restore_epoch_++;
    gen_++;
    changed();
}

void Bt::fetch(int64_t now)
{
    uint32_t g = gen_;
    bus_.call(dbus_message_new_method_call(kBluez, "/", kOm, "GetManagedObjects"), guard([this, g](DBusMessage* r) {
                  if (g == gen_)
                      on_objects(r);
              }),
              now, kCallMs);
    const char* path = kAgentPath;
    const char* cap = "DisplayYesNo";
    DBusMessage* m = dbus_message_new_method_call(kBluez, "/org/bluez", kAgentManager, "RegisterAgent");
    if (m)
        dbus_message_append_args(m, DBUS_TYPE_OBJECT_PATH, &path, DBUS_TYPE_STRING, &cap, DBUS_TYPE_INVALID);
    bus_.call(m, guard([](DBusMessage* r) {
                  if (!ok_reply(r) && error_of(r) != "org.bluez.Error.AlreadyExists")
                      log_failed("RegisterAgent", r);
              }),
              now, kCallMs);
    m = dbus_message_new_method_call(kBluez, "/org/bluez", kAgentManager, "RequestDefaultAgent");
    if (m)
        dbus_message_append_args(m, DBUS_TYPE_OBJECT_PATH, &path, DBUS_TYPE_INVALID);
    bus_.call(m, guard([](DBusMessage* r) {
                  if (!ok_reply(r))
                      log_failed("RequestDefaultAgent", r);
              }),
              now, kCallMs);
}

void Bt::on_objects(DBusMessage* r)
{
    s_.adapter = BtAdapter{};
    s_.devices.clear();
    if (!ok_reply(r) || !dbus_message_has_signature(r, "a{oa{sa{sv}}}")) {
        log_failed("GetManagedObjects", r);
        owner_.clear();
        s_.up = false;
        changed();
        return;
    }
    const char* sender = dbus_message_get_sender(r);
    owner_ = sender ? sender : "";
    std::vector<std::pair<std::string, Ifaces>> objs;
    DBusMessageIter it, obj;
    dbus_message_iter_init(r, &it);
    dbus_message_iter_recurse(&it, &obj);
    while (dbus_message_iter_get_arg_type(&obj) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter e;
        dbus_message_iter_recurse(&obj, &e);
        const char* path = nullptr;
        dbus_message_iter_get_basic(&e, &path);
        dbus_message_iter_next(&e);
        Ifaces ifs;
        read_ifaces(&e, ifs);
        objs.push_back({path ? path : "", std::move(ifs)});
        dbus_message_iter_next(&obj);
    }
    for (bool adapters : {true, false})
        for (const auto& o : objs)
            for (const auto& i : o.second)
                if ((i.first == kAdapterIface) == adapters)
                    add_iface(o.first, i.first, i.second);
    s_.up = true;
    changed();
}

bool Bt::on_message(DBusMessage* m)
{
    int t = dbus_message_get_type(m);
    if (t == DBUS_MESSAGE_TYPE_SIGNAL) {
        on_signal(m);
        return false;
    }
    if (t == DBUS_MESSAGE_TYPE_METHOD_CALL && dbus_message_has_path(m, kAgentPath)) {
        on_agent(m);
        return true;
    }
    return false;
}

void Bt::on_signal(DBusMessage* m)
{
    if (dbus_message_is_signal(m, DBUS_INTERFACE_DBUS, "NameOwnerChanged")) {
        const char* from = dbus_message_get_sender(m);
        const char *n = nullptr, *was = nullptr, *now_owner = nullptr;
        if (!from || strcmp(from, DBUS_SERVICE_DBUS) != 0 ||
            !dbus_message_get_args(m, nullptr, DBUS_TYPE_STRING, &n, DBUS_TYPE_STRING, &was, DBUS_TYPE_STRING,
                                   &now_owner, DBUS_TYPE_INVALID) ||
            strcmp(n, kBluez) != 0)
            return;
        std::fprintf(stderr, "blockie-frontend: bt org.bluez owner %s\n", *now_owner ? now_owner : "gone");
        drop();
        owner_ = now_owner;
        fetch_due_ = !owner_.empty();
        return;
    }
    const char* sender = dbus_message_get_sender(m);
    const char* path = dbus_message_get_path(m);
    DBusMessageIter it;
    if (owner_.empty() || !sender || owner_ != sender || !path || !dbus_message_iter_init(m, &it))
        return;
    if (dbus_message_is_signal(m, kProps, "PropertiesChanged")) {
        if (!dbus_message_has_signature(m, "sa{sv}as"))
            return;
        const char* iface = nullptr;
        dbus_message_iter_get_basic(&it, &iface);
        dbus_message_iter_next(&it);
        BtProps p;
        read_props(&it, p);
        apply(path, iface ? iface : "", p);
        return;
    }
    if (dbus_message_is_signal(m, kOm, "InterfacesAdded")) {
        if (!dbus_message_has_signature(m, "oa{sa{sv}}"))
            return;
        const char* obj = nullptr;
        dbus_message_iter_get_basic(&it, &obj);
        dbus_message_iter_next(&it);
        Ifaces ifs;
        read_ifaces(&it, ifs);
        for (bool adapters : {true, false})
            for (const auto& i : ifs)
                if ((i.first == kAdapterIface) == adapters)
                    add_iface(obj ? obj : "", i.first, i.second);
        changed();
        return;
    }
    if (dbus_message_is_signal(m, kOm, "InterfacesRemoved")) {
        if (!dbus_message_has_signature(m, "oas"))
            return;
        const char* obj = nullptr;
        dbus_message_iter_get_basic(&it, &obj);
        dbus_message_iter_next(&it);
        DBusMessageIter a;
        dbus_message_iter_recurse(&it, &a);
        while (dbus_message_iter_get_arg_type(&a) == DBUS_TYPE_STRING) {
            const char* iface = nullptr;
            dbus_message_iter_get_basic(&a, &iface);
            remove_iface(obj ? obj : "", iface ? iface : "");
            dbus_message_iter_next(&a);
        }
        changed();
    }
}

void Bt::add_iface(const std::string& path, const std::string& iface, const BtProps& p)
{
    if (iface == kAdapterIface) {
        if (s_.adapter.path.empty())
            s_.adapter.path = path;
        if (path == s_.adapter.path)
            bt_apply_adapter(s_.adapter, p);
        return;
    }
    if (iface != kDeviceIface)
        return;
    BtDevice d;
    if (BtDevice* was = device(path))
        d = *was;
    d.path = path;
    bt_apply_device(d, p);
    if (!s_.adapter.path.empty() && !d.adapter.empty() && d.adapter != s_.adapter.path)
        return;
    if (BtDevice* was = device(path))
        *was = d;
    else
        s_.devices.push_back(d);
}

void Bt::remove_iface(const std::string& path, const std::string& iface)
{
    if (iface == kAdapterIface && path == s_.adapter.path) {
        s_.adapter = BtAdapter{};
        s_.devices.clear();
        s_.searching = false;
        s_.pair_target.clear();
        s_.connect_target.clear();
        saved_ = false;
        clear_prompt();
        return;
    }
    if (iface != kDeviceIface)
        return;
    for (size_t i = 0; i < s_.devices.size(); i++)
        if (s_.devices[i].path == path) {
            s_.devices.erase(s_.devices.begin() + (std::ptrdiff_t)i);
            break;
        }
    for (std::string* p : {&s_.pair_target, &s_.pair_failed, &s_.connect_target, &s_.connect_failed})
        if (*p == path)
            p->clear();
    if (s_.prompt.path == path)
        clear_prompt();
}

void Bt::apply(const std::string& path, const std::string& iface, const BtProps& p)
{
    if (iface == kAdapterIface && path == s_.adapter.path) {
        BtAdapter was = s_.adapter;
        bt_apply_adapter(s_.adapter, p);
        if (s_.searching && !s_.adapter.powered)
            later_.push_back([this](int64_t t) { stop_search(t); });
        if (was != s_.adapter)
            changed();
        return;
    }
    if (iface != kDeviceIface)
        return;
    BtDevice* d = device(path);
    if (!d)
        return;
    BtDevice was = *d;
    bt_apply_device(*d, p);
    if (was != *d)
        changed();
}

BtDevice* Bt::device(const std::string& path)
{
    for (BtDevice& d : s_.devices)
        if (d.path == path)
            return &d;
    return nullptr;
}

void Bt::on_agent(DBusMessage* m)
{
    const char* member = dbus_message_get_member(m);
    const char* sender = dbus_message_get_sender(m);
    if (!member || !dbus_message_has_interface(m, kAgentIface)) {
        if (!dbus_message_get_no_reply(m))
            bus_.send(dbus_message_new_error(m, DBUS_ERROR_UNKNOWN_METHOD, "unknown method"));
        return;
    }
    if (owner_.empty() || !sender || owner_ != sender) {
        reply(m, false);
        return;
    }
    std::string dev, arg;
    uint32_t passkey = 0;
    DBusMessageIter it;
    if (dbus_message_iter_init(m, &it) && dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_OBJECT_PATH) {
        const char* d = nullptr;
        dbus_message_iter_get_basic(&it, &d);
        dev = d ? d : "";
        dbus_message_iter_next(&it);
        if (dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_UINT32) {
            dbus_uint32_t k = 0;
            dbus_message_iter_get_basic(&it, &k);
            passkey = k;
        } else if (dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_STRING) {
            const char* s = nullptr;
            dbus_message_iter_get_basic(&it, &s);
            arg = s ? s : "";
        }
    }
    std::string name = member;
    bool target = page_open_ && !game_ && !dev.empty() && dev == s_.pair_target;
    if (name == "Release" || name == "Cancel") {
        reply(m, true);
        clear_prompt();
        return;
    }
    if (name == "RequestConfirmation") {
        if (!target || held_ || !dbus_message_has_signature(m, "ou")) {
            reply(m, false);
            return;
        }
        dbus_message_ref(m);
        held_ = m;
        s_.prompt = BtPrompt{BtPrompt::Confirm, dev, "", passkey};
        changed();
        return;
    }
    if (name == "RequestPinCode") {
        reply(m, target, "0000");
        return;
    }
    if (name == "DisplayPasskey" || name == "DisplayPinCode") {
        if (target && !held_) {
            s_.prompt = BtPrompt{name == "DisplayPasskey" ? BtPrompt::Passkey : BtPrompt::Pin, dev, arg, passkey};
            changed();
        }
        reply(m, target);
        return;
    }
    if (name == "RequestAuthorization") {
        reply(m, target);
        return;
    }
    if (name == "AuthorizeService") {
        const BtDevice* d = bt_device(s_, dev);
        bool ok = false;
        for (const char* a : kServiceAllow)
            ok = ok || (d && d->paired && lower(arg).compare(0, 8, a) == 0);
        if (!ok)
            std::fprintf(stderr, "blockie-frontend: bt service %.8s from %s refused\n", arg.c_str(), dev.c_str());
        reply(m, ok);
        return;
    }
    if (name == "RequestPasskey") {
        reply(m, false);
        return;
    }
    if (!dbus_message_get_no_reply(m))
        bus_.send(dbus_message_new_error(m, DBUS_ERROR_UNKNOWN_METHOD, "unknown method"));
}

void Bt::reply(DBusMessage* m, bool ok, const char* pin)
{
    if (dbus_message_get_no_reply(m))
        return;
    DBusMessage* r = nullptr;
    if (!ok) {
        r = dbus_message_new_error(m, kRejected, "rejected");
    } else {
        r = dbus_message_new_method_return(m);
        if (r && pin && dbus_message_is_method_call(m, kAgentIface, "RequestPinCode"))
            dbus_message_append_args(r, DBUS_TYPE_STRING, &pin, DBUS_TYPE_INVALID);
    }
    bus_.send(r);
}

uint32_t Bt::call(const std::string& path, const char* iface, const char* member, Bus::Reply r, int64_t now, int ms)
{
    return bus_.call(dbus_message_new_method_call(kBluez, path.c_str(), iface, member), std::move(r), now, ms);
}

void Bt::set_prop(const std::string& path, const char* iface, const char* name, bool v, Bus::Reply r, int64_t now)
{
    DBusMessage* m = dbus_message_new_method_call(kBluez, path.c_str(), kProps, "Set");
    if (m) {
        DBusMessageIter it, var;
        dbus_bool_t b = v ? TRUE : FALSE;
        dbus_message_iter_init_append(m, &it);
        dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &iface);
        dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &name);
        dbus_message_iter_open_container(&it, DBUS_TYPE_VARIANT, DBUS_TYPE_BOOLEAN_AS_STRING, &var);
        dbus_message_iter_append_basic(&var, DBUS_TYPE_BOOLEAN, &b);
        dbus_message_iter_close_container(&it, &var);
    }
    bus_.call(m, std::move(r), now, kCallMs);
}

void Bt::set_prop(const std::string& path, const char* iface, const char* name, uint32_t v, Bus::Reply r,
                  int64_t now)
{
    DBusMessage* m = dbus_message_new_method_call(kBluez, path.c_str(), kProps, "Set");
    if (m) {
        DBusMessageIter it, var;
        dbus_uint32_t u = v;
        dbus_message_iter_init_append(m, &it);
        dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &iface);
        dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &name);
        dbus_message_iter_open_container(&it, DBUS_TYPE_VARIANT, DBUS_TYPE_UINT32_AS_STRING, &var);
        dbus_message_iter_append_basic(&var, DBUS_TYPE_UINT32, &u);
        dbus_message_iter_close_container(&it, &var);
    }
    bus_.call(m, std::move(r), now, kCallMs);
}

Bus::Reply Bt::logged(const char* what)
{
    return guard([what](DBusMessage* r) {
        if (!ok_reply(r))
            log_failed(what, r);
    });
}

void Bt::start_search(int64_t now)
{
    if (s_.searching || !bt_available(s_) || !s_.adapter.powered)
        return;
    if (!saved_) {
        saved_ = true;
        saved_pairable_ = s_.adapter.pairable;
        saved_timeout_ = s_.adapter.pairable_timeout;
    }
    restore_epoch_++;
    restore_left_ = 0;
    std::string a = s_.adapter.path;
    set_prop(a, kAdapterIface, "PairableTimeout", (uint32_t)kBtSearchS, logged("PairableTimeout"), now);
    set_prop(a, kAdapterIface, "Pairable", true, logged("Pairable"), now);
    uint32_t g = gen_;
    call(a, kAdapterIface, "StartDiscovery", guard([this, g](DBusMessage* r) {
             if (g != gen_ || ok_reply(r) || error_of(r) == "org.bluez.Error.InProgress")
                 return;
             log_failed("StartDiscovery", r);
             later_.push_back([this](int64_t t) { stop_search(t); });
         }),
         now);
    s_.searching = true;
    s_.search_end = now + (int64_t)kBtSearchS * 1000;
    changed();
}

void Bt::stop_search(int64_t now)
{
    if (!s_.searching)
        return;
    s_.searching = false;
    changed();
    restore(now);
    if (!s_.adapter.path.empty())
        call(s_.adapter.path, kAdapterIface, "StopDiscovery", logged("StopDiscovery"), now);
}

void Bt::restore(int64_t now)
{
    if (!saved_)
        return;
    if (s_.adapter.path.empty()) {
        saved_ = false;
        return;
    }
    uint32_t epoch = ++restore_epoch_;
    restore_left_ = 2;
    Bus::Reply done = guard([this, epoch](DBusMessage* r) {
        if (!ok_reply(r))
            log_failed("restore", r);
        if (epoch == restore_epoch_ && --restore_left_ == 0)
            saved_ = false;
    });
    std::string a = s_.adapter.path;
    set_prop(a, kAdapterIface, "Pairable", saved_pairable_, done, now);
    set_prop(a, kAdapterIface, "PairableTimeout", saved_timeout_, done, now);
}

void Bt::pair(const std::string& path, int64_t now)
{
    const BtDevice* d = bt_device(s_, path);
    if (!d || d->paired || !s_.pair_target.empty() || !bt_available(s_))
        return;
    s_.pair_target = path;
    if (s_.pair_failed == path)
        s_.pair_failed.clear();
    changed();
    uint32_t g = gen_;
    call(path, kDeviceIface, "Pair", guard([this, g, path](DBusMessage* r) {
             if (g != gen_)
                 return;
             bool ok = ok_reply(r) || error_of(r) == "org.bluez.Error.AlreadyExists";
             if (s_.pair_target == path)
                 s_.pair_target.clear();
             if (s_.prompt.path == path)
                 clear_prompt();
             if (!ok) {
                 s_.pair_failed = path;
                 log_failed("Pair", r);
             }
             changed();
             if (ok)
                 later_.push_back([this, path](int64_t t) { trust_then_connect(path, t); });
         }),
         now, kPairMs);
}

void Bt::trust_then_connect(const std::string& path, int64_t now)
{
    if (!bt_device(s_, path))
        return;
    s_.connect_target = path;
    changed();
    uint32_t g = gen_;
    set_prop(path, kDeviceIface, "Trusted", true, guard([this, g, path](DBusMessage* r) {
                 if (g != gen_)
                     return;
                 if (!ok_reply(r))
                     log_failed("Trusted", r);
                 later_.push_back([this, path](int64_t t) { connect(path, t); });
             }),
             now);
}

void Bt::connect(const std::string& path, int64_t now)
{
    const BtDevice* d = bt_device(s_, path);
    if (!d || !d->paired) {
        if (s_.connect_target == path) {
            s_.connect_target.clear();
            changed();
        }
        return;
    }
    s_.connect_target = path;
    if (s_.connect_failed == path)
        s_.connect_failed.clear();
    changed();
    uint32_t g = gen_;
    call(path, kDeviceIface, "Connect", guard([this, g, path](DBusMessage* r) {
             if (g != gen_)
                 return;
             if (s_.connect_target == path)
                 s_.connect_target.clear();
             if (!ok_reply(r)) {
                 s_.connect_failed = path;
                 log_failed("Connect", r);
             }
             changed();
         }),
         now);
}

void Bt::disconnect(const std::string& path, Bus::Reply r, int64_t now)
{
    bool was = s_.connect_target == path || s_.connect_failed == path;
    if (s_.connect_target == path)
        s_.connect_target.clear();
    if (s_.connect_failed == path)
        s_.connect_failed.clear();
    if (was)
        changed();
    call(path, kDeviceIface, "Disconnect", r ? std::move(r) : logged("Disconnect"), now);
}

void Bt::forget(const std::string& path, int64_t now)
{
    const BtDevice* d = bt_device(s_, path);
    if (!d)
        return;
    if (!d->connected) {
        remove(path, now);
        return;
    }
    uint32_t g = gen_;
    disconnect(path, guard([this, g, path](DBusMessage*) {
                   if (g == gen_)
                       later_.push_back([this, path](int64_t t) { remove(path, t); });
               }),
               now);
}

void Bt::remove(const std::string& path, int64_t now)
{
    if (s_.adapter.path.empty() || !bt_device(s_, path))
        return;
    DBusMessage* m = dbus_message_new_method_call(kBluez, s_.adapter.path.c_str(), kAdapterIface, "RemoveDevice");
    const char* p = path.c_str();
    if (m)
        dbus_message_append_args(m, DBUS_TYPE_OBJECT_PATH, &p, DBUS_TYPE_INVALID);
    bus_.call(m, logged("RemoveDevice"), now, kCallMs);
}

void Bt::cancel_pairing(int64_t now)
{
    if (!s_.pair_target.empty())
        call(s_.pair_target, kDeviceIface, "CancelPairing", logged("CancelPairing"), now);
}

void Bt::answer(bool yes)
{
    if (!held_)
        return;
    reply(held_, yes);
    dbus_message_unref(held_);
    held_ = nullptr;
}

void Bt::clear_prompt()
{
    answer(false);
    if (s_.prompt.kind != BtPrompt::None) {
        s_.prompt = BtPrompt{};
        changed();
    }
}

void Bt::run(const BtCmd& c, int64_t now)
{
    switch (c.type) {
    case BtCmd::Search:
        start_search(now);
        break;
    case BtCmd::StopSearch:
        stop_search(now);
        break;
    case BtCmd::Pair:
        pair(c.path, now);
        break;
    case BtCmd::Connect:
        connect(c.path, now);
        break;
    case BtCmd::Disconnect:
        if (bt_device(s_, c.path))
            disconnect(c.path, Bus::Reply(), now);
        break;
    case BtCmd::Forget:
        forget(c.path, now);
        break;
    case BtCmd::PowerOn:
        if (bt_available(s_) && !s_.adapter.powered)
            set_prop(s_.adapter.path, kAdapterIface, "Powered", true, logged("Powered"), now);
        break;
    case BtCmd::Answer:
        if (s_.prompt.kind == BtPrompt::Confirm) {
            answer(c.yes);
            s_.prompt = BtPrompt{};
            changed();
        }
        break;
    case BtCmd::Dismiss:
        clear_prompt();
        break;
    }
}

void Bt::set_page_open(bool open, int64_t now)
{
    if (open == page_open_)
        return;
    page_open_ = open;
    if (open)
        return;
    stop_search(now);
    cancel_pairing(now);
    clear_prompt();
}

void Bt::set_game_running(bool running, int64_t now)
{
    if (running == game_)
        return;
    game_ = running;
    if (!running)
        return;
    stop_search(now);
    cancel_pairing(now);
    clear_prompt();
}

void Bt::shutdown(int64_t now)
{
    stop_search(now);
    cancel_pairing(now);
    clear_prompt();
}

void Bt::tick(int64_t now)
{
    if (!bus_.connected()) {
        fetch_due_ = false;
        if (s_.up || held_ || !owner_.empty() || !later_.empty()) {
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
    if (s_.searching && now >= s_.search_end)
        stop_search(now);
}

int64_t Bt::deadline() const
{
    if (bus_.connected() && (fetch_due_ || !later_.empty()))
        return 0;
    return s_.searching ? s_.search_end : -1;
}

}
