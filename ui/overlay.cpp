#include "overlay.h"

#include <algorithm>
#include <climits>

#include "../core/gesture.h"
#include "../gfx/wrap.h"
#include "../lib/util.h"
#include "accent.h"
#include "rows.h"
#include "scrollbar.h"
#include "size.h"

namespace fd {

namespace {

int to64(fx u) { return (to_px(u) + 512) >> 10; }
constexpr int kSlots = tok::jumpCols >> kFxBits;
constexpr int kExitCells = tok::jumpExitCells >> kFxBits;
constexpr int kSlotCount = kSectionCount + kExitCells;
constexpr int kRows = (kSlotCount + kSlots - 1) / kSlots;
constexpr fx kPitch = tok::tileS + tok::jumpGap;
constexpr fx kLeft = (tok::canvasW - kSlots * tok::tileS - (kSlots - 1) * tok::jumpGap) / 2;
constexpr int ms_of(fx t) { return t >> kFxBits; }

int slot_of(int i) { return i == JumpGrid::kExit ? 0 : i == JumpGrid::kSearch ? 1 : i + kExitCells; }

pos64 row_top(int slot) { return (pos64)tok::jumpTop + (pos64)(slot / kSlots) * kPitch; }

int px64(pos64 u) { return (int)((((u * ppu()) >> kFxBits) + kHalf) >> kFxBits); }

const Role& letter_role() { return role::letterGlyph; }

void border(Canvas& c, Rect t, int bw, uint32_t rgb, int a)
{
    c.blend({t.x0, t.y0, t.x1, t.y0 + bw}, rgb, a);
    c.blend({t.x0, t.y1 - bw, t.x1, t.y1}, rgb, a);
    c.blend({t.x0, t.y0 + bw, t.x0 + bw, t.y1 - bw}, rgb, a);
    c.blend({t.x1 - bw, t.y0 + bw, t.x1, t.y1 - bw}, rgb, a);
}

size_t fit_bytes(Fonts& f, const TextStyle& s, const std::string& t, int width64)
{
    const char* p = t.c_str();
    const char* end = p + t.size();
    size_t fit = 0;
    while (p < end) {
        const char* q = p;
        utf8_next(q, end);
        if (f.width64(s, t.substr(0, (size_t)(q - t.c_str())).c_str()) > width64)
            break;
        fit = (size_t)(q - t.c_str());
        p = q;
    }
    return fit;
}

size_t wrap_at(Fonts& f, const TextStyle& s, const std::string& t, int width64)
{
    std::vector<std::string> first = wrap_lines(f, s, t, width64, 1);
    size_t k = first.empty() ? 0 : first[0].size();
    if (k > 0 && k < t.size() && t.compare(0, k, first[0]) == 0) {
        size_t r = t.find_first_not_of(' ', k);
        if (r == std::string::npos || f.width64(s, t.c_str() + r) <= width64)
            return k;
    }
    return fit_bytes(f, s, t, width64);
}

}

void Menu::open(Form f, const std::string& title, const std::vector<std::string>& items, int64_t now, bool read_only,
                int mixed_row, bool mixed_title, int lead)
{
    form_ = f;
    read_only_ = read_only;
    value_ = wrap_ = cells_ = false;
    mark_ = edit_ = -1;
    values_.clear();
    mixed_.clear();
    wraps_.clear();
    subs_.clear();
    swatches_.clear();
    pin_ = max_first_ = 0;
    mixed_title_ = mixed_title;
    mixed_row_ = mixed_row;
    lead_ = read_only ? 0 : std::clamp(lead, 0, std::max(0, (int)items.size() - 1));
    title_ = title;
    model_.items = items;
    list_.attach(&model_, sz().settingsPitch, 0, sz().settingsPitch * (int)items.size());
    list_.set_focus(lead_, 0, now, false);
    first_ = 0;
    shown_ = (int)items.size();
    fade_ = ThumbFade{};
    held_ = false;
}

void Menu::value(int focus, int mark, bool wrap, int64_t now)
{
    value_ = true;
    mark_ = mark;
    wrap_ = wrap;
    list_.set_focus(std::clamp(focus, lead_, std::max(lead_, model_.count() - 1)), 0, now, false);
}

void Menu::cells(std::vector<std::string> values, std::vector<bool> mixed, int edit)
{
    cells_ = true;
    values_ = std::move(values);
    mixed_ = std::move(mixed);
    edit_ = edit;
}

Rect Menu::render(Canvas& c, Fonts& f, int64_t now)
{
    TextStyle ts = role::dialogTitle.std_style(), is = sz().dialogItem.style(), ms = is, vs = sz().settingsValue.style(),
              cs = sz().listCount.style();
    Rect keep = c.clip();
    ms.tcase = Case::Mixed;
    if (mixed_title_)
        ts.tcase = Case::Mixed;
    int pitch = px(sz().settingsPitch), edge = px(tok::edge);
    int n = model_.count();
    auto value_style = [&](int i) {
        TextStyle s = vs;
        if (i == edit_)
            accent_bold(s);
        return s;
    };
    int title_h = title_.empty() ? 0 : f.ascent(ts.px) + f.descent(ts.px);
    int top, title_base;
    Rect bounds;
    int vis = n;
    bool parts = !wraps_.empty() || !subs_.empty();
    pin_ = (form_ == Sheet && (value_ || cells_)) || parts ? lead_ : 0;
    std::vector<size_t> cut((size_t)n, 0);
    std::vector<int> h((size_t)n, pitch), vx((size_t)n, INT_MIN);
    for (int i = 0; i < n; i++) {
        bool line = read_only_ || i < lead_, note = value_ && i < lead_;
        bool mixed = i == mixed_row_ || ((size_t)i < mixed_.size() && mixed_[(size_t)i]);
        int clip_x = kPortraitW - edge;
        if ((size_t)i < values_.size() && !values_[(size_t)i].empty()) {
            vx[(size_t)i] = to64(canvas_w() - tok::edge) - f.width64(value_style(i), values_[(size_t)i].c_str());
            clip_x = (vx[(size_t)i] >> 6) - px(tok::gutter);
        }
        int si = i - lead_;
        if (parts && i < lead_) {
            const TextStyle& s = mixed ? ms : is;
            if ((size_t)i < wraps_.size() && !wraps_[(size_t)i].empty())
                h[(size_t)i] += f.ascent(s.px) + f.descent(s.px);
            if ((size_t)i < subs_.size() && !subs_[(size_t)i].empty())
                h[(size_t)i] += f.ascent(cs.px) + f.descent(cs.px);
            continue;
        }
        if (form_ != Sheet || !line || note || !mixed || (!read_only_ && i >= pin_) ||
            (si >= 0 && (size_t)si < swatches_.size()))
            continue;
        const std::string& t = model_.items[(size_t)i];
        int w64 = clip_x * 64 - to64(tok::edge);
        if (f.width64(ms, t.c_str()) <= w64)
            continue;
        size_t k = wrap_at(f, ms, t, w64);
        if (k == 0)
            continue;
        cut[(size_t)i] = k;
        h[(size_t)i] = pitch + f.ascent(ms.px) + f.descent(ms.px);
    }
    int pin_h = 0;
    for (int i = 0; i < pin_ && i < n; i++)
        pin_h += h[(size_t)i];
    max_first_ = 0;
    if (form_ == Sheet && n > 0) {
        int room_h = bottom_px_ - top_px_ - 2 * edge - title_h;
        if (read_only_) {
            int used = 0, last = n;
            while (last > 0 && used + h[(size_t)last - 1] <= room_h)
                used += h[(size_t)--last];
            max_first_ = std::min(last, n - 1);
            first_ = std::clamp(first_, 0, max_first_);
            used = 0;
            vis = 0;
            while (first_ + vis < n && (vis == 0 || used + h[(size_t)(first_ + vis)] <= room_h))
                used += h[(size_t)(first_ + vis++)];
        } else {
            vis = std::clamp(pin_ + (room_h - pin_h) / pitch, std::min(n, pin_ + 1), n);
            int room = vis - pin_, fr = std::clamp(list_.focus_row(), pin_, n - 1);
            max_first_ = n - room;
            first_ = std::clamp(first_, std::max(pin_, fr - room + 1), std::min(fr, n - room));
        }
    } else {
        first_ = pin_;
    }
    shown_ = vis;
    int rows_h = 0;
    for (int row = 0; row < vis; row++)
        rows_h += h[(size_t)(row < pin_ ? row : first_ + row - pin_)];
    if (form_ == Sheet) {
        panel_h_ = edge + title_h + rows_h + edge;
        int y0 = bottom_px_ - panel_h_;
        c.set_clip(keep);
        c.fill({0, y0, kPortraitW, bottom_px_}, tok::color_bgElevated);
        title_base = y0 + edge + f.ascent(ts.px);
        top = y0 + edge + title_h;
        bounds = {0, y0, kPortraitW, bottom_px_};
    } else {
        c.set_clip(keep);
        c.ground(kScreen, tok::color_black);
        title_base = px(tok::pivotBaseline);
        top = px(tok::contentTop);
        bounds = {0, title_base - f.ascent(ts.px), kPortraitW, top + rows_h};
    }
    items_top_ = fx_div(fx_int(top), ppu());
    pin_extra_ = fx_div(fx_int(pin_h - pin_ * pitch), ppu());
    title_rect_ = {};
    if (!title_.empty()) {
        f.draw(c, ts, title_.c_str(), to64(tok::edge), title_base, kWhite, alpha256(role::dialogTitle.alpha));
        int x0 = px(tok::edge);
        title_rect_ = {x0, title_base - f.ascent(ts.px), x0 + (f.width64(ts, title_.c_str()) >> 6),
                       title_base + f.descent(ts.px)};
    }
    int rt = top;
    for (int row = 0; row < vis; row++) {
        int i = row < pin_ ? row : first_ + row - pin_, rh = h[(size_t)i];
        bool line = read_only_ || i < lead_, note = value_ && i < lead_;
        bool mixed = i == mixed_row_ || ((size_t)i < mixed_.size() && mixed_[(size_t)i]);
        const TextStyle& s = note ? vs : mixed ? ms : is;
        int base = rt + (pitch - f.ascent(s.px) - f.descent(s.px)) / 2 + f.ascent(s.px);
        fx a = note   ? sz().settingsValue.alpha
               : line ? tok::type_dialogItem_alphaDim
                      : list_.row_alpha(i, now, tok::type_dialogItem_alpha, tok::type_dialogItem_alphaDim);
        int clip_x = kPortraitW - edge;
        if (vx[(size_t)i] != INT_MIN) {
            const std::string& v = values_[(size_t)i];
            int vb = rt + (pitch - f.ascent(vs.px) - f.descent(vs.px)) / 2 + f.ascent(vs.px);
            fx va = i == edit_ ? kOne
                    : line     ? tok::settingsValueDim
                               : list_.row_alpha(i, now, tok::settingsValueFocused, tok::settingsValueDim);
            f.draw(c, value_style(i), v.c_str(), vx[(size_t)i], vb, i == edit_ ? accent() : kWhite, alpha256(va));
            clip_x = (vx[(size_t)i] >> 6) - px(tok::gutter);
        }
        if (line || cells_)
            c.set_clip(Rect{0, rt, clip_x, rt + rh}.intersect(keep));
        int si = i - lead_;
        uint32_t ink = i == mark_ ? accent() : kWhite;
        if (i == mark_ && accent_off())
            a = kOne;
        if (si >= 0 && (size_t)si < swatches_.size()) {
            int side = s.px, sx = px(tok::edge), sy = rt + (pitch - side) / 2;
            c.set_clip(keep);
            c.fill({sx, sy, sx + side, sy + side}, swatches_[(size_t)si]);
        } else if (cut[(size_t)i]) {
            const std::string& t = model_.items[(size_t)i];
            f.draw(c, s, t.substr(0, cut[(size_t)i]).c_str(), to64(tok::edge), base, ink, alpha256(a));
            size_t r = t.find_first_not_of(' ', cut[(size_t)i]);
            f.draw(c, s, r == std::string::npos ? "" : t.c_str() + r, to64(tok::edge),
                   base + f.ascent(s.px) + f.descent(s.px), ink, alpha256(a));
        } else {
            f.draw(c, s, model_.items[i].c_str(), to64(tok::edge), base, ink, alpha256(a));
        }
        if (parts && i < lead_) {
            int next = base;
            if ((size_t)i < wraps_.size() && !wraps_[(size_t)i].empty()) {
                next += f.ascent(s.px) + f.descent(s.px);
                f.draw(c, s, wraps_[(size_t)i].c_str(), to64(tok::edge), next, ink, alpha256(a));
            }
            if ((size_t)i < subs_.size() && !subs_[(size_t)i].empty())
                f.draw(c, cs, subs_[(size_t)i].c_str(), to64(tok::edge), next + f.descent(s.px) + f.ascent(cs.px),
                       kWhite, alpha256(sz().listCount.alpha));
        }
        c.set_clip(keep);
        rt += rh;
    }
    if (n > 0 && !read_only_) {
        int by = top + (pin_h - pin_ * pitch) + px64(list_.bar_top(now)) - (first_ - pin_) * pitch;
        int bx = px(tok::edge - tok::focus_barOffset);
        c.set_clip(Rect{0, top + pin_h, kPortraitW, top + rows_h}.intersect(keep));
        c.fill({bx, by, bx + px(tok::focus_barWidth), by + pitch}, kWhite);
        c.set_clip(keep);
    }
    bar_ = {};
    if (form_ == Sheet && max_first_ > pin_) {
        int64_t total = 0, above = 0, most = 0;
        for (int i = pin_; i < n; i++) {
            total += h[(size_t)i];
            above += i < first_ ? h[(size_t)i] : 0;
            most += i < max_first_ ? h[(size_t)i] : 0;
        }
        bar_ = thumb_of(top + pin_h, bottom_px_, total - most, total, above);
    }
    fade_.track(bar_.on(), first_ != drawn_first_ || held_, now);
    if (!held_)
        fade_.release(now);
    drawn_first_ = first_;
    thumb_ = bar_.faded(fade_.at(now));
    draw_thumb(c, thumb_);
    drawn_at_ = now;
    bounds_ = bounds;
    return bounds;
}

bool Menu::scroll_to(int first, int64_t now)
{
    int n = model_.count();
    if (form_ != Sheet || shown_ >= n)
        return false;
    first = std::clamp(first, pin_, std::max(pin_, max_first_));
    if (first == first_)
        return false;
    first_ = first;
    int fr = std::clamp(list_.focus_row(), first_, first_ + shown_ - pin_ - 1);
    if (fr != list_.focus_row())
        list_.set_focus(fr, 0, now, false);
    return true;
}

bool Menu::step(int delta, int64_t now, bool hold)
{
    if (read_only_)
        return scroll_to(first_ + delta, now);
    int n = model_.count(), to = list_.focus_row() + delta;
    if (to >= lead_ && to < n)
        return list_.move(delta, now);
    if (!wrap_ || hold || n - lead_ < 2)
        return false;
    list_.set_focus(delta > 0 ? lead_ : n - 1, 0, now, true);
    return true;
}

bool Menu::title_hit(fx x, fx y) const
{
    int px_x = fx_round(to_px(x)), px_y = fx_round(to_px(y));
    return px_x >= title_rect_.x0 && px_x < title_rect_.x1 && px_y >= title_rect_.y0 && px_y < title_rect_.y1;
}

int Menu::row_at(fx y) const
{
    if (y < items_top_)
        return -1;
    fx rel = y - items_top_, pinned = pin_ * sz().settingsPitch + pin_extra_;
    if (rel < pinned)
        return pin_ - 1;
    int i = pin_ + (int)((rel - pinned) / sz().settingsPitch);
    return i < shown_ ? first_ + i - pin_ : -1;
}

int Menu::hit(fx y) const
{
    int r = read_only_ ? -1 : row_at(y);
    return r >= lead_ ? r : -1;
}

bool Menu::lead_hit(fx y) const
{
    int r = row_at(y);
    return r >= 0 && r < lead_;
}

void JumpGrid::open(const bool avail[kSectionCount], int section)
{
    Scope scope(Fit::Kept, "jump");
    std::copy(avail, avail + kSectionCount, avail_);
    press_.reset();
    down_ = drag_ = caught_ = false;
    focus_ = has(section) ? section : -1;
    if (focus_ < 0)
        for (int i = 0; i < kSectionCount; i++)
            if (avail_[i]) {
                focus_ = i;
                break;
            }
    scroll_.set_limit(limit_of());
    scroll_.set(offset_for(focus_, 0));
    fade_ = ThumbFade{};
}

pos64 JumpGrid::limit_of() const
{
    return (pos64)tok::jumpTop + (pos64)(kRows - 1) * kPitch + tok::tileS + tok::edge - bottom_;
}

void JumpGrid::set_bottom(fx b, fx at)
{
    Scope scope(Fit::Kept, "jump.bottom");
    bottom_ = u_from(b, at);
    scroll_.set_limit(limit_of());
    scroll_.set(offset_for(focus_, scroll_.pos()));
}

bool JumpGrid::step(int delta, int64_t now)
{
    Scope scope(Fit::Kept, "jump");
    bool moved = step_focus(delta, now);
    pos64 off = scroll_.target(), to = offset_for(focus_, off);
    if (drag_ || to == off)
        return moved;
    scroll_.animate(now, to, ms_of(tok::motion_scroll), Curve::EaseIn);
    return true;
}

bool JumpGrid::step_focus(int delta, int64_t now)
{
    if (focus_ == kExit && delta > 0) {
        set_focus(kSearch, now);
        return true;
    }
    if (focus_ == kSearch && delta < 0) {
        set_focus(kExit, now);
        return true;
    }
    for (int i = (focus_ == kSearch ? kExit : focus_) + delta; i >= 0 && i < kSectionCount; i += delta)
        if (avail_[i]) {
            set_focus(i, now);
            return true;
        }
    if (delta < 0 && focus_ >= 0) {
        set_focus(kSearch, now);
        return true;
    }
    return false;
}

void JumpGrid::set_focus(int i, int64_t now)
{
    focus_ = i;
    press_.focus(i, i != kExit, now);
}

pos64 JumpGrid::offset_for(int i, pos64 off) const
{
    pos64 top = row_top(slot_of(i)), bottom = top + tok::tileS;
    if (top - off < tok::edge)
        off = top - tok::edge;
    else if (bottom - off > bottom_ - tok::edge)
        off = bottom - (bottom_ - tok::edge);
    return std::clamp<pos64>(off, 0, limit_of());
}

bool JumpGrid::in_band(int slot, pos64 off) const
{
    pos64 top = row_top(slot) - off;
    return top >= tok::edge && top + tok::tileS <= bottom_ - tok::edge;
}

void JumpGrid::focus_after_scroll(int64_t now)
{
    pos64 off = scroll_.pos();
    int fs = slot_of(focus_);
    if (in_band(fs, off))
        return;
    int d = row_top(fs) - off < tok::edge ? 1 : -1;
    for (int s = d > 0 ? 0 : kSlotCount - 1; s >= 0 && s < kSlotCount; s += d) {
        if (!in_band(s, off))
            continue;
        int i = s == 0 ? kExit : s == 1 ? kSearch : s - kExitCells;
        if (i == kExit || i == kSearch || avail_[i]) {
            if (i != focus_)
                set_focus(i, now);
            return;
        }
    }
}

void JumpGrid::cell_center(int i, fx& x, fx& y) const
{
    Scope scope(Fit::Kept, "jump");
    int slot = slot_of(i);
    x = cell_x(slot) + tok::tileS / 2;
    y = cell_y(slot) + tok::tileS / 2;
}

void JumpGrid::press(const Event& in, int64_t now)
{
    Scope scope(Fit::Kept, "jump");
    Event e = scoped(in);
    press(e.x, e.y, now);
}

void JumpGrid::press(fx x, fx y, int64_t now)
{
    Scope scope(Fit::Kept, "jump");
    caught_ = scroll_.moving();
    scroll_.stop();
    down_ = true;
    drag_ = false;
    down_x_ = x;
    down_y_ = y;
    vel_.clear();
    vel_.add(now, y);
    if (caught_)
        return;
    int h = hit(x, y);
    if (h < 0)
        return;
    focus_ = h;
    press_.down(h, now);
}

bool JumpGrid::move_touch(const Event& in, int64_t now)
{
    Scope scope(Fit::Kept, "jump");
    Event e = scoped(in);
    return move_touch(e.x, e.y, now);
}

bool JumpGrid::move_touch(fx x, fx y, int64_t now)
{
    Scope scope(Fit::Kept, "jump");
    if (!down_)
        return false;
    vel_.add(now, y);
    if (drag_) {
        pos64 was = scroll_.pos();
        int f = focus_;
        scroll_.drag(y);
        focus_after_scroll(now);
        return scroll_.pos() != was || focus_ != f;
    }
    int64_t dx = (int64_t)x - down_x_, dy = (int64_t)y - down_y_;
    int64_t slop = std_u(tok::touchSlop);
    if (dx * dx + dy * dy <= slop * slop)
        return false;
    press_.cancel(now);
    if (axis_of(dx, dy) == Axis::Horizontal) {
        down_ = false;
        return false;
    }
    drag_ = true;
    scroll_.begin(y);
    return false;
}

int JumpGrid::release(const Event& in, int64_t now)
{
    Scope scope(Fit::Kept, "jump");
    Event e = scoped(in);
    return release(e.x, e.y, now);
}

int JumpGrid::release(fx x, fx y, int64_t now)
{
    Scope scope(Fit::Kept, "jump");
    if (!down_)
        return -2;
    down_ = false;
    vel_.add(now, y);
    if (drag_) {
        drag_ = false;
        scroll_.release(now, vel_.per_s());
        return -2;
    }
    int64_t dx = (int64_t)x - down_x_, dy = (int64_t)y - down_y_;
    int64_t slop = std_u(tok::touchSlop);
    if (caught_ || dx * dx + dy * dy > slop * slop) {
        press_.cancel(now);
        return -2;
    }
    press_.tap(now);
    return hit(down_x_, down_y_);
}

bool JumpGrid::advance(int64_t now)
{
    Scope scope(Fit::Kept, "jump");
    if (!scroll_.moving())
        return false;
    bool fl = scroll_.flinging();
    pos64 was = scroll_.pos();
    int f = focus_;
    scroll_.step(now);
    if (fl)
        focus_after_scroll(now);
    return scroll_.pos() != was || focus_ != f;
}

void JumpGrid::stop_scroll()
{
    Scope scope(Fit::Kept, "jump");
    scroll_.stop();
    down_ = drag_ = false;
}

void JumpGrid::cancel_touch()
{
    Scope scope(Fit::Kept, "jump");
    stop_scroll();
    caught_ = false;
    press_.drop();
}

fx JumpGrid::cell_x(int slot) const { return kLeft + (slot % kSlots) * kPitch; }

fx JumpGrid::cell_y(int slot) const { return (fx)(row_top(slot) - scroll_.pos()); }

Rect JumpGrid::render(Canvas& c, Fonts& f, int64_t now)
{
    Scope scope(Fit::Kept, "jump");
    c.ground(kScreen, tok::color_bg);
    Rect e = cell_rect(kExit);
    if (e.y1 > 0) {
        TextStyle es = role::jumpExit.style();
        int ebase = e.y0 + (e.y1 - e.y0 - f.ascent(es.px) - f.descent(es.px)) / 2 + f.ascent(es.px);
        f.draw(c, es, "exit", to64(tok::edge), ebase, kWhite, alpha256(focus_ == kExit ? tok::alpha_full : role::jumpExit.alpha));
        if (focus_ == kExit) {
            int bx = px(tok::edge - tok::focus_barOffset);
            c.fill({bx, e.y0, bx + px(tok::focus_barWidth), e.y1}, kWhite);
        }
    }
    if (cell_rect(kSearch).y1 > 0)
        draw_search(c, now);
    for (int i = 0; i < kSectionCount; i++) {
        Rect t = cell_rect(i);
        if (t.y1 > 0 && t.y0 < kPortraitH)
            draw_cell(c, f, i, now);
    }
    pos64 base = scroll_.base();
    bar_ = thumb_of(px(tok::edge), px(bottom_ - tok::edge), bottom_, base + bottom_, std::min(scroll_.pos(), base));
    fade_.track(bar_.on(), scroll_.moving() || drag_, now);
    thumb_ = bar_.faded(fade_.at(now));
    draw_thumb(c, thumb_);
    return kScreen;
}

Rect JumpGrid::cell_rect(int i) const
{
    int slot = slot_of(i);
    int tile = std_px(tok::tileS), x = px(cell_x(slot)), y = px(cell_y(slot));
    return {x, y, x + tile, y + tile};
}

void JumpGrid::draw_search(Canvas& c, int64_t now)
{
    Rect t = cell_rect(kSearch);
    bool focused = focus_ == kSearch;
    int bw = px(focused ? tok::letterTileBorderFocused : tok::letterTileBorder);
    int ba = alpha256(focused ? tok::alpha_full : tok::alpha_low);
    Zoom z = tile_zoom(t, press_.draw(kSearch, now));
    if (z.on())
        c.frame_aa(Rect64::of(t, z), Rect64::of(t.grow(-bw), z), kWhite, ba);
    else
        border(c, t, bw, kWhite, ba);
    int g = px(tok::jumpSearchGlyph);
    int cx = t.x1 - px(tok::letterInsetX) - g * 27 / 64, cy = t.y1 - px(tok::letterInsetBaseline) - g * 27 / 64;
    if (z.on()) {
        cx = z.px_x(cx);
        cy = z.px_y(cy);
    }
    int a = alpha256(role::letterGlyph.alpha);
    if (!glyphs_)
        return;
    draw_centered(c, glyphs_->glyph("searchRing", g, z.s), cx, cy, kWhite, a);
    draw_centered(c, glyphs_->glyph("searchHandle", g, z.s), cx, cy, kWhite, a);
}

void JumpGrid::icons(std::vector<Icon>& out)
{
    Scope scope(Fit::Kept, "jump");
    int g = px(tok::jumpSearchGlyph);
    out.push_back({"searchRing", g});
    out.push_back({"searchHandle", g});
}

void JumpGrid::warm(std::vector<WarmText>& out)
{
    Scope scope(Fit::Kept, "jump");
    for (int i = 0; i < kSectionCount; i++)
        warm_add(out, letter_role().style(), section_letter(i));
}

void JumpGrid::draw_cell(Canvas& c, Fonts& f, int i, int64_t now)
{
    Rect t = cell_rect(i);
    bool focused = focus_ == i;
    int bw = px(focused ? tok::letterTileBorderFocused : tok::letterTileBorder);
    int ba = alpha256(focused ? tok::alpha_full : tok::alpha_low);
    Zoom z = tile_zoom(t, press_.draw(i, now));
    if (z.on())
        c.frame_aa(Rect64::of(t, z), Rect64::of(t.grow(-bw), z), kWhite, ba);
    else if (avail_[i])
        border(c, t, bw, kWhite, ba);
    TextStyle ls = letter_role().style();
    const char* l = section_letter(i);
    int w = f.width64(ls, l);
    Rect keep = c.clip();
    c.set_clip(z.rect(t).intersect(keep));
    f.draw(c, ls, l, (t.x1 - px(tok::letterInsetX)) * 64 - w, t.y1 - px(tok::letterInsetBaseline), kWhite,
           alpha256(avail_[i] ? letter_role().alpha : tok::alpha_low), z);
    c.set_clip(keep);
}

Rect JumpGrid::render_press(Canvas& c, Fonts& f, int64_t now)
{
    Scope scope(Fit::Kept, "jump");
    Rect dmg;
    for (int i : {press_.on.item, press_.back.item}) {
        if (i < 0)
            continue;
        Rect r = cell_rect(i).grow(1).intersect(kScreen);
        if (r.empty())
            continue;
        c.set_clip(r);
        c.ground(r, tok::color_bg);
        if (i == kSearch)
            draw_search(c, now);
        else
            draw_cell(c, f, i, now);
        dmg = dmg.unite(r);
    }
    c.set_clip(kScreen);
    return dmg;
}

int JumpGrid::hit(fx x, fx y) const
{
    Scope scope(Fit::Kept, "jump");
    pos64 cx = (pos64)x - (kLeft - tok::jumpGap / 2);
    pos64 cy = (pos64)y + scroll_.pos() - (tok::jumpTop - tok::jumpGap / 2);
    if (cx < 0 || cy < 0)
        return -2;
    int col = (int)(cx / kPitch), row = (int)(cy / kPitch), slot = row * kSlots + col;
    if (col >= kSlots || slot >= kSlotCount)
        return -2;
    if (slot == 0)
        return kExit;
    if (slot == 1)
        return kSearch;
    int i = slot - kExitCells;
    return avail_[i] ? i : -2;
}

}
