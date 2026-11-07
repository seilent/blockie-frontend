#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "library.h"

namespace fd {

enum class ArtType { Covers, Snaps, Titles };

struct HttpResult {
    int status;
    std::string body;
    bool transport_error;
};

using Fetcher = std::function<HttpResult(const std::string& url)>;

struct ArtOutcome {
    bool cached;
    bool not_found;
    bool transient;
    std::string path;
};

class Scraper {
public:
    Scraper(Library& lib, const std::string& data_root, Fetcher fetch = {}, const std::atomic<bool>* abort = nullptr);

    ArtOutcome art(int64_t game_id, const std::string& system_id, const std::string& libretro_folder,
                   const std::string& name, ArtType type);
    void scrape_game(const Game& g, const std::string& libretro_folder);
    std::string resolve_name(const std::string& system_id, const std::string& rompath);

    static std::string url_for(const std::string& libretro_folder, const std::string& name, ArtType type);
    static std::string local_path(const std::string& data_root, const std::string& system_id,
                                  const std::string& name, ArtType type);
    static std::optional<uint32_t> rom_crc(const std::string& path, const std::vector<std::string>& exts);
    static std::string sha256_hex(const std::string& data);

private:
    const std::unordered_map<uint32_t, std::string>* dat_index(const std::string& system_id,
                                                               const std::string& basename);

    Library& lib_;
    std::string data_root_;
    Fetcher fetch_;
    std::unordered_map<std::string, std::unordered_map<uint32_t, std::string>> dat_cache_;
    std::unordered_set<std::string> dat_tried_;
};

}
