#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "bus.h"

struct DBusMessage;

namespace fd {

int backlight_raw(int pct, int max);

class Backlight {
public:
    static constexpr const char* kLed = "/sys/class/leds/lcd-backlight";
    static constexpr int kCallMs = 1000;

    explicit Backlight(Bus& bus, std::string led = kLed) : bus_(bus), led_(std::move(led)) {}
    Backlight(const Backlight&) = delete;
    Backlight& operator=(const Backlight&) = delete;

    std::function<void()> on_blank;
    std::function<void(const std::string& word)> on_fail;

    void open();
    void preview(int pct, int64_t now);
    void restore(int64_t now);
    void drop() { queued_ = -1; }
    bool busy() const { return busy_; }
    const std::string& session() const { return session_; }

private:
    Bus::Reply guard(std::function<void(DBusMessage*)> f);
    int node(const char* name) const;
    void want(int raw, int64_t now);
    void send(int raw, int64_t now);
    void listed(DBusMessage* r, int raw);
    void active(std::vector<std::string> paths, size_t i, int raw);
    void call(int raw, int64_t now);
    void called(DBusMessage* r);
    void fail(const std::string& word);

    Bus& bus_;
    std::string led_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    std::string session_;
    int opened_ = -1;
    bool max_told_ = false;
    int queued_ = -1;
    bool busy_ = false;
};

}
