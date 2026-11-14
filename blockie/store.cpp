#include "store.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <unordered_map>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "library.h"
#include "ring_spin.h"
#include "sqlite3.h"

namespace fd {

namespace {

size_t cut_at(const std::string& s, size_t cap)
{
    if (s.size() <= cap)
        return s.size();
    size_t n = cap;
    while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80)
        n--;
    return n;
}

std::string column(sqlite3_stmt* st, int i)
{
    const unsigned char* t = sqlite3_column_text(st, i);
    return t ? std::string((const char*)t, (size_t)sqlite3_column_bytes(st, i)) : std::string();
}

std::string real(const std::string& p)
{
    char buf[PATH_MAX];
    return realpath(p.c_str(), buf) ? std::string(buf) : std::string();
}

std::string stem(const std::string& name)
{
    size_t n = name.size();
    return n > 4 && name.compare(n - 4, 4, ".zip") == 0 ? name.substr(0, n - 4) : name;
}

std::string leaf(const std::string& path)
{
    size_t s = path.find_last_of('/');
    return s == std::string::npos ? path : path.substr(s + 1);
}

bool by_title(const StoreItem& a, const StoreItem& b)
{
    std::string x = a.title, y = b.title;
    for (char& ch : x)
        ch = (char)std::tolower((unsigned char)ch);
    for (char& ch : y)
        ch = (char)std::tolower((unsigned char)ch);
    return x != y ? x < y : a.name < b.name;
}

bool cjk(uint32_t cp) { return cp >= 0x2E80; }

uint32_t decode(const std::string& s, size_t& i)
{
    unsigned char c = (unsigned char)s[i];
    int n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
    uint32_t cp = n == 1 ? c : c & (0x3F >> (n - 1));
    size_t end = std::min(s.size(), i + (size_t)n);
    for (size_t k = i + 1; k < end; k++)
        cp = (cp << 6) | ((unsigned char)s[k] & 0x3F);
    i = end;
    return cp;
}

bool has_genre(const StoreItem& it, const char* g)
{
    for (const std::string& s : it.genres)
        if (s == g)
            return true;
    return false;
}

}

std::string cap_text(const std::string& s, size_t cap)
{
    std::string out = s.substr(0, cut_at(s, cap));
    for (char& ch : out)
        if ((unsigned char)ch < 0x20 || ch == 0x7F)
            ch = ' ';
    return out;
}

std::string cap_body(const std::string& s, size_t cap)
{
    std::string out = s.substr(0, cut_at(s, cap));
    for (char& ch : out)
        if (((unsigned char)ch < 0x20 && ch != '\n') || ch == 0x7F)
            ch = ' ';
    return out;
}

std::vector<std::string> cap_list(const std::string& s, size_t cap)
{
    std::string raw = s.substr(0, cut_at(s, cap));
    std::vector<std::string> out;
    size_t i = 0;
    for (;;) {
        size_t t = raw.find('\t', i);
        std::string part = cap_text(raw.substr(i, t == std::string::npos ? std::string::npos : t - i), cap);
        if (!part.empty())
            out.push_back(part);
        if (t == std::string::npos)
            return out;
        i = t + 1;
    }
}

std::string images_base(const std::string& ports_dir)
{
    std::string root = real(ports_dir);
    return root.empty() ? std::string() : root + kStoreImages;
}

std::string confined_image(const std::string& path, const std::string& ports_dir)
{
    return confined_in(path, images_base(ports_dir));
}

std::string confined_in(const std::string& path, const std::string& base)
{
    if (path.empty() || base.empty())
        return {};
    std::string r = real(path);
    if (r.empty())
        return {};
    if (r.compare(0, base.size(), base) != 0)
        return {};
    size_t slash = r.find('/', base.size());
    if (slash == std::string::npos || slash == base.size() || r.find('/', slash + 1) != std::string::npos)
        return {};
    struct stat sb;
    if (stat(r.c_str(), &sb) != 0 || !S_ISREG(sb.st_mode))
        return {};
    return r;
}

