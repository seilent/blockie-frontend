#include <cstdio>
#include <cstring>
#include <string>

#include "library.h"

namespace {

int usage()
{
    std::fprintf(stderr, "blockie-scan [--data <root>] [scan|list]\n");
    return 2;
}

}

int main(int argc, char** argv)
{
    std::string data_root;
    std::string cmd = "scan";
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--data" && i + 1 < argc) {
            data_root = argv[++i];
        } else if (a == "scan" || a == "list") {
            cmd = a;
        } else {
            return usage();
        }
    }

    fd::Library lib;
    if (!lib.open(data_root)) {
        std::fprintf(stderr, "blockie-scan: open failed\n");
        return 1;
    }

    int total = lib.scan();

    if (cmd == "scan") {
        auto sys = lib.systems_with_games();
        for (const auto& s : sys)
            std::printf("%-8s %-20s %d\n", s.first.id.c_str(), s.first.name.c_str(), s.second);
        std::printf("total %d\n", total);
        return 0;
    }

    for (const auto& s : lib.systems_with_games()) {
        std::printf("== %s (%s) %d ==\n", s.first.id.c_str(), s.first.name.c_str(), s.second);
        for (const auto& g : lib.games(s.first.id)) {
            if (!g.present)
                continue;
            std::printf("  [%lld]%s %s\n", (long long)g.id, g.is_multidisc ? " (m3u)" : "", g.name.c_str());
        }
    }
    std::printf("total %d\n", total);
    return 0;
}
