#pragma once
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "shape.h"

namespace fd {

std::vector<std::pair<size_t, size_t>> wrap_ranges(std::string_view s, int width64, size_t max_rows,
                                                   const std::function<int(size_t, size_t)>& width,
                                                   const std::function<size_t(size_t, size_t)>& step);
std::vector<std::string> wrap_lines(Fonts& f, const TextStyle& st, const std::string& s, int width64, size_t max_lines);
std::vector<GlyphRun> wrap_runs(Fonts& f, const TextStyle& st, const std::string& s, Dir d, int width64, size_t max_rows);

}
