#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace fd {

enum class Dir : uint8_t { Auto, Ltr, Rtl };

constexpr size_t kBidiParaMax = 1024;

struct BidiPara {
    std::vector<uint32_t> cp;
    std::vector<uint32_t> at;
    std::vector<uint32_t> type;
    std::vector<int8_t> level;
    uint8_t base = 0;
};

struct BidiRun {
    uint32_t b = 0, e = 0;
    uint8_t level = 0;
};

uint32_t bidi_first_strong(std::string_view s, Dir& d);
uint32_t bidi_first_rtl(std::string_view s);
bool bidi_ignorable(uint32_t cp);
bool bidi_plain(std::string_view s, Dir base);
std::vector<std::string_view> bidi_pieces(std::string_view s);
BidiPara bidi_para(std::string_view s, Dir base);
std::vector<BidiRun> bidi_runs(const BidiPara& p, size_t b, size_t e);

}
