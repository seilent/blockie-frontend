#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "../engine.h"
#include "../motion.h"
#include "../pageform.h"
#include "../theme.h"
#include "../tile_grid.h"

using namespace fd;

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

uint32_t ground() { return LiveF::pack(theme().bg); }

void paint(Buffer& b, uint32_t mask)
{
    Surface s = b.surface();
    uint32_t g = ground();
    for (int y = 0; y < s.h; y++)
        for (int x = 0; x < s.w; x++) {
            uint32_t v = ((uint32_t)((x * 7 + y * 13) & 0xFF) << 16 | (uint32_t)((x * 3) & 0xFF) << 8 |
                          (uint32_t)(y & 0xFF)) ^
                         mask;
            s.at(x, y) = v == g ? v ^ 1 : v;
        }
}

void copy(const Surface& src, const Surface& dst)
{
    for (int y = 0; y < dst.h; y++)
        copy_px(dst.row(y), src.row(y), dst.w);
}

bool equal(const Surface& a, const Surface& b)
{
    for (int y = 0; y < a.h; y++)
        if (!std::equal(a.row(y), a.row(y) + a.w, b.row(y)))
            return false;
    return true;
}

constexpr int64_t kFrameThirds = 50;
constexpr int64_t kShortFrameThirds = 48;

constexpr int one_step(int step, int64_t dt3)
{
    return (int)std::min<int64_t>(eng::kFull, (int64_t)step * 256 * dt3 / kFrameThirds);
}

constexpr int rise_frames(int step)
{
    return (int)(((int64_t)eng::kFull + one_step(step, kShortFrameThirds) - 1) / one_step(step, kShortFrameThirds));
}

constexpr int kTextFrames = rise_frames(eng::kTextStep);
constexpr int kArtFrames = rise_frames(eng::kArtStep);

bool toward(uint32_t got, uint32_t want, uint32_t g)
{
    for (int sh = 0; sh < 24; sh += 8) {
        int v = (int)(got >> sh & 0xFF), w = (int)(want >> sh & 0xFF), o = (int)(g >> sh & 0xFF);
        if (v < std::min(w, o) || v > std::max(w, o))
            return false;
    }
    return true;
}

bool covered(const eng::Instance& in)
{
    const std::vector<uint16_t>& ids = in.ids();
    return std::find(ids.begin(), ids.end(), (uint16_t)0) == ids.end();
}

bool same_rect(Rect a, Rect b) { return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1; }

bool same_poses(const eng::Frame& a, const eng::Frame& b)
{
    if (a.poses.size() != b.poses.size())
        return false;
    for (size_t i = 0; i < a.poses.size(); i++) {
        const eng::Pose& p = a.poses[i];
        const eng::Pose& q = b.poses[i];
        if (p.block != q.block || !same_rect(p.rect, q.rect) || !same_rect(p.clip, q.clip))
            return false;
    }
    return true;
}

bool same_blocks(const eng::Frame& a, const eng::Frame& b)
{
    if (a.blocks.size() != b.blocks.size())
        return false;
    for (size_t i = 0; i < a.blocks.size(); i++) {
        const eng::Block& p = a.blocks[i];
        const eng::Block& q = b.blocks[i];
        if (p.id != q.id || p.ink != q.ink || p.z != q.z || p.source != q.source || !same_rect(p.src, q.src))
            return false;
    }
    return true;
}

bool at_rest(const eng::Frame& f, int source)
{
    int n = 0;
    for (const eng::Pose& q : f.poses) {
        const eng::Block& k = f.blocks[(size_t)q.block];
        if (k.source != source)
            continue;
        n++;
        if (!same_rect(q.rect, k.src) || !same_rect(q.clip.intersect(k.src), k.src))
            return false;
    }
    int want = 0;
    for (const eng::Block& k : f.blocks)
        want += k.source == source;
    return n == want;
}

bool hidden(const eng::Frame& f, int source)
{
    for (const eng::Pose& q : f.poses) {
        const eng::Block& k = f.blocks[(size_t)q.block];
        if (k.source == source && !q.rect.intersect(q.clip).intersect(kScreen).empty())
            return false;
    }
    return true;
}

PageForm form(bool closing)
{
    PageForm f;
    f.closing = closing;
    f.under = page_inks(page_art(kPaneSplit, true));
    f.over = page_inks(Rect{});
    return f;
}

int64_t frame_ms(int n) { return (int64_t)n * 1000 / 60; }

constexpr int kSpecBands[] = {0, 196, 282, 368};

std::vector<PageRow> spec_settings(int pressed)
{
    return {{"settings-header", {0, 0, kPortraitW, 140}},
            {"settings-pivots", {0, 116, kPortraitW, 172}},
            {"settings-row0", {0, 196, kPortraitW, 282}, eng::Ink::Text, pressed == 0},
            {"settings-row1", {0, 282, kPortraitW, 368}, eng::Ink::Text, pressed == 1}};
}

std::vector<PageRow> spec_bluetooth()
{
    std::vector<PageRow> out{{"bluetooth-header", {0, 0, kPortraitW, 196}}};
    for (int r = 0; r < 5; r++)
        out.push_back({"bluetooth-row" + std::to_string(r), {0, 196 + 80 * r, kPortraitW, 276 + 80 * r}});
    return out;
}

