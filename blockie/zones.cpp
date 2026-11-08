#include "zones.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace fd {

namespace {

constexpr size_t kZoneTabMax = 1 << 20;

bool zone_chars(const std::string& id)
{
    for (char ch : id)
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' ||
              ch == '+' || ch == '-' || ch == '/'))
            return false;
    return true;
}

std::string words_of(const std::string& path)
{
    std::string out;
    for (char ch : path) {
        if (ch == '/')
            out += " / ";
        else if (ch == '_')
            out += ' ';
        else if (ch >= 'A' && ch <= 'Z')
            out += (char)(ch - 'A' + 'a');
        else
            out += ch;
    }
    return out;
}

std::string trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \r");
    if (a == std::string::npos)
        return "";
    size_t b = s.find_last_not_of(" \r");
    return s.substr(a, b - a + 1);
}

}

Zones parse_zones(const std::string& text)
{
    Zones z;
    std::unordered_set<std::string> seen;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        size_t a = line.find('\t');
        size_t b = a == std::string::npos ? a : line.find('\t', a + 1);
        if (b == std::string::npos)
            continue;
        size_t c = line.find('\t', b + 1);
        std::string id = trim(line.substr(b + 1, c == std::string::npos ? std::string::npos : c - b - 1));
        size_t slash = id.find('/');
        if (slash == std::string::npos || slash == 0 || slash + 1 == id.size() || !zone_chars(id) ||
            !seen.insert(id).second)
            continue;
        std::string name = id.substr(0, slash);
        auto it = std::find_if(z.regions.begin(), z.regions.end(),
                               [&](const ZoneRegion& r) { return r.name == name; });
        if (it == z.regions.end()) {
            z.regions.push_back({name, words_of(name), {}});
            it = z.regions.end() - 1;
        }
        it->zones.push_back({id, words_of(id.substr(slash + 1))});
    }
    std::sort(z.regions.begin(), z.regions.end(), [](const ZoneRegion& x, const ZoneRegion& y) {
        return x.words != y.words ? x.words < y.words : x.name < y.name;
    });
    for (ZoneRegion& r : z.regions)
        std::sort(r.zones.begin(), r.zones.end(), [](const ZoneEntry& x, const ZoneEntry& y) {
            return x.words != y.words ? x.words < y.words : x.id < y.id;
        });
    return z;
}

Zones load_zones(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    std::string text(kZoneTabMax, '\0');
    in.read(&text[0], (std::streamsize)text.size());
    text.resize((size_t)in.gcount());
    if (text.size() == kZoneTabMax) {
        size_t nl = text.rfind('\n');
        text.resize(nl == std::string::npos ? 0 : nl + 1);
    }
    return parse_zones(text);
}

bool zone_is_utc(const std::string& id) { return id == "UTC" || id == "Etc/UTC"; }

ZoneSpot locate_zone(const Zones& z, const std::string& id)
{
    if (zone_is_utc(id))
        return {(int)z.regions.size(), -1};
    size_t slash = id.find('/');
    if (slash == std::string::npos)
        return {};
    std::string name = id.substr(0, slash);
    for (size_t r = 0; r < z.regions.size(); r++) {
        if (z.regions[r].name != name)
            continue;
        const std::vector<ZoneEntry>& zs = z.regions[r].zones;
        for (size_t k = 0; k < zs.size(); k++)
            if (zs[k].id == id)
                return {(int)r, (int)k};
        return {};
    }
    return {};
}

std::string zone_words(const Zones& z, const std::string& id)
{
    if (zone_is_utc(id))
        return kUtcWords;
    ZoneSpot s = locate_zone(z, id);
    if (s.region >= 0 && s.zone >= 0)
        return z.regions[(size_t)s.region].zones[(size_t)s.zone].words;
    return words_of(id);
}

}
