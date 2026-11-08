#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <unistd.h>

#include "../zones.h"

using namespace fd;

namespace {

int g_fail = 0;
int g_total = 0;

void check(bool ok, const char* what)
{
    g_total++;
    if (ok) {
        std::printf("PASS %s\n", what);
    } else {
        std::printf("FAIL %s\n", what);
        g_fail++;
    }
}

const char* const kFixture =
    "# tzdb timezone descriptions\n"
    "#\n"
    "#country-code\tcoordinates\tTZ\tcomments\n"
    "AR\t-3436-05827\tAmerica/Argentina/Buenos_Aires\tBuenos Aires (BA, CF)\n"
    "HT\t+1832-07220\tAmerica/Port-au-Prince\n"
    "TT\t+1039-06131\tAmerica/Port_of_Spain\n"
    "US\t+470659-1011757\tAmerica/North_Dakota/New_Salem\tCentral - ND (Morton rural)\n"
    "AQ\t-6640+14001\tAntarctica/DumontDUrville\tDumont-d'Urville\n"
    "\n"
    "SJ\t+7800+01600\tArctic/Longyearbyen\n"
    "JP\t+353916+1394441\tAsia/Tokyo\n"
    "GB\t+513030-0000731\tEurope/London\n"
    "FR\t+4852+00220\tEurope/Paris\n"
    "JP\t+353916+1394441\tAsia/Tokyo\n";

std::vector<std::string> region_words(const Zones& z)
{
    std::vector<std::string> v;
    for (const ZoneRegion& r : z.regions)
        v.push_back(r.words);
    return v;
}

std::vector<std::string> zone_list(const Zones& z, int r)
{
    std::vector<std::string> v;
    for (const ZoneEntry& e : z.regions[(size_t)r].zones)
        v.push_back(e.words);
    return v;
}

std::string write_tmp(const std::string& text)
{
    const char* dir = std::getenv("TMPDIR");
    std::string tmpl = std::string(dir && *dir ? dir : "/tmp") + "/blockie-zones-XXXXXX";
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    int fd = mkstemp(buf.data());
    if (fd < 0)
        return {};
    std::string path = buf.data();
    if (write(fd, text.data(), text.size()) != (ssize_t)text.size()) {
        close(fd);
        unlink(path.c_str());
        return {};
    }
    close(fd);
    return path;
}

}

