#include "scraper.h"

#include <cstdint>
#include <cstdio>
#include <ctime>

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <curl/curl.h>

namespace fd {

namespace {

constexpr const char* kDbPin = "c4e17d41ec1bb744fb064f7cefeb61b878fc5d64";
const std::string kDbEpoch(kDbPin, 7);
constexpr int64_t kNegTtl = 30LL * 24 * 3600;
constexpr size_t kMaxFetch = 32u * 1024 * 1024;

std::string dat_sha(const std::string& system_id)
{
    if (system_id == "gb")
        return "2dc0e5dd0ec696b60a4fc1987b1f84814a08cebc31138551f2e7ef9f11e10f74";
    if (system_id == "gbc")
        return "63cd203e8bc36273f13129be0b039c023f8475e5c82e69776bf659534041fb5a";
    if (system_id == "gba")
        return "0b8bd3a414276e9c31137b0a245fcdaf917d34fd035e02499c965a290ac8fce4";
    if (system_id == "nes")
        return "f8afe5fd148ba547287b1c5ddd1a92bb7fc3a42351205c466fe380da18aec34b";
    if (system_id == "snes")
        return "76bb6fc95243d03c0842b0600c34c9a0e6e711be0027d7d4fe90330658ae20ec";
    if (system_id == "genesis")
        return "2785056d877edd519a31bcc9e042f1aa8821e3ce2520edcc744dd7ecd56e6a4a";
    if (system_id == "n64")
        return "17c3a83994717859cca16a89f5b84952dcd8e55d45fe0cf42b1245f565683e49";
    return "";
}

std::string sha256_hex(const std::string& data)
{
    static const uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

    std::string msg = data;
    uint64_t bitlen = (uint64_t)msg.size() * 8;
    msg += (char)0x80;
    while (msg.size() % 64 != 56)
        msg += (char)0x00;
    for (int i = 7; i >= 0; i--)
        msg += (char)((bitlen >> (i * 8)) & 0xff);

    auto rotr = [](uint32_t x, int n) -> uint32_t { return (x >> n) | (x << (32 - n)); };
    for (size_t off = 0; off < msg.size(); off += 64) {
        const unsigned char* p = reinterpret_cast<const unsigned char*>(msg.data()) + off;
        uint32_t w[64];
        for (int i = 0; i < 16; i++)
            w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) | ((uint32_t)p[i * 4 + 2] << 8) |
                   (uint32_t)p[i * 4 + 3];
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; i++) {
            uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + maj;
            hh = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += hh;
    }
    static const char* hx = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (int i = 0; i < 8; i++)
        for (int j = 3; j >= 0; j--) {
            unsigned char byte = (h[i] >> (j * 8)) & 0xff;
            out += hx[byte >> 4];
            out += hx[byte & 0xf];
        }
    return out;
}

std::string pct(const std::string& s)
{
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
            c == '_' || c == '~') {
            out += (char)c;
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0xF];
        }
    }
    return out;
}

std::string lower(const std::string& s)
{
    std::string r(s);
    for (char& c : r)
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
    return r;
}

bool ends_with(const std::string& s, const std::string& suf)
{
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

bool is_cartridge(const std::string& id)
{
    static const char* const kCart[] = {"gb", "gbc", "nes", "snes", "genesis", "gba", "n64"};
    for (const char* c : kCart)
        if (id == c)
            return true;
    return false;
}

const char* dir_name(ArtType type)
{
    switch (type) {
    case ArtType::Covers:
        return "covers";
    case ArtType::Snaps:
        return "snaps";
    case ArtType::Titles:
        return "titles";
    }
    return "covers";
}

const char* named_type(ArtType type)
{
    switch (type) {
    case ArtType::Covers:
        return "Named_Boxarts";
    case ArtType::Snaps:
        return "Named_Snaps";
    case ArtType::Titles:
        return "Named_Titles";
    }
    return "Named_Boxarts";
}

bool file_exists(const std::string& path)
{
    struct stat sb;
    return stat(path.c_str(), &sb) == 0 && S_ISREG(sb.st_mode);
}

void mkdir_p(const std::string& dir)
{
    std::string cur;
    for (size_t i = 0; i < dir.size(); i++) {
        cur += dir[i];
        if (dir[i] == '/' && cur.size() > 1)
            mkdir(cur.c_str(), 0755);
    }
    if (!cur.empty())
        mkdir(cur.c_str(), 0755);
}

bool write_body(const std::string& path, const std::string& body)
{
    size_t slash = path.find_last_of('/');
    if (slash != std::string::npos)
        mkdir_p(path.substr(0, slash));
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f)
        return false;
    size_t n = body.empty() ? 0 : std::fwrite(body.data(), 1, body.size(), f);
    std::fclose(f);
    return n == body.size();
}

