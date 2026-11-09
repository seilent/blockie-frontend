#include "glyph.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

namespace fd {

namespace {

constexpr int kBox = 32;
constexpr int kStroke = 4;
constexpr int kSub = 4;
constexpr int kQ = 16;
constexpr double kMiterLimit = 2.0;

const GlyphPath kGlyphs[] = {
    {"back", false, "M 28 16 L 5 16 M 14 7 L 5 16 L 14 25"},
    {"play", true, "M 9 5 L 27 16 L 9 27 Z"},
    {"pause", true, "M 7 5 H 13 V 27 H 7 Z M 19 5 H 25 V 27 H 19 Z"},
    {"pauseWide", true, "M 6.43 5 H 12.43 V 27 H 6.43 Z M 19.57 5 H 25.57 V 27 H 19.57 Z"},
    {"next", true, "M 5 5 L 21 16 L 5 27 Z M 23 5 H 27 V 27 H 23 Z"},
    {"previous", true, "M 27 5 L 11 16 L 27 27 Z M 5 5 H 9 V 27 H 5 Z"},
    {"skipNext", true, "M 1.7 8 L 13.8 16 L 1.7 24 Z M 13.8 8 L 25.9 16 L 13.8 24 Z M 27.35 8 H 30.3 V 24 H 27.35 Z"},
    {"skipPrevious", true, "M 30.3 8 L 18.2 16 L 30.3 24 Z M 18.2 8 L 6.1 16 L 18.2 24 Z M 4.65 8 H 1.7 V 24 H 4.65 Z"},
    {"shuffle", false, "M 4 9 H 10 L 22 23 H 28 M 4 23 H 10 L 22 9 H 28 M 24 5 L 28 9 L 24 13 M 24 19 L 28 23 L 24 27"},
    {"repeat", false, "M 23 9 A 10 10 0 1 0 26 16 M 18 4 L 23 9 L 18 14"},
    {"one", false, "M 18.2 10.19 V 22.24 M 12.9 13.79 L 16.3 11.39"},
    {"heart", true, "M 16 28 L 5.5 17 A 7 7 0 0 1 16 9 A 7 7 0 0 1 26.5 17 Z"},
    {"plus", false, "M 16 4 V 28 M 4 16 H 28"},
    {"minus", false, "M 4 16 H 28"},
    {"bluetooth", false, "M 8 10 L 24 22 L 16 29 V 3 L 24 10 L 8 22"},
    {"battery", false, "M 3 9 H 25 V 23 H 3 Z M 27 13 V 19"},
    {"wifiDot", true, "M 28 28 H 21 A 7 7 0 0 1 28 21 Z"},
    {"wifiArc1", false, "M 14 28 A 14 14 0 0 1 28 14"},
    {"wifiArc2", false, "M 6 28 A 22 22 0 0 1 28 6"},
    {"speaker", true, "M 2 10.6 H 7.4 L 14.6 5.2 V 26.8 L 7.4 21.4 H 2 Z"},
    {"speakerWaves", false, "M 19.1 8.8 A 9 9 0 0 1 19.1 23.2 M 24.1 6.1 A 14.4 14.4 0 0 1 24.1 25.9"},
    {"micHead", true, "M 12 7 A 4 4 0 0 1 20 7 V 17.5 A 4 4 0 0 1 12 17.5 Z"},
    {"micBase", true,
     "M 8 12.6 V 17.4 A 8 8 0 0 0 24 17.4 V 12.6 H 21.8 V 17.4 A 5.8 5.8 0 0 1 10.2 17.4 V 12.6 Z M 14.85 25.3 V 27.6 H 17.15 V "
     "25.3 Z M 11.5 27.6 V 30 H 20.5 V 27.6 Z"},
    {"searchRing", true,
     "M 9.31 12.94 A 10 10 0 1 1 29.31 12.94 A 10 10 0 1 1 9.31 12.94 Z M 12.75 12.94 A 6.56 6.56 0 1 0 25.87 12.94 A 6.56 "
     "6.56 0 1 0 12.75 12.94 Z"},
    {"searchHandle", true, "M 15.63 20.58 L 11.67 16.62 L 3.4 24.89 A 2.8 2.8 0 0 0 7.36 28.85 Z"},
    {"home", false, "M 3 15.5 L 16 3 L 29 15.5 M 6 12.6 V 26 H 26 V 12.6 M 12.5 26 V 18.5 H 19.5 V 26"},
    {"disc", false, "M 4 16 A 12 12 0 1 1 28 16 A 12 12 0 1 1 4 16 Z M 14 16 A 2 2 0 1 1 18 16 A 2 2 0 1 1 14 16 Z"},
    {"gear", false,
     "M 13.70 6.78 L 13.97 3.16 L 18.03 3.16 L 18.30 6.78 L 20.89 7.86 L 23.64 5.48 L 26.52 8.36 L 24.14 11.11 L 25.22 13.70 "
     "L 28.84 13.97 L 28.84 18.03 L 25.22 18.30 L 24.14 20.89 L 26.52 23.64 L 23.64 26.52 L 20.89 24.14 L 18.30 25.22 L 18.03 "
     "28.84 L 13.97 28.84 L 13.70 25.22 L 11.11 24.14 L 8.36 26.52 L 5.48 23.64 L 7.86 20.89 L 6.78 18.30 L 3.16 18.03 L 3.16 "
     "13.97 L 6.78 13.70 L 7.86 11.11 L 5.48 8.36 L 8.36 5.48 L 11.11 7.86 Z M 12.5 16 A 3.5 3.5 0 1 1 19.5 16 A 3.5 3.5 0 1 1 "
     "12.5 16 Z"},
    {"queue", true, "M 8 6.98 H 24 V 10.59 H 8 Z M 8 14.2 H 20 V 17.81 H 8 Z M 8 21.41 H 16 V 25.02 H 8 Z"},
};

struct Pt {
    int64_t x, y;
};

struct Sub {
    std::vector<Pt> p;
    bool closed = false;
};

int64_t q(double v) { return (int64_t)std::llround(v * (1 << kQ)); }

void arc(Sub& s, double x1, double y1, double r, bool large, bool sweep, double x2, double y2)
{
    double dx = (x1 - x2) / 2, dy = (y1 - y2) / 2;
    double l = (dx * dx + dy * dy) / (r * r);
    if (l > 1)
        r *= std::sqrt(l);
    double num = r * r * r * r - r * r * dy * dy - r * r * dx * dx;
    double den = r * r * dy * dy + r * r * dx * dx;
    double co = den > 0 && num > 0 ? std::sqrt(num / den) : 0;
    if (large == sweep)
        co = -co;
    double cxp = co * dy, cyp = -co * dx;
    double cx = cxp + (x1 + x2) / 2, cy = cyp + (y1 + y2) / 2;
    double t1 = std::atan2((dy - cyp) / r, (dx - cxp) / r);
    double dt = std::atan2((-dy - cyp) / r, (-dx - cxp) / r) - t1;
    if (!sweep && dt > 0)
        dt -= 2 * M_PI;
    if (sweep && dt < 0)
        dt += 2 * M_PI;
    int n = std::max(2, (int)std::ceil(std::fabs(dt) / (M_PI / 24)));
    for (int i = 1; i <= n; i++) {
        double t = t1 + dt * i / n;
        s.p.push_back({q(cx + r * std::cos(t)), q(cy + r * std::sin(t))});
    }
}

std::vector<Sub> parse(const char* d)
{
    std::vector<Sub> out;
    double x = 0, y = 0;
    const char* s = d;
    auto num = [&]() {
        char* e = nullptr;
        double v = strtod(s, &e);
        s = e;
        return v;
    };
    while (*s) {
        while (*s == ' ')
            s++;
        char c = *s;
        if (!c)
            break;
        s++;
        if (c != 'M' && out.empty()) {
            out.push_back({});
            out.back().p.push_back({q(x), q(y)});
        }
        if (c == 'M') {
            x = num();
            y = num();
            out.push_back({});
            out.back().p.push_back({q(x), q(y)});
        } else if (c == 'L') {
            x = num();
            y = num();
            out.back().p.push_back({q(x), q(y)});
        } else if (c == 'H') {
            x = num();
            out.back().p.push_back({q(x), q(y)});
        } else if (c == 'V') {
            y = num();
            out.back().p.push_back({q(x), q(y)});
        } else if (c == 'A') {
            double r = num();
            num();
            num();
            bool large = num() != 0, sweep = num() != 0;
            double nx = num(), ny = num();
            arc(out.back(), x, y, r, large, sweep, nx, ny);
            x = nx;
            y = ny;
        } else if (c == 'Z') {
            out.back().closed = true;
        }
    }
    return out;
}

struct Quad {
    Pt v[4];
    int n;
};

int64_t cross(const Pt& a, const Pt& b, int64_t x, int64_t y)
{
    return (b.x - a.x) * (y - a.y) - (x - a.x) * (b.y - a.y);
}

int crossing(const Pt& a, const Pt& b, int64_t x, int64_t y)
{
    if (a.y <= y)
        return b.y > y && cross(a, b, x, y) > 0 ? 1 : 0;
    return b.y <= y && cross(a, b, x, y) < 0 ? -1 : 0;
}

int winding(const Pt* v, size_t n, int64_t x, int64_t y)
{
    int wind = 0;
    for (size_t i = 0; i < n; i++)
        wind += crossing(v[i], v[(i + 1) % n], x, y);
    return wind;
}

struct Seg {
    Pt a, b;
    double ux, uy, len;
};

struct Edge {
    Pt a, b;
};

struct Box {
    int64_t x0, y0, x1, y1;
};

Box box_of(const Pt* v, size_t n, int64_t m)
{
    Box b{v[0].x, v[0].y, v[0].x, v[0].y};
    for (size_t i = 1; i < n; i++) {
        b.x0 = std::min(b.x0, v[i].x);
        b.y0 = std::min(b.y0, v[i].y);
        b.x1 = std::max(b.x1, v[i].x);
        b.y1 = std::max(b.y1, v[i].y);
    }
    return {b.x0 - m, b.y0 - m, b.x1 + m, b.y1 + m};
}

struct Shape {
    bool fill;
    std::vector<Sub> subs;
    std::vector<Seg> segs;
    std::vector<Quad> joins;
    std::vector<Edge> edges;
    std::vector<Box> edge_box, seg_box, join_box;
};

struct Near {
    std::vector<int> edges, segs, joins;
};

void stroke(Shape& sh, double hw)
{
    for (const Sub& s : sh.subs) {
        size_t n = s.p.size();
        size_t first = sh.segs.size();
        size_t count = s.closed ? n : n - 1;
        for (size_t i = 0; i < count; i++) {
            Pt a = s.p[i], b = s.p[(i + 1) % n];
            double dx = (double)(b.x - a.x), dy = (double)(b.y - a.y);
            double len = std::sqrt(dx * dx + dy * dy);
            if (len <= 0)
                continue;
            sh.segs.push_back({a, b, dx / len, dy / len, len});
        }
        size_t m = sh.segs.size() - first;
        for (size_t i = 0; i + 1 < m || (s.closed && i < m && m > 1); i++) {
            const Seg& s1 = sh.segs[first + i];
            const Seg& s2 = sh.segs[first + (i + 1) % m];
            double cr = s1.ux * s2.uy - s1.uy * s2.ux;
            double dot = s1.ux * s2.ux + s1.uy * s2.uy;
            if (std::fabs(cr) < 1e-9 && dot > 0)
                continue;
            double side = cr > 0 ? -1 : 1;
            double n1x = -s1.uy * side, n1y = s1.ux * side;
            double n2x = -s2.uy * side, n2y = s2.ux * side;
            Pt v = s1.b;
            double h = hw * (1 << kQ);
            Pt a{v.x + (int64_t)(n1x * h), v.y + (int64_t)(n1y * h)};
            Pt b{v.x + (int64_t)(n2x * h), v.y + (int64_t)(n2y * h)};
            double mx = n1x + n2x, my = n1y + n2y;
            double ml = std::sqrt(mx * mx + my * my);
            double c = ml / 2;
            Quad qd;
            if (c > 1e-9 && 1 / c <= 2 * kMiterLimit) {
                double k = h / c / ml;
                Pt mpt{v.x + (int64_t)(mx * k), v.y + (int64_t)(my * k)};
                qd = {{v, a, mpt, b}, 4};
            } else {
                qd = {{v, a, b, v}, 3};
            }
            sh.joins.push_back(qd);
        }
    }
}

void boxes(Shape& sh, double hw)
{
    if (sh.fill)
        for (const Sub& s : sh.subs)
            for (size_t i = 0; i < s.p.size(); i++) {
                Pt v[2] = {s.p[i], s.p[(i + 1) % s.p.size()]};
                sh.edges.push_back({v[0], v[1]});
                sh.edge_box.push_back(box_of(v, 2, 0));
            }
    int64_t m = (int64_t)std::ceil(hw * (1 << kQ)) + 2;
    for (const Seg& s : sh.segs) {
        Pt v[2] = {s.a, s.b};
        sh.seg_box.push_back(box_of(v, 2, m));
    }
    for (const Quad& qd : sh.joins)
        sh.join_box.push_back(box_of(qd.v, (size_t)qd.n, 0));
}

void near_row(const Shape& sh, int64_t y0, int64_t y1, Near& n)
{
    n.edges.clear();
    n.segs.clear();
    n.joins.clear();
    for (size_t i = 0; i < sh.edge_box.size(); i++)
        if (sh.edge_box[i].y0 <= y1 && sh.edge_box[i].y1 >= y0)
            n.edges.push_back((int)i);
    for (size_t i = 0; i < sh.seg_box.size(); i++)
        if (sh.seg_box[i].y0 <= y1 && y0 <= sh.seg_box[i].y1)
            n.segs.push_back((int)i);
    for (size_t i = 0; i < sh.join_box.size(); i++)
        if (sh.join_box[i].y0 <= y1 && y0 <= sh.join_box[i].y1)
            n.joins.push_back((int)i);
}

void near(const Shape& sh, const Near& row, int64_t x0, int64_t x1, Near& n)
{
    n.edges.clear();
    n.segs.clear();
    n.joins.clear();
    for (int i : row.edges)
        if (sh.edge_box[i].x1 >= x0)
            n.edges.push_back(i);
    for (int i : row.segs)
        if (sh.seg_box[i].x0 <= x1 && x0 <= sh.seg_box[i].x1)
            n.segs.push_back(i);
    for (int i : row.joins)
        if (sh.join_box[i].x0 <= x1 && x0 <= sh.join_box[i].x1)
            n.joins.push_back(i);
}

bool inside(const Shape& sh, const Near& n, int64_t x, int64_t y, double hw)
{
    if (sh.fill) {
        int wind = 0;
        for (int i : n.edges)
            wind += crossing(sh.edges[i].a, sh.edges[i].b, x, y);
        return wind != 0;
    }
    double h = hw * (1 << kQ);
    for (int i : n.segs) {
        const Seg& s = sh.segs[i];
        double px = (double)(x - s.a.x), py = (double)(y - s.a.y);
        double t = px * s.ux + py * s.uy;
        if (t < 0 || t > s.len)
            continue;
        double d = px * s.uy - py * s.ux;
        if (d <= h && d >= -h)
            return true;
    }
    for (int i : n.joins)
        if (winding(sh.joins[i].v, (size_t)sh.joins[i].n, x, y))
            return true;
    return false;
}

void store(Coverage& out, int x, int y, int v)
{
    out.rot[out.index(x, y)] = (uint8_t)v;
}

Shape make_shape(const char* d, bool fill)
{
    Shape sh;
    sh.fill = fill;
    sh.subs = parse(d);
    double hw = kStroke / 2.0;
    if (!sh.fill)
        stroke(sh, hw);
    boxes(sh, hw);
    return sh;
}

const Shape& glyph_shape(ptrdiff_t i)
{
    static const std::vector<Shape> shapes = [] {
        std::vector<Shape> v;
        for (const GlyphPath& d : kGlyphs)
            v.push_back(make_shape(d.d, d.fill));
        return v;
    }();
    return shapes[i];
}

bool cover(const Shape& sh, int px, Coverage& out, fx scale)
{
    if (px <= 0)
        return false;
    double hw = kStroke / 2.0;
    out.w = out.h = px;
    out.rot.assign((size_t)px * px, 0);
    double size = px * (scale / 65536.0), k = kBox / size, off = (px - size) / 2;
    std::vector<int64_t> lo((size_t)px), hi((size_t)px), at((size_t)px * kSub);
    for (int v = 0; v < px; v++) {
        lo[v] = q((v + 0.5 / kSub - off) * k);
        hi[v] = q((v + (kSub - 0.5) / kSub - off) * k);
        for (int i = 0; i < kSub; i++)
            at[v * kSub + i] = q((v + (i + 0.5) / kSub - off) * k);
    }
    Near row, n;
    for (int y = 0; y < px; y++) {
        near_row(sh, lo[y], hi[y], row);
        if (row.edges.empty() && row.segs.empty() && row.joins.empty())
            continue;
        const int64_t* sy = &at[y * kSub];
        for (int x = 0; x < px; x++) {
            near(sh, row, lo[x], hi[x], n);
            if (n.edges.empty() && n.segs.empty() && n.joins.empty())
                continue;
            const int64_t* sx = &at[x * kSub];
            int hit = 0;
            for (int j = 0; j < kSub; j++)
                for (int i = 0; i < kSub; i++)
                    hit += inside(sh, n, sx[i], sy[j], hw);
            store(out, x, y, hit * 255 / (kSub * kSub));
        }
    }
    return true;
}

}

const GlyphPath* glyph_path(size_t i) { return i < sizeof(kGlyphs) / sizeof(kGlyphs[0]) ? &kGlyphs[i] : nullptr; }

bool glyph_coverage(const char* name, int px, Coverage& out, fx scale)
{
    for (const GlyphPath& d : kGlyphs)
        if (!strcmp(d.name, name))
            return cover(glyph_shape(&d - kGlyphs), px, out, scale);
    return false;
}

bool path_coverage(const char* d, bool fill, int px, Coverage& out, fx scale)
{
    return px > 0 && cover(make_shape(d, fill), px, out, scale);
}

void ring_coverage(int px, int stroke_px, Coverage& out, fx scale)
{
    out.w = out.h = px;
    out.rot.assign((size_t)px * px, 0);
    int64_t n = (int64_t)px * kSub;
    int64_t o = n * scale, in = (n - 2 * (int64_t)stroke_px * kSub) * scale;
    int64_t ro = o * o, ri = in * in;
    size_t m = px > 0 ? (size_t)px : 0;
    std::vector<int64_t> lo(m), hi(m), sq(m * kSub);
    for (int v = 0; v < px; v++) {
        int64_t a = 2 * (int64_t)v * kSub + 1 - n, b = a + 2 * (kSub - 1);
        int64_t l = a > 0 ? a : b < 0 ? -b : 0, h = std::max(std::abs(a), std::abs(b));
        lo[v] = (l * l) << (2 * kFxBits);
        hi[v] = (h * h) << (2 * kFxBits);
    }
    for (size_t s = 0; s < sq.size(); s++) {
        int64_t d = 2 * (int64_t)s + 1 - n;
        sq[s] = (d * d) << (2 * kFxBits);
    }
    for (int y = 0; y < px; y++) {
        const int64_t* sy = &sq[y * kSub];
        for (int x = 0; x < px; x++) {
            int64_t l = lo[x] + lo[y], h = hi[x] + hi[y];
            if (l > ro || h < ri)
                continue;
            if (h <= ro && l >= ri) {
                store(out, x, y, 255);
                continue;
            }
            const int64_t* sx = &sq[x * kSub];
            int hit = 0;
            for (int j = 0; j < kSub; j++)
                for (int i = 0; i < kSub; i++) {
                    int64_t d = sx[i] + sy[j];
                    hit += d <= ro && d >= ri;
                }
            store(out, x, y, hit * 255 / (kSub * kSub));
        }
    }
}

void arc_coverage(int px, int stroke_px, int start_deg, int sweep_deg, Coverage& out, fx scale)
{
    ring_coverage(px, stroke_px, out, scale);
    if (sweep_deg >= 360)
        return;
    const double rad = 3.14159265358979323846 / 180.0;
    int64_t sx = std::llround(std::cos(start_deg * rad) * 65536), sy = std::llround(std::sin(start_deg * rad) * 65536);
    int64_t ex = std::llround(std::cos((start_deg + sweep_deg) * rad) * 65536);
    int64_t ey = std::llround(std::sin((start_deg + sweep_deg) * rad) * 65536);
    for (int y = 0; y < px; y++)
        for (int x = 0; x < px; x++) {
            int64_t dx = 2 * x + 1 - px, dy = 2 * y + 1 - px;
            int64_t cs = sx * dy - sy * dx, pe = dx * ey - dy * ex;
            bool in = sweep_deg <= 180 ? cs >= 0 && pe > 0 : !(pe <= 0 && cs < 0);
            if (!in)
                store(out, x, y, 0);
        }
}

void glow_coverage(const Coverage& src, int grow, Coverage& out, int hole, int reach)
{
    int w = src.w + 2 * grow, h = src.h + 2 * grow;
    out.w = w;
    out.h = h;
    out.rot.assign((size_t)std::max(0, w) * std::max(0, h), 0);
    if (src.w <= 0 || grow <= 0)
        return;
    std::vector<uint8_t> a((size_t)w * h, 0), b((size_t)w * h, 0);
    for (int y = 0; y < src.h; y++)
        for (int x = 0; x < src.w; x++)
            a[(size_t)(y + grow) * w + x + grow] = src.rot[src.index(x, y)];
    auto pass = [&](const std::vector<uint8_t>& in, std::vector<uint8_t>& to, int r, int len, int lines, size_t step,
                    size_t line) {
        int n = 2 * r + 1;
        for (int l = 0; l < lines; l++) {
            const uint8_t* sp = in.data() + (size_t)l * line;
            uint8_t* dp = to.data() + (size_t)l * line;
            int sum = 0;
            for (int i = 0; i < std::min(r, len); i++)
                sum += sp[(size_t)i * step];
            for (int i = 0; i < len; i++) {
                if (i + r < len)
                    sum += sp[(size_t)(i + r) * step];
                if (i - r - 1 >= 0)
                    sum -= sp[(size_t)(i - r - 1) * step];
                dp[(size_t)i * step] = (uint8_t)((sum + n / 2) / n);
            }
        }
    };
    for (int k = 0; k < 3; k++) {
        int r = reach > 0 ? (reach + 2 - k) / 3 : std::max(1, grow / 3);
        if (r <= 0)
            continue;
        pass(a, b, r, w, h, 1, (size_t)w);
        pass(b, a, r, h, w, (size_t)w, 1);
    }
    int64_t cut = (int64_t)4 * hole * hole;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int64_t dx = 2 * x + 1 - w, dy = 2 * y + 1 - h;
            store(out, x, y, hole > 0 && dx * dx + dy * dy < cut ? 0 : a[(size_t)y * w + x]);
        }
}

