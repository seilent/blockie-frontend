#include "util.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace fd {

namespace {

struct CrcTable {
    uint32_t t[256];
};

constexpr CrcTable make_crc()
{
    CrcTable r{};
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++)
            c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        r.t[i] = c;
    }
    return r;
}

constexpr CrcTable kCrc = make_crc();

uint64_t fnv(uint64_t h, std::string_view s)
{
    for (unsigned char c : s) {
        h ^= c;
        h *= 0x100000001b3ull;
    }
    return h;
}

uint64_t mix(uint64_t h)
{
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdull;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ull;
    h ^= h >> 33;
    return h;
}

}

uint64_t hash64(std::string_view s, uint64_t salt)
{
    return mix(fnv(0xcbf29ce484222325ull ^ (salt * 0x9E3779B97F4A7C15ull), s));
}

uint64_t hash64_join(std::string_view a, char sep, std::string_view b)
{
    return mix(fnv(fnv(fnv(0xcbf29ce484222325ull, a), std::string_view(&sep, 1)), b));
}

uint32_t crc32(const void* data, size_t n, uint32_t crc)
{
    const uint8_t* p = (const uint8_t*)data;
    crc = ~crc;
    for (size_t i = 0; i < n; i++)
        crc = kCrc.t[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

uint32_t utf8_next(const char*& s, const char* end)
{
    const unsigned char* p = (const unsigned char*)s;
    uint32_t c = *p++;
    int n = 0;
    if (c >= 0xF8 || (c >= 0x80 && c < 0xC0)) {
        s = (const char*)p;
        return 0xFFFD;
    }
    if (c >= 0xF0) {
        c &= 0x07;
        n = 3;
    } else if (c >= 0xE0) {
        c &= 0x0F;
        n = 2;
    } else if (c >= 0xC0) {
        c &= 0x1F;
        n = 1;
    }
    for (; n > 0; n--) {
        if ((const char*)p >= end || (*p & 0xC0) != 0x80) {
            c = 0xFFFD;
            break;
        }
        c = (c << 6) | (*p++ & 0x3F);
    }
    s = (const char*)p;
    return c;
}

void utf8_put(std::string& out, uint32_t c)
{
    if (c < 0x80) {
        out += (char)c;
    } else if (c < 0x800) {
        out += (char)(0xC0 | c >> 6);
        out += (char)(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += (char)(0xE0 | c >> 12);
        out += (char)(0x80 | (c >> 6 & 0x3F));
        out += (char)(0x80 | (c & 0x3F));
    } else {
        out += (char)(0xF0 | c >> 18);
        out += (char)(0x80 | (c >> 12 & 0x3F));
        out += (char)(0x80 | (c >> 6 & 0x3F));
        out += (char)(0x80 | (c & 0x3F));
    }
}

std::string latin1_to_utf8(const char* s, size_t n)
{
    std::string out;
    out.reserve(n);
    for (size_t i = 0; i < n && s[i]; i++)
        utf8_put(out, (unsigned char)s[i]);
    return out;
}

std::string utf16_to_utf8(const uint8_t* s, size_t n, bool be)
{
    std::string out;
    out.reserve(n / 2);
    for (size_t i = 0; i + 1 < n; i += 2) {
        uint32_t u = be ? (s[i] << 8 | s[i + 1]) : (s[i + 1] << 8 | s[i]);
        if (u == 0)
            break;
        if (u >= 0xD800 && u < 0xDC00 && i + 3 < n) {
            uint32_t l = be ? (s[i + 2] << 8 | s[i + 3]) : (s[i + 3] << 8 | s[i + 2]);
            if (l >= 0xDC00 && l < 0xE000) {
                u = 0x10000 + ((u - 0xD800) << 10) + (l - 0xDC00);
                i += 2;
            } else {
                u = 0xFFFD;
            }
        } else if (u >= 0xD800 && u < 0xE000) {
            u = 0xFFFD;
        }
        utf8_put(out, u);
    }
    return out;
}

bool utf8_valid(const char* s, size_t n)
{
    const char* end = s + n;
    while (s < end) {
        unsigned char c = (unsigned char)*s;
        if (c < 0x80) {
            s++;
            continue;
        }
        const char* before = s;
        if (utf8_next(s, end) == 0xFFFD && !(end - before >= 3 && memcmp(before, "\xEF\xBF\xBD", 3) == 0))
            return false;
    }
    return true;
}

std::string trim(std::string_view s)
{
    size_t a = 0, b = s.size();
    while (a < b && (unsigned char)s[a] <= ' ')
        a++;
    while (b > a && (unsigned char)s[b - 1] <= ' ')
        b--;
    return std::string(s.substr(a, b - a));
}

std::string dir_of(const std::string& p)
{
    size_t k = p.rfind('/');
    if (k == std::string::npos)
        return ".";
    return k == 0 ? "/" : p.substr(0, k);
}

std::string base_of(const std::string& p)
{
    size_t k = p.rfind('/');
    return k == std::string::npos ? p : p.substr(k + 1);
}

bool fsync_dir(const std::string& dir)
{
    int fd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0)
        return false;
    bool ok = fsync(fd) == 0 || errno == EINVAL;
    ::close(fd);
    return ok;
}

bool write_atomic(const std::string& path, const std::vector<Segment>& segs, const std::string& tmp_path, mode_t mode)
{
    std::string tmp = tmp_path.empty() ? path + ".tmp" : tmp_path;
    struct stat ls;
    bool own_link = false;
    if (lstat(tmp.c_str(), &ls) == 0) {
        own_link = S_ISLNK(ls.st_mode) && ls.st_uid == geteuid();
        if (!own_link && (ls.st_uid != geteuid() || (S_ISREG(ls.st_mode) && ls.st_nlink > 1)))
            unlink(tmp.c_str());
    }
    int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | (own_link ? 0 : O_NOFOLLOW) | O_CLOEXEC, mode);
    if (fd < 0)
        return false;
    struct stat fs;
    if (fstat(fd, &fs) != 0 || (!own_link && (fs.st_uid != geteuid() || (S_ISREG(fs.st_mode) && fs.st_nlink != 1))) ||
        (S_ISREG(fs.st_mode) && ftruncate(fd, 0) != 0)) {
        ::close(fd);
        return false;
    }
    bool ok = mode == 0644 || fchmod(fd, mode) == 0;
    for (const Segment& g : segs) {
        const char* p = (const char*)g.p;
        size_t left = g.n;
        while (ok && left > 0) {
            ssize_t w = write(fd, p, left);
            if (w < 0 && errno == EINTR)
                continue;
            if (w <= 0) {
                ok = false;
                break;
            }
            p += w;
            left -= (size_t)w;
        }
    }
    ok = ok && fsync(fd) == 0;
    ok = ::close(fd) == 0 && ok;
    if (!ok || rename(tmp.c_str(), path.c_str()) != 0) {
        unlink(tmp.c_str());
        return false;
    }
    return fsync_dir(dir_of(path));
}

bool write_atomic(const std::string& path, const void* data, size_t n, const std::string& tmp_path, mode_t mode)
{
    return write_atomic(path, std::vector<Segment>{{data, n}}, tmp_path, mode);
}

bool read_file(const std::string& path, std::vector<uint8_t>& out, size_t cap)
{
    out.clear();
    int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return false;
    struct stat st;
    if (fstat(fd, &st) || st.st_size < 0 || (size_t)st.st_size > cap) {
        ::close(fd);
        return false;
    }
    out.resize((size_t)st.st_size);
    size_t got = 0;
    while (got < out.size()) {
        ssize_t r = pread(fd, out.data() + got, out.size() - got, (off_t)got);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            break;
        got += (size_t)r;
    }
    ::close(fd);
    if (got != out.size()) {
        out.clear();
        return false;
    }
    return true;
}

int read_or_tmp(const std::string& path, std::vector<uint8_t>& out, size_t cap)
{
    out.clear();
    struct stat st;
    int found = 1;
    for (const std::string& p : {path, path + ".tmp"}) {
        if (stat(p.c_str(), &st) == 0) {
            if (st.st_size < 0 || (size_t)st.st_size > cap)
                return -EFBIG;
            errno = 0;
            return read_file(p, out, cap) ? found : -(errno ? errno : EIO);
        }
        if (errno != ENOENT && errno != ENOTDIR)
            return -errno;
        found++;
    }
    return 0;
}

unsigned leftovers(const std::string& path)
{
    unsigned left = 0;
    if (access((path + ".tmp").c_str(), F_OK) == 0)
        left |= 1;
    if (access((path + ".new").c_str(), F_OK) == 0)
        left |= 2;
    return left;
}

bool mkdirs(const std::string& path)
{
    if (path.empty())
        return false;
    struct stat st;
    if (stat(path.c_str(), &st) == 0)
        return S_ISDIR(st.st_mode);
    std::string parent = dir_of(path);
    if (parent != path && !mkdirs(parent))
        return false;
    return mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
}

pid_t self_tid() { return (pid_t)syscall(SYS_gettid); }

void lower_priority(int nice_value, bool io_idle)
{
    pid_t tid = self_tid();
    setpriority(PRIO_PROCESS, (id_t)tid, nice_value);
    if (io_idle)
        syscall(SYS_ioprio_set, 1, (int)tid, 3 << 13);
}

long anon_kb(const std::string& proc)
{
    FILE* f = fopen((proc + "/status").c_str(), "r");
    if (!f)
        return -1;
    char line[256];
    long kb = -1;
    while (fgets(line, sizeof line, f))
        if (sscanf(line, "RssAnon: %ld", &kb) == 1)
            break;
    fclose(f);
    if (kb >= 0)
        return kb;
    f = fopen((proc + "/smaps").c_str(), "r");
    if (f) {
        long sum = 0, v;
        while (fgets(line, sizeof line, f))
            if (sscanf(line, "Anonymous: %ld", &v) == 1)
                sum += v;
        fclose(f);
        return sum;
    }
    f = fopen((proc + "/statm").c_str(), "r");
    if (!f)
        return -1;
    long size, resident, shared;
    int n = fscanf(f, "%ld %ld %ld", &size, &resident, &shared);
    fclose(f);
    if (n != 3 || resident < shared)
        return -1;
    return (resident - shared) * (sysconf(_SC_PAGESIZE) / 1024);
}

}
