#pragma once
#include <cstddef>
#include <functional>
#include <memory>
#include <string>

#include <sys/types.h>

#include "children.h"

namespace fd {

struct VolumeLevel {
    int pct = 0;
    bool muted = false;
};

bool parse_volume(const std::string& out, VolumeLevel& v);

class Volume {
public:
    static constexpr size_t kOutMax = 256;

    explicit Volume(Children& kids, std::string tool = "/usr/libexec/blockie/blockie-volume");
    ~Volume();
    Volume(const Volume&) = delete;
    Volume& operator=(const Volume&) = delete;

    std::function<void(const VolumeLevel& v)> on_value;
    std::function<void()> on_fail;
    std::function<void(bool ok)> on_set;

    void get();
    bool set(int pct) { return put(pct, -1); }
    bool set(int pct, bool muted) { return put(pct, muted ? 1 : 0); }
    int fd() const { return fd_; }
    void readable();
    bool getting() const { return get_pid_ > 0; }
    bool setting() const { return set_pid_ > 0; }

private:
    void run_get();
    void got(int status);
    bool put(int pct, int muted);
    bool run_set(int pct, int muted);
    void set_done(int status);
    void finish_set(bool ok);
    void drain();
    void close_fd();

    Children& kids_;
    std::string tool_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    pid_t get_pid_ = -1;
    pid_t set_pid_ = -1;
    int fd_ = -1;
    std::string out_;
    bool again_ = false;
    bool stale_ = false;
    int next_ = -1;
    int next_muted_ = -1;
};

}
