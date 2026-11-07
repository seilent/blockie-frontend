#include "library.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <map>
#include <set>

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <sqlite3.h>

namespace fd {

namespace {

struct SysSeed {
    const char* id;
    const char* name;
    const char* libretro_folder;
    const char* core;
    const char* exts;
};

constexpr SysSeed kSystems[] = {
    {"gb", "GB", "Nintendo - Game Boy", "gambatte", ".gb .zip"},
    {"gbc", "GBC", "Nintendo - Game Boy Color", "gambatte", ".gbc .zip"},
    {"nes", "NES", "Nintendo - Nintendo Entertainment System", "nestopia", ".nes .zip"},
    {"snes", "SNES", "Nintendo - Super Nintendo Entertainment System", "snes9x", ".sfc .smc .zip"},
    {"genesis", "Genesis", "Sega - Mega Drive - Genesis", "genesis_plus_gx", ".md .gen .bin .zip"},
    {"gba", "GBA", "Nintendo - Game Boy Advance", "mgba", ".gba .zip"},
    {"psx", "PS1", "Sony - PlayStation", "pcsx_rearmed", ".cue .chd .m3u"},
    {"n64", "N64", "Nintendo - Nintendo 64", "mupen64plus_next", ".z64 .n64 .v64 .zip"},
    {"psp", "PSP", "Sony - PlayStation Portable", "ppsspp", ".iso .cso .chd .pbp"},
    {"dc", "DC", "Sega - Dreamcast", "flycast", ".chd .gdi .m3u"},
};

std::string lower(const std::string& s)
{
    std::string r(s);
    for (char& c : r)
        c = (char)std::tolower((unsigned char)c);
    return r;
}

std::vector<std::string> split_ws(const std::string& s)
{
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && s[i] == ' ')
            i++;
        size_t j = i;
        while (j < s.size() && s[j] != ' ')
            j++;
        if (j > i)
            out.push_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}

std::string basename_of(const std::string& path)
{
    size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool matches_ext(const std::string& lname, const std::vector<std::string>& exts, std::string& ext_out)
{
    for (const std::string& e : exts) {
        if (lname.size() > e.size() && lname.compare(lname.size() - e.size(), e.size(), e) == 0) {
            ext_out = e;
            return true;
        }
    }
    return false;
}

bool is_archive_ext(const std::string& ext) { return ext == ".zip" || ext == ".7z"; }

std::set<std::string> m3u_discs(const std::string& m3u_path)
{
    std::set<std::string> out;
    std::ifstream in(m3u_path);
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' '))
            line.pop_back();
        size_t start = line.find_first_not_of(' ');
        if (start == std::string::npos)
            continue;
        line = line.substr(start);
        if (line.empty() || line[0] == '#')
            continue;
        out.insert(basename_of(line));
    }
    return out;
}

void collect_eboot(const std::string& dir, const std::string& parent_name, int depth,
                   std::vector<std::pair<std::string, std::string>>& out)
{
    if (depth > 8)
        return;
    DIR* d = opendir(dir.c_str());
    if (!d)
        return;
    std::vector<std::string> subdirs;
    bool has_eboot = false;
    struct dirent* e;
    while ((e = readdir(d))) {
        std::string nm = e->d_name;
        if (nm == "." || nm == "..")
            continue;
        std::string full = dir + "/" + nm;
        struct stat sb;
        if (lstat(full.c_str(), &sb) != 0)
            continue;
        if (S_ISDIR(sb.st_mode)) {
            if (nm != "media")
                subdirs.push_back(nm);
        } else if (S_ISREG(sb.st_mode) && nm == "EBOOT.PBP") {
            has_eboot = true;
        }
    }
    closedir(d);
    if (has_eboot && !parent_name.empty()) {
        std::string eboot = dir + "/EBOOT.PBP";
        if (eboot.find("/PSP/GAME/") == std::string::npos)
            out.emplace_back(parent_name, eboot);
    }
    std::sort(subdirs.begin(), subdirs.end());
    for (const std::string& sub : subdirs)
        collect_eboot(dir + "/" + sub, sub, depth + 1, out);
}

}

Library::~Library()
{
    if (db_)
        sqlite3_close(db_);
}

const std::vector<SystemCfg>& Library::config() const { return cfg_; }

