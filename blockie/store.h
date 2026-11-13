#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "ports.h"

namespace fd {

constexpr const char* kCatalogDb = "/.blockie/catalog.db";
constexpr const char* kSteamTsv = "/.blockie/steam.tsv";
constexpr const char* kStoreImages = "/PortMaster/config/images_";
constexpr int kCatalogVersion = 1;
constexpr size_t kNameCap = 128;
constexpr size_t kTextCap = 256;
constexpr size_t kBodyCap = 64u << 10;
constexpr size_t kSteamTsvCap = 64u << 10;
constexpr int64_t kStoreIdBase = int64_t(1) << 40;
constexpr int64_t kStoreShotBase = int64_t(2) << 40;
constexpr int64_t kCatalogStaleS = 24 * 3600;

constexpr int kStorePreviewTiles = 8;
constexpr int kStorePreviewRestMs = 500;
constexpr const char* kStoreAll = "all";
constexpr const char* kStoreInstalled = "installed";
constexpr const char* kStoreUpdates = "updates";
constexpr const char* kHmGenres[] = {"action", "adventure", "arcade",     "casino/card",  "fps",
                                     "platformer", "puzzle", "racing", "rhythm", "rpg",
                                     "simulation", "sports", "strategy", "visual novel", "other"};

struct StorePort {
    std::string name, title, porter, description, inst;
    std::vector<std::string> genres;
    int64_t size = 0;
    bool update = false;
    std::string screenshot, cover;
};

struct Catalog {
    int64_t gen = 0;
    std::vector<StorePort> ports;
};

struct IndexPort {
    int64_t game_id = 0;
    std::string base, title, dir, cover;
};

struct StoreItem {
    int64_t id = 0;
    int port = -1;
    std::string name, title, porter, description, inst, script, shot, cover;
    std::vector<std::string> genres;
    int64_t size = 0;
    int64_t game_id = -1;
    bool installed = false, update = false;
    bool steam = false;
};

struct StoreGenre {
    std::string word;
    std::vector<int> items;
};

struct SteamState {
    bool signed_in = false;
    std::unordered_map<std::string, bool> ports;
};

std::string cap_text(const std::string& s, size_t cap);
std::string cap_body(const std::string& s, size_t cap);
std::vector<std::string> cap_list(const std::string& s, size_t cap);
std::string images_base(const std::string& ports_dir);
std::string confined_in(const std::string& path, const std::string& base);
std::string confined_image(const std::string& path, const std::string& ports_dir);
bool read_catalog(const std::string& path, const std::string& ports_dir, Catalog& out);
SteamState read_steam(const std::string& path);

class Library;
std::vector<IndexPort> index_ports(Library& lib);
std::vector<StoreItem> store_items(const Catalog* cat, const std::vector<IndexPort>& index);
std::vector<StoreGenre> store_genres(const std::vector<StoreItem>& items, bool catalog);

std::vector<std::string> body_lines(const std::string& text, int avail64,
                                    const std::function<int(const std::string&)>& measure);
std::string join_words(const std::vector<std::string>& v);
std::string size_words(int64_t bytes);
std::string count_words(int n);
std::string progress_words(const HelperProgress& p);
bool progress_known(const HelperProgress& p);
int progress_arc(const HelperProgress& p);

struct StoreToast {
    bool show = false;
    std::string line1, line2;
};

StoreToast store_toast(const HelperResult& r);

}
