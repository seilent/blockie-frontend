#pragma once
#include <cstdint>
#include <functional>
#include <string>

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
    Scraper(Library& lib, const std::string& data_root, Fetcher fetch = {});

    ArtOutcome art(int64_t game_id, const std::string& system_id, const std::string& libretro_folder,
                   const std::string& name, ArtType type);
    void scrape_game(const Game& g, const std::string& libretro_folder);

    static std::string url_for(const std::string& libretro_folder, const std::string& name, ArtType type);
    static std::string local_path(const std::string& data_root, const std::string& system_id,
                                  const std::string& name, ArtType type);

private:
    Library& lib_;
    std::string data_root_;
    Fetcher fetch_;
};

}
