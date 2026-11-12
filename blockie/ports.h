#pragma once
#include <functional>
#include <string>

namespace fd {

constexpr const char* kPortsSystem = "ports";
constexpr const char* kPortsIndex = "/.blockie/index.tsv";

enum class HelperOp { None, Install, Update, Remove };

class PortsHelper {
public:
    HelperOp op = HelperOp::None;
    std::function<void(const std::string& name)> on_done;

    void line(const std::string& l);
};

}
