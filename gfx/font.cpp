#include "font.h"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <pthread.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include "stb_truetype.h"

#include <algorithm>

#include "../board/board.h"
#include "../core/log.h"
#include "../core/press.h"
#include "../core/prof.h"
#include "../lib/util.h"
#include "fontkey.h"
#include "shape.h"

namespace fd {

using namespace fontkey;

namespace {

constexpr size_t kJobCap = 256;
constexpr size_t kJobGrow = 256;
constexpr int kDirect = 0xFE;
constexpr int kWarmNice = 10;
constexpr int kPuts = 96;
constexpr unsigned long kSquashfsMagic = 0x73717368;
constexpr uint64_t kCopyBlock = 131072;

const char* kFaceFiles[Fonts::kPrimFaces] = {"selawkl.ttf", "selawk.ttf", "selawkb.ttf", "selawksl.ttf"};
static_assert((int)Weight::SemiLight + 1 == Fonts::kPrimFaces);

uint32_t next_cp(const char*& s)
{
    const unsigned char* p = (const unsigned char*)s;
    uint32_t c = *p++;
    int n = 0;
    if (c >= 0xF8) {
        c = 0xFFFD;
    } else if (c >= 0xF0) {
        c &= 0x07;
        n = 3;
    } else if (c >= 0xE0) {
        c &= 0x0F;
        n = 2;
    } else if (c >= 0xC0) {
        c &= 0x1F;
        n = 1;
    } else if (c >= 0x80) {
        c = 0xFFFD;
    }
    for (; n > 0; n--) {
        if ((*p & 0xC0) != 0x80) {
            c = 0xFFFD;
            break;
        }
        c = (c << 6) | (*p++ & 0x3F);
    }
    s = (const char*)p;
    return c;
}

int key_face(uint64_t k) { return (int)((k >> 40) & 0xFFFF); }
int key_px(uint64_t k) { return (int)((k >> 24) & 0xFFFF); }
int key_gid(uint64_t k) { return (int)(k & 0xFFFFFF); }

bool printable(const char* s)
{
    for (; *s; s++)
        if ((unsigned char)*s < 0x20 || (unsigned char)*s > 0x7E)
            return false;
    return true;
}

int64_t mono_ms()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int64_t mono_us()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

bool squashfs(const std::string& path)
{
    struct statfs sf;
    return statfs(path.c_str(), &sf) == 0 && (unsigned long)sf.f_type == kSquashfsMagic;
}

uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

}

Fonts::Fonts()
{
    plain_.cap = board::kMem.glyphs;
    scaled_.cap = board::kMem.glyphs_scaled;
}

Fonts::~Fonts()
{
    if (th_.joinable()) {
        {
            std::lock_guard<std::mutex> lk(mu_);
            quit_ = true;
        }
        cv_.notify_all();
        th_.join();
    }
    if (copy_.src >= 0)
        close(copy_.src);
    if (efd_ >= 0)
        close(efd_);
    drop_hb();
    auto drop = [](Face& f) {
        delete f.info;
        if (f.map)
            munmap(f.map, f.len);
    };
    for (Face& f : prim_)
        drop(f);
    for (Face& f : fall_)
        drop(f);
}

bool Fonts::load(Face& f, bool mapped)
{
    int fd = ::open(f.path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return false;
    struct stat st;
    if (fstat(fd, &st) || st.st_size <= 0) {
        ::close(fd);
        return false;
    }
    const uint8_t* bytes = nullptr;
    if (mapped) {
        void* m = mmap(nullptr, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (m != MAP_FAILED) {
            f.map = m;
            f.len = (size_t)st.st_size;
            bytes = (const uint8_t*)m;
        }
    } else {
        f.data.resize((size_t)st.st_size);
        if (read(fd, f.data.data(), f.data.size()) == (ssize_t)f.data.size())
            bytes = f.data.data();
    }
    ::close(fd);
    if (!bytes)
        return false;
    int off = stbtt_GetFontOffsetForIndex(bytes, 0);
    if (off < 0) {
        if (f.map)
            munmap(f.map, f.len);
        f.map = nullptr;
        f.len = 0;
        return false;
    }
    f.info = new stbtt_fontinfo;
    if (!stbtt_InitFont(f.info, bytes, off)) {
        delete f.info;
        f.info = nullptr;
        return false;
    }
    const uint8_t* head = f.info->data + f.info->head;
    f.upem = (head[18] << 8) | head[19];
    if (f.upem <= 0) {
        delete f.info;
        f.info = nullptr;
        return false;
    }
    int x0, y0, x1, y1;
    stbtt_GetFontBoundingBox(f.info, &x0, &y0, &x1, &y1);
    f.top = std::max(y1, 0);
    f.bottom = std::max(-y0, 0);
    return true;
}

bool Fonts::load_face(Face& f, bool worker)
{
    {
        std::lock_guard<std::mutex> lk(face_mu_);
        if (f.state.load(std::memory_order_relaxed) != 0)
            return false;
        f.state.store(2, std::memory_order_relaxed);
    }
    if (worker && gate_)
        gate_(0);
    bool ok = false;
    if (!f.image.empty()) {
        {
            std::lock_guard<std::mutex> lk(copy_mu_);
            ok = load(f, true);
            copy_.mapped = ok;
        }
        if (!ok) {
            if (f.map)
                munmap(f.map, f.len);
            f.map = nullptr;
            f.len = 0;
            fprintf(stderr, "funeos: font copy %s unavailable, %s used\n", f.path.c_str(), f.image.c_str());
            f.path = f.image;
        }
        f.copy = ok;
    }
    if (!ok)
        ok = load(f, true);
    if (ok)
        lock_face(f);
    else
        fprintf(stderr, "funeos: fallback font %s unavailable\n", f.path.c_str());
    if (ok && f.ranged) {
        f.box.assign((size_t)f.info->numGlyphs * 4, 0);
        for (int g = 0; g < f.info->numGlyphs; g++) {
            int x0, y0, x1, y1;
            if (!stbtt_GetGlyphBox(f.info, g, &x0, &y0, &x1, &y1))
                continue;
            int16_t* b = &f.box[(size_t)g * 4];
            b[0] = (int16_t)x0;
            b[1] = (int16_t)y0;
            b[2] = (int16_t)x1;
            b[3] = (int16_t)y1;
        }
        log_info("fonts: %s loaded\n", base_of(f.path).c_str());
    }
    {
        std::lock_guard<std::mutex> lk(face_mu_);
        f.state.store(ok ? 1 : -1, std::memory_order_release);
    }
    face_cv_.notify_all();
    return ok;
}

void Fonts::wait_face(Face& f)
{
    waits_++;
    boost();
    std::unique_lock<std::mutex> lk(face_mu_);
    face_cv_.wait(lk, [&] { return f.state.load(std::memory_order_acquire) != 2; });
}

std::vector<std::pair<size_t, size_t>> Fonts::lock_ranges(const uint8_t* d, size_t len, bool shaped)
{
    std::vector<std::pair<size_t, size_t>> out;
    if (len < 12)
        return out;
    size_t base = be32(d) == 0x74746366 && len >= 16 ? be32(d + 12) : 0;
    if (base + 12 > len)
        return out;
    size_t n = ((size_t)d[base + 4] << 8) | d[base + 5];
    for (size_t i = 0; i < n && base + 12 + 16 * (i + 1) <= len; i++) {
        const uint8_t* r = d + base + 12 + 16 * i;
        uint32_t tag = be32(r);
        size_t off = be32(r + 8), size = be32(r + 12);
        bool per_draw = tag == 0x636D6170 || tag == 0x68686561 || tag == 0x686D7478 || tag == 0x6B65726E ||
                        tag == 0x47504F53 || (shaped && (tag == 0x47535542 || tag == 0x47444546));
        if (per_draw && size > 0 && off <= len && size <= len - off)
            out.push_back({off, size});
    }
    return out;
}

void Fonts::lock_face(Face& f)
{
    if (!f.map || f.copy || !(rootfs_ ? rootfs_(f.path) : squashfs(f.path)))
        return;
    static std::atomic<bool> told{false};
    for (const std::pair<size_t, size_t>& r : lock_ranges((const uint8_t*)f.map, f.len, f.ranged)) {
        lock_tries_++;
        uint8_t* at = (uint8_t*)f.map + r.first;
        if (mlock(at, r.second) == 0)
            continue;
        int e = errno;
        munlock(at, r.second);
        if (!told.exchange(true))
            fprintf(stderr, "funeos: font lock of %s failed: %s\n", f.path.c_str(), strerror(e));
    }
}

bool Fonts::open(const std::string& dir, const std::vector<std::string>& fallback)
{
    bool ok = true;
    for (int i = 0; i < kPrimFaces; i++) {
        prim_[i].path = dir + "/" + kFaceFiles[i];
        bool got = load(prim_[i], false);
        prim_[i].state.store(got ? 1 : -1, std::memory_order_release);
        if (got)
            continue;
        if (i == (int)Weight::SemiLight) {
            prim_at_[i] = (int)Weight::Light;
            fprintf(stderr, "funeos: font %s missing, %s used\n", prim_[i].path.c_str(), kFaceFiles[prim_at_[i]]);
        } else {
            fprintf(stderr, "funeos: font %s missing\n", prim_[i].path.c_str());
            ok = false;
        }
    }
    for (const std::string& p : fallback) {
        fall_.emplace_back();
        fall_.back().path = p;
    }
    return ok;
}

bool Fonts::open(const std::string& dir, const std::vector<std::string>& fallback, const std::vector<RangedFace>& ranged)
{
    bool ok = open(dir, fallback);
    for (const RangedFace& r : ranged) {
        if (access(r.path.c_str(), F_OK) != 0)
            continue;
        Face probe;
        probe.path = r.path;
        bool good = load(probe, true);
        delete probe.info;
        if (probe.map)
            munmap(probe.map, probe.len);
        if (!good) {
            fprintf(stderr, "funeos: fallback font %s unavailable\n", r.path.c_str());
            continue;
        }
        fall_.emplace_back();
        Face& f = fall_.back();
        f.path = r.path;
        f.ranges = r.ranges;
        f.script = r.script;
        f.ranged = true;
        f.joined = r.joined;
    }
    return ok;
}

void Fonts::use_copy(const std::string& image, const std::string& dir, const std::string& build, bool crashed)
{
    std::lock_guard<std::mutex> lk(copy_mu_);
    Copy& c = copy_;
    c.image = image;
    c.dir = dir;
    c.file = dir + "/" + base_of(image);
    c.stamp = c.file + ".src";
    c.tmp = c.file + ".tmp";
    c.build = build;
    struct stat st;
    c.size = stat(image.c_str(), &st) == 0 && st.st_size > 0 ? (uint64_t)st.st_size : 0;
    std::vector<uint8_t> have;
    std::string want = build + " " + std::to_string(c.size) + "\n";
    bool valid = c.size > 0 && read_file(c.stamp, have, 256) && std::string(have.begin(), have.end()) == want &&
                 stat(c.file.c_str(), &st) == 0 && (uint64_t)st.st_size == c.size;
    bool gone = unlink(c.tmp.c_str()) == 0;
    gone = unlink((c.stamp + ".tmp").c_str()) == 0 || gone;
    if (crashed || !valid) {
        bool had = unlink(c.file.c_str()) == 0;
        gone = unlink(c.stamp.c_str()) == 0 || had || gone;
        if (had && crashed)
            fprintf(stderr, "funeos: font copy %s removed after an early exit\n", c.file.c_str());
    }
    if (gone)
        fsync_dir(dir);
    Face* f = nullptr;
    for (Face& x : fall_)
        if (x.path == image)
            f = &x;
    if (!f || !th_.joinable() || crashed || c.size == 0)
        return;
    if (valid) {
        f->image = image;
        f->path = c.file;
        return;
    }
    std::lock_guard<std::mutex> wl(mu_);
    c.want = true;
    c.pass = true;
    cv_.notify_one();
}

uint64_t Fonts::drop_copy()
{
    std::lock_guard<std::mutex> lk(copy_mu_);
    Copy& c = copy_;
    c.stop = true;
    if (c.file.empty())
        return 0;
    uint64_t freed = 0;
    bool any = false;
    for (const std::string& p : {c.file, c.stamp, c.tmp, c.stamp + ".tmp"}) {
        struct stat st;
        if (stat(p.c_str(), &st) != 0 || unlink(p.c_str()) != 0)
            continue;
        any = true;
        if (p != c.file || !c.mapped)
            freed += (uint64_t)st.st_size;
    }
    if (any) {
        fsync_dir(c.dir);
        fprintf(stderr, "funeos: font copy removed for space, %llu kB freed%s\n", (unsigned long long)(freed >> 10),
                c.mapped ? ", the mapped copy frees its space at the next start" : "");
    }
    return freed;
}

void Fonts::set_copy_need(std::function<uint64_t(const std::string& card)> f)
{
    std::lock_guard<std::mutex> lk(copy_mu_);
    copy_need_ = std::move(f);
}

void Fonts::set_copy_card(const std::string& dir)
{
    std::lock_guard<std::mutex> lk(mu_);
    copy_.card = dir;
    if (!copy_.known) {
        copy_.known = true;
        cv_.notify_one();
    }
}

void Fonts::set_copy_hold(bool hold)
{
    if (!th_.joinable())
        return;
    std::lock_guard<std::mutex> lk(mu_);
    if (hold == copy_.hold)
        return;
    copy_.hold = hold;
    if (!hold)
        cv_.notify_one();
}

bool Fonts::copying() const
{
    std::lock_guard<std::mutex> lk(mu_);
    return copy_.want;
}

bool Fonts::start_worker()
{
    if (th_.joinable())
        return true;
    efd_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (efd_ < 0)
        return false;
    th_ = std::thread([this] { run(); });
    pthread_setname_np(th_.native_handle(), "fd-font");
    return true;
}

Fonts::Face& Fonts::face(int i) { return i < kPrimFaces ? prim_[i] : fall_[(size_t)(i - kPrimFaces)]; }

bool Fonts::resolve(Weight w, uint32_t cp, int& fi, int& gid, bool ranged)
{
    int p = prim_at_[(int)w];
    if (prim_[p].state.load(std::memory_order_acquire) == 1) {
        gid = stbtt_FindGlyphIndex(prim_[p].info, (int)cp);
        if (gid) {
            fi = p;
            return true;
        }
    }
    for (size_t i = 0; i < fall_.size(); i++) {
        Face& f = fall_[i];
        if (f.ranged && (!ranged || !in_ranges(f, cp)))
            continue;
        int s = f.state.load(std::memory_order_acquire);
        if (s == 0) {
            load_face(f, false);
            s = f.state.load(std::memory_order_acquire);
        }
        if (s == 2) {
            wait_face(f);
            s = f.state.load(std::memory_order_acquire);
        }
        if (s != 1)
            continue;
        gid = stbtt_FindGlyphIndex(f.info, (int)cp);
        if (gid) {
            fi = kPrimFaces + (int)i;
            return true;
        }
    }
    fi = prim_[p].state.load(std::memory_order_acquire) == 1 ? p : -1;
    gid = 0;
    return false;
}

bool Fonts::resolve_ready(Weight w, uint32_t cp, int& fi, int& gid, bool ranged)
{
    int p = prim_at_[(int)w];
    fi = -1;
    gid = 0;
    if (prim_[p].state.load(std::memory_order_acquire) == 1) {
        gid = stbtt_FindGlyphIndex(prim_[p].info, (int)cp);
        if (gid) {
            fi = p;
            return true;
        }
    }
    for (size_t i = 0; i < fall_.size(); i++) {
        Face& f = fall_[i];
        if (f.ranged && (!ranged || !in_ranges(f, cp)))
            continue;
        int s = f.state.load(std::memory_order_acquire);
        if (s == 0 || s == 2)
            return false;
        if (s != 1)
            continue;
        gid = stbtt_FindGlyphIndex(f.info, (int)cp);
        if (gid) {
            fi = kPrimFaces + (int)i;
            return true;
        }
    }
    fi = prim_[p].state.load(std::memory_order_acquire) == 1 ? p : -1;
    gid = 0;
    return true;
}

Fonts::Glyph* Fonts::find(Cache& c, uint64_t key)
{
    auto it = c.map.find(key);
    if (it == c.map.end())
        return nullptr;
    c.lru.splice(c.lru.begin(), c.lru, it->second.lru);
    it->second.seen = frame_;
    return &it->second;
}

float Fonts::factor(int px, int b)
{
    int n = press_buckets(px);
    return 1.0f - (float)(kOne - tok::motion_pressScale) / kOne * (float)b / (float)n;
}

Fonts::Glyph Fonts::make(int fi, int px, int gid, float factor)
{
    Face& f = face(fi);
    float sc = stbtt_ScaleForMappingEmToPixels(f.info, (float)px) * factor;
    int x0, y0, x1, y1;
    stbtt_GetGlyphBitmapBox(f.info, gid, sc, sc, &x0, &y0, &x1, &y1);
    Glyph g;
    g.w = x1 - x0;
    g.h = y1 - y0;
    g.xoff = x0;
    g.yoff = y0;
    if (g.w > 0 && g.h > 0) {
        std::vector<uint8_t> tmp((size_t)g.w * g.h);
        stbtt_MakeGlyphBitmap(f.info, tmp.data(), g.w, g.h, g.w, sc, sc, gid);
        g.rot.resize(tmp.size());
        for (int y = 0; y < g.h; y++)
            for (int x = 0; x < g.w; x++)
                g.rot[LiveL::index(g.w, g.h, x, y)] = tmp[(size_t)y * g.w + x];
    } else {
        g.w = g.h = 0;
    }
    return g;
}

GlyphRaster Fonts::direct(int fi, int px, int gid)
{
    direct_rasters_++;
    Glyph g = make(fi, px, gid, 1.0f);
    return {g.w, g.h, g.xoff, g.yoff, std::move(g.rot)};
}

bool Fonts::glyph_box(int fi, int px, int gid, Rect& box)
{
    Face& f = face(fi);
    if (f.state.load(std::memory_order_acquire) != 1)
        return false;
    float sc = stbtt_ScaleForMappingEmToPixels(f.info, (float)px);
    stbtt_GetGlyphBitmapBox(f.info, gid, sc, sc, &box.x0, &box.y0, &box.x1, &box.y1);
    if (box.x1 <= box.x0 || box.y1 <= box.y0)
        box = {};
    return true;
}

void Fonts::ask_direct(int fi, int px, int gid)
{
    if (th_.joinable())
        ask(key_of(kDirect, fi, px, gid), Need::Predicted, false, false, -1, -1);
}

bool Fonts::take_direct(int fi, int px, int gid, GlyphRaster& out)
{
    if (!th_.joinable())
        return false;
    uint64_t key = key_of(kDirect, fi, px, gid);
    std::lock_guard<std::mutex> lk(mu_);
    for (size_t i = 0; i < direct_done_.size(); i++)
        if (direct_done_[i].job.key == key) {
            Glyph& g = direct_done_[i].g;
            out = {g.w, g.h, g.xoff, g.yoff, std::move(g.rot)};
            direct_done_.erase(direct_done_.begin() + (long)i);
            return true;
        }
    return false;
}

void Fonts::drop_direct(int fi, int px, int gid)
{
    if (!th_.joinable())
        return;
    uint64_t key = key_of(kDirect, fi, px, gid);
    std::lock_guard<std::mutex> lk(mu_);
    auto is = [key](const auto& v) { return v.key == key; };
    jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(), is), jobs_.end());
    direct_done_.erase(
        std::remove_if(direct_done_.begin(), direct_done_.end(), [key](const Done& d) { return d.job.key == key; }),
        direct_done_.end());
    if (busy_on_ && busy_.key == key)
        busy_.dropped = true;
}

size_t Fonts::direct_held() const
{
    std::lock_guard<std::mutex> lk(mu_);
    size_t n = 0;
    for (const Done& d : direct_done_)
        n += d.g.rot.size();
    return n;
}

Fonts::Glyph* Fonts::insert(Cache& c, uint64_t key, Glyph&& g, bool any)
{
    auto at = c.map.find(key);
    if (at != c.map.end())
        return &at->second;
    size_t cost = g.rot.size() + 64;
    if (!any && c.bytes + cost > c.cap) {
        if (c.dry_cost && cost >= c.dry_cost && c.dry_mark == mark_ && c.dry_rev == c.rev)
            return nullptr;
        size_t free = c.cap > c.bytes ? c.cap - c.bytes : 0;
        drop_.clear();
        for (auto it = c.lru.end(); free < cost && it != c.lru.begin();) {
            --it;
            walked_++;
            const Glyph& o = c.map.find(*it)->second;
            if (o.seen >= mark_)
                continue;
            free += o.rot.size() + 64;
            drop_.push_back(it);
        }
        if (free < cost) {
            c.dry_mark = mark_;
            c.dry_rev = c.rev;
            c.dry_cost = cost;
            return nullptr;
        }
        for (auto it : drop_) {
            auto old = c.map.find(*it);
            c.bytes -= old->second.rot.size() + 64;
            c.map.erase(old);
            c.lru.erase(it);
            c.rev++;
        }
    }
    while (c.bytes + cost > c.cap && !c.lru.empty()) {
        auto old = c.map.find(c.lru.back());
        c.bytes -= old->second.rot.size() + 64;
        c.map.erase(old);
        c.lru.pop_back();
        c.rev++;
    }
    c.rev++;
    c.lru.push_front(key);
    g.lru = c.lru.begin();
    g.seen = any ? frame_ : mark_ - 1;
    c.bytes += cost;
    return &c.map.emplace(key, std::move(g)).first->second;
}

Fonts::Glyph* Fonts::raster(Cache& c, uint64_t key, int fi, int px, int gid, float factor)
{
    rasters_++;
    if (fi >= kPrimFaces)
        fallback_rasters_++;
    return insert(c, key, make(fi, px, gid, factor), true);
}

Fonts::Glyph* Fonts::claim(Cache& c, uint64_t key, int fi, int px, int gid, float factor)
{
    if (!th_.joinable())
        return raster(c, key, fi, px, gid, factor);
    bool owed = false;
    {
        std::unique_lock<std::mutex> lk(mu_);
        if (busy_on_ && busy_.key == key) {
            waits_++;
            busy_nice_ = false;
            boost();
            done_cv_.wait(lk, [&] { return !(busy_on_ && busy_.key == key); });
        }
        for (size_t i = 0; i < done_.size(); i++)
            if (done_[i].job.key == key) {
                Done d = std::move(done_[i]);
                done_.erase(done_.begin() + (long)i);
                lk.unlock();
                if (d.job.owed) {
                    owed_in_ = true;
                    signal();
                }
                return insert(c, key, std::move(d.g), true);
            }
        for (size_t i = 0; i < jobs_.size(); i++)
            if (jobs_[i].key == key) {
                owed = jobs_[i].owed;
                jobs_.erase(jobs_.begin() + (long)i);
                break;
            }
    }
    Glyph* g = raster(c, key, fi, px, gid, factor);
    if (owed) {
        owed_in_ = true;
        signal();
    }
    return g;
}

Fonts::Glyph* Fonts::glyph(int fi, int px, int gid, int x, int y, bool& pending, bool& miss)
{
    uint64_t key = key_of(0, fi, px, gid);
    if (Glyph* g = find(plain_, key))
        return g;
    if (fi >= kPrimFaces)
        misses_++;
    if (th_.joinable() && fi >= kPrimFaces && budget_ >= 0) {
        ask(key, need_, need_ == Need::Visible, false, x, y);
        miss = true;
        return nullptr;
    }
    if (budget_ == 0) {
        pending = true;
        deferrals_++;
        return nullptr;
    }
    if (budget_ > 0)
        budget_--;
    return claim(plain_, key, fi, px, gid, 1.0f);
}

int Fonts::press_buckets(int px)
{
    int64_t span = (int64_t)px * (kOne - tok::motion_pressScale) * 4;
    return std::max(1, (int)((span + kOne - 1) / kOne));
}

int Fonts::press_bucket(int px, fx s)
{
    int n = press_buckets(px);
    return (press_step(s) * n + kPressSteps / 2) / kPressSteps;
}

Fonts::Glyph* Fonts::scaled(int fi, int px, int gid, int b, int x, int y, bool& pending, bool& miss, bool& subst,
                           uint64_t& got)
{
    int n = press_buckets(px);
    auto key = [&](int k) { return key_of(k, fi, px, gid); };
    got = key(b);
    if (Glyph* g = find(scaled_, got))
        return g;
    if (fi >= kPrimFaces)
        misses_++;
    bool later = th_.joinable() && fi >= kPrimFaces;
    if (later) {
        bool whole = budget_ < 0;
        ask(got, whole ? Need::Visible : need_, whole || need_ == Need::Visible, whole, x, y);
        subst = true;
    } else {
        if (b != n && press_budget_ != 0 && !find(scaled_, key(n))) {
            if (press_budget_ > 0)
                press_budget_--;
            claim(scaled_, key(n), fi, px, gid, factor(px, n));
        }
        if (press_budget_ != 0) {
            if (press_budget_ > 0)
                press_budget_--;
            return claim(scaled_, got, fi, px, gid, factor(px, b));
        }
        pending = true;
        deferrals_++;
    }
    for (int d = 1; d <= n; d++) {
        if (b - d >= 1)
            if (Glyph* g = find(scaled_, got = key(b - d)))
                return g;
        if (b + d <= n)
            if (Glyph* g = find(scaled_, got = key(b + d)))
                return g;
    }
    got = key(0);
    if (later)
        return glyph(fi, px, gid, x, y, pending, miss);
    bool more = false, lost = false;
    return glyph(fi, px, gid, x, y, more, lost);
}

int Fonts::width64(const TextStyle& st, const char* s)
{
    if (runs_ && !printable(s) && !bidi_plain(s, Dir::Auto)) {
        std::shared_ptr<const TextLayout> l = find_layout(st, s, Dir::Auto, false, 0);
        return run(st, *l, 0, l->text.size()).width64;
    }
    int pen = 0, prev_face = -1, prev_gid = 0;
    while (*s) {
        uint32_t cp = apply_case(next_cp(s), st.tcase);
        if (bidi_ignorable(cp))
            continue;
        int fi, gid;
        resolve(st.weight, cp, fi, gid);
        if (fi < 0)
            continue;
        Face& f = face(fi);
        if (fi == prev_face)
            pen += scale64(stbtt_GetGlyphKernAdvance(f.info, prev_gid, gid), st.px, f.upem);
        int adv, lsb;
        stbtt_GetGlyphHMetrics(f.info, gid, &adv, &lsb);
        pen += scale64(adv, st.px, f.upem) + st.track64;
        prev_face = fi;
        prev_gid = gid;
    }
    return pen;
}

void Fonts::ink64(const TextStyle& st, const char* s, int& left64, int& right64)
{
    left64 = right64 = 0;
    if (runs_ && !printable(s) && !bidi_plain(s, Dir::Auto)) {
        right64 = width64(st, s);
        return;
    }
    bool any = false;
    int pen = 0, prev_face = -1, prev_gid = 0;
    while (*s) {
        uint32_t cp = apply_case(next_cp(s), st.tcase);
        if (bidi_ignorable(cp))
            continue;
        int fi, gid;
        resolve(st.weight, cp, fi, gid);
        if (fi < 0)
            continue;
        Face& f = face(fi);
        if (fi == prev_face)
            pen += scale64(stbtt_GetGlyphKernAdvance(f.info, prev_gid, gid), st.px, f.upem);
        int x0, y0, x1, y1;
        if (stbtt_GetGlyphBox(f.info, gid, &x0, &y0, &x1, &y1)) {
            int l = pen + scale64(x0, st.px, f.upem), r = pen + scale64(x1, st.px, f.upem);
            left64 = any ? std::min(left64, l) : l;
            right64 = any ? std::max(right64, r) : r;
            any = true;
        }
        int adv, lsb;
        stbtt_GetGlyphHMetrics(f.info, gid, &adv, &lsb);
        pen += scale64(adv, st.px, f.upem) + st.track64;
        prev_face = fi;
        prev_gid = gid;
    }
    if (!any)
        right64 = pen;
}

bool Fonts::draw(Canvas& c, const TextStyle& st, const char* s, int x64, int baseline, uint32_t rgb, int a)
{
#ifndef FD_ALSA
    if (draws_)
        draws_->push_back({x64, baseline, width64(st, s), st.px, c.clip()});
#endif
    if (runs_ && !printable(s) && !bidi_plain(s, Dir::Auto))
        return draw(c, run(st, s, Dir::Auto), x64, baseline, rgb, a);
    prof::Scope ps(prof::Glyph);
    bool hold = th_.joinable() && budget_ >= 0 && a > 0 && !printable(s);
    Put puts[kPuts];
    int np = 0;
    bool complete = true, miss = false;
    int pen = x64, prev_face = -1, prev_gid = 0;
    Rect clip = c.clip();
    if (th_.joinable() && outside(clip, baseline, st, s))
        return true;
    int right = (clip.x1 + st.px) * 64, ay = inside(baseline - st.px / 2, clip.y0, clip.y1);
    while (*s && pen < right) {
        uint32_t cp = apply_case(next_cp(s), st.tcase);
        if (bidi_ignorable(cp))
            continue;
        int fi, gid;
        resolve(st.weight, cp, fi, gid);
        if (fi < 0)
            continue;
        Face& f = face(fi);
        if (fi == prev_face)
            pen += scale64(stbtt_GetGlyphKernAdvance(f.info, prev_gid, gid), st.px, f.upem);
        bool pending = false;
        int ax = inside((pen + 32) >> 6, clip.x0, clip.x1);
        Glyph* g = a > 0 ? glyph(fi, st.px, gid, ax, ay, pending, miss) : nullptr;
        if (pending)
            complete = false;
        if (g && g->w > 0) {
            int gx = ((pen + 32) >> 6) + g->xoff, gy = baseline + g->yoff;
            if (hold && np < kPuts)
                puts[np++] = {key_of(0, fi, st.px, gid), gx, gy};
            else
                c.coverage(g->rot.data(), g->w, g->h, gx, gy, rgb, a);
        }
        int adv, lsb;
        stbtt_GetGlyphHMetrics(f.info, gid, &adv, &lsb);
        pen += scale64(adv, st.px, f.upem) + st.track64;
        prev_face = fi;
        prev_gid = gid;
    }
    for (int i = 0; i < np && !miss && complete; i++) {
        auto it = plain_.map.find(puts[i].key);
        if (it != plain_.map.end() && it->second.w > 0)
            c.coverage(it->second.rot.data(), it->second.w, it->second.h, puts[i].x, puts[i].y, rgb, a);
    }
    if (miss && need_ == Need::Visible)
        owed_epoch_++;
    if (a > 0)
        note(clip, x64 >> 6, pen >> 6, baseline, st.px);
    return complete;
}

bool Fonts::draw(Canvas& c, const TextStyle& st, const char* s, int x64, int baseline, uint32_t rgb, int a, const Zoom& z)
{
    if (!z.on())
        return draw(c, st, s, x64, baseline, rgb, a);
#ifndef FD_ALSA
    if (draws_)
        draws_->push_back({x64, baseline, width64(st, s), st.px, c.clip()});
#endif
    if (runs_ && !printable(s) && !bidi_plain(s, Dir::Auto))
        return draw(c, run(st, s, Dir::Auto), x64, baseline, rgb, a, z);
    prof::Scope ps(prof::Glyph);
    bool hold = th_.joinable() && budget_ >= 0 && a > 0 && !printable(s);
    Put puts[kPuts];
    int np = 0;
    bool complete = true, miss = false, subst = false, gap = false;
    int b = press_bucket(st.px, z.s);
    int base = (z.y(baseline * 64) + 32) >> 6;
    int pen = x64, prev_face = -1, prev_gid = 0;
    Rect clip = c.clip();
    if (th_.joinable() && z.s <= kOne && outside(clip, base, st, s))
        return true;
    int right = (clip.x1 + st.px) * 64, ay = inside(base - st.px / 2, clip.y0, clip.y1);
    while (*s && z.x(pen) < right) {
        uint32_t cp = apply_case(next_cp(s), st.tcase);
        if (bidi_ignorable(cp))
            continue;
        int fi, gid;
        resolve(st.weight, cp, fi, gid);
        if (fi < 0)
            continue;
        Face& f = face(fi);
        if (fi == prev_face)
            pen += scale64(stbtt_GetGlyphKernAdvance(f.info, prev_gid, gid), st.px, f.upem);
        bool pending = false;
        uint64_t got = key_of(0, fi, st.px, gid);
        int ax = inside((z.x(pen) + 32) >> 6, clip.x0, clip.x1);
        Glyph* g = a > 0 ? b ? scaled(fi, st.px, gid, b, ax, ay, pending, miss, subst, got)
                             : glyph(fi, st.px, gid, ax, ay, pending, miss)
                         : nullptr;
        if (pending)
            complete = false;
        if (pending && !g)
            gap = true;
        if (g && g->w > 0) {
            int gx = ((z.x(pen) + 32) >> 6) + g->xoff, gy = base + g->yoff;
            if (hold && np < kPuts)
                puts[np++] = {got, gx, gy};
            else
                c.coverage(g->rot.data(), g->w, g->h, gx, gy, rgb, a);
        }
        int adv, lsb;
        stbtt_GetGlyphHMetrics(f.info, gid, &adv, &lsb);
        pen += scale64(adv, st.px, f.upem) + st.track64;
        prev_face = fi;
        prev_gid = gid;
    }
    for (int i = 0; i < np && !miss && !gap; i++) {
        Cache& cache = key_bucket(puts[i].key) ? scaled_ : plain_;
        auto it = cache.map.find(puts[i].key);
        if (it != cache.map.end() && it->second.w > 0)
            c.coverage(it->second.rot.data(), it->second.w, it->second.h, puts[i].x, puts[i].y, rgb, a);
    }
    if ((miss && need_ == Need::Visible) || (subst && (budget_ < 0 || need_ == Need::Visible)))
        owed_epoch_++;
    if (a > 0)
        note(clip, z.x(x64) >> 6, z.x(pen) >> 6, base, st.px);
    return complete;
}

bool Fonts::outside(const Rect& clip, int baseline, const TextStyle& st, const char* s) const
{
    int px = st.px, up = 0, down = 0;
    auto take = [&](const Face& f) {
        up = std::max(up, (f.top * px + f.upem - 1) / f.upem + 1);
        down = std::max(down, (f.bottom * px + f.upem - 1) / f.upem + 1);
    };
    for (const Face& f : prim_)
        if (f.state.load(std::memory_order_acquire) == 1)
            take(f);
    bool ascii = printable(s) && prim_[prim_at_[(int)st.weight]].state.load(std::memory_order_acquire) == 1;
    for (const Face& f : fall_) {
        if (f.ranged)
            continue;
        int state = f.state.load(std::memory_order_acquire);
        if (state == 1)
            take(f);
        else if (state != -1 && !ascii)
            return false;
    }
    return baseline + down <= clip.y0 || baseline - up >= clip.y1;
}

void Fonts::note(const Rect& clip, int x0, int x1, int baseline, int px)
{
    if (!th_.joinable() || budget_ < 0 || need_ != Need::Visible)
        return;
    Rect r = Rect{x0, baseline - px, x1 + 1, baseline + px / 4 + 1}.intersect(clip);
    if (!r.empty())
        boxes_.push_back(r);
}

void Fonts::ask(uint64_t key, Need n, bool owed, bool kept, int x, int y)
{
    std::lock_guard<std::mutex> lk(mu_);
    auto renew = [&](Job& j) {
        if (n < j.need)
            j.need = n;
        j.owed = j.owed || owed;
        j.kept = j.kept || kept;
        j.dropped = false;
        if (n == Need::Visible) {
            j.asked = frame_;
            j.x = x;
            j.y = y;
        }
    };
    bool found = false;
    if (busy_on_ && busy_.key == key) {
        renew(busy_);
        found = true;
    }
    for (size_t i = 0; i < done_.size() && !found; i++)
        if (done_[i].job.key == key) {
            renew(done_[i].job);
            found = true;
        }
    for (size_t i = 0; i < direct_done_.size() && !found; i++)
        found = direct_done_[i].job.key == key;
    for (size_t i = 0; i < jobs_.size() && !found; i++)
        if (jobs_[i].key == key) {
            renew(jobs_[i]);
            found = true;
        }
    if (!found) {
        if (jobs_.size() >= kJobCap) {
            auto oldest = [&](Need c, bool stale) {
                int at = -1;
                for (size_t i = 0; i < jobs_.size(); i++)
                    if (jobs_[i].need == c && (!stale || jobs_[i].asked < frame_) &&
                        (at < 0 || jobs_[i].seq < jobs_[(size_t)at].seq))
                        at = (int)i;
                return at;
            };
            int at = oldest(Need::Warm, false);
            if (at < 0 && n <= Need::Predicted)
                at = oldest(Need::Predicted, false);
            if (at < 0 && n <= Need::Visible)
                at = oldest(Need::Visible, true);
            if (at >= 0)
                jobs_.erase(jobs_.begin() + at);
            else if (n > Need::Visible || jobs_.size() >= kJobCap + kJobGrow)
                return;
        }
        Job j;
        j.key = key;
        j.need = n;
        j.seq = seq_++;
        j.asked = frame_;
        j.x = x;
        j.y = y;
        j.owed = owed;
        j.kept = kept;
        jobs_.push_back(j);
    }
    if (busy_on_ && busy_nice_ && (busy_.need < Need::Warm || n <= Need::Visible)) {
        busy_nice_ = false;
        boost();
    }
    cv_.notify_one();
}

void Fonts::boost()
{
    boosts_++;
    if (int t = tid_.load())
        setpriority(PRIO_PROCESS, (id_t)t, 0);
}

void Fonts::signal()
{
    uint64_t one = 1;
    ssize_t n = write(efd_, &one, sizeof one);
    (void)n;
}

void Fonts::input()
{
    if (!th_.joinable())
        return;
    std::lock_guard<std::mutex> lk(mu_);
    last_input_ = mono_ms();
    if (busy_on_ && busy_nice_) {
        busy_nice_ = false;
        boost();
    }
}

void Fonts::set_busy(bool busy)
{
    if (!th_.joinable())
        return;
    std::lock_guard<std::mutex> lk(mu_);
    if (busy == ui_busy_)
        return;
    ui_busy_ = busy;
    if (!busy)
        cv_.notify_one();
}

void Fonts::set_idle_ahead(bool pending)
{
    if (!th_.joinable())
        return;
    std::lock_guard<std::mutex> lk(mu_);
    if (pending == idle_ahead_)
        return;
    idle_ahead_ = pending;
    if (!pending)
        cv_.notify_one();
}

void Fonts::set_ui(bool busy, bool ahead)
{
    if (!th_.joinable())
        return;
    std::lock_guard<std::mutex> lk(mu_);
    bool freed = (ui_busy_ && !busy) || (idle_ahead_ && !ahead);
    ui_busy_ = busy;
    idle_ahead_ = ahead;
    if (freed)
        cv_.notify_one();
}

uint32_t Fonts::rests() const
{
    std::lock_guard<std::mutex> lk(mu_);
    return rests_;
}

bool Fonts::warm_queued() const
{
    if (!th_.joinable())
        return false;
    std::lock_guard<std::mutex> lk(mu_);
    if (busy_on_ && busy_.need == Need::Warm)
        return true;
    for (const Job& j : jobs_)
        if (j.need == Need::Warm)
            return true;
    return false;
}

bool Fonts::owes() const
{
    if (!th_.joinable())
        return false;
    if (owed_in_)
        return true;
    std::lock_guard<std::mutex> lk(mu_);
    if (busy_on_ && busy_.owed)
        return true;
    for (const Job& j : jobs_)
        if (j.owed)
            return true;
    for (const Done& d : done_)
        if (d.job.owed)
            return true;
    return false;
}

void Fonts::shown(bool full)
{
    if (full)
        mark_ = frame_;
    if (!th_.joinable() || (!full && boxes_.empty())) {
        boxes_.clear();
        return;
    }
    std::lock_guard<std::mutex> lk(mu_);
    auto decay = [&](Job& j) {
        if (j.need != Need::Visible || j.asked >= frame_ || j.kept)
            return;
        bool gone = full;
        for (size_t i = 0; i < boxes_.size() && !gone; i++) {
            const Rect& r = boxes_[i];
            gone = j.x >= r.x0 && j.x < r.x1 && j.y >= r.y0 && j.y < r.y1;
        }
        if (gone) {
            j.need = Need::Warm;
            j.owed = false;
        }
    };
    for (Job& j : jobs_)
        decay(j);
    for (Done& d : done_)
        decay(d.job);
    if (busy_on_)
        decay(busy_);
    boxes_.clear();
}

bool Fonts::warm(const TextStyle& st, const char* s, Need n, int bucket)
{
    if (!th_.joinable())
        return false;
    if (runs_ && !printable(s) && !bidi_plain(s, Dir::Auto))
        return warm(st, std::string_view(s), Dir::Auto, n, bucket);
    bool all = true;
    while (*s) {
        uint32_t cp = apply_case(next_cp(s), st.tcase);
        if (bidi_ignorable(cp))
            continue;
        int fi, gid;
        if (!resolve_ready(st.weight, cp, fi, gid)) {
            all = false;
            continue;
        }
        if (fi < 0)
            continue;
        uint64_t k = key_of(bucket, fi, st.px, gid);
        if (!(bucket ? scaled_ : plain_).map.count(k))
            ask(k, n, false, false, -1, -1);
    }
    return all;
}

bool Fonts::cached(const TextStyle& st, const char* s, int bucket)
{
    if (!th_.joinable())
        return true;
    if (runs_ && !printable(s) && !bidi_plain(s, Dir::Auto))
        return cached(st, std::string_view(s), Dir::Auto, bucket);
    while (*s) {
        uint32_t cp = apply_case(next_cp(s), st.tcase);
        if (bidi_ignorable(cp))
            continue;
        int fi, gid;
        if (!resolve_ready(st.weight, cp, fi, gid))
            return false;
        if (fi >= 0 && !(bucket ? scaled_ : plain_).map.count(key_of(bucket, fi, st.px, gid)))
            return false;
    }
    return true;
}

void Fonts::unhold()
{
    if (!th_.joinable())
        return;
    std::lock_guard<std::mutex> lk(mu_);
    auto drop = [](Job& j) {
        if (j.need == Need::Hold)
            j.need = j.owed ? Need::Visible : Need::Predicted;
    };
    for (Job& j : jobs_)
        drop(j);
    for (Done& d : done_)
        drop(d.job);
    if (busy_on_)
        drop(busy_);
}

void Fonts::preload()
{
    if (!th_.joinable())
        return;
    std::lock_guard<std::mutex> lk(mu_);
    for (size_t i = 0; i < fall_.size(); i++)
        if (fall_[i].state.load(std::memory_order_acquire) == 0 &&
            std::find(loads_.begin(), loads_.end(), kPrimFaces + (int)i) == loads_.end())
            loads_.push_back(kPrimFaces + (int)i);
    cv_.notify_one();
}

bool Fonts::faces_ready() const
{
    for (const Face& f : fall_) {
        if (f.ranged)
            continue;
        int s = f.state.load(std::memory_order_acquire);
        if (s == 0 || s == 2)
            return false;
    }
    return true;
}

void Fonts::drop_warm()
{
    if (!th_.joinable())
        return;
    std::lock_guard<std::mutex> lk(mu_);
    jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(), [](const Job& j) { return j.need == Need::Warm; }),
                jobs_.end());
}

