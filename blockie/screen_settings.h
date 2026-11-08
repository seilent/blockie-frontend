#pragma once
#include <cstdint>
#include <functional>

#include "../core/fx.h"
#include "../gfx/font.h"
#include "../gfx/pixel.h"
#include "ink.h"
#include "input_pad.h"
#include "theme.h"

namespace fd {

struct PageChrome {
    int header_dy = 0;
    fx header_alpha = kOne;
};

void draw_page_ground(Canvas& c, const Theme& th);
void draw_page_chrome(Canvas& c, Fonts& f, const char* word, const char* pivot, const PageChrome& ch, const Theme& th);

struct SheetView {
    const char* title = "";
    const char* const* items = nullptr;
    int count = 0;
    int focus = 0;
    fx bar = -kOne;
    fx progress = 0;
};

void draw_value_sheet(Canvas& c, Fonts& f, const SheetView& s, const Theme& th);

struct SettingsView {
    int focus = 0;
    bool light = false;
    fx sheet = 0;
    int sheet_focus = 0;
    fx sheet_bar = -kOne;
    PageChrome chrome;
};

void draw_settings(Canvas& c, Fonts& f, InkLayer& ink, const SettingsView& v, const Theme& th);

class SettingsPage {
public:
    std::function<void(bool light)> on_theme;

    void reset(bool light);
    void set_light(bool light) { light_ = light; }
    bool action(PadAction a, bool repeat, int64_t now);
    bool animating(int64_t now) const;
    void land()
    {
        sheet_at_ = -kFar;
        bar_at_ = -kFar;
    }
    SettingsView view(int64_t now) const;

private:
    static constexpr int64_t kFar = 1000000000;

    int focus_ = 0;
    bool light_ = false;
    bool sheet_ = false;
    int sheet_focus_ = 0;
    int64_t sheet_at_ = -kFar;
    fx bar_from_ = 0;
    int64_t bar_at_ = -kFar;

    fx bar_at(int64_t now) const;
};

}
