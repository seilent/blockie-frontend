#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "bus.h"
#include "wifi_model.h"

struct DBusMessage;

namespace fd {

class Wifi {
public:
    static constexpr const char* kIface = "wlan0";
    static constexpr const char* kTxt = "/var/lib/blockie/data/import/wifi.txt";
    static constexpr int kCallMs = 25000;
    static constexpr int kScanMs = 30000;
    static constexpr int kJoinMs = 90000;

    explicit Wifi(Bus& bus, std::string txt = kTxt) : bus_(bus), txt_(std::move(txt)) {}
    Wifi(const Wifi&) = delete;
    Wifi& operator=(const Wifi&) = delete;

    void start();
    const WifiState& state() const { return s_; }
    uint32_t serial() const { return serial_; }
    void run(WifiCmd& c, int64_t now);
    void set_page_open(bool open, int64_t now);
    void import_txt() { txt_due_ = true; }
    void tick(int64_t now);
    int64_t deadline() const;

private:
    using Later = std::function<void(int64_t)>;

    Bus::Reply guard(std::function<void(DBusMessage*)> f);
    void changed() { serial_++; }
    void drop();
    bool usable() const { return s_.up && s_.radio && !s_.device.empty(); }
    void owned(DBusMessage* r);
    void fetch(int64_t now);
    void find_device(int64_t now);
    void fetch_state(int64_t now);
    void fetch_permissions(int64_t now);
    void fetch_active(int64_t now);
    void fetch_saved(int64_t now);
    void fetch_aps(int64_t now, bool order);
    void fetch_ap(const std::string& path, int64_t now);
    bool on_message(DBusMessage* m);
    void on_state(uint32_t st, uint32_t reason, int64_t now);
    void scan(int64_t now);
    void scan_done(int64_t now);
    void start_join(const std::string& ssid, WifiSec sec, bool fresh, int64_t now);
    void join(WifiCmd& c, int64_t now);
    void connect(const std::string& ssid, WifiSec sec, int64_t now);
    void check_active(int64_t now);
    void persist(int64_t now);
    void join_end(bool ok, bool wrong);
    void disconnect(int64_t now);
    void forget(const std::string& ssid, WifiSec sec, int64_t now);
    void radio(bool on, int64_t now);
    void note(const std::string& title, const std::string& text);
    void run_import(int64_t now);
    void import_add(const WifiTxtNet& n, int64_t now);
    void import_rekey(const std::string& path, const std::string& psk, int64_t now);
    void import_step(bool ok);
    void import_done();

    Bus& bus_;
    std::string txt_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    WifiState s_;
    uint32_t serial_ = 1;
    std::string owner_;
    uint32_t gen_ = 1, saved_gen_ = 0, ap_gen_ = 0, perm_gen_ = 0, join_seq_ = 0;
    bool started_ = false, fetch_due_ = false, page_ = false, saved_ok_ = false;
    bool txt_due_ = false, importing_ = false, import_failed_ = false;
    int import_left_ = 0, import_n_ = 0;
    int64_t scan_until_ = -1;
    std::vector<Later> later_;
};

}