bool write_atomic(const std::string& path, const std::string& body)
{
    size_t slash = path.find_last_of('/');
    if (slash != std::string::npos)
        mkdir_p(path.substr(0, slash));
    std::string tmp = path + ".part";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f)
        return false;
    size_t n = body.empty() ? 0 : std::fwrite(body.data(), 1, body.size(), f);
    std::fflush(f);
    int fd = fileno(f);
    if (fd >= 0)
        fsync(fd);
    std::fclose(f);
    if (n != body.size()) {
        std::remove(tmp.c_str());
        return false;
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

bool read_whole(const std::string& path, std::string& out)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f)
        return false;
    char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
        out.append(buf, n);
    bool err = std::ferror(f) != 0;
    std::fclose(f);
    return !err;
}

uint32_t crc32_update(uint32_t crc, const unsigned char* buf, size_t n)
{
    static const uint32_t* table = [] {
        static uint32_t t[256];
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            t[i] = c;
        }
        return t;
    }();
    crc ^= 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++)
        crc = table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

std::optional<uint32_t> crc32_whole(const std::string& path)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f)
        return std::nullopt;
    uint32_t crc = 0;
    unsigned char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
        crc = crc32_update(crc, buf, n);
    bool err = std::ferror(f) != 0;
    std::fclose(f);
    if (err)
        return std::nullopt;
    return crc;
}

std::optional<uint32_t> zip_entry_crc(const std::string& path, const std::vector<std::string>& exts)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f)
        return std::nullopt;
    if (std::fseek(f, 0, SEEK_END) != 0) {
        std::fclose(f);
        return std::nullopt;
    }
    long lsz = std::ftell(f);
    if (lsz < 22) {
        std::fclose(f);
        return std::nullopt;
    }
    size_t sz = (size_t)lsz;

    size_t taillen = sz < 65557 ? sz : 65557;
    size_t base = sz - taillen;
    std::string tail;
    tail.resize(taillen);
    if (std::fseek(f, (long)base, SEEK_SET) != 0 || std::fread(&tail[0], 1, taillen, f) != taillen) {
        std::fclose(f);
        return std::nullopt;
    }
    const unsigned char* t = reinterpret_cast<const unsigned char*>(tail.data());
    auto t16 = [&](size_t o) -> uint32_t { return (uint32_t)t[o] | ((uint32_t)t[o + 1] << 8); };
    auto t32 = [&](size_t o) -> uint32_t {
        return (uint32_t)t[o] | ((uint32_t)t[o + 1] << 8) | ((uint32_t)t[o + 2] << 16) | ((uint32_t)t[o + 3] << 24);
    };

    size_t eocd = SIZE_MAX;
    for (size_t off = taillen - 22;; off--) {
        if (t32(off) == 0x06054b50u && off + 22 + t16(off + 20) == taillen) {
            eocd = off;
            break;
        }
        if (off == 0)
            break;
    }
    if (eocd == SIZE_MAX) {
        std::fclose(f);
        return std::nullopt;
    }

    uint32_t entries = t16(eocd + 10);
    if (entries == 0 || t16(eocd + 8) == 0xFFFF || t16(eocd + 10) == 0xFFFF || t32(eocd + 12) == 0xFFFFFFFFu ||
        t32(eocd + 16) == 0xFFFFFFFFu) {
        std::fclose(f);
        return std::nullopt;
    }
    size_t cd_off = t32(eocd + 16);
    size_t cd_size = t32(eocd + 12);
    if (cd_off + cd_size > sz) {
        std::fclose(f);
        return std::nullopt;
    }

    std::string cdbuf;
    const unsigned char* cd;
    size_t cdlen;
    if (cd_off >= base) {
        cd = t + (cd_off - base);
        cdlen = taillen - (cd_off - base);
    } else {
        cdbuf.resize(cd_size);
        if (std::fseek(f, (long)cd_off, SEEK_SET) != 0 || std::fread(&cdbuf[0], 1, cd_size, f) != cd_size) {
            std::fclose(f);
            return std::nullopt;
        }
        cd = reinterpret_cast<const unsigned char*>(cdbuf.data());
        cdlen = cd_size;
    }
    std::fclose(f);

    auto c16 = [&](size_t o) -> uint32_t { return (uint32_t)cd[o] | ((uint32_t)cd[o + 1] << 8); };
    auto c32 = [&](size_t o) -> uint32_t {
        return (uint32_t)cd[o] | ((uint32_t)cd[o + 1] << 8) | ((uint32_t)cd[o + 2] << 16) | ((uint32_t)cd[o + 3] << 24);
    };

    size_t pos = 0;
    std::optional<uint32_t> first_crc;
    std::optional<uint32_t> matched_crc;
    for (uint32_t i = 0; i < entries; i++) {
        if (pos + 46 > cdlen)
            break;
        if (c32(pos) != 0x02014b50u)
            return std::nullopt;
        uint32_t crc = c32(pos + 16);
        uint32_t fnl = c16(pos + 28);
        uint32_t efl = c16(pos + 30);
        uint32_t fcl = c16(pos + 32);
        if (pos + 46 + fnl > cdlen)
            break;
        std::string fname(reinterpret_cast<const char*>(cd) + pos + 46, fnl);
        if (!first_crc)
            first_crc = crc;
        if (!matched_crc) {
            std::string lf = lower(fname);
            for (const std::string& e : exts)
                if (e != ".zip" && ends_with(lf, e)) {
                    matched_crc = crc;
                    break;
                }
        }
        pos += 46 + fnl + efl + fcl;
    }
    return matched_crc ? matched_crc : first_crc;
}

