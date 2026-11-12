#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fd {

constexpr int64_t kRecentGen = 0;

struct DecodeJob {
    int64_t game_id = 0;
    std::string path;
    int64_t gen = kRecentGen;
    int64_t queued_ms = 0;
    std::vector<int> sides;
    int64_t index_gen = 0;
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
    bool current(int64_t game_id, const std::string& path, int64_t index_gen) const;
    void forget(const std::unordered_set<int64_t>& ids);
    size_t size() const { return q_.size(); }
    size_t busy() const { return busy_.size(); }

private:
    std::deque<DecodeJob> q_;
    std::unordered_map<int64_t, int> busy_;
    int64_t window_ = kRecentGen;
    std::unordered_set<int64_t> keep_;
    std::unordered_set<int64_t> recent_;
    std::unordered_map<int64_t, std::pair<std::string, int64_t>> wanted_;
};

}
