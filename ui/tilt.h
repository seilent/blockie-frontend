#pragma once
#include <algorithm>
#include <cmath>

#include "../gfx/xform.h"

namespace fd {

struct TiltPose {
    double rx = 0, ry = 0, depth = 0;
};

inline TiltPose tilt_pose(double nx, double ny, double angle_deg, double depth)
{
    constexpr double kPi = 3.14159265358979323846;
    nx = std::clamp(nx, 0.0, 1.0);
    ny = std::clamp(ny, 0.0, 1.0);
    double xm = std::fabs(nx - 0.5), ym = std::fabs(ny - 0.5), mag = xm + ym, s = mag > 0 ? xm / mag : 0;
    double angle = mag * angle_deg * kPi / 180;
    auto sign = [](double v) { return v > 0 ? 1.0 : v < 0 ? -1.0 : 0.0; };
    TiltPose p;
    p.ry = angle * s * -sign(nx - 0.5);
    p.rx = angle * (1 - s) * sign(ny - 0.5);
    p.depth = (1 - mag) * depth;
    return p;
}

inline void tilt_matrix(const TiltPose& p, double q, double cx, double cy, double d, double* h)
{
    double ax = p.rx * q, ay = p.ry * q, dz = p.depth * q;
    double ca = std::cos(ax), sa = std::sin(ax), cb = std::cos(ay), sb = std::sin(ay);
    const double to[9] = {1, 0, -cx, 0, 1, -cy, 0, 0, 1}, back[9] = {1, 0, cx, 0, 1, cy, 0, 0, 1};
    const double turn[9] = {d * cb, d * sa * sb, 0, 0, d * ca, 0, -sb, sa * cb, d + dz};
    double m[9];
    mat3_mul(turn, to, m);
    mat3_mul(back, m, h);
}

}
