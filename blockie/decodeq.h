#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_set>

namespace fd {

constexpr int64_t kRecentGen = 0;

struct DecodeJob {
    int64_t game_id = 0;
    std::string path;
    int64_t gen = kRecentGen;
    int64_t queued_ms = 0;
};

class DecodeQueue {
public:
    bool push(const DecodeJob& j);
    bool pop(DecodeJob& out);
    void done(int64_t game_id);
    void set_window(int64_t gen, std::unordered_set<int64_t> keep);
    void set_recent(std::unordered_set<int64_t> ids);
    bool accept(int64_t game_id) const;
    bool pending(int64_t game_id) const;
    size_t size() const { return q_.size(); }

private:
    std::deque<DecodeJob> q_;
    std::unordered_set<int64_t> busy_;
    int64_t window_ = kRecentGen;
    std::unordered_set<int64_t> keep_;
    std::unordered_set<int64_t> recent_;
};

}
