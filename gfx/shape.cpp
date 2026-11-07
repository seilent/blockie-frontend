#include "shape.h"

#include <algorithm>
#include <climits>
#include <iterator>
#include <hb.h>

#include "stb_truetype.h"

#include "../core/log.h"
#include "../core/prof.h"
#include "../lib/util.h"
#include "fontkey.h"

namespace fd {

using namespace fontkey;

namespace {

int round64(int v) { return ((v + 32) >> 6) * 64; }

int rpx64(int fu, int px, int upem)
{
    int64_t n = (int64_t)fu * px * 2 + upem, d = (int64_t)upem * 2;
    return (int)((n >= 0 ? n / d : -((-n + d - 1) / d)) * 64);
}

int floor_px(int v64) { return v64 >= 0 ? v64 >> 6 : -((-v64 + 63) >> 6); }

int ceil_px(int v64) { return v64 >= 0 ? (v64 + 63) >> 6 : -((-v64) >> 6); }

size_t first_at(const TextLayout& l, size_t byte)
{
    return (size_t)(std::lower_bound(l.g.begin(), l.g.end(), byte,
                                     [](const TextLayout::G& g, size_t b) { return g.byte < b; }) -
                    l.g.begin());
}

std::vector<int> visual_order(const TextLayout& l, size_t b, size_t e)
{
    std::vector<int> order;
    for (const BidiRun& br : bidi_runs(l.para, b, e)) {
        size_t lo = first_at(l, br.b), hi = first_at(l, br.e);
        if (br.level & 1)
            for (size_t k = hi; k > lo; k--)
                order.push_back((int)k - 1);
        else
            for (size_t k = lo; k < hi; k++)
                order.push_back((int)k);
    }
    return order;
}

}

size_t TextLayout::bytes() const
{
    return sizeof(TextLayout) + text.capacity() + para.cp.capacity() * 4 + para.at.capacity() * 4 +
           para.type.capacity() * 4 + para.level.capacity() + g.capacity() * sizeof(G);
}

std::vector<RangedFace> rtl_faces(const std::string& dir)
{
    return {
        {dir + "/NotoSansArabic-Light.ttf",
         {{0x0600, 0x06FF}, {0x0750, 0x077F}, {0x0870, 0x08FF}, {0xFB50, 0xFDFF}, {0xFE70, 0xFEFF}, {0x10EFD, 0x10EFF}},
         (uint32_t)HB_SCRIPT_ARABIC,
         true},
        {dir + "/NotoSansHebrew-Light.ttf", {{0x0590, 0x05FF}, {0xFB1D, 0xFB4F}}, (uint32_t)HB_SCRIPT_HEBREW, false},
    };
}

hb_font_t* Fonts::hb(Face& f)
{
    if (!f.hfont) {
        f.blob = hb_blob_create((const char*)f.map, (unsigned)f.len, HB_MEMORY_MODE_READONLY, nullptr, nullptr);
        f.hface = hb_face_create(f.blob, 0);
        f.hfont = hb_font_create(f.hface);
        hb_made_++;
    }
    if (!hb_buf_) {
        hb_buf_ = hb_buffer_create();
        if (!hb_buffer_allocation_successful(hb_buf_)) {
            hb_buffer_destroy(hb_buf_);
            hb_buf_ = nullptr;
        }
    }
    return f.hfont;
}

void Fonts::drop_hb()
{
    for (Face& f : fall_) {
        if (f.hfont)
            hb_font_destroy(f.hfont);
        if (f.hface)
            hb_face_destroy(f.hface);
        if (f.blob)
            hb_blob_destroy(f.blob);
        f.hfont = nullptr;
        f.hface = nullptr;
        f.blob = nullptr;
    }
    if (hb_buf_)
        hb_buffer_destroy(hb_buf_);
    hb_buf_ = nullptr;
}

std::shared_ptr<const TextLayout> Fonts::lay(const TextStyle& st, std::string_view s, Dir d, bool ready)
{
    layouts_++;
    auto l = std::make_shared<TextLayout>();
    l->text.assign(s);
    for (char& c : l->text)
        c = (char)apply_case((unsigned char)c, st.tcase);
    l->para = bidi_para(l->text, d);
    l->para.cp.shrink_to_fit();
    l->para.at.shrink_to_fit();
    const BidiPara& p = l->para;
    size_t n = p.cp.size();
    auto pick = [&](size_t i, int& fi, int& gid) {
        bool ok = ready ? resolve_ready(st.weight, p.cp[i], fi, gid, true) : resolve(st.weight, p.cp[i], fi, gid, true);
        if (!ok && ready)
            l->complete = false;
        return ok || !ready;
    };
    size_t i = 0;
    while (i < n) {
        int fi, gid;
        if (bidi_ignorable(p.cp[i]) || !pick(i, fi, gid) || fi < 0) {
            i++;
            continue;
        }
        Face& f = face(fi);
        if (!f.ranged) {
            int adv, lsb;
            stbtt_GetGlyphHMetrics(f.info, gid, &adv, &lsb);
            l->g.push_back({(uint16_t)fi, (uint8_t)p.level[i], (uint32_t)gid, p.at[i], adv, 0, 0});
            i++;
            continue;
        }
        size_t j = i + 1;
        while (j < n) {
            int fj, gj;
            if (!bidi_ignorable(p.cp[j]) &&
                (p.level[j] != p.level[i] || !in_ranges(f, p.cp[j]) || !pick(j, fj, gj) || fj != fi))
                break;
            j++;
        }
        uint8_t lv = (uint8_t)p.level[i];
        size_t b = p.at[i], e = p.at[j];
        hb_font_t* font = hb(f);
        if (!hb_buf_) {
            l->complete = false;
            i = j;
            continue;
        }
        hb_buffer_clear_contents(hb_buf_);
        hb_buffer_set_direction(hb_buf_, lv & 1 ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
        hb_buffer_set_script(hb_buf_, (hb_script_t)f.script);
        unsigned flags = HB_BUFFER_FLAG_REMOVE_DEFAULT_IGNORABLES;
        if (b == 0)
            flags |= HB_BUFFER_FLAG_BOT;
        if (e == l->text.size())
            flags |= HB_BUFFER_FLAG_EOT;
        hb_buffer_set_flags(hb_buf_, (hb_buffer_flags_t)flags);
        hb_buffer_add_utf8(hb_buf_, l->text.data(), (int)l->text.size(), (unsigned)b, (int)(e - b));
        hb_shape(font, hb_buf_, nullptr, 0);
        if (!hb_buffer_allocation_successful(hb_buf_)) {
            l->complete = false;
            i = j;
            continue;
        }
        unsigned gn = 0;
        hb_glyph_info_t* gi = hb_buffer_get_glyph_infos(hb_buf_, &gn);
        hb_glyph_position_t* gp = hb_buffer_get_glyph_positions(hb_buf_, &gn);
        size_t at = l->g.size();
        for (unsigned k = 0; k < gn; k++)
            l->g.push_back({(uint16_t)fi, lv, gi[k].codepoint, gi[k].cluster, gp[k].x_advance, gp[k].x_offset,
                            gp[k].y_offset});
        if (lv & 1)
            std::reverse(l->g.begin() + (long)at, l->g.end());
        i = j;
    }
    l->g.shrink_to_fit();
    return l;
}

std::shared_ptr<const TextLayout> Fonts::find_layout(const TextStyle& st, std::string_view s, Dir d, bool ready, int keep)
{
    if (s.size() > kBidiParaMax)
        s = bidi_pieces(s)[0];
    uint64_t h = hash64(s, (uint64_t)st.weight | (uint64_t)st.tcase << 8 | (uint64_t)d << 16);
    auto hit = lru_map_.find(h);
    if (hit != lru_map_.end())
        for (std::list<Layout>::iterator e : hit->second)
            if (e->text == s && e->weight == st.weight && e->tcase == st.tcase && e->dir == d) {
                if (keep == 1)
                    lru_.splice(lru_.begin(), lru_, e);
                return e->l;
            }
    std::shared_ptr<const TextLayout> l = lay(st, s, d, ready);
    if (keep == 0 || !l->complete)
        return l;
    Layout e{std::string(s), st.weight, st.tcase, d, l->bytes() + s.size(), l};
    std::list<Layout>::iterator at = lru_.insert(keep == 1 ? lru_.begin() : lru_.end(), std::move(e));
    lru_map_[h].push_back(at);
    lru_bytes_ += at->bytes;
    while ((lru_.size() > kLayoutEntries || lru_bytes_ > kLayoutBytes) && lru_.size() > 1) {
        std::list<Layout>::iterator old = std::prev(lru_.end());
        if (old == at)
            old = std::prev(old);
        uint64_t oh = hash64(old->text, (uint64_t)old->weight | (uint64_t)old->tcase << 8 | (uint64_t)old->dir << 16);
        std::vector<std::list<Layout>::iterator>& v = lru_map_[oh];
        v.erase(std::find(v.begin(), v.end(), old));
        if (v.empty())
            lru_map_.erase(oh);
        lru_bytes_ -= old->bytes;
        lru_.erase(old);
    }
    if (lru_bytes_ > lru_high_) {
        lru_high_ = lru_bytes_;
        log_info("text: lru %zu entries, %zu bytes\n", lru_.size(), lru_bytes_);
    }
    return l;
}

std::shared_ptr<const TextLayout> Fonts::layout(const TextStyle& st, std::string_view s, Dir d)
{
    return find_layout(st, s, d, false, 1);
}

int Fonts::place(const TextStyle& st, const TextLayout& l, const std::vector<int>& order,
                 std::vector<std::pair<int, int>>& at, std::vector<std::pair<int, int>>* starts)
{
    at.resize(order.size());
    if (starts)
        starts->resize(order.size());
    if (order.empty())
        return 0;
    auto span = std::minmax_element(order.begin(), order.end());
    size_t lo = (size_t)*span.first, hi = (size_t)*span.second;
    std::vector<uint32_t> unit(hi - lo + 1);
    for (size_t i = lo + 1; i <= hi; i++)
        unit[i - lo] = l.g[i].byte == l.g[i - 1].byte || l.g[i].adv == 0 ? unit[i - 1 - lo] : unit[i - 1 - lo] + 1;
    int pen = 0, track = 0;
    for (size_t k = 0; k < order.size(); k++) {
        const TextLayout::G& g = l.g[(size_t)order[k]];
        Face& f = face(g.face);
        if (k > 0) {
            const TextLayout::G& q = l.g[(size_t)order[k - 1]];
            if (unit[(size_t)order[k] - lo] != unit[(size_t)order[k - 1] - lo] && !(f.joined && q.face == g.face))
                track += st.track64;
            if (!f.ranged && q.face == g.face && !(g.level & 1) && order[k] == order[k - 1] + 1)
                pen += scale64(stbtt_GetGlyphKernAdvance(f.info, (int)q.gid, (int)g.gid), st.px, f.upem);
        }
        int adv = scale64(g.adv, st.px, f.upem), x = pen + track;
        if (starts)
            (*starts)[k] = {pen, track};
        if (f.ranged) {
            pen = round64(pen);
            adv = rpx64(g.adv, st.px, f.upem);
            x = pen + round64(track);
        }
        at[k] = {x, adv};
        pen += adv;
    }
    return at.back().first + at.back().second;
}

int Fonts::span64(const TextStyle& st, const TextLayout& l, size_t b, size_t e)
{
    std::vector<std::pair<int, int>> at;
    return place(st, l, visual_order(l, b, e), at);
}

bool Fonts::prefix64(const TextStyle& st, const TextLayout& l, TextPrefix& p)
{
    p.g.clear();
    if (l.g.empty())
        return false;
    for (int v : l.para.level)
        if (v != (int)l.para.base)
            return false;
    std::vector<int> order = visual_order(l, 0, l.text.size());
    std::vector<std::pair<int, int>> at, starts;
    place(st, l, order, at, &starts);
    p.g.resize(l.g.size());
    for (size_t k = 0; k < order.size(); k++) {
        size_t i = (size_t)order[k];
        p.g[i] = {at[k].first, at[k].first + at[k].second, starts[k].first, starts[k].second, face(l.g[i].face).ranged};
    }
    p.rtl = l.para.base & 1;
    return true;
}

bool TextPrefix::span64(const TextLayout& l, size_t b, size_t e, int& w) const
{
    if (g.empty())
        return false;
    size_t i = first_at(l, b), j = first_at(l, e);
    if (j <= i) {
        w = 0;
        return true;
    }
    const G& s = g[rtl ? j - 1 : i];
    if (s.track % 64 != 0 || (!s.ranged && s.pen % 64 != 0))
        return false;
    w = g[rtl ? i : j - 1].end - s.x;
    return true;
}

GlyphRun Fonts::run(const TextStyle& st, const TextLayout& l, size_t b, size_t e)
{
    GlyphRun r;
    r.st = st;
    r.complete = l.complete;
    std::vector<int> order = visual_order(l, b, e);
    std::vector<std::pair<int, int>> at;
    r.width64 = place(st, l, order, at);
    r.g.reserve(order.size());
    for (size_t k = 0; k < order.size(); k++) {
        const TextLayout::G& g = l.g[(size_t)order[k]];
        Face& f = face(g.face);
        GlyphRun::G o{g.face, g.gid, at[k].first, 0, 0, g.byte};
        if (f.ranged) {
            o.dx64 = rpx64(g.dx, st.px, f.upem);
            o.dy64 = -rpx64(g.dy, st.px, f.upem);
            r.ranged = true;
            if ((size_t)g.gid * 4 + 3 < f.box.size()) {
                const int16_t* bx = &f.box[(size_t)g.gid * 4];
                r.ink_top64 = std::max(r.ink_top64, scale64(bx[3], st.px, f.upem) - o.dy64);
                r.ink_bottom64 = std::max(r.ink_bottom64, o.dy64 - scale64(bx[1], st.px, f.upem));
                r.ink_right64 = std::max(r.ink_right64, o.x64 + o.dx64 + scale64(bx[2], st.px, f.upem) - r.width64);
            }
        }
        int lift = std::max(0, -o.dy64) >> 6, drop = std::max(0, o.dy64) >> 6;
        r.up = std::max(r.up, (f.top * st.px + f.upem - 1) / f.upem + 1 + lift);
        r.down = std::max(r.down, (f.bottom * st.px + f.upem - 1) / f.upem + 1 + drop);
        r.g.push_back(o);
    }
    return r;
}

GlyphRun Fonts::run(const TextStyle& st, std::string_view s, Dir d)
{
    std::shared_ptr<const TextLayout> l = find_layout(st, s, d, false, 1);
    return run(st, *l, 0, l->text.size());
}

bool Fonts::draw(Canvas& c, const GlyphRun& r, int x64, int baseline, uint32_t rgb, int a)
{
    return draw_run(c, r, x64, baseline, rgb, a, nullptr);
}

bool Fonts::draw(Canvas& c, const GlyphRun& r, int x64, int baseline, uint32_t rgb, int a, const Zoom& z)
{
    return draw_run(c, r, x64, baseline, rgb, a, z.on() ? &z : nullptr);
}

bool Fonts::draw_run(Canvas& c, const GlyphRun& r, int x64, int baseline, uint32_t rgb, int a, const Zoom* z)
{
    if (r.is_plain)
        return z ? draw(c, r.st, r.plain.c_str(), x64, baseline, rgb, a, *z)
                 : draw(c, r.st, r.plain.c_str(), x64, baseline, rgb, a);
    prof::Scope ps(prof::Glyph);
    Rect clip = c.clip();
    int px = r.st.px;
    int bucket = z ? press_bucket(px, z->s) : 0;
    int base = z ? (z->y(baseline * 64) + 32) >> 6 : baseline;
    if (r.g.empty() || ((!z || z->s <= kOne) && (base + r.down <= clip.y0 || base - r.up >= clip.y1)))
        return true;
    bool hold = th_.joinable() && budget_ >= 0 && a > 0;
    bool merge = a < 256 && r.ranged;
    bool complete = true, miss = false, subst = false, gap = false;
    int ay = inside(base - px / 2, clip.y0, clip.y1), x0 = INT_MAX, x1 = INT_MIN;
    run_puts_.clear();
    for (const GlyphRun::G& g : r.g) {
        int at64 = x64 + g.x64 + g.dx64;
        int sx = ((z ? z->x(at64) : at64) + 32) >> 6;
        Face& f = face(g.face);
        int lo = -px, hi = 2 * px;
        if ((size_t)g.gid * 4 + 3 < f.box.size()) {
            lo = floor_px(scale64(f.box[(size_t)g.gid * 4], px, f.upem)) - 1;
            hi = ceil_px(scale64(f.box[(size_t)g.gid * 4 + 2], px, f.upem)) + 1;
        }
        if (sx + hi <= clip.x0 || sx + lo >= clip.x1)
            continue;
        x0 = std::min(x0, sx);
        x1 = std::max(x1, sx + hi);
        if (a <= 0)
            continue;
        bool pending = false;
        int ax = inside(sx, clip.x0, clip.x1);
        uint64_t got = key_of(0, g.face, px, (int)g.gid);
        Glyph* gl = bucket ? scaled(g.face, px, (int)g.gid, bucket, ax, ay, pending, miss, subst, got)
                           : glyph(g.face, px, (int)g.gid, ax, ay, pending, miss);
        if (pending) {
            complete = false;
            gap = gap || !gl;
        }
        if (!gl || gl->w <= 0)
            continue;
        int gy = (z ? (z->y(baseline * 64 + g.dy64) + 32) >> 6 : baseline + (g.dy64 >> 6)) + gl->yoff;
        if (hold || merge)
            run_puts_.push_back({got, sx + gl->xoff, gy});
        else
            c.coverage(gl->rot.data(), gl->w, gl->h, sx + gl->xoff, gy, rgb, a);
    }
    bool show = !hold || (!miss && (z ? !gap : complete));
    if (show && !run_puts_.empty()) {
        auto find_put = [&](const Put& p) -> const Glyph* {
            Cache& cache = key_bucket(p.key) ? scaled_ : plain_;
            auto it = cache.map.find(p.key);
            return it == cache.map.end() || it->second.w <= 0 ? nullptr : &it->second;
        };
        if (merge) {
            Rect u{INT_MAX, INT_MAX, INT_MIN, INT_MIN};
            for (const Put& p : run_puts_)
                if (const Glyph* g = find_put(p))
                    u = {std::min(u.x0, p.x), std::min(u.y0, p.y), std::max(u.x1, p.x + g->w), std::max(u.y1, p.y + g->h)};
            u = u.intersect(clip);
            if (!u.empty()) {
                int mw = u.x1 - u.x0, mh = u.y1 - u.y0;
                mask_.assign((size_t)mw * mh, 0);
                for (const Put& p : run_puts_) {
                    const Glyph* g = find_put(p);
                    if (!g)
                        continue;
                    for (int y = std::max(p.y, u.y0); y < std::min(p.y + g->h, u.y1); y++)
                        for (int x = std::max(p.x, u.x0); x < std::min(p.x + g->w, u.x1); x++) {
                            uint8_t v = g->rot[LiveL::index(g->w, g->h, x - p.x, y - p.y)];
                            uint8_t& m = mask_[LiveL::index(mw, mh, x - u.x0, y - u.y0)];
                            m = std::max(m, v);
                        }
                }
                c.coverage(mask_.data(), mw, mh, u.x0, u.y0, rgb, a);
            }
        } else {
            for (const Put& p : run_puts_)
                if (const Glyph* g = find_put(p))
                    c.coverage(g->rot.data(), g->w, g->h, p.x, p.y, rgb, a);
        }
    }
    if ((miss && need_ == Need::Visible) || (subst && (budget_ < 0 || need_ == Need::Visible)))
        owed_epoch_++;
    if (a > 0 && x0 <= x1)
        note(clip, x0, x1, base, px);
    return complete;
}

bool Fonts::warm(const TextStyle& st, std::string_view s, Dir d, Need n, int bucket)
{
    if (!th_.joinable())
        return false;
    std::shared_ptr<const TextLayout> l = find_layout(st, s, d, true, 2);
    for (const TextLayout::G& g : l->g) {
        uint64_t k = key_of(bucket, g.face, st.px, (int)g.gid);
        if (!(bucket ? scaled_ : plain_).map.count(k))
            ask(k, n, false, false, -1, -1);
    }
    return l->complete;
}

bool Fonts::cached(const TextStyle& st, std::string_view s, Dir d, int bucket)
{
    if (!th_.joinable())
        return true;
    std::shared_ptr<const TextLayout> l = find_layout(st, s, d, true, 1);
    if (!l->complete)
        return false;
    for (const TextLayout::G& g : l->g)
        if (!(bucket ? scaled_ : plain_).map.count(key_of(bucket, g.face, st.px, (int)g.gid)))
            return false;
    return true;
}

}
