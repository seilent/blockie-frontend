#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <vector>

#include "../scale.h"
#include "../tile_grid.h"

namespace {

int g_fail = 0;
int g_total = 0;

void check(bool ok, const char* what)
{
    g_total++;
    if (ok) {
        std::printf("PASS %s\n", what);
    } else {
        std::printf("FAIL %s\n", what);
        g_fail++;
    }
}

std::vector<uint32_t> noise(int w, int h, uint32_t seed)
{
    std::vector<uint32_t> px((size_t)w * h);
    uint32_t s = seed;
    for (uint32_t& p : px) {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        p = s & 0xffffffu;
    }
    return px;
}

fd::Image cover(int side, uint32_t seed)
{
    std::vector<uint32_t> px = noise(side, side, seed);
    return fd::Image::from_portrait(px.data(), side, side);
}

fd::Buffer ground(uint32_t seed)
{
    fd::Buffer b;
    std::vector<uint32_t> g = noise(b.w, b.h, seed);
    for (size_t i = 0; i < b.pix.size(); i++)
        b.pix[i] = fd::LiveF::pack(g[i]);
    return b;
}

void k1(fd::Buffer& b, const fd::Image& img, fd::Rect64 r, fd::Rect clip, fd::Scaler k)
{
    fd::ScaleAxis xs, ys;
    fd::scale_axis(xs, r.x0, r.x1, img.w, clip.x0, clip.x1);
    fd::scale_axis(ys, r.y0, r.y1, img.h, clip.y0, clip.y1);
    fd::scale_tile(b.surface(), img, xs, ys, k);
}

void carved(fd::Buffer& b, const fd::Image& img, fd::Rect64 r, fd::Rect clip)
{
    fd::Canvas c(b.surface());
    c.set_clip(clip);
    c.blit_bilinear(img, r, 256);
}

int channel_diff(uint32_t a, uint32_t b)
{
    int m = 0;
    for (int sh = 0; sh < 24; sh += 8)
        m = std::max(m, std::abs((int)(a >> sh & 255) - (int)(b >> sh & 255)));
    return m;
}

int max_diff(const fd::Buffer& a, const fd::Buffer& b)
{
    int m = 0;
    for (size_t i = 0; i < a.pix.size(); i++)
        m = std::max(m, channel_diff(fd::LiveF::unpack(a.pix[i]), fd::LiveF::unpack(b.pix[i])));
    return m;
}

fd::Rect64 square(int x64, int y64, int side64) { return {x64, y64, x64 + side64, y64 + side64}; }

bool inside(fd::Rect r, int x, int y) { return x >= r.x0 && x < r.x1 && y >= r.y0 && y < r.y1; }

}