size_t Fonts::queued()
{
    std::lock_guard<std::mutex> lk(mu_);
    return jobs_.size() + loads_.size() + done_.size() + (busy_on_ ? 1 : 0) + (loading_ ? 1 : 0);
}

bool Fonts::pump()
{
    if (!th_.joinable())
        return false;
    uint64_t v;
    ssize_t r = read(efd_, &v, sizeof v);
    (void)r;
    std::vector<Done> got;
    {
        std::lock_guard<std::mutex> lk(mu_);
        got.swap(done_);
    }
    bool owed = owed_in_;
    owed_in_ = false;
    for (Done& d : got) {
        uint64_t k = d.job.key;
        Cache& c = key_bucket(k) ? scaled_ : plain_;
        insert(c, k, std::move(d.g), d.job.owed || d.job.need == Need::Hold);
        owed = owed || d.job.owed;
    }
    return owed;
}

int Fonts::pick(int64_t now, int64_t& until) const
{
    until = -1;
    bool held = ui_busy_ || idle_ahead_;
    bool quiet = !held && now >= last_input_ + kQuietMs;
    int best = -1;
    for (size_t i = 0; i < jobs_.size(); i++) {
        const Job& j = jobs_[i];
        if (j.need == Need::Warm && !quiet) {
            if (!held)
                until = last_input_ + kQuietMs;
            continue;
        }
        if (best < 0) {
            best = (int)i;
            continue;
        }
        const Job& o = jobs_[(size_t)best];
        bool better = j.need != o.need                ? j.need < o.need
                      : j.need == Need::Visible ? (j.asked != o.asked ? j.asked > o.asked : j.seq > o.seq)
                                                : j.seq < o.seq;
        if (better)
            best = (int)i;
    }
    return best;
}

