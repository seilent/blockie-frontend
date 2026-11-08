#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

struct DBusConnection;
struct DBusMessage;
struct DBusPendingCall;

namespace fd {

class Bus {
public:
    using Filter = std::function<bool(DBusMessage*)>;
    using Reply = std::function<void(DBusMessage*)>;
    using Hook = std::function<void(int64_t now)>;

    static constexpr int kReconnectMs = 5000;

    Bus() = default;
    ~Bus() { close(); }
    Bus(const Bus&) = delete;
    Bus& operator=(const Bus&) = delete;

    bool open(int64_t now);
    void close();
    bool connected() const;
    DBusConnection* conn() const { return conn_; }
    int fd() const;
    bool wants_write() const;
    bool data_remains() const;
    void pump();
    void tick(int64_t now);
    int64_t deadline() const;

    void add_filter(Filter f) { filters_.push_back(std::move(f)); }
    void add_match(const std::string& rule);
    void on_connect(Hook h) { hooks_.push_back(std::move(h)); }
    uint32_t call(DBusMessage* m, Reply r, int64_t now, int timeout_ms);
    bool send(DBusMessage* m);
    bool dispatch(DBusMessage* m);

private:
    struct Pending {
        DBusPendingCall* pc = nullptr;
        int64_t due = 0;
        Reply reply;
    };
    struct Notify {
        Bus* self = nullptr;
        uint32_t id = 0;
    };
    static void done(DBusPendingCall* pc, void* data);
    void finish(uint32_t id, DBusMessage* reply);
    void drop_all();

    DBusConnection* conn_ = nullptr;
    std::vector<Filter> filters_;
    std::vector<std::string> matches_;
    std::vector<Hook> hooks_;
    std::map<uint32_t, Pending> pending_;
    uint32_t next_ = 1;
    int64_t retry_at_ = -1;
};

}
