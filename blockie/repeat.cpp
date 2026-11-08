#include "repeat.h"

#include "../core/fx.h"
#include "../ui/tokens.h"

namespace fd {

bool is_direction(PadAction a)
{
    return a == PadAction::Up || a == PadAction::Down || a == PadAction::Left || a == PadAction::Right;
}

Repeat::Repeat() : delay_(fx_round(tok::input_repeatDelayMs)), interval_(fx_round(tok::input_repeatIntervalMs)) {}

void Repeat::press(PadAction dir, uint8_t src, int64_t now)
{
    if (!is_direction(dir) || src == 0)
        return;
    uint8_t mask = src;
    for (size_t i = 0; i < held_.size(); i++)
        if (held_[i].dir == dir) {
            mask |= held_[i].src;
            held_.erase(held_.begin() + (long)i);
            break;
        }
    held_.push_back({dir, mask});
    next_ = now + delay_;
}

void Repeat::drop(size_t i, int64_t now)
{
    bool was_current = i + 1 == held_.size();
    held_.erase(held_.begin() + (long)i);
    if (was_current && !held_.empty())
        next_ = now + delay_;
}

void Repeat::release(PadAction dir, uint8_t src, int64_t now)
{
    for (size_t i = 0; i < held_.size(); i++)
        if (held_[i].dir == dir) {
            held_[i].src &= (uint8_t)~src;
            if (held_[i].src == 0)
                drop(i, now);
            return;
        }
}

void Repeat::release_source(uint8_t src, int64_t now)
{
    for (size_t i = held_.size(); i-- > 0;) {
        held_[i].src &= (uint8_t)~src;
        if (held_[i].src == 0)
            drop(i, now);
    }
}

void Repeat::rearm(int64_t now)
{
    if (!held_.empty())
        next_ = now + delay_;
}

uint8_t Repeat::sources(PadAction dir) const
{
    for (const Hold& h : held_)
        if (h.dir == dir)
            return h.src;
    return 0;
}

bool Repeat::due(int64_t now, PadAction& out)
{
    if (held_.empty() || now < next_)
        return false;
    out = held_.back().dir;
    next_ += interval_;
    if (next_ <= now)
        next_ = now + interval_;
    return true;
}

}