void Fonts::run()
{
    tid_ = (int)syscall(SYS_gettid);
    std::unique_lock<std::mutex> lk(mu_);
    while (!quit_) {
        if (!loads_.empty()) {
            int fi = loads_.front();
            loads_.erase(loads_.begin());
            loading_ = true;
            lk.unlock();
            setpriority(PRIO_PROCESS, (id_t)tid_.load(), 0);
            load_face(face(fi), true);
            lk.lock();
            loading_ = false;
            signal();
            continue;
        }
        int64_t until, now = mono_ms();
        int i = pick(now, until);
        if (i < 0) {
            int64_t at = copy_at(now);
            if (at == 0) {
                lk.unlock();
                copy_item();
                lk.lock();
                continue;
            }
            if (at > 0 && (until < 0 || at < until))
                until = at;
            rests_++;
            if (until >= 0)
                cv_.wait_until(lk, std::chrono::steady_clock::time_point(std::chrono::milliseconds(until)));
            else
                cv_.wait(lk);
            continue;
        }
        busy_ = jobs_[(size_t)i];
        jobs_.erase(jobs_.begin() + i);
        busy_on_ = true;
        busy_nice_ = busy_.need == Need::Warm;
        uint64_t k = busy_.key;
        bool warm = busy_nice_;
        lk.unlock();
        setpriority(PRIO_PROCESS, (id_t)tid_.load(), warm ? kWarmNice : 0);
        if (warm) {
            lk.lock();
            bool raised = !busy_nice_;
            lk.unlock();
            if (raised)
                setpriority(PRIO_PROCESS, (id_t)tid_.load(), 0);
        }
        if (gate_)
            gate_(k);
        bool direct = key_bucket(k) == kDirect;
        Glyph g = make(key_face(k), key_px(k), key_gid(k), direct ? 1.0f : factor(key_px(k), key_bucket(k)));
        (direct ? ahead_rasters_ : worker_rasters_)++;
        lk.lock();
        if (!direct)
            done_.push_back({busy_, std::move(g)});
        else if (!busy_.dropped)
            direct_done_.push_back({busy_, std::move(g)});
        busy_on_ = busy_nice_ = false;
        done_cv_.notify_all();
        signal();
    }
}

