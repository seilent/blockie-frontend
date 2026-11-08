#pragma once
#include <cstdint>
#include <functional>
#include <string>

#include "bus.h"

struct DBusMessage;

namespace fd {

class ThemeSetting {
public:
    static constexpr const char* kKey = "frontend.theme";
    static constexpr int kTimeoutMs = 5000;

    std::function<void(bool light)> on_value;
    std::function<void()> on_fail;

    void attach(Bus& bus);
    void fetch(int64_t now);
    void set(bool light, int64_t now);
    bool answered() const { return answered_; }

    static bool parse_value(DBusMessage* m, std::string& out);
    static bool parse_changed(DBusMessage* m, std::string& key, std::string& value);

private:
    bool on_signal(DBusMessage* m);
    void apply(const std::string& v);

    Bus* bus_ = nullptr;
    bool answered_ = false;
    int64_t now_ = 0;
};

}