std::optional<uint32_t> crc32_rom(const std::string& path, const std::vector<std::string>& exts)
{
    if (ends_with(lower(path), ".zip"))
        return zip_entry_crc(path, exts);
    return crc32_whole(path);
}

std::string dat_url(const std::string& basename)
{
    return std::string("https://raw.githubusercontent.com/libretro/libretro-database/") + kDbPin +
           "/metadat/no-intro/" + pct(basename) + ".dat";
}

void parse_dat(const std::string& body, std::unordered_map<uint32_t, std::string>& out)
{
    std::string cur;
    size_t i = 0;
    size_t n = body.size();
    while (i < n) {
        size_t e = body.find('\n', i);
        if (e == std::string::npos)
            e = n;
        std::string line = body.substr(i, e - i);
        i = e + 1;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (line.size() > 7 && line[0] == '\t' && line[1] != '\t' && line.compare(1, 6, "name \"") == 0) {
            size_t a = 7;
            size_t b = line.rfind('"');
            if (b != std::string::npos && b > a)
                cur = line.substr(a, b - a);
            continue;
        }
        if (cur.empty() || line.find("rom (") == std::string::npos)
            continue;
        size_t cp = line.find(" crc ");
        if (cp == std::string::npos)
            continue;
        uint32_t crc = 0;
        int cnt = 0;
        for (size_t h = cp + 5; h < line.size() && cnt < 8; h++, cnt++) {
            char c = line[h];
            int d;
            if (c >= '0' && c <= '9')
                d = c - '0';
            else if (c >= 'a' && c <= 'f')
                d = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F')
                d = c - 'A' + 10;
            else
                break;
            crc = (crc << 4) | (uint32_t)d;
        }
        if (cnt == 8)
            out.emplace(crc, cur);
    }
}

size_t curl_write(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    std::string* body = static_cast<std::string*>(userdata);
    size_t want = size * nmemb;
    if (body->size() + want > kMaxFetch)
        return 0;
    body->append(ptr, want);
    return want;
}

int curl_xfer(void* clientp, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
    const std::atomic<bool>* a = static_cast<const std::atomic<bool>*>(clientp);
    return (a && a->load()) ? 1 : 0;
}

void ensure_global()
{
    static bool done = [] {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        return true;
    }();
    (void)done;
}

