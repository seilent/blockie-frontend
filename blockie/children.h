#pragma once
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <sys/types.h>

namespace fd {

class Children {
public:
    using Done = std::function<void(int status)>;

    pid_t spawn(const std::vector<std::string>& argv, Done done, int* out = nullptr);
    bool reaped(pid_t pid, int status);
    size_t running() const { return done_.size(); }

private:
    std::unordered_map<pid_t, Done> done_;
};

}
