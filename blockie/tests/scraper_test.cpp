#include <cstdint>
#include <cstdio>
#include <ctime>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <ftw.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

#include "../library.h"
#include "../scraper.h"

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

void mkdirs(const std::string& p)
{
    std::string cur;
    for (size_t i = 0; i < p.size(); i++) {
        cur += p[i];
        if (p[i] == '/' || i + 1 == p.size())
            mkdir(cur.c_str(), 0755);
    }
}

void write_file(const std::string& path, const std::string& body)
{
    size_t slash = path.find_last_of('/');
    if (slash != std::string::npos)
        mkdirs(path.substr(0, slash));
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f) {
        std::fwrite(body.data(), 1, body.size(), f);
        std::fclose(f);
    }
}

std::string read_file(const std::string& path)
{
    std::string out;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f)
        return out;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
        out.append(buf, n);
    std::fclose(f);
    return out;
}

int rm_cb(const char* path, const struct stat*, int, struct FTW*)
{
    remove(path);
    return 0;
}

void rm_rf(const std::string& p) { nftw(p.c_str(), rm_cb, 16, FTW_DEPTH | FTW_PHYS); }

bool has_media(const std::vector<fd::MediaRef>& v, const std::string& type, const std::string& source,
               const std::string& path)
{
    for (const auto& m : v)
        if (m.type == type && m.source == source && m.path == path)
            return true;
    return false;
}

void put16(std::string& s, uint32_t v)
{
    s += (char)(v & 0xff);
    s += (char)((v >> 8) & 0xff);
}

void put32(std::string& s, uint32_t v)
{
    s += (char)(v & 0xff);
    s += (char)((v >> 8) & 0xff);
    s += (char)((v >> 16) & 0xff);
    s += (char)((v >> 24) & 0xff);
}

struct ZEnt {
    std::string name;
    uint32_t crc;
    uint16_t method;
    std::string data;
};

std::string build_zip(const std::vector<ZEnt>& ents)
{
    std::string out;
    std::vector<uint32_t> offs;
    for (const ZEnt& e : ents) {
        offs.push_back((uint32_t)out.size());
        put32(out, 0x04034b50);
        put16(out, 20);
        put16(out, 0);
        put16(out, e.method);
        put16(out, 0);
        put16(out, 0);
        put32(out, e.crc);
        put32(out, (uint32_t)e.data.size());
        put32(out, (uint32_t)e.data.size());
        put16(out, (uint32_t)e.name.size());
        put16(out, 0);
        out += e.name;
        out += e.data;
    }
    uint32_t cd_off = (uint32_t)out.size();
    for (size_t i = 0; i < ents.size(); i++) {
        const ZEnt& e = ents[i];
        put32(out, 0x02014b50);
        put16(out, 20);
        put16(out, 20);
        put16(out, 0);
        put16(out, e.method);
        put16(out, 0);
        put16(out, 0);
        put32(out, e.crc);
        put32(out, (uint32_t)e.data.size());
        put32(out, (uint32_t)e.data.size());
        put16(out, (uint32_t)e.name.size());
        put16(out, 0);
        put16(out, 0);
        put16(out, 0);
        put16(out, 0);
        put32(out, 0);
        put32(out, offs[i]);
        out += e.name;
    }
    uint32_t cd_size = (uint32_t)out.size() - cd_off;
    put32(out, 0x06054b50);
    put16(out, 0);
    put16(out, 0);
    put16(out, (uint32_t)ents.size());
    put16(out, (uint32_t)ents.size());
    put32(out, cd_size);
    put32(out, cd_off);
    put16(out, 0);
    return out;
}

std::string hex8(uint32_t v)
{
    char b[9];
    std::snprintf(b, sizeof b, "%08X", v);
    return b;
}

const char* kGba = "Nintendo - Game Boy Advance";

}