PageForm part_form(bool closing, int pair)
{
    PageForm f = form(closing);
    if (pair == 0) {
        f.under_els = home_elements(kPaneSplit, true, 5);
        f.over_els = page_elements("settings", "chrome", spec_settings(-1));
    } else {
        f.under = page_inks(Rect{});
        f.under_els = page_elements("settings", "chrome", spec_settings(1));
        f.over_els = page_elements("bluetooth", "header", spec_bluetooth());
    }
    return f;
}

bool tiles(const std::vector<PageRow>& els)
{
    std::vector<uint8_t> n((size_t)kPortraitW * kPortraitH, 0);
    for (const PageRow& e : els) {
        if (!same_rect(e.r.intersect(kScreen), e.r))
            return false;
        for (int y = e.r.y0; y < e.r.y1; y++)
            for (int x = e.r.x0; x < e.r.x1; x++)
                n[(size_t)y * kPortraitW + (size_t)x]++;
    }
    return std::all_of(n.begin(), n.end(), [](uint8_t v) { return v == 1; });
}

int spec_band(const PageRow& e)
{
    int b = 0;
    for (int i = 0; i < 4; i++)
        if (kSpecBands[i] <= e.r.y0)
            b = i;
    return b + (e.pressed ? 1 : 0);
}

int spec_settle_dy(int top) { return top == 0 ? 0 : (int)std::lround(0.21 * (649 - top)); }

int thirds_of(int dist, int j) { return (2 * dist * j + 3) / 6; }

struct Off {
    int dx = -100000;
    int dy = -100000;
};

Off off_of(const eng::Frame& fr, size_t block)
{
    for (const eng::Pose& q : fr.poses)
        if ((size_t)q.block == block) {
            const Rect& s = fr.blocks[block].src;
            return {q.rect.x0 - s.x0, q.rect.y0 - s.y0};
        }
    return {};
}

struct Leave {
    int dist;
    int sign;
    int band;
};

Leave leave_of(const PageRow& e)
{
    bool left = e.r.x0 == 0 && e.r.x1 < kPortraitW;
    return {left ? e.r.x1 : kPortraitW - e.r.x0, left ? -1 : 1, spec_band(e)};
}

bool owned_ok(const Surface& out, const eng::Instance& in, const eng::Frame& fr, const Surface& a, const Surface& b)
{
    uint32_t g = ground();
    const std::vector<uint16_t>& ids = in.ids();
    for (int y = 0; y < out.h; y++)
        for (int x = 0; x < out.w; x++) {
            size_t id = ids[(size_t)y * (size_t)out.w + (size_t)x] & eng::kIdMask;
            if (id == 0) {
                if (out.at(x, y) != g)
                    return false;
                continue;
            }
            if (id > fr.blocks.size())
                return false;
            const eng::Block& k = fr.blocks[id - 1];
            Off o = off_of(fr, id - 1);
            int sx = x - o.dx, sy = y - o.dy;
            if (sx < k.src.x0 || sx >= k.src.x1 || sy < k.src.y0 || sy >= k.src.y1)
                return false;
            if (!toward(out.at(x, y), (k.source == 0 ? a : b).at(sx, sy), g))
                return false;
        }
    return true;
}

struct Drive {
    Buffer a, b, out, want;
    Buffer* dest = &b;
    Buffer* shown = &a;
    MotionCtl ctl;
    eng::Instance in{kScreen};
    std::vector<std::string> landed;

    Drive()
    {
        paint(a, 0x103050);
        paint(b, 0xC06010);
        ctl.render = [this](const Surface& s, Rect) { copy(dest->surface(), s); };
        ctl.last = [this](const Surface& s) { copy(shown->surface(), s); };
        ctl.render_kept = ctl.last;
        ctl.landed = [this](const char* what, const char* why) { landed.push_back(std::string(what) + ":" + why); };
    }

    std::shared_ptr<PageRun> run_of(const PageForm& f)
    {
        auto r = std::make_shared<PageRun>();
        r->form = f;
        r->in = &in;
        return r;
    }

    bool saw(const char* s) const { return std::find(landed.begin(), landed.end(), s) != landed.end(); }
};

