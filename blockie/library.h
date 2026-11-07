#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace fd {

struct SystemCfg {
    std::string id, name, libretro_folder, core;
    std::vector<std::string> exts;
};

struct Game {
    int64_t id;
    std::string system_id, name, rompath_abs, m3u_path;
    bool is_multidisc;
    bool present;
};

struct MediaRef {
    std::string type, path, source;
};

struct MetaField {
    std::string field, value;
};

class Library {
public:
    Library() = default;
    ~Library();
    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;

    bool open(const std::string& data_root);
    int scan();
    std::vector<std::pair<SystemCfg, int>> systems_with_games();
    std::vector<Game> games(const std::string& system_id);
    std::vector<MediaRef> media(int64_t game_id);
    std::vector<MetaField> meta(int64_t game_id);
    void set_media(int64_t game_id, const std::string& type, const std::string& path, const std::string& source);
    const std::vector<SystemCfg>& config() const;

private:
    bool init_schema();
    void seed_systems();
    int scan_system(const SystemCfg& sys);
    int64_t upsert_game(const std::string& system_id, const std::string& name, const std::string& rompath_abs,
                        bool is_multidisc, const std::string& m3u_path);
    void seed_meta(int64_t game_id, const std::string& name, const std::string& system_id);

    sqlite3* db_ = nullptr;
    std::string data_root_;
    std::string roms_dir_;
    std::vector<SystemCfg> cfg_;
};

}
