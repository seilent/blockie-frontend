#include "screen_store.h"

#include <algorithm>
#include <cstdint>
#include <functional>

#include "../ui/busydots.h"
#include "../ui/roles.h"
#include "../ui/size.h"
#include "ring_spin.h"
#include "screen_settings.h"
#include "text_wrap.h"
#include "theme.h"

namespace fd {

namespace {

constexpr int ms_of(fx t) { return t >> kFxBits; }
int index_px(fx at, int pitch) { return (int)(((int64_t)at * pitch + kHalf) >> kFxBits); }

void blit_art(Canvas& c, const Image* art, Rect r, int a)
{
    if (!art || art->w <= 0 || art->h <= 0 || a <= 0)
        return;
    c.blit(*art, r.x0 + (r.x1 - r.x0 - art->w) / 2, r.y0 + (r.y1 - r.y0 - art->h) / 2, a);
}

Surface shift_x(Surface s, int ox)
{
    if constexpr (std::is_same_v<LiveL, Rows>)
        s.px += ox;
    s.w -= ox;
    return s;
}

void draw_toast_of(Canvas& c, Fonts& f, InkLayer& ink, const ToastClock& tc, int64_t now, const Theme& t)
{
    if (tc.phase(now) == 0)
        return;
    ToastView v;
    v.line1 = tc.line1(now);
    v.line2 = tc.line2(now);
    v.e = tc.eased(now);
    v.text = tc.text(now);
    draw_toast(c, f, ink, v, t);
}

struct StopWords {
    const char* title;
    const char* keep;
};

StopWords stop_words(HelperCmd cmd)
{
    if (cmd == HelperCmd::SteamGet)
        return {"stop getting files", "keep getting files"};
    if (cmd == HelperCmd::Update)
        return {"stop update", "keep updating"};
    return {"stop install", "keep installing"};
}

}

PortPage::PortPage(Fonts& f, GlyphCache& g) : fonts_(f), glyphs_(g) {}

std::vector<PortPage::Action> PortPage::actions() const
{
    std::vector<Action> out;
    bool installing = working_ && cmd_ == HelperCmd::Install;
    bool updating = working_ && cmd_ == HelperCmd::Update;
    bool getting = working_ && cmd_ == HelperCmd::SteamGet;
    if (!item_.installed) {
        if ((online_ || installing) && item_.port >= 0)
            out.push_back({Kind::Install, "install", "plus"});
        return out;
    }
    if (item_.steam && !item_.update && (online_ || getting))
        out.push_back({Kind::SteamGet, "get files", "disc"});
    out.push_back({Kind::Play, "play", "play"});
    if (item_.update && (online_ || updating))
        out.push_back({Kind::Update, "update", "repeat"});
    if (item_.port >= 0)
        out.push_back({Kind::Remove, "remove", "minus"});
    return out;
}

void PortPage::open(const StoreItem& it, bool online, bool working)
{
    item_ = it;
    online_ = online;
    working_ = working;
    acts_ = actions();
    was_ = acts_;
    change_at_ = -1000000;
    focus_ = 0;
    land_focus_ = -1;
    bar_.set(0);
    column_ = false;
    scroll_.set(0);
    words_.clear();
    arc_ = 0;
    has_pending_ = false;
    dialog_ = Overlay{};
    login_dialog_ = Overlay{};
    dialog_.fade.set(0);
    login_dialog_.fade.set(0);
    login_wait_ = false;
    art_.reset();
    copying_ = false;
    qr_ = QrGrid();
    toast_.hide();
    layout();
}

void PortPage::layout()
{
    lines_.clear();
    int avail64 = (kPortColRight - kPortColX) * 64;
    auto measure_of = [&](const Role& r) {
        TextStyle st = r.style();
        return [this, st](const std::string& s) { return (int)fonts_.width64(st, s.c_str()); };
    };
    lines_.push_back({&role::npArtist, item_.porter, kPortPorterBase, role::npArtist.alpha});
    lines_.push_back({&role::npAlbum, join_words(item_.genres), kPortGenresBase, tok::type_npAlbum_alpha});
    TextStyle ts = role::npTitle.style();
    int pitch = fonts_.ascent(ts.px) + fonts_.descent(ts.px);
    std::vector<std::string> title = wrap_lines(item_.title, avail64, kPortTitleLines, measure_of(role::npTitle));
    int base = kPortTitleBase;
    for (size_t i = 0; i < title.size(); i++) {
        base = kPortTitleBase + (int)i * pitch;
        lines_.push_back({&role::npTitle, title[i], base, role::npTitle.alpha});
    }
    base += kPortSizeStep;
    lines_.push_back({&role::npNext, size_words(item_.size), base, role::npNext.alpha});
    bool any = false;
    for (const std::string* body : {&item_.description, &item_.inst}) {
        if (body->empty())
            continue;
        if (any)
            base += kPortBodyPitch;
        any = true;
        for (const std::string& l : body_lines(*body, avail64, measure_of(role::npNext))) {
            base += kPortBodyPitch;
            lines_.push_back({&role::npNext, l, base, role::npNext.alpha});
        }
    }
    for (Line& l : lines_) {
        TextStyle st = l.role->style();
        int top = 0, bottom = 0;
        fonts_.ink_rows(st, l.text.c_str(), top, bottom);
        l.up = std::max(fonts_.ascent(st.px), -top);
        l.down = std::max(fonts_.descent(st.px), bottom);
    }
}

int PortPage::scroll_max() const
{
    int last = lines_.empty() ? 0 : lines_.back().base;
    return std::max(0, last - kPortLastBase);
}

bool PortPage::rest_in(const Line& l, int s) const
{
    if (l.text.empty() || l.base + l.down - s > kBandBottom)
        return false;
    return s == 0 || l.base - l.up - s >= px(tok::toastHeight);
}

std::pair<int, int> PortPage::extent(int s) const
{
    int top = 0, bottom = 0;
    bool any = false;
    for (const Line& l : lines_) {
        if (!rest_in(l, s))
            continue;
        top = any ? std::min(top, l.base - l.up - s) : l.base - l.up - s;
        bottom = any ? std::max(bottom, l.base + l.down - s) : l.base + l.down - s;
        any = true;
    }
    return any ? std::make_pair(top, bottom) : std::make_pair(px(tok::toastHeight), px(tok::toastHeight));
}

Rect PortPage::list_clip(int64_t now) const
{
    std::pair<int, int> to = extent(scroll_.target());
    int span = scroll_.target() - fly_from_;
    if (!scroll_.running(now) || span == 0)
        return Rect{kPortColX, to.first, kPortColRight, to.second};
    int64_t done = scroll_.at(now) - fly_from_;
    int y0 = fly_top_ + (int)((to.first - fly_top_) * done / span);
    int y1 = fly_bot_ + (int)((to.second - fly_bot_) * done / span);
    return Rect{kPortColX, y0, kPortColRight, y1};
}

bool PortPage::drawn(int key, int64_t now) const
{
    if (rest_in(lines_[(size_t)key], scroll_.target()))
        return true;
    return scroll_.running(now) && std::find(fly_set_.begin(), fly_set_.end(), key) != fly_set_.end();
}

void PortPage::scroll_to(int to, int64_t now)
{
    Rect c = list_clip(now);
    std::vector<int> set;
    for (int i = 0; i < (int)lines_.size(); i++)
        if (drawn(i, now))
            set.push_back(i);
    fly_set_ = std::move(set);
    fly_from_ = scroll_.at(now);
    fly_top_ = c.y0;
    fly_bot_ = c.y1;
    scroll_.to(to, now, ms_of(tok::motion_scroll), Curve::EaseIn);
}

void PortPage::set_item(const StoreItem& it, bool online, int64_t now)
{
    std::vector<Action> was = acts_;
    bool fresh = it.title != item_.title || it.description != item_.description || it.inst != item_.inst ||
                 it.porter != item_.porter || it.size != item_.size || it.genres != item_.genres;
    item_ = it;
    online_ = online;
    if (fresh) {
        layout();
        scroll_.set(std::min(scroll_.target(), scroll_max()));
    }
    acts_ = actions();
    reflow(was, now);
}

void PortPage::reflow(const std::vector<Action>& was, int64_t now)
{
    auto same = [](const std::vector<Action>& a, const std::vector<Action>& b) {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); i++)
            if (a[i].kind != b[i].kind)
                return false;
        return true;
    };
    if (same(was, acts_))
        return;
    if (now >= change_at_)
        was_ = was;
    change_at_ = std::max(now, hold_until_);
    int from = land_focus_ >= 0 ? land_focus_ : focus_;
    Kind keep = from >= 0 && from < (int)was.size() ? was[(size_t)from].kind : Kind::Play;
    int to = -1;
    for (int i = 0; i < (int)acts_.size(); i++)
        if (acts_[(size_t)i].kind == keep)
            to = i;
    if (to < 0)
        to = std::min(from, (int)acts_.size()) - 1;
    land_focus_ = std::clamp(to, 0, std::max(0, (int)acts_.size() - 1));
    tick(now);
}

