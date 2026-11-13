#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "input_pad.h"

namespace fd {

struct DevInfo {
    PadId id;
    bool joy = false;
    bool touch = false;
};

enum class DevKind { None, Pad, Touch };

DevKind dev_kind(const DevInfo& d);

struct EvdevOps {
    std::function<std::vector<std::string>()> list;
    std::function<int(const std::string& path)> open;
    std::function<bool(int fd, DevInfo& out)> info;
    std::function<int(int fd)> grab;
    std::function<bool(int fd)> keys_down;
    std::function<std::string(const std::string& path)> real;
};

EvdevOps real_evdev();
bool input_held(const std::function<bool(unsigned long* bits, size_t bytes)>& keys,
                const std::function<bool(int code, int& value)>& abs);

class MenuGrab {
public:
    static constexpr int kRetryMs = 250;
    static constexpr int kBusyMs = 1000;
    static constexpr int kRescanMs = 2000;
    static constexpr int kReleaseMs = 1000;

    explicit MenuGrab(EvdevOps ops, bool wait_up = false) : ops_(std::move(ops)), wait_up_(wait_up) {}
    ~MenuGrab();
    MenuGrab(const MenuGrab&) = delete;
    MenuGrab& operator=(const MenuGrab&) = delete;

    void start(int64_t now);
    bool tick(int64_t now);
    int64_t deadline() const;
    void fds(std::vector<int>& out) const;
    bool read(std::vector<PadEdge>& out, std::vector<bool>* mode = nullptr);
    void begin_release(int64_t now);
    bool releasing(int64_t now);
    int held() const { return (int)nodes_.size(); }
    int busy() const { return (int)busy_.size(); }
    int waiting() const { return (int)waiting_.size(); }

private:
    struct Node {
        std::string path;
        DevKind kind = DevKind::None;
        int fd = -1;
        std::unique_ptr<PadReader> pad;
    };
    struct Busy {
        std::string path;
        DevKind kind = DevKind::None;
        int fd = -1;
        int64_t since = 0;
        bool first = false;
    };

    void scan(int64_t now, bool first);
    bool known(const std::string& path, const std::string& real) const;
    void take(const std::string& path, int fd, DevKind kind);
    void try_grab(const Busy& b, int64_t now);
    void drain(Node& n, std::vector<PadEdge>* out, std::vector<bool>* mode = nullptr);
    static void drop(Node& n);

    EvdevOps ops_;
    std::vector<Node> nodes_;
    std::vector<Busy> busy_, waiting_;
    std::vector<std::pair<std::string, std::string>> ignored_;
    int64_t retry_at_ = -1, rescan_at_ = -1, release_at_ = -1;
    bool wait_up_ = false;
};

class LineFifo {
public:
    LineFifo() = default;
    ~LineFifo();
    LineFifo(const LineFifo&) = delete;
    LineFifo& operator=(const LineFifo&) = delete;

    bool open(const std::string& path);
    void adopt(int fd);
    int fd() const { return fd_; }
    bool ended() const { return ended_; }
    void read(std::vector<std::string>& out);

private:
    int fd_ = -1;
    bool ended_ = false, skip_ = false;
    std::string buf_;
};

bool fifo_action(const std::string& word, PadAction& a);

}
