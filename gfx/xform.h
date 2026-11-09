#pragma once

#include "../core/fx.h"
#include "canvas.h"
#include "warp.h"

namespace fd {

void mat3_mul(const double* a, const double* b, double* out);
bool mat3_invert(const double* a, double* out);

struct Xform {
    fx x = 0, y = 0;
    fx scale = kOne;
    fx angle = 0;
    int hinge = 0, d = 1;
    bool swivel = false;
    int eye = 0;

    static Xform at(fx x, fx y)
    {
        Xform f;
        f.x = x;
        f.y = y;
        return f;
    }
    static Xform of(const Projection& p) { return {p.x, p.y, p.scale, p.angle, p.hinge, p.d, false}; }
    Projection projection() const { return {x, y, scale, angle, hinge, d}; }
    bool plain() const { return scale == kOne && angle == 0; }
    bool operator==(const Xform& o) const
    {
        return x == o.x && y == o.y && scale == o.scale && angle == o.angle && hinge == o.hinge && d == o.d &&
               swivel == o.swivel && eye == o.eye;
    }
    bool operator!=(const Xform& o) const { return !(*this == o); }
    void inverse(double* m, bool kernel = false) const;
    void forward(double* m, bool kernel = false) const;
};

Rect xform_bounds(const Xform& f, Rect r);

}