void PortPage::set_working(bool on, HelperCmd cmd, bool queued, int64_t now)
{
    working_ = on;
    cmd_ = cmd;
    if (!on)
        return;
    arc_ = 0;
    copying_ = false;
    qr_ = QrGrid();
    no_total_ = cmd == HelperCmd::Remove && !queued;
    spin0_ = now;
    words_ = queued                      ? progress_words(HelperProgress{"wait", 0, -1})
             : cmd == HelperCmd::Remove ? progress_words(HelperProgress{"remove", 0, -1})
                                        : std::string();
    shown_at_ = -1000000;
    has_pending_ = false;
    acts_ = actions();
    was_ = acts_;
    focus_ = std::clamp(focus_, 0, std::max(0, (int)acts_.size() - 1));
}

void PortPage::progress(const HelperProgress& p, int64_t now)
{
    if (!working_)
        return;
    if (p.what != "login")
        qr_ = QrGrid();
    if (p.what == "copy" && !copying_) {
        copying_ = true;
        if (dialog_.shown && stop_dialog_)
            hide_overlay(dialog_, now, true);
    }
    if (now - shown_at_ < kProgressMs) {
        pending_ = p;
        has_pending_ = true;
        return;
    }
    has_pending_ = false;
    shown_at_ = now;
    words_ = progress_words(p);
    arc_ = progress_arc(p);
    no_total_ = !progress_known(p);
}

