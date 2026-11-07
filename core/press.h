#pragma once
#include "tween.h"

namespace fd {

constexpr int kPressSteps = 16;

constexpr int press_step(fx s)
{
    fx span = kOne - tok::motion_pressScale;
    int k = (int)(((int64_t)(kOne - s) * kPressSteps + span / 2) / span);
    return k < 0 ? 0 : k > kPressSteps ? kPressSteps : k;
}

constexpr fx press_factor(int k) { return kOne - (fx)((int64_t)(kOne - tok::motion_pressScale) * k / kPressSteps); }

struct Press {
    static constexpr int kMs = tok::motion_press >> kFxBits;
    static constexpr int kReturnMs = tok::motion_touchReturnDelay >> kFxBits;

    Tween t;
    int64_t release_at = -1;
    int shown = 0;

    Press() { t.set(kOne); }

    void down(int64_t now)
    {
        fx v = scale(now);
        release_at = -1;
        t.run(now, v, tok::motion_pressScale, kMs, Curve::Linear);
    }

    void tap(int64_t now)
    {
        if (t.to != kOne && release_at < 0)
            release_at = now + kReturnMs;
    }

    void cancel(int64_t now)
    {
        if (!held())
            return;
        fx v = scale(now);
        release_at = -1;
        if (v == kOne)
            t.set(kOne);
        else
            t.run(now, v, kOne, kMs, Curve::Linear);
    }

    void reset()
    {
        t.set(kOne);
        release_at = -1;
    }

    void drop()
    {
        if (held())
            reset();
    }

    void hold(int64_t now)
    {
        if (t.to != kOne && (release_at < 0 || now < release_at))
            t.set(tok::motion_pressScale);
    }

    fx scale(int64_t now) const
    {
        if (release_at < 0 || now < release_at)
            return t.at(now);
        int64_t e = now - release_at;
        if (e >= kMs)
            return kOne;
        return fx_lerp(t.at(release_at), kOne, (fx)(e * kOne / kMs));
    }

    fx draw(int64_t now)
    {
        shown = press_step(scale(now));
        return press_factor(shown);
    }

    bool held() const { return t.to != kOne && release_at < 0; }
    bool rest(int64_t now) const { return !moving(now) && scale(now) == kOne; }
    bool stale(int64_t now) const { return moving(now) || press_step(scale(now)) != shown; }
    int64_t end() const { return release_at >= 0 ? release_at + kMs : t.end(); }
    bool moving(int64_t now) const
    {
        return !t.done(now) || (release_at >= 0 && now >= release_at && now < release_at + kMs);
    }
    int64_t due(int64_t now) const { return release_at > now ? release_at : -1; }
    int64_t next_step(int64_t now) const
    {
        int k = press_step(scale(now));
        for (int64_t w = now + 1, stop = end(); w <= stop; w++) {
            if (release_at > w && t.done(w))
                w = release_at;
            if (press_step(scale(w)) != k)
                return w;
        }
        return -1;
    }
};

struct PressSlots {
    struct Slot {
        Press p;
        int item = -1;
    };
    Slot on, back;
    bool key = false;

    static bool rest(const Slot& s, int64_t now) { return s.item < 0 || (!s.p.moving(now) && s.p.scale(now) == kOne); }
    bool rest(int64_t now) const { return rest(on, now) && rest(back, now); }

    void down(int item, int64_t now)
    {
        if (item != on.item) {
            Slot again;
            if (item == back.item) {
                again = back;
                back = Slot{};
            }
            let_go(now);
            on = again;
            on.item = item;
        }
        on.p.down(now);
    }

    void let_go(int64_t now)
    {
        if (rest(on, now)) {
            on = Slot{};
            return;
        }
        on.p.cancel(now);
        back = on;
        on = Slot{};
    }

    void focus(int item, bool pressable, int64_t now)
    {
        if (key && on.item != item) {
            if (pressable)
                down(item, now);
            else
                let_go(now);
            return;
        }
        if (on.item < 0 || on.item == item || !on.p.held())
            return;
        if (pressable)
            down(item, now);
        else
            let_go(now);
    }

    void key_down(int item, bool pressable, int64_t now)
    {
        key = true;
        if (pressable)
            down(item, now);
    }
    void key_up(int64_t now) { cancel(now); }
    void tap(int64_t now) { on.p.tap(now); }
    void cancel(int64_t now)
    {
        key = false;
        on.p.cancel(now);
    }
    void drop()
    {
        on.p.drop();
        back.p.drop();
    }
    void reset()
    {
        on = back = Slot{};
        key = false;
    }
    void hold(int64_t now)
    {
        on.p.hold(now);
        back.p.hold(now);
    }

    fx draw(int item, int64_t now)
    {
        if (item < 0)
            return kOne;
        if (item == on.item)
            return on.p.draw(now);
        if (item == back.item)
            return back.p.draw(now);
        return kOne;
    }

    fx scale(int item, int64_t now) const
    {
        if (item >= 0 && item == on.item)
            return on.p.scale(now);
        if (item >= 0 && item == back.item)
            return back.p.scale(now);
        return kOne;
    }

    bool stale(int item, int64_t now) const
    {
        return (item >= 0 && item == on.item && on.p.stale(now)) || (item >= 0 && item == back.item && back.p.stale(now));
    }

    bool stale(int64_t now) const { return stale(on.item, now) || stale(back.item, now); }

    int64_t due(int64_t now) const
    {
        int64_t a = on.item >= 0 ? on.p.due(now) : -1, b = back.item >= 0 ? back.p.due(now) : -1;
        return a < 0 ? b : b < 0 ? a : a < b ? a : b;
    }

    bool held() const { return on.item >= 0 && on.p.held(); }
};

}
