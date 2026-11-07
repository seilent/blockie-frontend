#pragma once
#include <string>
#include <vector>

#include "../core/event.h"
#include "../gfx/canvas.h"
#include "../gfx/font.h"
#include "../gfx/glyph.h"
#include "../lib/section.h"
#include "list.h"
#include "scrollbar.h"

namespace fd {

class ItemsModel : public ListModel {
public:
    std::vector<std::string> items;
    int count() const override { return (int)items.size(); }
    void fill(int row, Row& out) const override { out.primary = items[row]; }
    uint64_t key(int row, int) const override { return (uint64_t)row + 1; }
    bool find(uint64_t key, int& row, int& col) const override
    {
        row = (int)key - 1;
        col = 0;
        return row >= 0 && row < count();
    }
};

class Menu {
public:
    enum Form { Sheet, Dialog };
    void open(Form f, const std::string& title, const std::vector<std::string>& items, int64_t now,
              bool read_only = false, int mixed_row = -1, bool mixed_title = false, int lead = 0);
    Form form() const { return form_; }
    bool read_only() const { return read_only_; }
    void value(int focus, int mark, bool wrap, int64_t now);
    void mark(int i) { mark_ = i; }
    int mark() const { return mark_; }
    void cells(std::vector<std::string> values, std::vector<bool> mixed, int edit);
    void lead_parts(std::vector<std::string> wraps, std::vector<std::string> subs = {})
    {
        wraps_ = std::move(wraps);
        subs_ = std::move(subs);
    }
    const std::vector<std::string>& wraps() const { return wraps_; }
    const std::vector<std::string>& subs() const { return subs_; }
    int edit() const { return edit_; }
    void swatches(std::vector<uint32_t> colors) { swatches_ = std::move(colors); }
    const std::vector<uint32_t>& swatches() const { return swatches_; }
    bool step(int delta, int64_t now, bool hold = false);
    ListView& list() { return list_; }
    int focus() const { return list_.focus_row(); }
    const std::string& title() const { return title_; }
    const std::vector<std::string>& items() const { return model_.items; }
    Rect render(Canvas& c, Fonts& f, int64_t now);
    void set_frame(int top, int bottom)
    {
        top_px_ = top;
        bottom_px_ = bottom;
    }
    int bottom_px() const { return bottom_px_; }
    Rect bounds() const { return bounds_; }
    int panel_h() const { return panel_h_; }
    int first() const { return first_; }
    bool at_top() const { return first_ <= pin_; }
    void set_held(bool h) { held_ = h; }
    bool scroll_to(int first, int64_t now);
    int hit(fx y) const;
    bool lead_hit(fx y) const;
    bool title_hit(fx x, fx y) const;
    bool animating(int64_t now) const { return list_.animating(now) || list_.animating(drawn_at_); }
    bool thumb_stale(int64_t now) const { return fade_.fading(now) || bar_.faded(fade_.at(now)) != thumb_; }
    Rect thumb_rect() const { return bar_.r.unite(thumb_.r); }
    ThumbFade& thumb_fade() { return fade_; }
    const ThumbFade& thumb_fade() const { return fade_; }

private:
    int row_at(fx y) const;
    Form form_ = Dialog;
    bool read_only_ = false, mixed_title_ = false, value_ = false, wrap_ = false;
    int mixed_row_ = -1, lead_ = 0, mark_ = -1, edit_ = -1;
    bool cells_ = false;
    std::vector<std::string> values_;
    std::vector<bool> mixed_;
    std::vector<std::string> wraps_, subs_;
    std::vector<uint32_t> swatches_;
    std::string title_;
    ItemsModel model_;
    ListView list_;
    int top_px_ = 0, bottom_px_ = kPortraitH;
    int panel_h_ = 0, first_ = 0, shown_ = 0, pin_ = 0, max_first_ = 0;
    fx items_top_ = 0, pin_extra_ = 0;
    Rect title_rect_, bounds_;
    int64_t drawn_at_ = -1;
    Thumb bar_, thumb_;
    ThumbFade fade_;
    int drawn_first_ = 0;
    bool held_ = false;
};

class JumpGrid {
public:
    static constexpr int kExit = -1;
    static constexpr int kSearch = kSectionCount;

    void open(const bool avail[kSectionCount], int section);
    int focus() const { return focus_; }
    bool step(int delta, int64_t now);
    Rect render(Canvas& c, Fonts& f, int64_t now);
    Rect render_press(Canvas& c, Fonts& f, int64_t now);
    int hit(fx x, fx y) const;
    bool has(int i) const { return i >= 0 && i < kSectionCount && avail_[i]; }
    void press(const Event& e, int64_t now);
    void press(fx x, fx y, int64_t now);
    bool move_touch(const Event& e, int64_t now);
    bool move_touch(fx x, fx y, int64_t now);
    int release(const Event& e, int64_t now);
    int release(fx x, fx y, int64_t now);
    bool advance(int64_t now);
    bool moving() const { return scroll_.moving(); }
    bool dragging() const { return drag_; }
    void stop_scroll();
    void cancel_touch();
    pos64 offset() const { return scroll_.pos(); }
    pos64 limit() const { return scroll_.limit(); }
    void set_bottom(fx b, fx at);
    void key_down(int64_t now) { press_.key_down(focus_, focus_ != kExit, now); }
    void set_glyphs(GlyphCache* g) { glyphs_ = g; }
    static void icons(std::vector<Icon>& out);
    static void warm(std::vector<WarmText>& out);
    void key_up(int64_t now) { press_.key_up(now); }
    bool animating(int64_t now) const { return press_.stale(now); }
    int64_t press_due(int64_t now) const { return press_.due(now); }
    void reset_presses() { press_.reset(); }
    const PressSlots& presses() const { return press_; }
    void cell_center(int i, fx& x, fx& y) const;
    bool thumb_stale(int64_t now) const { return fade_.fading(now) || bar_.faded(fade_.at(now)) != thumb_; }
    Rect thumb_rect() const { return bar_.r.unite(thumb_.r); }
    ThumbFade& thumb_fade() { return fade_; }
    const ThumbFade& thumb_fade() const { return fade_; }

private:
    fx cell_x(int slot) const;
    fx cell_y(int slot) const;
    Rect cell_rect(int i) const;
    void draw_cell(Canvas& c, Fonts& f, int i, int64_t now);
    void draw_search(Canvas& c, int64_t now);
    bool step_focus(int delta, int64_t now);
    void set_focus(int i, int64_t now);
    pos64 offset_for(int i, pos64 off) const;
    bool in_band(int slot, pos64 off) const;
    void focus_after_scroll(int64_t now);
    pos64 limit_of() const;

    GlyphCache* glyphs_ = nullptr;
    fx bottom_ = tok::canvasH;
    bool avail_[kSectionCount] = {};
    int focus_ = -1;
    PressSlots press_;
    Scroll scroll_;
    Velocity vel_;
    bool down_ = false, drag_ = false, caught_ = false;
    fx down_x_ = 0, down_y_ = 0;
    Thumb bar_, thumb_;
    ThumbFade fade_;
};

}