void PortPage::finished(bool done, int64_t now)
{
    working_ = false;
    copying_ = false;
    qr_ = QrGrid();
    has_pending_ = false;
    arc_ = done ? 360 : 0;
    hold_until_ = done ? now + kPortDoneHoldMs : now;
    if (dialog_.shown && stop_dialog_)
        hide_overlay(dialog_, now, true);
    if (!done)
        words_.clear();
}

void PortPage::tick(int64_t now)
{
    if (has_pending_ && now - shown_at_ >= kProgressMs)
        progress(pending_, now);
    if (!working_ && now >= hold_until_ && arc_ == 360) {
        arc_ = 0;
        words_.clear();
    }
    if (land_focus_ >= 0 && now >= change_at_) {
        focus_ = std::clamp(land_focus_, 0, std::max(0, (int)acts_.size() - 1));
        land_focus_ = -1;
        bar_.set(fx_int(focus_));
        if (acts_.empty())
            column_ = true;
    }
}

int64_t PortPage::deadline(int64_t now, bool dots) const
{
    int64_t d = toast_.deadline(now);
    auto take = [&](int64_t at) {
        if (at > now && (d < 0 || at < d))
            d = at;
    };
    if (has_pending_)
        take(shown_at_ + kProgressMs);
    if (arc_ == 360)
        take(hold_until_);
    if (dots && working_ && no_total_)
        take(now + kBusyFrameMs);
    return d;
}

