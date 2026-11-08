#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "bt_model.h"
#include "bus.h"

struct DBusMessage;

namespace fd {

class Bt {
public:
    static constexpr const char* kAgentPath = "/net/seilent/Blockie/agent";
    static constexpr int kCallMs = 25000;
    static constexpr int kPairMs = 90000;

    explicit Bt(Bus& bus) : bus_(bus) {}
    ~Bt();
    Bt(const Bt&) = delete;
    Bt& operator=(const Bt&) = delete;

    void start();
    const BtState& state() const { return s_; }
    uint32_t serial() const { return serial_; }
    void run(const BtCmd& c, int64_t now);
    void set_page_open(bool open, int64_t now);
    void set_game_running(bool running, int64_t now);
    void shutdown(int64_t now);
    void tick(int64_t now);
    int64_t deadline() const;

private:
    using Later = std::function<void(int64_t)>;

    Bus::Reply guard(std::function<void(DBusMessage*)> f);
    Bus::Reply logged(const char* what);
    void changed() { serial_++; }
    void drop();
    void fetch(int64_t now);
    void on_objects(DBusMessage* r);
    bool on_message(DBusMessage* m);
    void on_signal(DBusMessage* m);
    void on_agent(DBusMessage* m);
    void reply(DBusMessage* m, bool ok, const char* pin = nullptr);
    void add_iface(const std::string& path, const std::string& iface, const BtProps& p);
    void remove_iface(const std::string& path, const std::string& iface);
    void apply(const std::string& path, const std::string& iface, const BtProps& p);
    BtDevice* device(const std::string& path);
    uint32_t call(const std::string& path, const char* iface, const char* member, Bus::Reply r, int64_t now,
                  int ms = kCallMs);
    void set_prop(const std::string& path, const char* iface, const char* name, bool v, Bus::Reply r, int64_t now);
    void set_prop(const std::string& path, const char* iface, const char* name, uint32_t v, Bus::Reply r,
                  int64_t now);
    void start_search(int64_t now);
    void stop_search(int64_t now);
    void restore(int64_t now);
    void pair(const std::string& path, int64_t now);
    void trust_then_connect(const std::string& path, int64_t now);
    void connect(const std::string& path, int64_t now);
    void disconnect(const std::string& path, Bus::Reply r, int64_t now);
    void forget(const std::string& path, int64_t now);
    void remove(const std::string& path, int64_t now);
    void cancel_pairing(int64_t now);
    void answer(bool yes);
    void clear_prompt();

    Bus& bus_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    BtState s_;
    uint32_t serial_ = 1;
    std::string owner_;
    uint32_t gen_ = 1;
    bool started_ = false, fetch_due_ = false, page_open_ = false, game_ = false;
    std::vector<Later> later_;
    DBusMessage* held_ = nullptr;
    bool saved_ = false, saved_pairable_ = false;
    uint32_t saved_timeout_ = 0;
    uint32_t restore_epoch_ = 0;
    int restore_left_ = 0;
};

}