int64_t Fonts::copy_at(int64_t now)
{
    if (!copy_.want)
        return -1;
    if (ui_busy_ || idle_ahead_ || copy_.hold || !copy_.known) {
        copy_.pass = true;
        return -1;
    }
    if (now < last_input_ + kQuietMs)
        copy_.pass = true;
    int64_t at = std::max(last_input_ + kQuietMs, copy_.next);
    return at > now ? at : 0;
}

void Fonts::copy_end()
{
    if (copy_.src >= 0)
        ::close(copy_.src);
    copy_.src = -1;
    std::lock_guard<std::mutex> lk(mu_);
    copy_.want = false;
}

void Fonts::copy_item()
{
    if (gate_)
        gate_(kCopyItem + copy_.off / kCopyBlock);
    setpriority(PRIO_PROCESS, (id_t)tid_.load(), 0);
    std::string card;
    {
        std::lock_guard<std::mutex> lk(mu_);
        card = copy_.card;
    }
    std::lock_guard<std::mutex> lk(copy_mu_);
    Copy& c = copy_;
    if (c.stop)
        return copy_end();
    if (!c.checked) {
        c.checked = true;
        uint64_t need = c.size + (copy_need_ ? copy_need_(card) : 0);
        struct statvfs v;
        uint64_t free = mkdirs(c.dir) && statvfs(c.dir.c_str(), &v) == 0 ? (uint64_t)v.f_bavail * v.f_frsize : 0;
        if (free < need) {
            log_info("funeos: font copy skipped, %llu kB free of %llu kB wanted\n", (unsigned long long)(free >> 10),
                     (unsigned long long)(need >> 10));
            return copy_end();
        }
        c.began = mono_ms();
    }
    const char* why = nullptr;
    int fd = -1, err = 0;
    struct stat st{};
    if (c.src < 0 && (c.src = ::open(c.image.c_str(), O_RDONLY | O_CLOEXEC)) < 0)
        why = "source missing";
    else if ((fd = ::open(c.tmp.c_str(), O_WRONLY | O_CLOEXEC | (c.off ? 0 : O_CREAT), 0644)) < 0)
        why = "tmp missing";
    else if (fstat(fd, &st) != 0 || (uint64_t)st.st_size < c.off)
        why = "tmp short";
    if (!why && c.pass) {
        uint64_t at = (uint64_t)st.st_size / kCopyBlock * kCopyBlock;
        if (at != (uint64_t)st.st_size && ftruncate(fd, (off_t)at) != 0)
            why = "tmp truncate";
        c.off = at;
        c.pass = false;
    }
    if (!why && c.off < c.size) {
        size_t n = (size_t)std::min(kCopyBlock, c.size - c.off), got = 0, put = 0;
        std::vector<uint8_t> buf(n);
        int64_t t0 = mono_us();
        while (got < n) {
            ssize_t r = pread(c.src, buf.data() + got, n - got, (off_t)(c.off + got));
            if (r < 0 && errno == EINTR)
                continue;
            if (r <= 0)
                break;
            got += (size_t)r;
        }
        int64_t t1 = mono_us();
        while (got == n && put < n) {
            ssize_t w = copy_write_ ? copy_write_(fd, buf.data() + put, n - put, (off_t)(c.off + put))
                                    : pwrite(fd, buf.data() + put, n - put, (off_t)(c.off + put));
            if (w < 0 && errno == EINTR)
                continue;
            if (w <= 0) {
                err = w < 0 ? errno : EIO;
                break;
            }
            put += (size_t)w;
        }
        int64_t t2 = mono_us();
        if (got != n) {
            why = "source short";
        } else if (put != n) {
            why = strerror(err);
            ::close(fd);
            fd = -1;
            unlink(c.tmp.c_str());
        } else {
            log_info("funeos: font copy block %llu read %lld us write %lld us\n",
                     (unsigned long long)(c.off / kCopyBlock), (long long)(t1 - t0), (long long)(t2 - t1));
            c.off += n;
        }
    }
    if (fd >= 0)
        ::close(fd);
    if (why) {
        fprintf(stderr, "funeos: font copy stopped at %llu: %s\n", (unsigned long long)c.off, why);
        return copy_end();
    }
    {
        std::lock_guard<std::mutex> wl(mu_);
        c.next = mono_ms() + copy_gap_;
    }
    if (c.off < c.size)
        return;
    std::string stamp = c.build + " " + std::to_string(c.size) + "\n";
    if (rename(c.tmp.c_str(), c.file.c_str()) != 0 || !fsync_dir(c.dir) ||
        !write_atomic(c.stamp, stamp.data(), stamp.size()))
        fprintf(stderr, "funeos: font copy %s not kept: %s\n", c.file.c_str(), strerror(errno));
    else
        log_info("funeos: font copy %s done, %llu kB in %lld ms\n", c.file.c_str(), (unsigned long long)(c.size >> 10),
                 (long long)(mono_ms() - c.began));
    copy_end();
}