void part_checks(int pair)
{
    std::string name = pair == 0 ? "U home to settings" : "U settings to bluetooth";
    PageForm push = part_form(false, pair), own = part_form(true, pair);
    int64_t want3 = pair == 0 ? 1750 : 1500;
    int64_t d = form_ms(push);
    std::string fold_ms = std::to_string(want3 / 3) + (want3 % 3 ? ".3" : "");
    check(form_thirds(push) == want3 && form_thirds(own) == want3 && d == (want3 + 2) / 3 &&
              form_duration(push) == d + eng::kTailMs && form_duration(own) == form_duration(push) &&
              form_duration(push) <= eng::kFoldMaxMs,
          (name + ": the fold is the last leg's end, " + fold_ms + " ms, plus the tail, the same for its back, "
                  "under kFoldMaxMs")
              .c_str());
    check(page_rest(push, 0, false, false) == -1 && page_rest(push, d / 2, false, false) == 0 &&
              page_rest(push, d - 1, false, true) == 0 && page_rest(push, d, false, true) == 1 &&
              page_rest(own, d + eng::kTailMs, false, false) == -1 && page_rest(own, 0, false, true) == 1,
          (name + ": the rest marks run on U's own D: -1 at each start, 1 once settled at each end").c_str());
    check(page_rest(push, d / 2, true, false) == 0 && page_rest(push, 0, true, false) == 0 &&
              page_rest(push, 0, true, true) == 1 && page_rest(push, d, true, false) == 0 &&
              page_rest(push, d, true, true) == 0 && page_rest(own, d + eng::kTailMs, true, false) == 0 &&
              page_rest(own, 0, true, true) == 1,
          (name + ": on a reversal the rest marks are 0 mid fold and at D, 1 once settled back at s 0, never -1")
              .c_str());

    std::vector<int> bands = part_bands(push);
    bool band_ok = bands == std::vector<int>(std::begin(kSpecBands), std::end(kSpecBands));
    for (const PageRow& e : push.under_els)
        band_ok = band_ok && part_band(push, e) == spec_band(e);
    check(band_ok, (name + ": the bands are the settings tops 0, 196, 282 and 368, each outgoing element takes the "
                           "band its top falls in, the pressed one the band after")
                       .c_str());

    size_t n_over = push.over_els.size(), n = n_over + push.under_els.size();
    eng::Frame f0 = page_frame(push, 0), f_end = page_frame(push, d);
    check(tiles(push.under_els) && tiles(push.over_els) && at_rest(f0, 0) && !at_rest(f0, 1) && at_rest(f_end, 1) &&
              hidden(f_end, 0),
          (name + ": each page's elements tile the screen; s 0 is the outgoing page at rest over the incoming, D "
                  "the incoming at rest alone")
              .c_str());
    check(same_blocks(f0, page_frame(push, d / 2)) && same_blocks(f0, f_end),
          (name + ": the block list and its ids are the same at s 0, mid push and at D").c_str());
    bool shades = f0.shades.size() == f0.blocks.size();
    for (size_t i = 0; shades && i < f0.blocks.size(); i++) {
        const eng::Shade& sh = f0.shades[i];
        int step = f0.blocks[i].ink == eng::Ink::Art ? eng::kArtStep : eng::kTextStep;
        shades = sh.t == eng::kFull && sh.end == eng::kFull && sh.step == step && !sh.dep;
    }
    check(shades, (name + ": art blocks glow at the art rate, text blocks at the text rate, none departs").c_str());

    bool z = f0.blocks.size() == n;
    auto pressed_at = [&](size_t i) { return i >= n_over && push.under_els[i - n_over].pressed; };
    for (size_t i = 0; z && i < n; i++) {
        const PageRow& e = i < n_over ? push.over_els[i] : push.under_els[i - n_over];
        z = same_rect(f0.blocks[i].src, e.r) && f0.blocks[i].source == (i < n_over ? 1 : 0) &&
            f0.blocks[i].ink == e.ink;
        for (size_t k = 0; z && k < n; k++) {
            const eng::Block& p = f0.blocks[i];
            const eng::Block& q = f0.blocks[k];
            if (i == k)
                continue;
            if (pressed_at(k))
                z = p.z < q.z;
            else if (!pressed_at(i) && (p.source > q.source || (p.source == q.source && p.src.y0 < q.src.y0)))
                z = p.z < q.z;
        }
    }
    check(z, (name + ": one block per element at its rect; z: the incoming beneath, rows bottom over top, the "
                     "pressed element on top")
                 .c_str());

    bool parts = true;
    std::vector<int> moved(push.under_els.size(), 0);
    for (int64_t t = 0; t <= d + eng::kTailMs; t++) {
        eng::Frame fr = page_frame(push, t);
        for (size_t i = 0; i < push.under_els.size(); i++) {
            Leave v = leave_of(push.under_els[i]);
            Off o = off_of(fr, n_over + i);
            int m = o.dx * v.sign;
            parts = parts && o.dy == 0 && m >= moved[i] && m <= v.dist;
            moved[i] = m;
        }
    }
    for (size_t i = 0; i < push.under_els.size(); i++) {
        Leave v = leave_of(push.under_els[i]);
        parts = parts && off_of(page_frame(push, d), n_over + i).dx == v.sign * v.dist;
        for (int j = 0; j <= 3; j++) {
            int64_t t = (250 * (v.band + j) + 2) / 3;
            parts = parts && off_of(page_frame(push, t), n_over + i).dx == v.sign * thirds_of(v.dist, j) &&
                    off_of(page_frame(push, 250 * (v.band + j) / 3), n_over + i).dx * v.sign <=
                        thirds_of(v.dist, j);
        }
    }
    check(parts, (name + ": each outgoing element leaves in three steps of a third of its own distance, one step after "
                         "the band above, a home left pane element through x 0, every other through x 960")
                     .c_str());

    bool settles = true;
    std::vector<Off> was(n_over);
    for (int64_t t = 0; t <= d; t++) {
        eng::Frame fr = page_frame(push, t);
        for (size_t i = 0; i < n_over; i++) {
            int sy = spec_settle_dy(push.over_els[i].r.y0);
            Off o = off_of(fr, i);
            bool ok = t == 0 ? o.dx == 41 && o.dy == sy
                             : o.dx <= was[i].dx && o.dy <= was[i].dy && o.dx >= 0 && o.dy >= 0;
            if (t >= 250)
                ok = ok && o.dx == 0 && o.dy == 0;
            settles = settles && ok;
            was[i] = o;
        }
    }
    for (size_t i = 0; i < n_over; i++) {
        int sy = spec_settle_dy(push.over_els[i].r.y0);
        for (int j = 0; j <= 3; j++) {
            Off o = off_of(page_frame(push, (250 * j + 2) / 3), i);
            settles = settles && o.dx == 41 - thirds_of(41, j) && o.dy == sy - thirds_of(sy, j);
        }
    }
    check(settles, (name + ": the incoming header settles from x +41, each row from x +41 and y + 0.21 (649 - its "
                           "top), all on one clock from s 0 over the step's leg")
                       .c_str());

    int64_t delta = 3 * d - want3;
    bool flat = true, back_parts = true, back_ends = true;
    std::vector<int> left(push.under_els.size(), 1 << 20);
    for (int64_t s = 0; s <= d + eng::kTailMs; s++) {
        eng::Frame fr = page_frame(own, d + eng::kTailMs - s);
        flat = flat && at_rest(fr, 1) && same_blocks(fr, f0);
        if (s == 0)
            back_ends = back_ends && hidden(fr, 0);
        if (s >= d)
            back_ends = back_ends && at_rest(fr, 0);
        for (size_t i = 0; i < push.under_els.size(); i++) {
            Leave v = leave_of(push.under_els[i]);
            Off o = off_of(fr, n_over + i);
            int m = o.dx * v.sign;
            back_parts = back_parts && o.dy == 0 && m <= left[i] && m >= 0;
            left[i] = m;
        }
    }
    for (size_t i = 0; i < push.under_els.size(); i++) {
        Leave v = leave_of(push.under_els[i]);
        for (int j = 0; j <= 3; j++) {
            int64_t s = (delta + 250 * (v.band + j) + 2) / 3;
            back_parts = back_parts && off_of(page_frame(own, d + eng::kTailMs - s), n_over + i).dx ==
                                           v.sign * (v.dist - thirds_of(v.dist, j));
        }
    }
    check(flat && back_ends && back_parts,
          (name + ": U's own back: the page left rests flat beneath, the returning elements come back over it top "
                  "down by bands from x 0 or x 960 on the push's staggers, the pressed one a band after its own, "
                  "from the open page to the page under it at rest")
              .c_str());

    Drive dv;
    auto pr = dv.run_of(push);
    dv.ctl.start(FoldKind::Push, page_fold(pr), 0);
    bool start = false, end = false, owned = true;
    for (int k = 0;; k++) {
        int64_t t = frame_ms(k);
        dv.ctl.frame(t, dv.out.surface());
        if (k == 0)
            start = equal(dv.out.surface(), dv.a.surface());
        if (dv.ctl.active())
            owned = owned && owned_ok(dv.out.surface(), dv.in, pr->frame, dv.a.surface(), dv.b.surface());
        if (!dv.ctl.active() || t > 2000)
            break;
        end = equal(dv.out.surface(), dv.b.surface());
    }
    check(start && end && owned && dv.saw("push:end"),
          (name + ": the push on the engine: frame 0 is the outgoing page, every pixel its element's content at its "
                  "pose, glowing from ground or at full, or ground where no element lies, the last frame the incoming "
                  "page")
              .c_str());

    Drive pv;
    pv.dest = &pv.a;
    pv.shown = &pv.b;
    auto qr = pv.run_of(own);
    pv.ctl.start(FoldKind::Pop, page_fold(qr), 0);
    bool pop_start = false, pop_end = false, pop_gap = true, pop_owned = true;
    for (int k = 0;; k++) {
        int64_t t = frame_ms(k);
        pv.ctl.frame(t, pv.out.surface());
        if (k == 0)
            pop_start = equal(pv.out.surface(), pv.b.surface());
        if (pv.ctl.active()) {
            pop_gap = pop_gap && covered(pv.in);
            pop_owned = pop_owned && owned_ok(pv.out.surface(), pv.in, qr->frame, pv.a.surface(), pv.b.surface());
        }
        if (!pv.ctl.active() || t > 2000)
            break;
        pop_end = equal(pv.out.surface(), pv.a.surface());
    }
    check(pop_start && pop_end && pop_gap && pop_owned && pv.saw("pop:end"),
          (name + ": U's own back on the engine, every frame covered: from the open page to the page under it at "
                  "rest, every pixel its element's content at its pose, or ground where no element lies")
              .c_str());

    Drive rv;
    auto rr = rv.run_of(part_form(false, pair));
    rv.ctl.start(FoldKind::Push, page_fold(rr), 0);
    for (int k = 0; k <= 5; k++)
        rv.ctl.frame(frame_ms(k), rv.out.surface());
    int64_t tb = frame_ms(6);
    rv.dest = &rv.a;
    rv.ctl.start(FoldKind::Pop, page_fold(rv.run_of(own)), tb);
    bool rev = rv.ctl.reversed() && rv.ctl.run_length() == tb + eng::kTailMs, track = true, back_end = false;
    for (int64_t t = tb;; t += 16) {
        rv.ctl.frame(t, rv.out.surface());
        if (rv.ctl.active())
            track = track && same_poses(rr->frame, page_frame(push, rv.ctl.forward_time(t)));
        if (!rv.ctl.active() || t > 2000)
            break;
        back_end = equal(rv.out.surface(), rv.a.surface());
    }
    check(rev && track && back_end && rv.saw("pop:end"),
          (name + ": Back mid push is the push reversed (rule 2), every frame the push's poses at e - s, ending on "
                  "the outgoing page at rest")
              .c_str());
    FoldPlay fp = page_fold(pr);
    check(fp.clamp_back && fp.tail == eng::kTailMs && fp.duration == form_duration(push),
          (name + ": the page fold clamps a Back in its tail to D and runs D plus the tail").c_str());

    Drive tl;
    tl.ctl.start(FoldKind::Push, page_fold(tl.run_of(push)), 0);
    int64_t in_tail = d + 100;
    for (int64_t t = 0; t < in_tail; t += 16)
        tl.ctl.frame(t, tl.out.surface());
    tl.ctl.frame(in_tail, tl.out.surface());
    tl.dest = &tl.a;
    tl.ctl.start(FoldKind::Pop, page_fold(tl.run_of(own)), in_tail);
    int64_t next = in_tail + 16;
    bool clamped = tl.ctl.active() && tl.ctl.reversed() && tl.ctl.run_length() == d + eng::kTailMs &&
                   tl.ctl.forward_time(next) < d &&
                   !same_poses(page_frame(push, tl.ctl.forward_time(next)), page_frame(push, d));
    check(clamped, (name + ": Back 100 ms into the tail reverses on the next frame").c_str());

    Drive cv;
    cv.ctl.start(FoldKind::Push, page_fold(cv.run_of(push)), 0);
    cv.ctl.frame(0, cv.out.surface());
    cv.ctl.frame(frame_ms(3), cv.out.surface());
    cv.ctl.input(frame_ms(4), []() {
        InputResult r;
        r.changed = true;
        return r;
    });
    cv.ctl.frame(frame_ms(4), cv.out.surface());
    check(cv.saw("push:input") && !cv.ctl.active() && equal(cv.out.surface(), cv.b.surface()),
          (name + ": a key during the push lands it, the next frame is the new page at rest").c_str());

    bool kept = true;
    int judged = 0;
    for (bool back : {false, true}) {
        Drive kv;
        if (back) {
            kv.dest = &kv.a;
            kv.shown = &kv.b;
        }
        auto kr = kv.run_of(back ? own : push);
        kv.ctl.start(back ? FoldKind::Pop : FoldKind::Push, page_fold(kr), 0);
        size_t px = (size_t)kPortraitW * kPortraitH;
        std::vector<int> since[2] = {std::vector<int>(px, -1), std::vector<int>(px, -1)};
        std::vector<int> seen[2] = {std::vector<int>(px, -1), std::vector<int>(px, -1)};
        Surface out = kv.out.surface(), src[2] = {kv.a.surface(), kv.b.surface()};
        for (int k = 0;; k++) {
            int64_t t = frame_ms(k);
            kv.ctl.frame(t, out);
            if (!kv.ctl.active() || t > 2000)
                break;
            const eng::Frame& fr = kr->frame;
            std::vector<Off> at(fr.blocks.size());
            for (size_t i = 0; i < fr.blocks.size(); i++)
                at[i] = off_of(fr, i);
            for (std::vector<int>& v : seen)
                std::fill(v.begin(), v.end(), -1);
            const std::vector<uint16_t>& ids = kv.in.ids();
            for (int y = 0; y < kPortraitH; y++)
                for (int x = 0; x < kPortraitW; x++) {
                    size_t id = ids[(size_t)y * kPortraitW + (size_t)x] & eng::kIdMask;
                    if (id == 0 || id > fr.blocks.size() || fr.blocks[id - 1].source > 1)
                        continue;
                    const eng::Block& b = fr.blocks[id - 1];
                    int sx = x - at[id - 1].dx, sy = y - at[id - 1].dy;
                    size_t s = (size_t)sy * kPortraitW + (size_t)sx;
                    int first = since[b.source][s] >= 0 ? since[b.source][s] : k;
                    seen[b.source][s] = first;
                    if (k - first < (b.ink == eng::Ink::Art ? kArtFrames : kTextFrames))
                        continue;
                    judged++;
                    kept = kept && out.at(x, y) == src[b.source].at(sx, sy);
                }
            since[0].swap(seen[0]);
            since[1].swap(seen[1]);
        }
    }
    check(kept && judged > 0,
          (name + ": on the push and on U's own back an element pixel shown for a rise (text " +
           std::to_string(kTextFrames) + " frames, art " + std::to_string(kArtFrames) +
           ") draws its content at T, moving or still (" + std::to_string(judged) + " px judged)")
              .c_str());
}

}