int main()
{
    Zones z = parse_zones(kFixture);
    check(region_words(z) == std::vector<std::string>({"america", "antarctica", "arctic", "asia", "europe"}),
          "comments and blank lines skipped, regions sorted");
    check(z.regions.size() == 5 && z.regions[0].name == "America" && z.regions[3].name == "Asia",
          "a region keeps its zone.tab name");
    check(zone_list(z, 0) == std::vector<std::string>({"argentina / buenos aires", "north dakota / new salem",
                                                       "port of spain", "port-au-prince"}),
          "three part words, _ to a space, case folded ascii order");
    check(zone_list(z, 1) == std::vector<std::string>({"dumontdurville"}), "a zone keeps its letters lowercased");
    check(zone_list(z, 2) == std::vector<std::string>({"longyearbyen"}),
          "arctic holds its one zone, the region part is not in its words");
    check(z.regions[3].zones.size() == 1 && z.regions[3].zones[0].id == "Asia/Tokyo", "a repeated id is listed once");

    ZoneSpot tokyo = locate_zone(z, "Asia/Tokyo");
    check(tokyo.region == 3 && tokyo.zone == 0, "Asia/Tokyo is located");
    ZoneSpot salem = locate_zone(z, "America/North_Dakota/New_Salem");
    check(salem.region == 0 && salem.zone == 1, "a three part id is located");
    ZoneSpot utc = locate_zone(z, "UTC"), etc = locate_zone(z, "Etc/UTC");
    check(utc.region == 5 && utc.zone == -1 && etc.region == 5 && etc.zone == -1,
          "UTC and Etc/UTC sit on the utc row after the regions");
    ZoneSpot none = locate_zone(z, "America/Argentina/ComodRivadavia");
    check(none.region == -1 && none.zone == -1, "an unlisted id in a listed region is not located");
    check(locate_zone(z, "Japan").region == -1 && locate_zone(z, "").region == -1 &&
              locate_zone(z, "Etc/GMT-9").region == -1,
          "ids outside the regions are not located");

    check(zone_words(z, "Asia/Tokyo") == "tokyo" &&
              zone_words(z, "America/Argentina/Buenos_Aires") == "argentina / buenos aires",
          "a listed zone shows its words");
    check(zone_words(z, "UTC") == "utc" && zone_words(z, "Etc/UTC") == "utc", "UTC and Etc/UTC read utc");
    check(zone_words(z, "Etc/GMT-9") == "etc / gmt-9" && zone_words(z, "Japan") == "japan" &&
              zone_words(z, "America/Argentina/ComodRivadavia") == "america / argentina / comodrivadavia",
          "an unlisted id keeps its first part");
    check(zone_words(z, "").empty(), "an empty id has no words");

    std::string path = write_tmp(kFixture);
    check(!path.empty(), "fixture written");
    Zones loaded = load_zones(path);
    check(region_words(loaded) == region_words(z) && zone_list(loaded, 0) == zone_list(z, 0),
          "loading the file gives the parsed zones");
    if (!path.empty())
        unlink(path.c_str());
    Zones gone = load_zones(path + ".missing");
    check(gone.regions.empty() && locate_zone(gone, "UTC").region == 0 &&
              zone_words(gone, "Asia/Tokyo") == "asia / tokyo",
          "a missing file gives only utc");
    check(parse_zones("XX\t+0000+00000\t\n\tbad\nYY\t+0000+00000\tNoSlash\n").regions.empty(),
          "a line with no id or no region adds nothing");
    Zones cased = parse_zones("XX\t+0000+00000\tSynth/AAc\nXX\t+0000+00000\tSynth/Aab\n");
    check(cased.regions.size() == 1 && zone_list(cased, 0) == std::vector<std::string>({"aab", "aac"}),
          "case folded order: Synth/Aab before Synth/AAc, which byte order would swap");
    Zones odd = parse_zones("XX\t+0000+00000\tSynth/Bad Zone\nXX\t+0000+00000\tSynth/Ok_Zone+1\n"
                            "XX\t+0000+00000\tSynth/../x\nXX\t+0000+00000\tSynth/Caf\xc3\xa9\n");
    check(odd.regions.size() == 1 && zone_list(odd, 0) == std::vector<std::string>({"ok zone+1"}),
          "an id with a character outside A-Za-z0-9_+-/ is skipped");
    Zones twins = parse_zones("XX\t+0000+00000\tSynth/B\nXX\t+0000+00000\tSYNTH/A\n");
    check(twins.regions.size() == 2 && twins.regions[0].name == "SYNTH" && twins.regions[1].name == "Synth",
          "two regions with the same words sort by their names");
    std::string big(1 << 20, '#');
    for (size_t i = 79; i < big.size(); i += 80)
        big[i] = '\n';
    std::string past = write_tmp(big + "\nJP\t+353916+1394441\tAsia/Tokyo\n");
    check(!past.empty() && load_zones(past).regions.empty(), "a zone past the first 1 MiB is not read");
    if (!past.empty())
        unlink(past.c_str());
    size_t cut = (1 << 20) - 27;
    std::string filler;
    while (filler.size() + 80 < cut)
        filler += std::string(79, '#') + "\n";
    filler += std::string(cut - filler.size() - 1, '#') + "\n";
    std::string straddle = write_tmp(filler + "JP\t+353916+1394441\tAsia/Tokyo\n");
    check(!straddle.empty() && filler.size() == cut && load_zones(straddle).regions.empty(),
          "a line cut by the 1 MiB cap is dropped, not read as Asia/Tok");
    if (!straddle.empty())
        unlink(straddle.c_str());

    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
