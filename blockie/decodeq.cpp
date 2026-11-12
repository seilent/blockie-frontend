#include "decodeq.h"

#include <algorithm>
#include <utility>

namespace fd {

bool DecodeQueue::push(const DecodeJob& j)
{
    std::pair<std::string, int64_t> key{j.path, j.index_gen};
    auto w = wanted_.find(j.game_id);
    bool same = w != wanted_.end() && w->second == key;
    wanted_[j.game_id] = key;
    if (busy_.count(j.game_id) && same)
        return false;
    for (DecodeJob& q : q_)
        if (q.game_id == j.game_id) {
            if (q.gen != kRecentGen)
                q.gen = j.gen;
            q.path = j.path;
            q.index_gen = j.index_gen;
            return false;
        }
    q_.push_back(j);
    return true;
}

bool DecodeQueue::pop(DecodeJob& out)
{
    q_.erase(std::remove_if(q_.begin(), q_.end(),
                            [&](const DecodeJob& j) { return j.gen != kRecentGen && j.gen != window_; }),
             q_.end());
    auto it = std::find_if(q_.begin(), q_.end(), [&](const DecodeJob& j) { return j.gen == window_; });
    if (it == q_.end())
        it = q_.begin();
    if (it == q_.end())
        return false;
    out = std::move(*it);
    q_.erase(it);
    busy_[out.game_id]++;
    return true;
}

void DecodeQueue::done(int64_t game_id)
{
    auto b = busy_.find(game_id);
    if (b != busy_.end() && --b->second <= 0)
        busy_.erase(b);
}

void DecodeQueue::set_window(int64_t gen, std::unordered_set<int64_t> keep)
{
    window_ = gen;
    keep_ = std::move(keep);
}

void DecodeQueue::set_recent(std::unordered_set<int64_t> ids) { recent_ = std::move(ids); }

bool DecodeQueue::accept(int64_t game_id) const { return keep_.count(game_id) || recent_.count(game_id); }

bool DecodeQueue::pending(int64_t game_id) const
{
    if (busy_.count(game_id))
        return true;
    for (const DecodeJob& q : q_)
        if (q.game_id == game_id)
            return true;
    return false;
}

bool DecodeQueue::current(int64_t game_id, const std::string& path, int64_t index_gen) const
{
    auto w = wanted_.find(game_id);
    return w != wanted_.end() && w->second.first == path && w->second.second == index_gen;
}

void DecodeQueue::forget(const std::unordered_set<int64_t>& ids)
{
    q_.erase(std::remove_if(q_.begin(), q_.end(), [&](const DecodeJob& j) { return ids.count(j.game_id) > 0; }),
             q_.end());
    for (int64_t id : ids)
        wanted_.erase(id);
}

}