bool read_catalog(const std::string& path, const std::string& ports_dir, Catalog& out)
{
    out.ports.clear();
    std::string uri = "file:" + path + "?mode=ro&immutable=1";
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(uri.c_str(), &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_URI, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return false;
    }
    bool ok = false;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, "PRAGMA user_version;", -1, &st, nullptr) == SQLITE_OK &&
        sqlite3_step(st) == SQLITE_ROW && sqlite3_column_int(st, 0) == kCatalogVersion)
        ok = true;
    sqlite3_finalize(st);
    st = nullptr;
    const char* sql = "SELECT name, title, genres, porter, description, inst, size, update_available, screenshot,"
                      " cover FROM ports;";
    if (ok && sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK)
        ok = false;
    int rc = SQLITE_DONE;
    while (ok && (rc = sqlite3_step(st)) == SQLITE_ROW) {
        StorePort p;
        p.name = cap_text(column(st, 0), kNameCap);
        if (p.name.empty())
            continue;
        p.title = cap_text(column(st, 1), kTextCap);
        if (p.title.empty())
            p.title = p.name;
        p.genres = cap_list(column(st, 2), kTextCap);
        for (std::string& g : p.genres)
            for (char& ch : g)
                ch = (char)std::tolower((unsigned char)ch);
        p.porter = cap_text(join_words(cap_list(column(st, 3), kBodyCap)), kTextCap);
        p.description = cap_body(column(st, 4), kBodyCap);
        p.inst = cap_body(column(st, 5), kBodyCap);
        p.size = std::max<int64_t>(0, sqlite3_column_int64(st, 6));
        p.update = sqlite3_column_int(st, 7) != 0;
        p.screenshot = confined_image(column(st, 8), ports_dir);
        p.cover = confined_image(column(st, 9), ports_dir);
        out.ports.push_back(std::move(p));
    }
    if (ok && rc != SQLITE_DONE)
        ok = false;
    sqlite3_finalize(st);
    sqlite3_close(db);
    if (!ok)
        out.ports.clear();
    return ok;
}

SteamState read_steam(const std::string& path)
{
    SteamState out;
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOCTTY | O_NOFOLLOW);
    if (fd < 0)
        return out;
    struct stat sb;
    bool ok = fstat(fd, &sb) == 0 && S_ISREG(sb.st_mode);
    std::string body(ok ? kSteamTsvCap : 0, '\0');
    size_t got = 0;
    while (got < body.size()) {
        ssize_t r = read(fd, &body[got], body.size() - got);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            break;
        got += (size_t)r;
    }
    close(fd);
    body.resize(got);
    size_t end = body.size() < kSteamTsvCap ? body.size() : body.rfind('\n') + 1;
    std::vector<std::string> lines;
    for (size_t i = 0; i < end;) {
        size_t nl = body.find('\n', i);
        if (nl == std::string::npos)
            nl = end;
        lines.push_back(body.substr(i, nl - i));
        i = nl + 1;
    }
    if (lines.empty() || (lines[0] != "signed\t0" && lines[0] != "signed\t1"))
        return out;
    out.signed_in = lines[0] == "signed\t1";
    bool bound = false;
    for (size_t k = 1; k < lines.size(); k++) {
        const std::string& l = lines[k];
        size_t a = l.find('\t'), b = a == std::string::npos ? a : l.find('\t', a + 1);
        if (l.compare(0, a, "bound") == 0) {
            std::string v = a == std::string::npos ? std::string() : l.substr(a + 1);
            bool num = v.size() <= 12 && v.find_first_not_of("0123456789") == std::string::npos;
            if (!bound && num)
                out.bound_until = std::strtoll(v.c_str(), nullptr, 10);
            bound = true;
            continue;
        }
        if (b == std::string::npos || l.compare(0, a, "port") != 0)
            continue;
        std::string name = l.substr(a + 1, b - a - 1), state = l.substr(b + 1);
        if (!name.empty() && name.size() <= kNameCap && (state == "get" || state == "got"))
            out.ports[name] = state == "got";
    }
    return out;
}

