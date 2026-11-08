#pragma once
#include <string>
#include <vector>

namespace fd {

struct ZoneEntry {
    std::string id;
    std::string words;
};

struct ZoneRegion {
    std::string name;
    std::string words;
    std::vector<ZoneEntry> zones;
};

struct Zones {
    std::vector<ZoneRegion> regions;
};

struct ZoneSpot {
    int region = -1;
    int zone = -1;
};

constexpr const char* kZoneTab = "/usr/share/zoneinfo/zone.tab";
constexpr const char* kUtcWords = "utc";

Zones parse_zones(const std::string& text);
Zones load_zones(const std::string& path);
bool zone_is_utc(const std::string& id);
ZoneSpot locate_zone(const Zones& z, const std::string& id);
std::string zone_words(const Zones& z, const std::string& id);

}
