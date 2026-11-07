#include "curves.h"

namespace fd {

namespace {

constexpr int kSeg = 512;
constexpr double kPi = 3.14159265358979323846;

constexpr double cexp(double x)
{
    double r = x / 1024;
    double term = 1, sum = 1;
    for (int i = 1; i < 12; i++) {
        term *= r / i;
        sum += term;
    }
    for (int i = 0; i < 10; i++)
        sum *= sum;
    return sum;
}

constexpr double cln(double x)
{
    int k = 0;
    while (x < 0.5) {
        x *= 2;
        k++;
    }
    double z = (x - 1) / (x + 1), term = z, sum = 0;
    for (int i = 0; i < 40; i++) {
        sum += term / (2 * i + 1);
        term *= z * z;
    }
    return 2 * sum - k * 0.69314718055994530942;
}

constexpr double csin(double x)
{
    double term = x, sum = x;
    for (int i = 1; i < 14; i++) {
        term *= -x * x / ((2 * i) * (2 * i + 1));
        sum += term;
    }
    return sum;
}

constexpr double cbez(double a, double b, double s)
{
    double m = 1 - s;
    return 3 * m * m * s * a + 3 * m * s * s * b + s * s * s;
}

constexpr double bezier(double x1, double y1, double x2, double y2, double t)
{
    double lo = 0, hi = 1;
    for (int i = 0; i < 48; i++) {
        double mid = (lo + hi) / 2;
        if (cbez(x1, x2, mid) < t)
            lo = mid;
        else
            hi = mid;
    }
    return cbez(y1, y2, (lo + hi) / 2);
}

enum Tab { TExpOut6, TExpIn6, TExpIn1, TExpOut1, TExpIn15, TDecel, TStrip, TSine, TPop, TPowOut17, TCount };

constexpr double eval(int k, double t)
{
    switch (k) {
    case TExpOut6:
        return 1 - (cexp(6 * (1 - t)) - 1) / (cexp(6) - 1);
    case TExpIn6:
        return (cexp(6 * t) - 1) / (cexp(6) - 1);
    case TExpIn1:
        return (cexp(t) - 1) / (cexp(1) - 1);
    case TExpOut1:
        return 1 - (cexp(1 - t) - 1) / (cexp(1) - 1);
    case TExpIn15:
        return (cexp(15 * t) - 1) / (cexp(15) - 1);
    case TDecel:
        return bezier(0.1, 0.9, 0.2, 1.0, t);
    case TStrip:
        return bezier(0.2, 0.8, 0.2, 1.0, t);
    case TSine:
        return csin(t * kPi / 2);
    case TPop:
        return 1 - cexp(-10 * t * 0.69314718055994530942);
    case TPowOut17:
        return t >= 1 ? 1 : 1 - cexp(1.7 * cln(1 - t));
    }
    return t;
}

struct Tables {
    int32_t v[TCount][kSeg + 1];
};

constexpr Tables make_tables()
{
    Tables r{};
    for (int k = 0; k < TCount; k++)
        for (int i = 0; i <= kSeg; i++) {
            double t = (double)i / kSeg;
            double y = (k == TDecel || k == TStrip) && (i == 0 || i == kSeg) ? t : eval(k, t);
            double q = y * 65536;
            r.v[k][i] = (int32_t)(q < 0 ? q - 0.5 : q + 0.5);
        }
    r.v[TPop][kSeg] = 65536;
    return r;
}

constexpr Tables kTables = make_tables();

fx lookup(int k, fx t)
{
    int64_t p = (int64_t)t * kSeg;
    int i = (int)(p >> kFxBits);
    if (i >= kSeg)
        return kTables.v[k][kSeg];
    fx f = (fx)(p & (kOne - 1));
    int32_t a = kTables.v[k][i], b = kTables.v[k][i + 1];
    return a + fx_mul(b - a, f);
}

}

fx curve(Curve c, fx t)
{
    t = fx_clamp(t, 0, kOne);
    switch (c) {
    case Curve::Linear:
        return t;
    case Curve::ExpOut6:
        return lookup(TExpOut6, t);
    case Curve::ExpIn6:
        return lookup(TExpIn6, t);
    case Curve::ExpIn1:
        return lookup(TExpIn1, t);
    case Curve::ExpOut1:
        return lookup(TExpOut1, t);
    case Curve::ExpIn15:
        return lookup(TExpIn15, t);
    case Curve::Decel:
        return lookup(TDecel, t);
    case Curve::Strip:
        return lookup(TStrip, t);
    case Curve::Sine:
        return lookup(TSine, t);
    case Curve::Pop:
        return lookup(TPop, t);
    case Curve::BezierPop: {
        constexpr fx s = 111515;
        fx m = t - kOne;
        return fx_mul(fx_mul(m, m), fx_mul(s + kOne, m) + s) + kOne;
    }
    case Curve::EaseIn:
        return fx_mul(t, t);
    case Curve::EaseOut:
        return fx_mul(t, 2 * kOne - t);
    case Curve::PowOut17:
        return lookup(TPowOut17, t);
    }
    return t;
}

fx sin_deg(fx deg)
{
    constexpr int64_t k90 = (int64_t)90 << kFxBits;
    int64_t d = deg % (4 * k90);
    if (d < 0)
        d += 4 * k90;
    bool neg = d >= 2 * k90;
    if (neg)
        d -= 2 * k90;
    if (d > k90)
        d = 2 * k90 - d;
    fx v = lookup(TSine, (fx)(d * kOne / k90));
    return neg ? -v : v;
}

fx cos_deg(fx deg) { return sin_deg(deg + fx_int(90)); }

}
