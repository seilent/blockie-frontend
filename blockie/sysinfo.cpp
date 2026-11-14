#include "sysinfo.h"

#include <cstdio>

#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>

#ifndef BLOCKIE_FE_COMMIT
#define BLOCKIE_FE_COMMIT ""
#endif
#ifndef BLOCKIE_FE_STEAM_FILES
#define BLOCKIE_FE_STEAM_FILES 0
#endif
#define BLOCKIE_WORD(x) #x
#define BLOCKIE_VALUE(x) BLOCKIE_WORD(x)

namespace fd {

namespace {

constexpr size_t kReadMax = 65536;
const char* const kCommit = BLOCKIE_FE_COMMIT;
const char* const kSteamFiles = "steam_files=" BLOCKIE_VALUE(BLOCKIE_FE_STEAM_FILES);

bool read_text(const std::string& path, std::string& out)
{
    out.clear();
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f)
        return false;
    char buf[4096];
    size_t n;
    while (out.size() < kReadMax && (n = std::fread(buf, 1, sizeof buf, f)) > 0)
        out.append(buf, n);
    bool ok = !std::ferror(f);
    std::fclose(f);
    return ok;
}

std::string trimmed(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string space_words(const Space& s)
{
    return s.ok ? gb_words(s.avail) + " of " + gb_words(s.total) + " gb free" : "unknown";
}

}

Battery read_battery(const std::string& dir)
{
    Battery b;
    std::string cap, status;
    if (!read_text(dir + "/capacity", cap))
        return b;
    cap = trimmed(cap);
    if (cap.empty() || cap.size() > 3)
        return b;
    int v = 0;
    for (char c : cap) {
        if (c < '0' || c > '9')
            return b;
        v = v * 10 + (c - '0');
    }
    if (v > 100)
        return b;
    b.ok = true;
    b.capacity = v;
    if (read_text(dir + "/status", status))
        b.status = trimmed(status);
    return b;
}

std::string battery_words(const Battery& b)
{
    if (!b.ok || b.status == "Unknown")
        return "unknown";
    std::string out = std::to_string(b.capacity) + " %";
    if (b.status == "Charging")
        out += " charging";
    else if (b.status == "Full")
        out += " full";
    else if (b.status == "Not charging")
        out += " not charging";
    return out;
}

void BatteryFeed::request(int64_t now)
{
    asked_at_ = now;
    wanted_.store(true);
}

void BatteryFeed::run()
{
    wanted_.store(false);
    Battery b = read_battery(dir_);
    reads_++;
    std::lock_guard<std::mutex> lk(mu_);
    value_ = b;
    seq_++;
}

bool BatteryFeed::take(Battery& out)
{
    std::lock_guard<std::mutex> lk(mu_);
    if (seq_ == taken_)
        return false;
    taken_ = seq_;
    out = value_;
    return true;
}

Space read_space(const std::string& path)
{
    Space s;
    struct statvfs v;
    if (statvfs(path.c_str(), &v) != 0)
        return s;
    s.ok = true;
    s.avail = (uint64_t)v.f_bavail * (uint64_t)v.f_frsize;
    s.total = (uint64_t)v.f_blocks * (uint64_t)v.f_frsize;
    return s;
}

Storage read_storage(const std::string& data, const std::string& var, const std::string& system)
{
    Storage s;
    struct stat d, r;
    s.mounted = stat(data.c_str(), &d) == 0 && stat(var.c_str(), &r) == 0 && d.st_dev != r.st_dev;
    if (s.mounted)
        s.data = read_space(data);
    s.system = read_space(system);
    return s;
}

std::string gb_words(uint64_t bytes)
{
    const uint64_t g = 1ull << 30;
    if (bytes == 0)
        return "0";
    uint64_t tenths = (bytes * 10 + g - 1) / g;
    if (tenths < 100)
        return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10);
    return std::to_string((bytes + g - 1) / g);
}

std::string storage_value(const Storage& s)
{
    if (!s.mounted)
        return "no card data";
    return s.data.ok ? gb_words(s.data.avail) + " gb free" : "unknown";
}

std::vector<InfoLine> storage_lines(const Storage& s)
{
    return {{"data", s.mounted ? space_words(s.data) : "no card"}, {"system", space_words(s.system)}};
}

std::string os_release_value(const std::string& text, const std::string& key)
{
    std::string want = key + "=";
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos)
            end = text.size();
        std::string line = trimmed(text.substr(at, end - at));
        at = end + 1;
        if (line.compare(0, want.size(), want) != 0)
            continue;
        std::string v = line.substr(want.size());
        if (v.size() >= 2 && (v.front() == '"' || v.front() == '\'') && v.back() == v.front())
            v = v.substr(1, v.size() - 2);
        return v;
    }
    return "";
}

std::string kernel_release()
{
    struct utsname u;
    return uname(&u) == 0 ? u.release : "";
}

const char* frontend_commit() { return kCommit; }

const char* frontend_steam_files() { return kSteamFiles; }

std::vector<InfoLine> about_lines(const std::string& os_release, const std::string& frontend,
                                  const std::string& kernel)
{
    std::string text, image;
    if (read_text(os_release, text))
        image = os_release_value(text, "IMAGE_VERSION");
    auto or_unknown = [](const std::string& v) { return v.empty() ? std::string("unknown") : v; };
    return {{"blockie", or_unknown(image)},
            {"frontend", or_unknown(frontend)},
            {"kernel", or_unknown(kernel)},
            {"wi-fi country", "JP"}};
}

}