bool Library::init_schema()
{
    const char* sql =
        "CREATE TABLE IF NOT EXISTS system(id TEXT PRIMARY KEY, name TEXT, folder TEXT, exts TEXT,"
        " libretro_folder TEXT, core TEXT);"
        "CREATE TABLE IF NOT EXISTS game(id INTEGER PRIMARY KEY, system_id TEXT, name TEXT, rompath_abs TEXT,"
        " is_multidisc INTEGER, m3u_path TEXT, added_at INTEGER, present INTEGER, last_played INTEGER);"
        "CREATE UNIQUE INDEX IF NOT EXISTS game_sys_name ON game(system_id, name);"
        "CREATE TABLE IF NOT EXISTS media(game_id INTEGER, type TEXT, path TEXT, source TEXT,"
        " UNIQUE(game_id, type));"
        "CREATE TABLE IF NOT EXISTS meta(game_id INTEGER, field TEXT, value TEXT, UNIQUE(game_id, field));";
    if (sqlite3_exec(db_, sql, nullptr, nullptr, nullptr) != SQLITE_OK)
        return false;
    sqlite3_exec(db_, "ALTER TABLE game ADD COLUMN last_played INTEGER;", nullptr, nullptr, nullptr);
    return true;
}

void Library::seed_systems()
{
    cfg_.clear();
    sqlite3_stmt* st = nullptr;
    const char* sql = "INSERT INTO system(id, name, folder, exts, libretro_folder, core)"
                      " VALUES(?,?,?,?,?,?)"
                      " ON CONFLICT(id) DO UPDATE SET name=excluded.name, folder=excluded.folder,"
                      " exts=excluded.exts, libretro_folder=excluded.libretro_folder, core=excluded.core;";
    sqlite3_prepare_v2(db_, sql, -1, &st, nullptr);
    for (const SysSeed& s : kSystems) {
        SystemCfg c;
        c.id = s.id;
        c.name = s.name;
        c.libretro_folder = s.libretro_folder;
        c.core = s.core;
        c.exts = split_ws(s.exts);
        cfg_.push_back(c);
        sqlite3_bind_text(st, 1, s.id, -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 2, s.name, -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 3, s.id, -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 4, s.exts, -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 5, s.libretro_folder, -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 6, s.core, -1, SQLITE_STATIC);
        sqlite3_step(st);
        sqlite3_reset(st);
    }
    sqlite3_finalize(st);
}

bool Library::open(const std::string& data_root)
{
    std::string root = data_root;
    if (root.empty()) {
        const char* env = std::getenv("BLOCKIE_DATA");
        root = env && *env ? env : "/var/lib/blockie/data";
    }
    while (root.size() > 1 && root.back() == '/')
        root.pop_back();
    data_root_ = root;
    roms_dir_ = root + "/roms";
    std::string cache = root + "/cache";
    mkdir(cache.c_str(), 0755);
    std::string db_path = cache + "/library.db";
    if (sqlite3_open(db_path.c_str(), &db_) != SQLITE_OK) {
        if (db_)
            sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL; PRAGMA foreign_keys=ON; PRAGMA busy_timeout=2000;",
                 nullptr, nullptr, nullptr);
    if (!init_schema())
        return false;
    seed_systems();
    return true;
}