std::vector<StoreItem> store_items(const Catalog* cat, const std::vector<IndexPort>& index)
{
    std::vector<StoreItem> out;
    std::vector<bool> used(index.size(), false);
    auto script_of = [&](const std::string& dir, const std::string& title, StoreItem& it) {
        for (size_t k = 0; k < index.size(); k++) {
            const IndexPort& ip = index[k];
            bool match = !ip.dir.empty() ? leaf(ip.dir) == dir : ip.title == title;
            if (!match)
                continue;
            used[k] = true;
            if (it.game_id < 0) {
                it.game_id = ip.game_id;
                it.script = ip.base;
                if (it.cover.empty())
                    it.cover = ip.cover;
            }
        }
    };
    if (cat)
        for (size_t i = 0; i < cat->ports.size(); i++) {
            const StorePort& p = cat->ports[i];
            StoreItem it;
            it.port = (int)i;
            it.name = p.name;
            it.title = p.title;
            it.porter = p.porter;
            it.description = p.description;
            it.inst = p.inst;
            it.genres = p.genres;
            it.size = p.size;
            it.shot = p.screenshot;
            it.cover = p.cover;
            script_of(stem(p.name), p.title, it);
            it.installed = it.game_id >= 0;
            it.update = it.installed && p.update;
            out.push_back(std::move(it));
        }
    for (size_t k = 0; k < index.size(); k++) {
        if (used[k])
            continue;
        const IndexPort& ip = index[k];
        bool same = false;
        for (StoreItem& it : out)
            if (it.port < 0 && !ip.dir.empty() && it.name == leaf(ip.dir))
                same = true;
        if (same)
            continue;
        StoreItem it;
        it.name = ip.dir.empty() ? ip.base : leaf(ip.dir);
        it.title = ip.title.empty() ? ip.base : ip.title;
        it.cover = ip.cover;
        it.script = ip.base;
        it.game_id = ip.game_id;
        it.installed = true;
        out.push_back(std::move(it));
    }
    std::sort(out.begin(), out.end(), by_title);
    for (StoreItem& it : out)
        it.id = kStoreIdBase + (int64_t)(std::hash<std::string>{}(it.name) & (uint64_t)(kStoreIdBase - 1));
    return out;
}

std::vector<StoreGenre> store_genres(const std::vector<StoreItem>& items, bool catalog)
{
    std::vector<StoreGenre> out;
    StoreGenre all{kStoreAll, {}}, inst{kStoreInstalled, {}}, upd{kStoreUpdates, {}};
    for (int i = 0; i < (int)items.size(); i++) {
        if (items[i].port >= 0)
            all.items.push_back(i);
        if (items[i].installed)
            inst.items.push_back(i);
        if (items[i].update)
            upd.items.push_back(i);
    }
    if (catalog)
        out.push_back(std::move(all));
    out.push_back(std::move(inst));
    if (catalog && !upd.items.empty())
        out.push_back(std::move(upd));
    if (!catalog)
        return out;
    for (const char* g : kHmGenres) {
        StoreGenre sg{g, {}};
        for (int i = 0; i < (int)items.size(); i++)
            if (items[i].port >= 0 && has_genre(items[i], g))
                sg.items.push_back(i);
        if (!sg.items.empty())
            out.push_back(std::move(sg));
    }
    return out;
}

std::vector<std::string> body_lines(const std::string& text, int avail64,
                                    const std::function<int(const std::string&)>& measure)
{
    std::vector<std::string> out;
    size_t p = 0;
    for (;;) {
        size_t nl = text.find('\n', p);
        std::string par = text.substr(p, nl == std::string::npos ? std::string::npos : nl - p);
        std::vector<std::string> toks;
        std::string word;
        for (size_t i = 0; i < par.size();) {
            size_t at = i;
            uint32_t cp = decode(par, i);
            if (cp == ' ') {
                if (!word.empty())
                    toks.push_back(word);
                word.clear();
                toks.push_back(" ");
            } else if (cjk(cp)) {
                if (!word.empty())
                    toks.push_back(word);
                word.clear();
                toks.push_back(par.substr(at, i - at));
            } else {
                word += par.substr(at, i - at);
            }
        }
        if (!word.empty())
            toks.push_back(word);
        std::string cur;
        for (const std::string& tk : toks) {
            if (tk == " ") {
                if (!cur.empty())
                    cur += " ";
                continue;
            }
            if (measure(cur + tk) <= avail64) {
                cur += tk;
                continue;
            }
            while (!cur.empty() && cur.back() == ' ')
                cur.pop_back();
            if (!cur.empty())
                out.push_back(cur);
            cur.clear();
            for (size_t i = 0; i < tk.size();) {
                size_t at = i;
                decode(tk, i);
                std::string ch = tk.substr(at, i - at);
                if (!cur.empty() && measure(cur + ch) > avail64) {
                    out.push_back(cur);
                    cur.clear();
                }
                cur += ch;
            }
        }
        while (!cur.empty() && cur.back() == ' ')
            cur.pop_back();
        out.push_back(cur);
        if (nl == std::string::npos)
            return out;
        p = nl + 1;
    }
}

