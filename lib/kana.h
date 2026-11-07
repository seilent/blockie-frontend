#pragma once
#include <cstdint>

namespace fd {

uint32_t lower(uint32_t c);
uint32_t nfkc(uint32_t c);
uint32_t to_hiragana(uint32_t c);
bool han(uint32_t c);

}
