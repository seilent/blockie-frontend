#include "input_pad.h"

#include <cerrno>
#include <cstring>

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace fd {

namespace {

constexpr int kStr = 256;

std::string ioctl_str(int fd, unsigned long req)
{
    char buf[kStr] = {0};
    if (ioctl(fd, req, buf) < 0)
        return {};
    buf[kStr - 1] = 0;
    return buf;
}

constexpr PadAction kDirs[4] = {PadAction::Up, PadAction::Down, PadAction::Left, PadAction::Right};
constexpr int kDpadCodes[4] = {BTN_DPAD_UP, BTN_DPAD_DOWN, BTN_DPAD_LEFT, BTN_DPAD_RIGHT};

int dir_index(PadAction a)
{
    for (int i = 0; i < 4; i++)
        if (kDirs[i] == a)
            return i;
    return -1;
}

void hat_edges(std::vector<PadEdge>& out, int& cur, int v, PadAction neg, PadAction pos)
{
    if (v == cur)
        return;
    if (cur < 0)
        out.push_back({neg, false});
    else if (cur > 0)
        out.push_back({pos, false});
    if (v < 0)
        out.push_back({neg, true});
    else if (v > 0)
        out.push_back({pos, true});
    cur = v;
}

}

bool pad_matches(const PadId& id)
{
    return id.name == "AYANEO Controller" && id.bus == 0x0019 && id.vendor == 0x4001 && id.product == 0x0428 &&
           id.phys == "ayaneo-gamepad/input0";
}

PadId pad_id(int fd)
{
    PadId id;
    input_id iid = {};
    if (ioctl(fd, EVIOCGID, &iid) == 0) {
        id.bus = iid.bustype;
        id.vendor = iid.vendor;
        id.product = iid.product;
    }
    id.name = ioctl_str(fd, EVIOCGNAME(kStr - 1));
    id.phys = ioctl_str(fd, EVIOCGPHYS(kStr - 1));
    return id;
}

PadReader::~PadReader() { close(); }

void PadReader::close()
{
    if (fd_ >= 0)
        ::close(fd_);
    fd_ = -1;
    hat_x_ = 0;
    hat_y_ = 0;
    for (bool& d : dpad_)
        d = false;
    dropping_ = false;
}

void PadReader::adopt_fd(int fd)
{
    close();
    fd_ = fd;
}

bool PadReader::open()
{
    DIR* dp = opendir("/dev/input");
    if (!dp)
        return false;
    bool found = false;
    for (struct dirent* e = readdir(dp); e && !found; e = readdir(dp)) {
        if (std::strncmp(e->d_name, "event", 5) != 0)
            continue;
        std::string path = std::string("/dev/input/") + e->d_name;
        int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            continue;
        if (pad_matches(pad_id(fd))) {
            fd_ = fd;
            found = true;
        } else {
            ::close(fd);
        }
    }
    closedir(dp);
    return found;
}

std::vector<PadAction> PadReader::poll()
{
    std::vector<PadAction> out;
    for (const PadEdge& e : poll_edges())
        if (e.down)
            out.push_back(e.action);
    return out;
}

std::vector<PadEdge> PadReader::poll_edges()
{
    std::vector<PadEdge> out;
    if (fd_ < 0)
        return out;
    input_event ev;
    for (;;) {
        ssize_t n = read(fd_, &ev, sizeof ev);
        if (n != (ssize_t)sizeof ev) {
            if (n < 0 && errno == EINTR)
                continue;
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                break;
            close();
            break;
        }
        if (ev.type == EV_SYN && ev.code == SYN_DROPPED) {
            dropping_ = true;
            continue;
        }
        if (dropping_) {
            if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
                dropping_ = false;
                resync(out);
            }
            continue;
        }
        if (ev.type == EV_KEY && ev.value != 2) {
            bool down = ev.value == 1;
            switch (ev.code) {
            case BTN_EAST:
                if (down)
                    out.push_back({PadAction::Accept, true});
                break;
            case BTN_SOUTH:
                if (down)
                    out.push_back({PadAction::Back, true});
                break;
            case BTN_WEST:
                if (down)
                    out.push_back({PadAction::Context, true});
                break;
            case BTN_TL:
                if (down)
                    out.push_back({PadAction::Prev, true});
                break;
            case BTN_TR:
                if (down)
                    out.push_back({PadAction::Next, true});
                break;
            case BTN_START:
                if (down)
                    out.push_back({PadAction::Start, true});
                break;
            default:
                for (int i = 0; i < 4; i++)
                    if (ev.code == kDpadCodes[i]) {
                        dpad_[i] = down;
                        out.push_back({kDirs[i], down});
                    }
                break;
            }
        } else if (ev.type == EV_ABS && ev.code == ABS_HAT0X) {
            hat_edges(out, hat_x_, ev.value, PadAction::Left, PadAction::Right);
        } else if (ev.type == EV_ABS && ev.code == ABS_HAT0Y) {
            hat_edges(out, hat_y_, ev.value, PadAction::Up, PadAction::Down);
        }
    }
    return out;
}

Held PadReader::held(PadAction dir) const
{
    int i = dir_index(dir);
    if (i < 0 || fd_ < 0)
        return Held::No;
    constexpr int kBits = 8 * (int)sizeof(unsigned long);
    unsigned long keys[(KEY_MAX + kBits) / kBits] = {0};
    bool have_keys = ioctl(fd_, EVIOCGKEY(sizeof keys), keys) >= 0;
    input_absinfo ai = {};
    bool vertical = dir == PadAction::Up || dir == PadAction::Down;
    bool have_abs = ioctl(fd_, EVIOCGABS(vertical ? ABS_HAT0Y : ABS_HAT0X), &ai) >= 0;
    if (!have_keys && !have_abs)
        return Held::Unknown;
    int code = kDpadCodes[i];
    bool key = have_keys && ((keys[code / kBits] >> (code % kBits)) & 1UL);
    bool negative = dir == PadAction::Up || dir == PadAction::Left;
    bool hat = have_abs && (negative ? ai.value < 0 : ai.value > 0);
    return key || hat ? Held::Yes : Held::No;
}

void PadReader::resync(std::vector<PadEdge>& out)
{
    for (int i = 0; i < 4; i++) {
        PadAction d = kDirs[i];
        bool negative = d == PadAction::Up || d == PadAction::Left;
        int& hat = (d == PadAction::Up || d == PadAction::Down) ? hat_y_ : hat_x_;
        bool by_hat = negative ? hat < 0 : hat > 0;
        if (!dpad_[i] && !by_hat)
            continue;
        if (held(d) == Held::Yes)
            continue;
        dpad_[i] = false;
        if (by_hat)
            hat = 0;
        out.push_back({d, false});
    }
}

}