int64_t Library::upsert_game(const std::string& system_id, const std::string& name, const std::string& rompath_abs,
                             bool is_multidisc, const std::string& m3u_path)
{
    sqlite3_stmt* st = nullptr;
    const char* sql = "INSERT INTO game(system_id, name, rompath_abs, is_multidisc, m3u_path, added_at, present)"
                      " VALUES(?,?,?,?,?,?,1)"
                      " ON CONFLICT(system_id, name) DO UPDATE SET rompath_abs=excluded.rompath_abs,"
                      " is_multidisc=excluded.is_multidisc, m3u_path=excluded.m3u_path, present=1;";
    sqlite3_prepare_v2(db_, sql, -1, &st, nullptr);
    sqlite3_bind_text(st, 1, system_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, rompath_abs.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 4, is_multidisc ? 1 : 0);
    sqlite3_bind_text(st, 5, m3u_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 6, (sqlite3_int64)std::time(nullptr));
    sqlite3_step(st);
    sqlite3_finalize(st);

    int64_t id = -1;
    sqlite3_stmt* sel = nullptr;
    sqlite3_prepare_v2(db_, "SELECT id FROM game WHERE system_id=? AND name=?;", -1, &sel, nullptr);
    sqlite3_bind_text(sel, 1, system_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(sel, 2, name.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(sel) == SQLITE_ROW)
        id = sqlite3_column_int64(sel, 0);
    sqlite3_finalize(sel);
    return id;
}

void Library::seed_meta(int64_t game_id, const std::string& name, const std::string& system_id)
{
    sqlite3_stmt* st = nullptr;
    const char* sql = "INSERT INTO meta(game_id, field, value) VALUES(?,?,?)"
                      " ON CONFLICT(game_id, field) DO UPDATE SET value=excluded.value;";
    const char* fields[2] = {"name", "system"};
    const std::string values[2] = {name, system_id};
    for (int i = 0; i < 2; i++) {
        sqlite3_prepare_v2(db_, sql, -1, &st, nullptr);
        sqlite3_bind_int64(st, 1, game_id);
        sqlite3_bind_text(st, 2, fields[i], -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 3, values[i].c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(st);
        sqlite3_finalize(st);
    }
}

int Library::scan_system(const SystemCfg& sys)
{
    std::string dir = roms_dir_ + "/" + sys.id;
    DIR* d = opendir(dir.c_str());
    if (!d)
        return 0;

    std::vector<std::string> files;
    struct dirent* e;
    while ((e = readdir(d))) {
        std::string nm = e->d_name;
        if (nm == "." || nm == ".." || nm == "media")
            continue;
        std::string full = dir + "/" + nm;
        struct stat sb;
        if (stat(full.c_str(), &sb) != 0 || !S_ISREG(sb.st_mode))
            continue;
        files.push_back(nm);
    }
    closedir(d);
    std::sort(files.begin(), files.end());

    struct Cand {
        std::string path;
        bool is_archive;
        bool is_m3u;
    };
    std::map<std::string, Cand> byname;
    std::set<std::string> absorbed;

    for (const std::string& nm : files) {
        std::string ext;
        if (!matches_ext(lower(nm), sys.exts, ext) || ext != ".m3u")
            continue;
        std::string full = dir + "/" + nm;
        std::set<std::string> discs = m3u_discs(full);
        absorbed.insert(discs.begin(), discs.end());
        std::string base = nm.substr(0, nm.size() - ext.size());
        if (!byname.count(base))
            byname[base] = Cand{full, false, true};
    }

    for (const std::string& nm : files) {
        std::string ext;
        if (!matches_ext(lower(nm), sys.exts, ext) || ext == ".m3u")
            continue;
        if (absorbed.count(nm))
            continue;
        std::string base = nm.substr(0, nm.size() - ext.size());
        std::string full = dir + "/" + nm;
        bool arch = is_archive_ext(ext);
        auto it = byname.find(base);
        if (it == byname.end()) {
            byname[base] = Cand{full, arch, false};
        } else if (!it->second.is_m3u && it->second.is_archive && !arch) {
            it->second = Cand{full, arch, false};
        }
    }

    if (sys.id == "psp") {
        std::vector<std::pair<std::string, std::string>> eboots;
        collect_eboot(dir, std::string(), 0, eboots);
        for (const auto& ng : eboots)
            if (!byname.count(ng.first))
                byname[ng.first] = Cand{ng.second, false, false};
    }

    for (const auto& kv : byname) {
        const Cand& c = kv.second;
        int64_t id = upsert_game(sys.id, kv.first, c.path, c.is_m3u, c.is_m3u ? c.path : std::string());
        if (id >= 0)
            seed_meta(id, kv.first, sys.id);
    }
    return (int)byname.size();
}

int Library::scan()
{
    if (!db_)
        return 0;
    sqlite3_exec(db_, "BEGIN;", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "UPDATE game SET present=0;", nullptr, nullptr, nullptr);
    for (const SystemCfg& sys : cfg_)
        scan_system(sys);
    sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);

    int n = 0;
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM game WHERE present=1;", -1, &st, nullptr);
    if (sqlite3_step(st) == SQLITE_ROW)
        n = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return n;
}

std::vector<std::pair<SystemCfg, int>> Library::systems_with_games()
{
    std::vector<std::pair<SystemCfg, int>> out;
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM game WHERE system_id=? AND present=1;", -1, &st, nullptr);
    for (const SystemCfg& sys : cfg_) {
        sqlite3_reset(st);
        sqlite3_bind_text(st, 1, sys.id.c_str(), -1, SQLITE_TRANSIENT);
        int n = 0;
        if (sqlite3_step(st) == SQLITE_ROW)
            n = sqlite3_column_int(st, 0);
        if (n > 0)
            out.emplace_back(sys, n);
    }
    sqlite3_finalize(st);
    return out;
}

std::vector<Game> Library::games(const std::string& system_id)
{
    std::vector<Game> out;
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
                       "SELECT id, system_id, name, rompath_abs, is_multidisc, m3u_path, present"
                       " FROM game WHERE system_id=? ORDER BY name;",
                       -1, &st, nullptr);
    sqlite3_bind_text(st, 1, system_id.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW) {
        Game g;
        g.id = sqlite3_column_int64(st, 0);
        g.system_id = (const char*)sqlite3_column_text(st, 1);
        g.name = (const char*)sqlite3_column_text(st, 2);
        g.rompath_abs = (const char*)sqlite3_column_text(st, 3);
        g.is_multidisc = sqlite3_column_int(st, 4) != 0;
        const unsigned char* m3u = sqlite3_column_text(st, 5);
        g.m3u_path = m3u ? (const char*)m3u : "";
        g.present = sqlite3_column_int(st, 6) != 0;
        out.push_back(std::move(g));
    }
    sqlite3_finalize(st);
    return out;
}

