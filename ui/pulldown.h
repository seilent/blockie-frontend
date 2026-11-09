#pragma once
#include <string>

#include "../core/press.h"
#include "../gfx/canvas.h"
#include "../gfx/font.h"
#include "../gfx/glyph.h"

namespace fd {

class PullDown {
public:
    enum Item { Bluetooth, Wifi, Gain, Sleep, Dimmer, Brighter, Prev, Play, Next, Track, kItems };

    struct Info {
        bool bt = false;
        int wifi = -1;
        std::string gain, sleep;
        int brightness = 0;
        bool dimmer = true, brighter = true;
        bool playing = false, np = false;
        bool has_bt = true, has_light = true;
        std::string artist, title;
        bool operator==(const Info& o) const
        {
            return bt == o.bt && wifi == o.wifi && gain == o.gain && sleep == o.sleep && brightness == o.brightness &&
                   dimmer == o.dimmer && brighter == o.brighter && playing == o.playing && np == o.np &&
                   has_bt == o.has_bt && has_light == o.has_light && artist == o.artist && title == o.title;
        }
    };

    void open(int64_t now);
    void close(int64_t now);
    void dismiss() { shown_ = false; }
    void set(const Info& i) { info_ = i; }
    const Info& info() const { return info_; }
    bool visible(int64_t now) const { return shown_ && (fade_at_ < 0 || now < fade_at_ + fade_ms()); }
    bool closing() const { return fade_at_ >= 0; }
    bool moving(int64_t now) const;
    fx opacity(int64_t now) const;
    int hit(fx x, fx y) const;
    bool center(int item, fx& x, fx& y) const;
    void touch_down(int item, int64_t now) { press_.down(item, now); }
    void tap(int64_t now) { press_.tap(now); }
    void cancel(int64_t now) { press_.cancel(now); }
    void reset_presses() { press_.reset(); }
    bool pressing(int64_t now) const { return press_.stale(now); }
    int64_t press_due(int64_t now) const { return press_.due(now); }
    Rect damage(int64_t now) const;
    int64_t next_change(int64_t now) const;
    void compose(Fonts& f, GlyphCache& g, const Surface& page, const Surface& dst, int64_t now, Rect d = kScreen);
    static void icons(std::vector<Icon>& out);
    static void warm_track(const std::string& artist, const std::string& title, bool buckets, std::vector<WarmText>& out);
#ifndef FD_ALSA
    void set_box_inset(int px) { inset_ = px; }
#endif

private:
    static int fade_ms();
    bool has(int item) const;
    Rect cell(int item) const;
    Rect box_of(int item) const;
    Rect value_box() const;
    bool clip(Canvas& c, Rect b, Rect d) const;
    bool value_changed(int item) const;
    int slot(int item) const;
    fx grid_top() const;
    fx ring_y() const;
    void draw_tile(Canvas& c, Fonts& f, GlyphCache& g, int item, int a, int64_t now);
    void draw_player(Canvas& c, Fonts& f, GlyphCache& g, int a, int64_t now, Rect d);

    Info info_, drawn_;
    bool shown_ = false;
    int64_t in_at_ = 0, fade_at_ = -1;
    fx in_from_ = 0;
    int drawn_a_ = -1;
    int drawn_step_[kItems] = {};
    int inset_ = 0;
    PressSlots press_;
};

}