std::string join_words(const std::vector<std::string>& v)
{
    std::string out;
    for (const std::string& s : v) {
        if (!out.empty())
            out += ", ";
        out += s;
    }
    return out;
}

std::string size_words(int64_t bytes)
{
    constexpr int64_t kKb = 1024, kMb = kKb * 1024, kGb = kMb * 1024;
    char b[32];
    if (bytes <= 0)
        return {};
    if (bytes >= kGb)
        snprintf(b, sizeof b, "%.1f gb", (double)bytes / (double)kGb);
    else if (bytes >= kMb)
        snprintf(b, sizeof b, "%lld mb", (long long)((bytes + kMb - 1) / kMb));
    else
        snprintf(b, sizeof b, "%lld kb", (long long)((bytes + kKb - 1) / kKb));
    return b;
}

std::string count_words(int n)
{
    char b[32];
    snprintf(b, sizeof b, n == 1 ? "%d port" : "%d ports", n);
    return b;
}

std::string progress_words(const HelperProgress& p)
{
    if (p.what == "wait")
        return "waiting";
    if (p.what == "login")
        return p.total > 0 ? "signing in " + std::to_string(std::max<int64_t>(0, p.total - p.done)) : "signing in";
    if (p.what == "check")
        return "checking";
    if (p.what == "copy")
        return "copying";
    if (p.what == "unpack")
        return "unpacking";
    if (p.what == "install")
        return "installing";
    if (p.what == "remove")
        return "removing";
    if ((p.what != "download" && p.what != "runtime") || p.total <= 0)
        return p.what;
    std::string words = (p.done > 0 ? size_words(p.done) : std::string("0 kb")) + " of " + size_words(p.total);
    return p.what == "runtime" ? "runtime " + words : words;
}

bool progress_known(const HelperProgress& p) { return p.total > 0 && p.what != "wait" && p.what != "copy"; }

int progress_arc(const HelperProgress& p) { return progress_known(p) ? ring_arc(p.done, p.total, 0).sweep : 0; }

std::vector<IndexPort> index_ports(Library& lib)
{
    std::vector<IndexPort> out;
    for (const Game& g : lib.games(kPortsSystem)) {
        if (!g.present)
            continue;
        IndexPort ip{g.id, g.name, g.title.empty() ? g.name : g.title, "", ""};
        for (const MetaField& m : lib.meta(g.id))
            if (m.field == "port")
                ip.dir = m.value;
        for (const MediaRef& m : lib.media(g.id))
            if (m.type == "covers")
                ip.cover = m.path;
        out.push_back(std::move(ip));
    }
    return out;
}

StoreToast store_toast(const HelperResult& r)
{
    StoreToast t;
    if (r.done || r.why == "cancel" || r.why == "unknown-runtime")
        return t;
    if (r.cmd == HelperCmd::SteamGet || r.cmd == HelperCmd::SteamLogout) {
        static const std::unordered_map<std::string, const char*> kSteamLine2 = {
            {"network", "download failed"}, {"space", "card full"},          {"owned", "not in your library"},
            {"expired", "sign in timed out"}, {"login", "sign in failed"},   {"exists", "files already there"},
            {"busy", "a game is running"},    {"nodata", "no card"},
        };
        bool get = r.cmd == HelperCmd::SteamGet;
        auto line2 = kSteamLine2.find(r.why);
        t.show = true;
        t.line1 = get ? "get files failed" : "sign out failed";
        t.line2 = get && line2 != kSteamLine2.end() ? line2->second : "";
        return t;
    }
    static const std::unordered_map<std::string, const char*> kLine2 = {
        {"network", "download failed"},  {"space", "card full"},     {"md5", "download damaged"},
        {"runtime", "runtime download failed"}, {"nodata", "no card"}, {"busy", "a game is running"},
    };
    const char* verb = r.cmd == HelperCmd::Install  ? "install failed"
                       : r.cmd == HelperCmd::Update ? "update failed"
                       : r.cmd == HelperCmd::Remove ? "remove failed"
                                                    : "refresh failed";
    auto it = kLine2.find(r.why);
    t.show = true;
    t.line1 = verb;
    t.line2 = it != kLine2.end() ? it->second : "";
    return t;
}

}
