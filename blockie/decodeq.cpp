#include "decodeq.h"

#include <algorithm>
#include <utility>

namespace fd {

bool DecodeQueue::push(const DecodeJob& j)
{
    if (busy_.count(j.game_id))
        return false;
    for (DecodeJob& q : q_)
        if (q.game_id == j.game_id) {
            if (q.gen != kRecentGen)
                q.gen = j.gen;
            q.path = j.path;
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
    busy_.insert(out.game_id);
    return true;
}

void DecodeQueue::done(int64_t game_id) { busy_.erase(game_id); }

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

}
