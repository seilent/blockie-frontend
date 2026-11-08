#pragma once

#include <functional>
#include <string>
#include <vector>

namespace fd {

std::vector<std::string> wrap_lines(const std::string& text, int avail, int max_lines,
                                    const std::function<int(const std::string&)>& measure);

}