bool Fonts::in_ranges(const Face& f, uint32_t cp)
{
    for (const std::pair<uint32_t, uint32_t>& r : f.ranges)
        if (cp >= r.first && cp <= r.second)
            return true;
    return false;
}

bool Fonts::covers(uint32_t cp) const
{
    for (const Face& f : prim_)
        if (f.state.load(std::memory_order_acquire) == 1 && stbtt_FindGlyphIndex(f.info, (int)cp))
            return true;
    for (const Face& f : fall_) {
        int s = f.state.load(std::memory_order_acquire);
        if (f.ranged ? in_ranges(f, cp) && s != -1 : s == 1 && stbtt_FindGlyphIndex(f.info, (int)cp))
            return true;
    }
    return false;
}

int Fonts::face_state(int i) const
{
    const Face& f = i < kPrimFaces ? prim_[i] : fall_[(size_t)(i - kPrimFaces)];
    return f.state.load(std::memory_order_acquire);
}

std::string Fonts::face_file(int i) const
{
    return base_of(i < kPrimFaces ? prim_[i].path : fall_[(size_t)(i - kPrimFaces)].path);
}

int Fonts::ascent(int px) const
{
    const Face& f = prim_[1];
    if (f.state.load(std::memory_order_acquire) != 1)
        return px;
    int a, d, g;
    stbtt_GetFontVMetrics(f.info, &a, &d, &g);
    return (int)(((int64_t)a * px + f.upem - 1) / f.upem);
}

int Fonts::descent(int px) const
{
    const Face& f = prim_[1];
    if (f.state.load(std::memory_order_acquire) != 1)
        return px / 4;
    int a, d, g;
    stbtt_GetFontVMetrics(f.info, &a, &d, &g);
    return (int)(((int64_t)-d * px + f.upem - 1) / f.upem);
}

}
