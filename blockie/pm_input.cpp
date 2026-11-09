#include "pm_input.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace fd {

namespace {

constexpr const char* kKeyNodes[] = {"mtk-pmic-keys", "odm:gpio_keys", "mtk-kpd"};
constexpr int kBits = 8 * (int)sizeof(unsigned long);

bool bit(const unsigned long* v, int i) { return (v[i / kBits] >> (i % kBits)) & 1UL; }

}

DevKind dev_kind(const DevInfo& d)
{
    for (const char* n : kKeyNodes)
        if (d.id.name == n)
            return DevKind::None;
    if (pad_matches(d.id) || d.joy)
        return DevKind::Pad;
    if (d.touch)
        return DevKind::Touch;
    return DevKind::None;
}

EvdevOps real_evdev()
{
    EvdevOps o;
    o.list = []() {
        std::vector<std::string> out;
        DIR* dp = opendir("/dev/input");
        if (!dp)
            return out;
        for (struct dirent* e = readdir(dp); e; e = readdir(dp))
            if (std::strncmp(e->d_name, "event", 5) == 0)
                out.push_back(std::string("/dev/input/") + e->d_name);
        closedir(dp);
        std::sort(out.begin(), out.end());
        return out;
    };
    o.open = [](const std::string& path) { return ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC); };
    o.info = [](int fd, DevInfo& d) {
        d.id = pad_id(fd);
        unsigned long keys[(KEY_MAX + kBits) / kBits] = {0};
        unsigned long abs[(ABS_MAX + kBits) / kBits] = {0};
        bool have_keys = ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys) >= 0;
        bool have_abs = ioctl(fd, EVIOCGBIT(EV_ABS, sizeof abs), abs) >= 0;
        for (int c = BTN_JOYSTICK; have_keys && c <= BTN_THUMBR && !d.joy; c++)
            d.joy = bit(keys, c);
        d.touch = have_keys && have_abs && bit(keys, BTN_TOUCH) && bit(abs, ABS_MT_POSITION_X);
        return true;
    };
    o.grab = [](int fd) { return ioctl(fd, EVIOCGRAB, 1) == 0 ? 0 : errno; };
    o.real = [](const std::string& path) {
        std::string node = "/sys/class/input/" + path.substr(path.rfind('/') + 1);
        char buf[PATH_MAX];
        return realpath(node.c_str(), buf) ? std::string(buf) : std::string();
    };
    o.keys_down = [](int fd) {
        unsigned long keys[(KEY_MAX + kBits) / kBits] = {0};
        if (ioctl(fd, EVIOCGKEY(sizeof keys), keys) < 0)
            return false;
        for (unsigned long w : keys)
            if (w)
                return true;
        return false;
    };
    return o;
}

MenuGrab::~MenuGrab()
{
    for (Node& n : nodes_)
        drop(n);
    for (Busy& b : busy_)
        ::close(b.fd);
}

void MenuGrab::drop(Node& n)
{
    if (n.pad)
        n.pad->close();
    else if (n.fd >= 0)
        ::close(n.fd);
    n.pad.reset();
    n.fd = -1;
}

bool MenuGrab::known(const std::string& path, const std::string& real) const
{
    for (const Node& n : nodes_)
        if (n.path == path)
            return true;
    for (const Busy& b : busy_)
        if (b.path == path)
            return true;
    for (const auto& g : ignored_)
        if (g.first == path && g.second == real)
            return true;
    return false;
}

void MenuGrab::take(const std::string& path, int fd, DevKind kind)
{
    Node n;
    n.path = path;
    n.kind = kind;
    n.fd = fd;
    if (kind == DevKind::Pad) {
        n.pad = std::make_unique<PadReader>();
        n.pad->adopt_fd(fd);
    }
    nodes_.push_back(std::move(n));
}

void MenuGrab::scan(int64_t now, bool first)
{
    std::vector<std::string> paths = ops_.list();
    ignored_.erase(std::remove_if(ignored_.begin(), ignored_.end(),
                                  [&paths](const std::pair<std::string, std::string>& g) {
                                      return std::find(paths.begin(), paths.end(), g.first) == paths.end();
                                  }),
                   ignored_.end());
    for (const std::string& path : paths) {
        std::string real = ops_.real ? ops_.real(path) : std::string();
        if (known(path, real))
            continue;
        int fd = ops_.open(path);
        if (fd < 0)
            continue;
        DevInfo d;
        DevKind kind = ops_.info(fd, d) ? dev_kind(d) : DevKind::None;
        if (kind == DevKind::None) {
            ::close(fd);
            ignored_.erase(std::remove_if(ignored_.begin(), ignored_.end(),
                                          [&path](const std::pair<std::string, std::string>& g) {
                                              return g.first == path;
                                          }),
                           ignored_.end());
            ignored_.emplace_back(path, real);
            continue;
        }
        int rc = ops_.grab(fd);
        if (rc == 0) {
            take(path, fd, kind);
        } else if (rc == EBUSY && first) {
            busy_.push_back({path, kind, fd, now});
            if (retry_at_ < 0)
                retry_at_ = now + kRetryMs;
        } else {
            if (rc == EBUSY)
                std::fprintf(stderr, "blockie-powermenu: %s is busy at a rescan, skipped\n", path.c_str());
            ::close(fd);
        }
    }
    rescan_at_ = now + kRescanMs;
}

