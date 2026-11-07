#pragma once
#include <string>
#include <vector>

#include "../core/event.h"
#include "../core/fx.h"
#include "../core/gesture.h"
#include "../gfx/canvas.h"
#include "../gfx/font.h"
#include "../gfx/glyph.h"
#include "list.h"
#include "roles.h"
#include "scrollbar.h"

namespace fd {

bool draw_clipped(Canvas& c, Fonts& f, const TextStyle& st, const std::string& s, int x64, int base, int a, Rect clip,
                  const Zoom& z = {}, uint32_t color = kWhite);

struct ChromePart {
    int dx64 = 0, dy = 0;
    fx alpha = kOne;
    bool hidden = false;
};

class Chrome {
public:
    void measure(Fonts& f, const char* word, std::vector<const char*> pivots);
    int pivots() const { return (int)names_.size(); }
    const char* name(int i) const { return names_[i]; }
    int active() const { return active_; }
    void set_active(int i)
    {
        active_ = from_active_ = i;
        at_ = -1000000;
    }
    void switch_to(int i, int64_t now);
    bool moving(int64_t now) const;
    void land() { at_ = -1000000; }
    int header_w64() const { return header_w64_; }
    int pivot_x64(int i) const;
    bool draw(Canvas& c, Fonts& f, fx fly, int64_t now) const;
    int pivot_hit(fx x, fx y) const;
    bool header_hit(fx x, fx y) const;
    void set_header_part(const ChromePart& p) { header_part_ = p; }
    void set_pivot_part(int i, const ChromePart& p)
    {
        if (i >= 0 && i < (int)pivot_parts_.size())
            pivot_parts_[(size_t)i] = p;
    }
    void clear_parts();

private:
    std::string word_;
    std::vector<const char*> names_;
    ChromePart header_part_;
    std::vector<ChromePart> pivot_parts_;
    std::vector<int> w64_[2], std64_[2];
    std::vector<std::pair<int, int>> ink_[2], std_ink_[2];
    fx gap(int k) const;
    int pos64(int i) const;
    int rest_row64() const;
    int row64(int64_t now) const;
    int std_pos64(int i) const;
    int std_row64(int64_t now) const;
    int header_x64(int std_row) const;

    int header_w64_ = 0;
    int active_ = 0, from_active_ = 0, from_row_ = 0, from_std_ = 0;
    int64_t at_ = -1000000;
};

class PivotMotion {
public:
    void begin(int64_t now, int dir);
    void end() { at_ = -1; }
    bool active(int64_t now) const;
    bool entering(int64_t now) const;
    bool lands(const Event& e, int64_t now) const
    {
        return active(now) && e.kind != Ev::TouchMove && e.kind != Ev::TouchUp;
    }
    bool needs_in(int64_t now) const { return entering(now) && !in_ready_; }
    void set_in_ready() { in_ready_ = true; }
    void clear_in_ready() { in_ready_ = false; }
    fx alpha(int64_t now, int row) const;
    Thumb thumb(const Thumb& out, const Thumb& in, int64_t now) const;
    void compose(const Surface& layer, Canvas& c, Rect area, pos64 top, fx pitch, pos64 off, int first, int last,
                 int64_t now) const;
    void compose(const Surface& layer, Canvas& c, Rect area, pos64 top, const std::vector<fx>& tops,
                 const std::vector<fx>& heights, pos64 off, int first, int last, int64_t now) const;

private:
    void blend_row(const Surface& layer, Canvas& c, Rect clip, pos64 y, fx h, int row, int64_t now) const;

    int64_t at_ = -1;
    int dir_ = 1;
    bool in_ready_ = false;
};

int row_title_x64(Fonts& f, const Row& r, bool ring);
bool draw_list_row(Canvas& c, Fonts& f, GlyphCache* g, const Row& r, int top, fx primary_alpha, bool focused = false,
                   fx press = kOne);
void warm_list_row(const Row& r, std::vector<WarmText>& out);
void draw_focus_bar(Canvas& c, int y, fx pitch);
std::vector<std::string> settings_value_lines(Fonts& f, const Role& role, const std::string& title,
                                              const std::string& value);
std::vector<std::string> settings_title_lines(Fonts& f, const Role& role, const std::string& title,
                                              const std::vector<std::string>& values);
const std::string& settings_wide_line(Fonts& f, const Role& role, const std::vector<std::string>& lines);
bool draw_settings_value(Canvas& c, Fonts& f, const Role& role, const std::string& title, const std::string& value,
                         int top, uint32_t rgb, int a256, int& clip_x, bool split = true, int h = -1,
                         bool edit = false);
bool draw_settings_title(Canvas& c, Fonts& f, const Role& role, const std::vector<std::string>& lines, int top, int a256,
                         int clip_x, int h = -1);
bool draw_settings_row(Canvas& c, Fonts& f, const Role& role, const std::string& title, const std::string& value, int top,
                       fx focus, const std::vector<std::string>& words = {});
Zoom tile_zoom(Rect t, fx press);
void draw_art_tile(Canvas& c, const Image* img, Rect t, int border, const Zoom& z);
void draw_letter_tile(Canvas& c, Rect t, int border, int border_a, const Zoom& z);

enum class PlayState { None, Playing, Paused };
struct StatusBox {
    fx top, height, right, gap, left, battery_w, battery_h, text;
};
inline constexpr StatusBox kStatus{tok::statusTop, tok::statusHeight, tok::edge, tok::statusGap,
                                   tok::backRingX + tok::backRing, 0, 0, tok::type_time_size};
inline constexpr StatusBox kNpStatus{tok::npStatusTop, tok::npStatusHeight, tok::npStatusRight, tok::npStatusGap,
                                     tok::npBackRingX + tok::npBackRing, tok::npStatusBatteryW, tok::npStatusBatteryH,
                                     tok::npStatusText};
Rect status_rect(const StatusBox& box = kStatus);
bool draw_status(Canvas& c, Fonts& f, GlyphCache& g, int battery, PlayState st, int a256, bool bluetooth = false,
                 const std::string& clock = "", bool percent = false, int wifi = -1, int volume = -1,
                 const StatusBox& box = kStatus);
void status_icons(std::vector<Icon>& out, const StatusBox& box = kStatus);
void draw_wifi(Canvas& c, GlyphCache& g, int size, int cx, int cy, int level, int a256, fx press = kOne);
void wifi_icons(std::vector<Icon>& out, int size);
void draw_mic(Canvas& c, GlyphCache& g, int size, int cx, int cy, int a256, fx scale = kOne);
void mic_icons(std::vector<Icon>& out, int size);

}
