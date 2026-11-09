#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace fd {

struct InfoLine {
    std::string title, value;
};

struct Battery {
    bool ok = false;
    int capacity = 0;
    std::string status;
};

struct Space {
    bool ok = false;
    uint64_t avail = 0, total = 0;
};

struct Storage {
    bool mounted = false;
    Space data, system;
};

constexpr const char* kBatteryDir = "/sys/class/power_supply/battery";
constexpr const char* kDataDir = "/var/lib/blockie/data";
constexpr const char* kVarDir = "/var";
constexpr const char* kSystemDir = "/sysroot";
constexpr const char* kOsRelease = "/usr/lib/os-release";
constexpr int kBatteryPollMs = 10000;

Battery read_battery(const std::string& dir = kBatteryDir);
std::string battery_words(const Battery& b);

class BatteryFeed {
public:
    explicit BatteryFeed(std::string dir = kBatteryDir) : dir_(std::move(dir)) {}
    BatteryFeed(const BatteryFeed&) = delete;
    BatteryFeed& operator=(const BatteryFeed&) = delete;

    void restart() { asked_at_ = -1; }
    bool due(int64_t now) const { return asked_at_ < 0 || now - asked_at_ >= kBatteryPollMs; }
    int64_t deadline() const { return asked_at_ < 0 ? 0 : asked_at_ + kBatteryPollMs; }
    void request(int64_t now);
    bool wanted() const { return wanted_.load(); }
    void run();
    bool take(Battery& out);
    int reads() const { return reads_.load(); }

private:
    std::string dir_;
    int64_t asked_at_ = -1;
    std::atomic<bool> wanted_{false};
    std::atomic<int> reads_{0};
    std::mutex mu_;
    Battery value_;
    uint32_t seq_ = 0, taken_ = 0;
};
Space read_space(const std::string& path);
Storage read_storage(const std::string& data = kDataDir, const std::string& var = kVarDir,
                     const std::string& system = kSystemDir);
std::string gb_words(uint64_t bytes);
std::string storage_value(const Storage& s);
std::vector<InfoLine> storage_lines(const Storage& s);
std::string os_release_value(const std::string& text, const std::string& key);
std::string kernel_release();
const char* frontend_commit();
std::vector<InfoLine> about_lines(const std::string& os_release, const std::string& frontend,
                                  const std::string& kernel);

}
