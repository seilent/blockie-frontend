#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <vector>

namespace fd {

uint64_t hash64(std::string_view s, uint64_t salt = 0);
uint64_t hash64_join(std::string_view a, char sep, std::string_view b);
uint32_t crc32(const void* data, size_t n, uint32_t crc = 0);

uint32_t utf8_next(const char*& s, const char* end);
void utf8_put(std::string& out, uint32_t cp);
std::string latin1_to_utf8(const char* s, size_t n);
std::string utf16_to_utf8(const uint8_t* s, size_t n, bool big_endian);
bool utf8_valid(const char* s, size_t n);
std::string trim(std::string_view s);

struct Segment {
    const void* p;
    size_t n;
};

bool write_atomic(const std::string& path, const void* data, size_t n, const std::string& tmp = "", mode_t mode = 0644);
bool write_atomic(const std::string& path, const std::vector<Segment>& segs, const std::string& tmp = "",
                  mode_t mode = 0644);
bool fsync_dir(const std::string& path);
bool read_file(const std::string& path, std::vector<uint8_t>& out, size_t cap);
int read_or_tmp(const std::string& path, std::vector<uint8_t>& out, size_t cap);
unsigned leftovers(const std::string& path);
std::string dir_of(const std::string& path);
std::string base_of(const std::string& path);
bool mkdirs(const std::string& path);

void lower_priority(int nice_value, bool io_idle);
pid_t self_tid();
long anon_kb(const std::string& proc = "/proc/self");

}
