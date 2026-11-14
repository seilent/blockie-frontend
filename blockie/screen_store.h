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
#include "../ui/qr.h"
#include "../ui/rows.h"
#include "glide.h"
#include "ink.h"
#include "input_pad.h"
#include "screen_selector.h"
#include "screen_settings.h"
#include "store.h"
#include "toast_clock.h"

namespace fd {

constexpr int kStoreShotSide = 2 * kTileSide + kGridGutter;

constexpr int kPortArtX = kGridX;
constexpr int kPortArtTop = kTileFirstTop;
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
constexpr int kPortRowsTop = kTileFirstTop + 2 * kGridRowPitch;
constexpr int kPortRowX = kGridX - std_px(tok::hubEdge);
constexpr int kPortRowClip = 408;
constexpr fx kPortDotsDrop = fx_int(21);
constexpr int kPortDialogClip = 928;
constexpr const char* kLoginWord = "steam";
constexpr int kPortDoneHoldMs = 250;
constexpr int kProgressMs = 250;

Rect login_box();

class PortPage {
public:
    enum class Act { None, Pop, Install, Update, Remove, Play, Stop, SteamGet };
    enum class Kind { Install, Play, Update, Remove, SteamGet };

    PortPage(Fonts& f, GlyphCache& g);
    PortPage(const PortPage&) = delete;
    PortPage& operator=(const PortPage&) = delete;

    void open(const StoreItem& it, bool online, bool working);
    void set_item(const StoreItem& it, bool online, int64_t now);
    void set_working(bool on, HelperCmd cmd, bool queued, int64_t now);
    HelperCmd cmd() const { return cmd_; }
    bool toasting(int64_t now) const { return toast_.phase(now) != 0; }
    bool dialog_shown() const { return dialog_.shown; }
    const Overlay& dialog_box() const { return dialog_; }
    const std::string& words() const { return words_; }
    bool arcing() const { return working_ && !no_total_ && arc_ > 0; }
    int focus() const { return column_focus() ? -1 : focus_; }
    void progress(const HelperProgress& p, int64_t now);
    void finished(bool done, bool cross, int64_t now);
    void set_art(const Art& art) { art_ = art; }
    void set_qr(QrGrid q) { qr_ = std::move(q); }
    void clear_qr() { qr_ = QrGrid(); }
    bool qr_shown() const { return qr_.size > 0; }
    bool copying() const { return copying_; }
    void set_copying() { copying_ = true; }
    Act action(PadAction a, bool repeat, int64_t now);
    void draw(Canvas& c, InkLayer& ink, int64_t now);
    void draw_login(Canvas& c, InkLayer& ink, int64_t now);
    Act login_action(PadAction a, bool repeat, int64_t now);
    bool login_dialog_shown() const { return login_dialog_.shown; }
    bool login_dialog_visible(int64_t now) const { return login_dialog_.shown || login_dialog_.fade.at(now) > 0; }
    bool login_animating(int64_t now) const { return overlay_moving(login_dialog_, now); }
    void drop_login_dialog();
    bool overlays_idle(int64_t now) const;
    int64_t overlays_end() const;
    void set_login_wait(bool on) { login_wait_ = on; }
    bool login_wait() const { return login_wait_; }
    bool animating(int64_t now) const;
    int64_t deadline(int64_t now, bool dots = true) const;
    void tick(int64_t now);
    void toast(int64_t now, const std::string& l1, const std::string& l2) { toast_.show(now, l1, l2, false); }
    const StoreItem& item() const { return item_; }
    int focus_y() const;
    int action_count(int64_t now) const;
    Rect art_rect() const;
    std::vector<Rect> line_rects(int64_t now) const;
    int64_t cross_end() const { return cross_end_; }

private:
    struct Line {
        const Role* role = nullptr;
        std::string text;
        int base = 0;
        fx alpha = kOne;
        int up = 0, down = 0;
    };
    struct Action {
        Kind kind = Kind::Install;
        const char* word = "";
        const char* glyph = "";
    };
    struct CrossSlot {
        int from = -1;
        int to = -1;
        int at = 0;
    };

    std::vector<Action> actions() const;
    void layout();
    void reflow(const std::vector<Action>& was, int64_t now);
    bool live(const Action& a) const;
    void draw_dots(Canvas& k, const Row& r, int top, int64_t now);
    int scroll_max() const;
    bool rest_in(const Line& l, int s) const;
    std::pair<int, int> extent(int s) const;
    Rect list_clip(int64_t now) const;
    bool drawn(int key, int64_t now) const;
    void scroll_to(int to, int64_t now);
    bool column_focus() const { return column_ || (acts_.empty() && land_focus_ < 0); }
    void dialog(bool stop, int64_t now);
    void open_stop(Overlay& d, int64_t now);
    void dialog_action(Overlay& d, bool stop, PadAction a, bool repeat, int64_t now, Act& out);
    Row row_of(const Action& a, bool rest = false) const;
    void draw_row(Canvas& k, const Row& r, bool dots, int top, fx alpha, bool focused, int64_t now);
    void snap_rows(int64_t now);
    std::vector<CrossSlot> cross_slots() const;
    int64_t cross_ms() const;
    void draw_cross(Canvas& k, InkLayer& ink, int64_t now, fx pane);
    void draw_rows(Canvas& c, InkLayer& ink, int64_t now, fx pane);
    void draw_column(Canvas& c, InkLayer& ink, int64_t now, fx pane);

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
    bool copying_ = false;
    QrGrid qr_;
    HelperProgress pending_;
    bool has_pending_ = false;
    int64_t shown_at_ = -1000000;
    std::vector<Action> acts_, was_;
    int64_t change_at_ = -1000000;
    std::vector<Row> cross_rows_;
    std::vector<uint8_t> cross_dots_;
    int64_t cross_at_ = -1000000;
    int64_t cross_end_ = -1000000;
    int focus_ = 0;
    int land_focus_ = -1;
    Tween bar_;
    bool column_ = false;
    Glide scroll_;
    int fly_from_ = 0, fly_top_ = 0, fly_bot_ = 0;
    std::vector<int> fly_set_;
    std::vector<Line> lines_;
    Art art_;
    Overlay dialog_, login_dialog_;
    bool login_wait_ = false;
    bool stop_dialog_ = false;
    ToastClock toast_;
};

}
