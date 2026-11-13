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
#include "setd_key.h"
#include "sysinfo.h"
#include "theme.h"
#include "toast_clock.h"
#include "zones.h"

namespace fd {

void draw_page_ground(Canvas& c, const Theme& th);
void draw_page_chrome(Canvas& c, Fonts& f, const char* word, const char* pivot, const Theme& th);
void draw_page_header(Canvas& c, Fonts& f, const char* word, fx band, const Theme& th);

struct SheetView {
    const char* title = "";
    const char* lead = "";
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
    bool mixed = false;
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

struct ToastView {
    fx e = 0;
    fx text = kOne;
    std::string line1, line2;
    int strip_h = 0;
};

bool draw_toast(Canvas& c, Fonts& f, InkLayer& ink, const ToastView& v, const Theme& th, Rect* strip = nullptr);
std::string setd_fail_text(const std::string& error, const std::string& text);

enum class RowKind { Sheet, Live, Info, Push, RowSheet, Action };

struct SettingsRowSpec {
    const char* pivot = "";
    const char* title = "";
    RowKind kind = RowKind::Sheet;
    const char* id = "";
    std::vector<const char*> words;
    std::vector<const char*> values;
    bool bounded = false;
    const char* lead = "";
    bool mixed = false;
    bool setd = false;
    SetdType type = SetdType::S;
};

struct SettingsRowState {
    bool shown = false;
    std::string value;
    std::string text;
    std::vector<InfoLine> lines;
};

struct SettingsRowView {
    std::string title, value;
    RowKind kind = RowKind::Sheet;
};

struct RowSheetView {
    const char* title = "";
    std::vector<InfoLine> lines;
    bool mixed = false;
    fx progress = 0;
    bool open = false;
};

constexpr const char* kThemeRow = "frontend.theme";
constexpr const char* kZoneRow = "system.timezone";
constexpr const char* kBrightnessRow = "display.brightness";
constexpr const char* kVolumeRow = "sound.volume";
constexpr const char* kRadioRow = "network.wifi";
constexpr const char* kBluetoothRow = "network.bluetooth";
constexpr const char* kBatteryRow = "power.battery";
constexpr const char* kStorageRow = "system.storage";
constexpr const char* kAboutRow = "system.about";
constexpr int kSettingsPivots = 7;
extern const char* const kSettingsPivotWords[kSettingsPivots];
std::vector<SettingsRowSpec> settings_rows();
SettingsRowState volume_row(bool ok, int pct, bool muted);
SettingsRowState radio_row(bool up, bool on);
SettingsRowState info_row(const std::string& text, std::vector<InfoLine> lines = {});

struct SettingsView {
    int focus = 0;
    fx bar = -kOne;
    bool light = false;
    bool zone_row = false;
    std::string zone;
    SheetView sheet;
    std::vector<const char*> pivots;
    int pivot = 0;
    std::vector<SettingsRowView> rows;
    int from_pivot = -1;
    int from_focus = 0;
    std::vector<SettingsRowView> from_rows;
    int dir = 1;
    int64_t switch_at = 0;
    int64_t now = 0;
    RowSheetView rowsheet;
    ToastView toast;
};

int64_t settings_switch_ms();
void settings_rows_switch(const Surface& from, const Surface& to, const Surface& dst, Rect area, int64_t t, int dir,
                          uint32_t ground);
std::vector<std::string> row_sheet_wrap(Fonts& f, const TextStyle& st, const std::string& s, int first64, int next64);
void draw_row_sheet(Canvas& c, Fonts& f, const RowSheetView& s, const Theme& th);
void draw_settings(Canvas& c, Fonts& f, InkLayer& ink, const SettingsView& v, const Theme& th);
void draw_page_pivots(Canvas& c, Fonts& f, InkLayer& ink, const char* word, const SettingsView& v, const Theme& th);

class SettingsPage {
public:
    static constexpr int kSheetRows = 6;

