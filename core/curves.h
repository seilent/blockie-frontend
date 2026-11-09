#pragma once
#include "fx.h"

namespace fd {

enum class Curve {
    Linear,
    ExpOut6,
    ExpIn6,
    ExpIn1,
    ExpOut1,
    ExpIn15,
    Decel,
    Strip,
    Sine,
    Pop,
    BezierPop,
    EaseIn,
    EaseOut,
    PowOut17,
    CubicOut,
    SmoothStep,
    PowOut15,
    WordOpen,
    QuartOut,
    PowIn15,
};

fx curve(Curve c, fx t);
fx sin_deg(fx deg);
fx cos_deg(fx deg);

}