HttpResult curl_fetch(const std::string& url, const std::atomic<bool>* abort)
{
    HttpResult r{0, std::string(), false};
    ensure_global();
    CURL* h = curl_easy_init();
    if (!h) {
        r.transport_error = true;
        return r;
    }
    curl_easy_setopt(h, CURLOPT_URL, url.c_str());
    curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(h, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, curl_write);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &r.body);
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(h, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(h, CURLOPT_MAXFILESIZE_LARGE, (curl_off_t)kMaxFetch);
    curl_easy_setopt(h, CURLOPT_USERAGENT, "blockie-frontend");
    curl_easy_setopt(h, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
    if (abort) {
        curl_easy_setopt(h, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(h, CURLOPT_XFERINFOFUNCTION, curl_xfer);
        curl_easy_setopt(h, CURLOPT_XFERINFODATA, const_cast<std::atomic<bool>*>(abort));
    }
    CURLcode rc = curl_easy_perform(h);
    long code = 0;
    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(h);
    if (rc != CURLE_OK) {
        r.transport_error = true;
        r.body.clear();
        return r;
    }
    r.status = (int)code;
    if (code >= 500) {
        r.transport_error = true;
        r.body.clear();
    }
    return r;
}

std::string neg_key(const std::string& system_id, ArtType type, const std::string& name)
{
    return kDbEpoch + "|" + system_id + "|" + dir_name(type) + "|" + name;
}

}

Scraper::Scraper(Library& lib, const std::string& data_root, Fetcher fetch, const std::atomic<bool>* abort)
    : lib_(lib), data_root_(data_root), fetch_(std::move(fetch))
{
    while (data_root_.size() > 1 && data_root_.back() == '/')
        data_root_.pop_back();
    if (!fetch_)
        fetch_ = [abort](const std::string& url) { return curl_fetch(url, abort); };
}

std::string Scraper::url_for(const std::string& libretro_folder, const std::string& name, ArtType type)
{
    return "https://thumbnails.libretro.com/" + pct(libretro_folder) + "/" + pct(named_type(type)) + "/" + pct(name) +
           ".png";
}

std::string Scraper::local_path(const std::string& data_root, const std::string& system_id, const std::string& name,
                                ArtType type)
{
    std::string root = data_root;
    while (root.size() > 1 && root.back() == '/')
        root.pop_back();
    return root + "/media/" + system_id + "/" + dir_name(type) + "/" + name + ".png";
}

std::optional<uint32_t> Scraper::rom_crc(const std::string& path, const std::vector<std::string>& exts)
{
    return crc32_rom(path, exts);
}

std::string Scraper::sha256_hex(const std::string& data)
{
    return fd::sha256_hex(data);
}

void Scraper::set_expected_dat_sha(const std::string& system_id, const std::string& sha)
{
    dat_sha_override_[system_id] = sha;
}

std::string Scraper::expected_dat_sha(const std::string& system_id) const
{
    auto it = dat_sha_override_.find(system_id);
    return it != dat_sha_override_.end() ? it->second : dat_sha(system_id);
}

const std::unordered_map<uint32_t, std::string>* Scraper::dat_index(const std::string& system_id,
                                                                    const std::string& basename)
{
    auto it = dat_cache_.find(system_id);
    if (it != dat_cache_.end())
        return &it->second;
    if (dat_tried_.count(system_id))
        return nullptr;
    dat_tried_.insert(system_id);

    std::string want = expected_dat_sha(system_id);
    std::string path = data_root_ + "/cache/dat/" + basename + ".dat";
    std::string body;
    if (read_whole(path, body) && body.compare(0, 12, "clrmamepro (") == 0 && sha256_hex(body) == want) {
        auto& m = dat_cache_[system_id];
        parse_dat(body, m);
        return &m;
    }

    HttpResult r = fetch_(dat_url(basename));
    if (r.transport_error || r.status != 200 || r.body.empty() || r.body.compare(0, 12, "clrmamepro (") != 0)
        return nullptr;
    if (sha256_hex(r.body) != want)
        return nullptr;
    write_atomic(path, r.body);
    auto& m = dat_cache_[system_id];
    parse_dat(r.body, m);
    return &m;
}

std::string Scraper::resolve_name(const std::string& system_id, const std::string& rompath)
{
    if (rompath.empty() || !is_cartridge(system_id))
        return "";
    const SystemCfg* cfg = nullptr;
    for (const SystemCfg& c : lib_.config())
        if (c.id == system_id) {
            cfg = &c;
            break;
        }
    if (!cfg)
        return "";

    struct stat sb;
    if (stat(rompath.c_str(), &sb) != 0)
        return "";
    int64_t size = (int64_t)sb.st_size;
    int64_t mtime = (int64_t)sb.st_mtime;

    std::string cached;
    if (lib_.resolve_get(rompath, size, mtime, kDbEpoch, cached))
        return cached;

    std::optional<uint32_t> crc = crc32_rom(rompath, cfg->exts);
    if (!crc)
        return "";
    const std::unordered_map<uint32_t, std::string>* idx = dat_index(system_id, cfg->libretro_folder);
    if (!idx)
        return "";
    auto hit = idx->find(*crc);
    std::string name = hit != idx->end() ? hit->second : "";
    lib_.resolve_put(rompath, size, mtime, kDbEpoch, name);
    return name;
}

std::string Scraper::cached_name(const std::string& system_id, const std::string& rompath)
{
    if (rompath.empty() || !is_cartridge(system_id))
        return "";
    struct stat sb;
    if (stat(rompath.c_str(), &sb) != 0)
        return "";
    std::string name;
    if (!lib_.resolve_get(rompath, (int64_t)sb.st_size, (int64_t)sb.st_mtime, kDbEpoch, name))
        return "";
    return name;
}

std::string Scraper::record_local(const Game& g, ArtType type)
{
    std::string resolved = cached_name(g.system_id, g.rompath_abs);
    const std::string* names[] = {&resolved, &g.name};
    for (const std::string* name : names) {
        if (name->empty())
            continue;
        std::string path = local_path(data_root_, g.system_id, *name, type);
        if (file_exists(path)) {
            lib_.set_media(g.id, dir_name(type), path, "local");
            lib_.neg_clear(neg_key(g.system_id, type, *name));
            return path;
        }
    }
    return "";
}

int Scraper::sweep_local(ArtType type)
{
    int n = 0;
    std::string want = dir_name(type);
    lib_.begin();
    for (const SystemCfg& sys : lib_.config())
        for (const Game& g : lib_.games(sys.id)) {
            if (!g.present)
                continue;
            bool have = false;
            for (const MediaRef& m : lib_.media(g.id))
                if (m.type == want && file_exists(m.path)) {
                    have = true;
                    break;
                }
            if (!have && !record_local(g, type).empty())
                n++;
        }
    lib_.commit();
    return n;
}

ArtOutcome Scraper::art(int64_t game_id, const std::string& system_id, const std::string& libretro_folder,
                        const std::string& name, ArtType type)
{
    std::string path = local_path(data_root_, system_id, name, type);
    std::string key = neg_key(system_id, type, name);

    if (file_exists(path)) {
        lib_.set_media(game_id, dir_name(type), path, "local");
        lib_.neg_clear(key);
        return {true, false, false, path};
    }
    if (lib_.neg_fresh(key, kNegTtl)) {
        return {false, true, false, std::string()};
    }

    HttpResult r = fetch_(url_for(libretro_folder, name, type));
    if (r.transport_error || r.status >= 500) {
        return {false, false, true, std::string()};
    }
    if (r.status == 404) {
        lib_.neg_mark(key, (int64_t)std::time(nullptr));
        return {false, true, false, std::string()};
    }
    if (r.status == 200) {
        if (!write_body(path, r.body))
            return {false, false, true, std::string()};
        lib_.set_media(game_id, dir_name(type), path, "libretro");
        lib_.neg_clear(key);
        return {true, false, false, path};
    }
    return {false, false, true, std::string()};
}

void Scraper::scrape_game(const Game& g, const std::string& libretro_folder)
{
    std::string eff = resolve_name(g.system_id, g.rompath_abs);
    if (eff.empty())
        eff = g.name;
    art(g.id, g.system_id, libretro_folder, eff, ArtType::Covers);
    art(g.id, g.system_id, libretro_folder, eff, ArtType::Snaps);
    art(g.id, g.system_id, libretro_folder, eff, ArtType::Titles);
}

}
