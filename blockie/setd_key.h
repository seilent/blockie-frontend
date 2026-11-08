#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "bus.h"

struct DBusMessage;

namespace fd {

class SetdKey {
public:
    static constexpr const char* kDest = "net.seilent.Blockie.Settings";
    static constexpr const char* kPath = "/net/seilent/Blockie/Settings";
    static constexpr const char* kIface = "net.seilent.Blockie.Settings1";
    static constexpr const char* kErr = "net.seilent.Blockie.Settings.Error.";
    static constexpr int kTimeoutMs = 5000;

    explicit SetdKey(std::string key) : key_(std::move(key)) {}
    SetdKey(const SetdKey&) = delete;
    SetdKey& operator=(const SetdKey&) = delete;

    std::function<void(const std::string& value)> on_value;
    std::function<void()> on_fail;
    std::function<void()> on_missing;

    void attach(Bus& bus);
    void fetch(int64_t now);
    void set(const std::string& value, int64_t now);
    const std::string& key() const { return key_; }
    const std::string& value() const { return value_; }
    bool has_value() const { return has_; }
    bool missing() const { return missing_; }
    bool answered() const { return answered_; }
    bool setting() const { return setting_; }

    static bool parse_value(DBusMessage* m, std::string& out);
    static bool parse_changed(DBusMessage* m, std::string& key, std::string& value);

private:
    Bus::Reply guard(std::function<void(DBusMessage*)> f);
    bool on_signal(DBusMessage* m);
    void got(DBusMessage* r);
    void send(const std::string& value, int64_t now);
    void sent(const std::string& value, DBusMessage* r);

    std::string key_;
    Bus* bus_ = nullptr;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    std::string value_;
    bool has_ = false;
    bool missing_ = false;
    bool answered_ = false;
    bool getting_ = false;
    bool again_ = false;
    bool back_if_get_fails_ = false;
    bool setting_ = false;
    bool queued_ = false;
    std::string next_;
};

}
