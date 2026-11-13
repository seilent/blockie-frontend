#include "ports.h"

#include <cerrno>
#include <cstdlib>
#include <vector>

#include <signal.h>
#include <unistd.h>

namespace fd {

namespace {

std::vector<std::string> fields(const std::string& l)
{
    std::vector<std::string> out;
    size_t i = 0;
    for (;;) {
        size_t t = l.find('\t', i);
        out.push_back(l.substr(i, t == std::string::npos ? std::string::npos : t - i));
        if (t == std::string::npos)
            return out;
        i = t + 1;
    }
}

bool number(const std::string& s, int64_t& out)
{
    if (s.empty() || s.size() > 18)
        return false;
    for (char ch : s)
        if (ch < '0' || ch > '9')
            return false;
    out = std::strtoll(s.c_str(), nullptr, 10);
    return true;
}

}

const char* helper_word(HelperCmd c)
{
    switch (c) {
    case HelperCmd::Refresh: return "refresh";
    case HelperCmd::Install: return "install";
    case HelperCmd::Update: return "update";
    case HelperCmd::Remove: return "uninstall";
    }
    return "refresh";
}

HelperOp helper_op(HelperCmd c)
{
    switch (c) {
    case HelperCmd::Install: return HelperOp::Install;
    case HelperCmd::Update: return HelperOp::Update;
    case HelperCmd::Remove: return HelperOp::Remove;
    case HelperCmd::Refresh: break;
    }
    return HelperOp::None;
}

PortsHelper::~PortsHelper()
{
    if (fd_ >= 0)
        close(fd_);
}

void PortsHelper::line(const std::string& l)
{
    std::vector<std::string> f = fields(l);
    if (f[0] == "progress" && f.size() == 4) {
        HelperProgress p;
        p.what = f[1].substr(0, kHelperWhatCap);
        if (!number(f[2], p.done) || (f[3] != "-" && !number(f[3], p.total)))
            return;
        if (on_progress)
            on_progress(p);
        return;
    }
    std::string want = cmd_ == HelperCmd::Refresh ? std::string("refresh") : name_;
    if (f[0] == "done" && f.size() == 2 && f[1] == want) {
        have_ = true;
        last_ = HelperResult{cmd_, name_, true, ""};
    } else if (f[0] == "fail" && f.size() == 3 && f[1] == want && !f[2].empty()) {
        have_ = true;
        last_ = HelperResult{cmd_, name_, false, f[2]};
    }
}

bool PortsHelper::ask(Children& kids, HelperCmd c, const std::string& name)
{
    kids_ = &kids;
    if (running()) {
        queued_ = true;
        next_cmd_ = c;
        next_name_ = name;
        return false;
    }
    return start(c, name);
}

bool PortsHelper::cancel(HelperCmd c, const std::string& name, int64_t now)
{
    if (queued_ && next_cmd_ == c && next_name_ == name) {
        queued_ = false;
        if (on_result)
            on_result(HelperResult{c, name, false, "cancel"});
        return true;
    }
    if (!running() || cmd_ != c || name_ != name)
        return false;
    stop(now);
    return true;
}

bool PortsHelper::holds(const std::string& name, HelperCmd& c) const
{
    if (queued_ && next_cmd_ != HelperCmd::Refresh && next_name_ == name) {
        c = next_cmd_;
        return true;
    }
    if (running() && cmd_ != HelperCmd::Refresh && name_ == name) {
        c = cmd_;
        return true;
    }
    return false;
}

bool PortsHelper::start(HelperCmd c, const std::string& name)
{
    std::vector<std::string> argv{path, helper_word(c)};
    if (c != HelperCmd::Refresh)
        argv.push_back(name);
    cmd_ = c;
    name_ = c == HelperCmd::Refresh ? std::string() : name;
    buf_.clear();
    have_ = false;
    stopped_ = false;
    kill_at_ = -1;
    int out = -1;
    pid_t pid = spawn ? spawn(argv, &out) : kids_ ? kids_->spawn(argv, [this](int) { finish(); }, &out) : -1;
    if (pid <= 0) {
        if (out >= 0)
            close(out);
        if (on_result)
            on_result(HelperResult{c, name_, false, "other"});
        return false;
    }
    pid_ = pid;
    fd_ = out;
    op = helper_op(c);
    if (on_start)
        on_start(c, name_);
    return true;
}

void PortsHelper::drain()
{
    if (fd_ < 0)
        return;
    char chunk[4096];
    for (;;) {
        ssize_t r = read(fd_, chunk, sizeof chunk);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0) {
            if (r == 0 || errno != EAGAIN) {
                close(fd_);
                fd_ = -1;
            }
            break;
        }
        buf_.append(chunk, (size_t)r);
        size_t nl;
        while ((nl = buf_.find('\n')) != std::string::npos) {
            line(buf_.substr(0, nl));
            buf_.erase(0, nl + 1);
        }
        if (buf_.size() > kHelperLineCap)
            buf_.clear();
    }
}

void PortsHelper::readable() { drain(); }

void PortsHelper::finish()
{
    drain();
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
    HelperResult r = have_ ? last_ : HelperResult{cmd_, name_, false, stopped_ ? "cancel" : "other"};
    pid_ = -1;
    kill_at_ = -1;
    op = HelperOp::None;
    if (on_result)
        on_result(r);
    if (queued_ && !running()) {
        queued_ = false;
        start(next_cmd_, next_name_);
    }
}

void PortsHelper::send(int sig)
{
    if (signal_child)
        signal_child(pid_, sig);
    else if (!spawn)
        kill(pid_, sig);
}

void PortsHelper::stop(int64_t now)
{
    if (!running() || kill_at_ >= 0)
        return;
    stopped_ = true;
    send(SIGTERM);
    kill_at_ = now + stop_ms;
}

void PortsHelper::tick(int64_t now)
{
    if (running() && kill_at_ >= 0 && now >= kill_at_) {
        send(SIGKILL);
        kill_at_ = -1;
    }
}

}
