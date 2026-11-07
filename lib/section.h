#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace fd {

constexpr int kSectionCount = 38;

struct SectionKey {
    int letter = 0;
    std::string sort;
};

SectionKey section_key(std::string_view title, bool ignore_the);
const char* section_letter(int i);
std::string fold_lower(std::string_view s);
uint64_t artist_key(std::string_view name);

}
