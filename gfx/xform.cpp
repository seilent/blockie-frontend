#include "xform.h"

#include <algorithm>
#include <cmath>

namespace fd {

void mat3_mul(const double* a, const double* b, double* out)
{
    double r[9];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            r[i * 3 + j] = a[i * 3] * b[j] + a[i * 3 + 1] * b[3 + j] + a[i * 3 + 2] * b[6 + j];
    std::copy(r, r + 9, out);
}

bool mat3_invert(const double* a, double* out)
{
    double c[9] = {a[4] * a[8] - a[5] * a[7], a[2] * a[7] - a[1] * a[8], a[1] * a[5] - a[2] * a[4],
                   a[5] * a[6] - a[3] * a[8], a[0] * a[8] - a[2] * a[6], a[2] * a[3] - a[0] * a[5],
                   a[3] * a[7] - a[4] * a[6], a[1] * a[6] - a[0] * a[7], a[0] * a[4] - a[1] * a[3]};
    double det = a[0] * c[0] + a[1] * c[3] + a[2] * c[6];
    if (std::fabs(det) < 1e-300)
        return false;
    for (int i = 0; i < 9; i++)
        out[i] = c[i] / det;
    return true;
}

void Xform::inverse(double* m) const
{
    const double kPi = 3.14159265358979323846;
    double ang = angle / 65536.0 * kPi / 180.0, c = std::cos(ang), s = std::sin(ang);
    double h = hinge, dd = d ? d : 1, sc = scale / 65536.0, px = x / 65536.0, py = y / 65536.0, es = eye * s / dd;
    double sm[9] = {1 / sc, 0, -px / sc, 0, 1 / sc, -py / sc, 0, 0, 1};
    double ay[9] = {1 - h * s / dd, 0, h * c - h + h * h * s / dd, -es, c, es * h, -s / dd, 0, c + h * s / dd};
    double ax[9] = {c, 0, 0, 0, 1 - h * s / dd, h * c - h + h * h * s / dd, 0, -s / dd, c + h * s / dd};
    mat3_mul(swivel ? ax : ay, sm, m);
}

void Xform::forward(double* m) const
{
    double i[9];
    inverse(i);
    if (!mat3_invert(i, m))
        std::fill(m, m + 9, 0.0);
}

Rect xform_bounds(const Xform& f, Rect r)
{
    if (r.empty())
        return {};
    if (f.plain()) {
        auto lo = [](int v, fx t) { return (int)std::floor(v + t / 65536.0); };
        auto hi = [](int v, fx t) { return (int)std::ceil(v + t / 65536.0); };
        return {lo(r.x0, f.x), lo(r.y0, f.y), hi(r.x1, f.x), hi(r.y1, f.y)};
    }
    double m[9];
    f.forward(m);
    double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
    for (int px : {r.x0, r.x1})
        for (int py : {r.y0, r.y1}) {
            double w = m[6] * px + m[7] * py + m[8];
            if (!(w > 0))
                return kScreen;
            double u = (m[0] * px + m[1] * py + m[2]) / w, v = (m[3] * px + m[4] * py + m[5]) / w;
            x0 = std::min(x0, u);
            y0 = std::min(y0, v);
            x1 = std::max(x1, u);
            y1 = std::max(y1, v);
        }
    auto clamp = [](double v) { return std::clamp(v, -1e6, 1e6); };
    return Rect{(int)std::floor(clamp(x0)), (int)std::floor(clamp(y0)), (int)std::ceil(clamp(x1)),
                (int)std::ceil(clamp(y1))}
        .grow(1);
}

}