int main()
{
    check(fd::Scraper::url_for(kGba, "Synth Game (USA, Europe)", fd::ArtType::Covers) ==
              "https://thumbnails.libretro.com/Nintendo%20-%20Game%20Boy%20Advance/Named_Boxarts/"
              "Synth%20Game%20%28USA%2C%20Europe%29.png",
          "url_for covers encodes folder+name (space %20, comma %2C, parens %28 %29)");
    check(fd::Scraper::url_for(kGba, "Anguna", fd::ArtType::Snaps) ==
              "https://thumbnails.libretro.com/Nintendo%20-%20Game%20Boy%20Advance/Named_Snaps/Anguna.png",
          "url_for snaps maps to Named_Snaps");
    check(fd::Scraper::url_for(kGba, "Anguna", fd::ArtType::Titles) ==
              "https://thumbnails.libretro.com/Nintendo%20-%20Game%20Boy%20Advance/Named_Titles/Anguna.png",
          "url_for titles maps to Named_Titles");

    check(fd::Scraper::local_path("/data", "gba", "Anguna", fd::ArtType::Covers) == "/data/media/gba/covers/Anguna.png",
          "local_path covers layout");
    check(fd::Scraper::local_path("/data/", "gba", "Anguna", fd::ArtType::Snaps) == "/data/media/gba/snaps/Anguna.png",
          "local_path strips trailing slash, snaps dir");
    check(fd::Scraper::local_path("/data", "gba", "Anguna", fd::ArtType::Titles) == "/data/media/gba/titles/Anguna.png",
          "local_path titles dir");

    char tmpl[] = "/tmp/blockie-scraper-test.XXXXXX";
    char* root_c = mkdtemp(tmpl);
    if (!root_c) {
        std::fprintf(stderr, "mkdtemp failed\n");
        return 1;
    }
    std::string root = root_c;

    fd::Library lib;
    check(lib.open(root), "library open");

    int calls = 0;
    std::string last_url;
    std::function<fd::HttpResult(const std::string&)> responder = [](const std::string&) {
        return fd::HttpResult{0, std::string(), true};
    };
    fd::Fetcher fetch = [&](const std::string& url) {
        calls++;
        last_url = url;
        return responder(url);
    };

    fd::Scraper scr(lib, root, fetch);

    {
        std::string local = fd::Scraper::local_path(root, "gba", "Local Hit", fd::ArtType::Covers);
        write_file(local, "localbytes");
        int before = calls;
        fd::ArtOutcome o = scr.art(1, "gba", kGba, "Local Hit", fd::ArtType::Covers);
        check(o.cached && !o.not_found && !o.transient && o.path == local, "local hit returns cached local path");
        check(calls == before, "local hit does not call the fetcher");
        std::vector<fd::MediaRef> md = lib.media(1);
        check(has_media(md, "covers", "local", local), "local hit records media source=local");
    }

    {
        responder = [](const std::string&) { return fd::HttpResult{200, "pngbytes", false}; };
        int before = calls;
        fd::ArtOutcome o = scr.art(2, "gba", kGba, "Fetch Ok", fd::ArtType::Snaps);
        std::string expect = fd::Scraper::local_path(root, "gba", "Fetch Ok", fd::ArtType::Snaps);
        check(o.cached && !o.not_found && !o.transient && o.path == expect, "200 returns cached written path");
        check(calls == before + 1, "200 called the fetcher once");
        check(read_file(expect) == "pngbytes", "200 wrote the body to the local path");
        std::vector<fd::MediaRef> md = lib.media(2);
        check(has_media(md, "snaps", "libretro", expect), "200 records media source=libretro");
    }

    {
        responder = [](const std::string&) { return fd::HttpResult{404, std::string(), false}; };
        fd::ArtOutcome o = scr.art(3, "gba", kGba, "Missing Art", fd::ArtType::Titles);
        check(!o.cached && o.not_found && !o.transient, "404 returns not_found");
        check(lib.media(3).empty(), "404 records nothing");
        std::string p = fd::Scraper::local_path(root, "gba", "Missing Art", fd::ArtType::Titles);
        check(access(p.c_str(), F_OK) != 0, "404 writes no file");
    }

    {
        responder = [](const std::string&) { return fd::HttpResult{0, std::string(), true}; };
        fd::ArtOutcome o = scr.art(4, "gba", kGba, "Net Down", fd::ArtType::Covers);
        check(!o.cached && !o.not_found && o.transient, "transport_error returns transient");
        check(lib.media(4).empty(), "transient records nothing");
    }

    {
        responder = [](const std::string&) { return fd::HttpResult{503, std::string(), false}; };
        fd::ArtOutcome o = scr.art(8, "gba", kGba, "Server Err", fd::ArtType::Covers);
        check(!o.cached && !o.not_found && o.transient, "5xx returns transient");
        check(lib.media(8).empty(), "5xx records nothing");
    }

    {
        responder = [](const std::string&) { return fd::HttpResult{0, std::string(), true}; };
        fd::ArtOutcome o1 = scr.art(5, "gba", kGba, "Flaky Art", fd::ArtType::Snaps);
        check(o1.transient && lib.media(5).empty(), "first attempt transient, no negative mark");
        int c1 = calls;
        responder = [](const std::string&) { return fd::HttpResult{200, "later", false}; };
        fd::ArtOutcome o2 = scr.art(5, "gba", kGba, "Flaky Art", fd::ArtType::Snaps);
        check(calls == c1 + 1, "transient game is re-fetched (no negative cache)");
        check(o2.cached, "retry after transient caches");
        std::string expect = fd::Scraper::local_path(root, "gba", "Flaky Art", fd::ArtType::Snaps);
        check(has_media(lib.media(5), "snaps", "libretro", expect), "retry records media source=libretro");
    }

    {
        responder = [](const std::string& url) -> fd::HttpResult {
            if (url.find("Named_Boxarts") != std::string::npos)
                return {200, "box", false};
            return {404, std::string(), false};
        };
        fd::Game g;
        g.id = 7;
        g.system_id = "gba";
        g.name = "Anguna";
        g.is_multidisc = false;
        g.present = true;
        scr.scrape_game(g, kGba);
        std::vector<fd::MediaRef> md = lib.media(7);
        std::string cover = fd::Scraper::local_path(root, "gba", "Anguna", fd::ArtType::Covers);
        check(md.size() == 1 && has_media(md, "covers", "libretro", cover),
              "scrape_game fills only the type that exists (per-type 404 for the rest)");
    }

    {
        std::string hp = root + "/crc_hello.bin";
        write_file(hp, "hello");
        std::optional<uint32_t> c = fd::Scraper::rom_crc(hp, {});
        check(c && *c == 0x3610A686u, "crc32 of 'hello' matches the zlib value");
        check(!fd::Scraper::rom_crc(root + "/does-not-exist.bin", {}).has_value(),
              "crc of a missing file returns empty");

        std::string payload = "this is a synthetic rom body for the zip tests";
        write_file(root + "/raw.bin", payload);
        std::optional<uint32_t> rc = fd::Scraper::rom_crc(root + "/raw.bin", {});
        check(rc.has_value(), "crc of a written raw file succeeds");
        uint32_t pcrc = rc ? *rc : 0;

        write_file(root + "/stored.zip", build_zip({{"Game (USA).gba", pcrc, 0, payload}}));
        std::optional<uint32_t> zs = fd::Scraper::rom_crc(root + "/stored.zip", {".gba", ".zip"});
        check(zs && *zs == pcrc, "zip stored entry crc read from the central directory");

        write_file(root + "/deflate.zip", build_zip({{"Game (USA).gba", pcrc, 8, "not-really-deflate"}}));
        std::optional<uint32_t> zd = fd::Scraper::rom_crc(root + "/deflate.zip", {".gba", ".zip"});
        check(zd && *zd == pcrc, "zip deflate entry crc read without inflating");

        write_file(root + "/two.zip",
                   build_zip({{"readme.txt", 0x11111111u, 0, "r"}, {"Game.gba", 0x22222222u, 0, "g"}}));
        std::optional<uint32_t> z2 = fd::Scraper::rom_crc(root + "/two.zip", {".gba"});
        check(z2 && *z2 == 0x22222222u, "zip picks the entry matching a system ext, not the first entry");

        write_file(root + "/zero.zip", build_zip({}));
        check(!fd::Scraper::rom_crc(root + "/zero.zip", {".gba"}).has_value(), "zip with zero entries returns empty");

        std::string fakedata = std::string("\x50\x4b\x05\x06", 4) + std::string(18, '\0');
        write_file(root + "/fake.zip", build_zip({{"Game.gba", 0x33333333u, 0, fakedata}}));
        std::optional<uint32_t> zf = fd::Scraper::rom_crc(root + "/fake.zip", {".gba"});
        check(zf && *zf == 0x33333333u, "entry data containing PK0506 bytes still reads the correct crc");

        std::string z = build_zip({{"Game.gba", 0x44444444u, 0, "g"}});
        write_file(root + "/trunc.zip", z.substr(0, z.size() - 10));
        check(!fd::Scraper::rom_crc(root + "/trunc.zip", {".gba"}).has_value(), "truncated eocd returns empty");
    }

    {
        std::string gbarom = root + "/synth.gba";
        std::string body(256, '\0');
        for (size_t i = 0; i < body.size(); i++)
            body[i] = (char)(i * 7 + 3);
        write_file(gbarom, body);
        std::optional<uint32_t> rc = fd::Scraper::rom_crc(gbarom, {".gba", ".zip"});
        check(rc.has_value(), "synth gba rom crc computes");
        uint32_t crc = rc ? *rc : 0;

        std::string datpath = root + "/cache/dat/Nintendo - Game Boy Advance.dat";
        std::string dat = "clrmamepro (\n\tname \"Nintendo - Game Boy Advance\"\n)\n"
                          "game (\n\tname \"Synth Game (USA)\"\n"
                          "\trom ( name \"Synth Game (USA).gba\" size 256 crc " +
                          hex8(crc) + " )\n)\n";
        write_file(datpath, dat);

        fd::Scraper rs(lib, root, fetch);
        rs.set_expected_dat_sha("gba", fd::Scraper::sha256_hex(dat));
        check(rs.resolve_name("gba", gbarom) == "Synth Game (USA)", "crc resolves to the No-Intro name via the dat");
        check(rs.resolve_name("psx", root + "/whatever.chd").empty(), "disc system is not content-resolved");
        check(rs.resolve_name("gba", root + "/missing.gba").empty(), "a missing rom resolves to empty");

        std::string dat2 = "clrmamepro (\n\tname \"Nintendo - Game Boy Advance\"\n)\n"
                           "game (\n\tname \"Renamed Game\"\n"
                           "\trom ( name \"x.gba\" size 256 crc " +
                           hex8(crc) + " )\n)\n";
        write_file(datpath, dat2);
        fd::Scraper rs2(lib, root, fetch);
        check(rs2.resolve_name("gba", gbarom) == "Synth Game (USA)",
              "a cached resolution is reused without re-reading the dat");

        struct utimbuf tb;
        tb.actime = 1000;
        tb.modtime = 1000;
        utime(gbarom.c_str(), &tb);
        fd::Scraper rs3(lib, root, fetch);
        rs3.set_expected_dat_sha("gba", fd::Scraper::sha256_hex(dat2));
        check(rs3.resolve_name("gba", gbarom) == "Renamed Game",
              "changing the rom mtime invalidates the cached resolution");

        struct utimbuf tb2;
        tb2.actime = 2000;
        tb2.modtime = 2000;
        utime(gbarom.c_str(), &tb2);
        responder = [](const std::string&) { return fd::HttpResult{404, std::string(), false}; };
        fd::Scraper rs5(lib, root, fetch);
        check(rs5.resolve_name("gba", gbarom).empty(),
              "a cached dat whose sha256 does not match the pin is not trusted, re-downloaded");
    }

    {
        responder = [](const std::string&) { return fd::HttpResult{404, std::string(), false}; };
        fd::ArtOutcome a1 = scr.art(20, "gba", kGba, "Neg One", fd::ArtType::Covers);
        check(a1.not_found, "first 404 returns not_found");
        int c0 = calls;
        fd::ArtOutcome a2 = scr.art(20, "gba", kGba, "Neg One", fd::ArtType::Covers);
        check(a2.not_found && calls == c0, "a fresh negative-cache hit skips the fetch");

        std::string lp = fd::Scraper::local_path(root, "gba", "Neg One", fd::ArtType::Covers);
        write_file(lp, "sideloaded");
        int c1 = calls;
        fd::ArtOutcome a3 = scr.art(20, "gba", kGba, "Neg One", fd::ArtType::Covers);
        check(a3.cached && calls == c1, "a later-sideloaded local file wins over the negative cache");

        remove(lp.c_str());
        responder = [](const std::string&) { return fd::HttpResult{200, "box", false}; };
        int c2 = calls;
        fd::ArtOutcome a4 = scr.art(20, "gba", kGba, "Neg One", fd::ArtType::Covers);
        check(a4.cached && calls == c2 + 1, "a local hit cleared the negative row so the next lookup fetches again");
    }

    {
        responder = [](const std::string&) { return fd::HttpResult{200, "box", false}; };
        lib.neg_mark("c4e17d4|gba|covers|Aged Miss", 1000);
        int c0 = calls;
        fd::ArtOutcome o = scr.art(21, "gba", kGba, "Aged Miss", fd::ArtType::Covers);
        check(o.cached && calls == c0 + 1, "an aged (expired) negative row re-fetches");

        lib.neg_mark("oldpin|gba|covers|Epoch Miss", (int64_t)std::time(nullptr));
        int c1 = calls;
        fd::ArtOutcome o2 = scr.art(22, "gba", kGba, "Epoch Miss", fd::ArtType::Covers);
        check(o2.cached && calls == c1 + 1, "a negative row under a different dat epoch does not suppress the lookup");
    }

    {
        check(fd::Scraper::sha256_hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
              "sha256 abc KAT");
        check(fd::Scraper::sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
                  "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
              "sha256 two-block KAT");

        std::string gbrom = root + "/synth.gb";
        std::string gbbody(200, '\0');
        for (size_t i = 0; i < gbbody.size(); i++)
            gbbody[i] = (char)(i * 11 + 1);
        write_file(gbrom, gbbody);
        std::optional<uint32_t> rc = fd::Scraper::rom_crc(gbrom, {".gb", ".zip"});
        uint32_t crc = rc ? *rc : 0;
        std::string dat = "clrmamepro (\n\tname \"Nintendo - Game Boy\"\n)\n"
                          "game (\n\tname \"Should Not Resolve\"\n"
                          "\trom ( name \"x.gb\" size 200 crc " +
                          hex8(crc) + " )\n)\n";
        responder = [dat](const std::string& url) -> fd::HttpResult {
            if (url.find("metadat/no-intro") != std::string::npos)
                return {200, dat, false};
            return {404, std::string(), false};
        };
        fd::Scraper rs4(lib, root, fetch);
        check(rs4.resolve_name("gb", gbrom).empty(),
              "a fetched dat whose sha256 does not match the pin is rejected, not parsed");
    }

    {
        std::string sroot = root + "/sweep";
        std::string rdir = sroot + "/roms/gba/";
        write_file(rdir + "Synth Alpha.gba", "alpha rom body");
        write_file(rdir + "Synth Beta.gba", "beta rom body");
        write_file(rdir + "Synth Gamma.gba", "gamma rom body");
        write_file(rdir + "Synth Delta.gba", "delta rom body");
        write_file(rdir + "Synth Eps.gba", "eps rom body");
        write_file(rdir + "Synth Zeta.gba", "zeta rom body");
        write_file(rdir + "Synth Gone.gba", "gone rom body");
        fd::Library sl;
        check(sl.open(sroot) && sl.scan() == 7, "sweep fixture scans seven synthetic games");
        unlink((rdir + "Synth Gone.gba").c_str());
        check(sl.scan() == 6, "sweep fixture marks one game not present");
        std::unordered_map<std::string, fd::Game> by;
        for (const fd::Game& g : sl.games("gba"))
            by[g.name] = g;
        check(by.size() == 7, "sweep fixture lists the seven games by name");

        std::optional<uint32_t> bc = fd::Scraper::rom_crc(rdir + "Synth Beta.gba", {".gba"});
        std::optional<uint32_t> dc = fd::Scraper::rom_crc(rdir + "Synth Delta.gba", {".gba"});
        std::string dat = "clrmamepro (\n\tname \"Nintendo - Game Boy Advance\"\n)\n"
                          "game (\n\tname \"Synth Beta (World)\"\n"
                          "\trom ( name \"b.gba\" size 13 crc " +
                          hex8(bc ? *bc : 0) +
                          " )\n)\n"
                          "game (\n\tname \"Synth Delta (World)\"\n"
                          "\trom ( name \"d.gba\" size 14 crc " +
                          hex8(dc ? *dc : 0) + " )\n)\n";
        responder = [dat](const std::string& url) -> fd::HttpResult {
            if (url.find("metadat/no-intro") != std::string::npos)
                return {200, dat, false};
            return {404, std::string(), false};
        };
        fd::Scraper ss(sl, sroot, fetch);
        ss.set_expected_dat_sha("gba", fd::Scraper::sha256_hex(dat));
        check(ss.resolve_name("gba", by["Synth Beta"].rompath_abs) == "Synth Beta (World)",
              "sweep fixture caches the resolved name for one game");

        auto cover = [&](const std::string& name) {
            return fd::Scraper::local_path(sroot, "gba", name, fd::ArtType::Covers);
        };
        write_file(cover("Synth Alpha"), "a");
        write_file(cover("Synth Beta (World)"), "b");
        write_file(cover("Synth Beta"), "b2");
        write_file(cover("Synth Delta (World)"), "d");
        write_file(cover("Synth Eps"), "e");
        write_file(cover("Synth Zeta"), "z");
        write_file(cover("Synth Gone"), "x");
        write_file(sroot + "/elsewhere.png", "live");
        sl.set_media(by["Synth Eps"].id, "covers", sroot + "/elsewhere.png", "libretro");
        sl.set_media(by["Synth Zeta"].id, "covers", sroot + "/deleted.png", "libretro");

        int before = calls;
        int n = ss.sweep_local(fd::ArtType::Covers);
        check(n == 3, "sweep records the three local covers it can name without a crc");
        check(calls == before, "sweep never calls the fetcher");
        check(has_media(sl.media(by["Synth Alpha"].id), "covers", "local", cover("Synth Alpha")),
              "sweep finds a cover by the library name");
        check(has_media(sl.media(by["Synth Beta"].id), "covers", "local", cover("Synth Beta (World)")),
              "sweep prefers the cached resolved name over the library name");
        check(sl.media(by["Synth Gamma"].id).empty(), "sweep records nothing for a game with no local file");
        check(sl.media(by["Synth Delta"].id).empty(), "sweep does not crc to find an uncached resolved name");
        check(has_media(sl.media(by["Synth Eps"].id), "covers", "libretro", sroot + "/elsewhere.png"),
              "sweep leaves a covers row that points at a live file alone");
        check(has_media(sl.media(by["Synth Zeta"].id), "covers", "local", cover("Synth Zeta")),
              "sweep replaces a covers row that points at a deleted file");
        check(sl.media(by["Synth Gone"].id).empty(), "sweep skips a game that is not present");
        check(ss.sweep_local(fd::ArtType::Covers) == 0, "a second sweep records nothing new");

        check(ss.record_local(by["Synth Gamma"], fd::ArtType::Covers).empty(), "record_local misses with no file");
        write_file(cover("Synth Gamma"), "g");
        check(ss.record_local(by["Synth Gamma"], fd::ArtType::Covers) == cover("Synth Gamma") &&
                  has_media(sl.media(by["Synth Gamma"].id), "covers", "local", cover("Synth Gamma")),
              "record_local picks up a file that appears later");
        check(calls == before, "record_local never calls the fetcher");
    }

    rm_rf(root);
    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
