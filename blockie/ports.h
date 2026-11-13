#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <sys/types.h>

#include "children.h"

namespace fd {

constexpr const char* kPortsSystem = "ports";
constexpr const char* kPortsIndex = "/.blockie/index.tsv";
constexpr const char* kPortsHelperPath = "/usr/libexec/blockie/blockie-ports";
constexpr int64_t kHelperStopMs = 10000;
constexpr size_t kHelperLineCap = 4096;
constexpr size_t kHelperWhatCap = 32;

enum class HelperOp { None, Install, Update, Remove };
enum class HelperCmd { Refresh, Install, Update, Remove };

struct HelperResult {
    HelperCmd cmd = HelperCmd::Refresh;
    std::string name;
    bool done = false;
    std::string why;
};

struct HelperProgress {
    std::string what;
    int64_t done = 0;
    int64_t total = -1;
};

const char* helper_word(HelperCmd c);
HelperOp helper_op(HelperCmd c);

class PortsHelper {
public:
    HelperOp op = HelperOp::None;
    std::string path = kPortsHelperPath;
    int64_t stop_ms = kHelperStopMs;
    std::function<void(const HelperResult&)> on_result;
    std::function<void(const HelperProgress&)> on_progress;
    std::function<void(HelperCmd, const std::string&)> on_start;
    std::function<pid_t(const std::vector<std::string>& argv, int* out)> spawn;
    std::function<void(pid_t pid, int sig)> signal_child;

    PortsHelper() = default;
    PortsHelper(const PortsHelper&) = delete;
    PortsHelper& operator=(const PortsHelper&) = delete;
    ~PortsHelper();

    void line(const std::string& l);
    bool ask(Children& kids, HelperCmd c, const std::string& name);
    bool cancel(HelperCmd c, const std::string& name, int64_t now);
    bool running() const { return pid_ > 0; }
    bool holds(const std::string& name, HelperCmd& c) const;
    HelperCmd cmd() const { return cmd_; }
    const std::string& name() const { return name_; }
    int fd() const { return fd_; }
    void readable();
    void finish();
    void stop(int64_t now);
    void tick(int64_t now);
    int64_t deadline() const { return kill_at_; }

private:
    bool start(HelperCmd c, const std::string& name);
    void drain();
    void send(int sig);

    Children* kids_ = nullptr;
    pid_t pid_ = -1;
    int fd_ = -1;
    HelperCmd cmd_ = HelperCmd::Refresh;
    std::string name_;
    std::string buf_;
    bool have_ = false;
    bool stopped_ = false;
    HelperResult last_;
    int64_t kill_at_ = -1;
    bool queued_ = false;
    HelperCmd next_cmd_ = HelperCmd::Refresh;
    std::string next_name_;
};

}
