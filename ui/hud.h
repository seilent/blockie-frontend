#pragma once
#include <algorithm>
#include <string>
#include <vector>

#include "../core/event.h"
#include "../core/fx.h"
#include "../core/press.h"
#include "../gfx/canvas.h"
#include "../gfx/font.h"
#include "../gfx/glyph.h"
#include "tokens.h"

namespace fd {

class VolumeHud {
public:
    enum Hit { None = -1, Exit, Plus, Minus, Prev, Ring, Next, Queue };
    enum class Act : uint8_t { None, Pass, Plus, Minus, Prev, Ring, Next, Queue, Volume, Seek, Draw };
    struct Pos {
        bool on = false;
        int64_t ms = 0, dur = 0;
        uint32_t track = 0;
    };
    struct Look {
        fx group = kOne, k = kOne, size = kOne, number = kOne, exit = kOne, text = kOne, text_dx = 0, scrim = kOne,
           queue = kOne;
    };

    void show(int64_t now, int percent, bool playing, const std::string& artist, const std::string& title,
              bool step = false);
    void dismiss()
    {
        fade_at_ = close_at_ = press_up_ = -1;
        press_ = None;
        for (Coverage& cv : glow_)
            cv = Coverage{};
        shown_ = false;
        tilt_ = false;
        up_at_ = -1;
        drop_touch();
    }
    void invalidate()
    {
        num_dirty_ = true;
        built_ = false;
    }
    bool set_track(bool playing, const std::string& artist, const std::string& title, int64_t now = -1);
    bool set_queue(bool np)
    {
        if (np == queue_)
            return false;
        queue_ = np;
        built_ = false;
        return true;
    }
    bool queue() const { return queue_; }
    fx queue_y() const { return queue_y_; }
    static int text_top(Fonts& f);
    void hide(int64_t now);
    bool visible(int64_t now) const
    {
        return shown_ && !(close_at_ >= 0 && now >= close_at_ + ms_of(tok::motion_volumeTouchClose)) &&
               !(fade_at_ >= 0 &&
                 now >= fade_at_ + ms_of(tok::motion_volumeRestSlow) + ms_of(tok::motion_volumeScrimOut));
    }
    bool closing(int64_t now) const
    {
        return close_at_ >= 0 || (fade_at_ >= 0 && now >= fade_at_ + ms_of(tok::motion_volumeRestSlow));
    }
    Look look(int64_t now) const;
    bool moving(int64_t now) const;
    int64_t wake_at(int64_t now) const;
    fx opacity(int64_t now) const;
    void compose(Fonts& f, GlyphCache& g, const Surface& page, const Surface& layer, uint8_t* mask, const Surface& dst,
                 int64_t now);
    static void warm(const std::string& artist, const std::string& title, std::vector<WarmText>& out);
    static void icons(std::vector<Icon>& out);
    int hit(fx x, fx y, fx k = kOne, fx size = kOne) const;
    Act event(const Event& in, int64_t now, int percent, const Pos& at);
    void set_step(int pct) { step_ = pct; }
    int number() const { return to_; }
    void took(int percent) { keep_ = percent; }
    void anchor(int percent);
    bool seeking() const { return seeking_; }
    bool seek_moved() const { return seek_to_ != at_.ms; }
    int64_t seek_to() const { return seek_to_; }
    uint32_t seek_track() const { return at_.track; }
    const std::string& readout() const { return readout_; }
    int label_px() const { return label_px_; }
    bool held() const { return held_; }
    void release()
    {
        for (Coverage& cv : glow_)
            cv = Coverage{};
    }
    size_t glow_bytes() const { return glow_[0].rot.size() + glow_[1].rot.size() + glow_[2].rot.size(); }
    bool playing() const { return playing_; }
    bool exit_lit() const { return exit_lit_; }
    bool owns() const { return own_; }
    bool drops() const { return drop_; }
    void touch() { own_ = drop_ = false; }
    void drop_touch();
    void cancel(int64_t now);
    int keep() const { return own_ && drag_ == Drag::Volume ? keep_ : -1; }
    enum Box { BExit, BNumber, BPlus, BRow, BMinus, BLabel, BArtist, BTitle, BQueue, kBoxes };
    fx tilt_k(int64_t now) const;
    void tilt(int64_t now, double& rx, double& ry, double& depth) const;
    void plate(fx k, double* h) const;
    Rect box(int i) const { return i >= 0 && i < kBoxes ? box_[i] : Rect{}; }
    Rect readout_box() const { return read_; }
    bool plate_moved() const { return plate_moved_; }
    bool plate_nearest() const { return near_drawn_; }
#ifndef FD_ALSA
    void shot_seek(int64_t to, int64_t dur);
    void shot_tilt(double rx, double ry, double depth);
#endif

private:
    enum class Drag : uint8_t { Tap, Volume, Seek, Off };
    static constexpr int64_t ms_of(fx t) { return t >> kFxBits; }
    int64_t end_at() const { return in_at_ + ms_of(tok::motion_volumeOpenEnd); }
    int64_t number_at() const
    {
        return std::max(end_at(), chg_at_) + num_shift_ + ms_of(tok::motion_volumeNumberHold);
    }
    void rest_from(int64_t now)
    {
        fade_at_ = held_ ? -1 : std::max(end_at(), now) + ms_of(tok::motion_volumeRestHold);
    }
    void hold_end(int64_t now)
    {
        if (held_at_ >= 0)
            num_shift_ += now - held_at_;
        held_at_ = -1;
    }
    fx rest(int64_t now) const;
    fx scrim_k(int64_t now) const;
    fx number_k(int64_t now) const;
    void revive(int64_t now);
    static int number_right64();
    static TextStyle number_style(Fonts& f, const std::string& num);
    static void number_place(Fonts& f, const TextStyle& ns, const std::string& num, int& x64, int& base);
    static TextStyle label_style(int& top);
    int text_right() const;
    static int pause_px();
    fx pop(int64_t now) const;
    Act slide(const Event& e, int percent, const Pos& at);
    void tilt_at(fx x, fx y);
    void tilt_up(int64_t now)
    {
        if (tilt_ && up_at_ < 0)
            up_at_ = now;
    }
    void corners(const double* h, int* out) const;
    bool corners_moved() const;
    void draw_box(int i, Fonts& f, GlyphCache& g, Canvas& lc, bool fresh);
    void build_boxes(Fonts& f, GlyphCache& g, const Surface& layer, uint8_t* mask);
    void build_glows(GlyphCache& g);
    fx press_k(int64_t now) const;
    void draw_plate(Fonts& f, GlyphCache& g, const Surface& layer, uint8_t* mask, const Surface& dst, const Look& l,
                    fx k, fx s, int64_t now);
    bool shown_ = false;
    int percent_ = 0;
    bool playing_ = false;
    std::string artist_, title_;
    int64_t in_at_ = 0, pop_at_ = 0, fade_at_ = -1, chg_at_ = -1, close_at_ = -1, back_at_ = -1, held_at_ = -1,
            num_shift_ = 0, cross_at_ = -1;
    fx back_from_ = kOne, scrim_from_ = kOne, close_from_ = kOne, close_scrim_ = kOne, num_from_ = kOne, close_k_ = kOne,
       close_size_ = kOne;
    bool at_rest_ = false;
    std::string cross_artist_, cross_title_;
    int drawn_look_[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    bool num_dirty_ = true;
    Rect num_rect_;
    fx drawn_pop_ = -1;
    bool held_ = false, own_ = false, drop_ = false, row_ = false, seeking_ = false, exit_lit_ = false, queue_ = false;
    Drag drag_ = Drag::Tap;
    int hit_ = None, from_ = 0, to_ = 0, keep_ = -1, label_px_ = 0, step_ = (int)(tok::volume_stepDefault >> kFxBits);
    fx x0_ = 0, y0_ = 0, ax_ = 0, ay_ = 0, ly_ = 0, queue_y_ = -1;
    int64_t seek_to_ = 0;
    Pos at_;
    std::string readout_;
    bool tilt_ = false, built_ = false, row_playing_ = false, row_seeking_ = false, plate_moved_ = false,
         plated_ = false, near_drawn_ = false;
    double rx_ = 0, ry_ = 0, depth_ = 0;
    int64_t up_at_ = -1, moved_at_ = 0;
    fx tx_ = 0, ty_ = 0, drawn_tx_ = 0, drawn_ty_ = 0, drawn_k_ = 0;
    Rect box_[kBoxes], row_ink_[2], read_, part_[3];
    Coverage glow_[3];
    int press_ = None, row_press_ = None;
    int64_t press_up_ = -1;
    int corner_[kBoxes * 8] = {};
    std::string row_readout_, box_artist_, box_title_;
};

class AdjustHud {
public:
    struct Col {
        std::string num, label;
        bool up = true, down = true;
        bool operator==(const Col& o) const { return num == o.num && label == o.label && up == o.up && down == o.down; }
    };
    enum Hit { None = -1, Exit, Up0, Down0, Up1, Down1 };