void MenuGrab::start(int64_t now) { scan(now, true); }

bool MenuGrab::tick(int64_t now)
{
    if (release_at_ >= 0)
        return true;
    if (retry_at_ >= 0 && now >= retry_at_) {
        retry_at_ = -1;
        for (size_t i = 0; i < busy_.size();) {
            Busy& b = busy_[i];
            int rc = ops_.grab(b.fd);
            if (rc == 0) {
                take(b.path, b.fd, b.kind);
                busy_.erase(busy_.begin() + (long)i);
            } else if (rc == EBUSY) {
                if (now - b.since >= kBusyMs) {
                    for (Busy& k : busy_)
                        ::close(k.fd);
                    busy_.clear();
                    return false;
                }
                i++;
            } else {
                ::close(b.fd);
                busy_.erase(busy_.begin() + (long)i);
            }
        }
        if (!busy_.empty())
            retry_at_ = now + kRetryMs;
    }
    if (now >= rescan_at_)
        scan(now, false);
    return true;
}

int64_t MenuGrab::deadline() const
{
    if (release_at_ >= 0)
        return release_at_ + kReleaseMs;
    return retry_at_ < 0 ? rescan_at_ : std::min(retry_at_, rescan_at_);
}

void MenuGrab::fds(std::vector<int>& out) const
{
    for (const Node& n : nodes_)
        if (n.fd >= 0)
            out.push_back(n.fd);
}

void MenuGrab::drain(Node& n, std::vector<PadEdge>* out)
{
    if (n.pad) {
        std::vector<PadEdge> e = n.pad->poll_edges();
        if (out)
            out->insert(out->end(), e.begin(), e.end());
        if (!n.pad->is_open())
            n.fd = -1;
        return;
    }
    input_event ev[16];
    for (;;) {
        ssize_t r = ::read(n.fd, ev, sizeof ev);
        if (r > 0)
            continue;
        if (r < 0 && errno == EINTR)
            continue;
        if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return;
        ::close(n.fd);
        n.fd = -1;
        return;
    }
}

bool MenuGrab::read(std::vector<PadEdge>& out)
{
    bool dropped = false;
    for (Node& n : nodes_) {
        drain(n, &out);
        dropped = dropped || (n.fd < 0 && n.kind == DevKind::Pad);
    }
    nodes_.erase(std::remove_if(nodes_.begin(), nodes_.end(), [](const Node& n) { return n.fd < 0; }), nodes_.end());
    return dropped;
}

void MenuGrab::begin_release(int64_t now)
{
    if (release_at_ >= 0)
        return;
    release_at_ = now;
    for (Busy& b : busy_)
        ::close(b.fd);
    busy_.clear();
}

bool MenuGrab::releasing(int64_t now)
{
    bool cap = now - release_at_ >= kReleaseMs;
    for (Node& n : nodes_) {
        drain(n, nullptr);
        if (n.fd >= 0 && (cap || !ops_.keys_down(n.fd)))
            drop(n);
    }
    nodes_.erase(std::remove_if(nodes_.begin(), nodes_.end(), [](const Node& n) { return n.fd < 0; }), nodes_.end());
    return !nodes_.empty();
}

LineFifo::~LineFifo()
{
    if (fd_ >= 0)
        ::close(fd_);
}

bool LineFifo::open(const std::string& path)
{
    fd_ = ::open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    return fd_ >= 0;
}

void LineFifo::read(std::vector<std::string>& out)
{
    if (fd_ < 0)
        return;
    char buf[256];
    for (;;) {
        ssize_t r = ::read(fd_, buf, sizeof buf);
        if (r > 0) {
            buf_.append(buf, (size_t)r);
            continue;
        }
        if (r < 0 && errno == EINTR)
            continue;
        break;
    }
    size_t at;
    while ((at = buf_.find('\n')) != std::string::npos) {
        std::string ln = buf_.substr(0, at);
        buf_.erase(0, at + 1);
        if (!ln.empty() && ln.back() == '\r')
            ln.pop_back();
        if (!ln.empty())
            out.push_back(ln);
    }
}

bool fifo_action(const std::string& word, PadAction& a)
{
    static const struct {
        const char* word;
        PadAction a;
    } kWords[] = {{"up", PadAction::Up},         {"down", PadAction::Down},     {"left", PadAction::Left},
                  {"right", PadAction::Right},   {"accept", PadAction::Accept}, {"back", PadAction::Back}};
    for (const auto& w : kWords)
        if (word == w.word) {
            a = w.a;
            return true;
        }
    return false;
}

}
