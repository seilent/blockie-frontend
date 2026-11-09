#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include <ftw.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../sysinfo.h"

using namespace fd;

namespace {

int g_fail = 0;
int g_total = 0;

void check(bool ok, const char* what)
{
    g_total++;
    if (ok) {
        std::printf("PASS %s\n", what);
    } else {
        std::printf("FAIL %s\n", what);
        g_fail++;
    }
}

void write_file(const std::string& path, const std::string& body)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f) {
        std::fwrite(body.data(), 1, body.size(), f);
        std::fclose(f);
    }
}

int rm_cb(const char* path, const struct stat*, int, struct FTW*)
{
    remove(path);
    return 0;
}

std::string make_root()
{
    const char* dir = std::getenv("TMPDIR");
    std::string tmpl = std::string(dir && *dir ? dir : "/tmp") + "/blockie-sysinfo-XXXXXX";
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    return mkdtemp(buf.data()) ? std::string(buf.data()) : std::string();
}

bool ends_with(const std::string& s, const std::string& tail)
{
    return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

}

int main(int argc, char** argv)
{
    std::string root = make_root();
    if (root.empty()) {
        std::printf("FAIL no temp dir\n");
        return 1;
    }
    std::string bat = root + "/battery";
    mkdir(bat.c_str(), 0755);

    write_file(bat + "/capacity", "62\n");
    write_file(bat + "/status", "Charging\n");
    Battery b = read_battery(bat);
    check(b.ok && b.capacity == 62 && b.status == "Charging", "battery capacity and status read");
    check(battery_words(b) == "62 % charging", "charging reads 62 % charging");
    const char* const statuses[][2] = {
        {"Full", "62 % full"}, {"Not charging", "62 % not charging"}, {"Discharging", "62 %"}, {"Unknown", "unknown"}};
    for (const auto& st : statuses) {
        write_file(bat + "/status", std::string(st[0]) + "\n");
        std::string what = std::string("status ") + st[0] + " reads " + st[1];
        check(battery_words(read_battery(bat)) == st[1], what.c_str());
    }
    unlink((bat + "/status").c_str());
    check(battery_words(read_battery(bat)) == "62 %", "no status file: the level only");
    write_file(bat + "/capacity", "100\n");
    check(battery_words(read_battery(bat)) == "100 %", "100 reads");
    write_file(bat + "/capacity", "0");
    check(battery_words(read_battery(bat)) == "0 %", "0 without a newline reads");
    for (const char* bad : {"101\n", "abc\n", "\n", "-5\n", "6 2\n"}) {
        write_file(bat + "/capacity", bad);
        std::string what = std::string("capacity ") + bad + " reads unknown";
        what.erase(what.find('\n'), 1);
        check(!read_battery(bat).ok && battery_words(read_battery(bat)) == "unknown", what.c_str());
    }
    check(battery_words(read_battery(root + "/none")) == "unknown", "no battery dir reads unknown");

    write_file(bat + "/capacity", "62\n");
    write_file(bat + "/status", "Charging\n");
    BatteryFeed feed(bat);
    Battery got;
    check(feed.due(0) && feed.deadline() == 0 && !feed.wanted(), "a fresh feed is due at once");
    feed.request(1000);
    check(feed.wanted() && !feed.due(1000 + kBatteryPollMs - 1) && feed.due(1000 + kBatteryPollMs) &&
              feed.deadline() == 1000 + kBatteryPollMs,
          "a request is wanted, the next one is due 10 s later");
    check(!feed.take(got) && feed.reads() == 0, "nothing to take before the worker read");
    std::thread worker([&] { feed.run(); });
    worker.join();
    check(!feed.wanted() && feed.reads() == 1, "the worker read once and cleared the request");
    check(feed.take(got) && got.ok && got.capacity == 62 && battery_words(got) == "62 % charging",
          "the frame side takes the worker's value");
    write_file(bat + "/capacity", "10\n");
    bool again = false;
    for (int i = 0; i < 5; i++)
        again = again || feed.take(got);
    check(!again && got.capacity == 62 && feed.reads() == 1,
          "one value per read: taking again reads no sysfs and keeps the last value");
    feed.request(1000 + kBatteryPollMs);
    worker = std::thread([&] { feed.run(); });
    worker.join();
    check(feed.take(got) && got.capacity == 10 && feed.reads() == 2, "the next read publishes the new level");
    feed.restart();
    check(feed.due(1000 + kBatteryPollMs + 1) && feed.deadline() == 0, "a page open makes a read due at once");

    const uint64_t g = 1ull << 30;
    check(gb_words(0) == "0", "0 bytes");
    check(gb_words(1) == "0.1", "a byte rounds up to 0.1");
    check(gb_words(88046829568ull) == "82", "82 GiB");
    check(gb_words(113816633344ull) == "106", "106 GiB");
    check(gb_words(4831838208ull) == "4.5", "4.5 GiB");
    check(gb_words(4617089843ull) == "4.3", "just under 4.3 GiB rounds up to 4.3");
    check(gb_words(10 * g - 1) == "10", "just under 10 GiB reads 10");
    check(gb_words(10 * g) == "10", "10 GiB");
    check(gb_words(10 * g + 1) == "11", "past 10 GiB rounds up to the next whole");

    std::string data = root + "/data";
    std::string var = root + "/var";
    mkdir(data.c_str(), 0755);
    mkdir(var.c_str(), 0755);
    Storage s = read_storage(data, var, root);
    check(!s.mounted && storage_value(s) == "no card data", "data on /var's device reads no card data");
    std::vector<InfoLine> lines = storage_lines(s);
    check(lines.size() == 2 && lines[0].title == "data" && lines[0].value == "no card data" &&
              lines[1].title == "system" && ends_with(lines[1].value, " gb free") &&
              lines[1].value.find(" of ") != std::string::npos,
          "row sheet: data no card data, system <free> of <total> gb free");
    s = read_storage("/proc", var, root);
    check(s.mounted && s.data.ok && ends_with(storage_value(s), " gb free"), "data on its own device reads free space");
    s = read_storage(root + "/none", var, root + "/none");
    lines = storage_lines(s);
    check(!s.mounted && lines.size() == 2 && lines[1].value == "unknown", "an unreadable system reads unknown");
    Storage fixed;
    fixed.mounted = true;
    fixed.data = {true, 88046829568ull, 113816633344ull};
    fixed.system = {true, 4617089843ull, 8375186227ull};
    lines = storage_lines(fixed);
    check(storage_value(fixed) == "82 gb free" && lines[0].value == "82 of 106 gb free" &&
              lines[1].value == "4.3 of 7.8 gb free",
          "words for 82 of 106 and 4.3 of 7.8");

    std::string osr = root + "/os-release";
    write_file(osr, "NAME=Blockie\nID=blockie\nPRETTY_NAME=\"Blockie\"\nIMAGE_VERSION=20261109.abc1234\n");
    lines = about_lines(osr, "0abc123", "5.10.0-test");
    check(lines.size() == 4 && lines[0].title == "blockie" && lines[0].value == "20261109.abc1234" &&
              lines[1].title == "frontend" && lines[1].value == "0abc123" && lines[2].title == "kernel" &&
              lines[2].value == "5.10.0-test" && lines[3].title == "wi-fi country" && lines[3].value == "jp",
          "about lines from os-release, the build and uname");
    check(os_release_value("A=1\nIMAGE_VERSION=\"20261109.q\"\n", "IMAGE_VERSION") == "20261109.q" &&
              os_release_value("IMAGE_VERSION='x'", "IMAGE_VERSION") == "x" &&
              os_release_value("XIMAGE_VERSION=y\n", "IMAGE_VERSION").empty(),
          "os-release values: quotes dropped, whole key only");
    lines = about_lines(root + "/none", "", "");
    check(lines.size() == 4 && lines[0].value == "unknown" && lines[1].value == "unknown" &&
              lines[2].value == "unknown",
          "a missing field reads unknown");
    check(!kernel_release().empty(), "uname gives a kernel release");
    std::string want = argc > 1 ? argv[1] : "";
    check(want == frontend_commit(), "the frontend commit is the build's BLOCKIE_FE_COMMIT");
    lines = about_lines(osr, frontend_commit(), "5.10.0-test");
    check(lines.size() == 4 && lines[1].value == (want.empty() ? std::string("unknown") : want),
          "the about row shows the build's commit, unknown when the build had none");

    nftw(root.c_str(), rm_cb, 16, FTW_DEPTH | FTW_PHYS);
    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