void dilate_coverage(const Coverage& src, int r, Coverage& out)
{
    out = src;
    if (src.w <= 0 || src.h <= 0 || r <= 0)
        return;
    int w = src.w, h = src.h;
    std::vector<uint8_t> a((size_t)w * h), b((size_t)w * h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            a[(size_t)y * w + x] = src.rot[src.index(x, y)];
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint8_t m = 0;
            for (int i = std::max(0, x - r); i <= std::min(w - 1, x + r); i++)
                m = std::max(m, a[(size_t)y * w + i]);
            b[(size_t)y * w + x] = m;
        }
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint8_t m = 0;
            for (int i = std::max(0, y - r); i <= std::min(h - 1, y + r); i++)
                m = std::max(m, b[(size_t)i * w + x]);
            store(out, x, y, m);
        }
}

void scale_coverage(const Coverage& src, int px, Coverage& out)
{
    out.w = out.h = px;
    out.rot.assign((size_t)px * px, 0);
    if (src.w <= 0 || px <= 0)
        return;
    auto at = [&](int x, int y) -> int {
        if (x < 0 || y < 0 || x >= src.w || y >= src.h)
            return 0;
        return src.rot[src.index(x, y)];
    };
    int64_t step = ((int64_t)src.w << 16) / px;
    for (int y = 0; y < px; y++) {
        int64_t sy = (((int64_t)y << 16) + (1 << 15)) * src.h / px - (1 << 15);
        int y0 = (int)(sy >> 16);
        int fy = (int)(sy & 0xFFFF);
        if (sy < 0) {
            y0 = -1;
            fy = (int)(sy + 65536);
        }
        for (int x = 0; x < px; x++) {
            int64_t sx = ((int64_t)x * step) + (step >> 1) - (1 << 15);
            int x0 = (int)(sx >> 16);
            int fx = (int)(sx & 0xFFFF);
            if (sx < 0) {
                x0 = -1;
                fx = (int)(sx + 65536);
            }
            int64_t top = (int64_t)at(x0, y0) * (65536 - fx) + (int64_t)at(x0 + 1, y0) * fx;
            int64_t bot = (int64_t)at(x0, y0 + 1) * (65536 - fx) + (int64_t)at(x0 + 1, y0 + 1) * fx;
            store(out, x, y, (int)((top * (65536 - fy) + bot * fy) >> 32));
        }
    }
}

