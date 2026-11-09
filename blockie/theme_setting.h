#pragma once
#include <cstdint>
#include <functional>
#include <string>

#include "bus.h"
#include "setd_key.h"

namespace fd {

class ThemeSetting {
public:
    static constexpr const char* kKey = "frontend.theme";

    std::function<void(bool light)> on_value;
    std::function<void(const std::string& error, const std::string& text)> on_fail;

    ThemeSetting();
    ThemeSetting(const ThemeSetting&) = delete;
    ThemeSetting& operator=(const ThemeSetting&) = delete;

    void attach(Bus& bus) { key_.attach(bus); }
    void fetch(int64_t now) { key_.fetch(now); }
    void set(bool light, int64_t now) { key_.set(light ? "light" : "dark", now); }
    bool answered() const { return key_.answered(); }

private:
    SetdKey key_{kKey};
};

}
