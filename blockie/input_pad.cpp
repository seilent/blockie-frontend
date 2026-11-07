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

std::string ioctl_str(int fd, unsigned long req)
{
    char buf[256] = {0};
    if (ioctl(fd, req, sizeof buf - 1, buf) < 0)
        return {};
    buf[sizeof buf - 1] = 0;
    return buf;
}

}

bool pad_matches(const PadId& id)
{
    if (id.vendor == 0x4001 && id.product == 0x0428)
        return true;
    if (id.name == "AYANEO Controller")
        return true;
    if (id.phys.rfind("ayaneo-gamepad", 0) == 0)
        return true;
    return false;
}

PadReader::~PadReader() { close(); }

void PadReader::close()
{
    if (fd_ >= 0)
        ::close(fd_);
    fd_ = -1;
    hat_x_ = 0;
    hat_y_ = 0;
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
        input_id iid = {};
        PadId id;
        if (ioctl(fd, EVIOCGID, &iid) == 0) {
            id.bus = iid.bustype;
            id.vendor = iid.vendor;
            id.product = iid.product;
        }
        id.name = ioctl_str(fd, EVIOCGNAME(0));
        id.phys = ioctl_str(fd, EVIOCGPHYS(0));
        if (pad_matches(id)) {
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
        if (ev.type == EV_KEY && ev.value == 1) {
            switch (ev.code) {
            case BTN_EAST: out.push_back(PadAction::Accept); break;
            case BTN_SOUTH: out.push_back(PadAction::Back); break;
            case BTN_DPAD_UP: out.push_back(PadAction::Up); break;
            case BTN_DPAD_DOWN: out.push_back(PadAction::Down); break;
            case BTN_DPAD_LEFT: out.push_back(PadAction::Left); break;
            case BTN_DPAD_RIGHT: out.push_back(PadAction::Right); break;
            default: break;
            }
        } else if (ev.type == EV_ABS && ev.code == ABS_HAT0X) {
            if (ev.value != hat_x_ && ev.value > 0)
                out.push_back(PadAction::Right);
            else if (ev.value != hat_x_ && ev.value < 0)
                out.push_back(PadAction::Left);
            hat_x_ = ev.value;
        } else if (ev.type == EV_ABS && ev.code == ABS_HAT0Y) {
            if (ev.value != hat_y_ && ev.value > 0)
                out.push_back(PadAction::Down);
            else if (ev.value != hat_y_ && ev.value < 0)
                out.push_back(PadAction::Up);
            hat_y_ = ev.value;
        }
    }
    return out;
}

}
