#pragma once
#include <cstddef>
#include <string>

namespace fd {

bool dump_z(const std::string& path, const void* data, size_t n);
bool load_z(const std::string& path, void* data, size_t n);

}
