#include "scraper.h"

#include <cstdio>

#include <sys/stat.h>
#include <sys/types.h>

#include <curl/curl.h>

namespace fd {

namespace {

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

size_t curl_write(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    std::string* body = static_cast<std::string*>(userdata);
    body->append(ptr, size * nmemb);
    return size * nmemb;
}

void ensure_global()
{
    static bool done = [] {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        return true;
    }();
    (void)done;
}

HttpResult curl_fetch(const std::string& url)
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
    curl_easy_setopt(h, CURLOPT_USERAGENT, "blockie-frontend");
    curl_easy_setopt(h, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
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

}

Scraper::Scraper(Library& lib, const std::string& data_root, Fetcher fetch)
    : lib_(lib), data_root_(data_root), fetch_(std::move(fetch))
{
    while (data_root_.size() > 1 && data_root_.back() == '/')
        data_root_.pop_back();
    if (!fetch_)
        fetch_ = curl_fetch;
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

ArtOutcome Scraper::art(int64_t game_id, const std::string& system_id, const std::string& libretro_folder,
                        const std::string& name, ArtType type)
{
    std::string path = local_path(data_root_, system_id, name, type);
    if (file_exists(path)) {
        lib_.set_media(game_id, dir_name(type), path, "local");
        return {true, false, false, path};
    }

    HttpResult r = fetch_(url_for(libretro_folder, name, type));
    if (r.transport_error || r.status >= 500) {
        return {false, false, true, std::string()};
    }
    if (r.status == 404) {
        return {false, true, false, std::string()};
    }
    if (r.status == 200) {
        if (!write_body(path, r.body))
            return {false, false, true, std::string()};
        lib_.set_media(game_id, dir_name(type), path, "libretro");
        return {true, false, false, path};
    }
    return {false, false, true, std::string()};
}

void Scraper::scrape_game(const Game& g, const std::string& libretro_folder)
{
    art(g.id, g.system_id, libretro_folder, g.name, ArtType::Covers);
    art(g.id, g.system_id, libretro_folder, g.name, ArtType::Snaps);
    art(g.id, g.system_id, libretro_folder, g.name, ArtType::Titles);
}

}