int main()
{
    const fd::Scaler both[] = {fd::Scaler::Channel, fd::Scaler::Swar};
    fd::Image img = cover(fd::kBigTileSide, 0x2545f491u);
    fd::Rect full{0, 0, fd::kPortraitW, fd::kPortraitH};
    fd::Buffer base = ground(0x9e3779b9u);

    {
        fd::Rect64 r = square(120 * 64, 80 * 64, fd::kBigTileSide * 64);
        bool copied = true, carved_eq = true;
        fd::Buffer ref = base;
        carved(ref, img, r, full);
        for (fd::Scaler k : both) {
            fd::Buffer got = base;
            k1(got, img, r, full, k);
            for (int y = 0; y < img.h; y++)
                for (int x = 0; x < img.w; x++)
                    if (got.surface().at(120 + x, 80 + y) != img.at(x, y))
                        copied = false;
            if (got.pix != ref.pix)
                carved_eq = false;
        }
        check(copied, "at identity both K1 variants copy the 232 image pixel for pixel");
        check(carved_eq, "at identity both K1 variants equal the carved bilinear");
    }

    {
        bool chan = true;
        int swar = 0;
        for (int side = fd::kTileSide; side <= fd::kBigTileSide; side++) {
            fd::Rect64 r = square(120 * 64 + 17, 80 * 64 + 41, side * 64 + 23);
            fd::Buffer ref = base, a = base, b = base;
            carved(ref, img, r, full);
            k1(a, img, r, full, fd::Scaler::Channel);
            k1(b, img, r, full, fd::Scaler::Swar);
            if (a.pix != ref.pix)
                chan = false;
            swar = std::max(swar, max_diff(ref, b));
        }
        check(chan, "K1 per channel equals the carved bilinear at sides 154 to 232 on fractional edges");
        check(swar <= 1, "K1 SWAR is within one level per channel of the carved bilinear at sides 154 to 232");
    }

    {
        bool chan = true;
        int swar = 0;
        for (fd::fx g = 0; g <= fd::kOne; g += fd::kOne / 16)
            for (int k : {0, 1, 3, 4}) {
                fd::Rect64 r = fd::grow_tile_rect(k, g, 0);
                fd::Buffer ref = base, a = base, b = base;
                carved(ref, img, r, full);
                k1(a, img, r, full, fd::Scaler::Channel);
                k1(b, img, r, full, fd::Scaler::Swar);
                if (a.pix != ref.pix)
                    chan = false;
                swar = std::max(swar, max_diff(ref, b));
            }
        check(chan && swar <= 1,
              "on the grow poses K1 per channel equals the carved bilinear and SWAR is within one level");
    }

    {
        fd::Rect clip{0, fd::kBandTop, 500, fd::kBandBottom};
        bool kept = true;
        for (fd::Scaler k : both)
            for (fd::Rect64 r : {fd::grow_tile_rect(7, fd::kOne / 2, 0), fd::grow_tile_rect(1, fd::kOne / 2, 1)}) {
                fd::Buffer whole = base, part = base;
                k1(whole, img, r, full, k);
                k1(part, img, r, clip, k);
                for (int y = 0; y < fd::kPortraitH; y++)
                    for (int x = 0; x < fd::kPortraitW; x++) {
                        size_t i = (size_t)y * fd::kPortraitW + x;
                        if (part.pix[i] != (inside(clip, x, y) ? whole.pix[i] : base.pix[i]))
                            kept = false;
                    }
            }
        check(kept, "the band and x < b clip keep every pixel outside and match the unclipped tile inside");
    }

    {
        fd::Rect64 r = square(300 * 64 + 9, 200 * 64 + 50, 190 * 64 + 31);
        fd::Rect outer = r.outer();
        fd::Buffer other = ground(0x85ebca6bu);
        bool outside = true, ring = true;
        int partial = 0;
        for (fd::Scaler k : both) {
            fd::Buffer a = base, b = other;
            k1(a, img, r, full, k);
            k1(b, img, r, full, k);
            for (int y = 0; y < fd::kPortraitH; y++)
                for (int x = 0; x < fd::kPortraitW; x++) {
                    size_t i = (size_t)y * fd::kPortraitW + x;
                    if (!inside(outer, x, y)) {
                        if (a.pix[i] != base.pix[i] || b.pix[i] != other.pix[i])
                            outside = false;
                        continue;
                    }
                    bool edge = x == outer.x0 || x == outer.x1 - 1 || y == outer.y0 || y == outer.y1 - 1;
                    if (a.pix[i] != b.pix[i]) {
                        partial++;
                        if (!edge)
                            ring = false;
                    }
                }
        }
        check(outside, "nothing is written outside the tile's outer rect");
        check(ring && partial > 0, "only the outer ring of pixels blends with the ground, the rest is opaque");
    }

    {
        fd::Image small = cover(fd::kTileSide, 7);
        fd::ScaleAxis xs, ys;
        fd::Rect64 r = square(120 * 64, 80 * 64, 200 * 64);
        fd::scale_axis(xs, r.x0, r.x1, fd::kBigTileSide, 0, fd::kPortraitW);
        fd::scale_axis(ys, r.y0, r.y1, fd::kBigTileSide, 0, fd::kPortraitH);
        fd::Buffer a = base;
        fd::scale_tile(a.surface(), small, xs, ys, fd::Scaler::Swar);
        check(a.pix == base.pix, "axes built for another source side draw nothing");
        fd::scale_axis(xs, r.x0, r.x1, fd::kBigTileSide, 0, 100);
        check(xs.taps.empty(), "an axis clipped wholly away from the tile has no taps");
    }

    std::vector<fd::Image> covers;
    for (int k = 0; k < 12; k++)
        covers.push_back(cover(fd::kBigTileSide, 0x1000u + (uint32_t)k));
    std::vector<fd::ScaleAxis> xs(fd::kBigCols), ys(covers.size() / fd::kBigCols);
    std::vector<fd::ScaleJob> jobs;
    auto pose = [&](fd::fx g, int q, fd::Rect clip) {
        for (int c = 0; c < (int)xs.size(); c++) {
            fd::Rect64 r = fd::grow_tile_rect(c, g, q);
            fd::scale_axis(xs[c], r.x0, r.x1, fd::kBigTileSide, clip.x0, clip.x1);
        }
        for (int row = 0; row < (int)ys.size(); row++) {
            fd::Rect64 r = fd::grow_tile_rect(row * fd::kBigCols, g, q);
            fd::scale_axis(ys[row], r.y0, r.y1, fd::kBigTileSide, clip.y0, clip.y1);
        }
        jobs.clear();
        for (size_t k = 0; k < covers.size(); k++)
            jobs.push_back({&covers[k], &xs[k % fd::kBigCols], &ys[k / fd::kBigCols]});
    };
    auto single = [&](fd::Buffer& b, fd::Scaler v) {
        for (const fd::ScaleJob& j : jobs)
            fd::scale_tile(b.surface(), *j.img, *j.xs, *j.ys, v);
    };

    auto rows_of = [&](const std::vector<fd::ScaleJob>& list, int& lo, int& hi) {
        int64_t n = 0;
        lo = fd::kPortraitH;
        hi = 0;
        for (const fd::ScaleJob& j : list) {
            if (j.xs->taps.empty() || j.ys->taps.empty())
                continue;
            int a = std::max(0, j.ys->p0), b = std::min(j.ys->p0 + (int)j.ys->taps.size(), fd::kPortraitH);
            if (a >= b)
                continue;
            n += b - a;
            lo = std::min(lo, a);
            hi = std::max(hi, b);
        }
        return n;
    };
    auto chunks_of = [&](const std::vector<fd::ScaleJob>& list) {
        int lo = 0, hi = 0;
        rows_of(list, lo, hi);
        return hi > lo ? (hi - lo + fd::kScaleChunkRows - 1) / fd::kScaleChunkRows : 0;
    };
    int64_t both_helper = 0, both_total = 0;
    auto shares = [&](fd::ScaleHelper& helper, const std::vector<fd::ScaleJob>& list, fd::Scaler v,
                      const fd::Buffer& want) {
        int lo = 0, hi = 0;
        int64_t rows = rows_of(list, lo, hi);
        int chunks = chunks_of(list);
        bool ok = true;
        for (fd::ScaleShare share : {fd::ScaleShare::Both, fd::ScaleShare::Ui, fd::ScaleShare::Helper}) {
            fd::Buffer got = base;
            helper.run(got.surface(), list, v, share);
            fd::ScaleSplit sp = helper.last();
            if (share == fd::ScaleShare::Both) {
                both_helper += sp.helper_chunks;
                both_total += sp.chunks;
            }
            int64_t did = sp.helper_rows;
            if (got.pix != want.pix || sp.chunks != chunks || sp.ui_chunks + sp.helper_chunks != sp.chunks)
                ok = false;
            if (share == fd::ScaleShare::Ui && (sp.helper_chunks != 0 || did != 0))
                ok = false;
            if (share == fd::ScaleShare::Helper && (sp.ui_chunks != 0 || did != rows))
                ok = false;
        }
        return ok;
    };

    {
        fd::ScaleHelper helper;
        bool equal = true, halves = true;
        for (int b : {500, fd::kPaneSplitWide})
            for (int q : {0, 2})
                for (fd::fx g = 0; g <= fd::kOne; g += fd::kOne / 16) {
                    pose(g, q, {0, fd::kBandTop, b, fd::kBandBottom});
                    for (fd::Scaler v : both) {
                        fd::Buffer one = base;
                        single(one, v);
                        if (!shares(helper, jobs, v, one))
                            equal = false;
                        for (int y : {0, fd::kBandTop + 101, 400, fd::kPortraitH}) {
                            fd::Buffer parts = base;
                            for (const fd::ScaleJob& j : jobs) {
                                fd::scale_tile(parts.surface(), *j.img, *j.xs, *j.ys, v, 0, y);
                                fd::scale_tile(parts.surface(), *j.img, *j.xs, *j.ys, v, y, fd::kPortraitH);
                            }
                            if (parts.pix != one.pix)
                                halves = false;
                        }
                    }
                }
        check(equal, "K3 is byte equal to single thread K1 on every grow pose with chunks shared, on the UI thread only"
                     " (the helper takes none) and on the helper only, and the chunk counts sum to the total");
        check(halves, "the rows above a split row and the rows from it together are the whole frame, both variants");
    }

    {
        fd::ScaleHelper helper;
        fd::Image ia = cover(fd::kBigTileSide, 0x77u), ib = cover(fd::kBigTileSide, 0x99u);
        fd::ScaleAxis ax, ay, bx, by;
        fd::Rect64 ra = square(100 * 64 + 13, 100 * 64 + 7, 200 * 64 + 11);
        fd::Rect64 rb = square(180 * 64 + 31, 160 * 64 + 45, 210 * 64 + 3);
        fd::scale_axis(ax, ra.x0, ra.x1, fd::kBigTileSide, 0, fd::kPortraitW);
        fd::scale_axis(ay, ra.y0, ra.y1, fd::kBigTileSide, 0, fd::kPortraitH);
        fd::scale_axis(bx, rb.x0, rb.x1, fd::kBigTileSide, 0, fd::kPortraitW);
        fd::scale_axis(by, rb.y0, rb.y1, fd::kBigTileSide, 0, fd::kPortraitH);
        std::vector<fd::ScaleJob> lap = {{&ia, &ax, &ay}, {&ib, &bx, &by}};
        bool same = true;
        for (fd::Scaler v : both) {
            fd::Buffer one = base;
            for (const fd::ScaleJob& j : lap)
                fd::scale_tile(one.surface(), *j.img, *j.xs, *j.ys, v);
            if (!shares(helper, lap, v, one))
                same = false;
        }
        check(same && chunks_of(lap) == (371 - 100 + fd::kScaleChunkRows - 1) / fd::kScaleChunkRows,
              "overlapping tiles on different row axes stay byte equal under every share of the chunks");
    }

    {
        {
            fd::ScaleHelper unused;
        }
        fd::ScaleHelper helper;
        std::vector<fd::ScaleJob> none;
        fd::Buffer empty = base;
        helper.run(empty.surface(), none, fd::Scaler::Swar);
        fd::ScaleSplit idle = helper.last();
        check(empty.pix == base.pix && idle.chunks == 0 && idle.ui_chunks == 0 && idle.helper_chunks == 0 &&
                  idle.helper_cpu >= 0 && idle.ui_cpu >= 0 && idle.helper_rows == 0,
              "a helper stops with no frame run, and a frame with no tiles has no chunks and writes nothing");
        pose(fd::kOne / 2, 0, {0, fd::kBandTop, fd::kPaneSplitWide, fd::kBandBottom});
        fd::Buffer want = base;
        single(want, fd::Scaler::Swar);
        bool repeat = true;
        for (int f = 0; f < 50; f++) {
            fd::Buffer got = base;
            helper.run(got.surface(), jobs, fd::Scaler::Swar);
            fd::ScaleSplit sp = helper.last();
            if (got.pix != want.pix || sp.ui_chunks + sp.helper_chunks != chunks_of(jobs))
                repeat = false;
        }
        fd::ScaleHelper second;
        fd::Buffer other = base;
        second.run(other.surface(), jobs, fd::Scaler::Swar);
        check(repeat && other.pix == want.pix, "one helper runs 50 frames in a row and a second helper runs beside it");
    }

    std::printf("INFO with both threads pulling, the helper took %lld of %lld chunks\n", (long long)both_helper,
                (long long)both_total);
    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