int main()
{
    for (int pair : {0, 1})
        part_checks(pair);
    {
        int cap = 3;
        auto many = [](int pressed) {
            std::vector<PageRow> rows{{"settings-header", {0, 0, kPortraitW, 140}},
                                      {"settings-pivots", {0, 116, kPortraitW, 172}}};
            for (int r = 0; r < 6; r++)
                rows.push_back({"settings-row" + std::to_string(r), {0, 196 + 86 * r, kPortraitW, 282 + 86 * r},
                                eng::Ink::Text, r == pressed});
            return rows;
        };
        bool capped = true;
        for (int pair : {0, 1})
            for (bool closing : {false, true}) {
                PageForm f = part_form(closing, pair);
                if (pair == 0) {
                    f.over_els = page_elements("settings", "chrome", many(-1));
                } else {
                    f.under_els = page_elements("settings", "chrome", many(5));
                }
                int pressed_band = -1, others = -1;
                for (const PageRow& e : f.under_els)
                    if (e.pressed)
                        pressed_band = part_band(f, e);
                    else
                        others = std::max(others, part_band(f, e));
                int64_t d = form_ms(f);
                eng::Frame end = page_frame(f, closing ? 0 : d);
                capped = capped && part_bands(f).size() == (pair == 0 ? 8u : 7u) && others == cap &&
                         pressed_band == cap + 1 && form_duration(f) == 734 && form_duration(f) <= eng::kFoldMaxMs &&
                         (closing ? at_rest(end, 0) && at_rest(end, 1) : at_rest(end, 1) && hidden(end, 0));
            }
        check(capped, (std::string("U band cap: with six settings rows, past the cap of ") + std::to_string(cap) +
                       ", every element leaves by the cap band, the pressed one last a band later, and the push and "
                       "U's own back fold in 734 ms, inside kFoldMaxMs, ending at rest")
                          .c_str());
    }
    {
        std::vector<PageRow> h = home_elements(kPaneSplit, true, 5), full = home_elements(kPaneSplit, false, -1);
        bool cut = h.size() == 13 && tiles(h) && tiles(full) && full.size() == 12 && full[0].r.y0 == 0 &&
                   h[0].name == "home-head" && same_rect(h[0].r, {0, 0, 436, 76}) && h[0].ink == eng::Ink::Text &&
                   h[1].name == "home-grid0" && h[5].name == "home-menu-top" && h[6].name == "home-menu0" &&
                   h[12].name == "home-menu-mark";
        for (int k = 0; cut && k < 4; k++)
            cut = h[(size_t)k + 1].ink == eng::Ink::Art &&
                  same_rect(h[(size_t)k + 1].r, {0, 76 + 158 * k, 436, k == 3 ? 720 : 234 + 158 * k});
        cut = cut && same_rect(h[5].r, {436, 0, 960, 37});
        for (int k = 0; cut && k < 6; k++)
            cut = h[(size_t)k + 6].ink == eng::Ink::Text && h[(size_t)k + 6].pressed == (k == 5) &&
                  same_rect(h[(size_t)k + 6].r, {436, 37 + 96 * k, 960, 133 + 96 * k});
        cut = cut && same_rect(h[12].r, {436, 613, 960, 720}) && !h[12].pressed;
        check(cut, "U home elements, named by the home row each comes from: the head 0-436 by 0-76, tile rows from "
                   "76, 234, 392 and 550 (the last to 720), the strip above the menu to 37, menu items 37 + 96 k with "
                   "only the pressed one marked, the watermark from 613, tiling the screen");
        std::vector<PageRow> st = page_elements("settings", "chrome", spec_settings(1));
        std::vector<PageRow> shuffled = spec_settings(1);
        std::reverse(shuffled.begin(), shuffled.end());
        std::vector<PageRow> bt = page_elements("bluetooth", "header", spec_bluetooth());
        std::vector<PageRow> none = page_elements("bluetooth", "header", {});
        bool pages = st.size() == 4 && st[0].name == "settings-header+settings-pivots" &&
                     same_rect(st[0].r, {0, 0, 960, 196}) &&
                     st[1].name == "settings-row0" && same_rect(st[1].r, {0, 196, 960, 282}) && !st[1].pressed &&
                     st[2].name == "settings-row1" && same_rect(st[2].r, {0, 282, 960, 368}) && st[2].pressed &&
                     st[3].name == "settings-ground" && same_rect(st[3].r, {0, 368, 960, 720}) && !st[0].pressed &&
                     bt.size() == 7 && bt[0].name == "bluetooth-header" && same_rect(bt[0].r, {0, 0, 960, 196}) &&
                     same_rect(bt[5].r, {0, 516, 960, 596}) && bt[6].name == "bluetooth-ground" &&
                     same_rect(bt[6].r, {0, 596, 960, 720}) && none.size() == 1 && none[0].name == "bluetooth-header" &&
                     same_rect(none[0].r, kScreen) && tiles(st) && tiles(bt);
        std::vector<PageRow> again = page_elements("settings", "chrome", shuffled);
        for (size_t i = 0; pages && i < st.size(); i++)
            pages = again.size() == st.size() && again[i].name == st[i].name && same_rect(again[i].r, st[i].r);
        std::vector<PageRow> low = page_elements("settings", "chrome", {{"settings-row0", {0, 98, 960, 184}}});
        pages = pages && low.size() == 3 && low[0].name == "settings-chrome" && same_rect(low[0].r, {0, 0, 960, 98}) &&
                low[1].name == "settings-row0" && low[2].name == "settings-ground";
        check(pages, "U page elements: the settings header and pivot rows overlap, so they are one chrome block to the "
                     "first row named by both rows, each row its own block under its row's name, a ground block below "
                     "the last row, the head's own name where no row starts at 0; any row order");
    }
    {
        auto noise = [](Buffer& b, uint32_t seed) {
            Surface s = b.surface();
            for (int y = 0; y < s.h; y++)
                for (int x = 0; x < s.w; x++)
                    s.at(x, y) = ((uint32_t)x * 2654435761u ^ (uint32_t)y * 40503u ^ seed) & 0xFFFFFFu;
        };
        auto planes = [](const eng::Instance& p, const eng::Instance& q) {
            return p.e() == q.e() && p.ids() == q.ids() && p.age() == q.age() && p.cover() == q.cover() &&
                   p.settled() == q.settled() && p.inited() == q.inited();
        };
        Buffer sa, sb;
        paint(sa, 0x103050);
        paint(sb, 0xC06010);
        std::vector<Surface> src{sa.surface(), sb.surface()};
        bool same = true;
        for (bool closing : {false, true})
            for (int64_t lead : {1, 8, 16, -8}) {
                PageForm pf = part_form(closing, 0);
                int64_t dur = form_duration(pf), ahead = lead < 0 ? -lead : lead;
                uint32_t g = lead < 0 ? 0xEEEEEEu : ground();
                auto ft_of = [&](int64_t s) { return closing ? std::max<int64_t>(0, dur - s) : std::min(s, dur); };
                eng::Instance one(kScreen), two(kScreen), dirty(kScreen);
                Buffer d1, d2, d3, scratch;
                noise(d1, 1);
                noise(d2, 1);
                noise(d3, 2);
                noise(scratch, 3);
                eng::Frame f0 = page_frame(pf, ft_of(0));
                const int64_t t0 = 1000;
                one.begin(t0, false);
                two.begin(t0, false);
                dirty.begin(t0, false);
                two.advance(t0, t0);
                two.compose(scratch.surface(), f0, src, g, kPortraitW);
                bool ok = true;
                for (int n = 0; n < 8 && ok; n++) {
                    int64_t s = ahead + frame_ms(n);
                    eng::Frame f = page_frame(pf, ft_of(s));
                    one.advance(t0, t0 + s);
                    two.advance(t0, t0 + s);
                    dirty.advance(t0, t0 + s);
                    one.compose(d1.surface(), f, src, g, kPortraitW, one.inited() ? nullptr : &f0);
                    two.compose(d2.surface(), f, src, g, kPortraitW);
                    dirty.compose(d3.surface(), f, src, g, kPortraitW, dirty.inited() ? nullptr : &f0);
                    ok = d1.pix == d2.pix && planes(one, two) && planes(one, dirty) &&
                         equal(d1.surface(), d3.surface());
                }
                if (!ok)
                    std::printf("  %s lead %lld%s differs\n", closing ? "pop" : "push", (long long)ahead,
                                lead < 0 ? " light" : "");
                same = same && ok;
            }
        check(same, "page: a U push or its own back whose input leads its first frame by 1, 8 or 16 ms (8 in light) "
                    "draws and keeps the same state as its start frame composed alone at t0 into another target, and "
                    "the same state over a different target");
    }

    {
        auto head = [](const char* page, int letters, int tabs, bool layer) {
            PageHead h;
            h.word = page;
            h.layer = layer;
            for (int k = 0; k < letters; k++)
                h.letters.push_back({std::string(page) + "-header-l" + std::to_string(k),
                                     {k * kPortraitW / letters, 0, (k + 1) * kPortraitW / letters, 140}});
            for (int j = 0; j < tabs; j++)
                h.tabs.push_back({std::string(page) + "-pivots-t" + std::to_string(j),
                                  {j * kPortraitW / tabs, 116, (j + 1) * kPortraitW / tabs, 172}});
            return h;
        };
        struct Set {
            int n = 0, z = -1, dx = 0, dy = 0;
            bool clip = true;
        };
        auto set_of = [](const eng::Frame& fr, int source, int y0, Rect clip, int k = 0) {
            Set out;
            for (const eng::Pose& q : fr.poses) {
                const eng::Block& b = fr.blocks[(size_t)q.block];
                if (b.source != source || b.src.y0 != y0)
                    continue;
                if (out.n++ == k) {
                    out.dx = q.rect.x0 - b.src.x0;
                    out.dy = q.rect.y0 - b.src.y0;
                }
                out.z = b.z;
                out.clip = out.clip && q.clip.x0 == clip.x0 && q.clip.y0 == clip.y0 && q.clip.x1 == clip.x1 &&
                           q.clip.y1 == clip.y1;
            }
            return out;
        };
        auto zs = [](const eng::Frame& fr, int source, bool hi, int y0 = 0) {
            int z = hi ? -1 : 1 << 30;
            for (const eng::Block& b : fr.blocks)
                if (b.source == source && b.src.y0 >= y0)
                    z = hi ? std::max(z, b.z) : std::min(z, b.z);
            return z;
        };
        auto chrome = [](const eng::Frame& fr, int source) {
            for (const eng::Block& b : fr.blocks)
                if (b.source == source && b.src.y0 == 0)
                    return true;
            return false;
        };
        Rect band{0, 0, kPortraitW, 140}, strip{0, 116, kPortraitW, 172};
        PageForm push = part_form(false, 0), plain = push;
        push.over_head = head("settings", 8, 5, true);
        push.over_head.rank = tab_ranks(push, push.over_head);
        const std::vector<int> w{3, 1, 0, 2, 4};
        eng::Frame f0 = page_frame(push, 0), f27 = page_frame(push, 27), fe = page_frame(push, form_ms(push));
        Set l0 = set_of(f0, 3, 0, band), t0 = set_of(f0, 1, 116, strip), le = set_of(fe, 3, 0, band),
            te = set_of(fe, 1, 116, strip), a27 = set_of(f27, 3, 0, band, 0), b27 = set_of(f27, 3, 0, band, 1);
        Set r0 = set_of(f27, 1, 116, strip, 2), r4 = set_of(f27, 1, 116, strip, 4);
        check(r0.dy > -41 && r4.dy == -41,
              "the tabs start by rank: at 27 ms on home to settings t2 (rank 0) moves while t4 (rank 4) waits");
        bool stagger = true;
        for (int k = 0; k <= 30; k++)
            stagger = stagger && std::abs(3 * head_start3(k) - (int64_t)k * eng::kStepThirds) <= 1;
        PageForm many = part_form(false, 0), mplain = many;
        many.over_head = head("settings", 25, 0, true);
        many.over_head.rank = tab_ranks(many, many.over_head);
        eng::Frame fm = page_frame(many, (head_start3(20) + eng::kStepThirds / 2) / 3);
        Set m20 = set_of(fm, 3, 0, band, 20), m24 = set_of(fm, 3, 0, band, 24);
        check(stagger && m20.dy == m24.dy && m20.dy > -41 &&
                  form_thirds(many) == std::max(form_thirds(mplain), head_start3(20) + eng::kStepThirds) &&
                  form_ms(many) + eng::kTailMs <= eng::kFoldMaxMs,
              "the letter stagger is k x 250 / 3 thirds to within a third, items past index 20 start with item 20, "
              "and a 25 letter header lands inside 800 ms");
        bool beneath = l0.z > zs(f0, 1, true, 172) && t0.z == l0.z + 1 && t0.z < zs(f0, 0, false);
        check(l0.n == 8 && t0.n == 5 && l0.clip && t0.clip && l0.dx == 0 && l0.dy == -41 && t0.dy == -41 &&
                  le.dy == 0 && te.dy == 0 && a27.dy > -41 && b27.dy == -41 && l0.dx == 0 && t0.dx == 0 &&
                  !chrome(f0, 1) && beneath && push.over_head.rank == w &&
                  form_thirds(push) == form_thirds(plain),
              "H and P on home to settings: 8 letters from the word layer (source 3) and 5 tabs from the rest, "
              "clipped to the band and the strip, beneath the outgoing with the tabs over the letters, settle from y "
              "-41 with no x move in one step from letter 0, 27.8 ms apart, at rest by the fold end, the "
              "chrome block replaced, the tabs ranked t2, t1, t3, t0, t4 as home uncovers them, the fold unchanged");
        std::vector<std::string> names = page_block_names(push);
        bool named = names.size() == f0.blocks.size();
        int nl = 0, nt = 0;
        for (size_t i = 0; named && i < names.size(); i++) {
            const eng::Block& b = f0.blocks[i];
            if (b.source == 3)
                named = names[i] == "settings-header-l" + std::to_string(nl++);
            else if (b.source == 1 && b.src.y0 == 116)
                named = names[i] == "settings-pivots-t" + std::to_string(nt++);
            else
                named = !names[i].empty();
        }
        named = named && nl == 8 && nt == 5;
        check(named, "the pose log names each block by its element, the letters settings-header-l<k> and the tabs "
                     "settings-pivots-t<j>");
        PageForm back = part_form(true, 1), bplain = back;
        back.under_head = head("settings", 8, 3, true);
        back.under_head.rank = tab_ranks(back, back.under_head);
        eng::Frame g0 = page_frame(back, form_duration(back)), ge = page_frame(back, 0);
        Set bl = set_of(g0, 2, 0, band), bt = set_of(g0, 0, 116, strip), ble = set_of(ge, 2, 0, band),
            bte = set_of(ge, 0, 116, strip);
        check(bl.n == 8 && bt.n == 3 && bl.dy == -140 && bt.dy == -56 && ble.dy == 0 && bte.dy == 0 &&
                  bl.z > zs(g0, 0, true, 172) && bl.z > zs(g0, 1, true) && bt.z == bl.z + 1 && !chrome(g0, 0) &&
                  back.under_head.rank == std::vector<int>{0, 1, 2} && form_thirds(back) == form_thirds(bplain),
              "H and P on the Bluetooth back: the settings letters from y -140 and the tabs from y 60 over every "
              "block, left to right as the page left rests, at rest by the fold end, the fold unchanged");
        PageForm bpush = part_form(false, 1), bpplain = bpush;
        bpush.over_head = head("bluetooth", 9, 0, false);
        Set bb = set_of(page_frame(bpush, 0), 1, 0, band);
        check(bb.n == 9 && bb.dy == -41 && form_thirds(bpush) == form_thirds(bpplain),
              "H on settings to Bluetooth: 9 letters from the Bluetooth rest (source 1), settling from y -41, the "
              "fold unchanged");
        {
            Drive dv;
            Buffer word(kPortraitW, 140);
            paint(word, 0x205070);
            PageForm bf = back;
            std::shared_ptr<PageRun> r = dv.run_of(bf);
            r->words[0] = word.surface();
            int64_t tb = 1000;
            dv.ctl.start(FoldKind::Pop, page_fold(r), tb);
            int prev = -140, enter = 0;
            bool rise = true;
            for (int64_t t = tb; t <= tb + 32 && dv.ctl.active(); t += 16) {
                dv.ctl.frame(t, dv.out.surface());
                int dy = set_of(page_frame(bf, dv.ctl.forward_time(t)), 2, 0, band).dy;
                if (t == tb + 32) {
                    enter = dy - prev;
                    for (int y = 0; y < enter; y++)
                        for (int x = 0; x < kPortraitW / 8; x++) {
                            size_t i = (size_t)y * kPortraitW + (size_t)x;
                            rise = rise && dv.in.e()[i] == one_step(eng::kTextStep, 48) && (dv.in.age()[i] & 0x80);
                        }
                }
                prev = dy;
            }
            check(rise && enter > 5,
                  ("H on the back: the rows a letter brings in through y 0 rise from ground at one text step, "
                   "whichever block's id those pixels held the frame before, the letter's own included (" +
                   std::to_string(enter) + " rows)")
                      .c_str());
        }
    }

    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
