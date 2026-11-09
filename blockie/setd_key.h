#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "bus.h"

struct DBusMessage;

namespace fd {

enum class SetdType : char { S = 's', U = 'u', B = 'b' };

class SetdKey {
public:
    static constexpr const char* kDest = "net.seilent.Blockie.Settings";
    static constexpr const char* kPath = "/net/seilent/Blockie/Settings";
    static constexpr const char* kIface = "net.seilent.Blockie.Settings1";
    static constexpr const char* kErr = "net.seilent.Blockie.Settings.Error.";
    static constexpr int kTimeoutMs = 5000;

    explicit SetdKey(std::string key, SetdType type = SetdType::S) : key_(std::move(key)), type_(type) {}
    SetdKey(const SetdKey&) = delete;
    SetdKey& operator=(const SetdKey&) = delete;

    std::function<void(const std::string& value)> on_value;
    std::function<void(const std::string& error, const std::string& text)> on_fail;
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

    static bool parse_value(DBusMessage* m, SetdType type, std::string& out);
    static bool parse_changed(DBusMessage* m, std::string& key);

private:
    Bus::Reply guard(std::function<void(DBusMessage*)> f);
    bool on_signal(DBusMessage* m);
    void got(DBusMessage* r);
    void send(const std::string& value, int64_t now);
    void sent(const std::string& value, DBusMessage* r);

    std::string key_;
    SetdType type_ = SetdType::S;
    Bus* bus_ = nullptr;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    std::string value_;
    bool has_ = false;
    bool missing_ = false;
    bool answered_ = false;
    bool getting_ = false;
    bool again_ = false;
    bool back_if_get_fails_ = false;
    std::string fail_error_, fail_text_;
    bool setting_ = false;
    bool queued_ = false;
    std::string next_;
};

}
