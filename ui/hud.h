#pragma once
#include <string>
#include <vector>

#include "../core/event.h"
#include "../core/fx.h"
#include "../core/press.h"
#include "../gfx/canvas.h"
#include "../gfx/font.h"
#include "../gfx/glyph.h"

namespace fd {

class VolumeHud {
public:
    enum Hit { None = -1, Exit, Plus, Minus, Prev, Ring, Next };
    enum class Act : uint8_t { None, Pass, Plus, Minus, Prev, Ring, Next, Volume, Seek, Draw };
    struct Pos {
        bool on = false;
        int64_t ms = 0, dur = 0;
        uint32_t track = 0;
    };

    void show(int64_t now, int percent, bool playing, const std::string& artist, const std::string& title);
    void dismiss()
    {
        fade_at_ = -1;
        shown_ = false;
        drop_touch();
    }
    void invalidate() { num_dirty_ = true; }
    void set_track(bool playing, const std::string& artist, const std::string& title)
    {
        playing_ = playing;
        artist_ = artist;
        title_ = title;
    }
    void hide(int64_t now)
    {
        if (held_ || fade_at_ > now)
            fade_at_ = now;
        drop_touch();
    }
    bool visible(int64_t now) const { return shown_ && (fade_at_ < 0 || now < fade_at_ + fade_ms()); }
    bool closing(int64_t now) const { return fade_at_ >= 0 && now >= fade_at_; }
    bool moving(int64_t now) const;
    int64_t wake_at() const { return fade_at_; }
    fx opacity(int64_t now) const;
    void compose(Fonts& f, GlyphCache& g, const Surface& page, const Surface& layer, uint8_t* mask, const Surface& dst,
                 int64_t now);
    static void warm(const std::string& artist, const std::string& title, std::vector<WarmText>& out);
    static void icons(std::vector<Icon>& out);
    int hit(fx x, fx y) const;
    Act event(const Event& in, int64_t now, int percent, const Pos& at);
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
    bool owns() const { return own_; }
    bool drops() const { return drop_; }
    void touch() { own_ = drop_ = false; }
    void drop_touch();
    void cancel(int64_t now);
    int keep() const { return own_ && drag_ == Drag::Volume ? keep_ : -1; }
#ifndef FD_ALSA
    void shot_seek(int64_t to, int64_t dur);
#endif

private:
    enum class Drag : uint8_t { Tap, Volume, Seek, Off };
    static int fade_ms();
    static TextStyle number_style(Fonts& f, const std::string& num);
    static int pause_px();
    fx pop(int64_t now) const;
    bool shown_ = false;
    int percent_ = 0;
    bool playing_ = false;
    std::string artist_, title_;
    int64_t in_at_ = 0, pop_at_ = 0, fade_at_ = -1;
    fx in_from_ = 0;
    bool num_dirty_ = true;
    Rect num_rect_;
    int drawn_a_ = -1;
    fx drawn_pop_ = -1;
    bool held_ = false, own_ = false, drop_ = false, row_ = false, seeking_ = false;
    Drag drag_ = Drag::Tap;
    int hit_ = None, from_ = 0, to_ = 0, keep_ = -1, label_px_ = 0;
    fx x0_ = 0, y0_ = 0, ax_ = 0, ay_ = 0, ly_ = 0;
    int64_t seek_to_ = 0;
    Pos at_;
    std::string readout_;
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
    void show(int64_t now, const std::string& artist, const std::string& title, bool track = false);
    void hide()
    {
        phase_ = Hidden;
        reset();
    }
    bool visible(int64_t now) const { return phase(now) != Hidden; }
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
    static void warm(const std::string& artist, const std::string& title, std::vector<WarmText>& out);

private:
    enum Phase { Hidden, In, Hold, Out };
    Phase phase(int64_t now) const;
    fx eased(int64_t now) const;
    fx text_alpha(int64_t now) const;

    Phase phase_ = Hidden;
    fx drawn_e_ = -1, drawn_ta_ = -1;
    int64_t in_at_ = 0;
    fx in_from_ = 0;
    int64_t hold_at_ = 0;
    int64_t swap_at_ = -1;
    std::string artist_, title_, next_artist_, next_title_;
    bool track_ = false, next_track_ = false, target_ = false;
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