void PortPage::dialog(bool stop, int64_t now)
{
    stop_dialog_ = stop;
    if (stop) {
        open_stop(dialog_, now);
        return;
    }
    dialog_.title = "remove " + item_.title;
    dialog_.items = {"remove", "cancel"};
    open_overlay(dialog_, 1, now, true);
}

void PortPage::open_stop(Overlay& d, int64_t now)
{
    StopWords w = stop_words(cmd_);
    d.title = w.title;
    d.items = {"stop", w.keep};
    open_overlay(d, 1, now, true);
}

void PortPage::dialog_action(Overlay& d, bool stop, PadAction a, bool repeat, int64_t now, Act& out)
{
    switch (a) {
    case PadAction::Up:
    case PadAction::Down:
        move_overlay(d, std::clamp(d.focus + (a == PadAction::Up ? -1 : 1), 0, 1), 0, now);
        break;
    case PadAction::Accept:
    case PadAction::Back:
        if (repeat)
            break;
        if (a == PadAction::Accept && d.focus == 0)
            out = stop ? Act::Stop : Act::Remove;
        hide_overlay(d, now, true);
        break;
    default:
        break;
    }
}

PortPage::Act PortPage::action(PadAction a, bool repeat, int64_t now)
{
    Act out = Act::None;
    if (overlay_moving(dialog_, now)) {
        dialog_.fade.set(dialog_.fade.to);
        dialog_.scale.set(dialog_.scale.to);
        dialog_.bar.set(fx_int(dialog_.focus));
    }
    bool held = now < change_at_ || (!working_ && arc_ == 360 && now < hold_until_);
    if (held) {
        change_at_ = now;
        hold_until_ = now;
        arc_ = 0;
        words_.clear();
        tick(now);
    }
    if (dialog_.shown) {
        dialog_action(dialog_, stop_dialog_, a, repeat, now, out);
        return out;
    }
    if (working_) {
        if (a == PadAction::Back && !repeat && !copying_)
            dialog(true, now);
        return out;
    }
    if (held && a == PadAction::Accept)
        return out;
    switch (a) {
    case PadAction::Up:
    case PadAction::Down: {
        int d = a == PadAction::Up ? -1 : 1;
        if (column_focus()) {
            int to = std::clamp(scroll_.target() + d * kPortScrollStep, 0, scroll_max());
            if (to != scroll_.target())
                scroll_to(to, now);
            break;
        }
        int to = std::clamp(focus_ + d, 0, (int)acts_.size() - 1);
        if (to != focus_) {
            bar_.run(now, bar_.at(now), fx_int(to), ms_of(tok::motion_focusCursor), Curve::Sine);
            focus_ = to;
        }
        break;
    }
    case PadAction::Right:
        if (!column_ && !acts_.empty() && scroll_max() > 0)
            column_ = true;
        break;
    case PadAction::Left:
        if (column_ && !acts_.empty())
            column_ = false;
        break;
    case PadAction::Accept:
        if (repeat || column_focus())
            break;
        switch (acts_[(size_t)focus_].kind) {
        case Kind::Install: out = Act::Install; break;
        case Kind::Update: out = Act::Update; break;
        case Kind::Play: out = Act::Play; break;
        case Kind::Remove: dialog(false, now); break;
        case Kind::SteamGet: out = Act::SteamGet; break;
        }
        break;
    case PadAction::Back:
        if (!repeat)
            out = Act::Pop;
        break;
    default:
        break;
    }
    return out;
}

Row PortPage::row_of(const Action& a) const
{
    Row r;
    r.primary = a.word;
    r.bold = true;
    r.ring = true;
    r.glyph = a.glyph;
    r.ring_scale = kOne;
    if (live(a)) {
        r.arc = no_total_ && working_ ? 0 : arc_;
        r.secondary = words_;
    }
    if (r.secondary.empty() && a.kind == Kind::Install)
        r.secondary = size_words(item_.size);
    if (r.secondary.empty() && a.kind == Kind::SteamGet)
        r.secondary = "from steam";
    return r;
}

