#include "log.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace fd {

namespace {

std::atomic<bool> g_on{false};

}

void log_info(const char* fmt, ...)
{
    if (!g_on.load(std::memory_order_relaxed))
        return;
    va_list a;
    va_start(a, fmt);
    vprintf(fmt, a);
    va_end(a);
}

void log_info_set(bool on) { g_on = on; }

bool log_info_on() { return g_on.load(); }

bool log_rotate(size_t cap)
{
    fflush(stdout);
    fflush(stderr);
    struct stat out, err;
    if (fstat(1, &out) != 0 || !S_ISREG(out.st_mode) || (size_t)out.st_size < cap)
        return false;
    char buf[512];
    ssize_t n = readlink("/proc/self/fd/1", buf, sizeof buf - 1);
    if (n <= 0 || n >= (ssize_t)sizeof buf - 1)
        return false;
    buf[n] = 0;
    std::string path = buf;
    if (path.empty() || path[0] != '/' || path.find(" (deleted)") != std::string::npos)
        return false;
    bool both = fstat(2, &err) == 0 && err.st_dev == out.st_dev && err.st_ino == out.st_ino;
    if (rename(path.c_str(), (path + ".1").c_str()) != 0)
        return false;
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) {
        rename((path + ".1").c_str(), path.c_str());
        return false;
    }
    dup2(fd, 1);
    if (both)
        dup2(fd, 2);
    close(fd);
    return true;
}

}
