#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "../core/tween.h"
#include "../gfx/canvas.h"
#include "../gfx/font.h"
#include "../gfx/glyph.h"
#include "../ui/list.h"
#include "../ui/rows.h"
#include "bt_model.h"
#include "ink.h"
#include "input_pad.h"
#include "screen_settings.h"

namespace fd {

inline const char* bluetooth_word() { return "bluetooth"; }

class BtPage {
public:
    enum class Act { None, Pop };

    BtPage(Fonts& f, GlyphCache& g);
    BtPage(const BtPage&) = delete;
    BtPage& operator=(const BtPage&) = delete;

    void set_send(std::function<void(const BtCmd&)> send) { send_ = std::move(send); }
    void open(int64_t now);
    void close();
    void set_model(const BtState& s, int64_t now);
    Act action(PadAction a, bool repeat, int64_t now);
    void land(int64_t now);
    void draw(Canvas& c, InkLayer& ink, int64_t now);
    bool dirty(int64_t now) const;
    bool animating(int64_t now) const;
    int64_t deadline(int64_t now) const;
    bool sheet_shown() const { return sheet_.shown; }
    bool dialog_shown() const { return dialog_.shown; }
    int focus_row() const { return list_.focus_row(); }
    std::vector<Rect> regions() const;
    const std::vector<BtRow>& rows() const { return rows_.v; }

private:
    struct Rows : ListModel {
        std::vector<BtRow> v;
        int count() const override { return (int)v.size(); }
        void fill(int row, Row& out) const override;
        uint64_t key(int row, int col) const override;
        bool find(uint64_t key, int& row, int& col) const override;
    };
    struct BtOverlay : Overlay {
        bool connected = false;
        std::string path;
    };

    void send(BtCmd::Type t, const std::string& path = "", bool yes = false);
    void accept();
    void open_sheet(const BtRow& r, int64_t now);
    void show_dialog(int64_t now);
    void hide(BtOverlay& o, int ms, int64_t now);
    void move(BtOverlay& o, int delta, int64_t now);
    void sheet_action(PadAction a, bool repeat, int64_t now);
    void dialog_action(PadAction a, bool repeat, int64_t now);
    bool visible(const BtOverlay& o, int64_t now) const;
    bool moving(int64_t at) const;
    void draw_rows(Canvas& k, int64_t now, fx bar_alpha);
    void draw_sweep(Canvas& c, int64_t now);
    void draw_sheet(Canvas& c, InkLayer& ink, int64_t now);

    Fonts& fonts_;
    GlyphCache& glyphs_;
    std::function<void(const BtCmd&)> send_;
    BtState state_;
    Rows rows_;
    ListView list_;
    BtOverlay sheet_, dialog_;
    bool dirty_ = true, retry_ = false;
    int drawn_secs_ = -1;
    int64_t drawn_at_ = -1;
};

}
