#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "../core/tween.h"
#include "../gfx/canvas.h"
#include "../gfx/font.h"
#include "../gfx/glyph.h"
#include "../ui/list.h"
#include "../ui/rows.h"
#include "glide.h"
#include "ink.h"
#include "input_pad.h"
#include "screen_selector.h"
#include "store.h"
#include "toast_clock.h"

namespace fd {

constexpr int kStoreShotSide = 336;

constexpr int kPortArtX = 32;
constexpr int kPortArtTop = 144;
constexpr int kPortColX = 512;
constexpr int kPortColRight = 928;
constexpr int kPortColBottom = 720;
constexpr int kPortLastBase = 688;
constexpr int kPortPorterBase = 120;
constexpr int kPortGenresBase = 168;
constexpr int kPortTitleBase = 232;
constexpr int kPortTitleLines = 4;
constexpr int kPortSizeStep = 48;
constexpr int kPortBodyPitch = 36;
constexpr int kPortScrollStep = 36;
constexpr int kPortRowsTop = 480;
constexpr int kPortRowClip = 408;
constexpr fx kPortDotsDrop = fx_int(21);
constexpr int kPortDialogClip = 928;
constexpr int kPortDoneHoldMs = 250;
constexpr int kProgressMs = 250;

struct StoreOverlay {
    bool shown = false;
    std::string title;
    std::vector<std::string> items;
    int focus = 0;
    Tween fade, scale, bar;
};

class PortPage {
public:
    enum class Act { None, Pop, Install, Update, Remove, Play, Stop };
    enum class Kind { Install, Play, Update, Remove };

    PortPage(Fonts& f, GlyphCache& g);
    PortPage(const PortPage&) = delete;
    PortPage& operator=(const PortPage&) = delete;

    void open(const StoreItem& it, bool online, bool working);
    void set_item(const StoreItem& it, bool online, int64_t now);
    void set_working(bool on, HelperCmd cmd, bool queued, int64_t now);
    HelperCmd cmd() const { return cmd_; }
    bool toasting(int64_t now) const { return toast_.phase(now) != 0; }
    bool dialog_shown() const { return dialog_.shown; }
    const std::string& words() const { return words_; }
    bool arcing() const { return working_ && !no_total_ && arc_ > 0; }
    int focus() const { return column_focus() ? -1 : focus_; }
    void progress(const HelperProgress& p, int64_t now);
    void finished(bool done, int64_t now);
    void set_art(const Art& art) { art_ = art; }
    Act action(PadAction a, bool repeat, int64_t now);
    void draw(Canvas& c, InkLayer& ink, int64_t now);
    bool animating(int64_t now) const;
    int64_t deadline(int64_t now) const;
    void tick(int64_t now);
    void toast(int64_t now, const std::string& l1, const std::string& l2) { toast_.show(now, l1, l2, false); }
    const StoreItem& item() const { return item_; }
    int focus_y() const;
    int action_count(int64_t now) const { return (int)(now < change_at_ ? was_ : acts_).size(); }
    std::vector<Rect> line_rects(int64_t now) const;

private:
    struct Line {
        const Role* role = nullptr;
        std::string text;
        int base = 0;
        fx alpha = kOne;
    };
    struct Action {
        Kind kind = Kind::Install;
        const char* word = "";
        const char* glyph = "";
    };

    std::vector<Action> actions() const;
    void layout();
    void reflow(const std::vector<Action>& was, int64_t now);
    bool live(const Action& a) const;
    void draw_dots(Canvas& k, const Row& r, int top, int64_t now);
    int scroll_max() const;
    bool column_focus() const { return column_ || (acts_.empty() && land_focus_ < 0); }
    void dialog(bool stop, int64_t now);
    void dialog_action(PadAction a, bool repeat, int64_t now, Act& out);
    Row row_of(const Action& a) const;
    void draw_row(Canvas& k, const Action& a, int top, fx alpha, bool focused, int64_t now);
    void draw_rows(Canvas& c, InkLayer& ink, int64_t now, fx pane);
    void draw_column(Canvas& c, InkLayer& ink, int64_t now, fx pane);
    void draw_dialog(Canvas& c, InkLayer& ink, int64_t now);

    Fonts& fonts_;
    GlyphCache& glyphs_;
    StoreItem item_;
    bool online_ = true, working_ = false;
    HelperCmd cmd_ = HelperCmd::Install;
    std::string words_;
    int arc_ = 0;
    int64_t hold_until_ = -1000000;
    int64_t spin0_ = 0;
    bool no_total_ = false;
    HelperProgress pending_;
    bool has_pending_ = false;
    int64_t shown_at_ = -1000000;
    std::vector<Action> acts_, was_;
    int64_t change_at_ = -1000000;
    int focus_ = 0;
    int land_focus_ = -1;
    Tween bar_;
    bool column_ = false;
    Glide scroll_;
    std::vector<Line> lines_;
    Art art_;
    StoreOverlay dialog_;
    bool stop_dialog_ = false;
    ToastClock toast_;
};

}