std::vector<Game> Library::recent_games(int limit)
{
    std::vector<Game> out;
    if (limit <= 0)
        return out;
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
                       "SELECT id, system_id, name, rompath_abs, is_multidisc, m3u_path, present"
                       " FROM game WHERE present=1"
                       " ORDER BY last_played IS NULL, last_played DESC, added_at DESC, id DESC LIMIT ?;",
                       -1, &st, nullptr);
    sqlite3_bind_int(st, 1, limit);
    while (sqlite3_step(st) == SQLITE_ROW) {
        Game g;
        g.id = sqlite3_column_int64(st, 0);
        g.system_id = (const char*)sqlite3_column_text(st, 1);
        g.name = (const char*)sqlite3_column_text(st, 2);
        g.rompath_abs = (const char*)sqlite3_column_text(st, 3);
        g.is_multidisc = sqlite3_column_int(st, 4) != 0;
        const unsigned char* m3u = sqlite3_column_text(st, 5);
        g.m3u_path = m3u ? (const char*)m3u : "";
        g.present = sqlite3_column_int(st, 6) != 0;
        out.push_back(std::move(g));
    }
    sqlite3_finalize(st);
    return out;
}

void Library::mark_played(int64_t game_id, int64_t when)
{
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "UPDATE game SET last_played=? WHERE id=?;", -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, when);
    sqlite3_bind_int64(st, 2, game_id);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

std::vector<MediaRef> Library::media(int64_t game_id)
{
    std::vector<MediaRef> out;
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "SELECT type, path, source FROM media WHERE game_id=? ORDER BY type;", -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, game_id);
    while (sqlite3_step(st) == SQLITE_ROW) {
        MediaRef m;
        m.type = (const char*)sqlite3_column_text(st, 0);
        m.path = (const char*)sqlite3_column_text(st, 1);
        m.source = (const char*)sqlite3_column_text(st, 2);
        out.push_back(std::move(m));
    }
    sqlite3_finalize(st);
    return out;
}

std::vector<MetaField> Library::meta(int64_t game_id)
{
    std::vector<MetaField> out;
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "SELECT field, value FROM meta WHERE game_id=? ORDER BY field;", -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, game_id);
    while (sqlite3_step(st) == SQLITE_ROW) {
        MetaField f;
        f.field = (const char*)sqlite3_column_text(st, 0);
        f.value = (const char*)sqlite3_column_text(st, 1);
        out.push_back(std::move(f));
    }
    sqlite3_finalize(st);
    return out;
}

void Library::set_media(int64_t game_id, const std::string& type, const std::string& path, const std::string& source)
{
    sqlite3_stmt* st = nullptr;
    const char* sql = "INSERT INTO media(game_id, type, path, source) VALUES(?,?,?,?)"
                      " ON CONFLICT(game_id, type) DO UPDATE SET path=excluded.path, source=excluded.source;";
    sqlite3_prepare_v2(db_, sql, -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, game_id);
    sqlite3_bind_text(st, 2, type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, source.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

}