    std::function<void(bool light)> on_theme;
    std::function<void(const std::string& id)> on_zone;
    std::function<void(const std::string& row, const std::string& value)> on_pick;
    std::function<void(const std::string& row, const std::string& value)> on_preview;
    std::function<void(const std::string& row)> on_open;
    std::function<void(const std::string& row)> on_close;
    std::function<void(const std::string& row)> on_restore;
    std::function<void(const std::string& row)> on_push;
    std::function<void(const std::string& row)> on_action;

    SettingsPage() : table_(settings_rows()) { size_states(); }
    SettingsPage(const SettingsPage&) = delete;
    SettingsPage& operator=(const SettingsPage&) = delete;

    void set_table(std::vector<SettingsRowSpec> t);
    void reset(bool light);
    void set_light(bool light) { light_ = light; }
    void set_zones(Zones z);
    void set_zone(const std::string& id);
    void set_zone_missing(int64_t now);
    void set_row(const std::string& id, const SettingsRowState& st, int64_t now);
    void close_row(const std::string& id, int64_t now);
    void set_sheet_rows(int rows) { rows_ = rows < 1 ? 1 : rows; }
    void toast(int64_t now, const std::string& line1, const std::string& line2) { toast_.show(now, line1, line2, false); }
    void refused(const std::string& id, const std::string& error, const std::string& text, int64_t now);
    std::string title(const std::string& id) const;
    bool action(PadAction a, bool repeat, int64_t now);
    bool animating(int64_t now) const;
    int64_t deadline(int64_t now) const;
    const char* pivot() const { return kSettingsPivotWords[pivot_]; }
    int focus_y() const;
    void land()
    {
        sheet_at_ = -kFar;
        bar_at_ = -kFar;
        page_bar_at_ = -kFar;
        switch_at_ = -kFar;
        rowsheet_at_ = -kFar;
    }
    SettingsView view(int64_t now) const;

private:
    enum class Kind { Theme, Region, Zone, Words };
    static constexpr int64_t kFar = 1000000000;

    int count() const { return (int)items_.size(); }
    int mark() const;
    void size_states();
    bool shown(int row) const;
    int pivot_of(int row) const;
    std::vector<int> rows_of(int pivot) const;
    std::vector<int> pivots_shown() const;
    std::string row_value(int row) const;
    std::vector<SettingsRowView> row_views(int pivot) const;
    int row_at(int pivot, int focus) const;
    int find_row(const std::string& id) const;
    void shut_sheets(int row, int64_t now);
    void row_going(int row, int64_t now);
    void row_coming(int row, bool had);
    void keep_pivot();
    void switch_pivot(int dir, int64_t now);
    void zone_changed();
    void items();
    void fit();
    void open(Kind k, int focus, int64_t now);
    void open_words(int row, int64_t now);
    void move(int to, int64_t now, bool wrap);
    void accept(int64_t now);
    void close_sheet(bool restore, int64_t now);
    void pick(const std::string& id, int64_t now);
    fx bar_at(int64_t now) const;
    fx page_bar(int64_t now) const;

    std::vector<SettingsRowSpec> table_;
    std::vector<SettingsRowState> states_;
    int pivot_ = 0;
    int from_pivot_ = 0;
    int dir_ = 1;
    int64_t switch_at_ = -kFar;
    int pfocus_[kSettingsPivots] = {0, 0, 0, 0, 0, 0, 0};
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
    int sheet_row_ = -1;
    int region_ = 0;
    int sheet_focus_ = 0;
    bool moved_ = false;
    int first_ = 0;
    int back_focus_ = 0;
    int back_first_ = 0;
    int rows_ = kSheetRows;
    std::vector<const char*> items_;
    int64_t sheet_at_ = -kFar;
    fx bar_from_ = 0;
    int64_t bar_at_ = -kFar;
    bool rowsheet_ = false;
    int rowsheet_row_ = -1;
    int64_t rowsheet_at_ = -kFar;
    ToastClock toast_;
};

int sheet_rows(Fonts& f, int want = SettingsPage::kSheetRows);
bool sync_bt_row(SettingsPage& p, std::string& last, const std::string& sub, int64_t now);

}
