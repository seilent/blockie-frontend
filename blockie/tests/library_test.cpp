#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <fcntl.h>
#include <ftw.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../library.h"

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
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f) {
        std::fwrite(body.data(), 1, body.size(), f);
        std::fclose(f);
    }
}

int rm_cb(const char* path, const struct stat*, int, struct FTW*)
{
    remove(path);
    return 0;
}

void rm_rf(const std::string& p) { nftw(p.c_str(), rm_cb, 16, FTW_DEPTH | FTW_PHYS); }

const fd::Game* find_game(const std::vector<fd::Game>& v, const std::string& name)
{
    for (const auto& g : v)
        if (g.name == name)
            return &g;
    return nullptr;
}

int count_present(const std::vector<fd::Game>& v)
{
    int n = 0;
    for (const auto& g : v)
        if (g.present)
            n++;
    return n;
}

int sys_count(std::vector<std::pair<fd::SystemCfg, int>>& v, const std::string& id)
{
    for (auto& p : v)
        if (p.first.id == id)
            return p.second;
    return 0;
}

}

int main()
{
    char tmpl[] = "/tmp/blockie-lib-test.XXXXXX";
    char* root_c = mkdtemp(tmpl);
    if (!root_c) {
        std::fprintf(stderr, "mkdtemp failed\n");
        return 1;
    }
    std::string root = root_c;
    std::string roms = root + "/roms";

    mkdirs(roms + "/gba/media");
    mkdirs(roms + "/psx");
    mkdirs(roms + "/ngpc");
    mkdirs(roms + "/psp");

    write_file(roms + "/gba/Synth Game (USA).gba", "x");
    write_file(roms + "/gba/Homebrew Demo.zip", "x");
    write_file(roms + "/gba/Dupe Title.gba", "x");
    write_file(roms + "/gba/Dupe Title.zip", "x");
    write_file(roms + "/gba/media/cover.gba", "x");

    write_file(roms + "/ngpc/Synth NGPC Game.zip", "x");

    write_file(roms + "/psx/Multi RPG.m3u", "Multi RPG (Disc 1).chd\nMulti RPG (Disc 2).chd\n");
    write_file(roms + "/psx/Multi RPG (Disc 1).chd", "x");
    write_file(roms + "/psx/Multi RPG (Disc 2).chd", "x");
    write_file(roms + "/psx/Standalone Game.chd", "x");

    mkdirs(roms + "/psp/Folder Game");
    write_file(roms + "/psp/Folder Game/EBOOT.PBP", "x");
    mkdirs(roms + "/psp/Nested/Inner");
    write_file(roms + "/psp/Nested/Inner/EBOOT.PBP", "x");
    mkdirs(roms + "/psp/Dump/PSP/GAME/ABCD00001");
    write_file(roms + "/psp/Dump/PSP/GAME/ABCD00001/EBOOT.PBP", "x");
    mkdirs(roms + "/psp/Both");
    write_file(roms + "/psp/Both/EBOOT.PBP", "x");
    write_file(roms + "/psp/Both.iso", "x");
    write_file(roms + "/psp/Loose UMD.iso", "x");

    fd::Library lib;
    check(lib.open(root), "open");

    int total = lib.scan();

    auto sys = lib.systems_with_games();
    check(sys_count(sys, "gba") == 3, "gba present count is 3 (dedupe + media skip)");
    check(sys_count(sys, "psx") == 2, "psx present count is 2 (m3u one entry + standalone chd)");
    check(sys_count(sys, "psp") == 4, "psp present count is 4 (folder + nested eboot + file wins over folder + loose iso)");
    check(sys_count(sys, "ngpc") == 0, "ngpc ignored (unconfigured folder)");
    check(total == 9, "total present games is 9");

    auto gba = lib.games("gba");
    const fd::Game* arch = find_game(gba, "Homebrew Demo");
    check(arch != nullptr, "archive is one game keyed by base name");
    check(arch && arch->rompath_abs.size() >= 4 && arch->rompath_abs.substr(arch->rompath_abs.size() - 4) == ".zip",
          "archive game rompath is the .zip");

    const fd::Game* dupe = find_game(gba, "Dupe Title");
    check(dupe != nullptr, "dedupe collapses Dupe Title.gba + .zip to one game");
    check(dupe && dupe->rompath_abs.substr(dupe->rompath_abs.size() - 4) == ".gba",
          "dedupe prefers the non-archive path");
    check(find_game(gba, "cover") == nullptr, "media subdir file not scanned");

    auto psx = lib.games("psx");
    const fd::Game* m3u = find_game(psx, "Multi RPG");
    check(m3u != nullptr, "m3u is one entry keyed by its base name");
    check(m3u && m3u->is_multidisc, "m3u entry is_multidisc true");
    check(m3u && !m3u->m3u_path.empty() &&
              m3u->m3u_path.substr(m3u->m3u_path.size() - 4) == ".m3u" && m3u->rompath_abs == m3u->m3u_path,
          "m3u entry m3u_path and rompath point at the .m3u");
    check(find_game(psx, "Multi RPG (Disc 1)") == nullptr && find_game(psx, "Multi RPG (Disc 2)") == nullptr,
          "m3u-referenced discs absorbed, not separate entries");
    const fd::Game* standalone = find_game(psx, "Standalone Game");
    check(standalone != nullptr && !standalone->is_multidisc, "standalone chd is its own entry");

    int64_t synth_id = -1;
    {
        const fd::Game* g = find_game(gba, "Synth Game (USA)");
        check(g != nullptr && g->present, "Synth Game present on first scan");
        if (g)
            synth_id = g->id;
    }

    {
        auto psp = lib.games("psp");
        const fd::Game* folder = find_game(psp, "Folder Game");
        check(folder != nullptr && folder->present, "psp folder game named by its directory");
        check(folder && folder->rompath_abs.size() >= 10 &&
                  folder->rompath_abs.substr(folder->rompath_abs.size() - 10) == "/EBOOT.PBP",
              "psp folder game rompath points at EBOOT.PBP");
        check(find_game(psp, "Inner") != nullptr, "nested eboot named by its immediate parent dir");
        check(find_game(psp, "ABCD00001") == nullptr, "eboot under PSP/GAME/ skipped");
        const fd::Game* both = find_game(psp, "Both");
        check(both != nullptr && both->rompath_abs.substr(both->rompath_abs.size() - 4) == ".iso",
              "a rom file beats a folder of the same base name");
        check(find_game(psp, "Loose UMD") != nullptr, "loose psp iso is its own entry");
    }

    check(remove((roms + "/gba/Synth Game (USA).gba").c_str()) == 0, "remove a rom file");
    int total2 = lib.scan();
    check(total2 == 8, "total drops to 8 after removal");
    {
        auto gba2 = lib.games("gba");
        const fd::Game* g = find_game(gba2, "Synth Game (USA)");
        check(g != nullptr && !g->present, "removed game kept as present=0 (row not deleted)");
        check(g && g->id == synth_id, "removed game keeps its id");
        check(count_present(gba2) == 2, "two gba games present after removal");
        auto sys2 = lib.systems_with_games();
        check(sys_count(sys2, "gba") == 2, "systems_with_games gba count is 2 after removal");
    }

    write_file(roms + "/gba/Synth Game (USA).gba", "x");
    int total3 = lib.scan();
    check(total3 == 9, "total back to 9 after re-adding");
    {
        auto gba3 = lib.games("gba");
        const fd::Game* g = find_game(gba3, "Synth Game (USA)");
        check(g != nullptr && g->present, "re-added game flips present back to 1");
        check(g && g->id == synth_id, "re-added game keeps the same id");
    }

    {
        auto gba_meta = lib.games("gba");
        const fd::Game* g = find_game(gba_meta, "Dupe Title");
        if (g) {
            auto m = lib.meta(g->id);
            bool has_name = false, has_system = false;
            for (const auto& f : m) {
                if (f.field == "name" && f.value == "Dupe Title")
                    has_name = true;
                if (f.field == "system" && f.value == "gba")
                    has_system = true;
            }
            check(has_name && has_system, "meta seeds name + system");

            lib.set_media(g->id, "covers", "/data/media/covers/Dupe Title.png", "libretro");
            auto md = lib.media(g->id);
            bool ok = md.size() == 1 && md[0].type == "covers" && md[0].source == "libretro";
            check(ok, "set_media stores a covers row");
        } else {
            check(false, "meta/media game lookup");
        }
    }

    {
        int present_total = 0;
        for (auto& s : lib.systems_with_games())
            present_total += s.second;
        check((int)lib.recent_games(500).size() == present_total, "recent_games returns every present game");
        auto gall = lib.games("gba");
        if (gall.size() >= 2) {
            int64_t a = gall[0].id, b = gall[1].id;
            lib.mark_played(a, 100);
            lib.mark_played(b, 200);
            auto rec = lib.recent_games(500);
            check(rec.size() >= 2 && rec[0].id == b && rec[1].id == a, "played games lead, most recent first");
            auto one = lib.recent_games(1);
            check(one.size() == 1 && one[0].id == b, "recent_games honors the limit");
        } else {
            check(false, "recent_games needs at least two gba games");
        }
    }

    check(lib.config().size() == 10, "config exposes all 10 configured systems");

    {
        static const char* const kWrapperSystems[] = {"gb",  "gbc", "nes", "snes", "genesis",
                                                       "gba", "ps1", "n64", "psp",  "dc"};
        for (const auto& s : lib.config()) {
            std::string low = s.name;
            for (char& ch : low)
                ch = (char)tolower((unsigned char)ch);
            bool known = false;
            for (const char* w : kWrapperSystems)
                if (low == w) {
                    known = true;
                    break;
                }
            check(known, ("launch name for " + s.id + " is a blockie-game system").c_str());
        }
    }

    rm_rf(root);
    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
