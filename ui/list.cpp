#include "list.h"

#include <algorithm>

#include "../core/gesture.h"

namespace fd {

namespace {

constexpr int ms_of(fx t) { return t >> kFxBits; }

}

void ListView::attach(const ListModel* m, fx pitch, fx top, fx bottom, fx collapse)
{
    m_ = m;
    pitch_ = pitch;
    top_ = top;
    bottom_ = bottom;
    collapse_ = collapse;
    int n = (int)((bottom - top + collapse + pitch - 1) / pitch) + 2;
    slots_.assign((size_t)n, Row{});
    row_ = col_ = 0;
    prev_row_ = -1;
    scroll_.set(0);
    sync_limit();
    clamp_focus();
    bar_from_ = bar_to_ = (pos64)row_ * pitch_;
    alpha_in_.set(kOne);
    alpha_out_.set(0);
    press_.reset();
    invalidate();
}

void ListView::set_bottom(fx bottom)
{
    if (bottom == bottom_)
        return;
    bottom_ = bottom;
    if (!pitch_)
        return;
    size_t n = (size_t)((bottom - top_ + collapse_ + pitch_ - 1) / pitch_) + 2;
    if (n > slots_.size())
        slots_.resize(n);
    int count = m_ ? m_->count() : 0;
    pos64 view = bottom_ - top_;
    scroll_.fit((pos64)count * pitch_ - view, row_ < count ? (pos64)(row_ + 1) * pitch_ - view : 0, INT64_MAX);
    invalidate();
}

void ListView::sync_limit()
{
    int n = m_ ? m_->count() : 0;
    scroll_.set_limit((pos64)n * pitch_ - (bottom_ - top_));
}

void ListView::clamp_focus()
{
    int n = m_ ? m_->count() : 0;
    if (n == 0) {
        row_ = col_ = 0;
        return;
    }
    row_ = std::clamp(row_, 0, n - 1);
    col_ = std::clamp(col_, 0, std::max(0, m_->cells(row_) - 1));
}

void ListView::invalidate()
{
    gen_++;
    for (Row& r : slots_)
        r.index = -1;
}

bool ListView::keep_motion(const ListModel* m, int was, uint64_t key)
{
    int r = 0, c = 0;
    if (!scroll_.moving() || !key || m->count() < was || !m->find(key, r, c) || r != row_ || c != col_)
        return false;
    m_ = m;
    sync_limit();
    invalidate();
    return true;
}

void ListView::reset_model(const ListModel* m, int64_t now, uint64_t key)
{
    pos64 screen = (pos64)row_ * pitch_ - scroll_.pos();
    m_ = m;
    sync_limit();
    int r = 0, c = 0;
    if (key && m_->find(key, r, c)) {
        row_ = r;
        col_ = c;
        scroll_.set((pos64)row_ * pitch_ - screen);
    } else {
        clamp_focus();
        scroll_.set(scroll_.pos());
    }
    prev_row_ = -1;
    down_row_ = -1;
    bar_from_ = bar_to_ = (pos64)row_ * pitch_;
    bar_start_ = now - 1000000;
    press_.reset();
    invalidate();
}

int ListView::first_visible() const
{
    return (int)std::max<pos64>(0, (scroll_.pos() - lift()) / pitch_);
}

int ListView::last_visible() const
{
    int n = m_ ? m_->count() : 0;
    pos64 end = scroll_.pos() + (bottom_ - top_);
    return std::min(n - 1, (int)((end - 1) / pitch_));
}

bool ListView::slow() const
{
    int64_t v = scroll_.velocity();
    return (v < 0 ? -v : v) < (int64_t)(bottom_ - top_);
}

pos64 ListView::bar_top(int64_t now) const
{
    int d = ms_of(tok::motion_focusCursor);
    int64_t e = now - bar_start_;
    if (e >= d)
        return bar_to_;
    if (e <= 0)
        return bar_from_;
    fx t = curve(Curve::Sine, (fx)(e * kOne / d));
    return bar_from_ + (((bar_to_ - bar_from_) * t) >> kFxBits);
}

fx ListView::row_alpha(int row, int64_t now, fx focused, fx other) const
{
    if (row == row_)
        return fx_lerp(other, focused, alpha_in_.at(now));
    if (row == prev_row_)
        return fx_lerp(other, focused, alpha_out_.at(now));
    return other;
}

void ListView::set_focus(int row, int col, int64_t now, bool animate)
{
    int n = m_ ? m_->count() : 0;
    if (n == 0)
        return;
    row = std::clamp(row, 0, n - 1);
    col = std::clamp(col, 0, std::max(0, m_->cells(row) - 1));
    if (row == row_ && col == col_)
        return;
    int a = ms_of(tok::motion_focusAlpha);
    if (row != row_) {
        fx was = alpha_in_.at(now);
        prev_row_ = row_;
        alpha_out_.run(now, was, 0, animate ? a : 0, Curve::Linear);
        alpha_in_.run(now, 0, kOne, animate ? a : 0, Curve::Linear);
    }
    bar_from_ = animate ? bar_top(now) : (pos64)row * pitch_;
    bar_to_ = (pos64)row * pitch_;
    bar_start_ = animate ? now : now - 1000000;
    row_ = row;
    col_ = col;
    if (press_rows && (press_.held() || press_.key))
        press_.focus(item(row, col), pressable(row, col), now);
}

bool ListView::move(int delta, int64_t now)
{
    int n = m_ ? m_->count() : 0;
    if (n == 0)
        return false;
    int r = row_, c = col_;
    if (delta > 0) {
        if (c + 1 < m_->cells(r))
            c++;
        else if (r + 1 < n) {
            r++;
            c = 0;
        } else
            return false;
    } else {
        if (c > 0)
            c--;
        else if (r > 0) {
            r--;
            c = std::max(0, m_->cells(r) - 1);
        } else if (scroll_.target() > 0) {
            ensure_visible(now);
            return true;
        } else
            return false;
    }
    set_focus(r, c, now, true);
    ensure_visible(now);
    return true;
}

bool ListView::to_top(int64_t now)
{
    if (!m_ || m_->count() == 0 || (row_ == 0 && col_ == 0 && scroll_.pos() == 0 && !scroll_.moving()))
        return false;
    set_focus(0, 0, now, false);
    scroll_.animate(now, 0, ms_of(tok::motion_scroll), Curve::EaseIn);
    return true;
}

void ListView::ensure_visible(int64_t now) { scroll_into((pos64)row_ * pitch_, pitch_, now); }

bool ListView::lift_focus(fx y, int64_t now)
{
    pos64 top = (pos64)row_ * pitch_, off = scroll_.target();
    pos64 by = (pos64)top_ + top + pitch_ - off - y;
    if (!m_ || row_ >= m_->count() || by <= 0 || top - off < by)
        return false;
    pos64 to = (off + by + pitch_ - 1) / pitch_ * pitch_;
    scroll_.lift(now, to - off, ms_of(tok::motion_scroll), Curve::EaseIn);
    return true;
}

void ListView::focus_after_scroll(int64_t now)
{
    int n = m_ ? m_->count() : 0;
    if (n == 0)
        return;
    pos64 off = scroll_.pos();
    pos64 view = bottom_ - top_;
    pos64 top = (pos64)row_ * pitch_;
    pos64 band = off - lift_at(off);
    if (top >= band && top + pitch_ <= off + view)
        return;
    int first = (int)((band + pitch_ - 1) / pitch_);
    int last = (int)((off + view) / pitch_) - 1;
    if (last < first)
        last = first;
    int r = std::clamp(top < band ? first : last, 0, n - 1);
    set_focus(r, std::min(col_, std::max(0, m_->cells(r) - 1)), now, false);
}

int ListView::col_at(fx x) const
{
    if (cell_x.empty())
        return 0;
    for (size_t i = 0; i < cell_x.size(); i++)
        if (x >= cell_x[i] && x < cell_x[i] + kept_u(tok::gridTile))
            return (int)i;
    return -1;
}

void ListView::press(fx x, fx y, int64_t at, int64_t now)
{
    bool caught = scroll_.grab(at);
    down_ = true;
    drag_ = false;
    down_x_ = x;
    down_y_ = y;
    vel_.clear();
    vel_.add(at, y);
    down_row_ = -1;
    int n = m_ ? m_->count() : 0;
    if (caught || (pos64)y < (pos64)top_ - lift() || y >= bottom_ || n == 0)
        return;
    pos64 c = (pos64)(y - top_) + scroll_.pos();
    int r = (int)(c / pitch_);
    int col = col_at(x);
    if (r >= n || col < 0 || col >= m_->cells(r) || (cell_h && c - (pos64)r * pitch_ >= cell_h))
        return;
    down_row_ = r;
    down_col_ = col;
    set_focus(r, col, now, true);
    if (press_rows && pressable(r, col))
        press_.down(item(r, col), now);
}

void ListView::move_touch(fx x, fx y, int64_t at, int64_t now)
{
    if (!down_)
        return;
    vel_.add(at, y);
    if (drag_) {
        scroll_.drag(y);
        focus_after_scroll(now);
        return;
    }
    int64_t dx = (int64_t)x - down_x_, dy = (int64_t)y - down_y_;
    int64_t slop = std_u(tok::touchSlop);
    if (dx * dx + dy * dy <= slop * slop)
        return;
    press_.cancel(now);
    if (axis_of(dx, dy) == Axis::Horizontal) {
        down_ = false;
        return;
    }
    drag_ = true;
    scroll_.begin(y);
}

bool ListView::release(fx x, fx y, int64_t at, int64_t now, int& tap_row, int& tap_col)
{
    if (!down_)
        return false;
    down_ = false;
    vel_.add(at, y);
    if (drag_) {
        drag_ = false;
        scroll_.release(now, vel_.per_s());
        return false;
    }
    int64_t dx = (int64_t)x - down_x_, dy = (int64_t)y - down_y_;
    int64_t slop = std_u(tok::touchSlop);
    if (dx * dx + dy * dy > slop * slop || down_row_ < 0) {
        press_.cancel(now);
        return false;
    }
    press_.tap(now);
    tap_row = down_row_;
    tap_col = down_col_;
    scroll_into((pos64)tap_row * pitch_, pitch_, now);
    return true;
}

void ListView::scroll_into(pos64 top, pos64 extent, int64_t now)
{
    pos64 view = bottom_ - top_;
    pos64 off = scroll_.target();
    pos64 want = off;
    if (top < off - lift_at(off) || (top == 0 && off > 0))
        want = top ? top + collapse_ : 0;
    else if (top + extent > off + view)
        want = top + extent - view;
    if (want != off)
        scroll_.animate(now, want, ms_of(tok::motion_scroll), Curve::EaseIn);
}

bool ListView::step(int64_t now)
{
    bool fl = scroll_.flinging();
    bool moved = scroll_.step(now);
    if (fl)
        focus_after_scroll(now);
    return moved;
}

bool ListView::animating(int64_t now) const
{
    return scroll_.moving() || now - bar_start_ < ms_of(tok::motion_focusCursor) || !alpha_in_.done(now) ||
           !alpha_out_.done(now) || press_stale(now);
}

bool ListView::pressable(int row, int col)
{
    if (!m_ || row < 0 || row >= m_->count() || col < 0 || col >= m_->cells(row))
        return false;
    Row::Kind k = slot(row).kind;
    return k == Row::Item || k == Row::Grid;
}

bool ListView::press_stale(int64_t now) const
{
    int lo = first_visible(), hi = last_visible();
    for (const PressSlots::Slot* s : {&press_.on, &press_.back})
        if (s->item >= 0 && s->item / 4 >= lo && s->item / 4 <= hi && press_.stale(s->item, now))
            return true;
    return false;
}

void ListView::key_down(int64_t now)
{
    if (press_rows)
        press_.key_down(item(row_, col_), pressable(row_, col_), now);
}

Row& ListView::slot(int row)
{
    for (Row& r : slots_)
        if (r.index == row && r.gen == gen_)
            return r;
    int lo = first_visible(), hi = last_visible();
    Row* pick = nullptr;
    int far = -1;
    for (Row& r : slots_) {
        if (r.gen != gen_ || r.index < 0) {
            pick = &r;
            break;
        }
        int d = r.index < lo ? lo - r.index : r.index > hi ? r.index - hi : -1;
        if (d > far) {
            far = d;
            pick = &r;
        }
    }
    if (!pick)
        pick = &slots_[0];
    pick->index = row;
    pick->gen = gen_;
    pick->ncells = 0;
    pick->count_line = pick->accent = pick->bold = pick->ring = false;
    pick->primary.clear();
    pick->secondary.clear();
    pick->number.clear();
    pick->trailing.clear();
    m_->fill(row, *pick);
    fills_++;
    return *pick;
}

}
