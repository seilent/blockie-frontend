#include "volume.h"

#include <cerrno>
#include <cstdio>
#include <utility>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

namespace fd {

namespace {

bool exited_ok(int status) { return WIFEXITED(status) && WEXITSTATUS(status) == 0; }

}

bool parse_volume(const std::string& out, VolumeLevel& v)
{
    std::string s = out;
    if (!s.empty() && s.back() == '\n')
        s.pop_back();
    size_t sp = s.find(' ');
    if (sp == std::string::npos || sp == 0 || sp > 3)
        return false;
    int pct = 0;
    for (size_t i = 0; i < sp; i++) {
        if (s[i] < '0' || s[i] > '9')
            return false;
        pct = pct * 10 + (s[i] - '0');
    }
    std::string word = s.substr(sp + 1);
    if (word != "muted" && word != "unmuted")
        return false;
    v.pct = pct;
    v.muted = word == "muted";
    return true;
}

Volume::Volume(Children& kids, std::string tool) : kids_(kids), tool_(std::move(tool)) {}

Volume::~Volume() { close_fd(); }

void Volume::get()
{
    if (get_pid_ > 0 || set_pid_ > 0) {
        again_ = true;
        return;
    }
    run_get();
}

void Volume::run_get()
{
    again_ = false;
    stale_ = false;
    out_.clear();
    std::weak_ptr<int> alive = life_;
    int out = -1;
    pid_t pid = kids_.spawn(
        {tool_, "get"},
        [this, alive](int status) {
            if (!alive.expired())
                got(status);
        },
        &out);
    if (pid < 0) {
        std::fprintf(stderr, "blockie-frontend: volume get did not start\n");
        if (on_fail)
            on_fail();
        return;
    }
    get_pid_ = pid;
    fd_ = out;
}

void Volume::readable() { drain(); }

void Volume::drain()
{
    char buf[128];
    while (fd_ >= 0) {
        ssize_t n = read(fd_, buf, sizeof buf);
        if (n > 0) {
            out_.append(buf, (size_t)n);
            if (out_.size() > kOutMax)
                out_.resize(kOutMax);
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        if (n == 0)
            close_fd();
        break;
    }
}

void Volume::close_fd()
{
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
}

void Volume::got(int status)
{
    get_pid_ = -1;
    drain();
    close_fd();
    VolumeLevel v;
    bool ok = exited_ok(status) && parse_volume(out_, v);
    out_.clear();
    if (stale_) {
        stale_ = false;
        again_ = true;
    } else if (ok) {
        if (on_value)
            on_value(v);
    } else {
        std::fprintf(stderr, "blockie-frontend: volume get failed\n");
        if (on_fail)
            on_fail();
    }
    if (again_ && get_pid_ <= 0 && set_pid_ <= 0)
        run_get();
}

bool Volume::put(int pct, int muted)
{
    if (pct < 0 || pct > 100)
        return false;
    if (set_pid_ > 0) {
        next_ = pct;
        next_muted_ = muted;
    } else if (!run_set(pct, muted)) {
        finish_set(false);
    }
    return true;
}

bool Volume::run_set(int pct, int muted)
{
    if (get_pid_ > 0)
        stale_ = true;
    std::vector<std::string> argv = {tool_, "set", std::to_string(pct)};
    if (muted >= 0)
        argv.push_back(muted ? "muted" : "unmuted");
    std::weak_ptr<int> alive = life_;
    pid_t pid = kids_.spawn(argv, [this, alive](int status) {
        if (!alive.expired())
            set_done(status);
    });
    if (pid < 0)
        return false;
    set_pid_ = pid;
    return true;
}

void Volume::set_done(int status)
{
    set_pid_ = -1;
    bool ok = exited_ok(status);
    if (next_ >= 0) {
        int pct = next_;
        next_ = -1;
        if (run_set(pct, next_muted_))
            return;
        ok = false;
    }
    finish_set(ok);
}

void Volume::finish_set(bool ok)
{
    if (!ok)
        std::fprintf(stderr, "blockie-frontend: volume set failed\n");
    if (on_set)
        on_set(ok);
    if (again_ && get_pid_ <= 0 && set_pid_ <= 0)
        run_get();
}

}