const Coverage& GlyphCache::arc(int px, int stroke_px, int start_deg, int sweep_deg, fx scale)
{
    std::string k = "arc:" + std::to_string(px) + ":" + std::to_string(stroke_px) + ":" + std::to_string(start_deg) + ":" +
                    std::to_string(sweep_deg);
    if (scale != kOne)
        k += ":s" + std::to_string(scale);
    auto it = map_.find(k);
    if (it != map_.end())
        return it->second;
    Coverage& cv = map_[k];
    arc_coverage(px, stroke_px, start_deg, sweep_deg, cv, scale);
    misses_++;
    bytes_ += cv.rot.size();
    return cv;
}

const Coverage& GlyphCache::glyph(const char* name, int px, fx scale)
{
    std::string k = std::string(name) + ":" + std::to_string(px);
    if (scale != kOne)
        k += ":s" + std::to_string(scale);
    auto it = map_.find(k);
    if (it != map_.end())
        return it->second;
    Coverage& cv = map_[k];
    glyph_coverage(name, px, cv, scale);
    misses_++;
    bytes_ += cv.rot.size();
    return cv;
}

const Coverage& GlyphCache::ring(int px, int stroke_px, fx scale)
{
    std::string k = "ring:" + std::to_string(px) + ":" + std::to_string(stroke_px);
    if (scale != kOne)
        k += ":s" + std::to_string(scale);
    auto it = map_.find(k);
    if (it != map_.end())
        return it->second;
    Coverage& cv = map_[k];
    ring_coverage(px, stroke_px, cv, scale);
    misses_++;
    bytes_ += cv.rot.size();
    return cv;
}

bool GlyphCache::warm(const Icon& i)
{
    size_t m = misses_;
    if (i.glyph)
        glyph(i.glyph, i.px, i.scale);
    else
        ring(i.px, i.stroke, i.scale);
    return misses_ != m;
}

void draw_centered(Canvas& c, const Coverage& cv, int cx, int cy, uint32_t rgb, int a)
{
    if (cv.w > 0)
        c.coverage(cv.rot.data(), cv.w, cv.h, cx - cv.w / 2, cy - cv.h / 2, rgb, a);
}

}