    void open(int64_t now, std::vector<Col> cols, const std::string& line1, const std::string& line2);
    void set(int64_t now, std::vector<Col> cols, const std::string& line2);
    void hide(int64_t now)
    {
        if (hide_at_ < 0)
            hide_at_ = now;
    }
    void drop()
    {
        shown_ = false;
        hide_at_ = -1;
    }
    bool shown() const { return shown_; }
    bool closing() const { return hide_at_ >= 0; }
    bool visible(int64_t now) const { return shown_ && (hide_at_ < 0 || now < hide_at_ + fade_ms()); }
    bool moving(int64_t now) const;
    int hit(fx x, fx y) const;
    int number_at(fx x, fx y) const;
    Rect number_rect(int i) const { return i >= 0 && i < 2 ? num_rect_[i] : Rect{}; }
    void center(int h, fx& x, fx& y) const;
    const std::vector<Col>& cols() const { return cols_; }
    const std::string& line2() const { return line2_; }
    void invalidate() { dirty_ = true; }
    void compose(Fonts& f, GlyphCache& g, const Surface& page, const Surface& layer, uint8_t* mask, const Surface& dst,
                 int64_t now);

private:
    static int fade_ms();
    fx opacity(int64_t now) const;
    int col_x(int i) const;
    bool shown_ = false, dirty_ = true;
    std::vector<Col> cols_;
    std::string line1_, line2_;
    int64_t in_at_ = 0, hide_at_ = -1, pop_at_[2] = {-1, -1};
    Rect num_rect_[2];
};

class Toast {
public:
    enum Place { Top, Bottom };
    struct Look {
        Rect strip, clip;
        int y = 0, h = 0, scrim = 0;
        fx a = 0, zoom = kOne;
    };
    void show(int64_t now, const std::string& artist, const std::string& title, bool track = false);
    bool placed() const { return placed_; }
    bool bottom() const { return placed_ && place_ == Bottom; }
    void set_place(Place p, int64_t now);
    void to_top(int64_t now);
    void set_nav(bool on) { nav_ = on; }
    void hide()
    {
        phase_ = Hidden;
        reset();
    }
    bool visible(int64_t now) const { return phase(now) != Hidden; }
    bool showing(int64_t now) const
    {
        Phase p = phase(now);
        return p == In || p == Hold;
    }
    const std::string& artist() const { return swap_at_ >= 0 ? next_artist_ : artist_; }
    const std::string& title() const { return swap_at_ >= 0 ? next_title_ : title_; }
    bool track() const { return swap_at_ >= 0 ? next_track_ : track_; }
    uint32_t shows() const { return shows_; }
    bool hit(int64_t now, fx x, fx y) const;
    bool target() const { return target_; }
    bool pressed() const { return press_.held(); }
    void down(int64_t now)
    {
        press_.down(now);
        target_ = true;
    }
    bool tap(int64_t now)
    {
        press_.tap(now);
        bool t = target_;
        target_ = false;
        return t;
    }
    void cancel(int64_t now)
    {
        press_.cancel(now);
        target_ = false;
    }
    void reset()
    {
        press_.reset();
        target_ = false;
    }
    bool moving(int64_t now) const;
    int64_t wake_at(int64_t now) const;
    Rect rect() const;
    void draw(Canvas& c, Fonts& f, int64_t now);
    bool look(int64_t now, Look& l);
    const std::string& line(int i) const { return i ? title_ : artist_; }
    bool text_at(Canvas& c, Fonts& f, int y, Rect clip, fx a, fx zoom) const;
    static int height_max();
    static void warm(const std::string& artist, const std::string& title, std::vector<WarmText>& out);

private:
    enum Phase { Hidden, In, Hold, Out };
    Phase phase(int64_t now) const;
    fx eased(int64_t now) const;
    fx text_alpha(int64_t now) const;
    int in_ms() const;
    fx surface() const;
    int surface_px() const;
    int strip_y(fx e) const;

    Phase phase_ = Hidden;
    fx drawn_e_ = -1, drawn_ta_ = -1;
    int64_t in_at_ = 0;
    fx in_from_ = 0;
    int64_t hold_at_ = 0;
    int64_t swap_at_ = -1;
    std::string artist_, title_, next_artist_, next_title_;
    bool track_ = false, next_track_ = false, target_ = false;
    bool placed_ = false, nav_ = false;
    Place place_ = Top;
    uint32_t shows_ = 0;
    Press press_;
};

class NavStrip {
public:
    void show(int64_t now);
    void hide(int64_t now);
    void drop() { phase_ = Hidden; }
    bool visible(int64_t now) const { return phase(now) != Hidden; }
    bool moving(int64_t now) const;
    int64_t wake_at(int64_t now) const;
    fx eased(int64_t now) const;
    void drawn(int64_t now) { drawn_ = eased(now); }

private:
    enum Phase { Hidden, In, Hold, Out };
    Phase phase(int64_t now) const;

    Phase phase_ = Hidden;
    int64_t in_at_ = 0, hold_at_ = 0, out_at_ = -1;
    fx in_from_ = 0, out_from_ = kOne, drawn_ = -1;
};

}