bool PortPage::live(const Action& a) const
{
    return (working_ || arc_ == 360) && ((cmd_ == HelperCmd::Install && a.kind == Kind::Install) ||
                                         (cmd_ == HelperCmd::Update && a.kind == Kind::Update) ||
                                         (cmd_ == HelperCmd::Remove && a.kind == Kind::Remove) ||
                                         (cmd_ == HelperCmd::SteamGet && a.kind == Kind::SteamGet));
}

void PortPage::draw_row(Canvas& k, const Action& a, int top, fx alpha, bool focused, int64_t now)
{
    Row r = row_of(a);
    Rect keep = k.clip();
    Canvas v(shift_x(k.surface(), kPortRowX));
    v.set_clip(Rect{0, top, kPortRowClip - kPortRowX, top + px(sz().rowHeight)}.intersect(
        Rect{keep.x0 - kPortRowX, keep.y0, keep.x1 - kPortRowX, keep.y1}));
    draw_list_row(v, fonts_, &glyphs_, r, top, alpha, focused);
    if (working_ && no_total_ && live(a))
        draw_dots(v, r, top, now);
}

void PortPage::draw_dots(Canvas& k, const Row& r, int top, int64_t now)
{
    int x0 = row_title_x64(fonts_, r, true) >> 6;
    int y = top + px(kPortDotsDrop);
    busy_dots(k, Rect{x0, y, kPortRowClip - kPortRowX, y + px(kDotSide)}, kWhite, now - spin0_);
}

void PortPage::draw_rows(Canvas& c, InkLayer& ink, int64_t now, fx pane)
{
    int pitch = px(sz().rowHeight);
    fx lit = tok::type_listPlaylist_alpha, dim = tok::type_listPlaylist_alphaDim;
    const std::vector<Action>& rows = now < change_at_ ? was_ : acts_;
    Canvas& k = ink.begin(Rect{0, kPortRowsTop, kPaneSplit, kPortraitH});
    for (size_t i = 0; i < rows.size(); i++)
        draw_row(k, rows[i], kPortRowsTop + (int)i * pitch, fx_mul((int)i == focus_ ? lit : dim, pane),
                 (int)i == focus_, now);
    if (!rows.empty() && !column_focus()) {
        int x = kPortRowX + px(tok::hubEdge - tok::focus_barOffset);
        int y = kPortRowsTop + index_px(bar_.at(now), pitch);
        k.blend({x, y, x + px(tok::focus_barWidth), y + pitch}, kWhite, alpha256(pane));
    }
    ink.compose(c.surface(), theme().ink);
}

void PortPage::draw_column(Canvas& c, InkLayer& ink, int64_t now, fx pane)
{
    Canvas& k = ink.begin(Rect{kPaneSplit, 0, kPortraitW, kPortColBottom});
    Rect keep = k.clip();
    k.set_clip(list_clip(now).intersect(keep));
    int off = scroll_.at(now);
    for (int i = 0; i < (int)lines_.size(); i++) {
        const Line& l = lines_[(size_t)i];
        if (l.text.empty() || !drawn(i, now))
            continue;
        fonts_.draw(k, l.role->style(), l.text.c_str(), kPortColX * 64, l.base - off, kWhite,
                    alpha256(fx_mul(l.alpha, pane)));
    }
    k.set_clip(keep);
    ink.compose(c.surface(), theme().ink);
}

