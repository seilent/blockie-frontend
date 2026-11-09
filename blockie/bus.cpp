#include "bus.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include <dbus/dbus.h>

namespace fd {

namespace {

DBusHandlerResult bus_filter(DBusConnection*, DBusMessage* m, void* data)
{
    return static_cast<Bus*>(data)->dispatch(m) ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

std::string system_address()
{
    const char* env = std::getenv("DBUS_SYSTEM_BUS_ADDRESS");
    if (env && *env)
        return env;
    return "unix:path=/run/dbus/system_bus_socket";
}

}

bool Bus::open(int64_t now)
{
    close();
    DBusError err;
    dbus_error_init(&err);
    std::string addr = system_address();
    conn_ = dbus_connection_open_private(addr.c_str(), &err);
    if (!conn_) {
        std::fprintf(stderr, "blockie-frontend: bus open %s failed: %s\n", addr.c_str(),
                     err.message ? err.message : "?");
        dbus_error_free(&err);
        retry_at_ = now + kReconnectMs;
        return false;
    }
    dbus_connection_set_exit_on_disconnect(conn_, FALSE);
    if (!dbus_bus_register(conn_, &err)) {
        std::fprintf(stderr, "blockie-frontend: bus register failed: %s\n", err.message ? err.message : "?");
        dbus_error_free(&err);
        close();
        retry_at_ = now + kReconnectMs;
        return false;
    }
    dbus_connection_add_filter(conn_, bus_filter, this, nullptr);
    for (const std::string& rule : matches_)
        dbus_bus_add_match(conn_, rule.c_str(), nullptr);
    retry_at_ = -1;
    for (Hook& h : hooks_)
        h(now);
    dbus_connection_flush(conn_);
    return true;
}

void Bus::close()
{
    if (conn_) {
        dbus_connection_remove_filter(conn_, bus_filter, this);
        dbus_connection_close(conn_);
        dbus_connection_unref(conn_);
        conn_ = nullptr;
    }
    drop_all();
}

void Bus::drop_all()
{
    std::map<uint32_t, Pending> gone;
    gone.swap(pending_);
    for (auto& p : gone) {
        dbus_pending_call_cancel(p.second.pc);
        dbus_pending_call_unref(p.second.pc);
    }
    for (auto& p : gone)
        if (p.second.reply)
            p.second.reply(nullptr);
}

bool Bus::connected() const { return conn_ && dbus_connection_get_is_connected(conn_); }

int Bus::fd() const
{
    int f = -1;
    if (!connected() || !dbus_connection_get_unix_fd(conn_, &f))
        return -1;
    return f;
}

bool Bus::wants_write() const { return connected() && dbus_connection_has_messages_to_send(conn_); }

bool Bus::data_remains() const
{
    return connected() && dbus_connection_get_dispatch_status(conn_) == DBUS_DISPATCH_DATA_REMAINS;
}

void Bus::pump()
{
    if (!conn_)
        return;
    dbus_connection_read_write(conn_, 0);
    while (conn_ && dbus_connection_dispatch(conn_) == DBUS_DISPATCH_DATA_REMAINS) {
    }
    if (conn_)
        dbus_connection_flush(conn_);
}

void Bus::tick(int64_t now)
{
    if (conn_ && !dbus_connection_get_is_connected(conn_)) {
        std::fprintf(stderr, "blockie-frontend: bus disconnected\n");
        close();
        retry_at_ = now + kReconnectMs;
    }
    if (!conn_ && retry_at_ >= 0 && now >= retry_at_)
        open(now);
    std::vector<uint32_t> late;
    for (auto& p : pending_)
        if (now >= p.second.due)
            late.push_back(p.first);
    for (uint32_t id : late) {
        auto it = pending_.find(id);
        if (it == pending_.end())
            continue;
        Pending p = std::move(it->second);
        pending_.erase(it);
        dbus_pending_call_cancel(p.pc);
        dbus_pending_call_unref(p.pc);
        if (p.reply)
            p.reply(nullptr);
    }
}

int64_t Bus::deadline() const
{
    int64_t d = conn_ ? -1 : retry_at_;
    for (auto& p : pending_)
        if (d < 0 || p.second.due < d)
            d = p.second.due;
    return d;
}

void Bus::add_match(const std::string& rule)
{
    matches_.push_back(rule);
    if (connected())
        dbus_bus_add_match(conn_, rule.c_str(), nullptr);
}

void Bus::remove_match(const std::string& rule)
{
    auto it = std::find(matches_.begin(), matches_.end(), rule);
    if (it == matches_.end())
        return;
    matches_.erase(it);
    if (connected())
        dbus_bus_remove_match(conn_, rule.c_str(), nullptr);
}

bool Bus::send(DBusMessage* m)
{
    if (!m)
        return false;
    bool ok = connected() && dbus_connection_send(conn_, m, nullptr);
    dbus_message_unref(m);
    return ok;
}

uint32_t Bus::call(DBusMessage* m, Reply r, int64_t now, int timeout_ms)
{
    uint32_t id = next_++;
    if (next_ == 0)
        next_ = 1;
    DBusPendingCall* pc = nullptr;
    if (!m || !connected() || !dbus_connection_send_with_reply(conn_, m, &pc, DBUS_TIMEOUT_INFINITE) || !pc) {
        if (m)
            dbus_message_unref(m);
        if (r)
            r(nullptr);
        return id;
    }
    dbus_message_unref(m);
    Pending p;
    p.pc = pc;
    p.due = now + timeout_ms;
    p.reply = std::move(r);
    pending_[id] = std::move(p);
    Notify* n = new Notify{this, id};
    dbus_pending_call_set_notify(pc, done, n, [](void* d) { delete static_cast<Notify*>(d); });
    return id;
}

void Bus::done(DBusPendingCall* pc, void* data)
{
    Notify* n = static_cast<Notify*>(data);
    DBusMessage* reply = dbus_pending_call_steal_reply(pc);
    n->self->finish(n->id, reply);
    if (reply)
        dbus_message_unref(reply);
}

void Bus::finish(uint32_t id, DBusMessage* reply)
{
    auto it = pending_.find(id);
    if (it == pending_.end())
        return;
    Pending p = std::move(it->second);
    pending_.erase(it);
    if (p.reply)
        p.reply(reply);
    dbus_pending_call_unref(p.pc);
}

bool Bus::dispatch(DBusMessage* m)
{
    for (Filter& f : filters_)
        if (f(m))
            return true;
    return false;
}

}
