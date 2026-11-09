#pragma once
#include <cstdint>

#include "fx.h"

namespace fd {

enum class Ev {
    KnobCw,
    KnobCcw,
    KnobSelect,
    KnobDouble,
    Prev,
    PrevHold,
    Play,
    PlayHold,
    PlayHoldLong,
    PlayDouble,
    Next,
    NextHold,
    PrevHoldEnd,
    NextHoldEnd,
    TouchDown,
    TouchMove,
    TouchUp,
    MediaPlay,
    MediaPause,
    RemoteRew,
    RemoteFf,
    RemoteRewEnd,
    RemoteFfEnd,
    VolUp,
    VolDown,
    PowerTap,
    PowerHold,
    PowerDouble,
    PowerHoldShort,
    PowerHoldShortEnd,
    VolUpHold,
    VolDownHold,
    VolUpRelease,
    VolDownRelease,
    SelectDown,
    SelectUp,
    HoldLocked,
    PivotPrev,
    PivotNext,
};

enum class Src : uint8_t { Board, Remote, Avrcp };

constexpr fx kNoPx = INT32_MIN;

struct Event {
    Ev kind;
    int64_t ms = 0;
    fx x = 0, y = 0;
    Src src = Src::Board;
    bool nav = false;
    int64_t tap_at = -1;
    bool bare_lift = false;
    int64_t press_at = -1;
    fx px_x = kNoPx, px_y = kNoPx, ppu = 0;
    int64_t pressed() const { return press_at < 0 ? ms : press_at; }
};

inline Event std_event(Event e)
{
    e.ppu = tok::u;
    e.px_x = fx_mul(e.x, tok::u);
    e.px_y = fx_mul(e.y, tok::u);
    return e;
}

inline Event scoped(Event e)
{
    if (e.px_x == kNoPx) {
        e.ppu = fd::ppu();
        e.px_x = fx_mul(e.x, e.ppu);
        e.px_y = fx_mul(e.y, e.ppu);
    } else if (e.ppu != fd::ppu()) {
        e.ppu = fd::ppu();
        e.x = fx_div(e.px_x, e.ppu);
        e.y = fx_div(e.px_y, e.ppu);
    }
    return e;
}

}