void PortPage::draw(Canvas& c, InkLayer& ink, int64_t now)
{
    tick(now);
    const Theme& t = theme();
    Rect keep = c.clip();
    c.set_clip(kScreen.intersect(keep));
    draw_page_ground(c, t);
    bool col = column_focus();
    fx rows_a = col ? tok::motion_quickplayAlpha : tok::alpha_full;
    fx col_a = col ? tok::alpha_full : tok::motion_quickplayAlpha;
    Rect art{kPortArtX, kPortArtTop, kPortArtX + kStoreShotSide, kPortArtTop + kStoreShotSide};
    if (qr_shown() && !login_wait_)
        qr_draw(c, qr_, art, t.light ? t.ink : t.bg, t.light ? t.bg : t.ink);
    else
        blit_art(c, art_.get(), art, 256);
    draw_rows(c, ink, now, rows_a);
    draw_column(c, ink, now, col_a);
    draw_dialog(c, fonts_, ink, dialog_, now, t, true, kPortDialogClip);
    draw_toast_of(c, fonts_, ink, toast_, now, t);
    c.set_clip(keep);
}

Rect login_box()
{
    return Rect{px(tok::hubEdge), px(tok::contentTop), kPortraitW - px(tok::hubEdge), kPortraitH - px(tok::qpHintDrop)};
}

void PortPage::draw_login(Canvas& c, InkLayer& ink, int64_t now)
{
    tick(now);
    const Theme& t = theme();
    Rect keep = c.clip();
    c.set_clip(kScreen.intersect(keep));
    draw_page_ground(c, t);
    draw_page_chrome(c, fonts_, kLoginWord, nullptr, t);
    Canvas& k = ink.begin(kScreen);
    fonts_.draw(k, role::listCount.style(), words_.c_str(), u64(tok::hubEdge), px(tok::queueCountBaseline), kWhite,
                alpha256(role::listCount.alpha));
    ink.compose(c.surface(), t.ink);
    if (qr_shown())
        qr_draw(c, qr_, login_box(), t.light ? t.ink : t.bg, t.light ? t.bg : t.ink);
    draw_dialog(c, fonts_, ink, login_dialog_, now, t, true, kPortDialogClip);
    c.set_clip(keep);
}

PortPage::Act PortPage::login_action(PadAction a, bool repeat, int64_t now)
{
    Act out = Act::None;
    if (overlay_moving(login_dialog_, now)) {
        login_dialog_.fade.set(login_dialog_.fade.to);
        login_dialog_.scale.set(login_dialog_.scale.to);
        login_dialog_.bar.set(fx_int(login_dialog_.focus));
    }
    if (login_dialog_.shown)
        dialog_action(login_dialog_, true, a, repeat, now, out);
    else if (a == PadAction::Back && !repeat && working_ && !copying_)
        open_stop(login_dialog_, now);
    return out;
}

void PortPage::drop_login_dialog()
{
    login_dialog_.shown = false;
    login_dialog_.fade.set(0);
    login_dialog_.scale.set(kOne);
}

bool PortPage::overlays_idle(int64_t now) const
{
    return !dialog_.shown && !overlay_moving(dialog_, now);
}

int64_t PortPage::overlays_end() const
{
    if (dialog_.shown)
        return -1;
    return std::max({dialog_.fade.end(), dialog_.scale.end(), dialog_.bar.end()});
}

bool PortPage::animating(int64_t now) const
{
    if (overlay_moving(dialog_, now) || toast_.animating(now) || scroll_.running(now) || !bar_.done(now))
        return true;
    return now < change_at_;
}

int PortPage::focus_y() const { return kPortRowsTop + focus_ * px(sz().rowHeight) + px(sz().rowHeight) / 2; }

std::vector<Rect> PortPage::line_rects(int64_t now) const
{
    std::vector<Rect> out;
    int off = scroll_.at(now);
    Rect clip = list_clip(now);
    for (int i = 0; i < (int)lines_.size(); i++) {
        const Line& l = lines_[(size_t)i];
        if (l.text.empty() || !drawn(i, now))
            continue;
        int y0 = std::max(clip.y0, l.base - off - l.up);
        int y1 = std::min(clip.y1, l.base - off + l.down);
        if (y1 > y0)
            out.push_back(Rect{kPaneSplit, y0, kPortraitW, y1});
    }
    return out;
}

}
