#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "../core/fx.h"
#include "../gfx/font.h"
#include "../gfx/pixel.h"
#include "ink.h"
#include "input_pad.h"
#include "theme.h"
#include "zones.h"

namespace fd {

void draw_page_ground(Canvas& c, const Theme& th);
void draw_page_chrome(Canvas& c, Fonts& f, const char* word, const char* pivot, const Theme& th);

struct SheetView {
    const char* title = "";
    const char* const* items = nullptr;
    int count = 0;
    int focus = 0;
    fx bar = -kOne;
    fx progress = 0;
    int first = 0;
    int mark = -1;
    int rows = 0;
    bool open = false;
    int level = 0;
    bool opaque = false;
};

struct SheetGeom {
    int top = 0;
    int rows_top = 0;
    int pitch = 0;
    int shown = 0;
};

SheetGeom sheet_geom(Fonts& f, const SheetView& s, Rect sr);
void draw_value_sheet(Canvas& c, Fonts& f, const SheetView& s, const Theme& th);
int value_sheet_rows(Fonts& f);
int settings_row_y(int row);

struct SettingsView {
    int focus = 0;
    fx bar = -kOne;
    bool light = false;
    bool zone_row = false;
    std::string zone;
    SheetView sheet;
};

void draw_settings(Canvas& c, Fonts& f, InkLayer& ink, const SettingsView& v, const Theme& th);

class SettingsPage {
public:
    static constexpr int kSheetRows = 6;

    std::function<void(bool light)> on_theme;
    std::function<void(const std::string& id)> on_zone;

    SettingsPage() = default;
    SettingsPage(const SettingsPage&) = delete;
    SettingsPage& operator=(const SettingsPage&) = delete;

    void reset(bool light);
    void set_light(bool light) { light_ = light; }
    void set_zones(Zones z);
    void set_zone(const std::string& id);
    void set_zone_missing(int64_t now);
    void set_sheet_rows(int rows) { rows_ = rows < 1 ? 1 : rows; }
    bool action(PadAction a, bool repeat, int64_t now);
    bool animating(int64_t now) const;
    void land()
    {
        sheet_at_ = -kFar;
        bar_at_ = -kFar;
        page_bar_at_ = -kFar;
    }
    SettingsView view(int64_t now) const;

private:
    enum class Kind { Theme, Region, Zone };
    static constexpr int64_t kFar = 1000000000;

    int count() const { return (int)items_.size(); }
    int mark() const;
    void zone_changed();
    void items();
    void fit();
    void open(Kind k, int focus, int64_t now);
    void move(int to, int64_t now, bool wrap);
    void accept(int64_t now);
    void pick(const std::string& id, int64_t now);
    fx bar_at(int64_t now) const;
    fx page_bar(int64_t now) const;

    int focus_ = 0;
    fx page_bar_from_ = 0;
    int64_t page_bar_at_ = -kFar;
    bool light_ = false;
    Zones zones_;
    std::string zone_;
    std::string zone_words_;
    ZoneSpot spot_;
    bool zone_row_ = false;
    bool sheet_ = false;
    Kind kind_ = Kind::Theme;
    int region_ = 0;
    int sheet_focus_ = 0;
    int first_ = 0;
    int back_focus_ = 0;
    int back_first_ = 0;
    int rows_ = kSheetRows;
    std::vector<const char*> items_;
    int64_t sheet_at_ = -kFar;
    fx bar_from_ = 0;
    int64_t bar_at_ = -kFar;
};

int sheet_rows(Fonts& f, int want = SettingsPage::kSheetRows);

}
