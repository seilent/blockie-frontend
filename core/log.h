#pragma once
#include <cstddef>

namespace fd {

void log_info(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void log_info_set(bool on);
bool log_info_on();
bool log_rotate(size_t cap);

}
