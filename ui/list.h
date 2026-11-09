#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "../core/press.h"
#include "../core/scroll.h"
#include "../core/tween.h"
#include "../gfx/font.h"
#include "../gfx/glyph.h"

namespace fd {

struct Coverage;

struct Cell {
    bool letter = false, accent = false;
    std::string title, sub;
    uint32_t album = 0xFFFFFFFFu;
};

struct Row {
    enum Kind { Item, Divider, Grid, Label };
    int index = -1;
    uint32_t gen = 0;
    Kind kind = Item;
    std::string primary, secondary, number, trailing;
    bool count_line = false, accent = false, bold = false, ring = false;
    bool ring_toggle = false, ring_on = false, ring_bare = false, hide = false;
    bool collection = false, lib_sub = false;
    const std::vector<uint32_t>* thumbs = nullptr;
    const char* glyph = "play";
    int arc = 0, level = -1;
    fx ring_scale = kOne;
    const Coverage* arc_cv = nullptr;
    int ncells = 0;
    Cell cells[3];
};

class ListModel {
public:
    virtual ~ListModel() = default;
    virtual int count() const = 0;
    virtual int cells(int row) const { (void)row; return 1; }
    virtual void fill(int row, Row& out) const = 0;
    virtual uint64_t key(int row, int col) const = 0;
    virtual bool find(uint64_t key, int& row, int& col) const = 0;
};

struct GlowText {
    TextStyle st;
    std::string s;
    int x64 = 0, base = 0;
};

class PressGlow {
public:
    void down(int key, int64_t now)
    {
        clear();
        key_ = key;
        at_ = now;
    }
    void clear()
    {
        key_ = -1;
        sig_ = 0;
        mask_ = Coverage{};
    }
    bool off()
    {
        bool was = key_ >= 0;
        clear();
        return was;
    }
    int key() const { return key_; }
    int64_t at() const { return at_; }
    bool moving(int64_t now) const { return key_ >= 0 && now - at_ < (tok::motion_pressGlow >> kFxBits); }
    size_t bytes() const { return mask_.rot.capacity(); }
    bool draw(Canvas& c, Fonts& f, const std::vector<GlowText>& t, Rect clip, const Zoom& z, int64_t now);

private:
    bool build(Fonts& f, const std::vector<GlowText>& t, Rect clip, const Zoom& z);

    Coverage mask_;
    int key_ = -1, x_ = 0, y_ = 0;
    int64_t at_ = 0;
    uint64_t sig_ = 0;
};

class ListView {
public:
    void attach(const ListModel* m, fx pitch, fx top, fx bottom, fx collapse = 0);
    void set_bottom(fx bottom);
    void reset_model(const ListModel* m, int64_t now) { reset_model(m, now, focus_key()); }
    void reset_model(const ListModel* m, int64_t now, uint64_t key);
    bool keep_motion(const ListModel* m, int was, uint64_t key);
    uint64_t focus_key() const { return m_ && row_ < m_->count() ? m_->key(row_, col_) : 0; }
    const ListModel* model() const { return m_; }

    int focus_row() const { return row_; }
    int64_t focus_end() const { return alpha_in_.end(); }
    int focus_col() const { return col_; }
    void set_focus(int row, int col, int64_t now, bool animate);
    bool move(int delta, int64_t now);
    void ensure_visible(int64_t now);
    bool lift_focus(fx y, int64_t now, bool to_top = false);
    bool to_top(int64_t now);

    void press(fx x, fx y, int64_t at, int64_t now);
    void move_touch(fx x, fx y, int64_t at, int64_t now);
    bool release(fx x, fx y, int64_t at, int64_t now, int& tap_row, int& tap_col);
    bool dragging() const { return drag_; }
    bool pressed() const { return down_; }
    int pressed_row() const { return down_ && !drag_ ? down_row_ : -1; }
    void cancel()
    {
        down_ = drag_ = false;
        down_row_ = -1;
        press_.drop();
        glow_.clear();
        scroll_.end_drag();
    }

    bool press_rows = false;
    fx press_draw(int row, int col, int64_t now) { return press_.draw(item(row, col), now); }
    fx press_scale(int row, int col, int64_t now) const { return press_.scale(item(row, col), now); }
    bool press_frame(int row, int col, int64_t now)
    {
        press_.draw(item(row, col), now);
        return press_.framed(item(row, col), now);
    }
    bool press_stale(int64_t now) const;
    int64_t press_due(int64_t now) const { return press_.due(now); }
    void hold_presses(int64_t now) { press_.hold(now); }
    void reset_presses()
    {
        press_.reset();
        glow_.clear();
    }
    PressGlow* glow(int row, int64_t now);
    void key_down(int64_t now);
    void key_up(int64_t now) { press_.key_up(now); }
    const PressSlots& presses() const { return press_; }

    bool step(int64_t now);
    bool moving() const { return scroll_.moving() || drag_; }
    bool animating(int64_t now) const;
    pos64 offset() const { return scroll_.pos(); }
    pos64 lift() const { return lift_at(scroll_.pos()); }
    Scroll& scroll() { return scroll_; }
    const Scroll& scroll() const { return scroll_; }

    fx pitch() const { return pitch_; }
    fx top() const { return top_; }
    fx bottom() const { return bottom_; }
    fx collapse() const { return collapse_; }
    int first_visible() const;
    int last_visible() const;
    bool slow() const;
    pos64 bar_top(int64_t now) const;
    fx row_alpha(int row, int64_t now, fx focused, fx other) const;

    Row& slot(int row);
    void invalidate();
    size_t slot_count() const { return slots_.size(); }
    int fills() const { return fills_; }

    int col_at(fx x) const;
    int row_at(fx y) const;
    int bands(int* y, int max) const;
    std::vector<int> cell_x;
    fx cell_h = 0;
    int hide_title = -1;

private:
    pos64 lift_at(pos64 off) const { return std::clamp<pos64>(off, 0, collapse_); }
    void clamp_focus();
    void sync_limit();
    void focus_after_scroll(int64_t now);
    void scroll_into(pos64 top, pos64 extent, int64_t now);
    static int item(int row, int col) { return row * 4 + col; }
    bool pressable(int row, int col);

    const ListModel* m_ = nullptr;
    fx pitch_ = 0, top_ = 0, bottom_ = 0, collapse_ = 0;
    Scroll scroll_{ScrollKind::List};
    int row_ = 0, col_ = 0;
    int prev_row_ = -1;
    pos64 bar_from_ = 0, bar_to_ = 0;
    int64_t bar_start_ = -1000000;
    Tween alpha_in_, alpha_out_;
    bool down_ = false, drag_ = false;
    fx down_x_ = 0, down_y_ = 0;
    int down_row_ = -1, down_col_ = 0;
    Velocity vel_;
    std::vector<Row> slots_;
    uint32_t gen_ = 1;
    int fills_ = 0;
    PressSlots press_;
    PressGlow glow_;
};

}
