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

void open_overlay(StoreOverlay& o, int focus, int64_t now)
{
    o.shown = true;
    o.focus = focus;
    o.bar.set(fx_int(focus));
    o.fade.run(now, 0, kOne, ms_of(tok::motion_dialog), Curve::Pop);
    o.scale.run(now, tok::motion_dialogScale, kOne, ms_of(tok::motion_dialog), Curve::BezierPop);
}

void hide_overlay(StoreOverlay& o, int64_t now)
{
    o.shown = false;
    o.fade.run(now, o.fade.at(now), 0, ms_of(tok::motion_dialogHide), Curve::Pop);
    o.scale.set(o.scale.at(now));
}

bool overlay_moving(const StoreOverlay& o, int64_t now)
{
    return !o.fade.done(now) || !o.scale.done(now) || !o.bar.done(now);
}

void move_overlay(StoreOverlay& o, int to, int64_t now)
{
    if (to == o.focus)
        return;
    o.focus = to;
    if (o.fade.done(now))
        o.bar.run(now, o.bar.at(now), fx_int(to), ms_of(tok::motion_focusCursor), Curve::Sine);
    else
        o.bar.set(fx_int(to));
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

}

PortPage::PortPage(Fonts& f, GlyphCache& g) : fonts_(f), glyphs_(g) {}

std::vector<PortPage::Action> PortPage::actions() const
{
    std::vector<Action> out;
    bool installing = working_ && cmd_ == HelperCmd::Install;
    bool updating = working_ && cmd_ == HelperCmd::Update;
    if (!item_.installed) {
        if ((online_ || installing) && item_.port >= 0)
            out.push_back({Kind::Install, "install", "plus"});
        return out;
    }
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
    dialog_ = StoreOverlay{};
    dialog_.fade.set(0);
    art_.reset();
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
}

int PortPage::scroll_max() const
{
    int last = lines_.empty() ? 0 : lines_.back().base;
    return std::max(0, last - kPortLastBase);
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
    has_pending_ = false;
    arc_ = done ? 360 : 0;
    hold_until_ = done ? now + kPortDoneHoldMs : now;
    if (dialog_.shown && stop_dialog_)
        hide_overlay(dialog_, now);
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

int64_t PortPage::deadline(int64_t now) const
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
    if (working_ && no_total_)
        take(now + kBusyFrameMs);
    return d;
}

void PortPage::dialog(bool stop, int64_t now)
{
    stop_dialog_ = stop;
    bool upd = cmd_ == HelperCmd::Update;
    dialog_.title = stop ? (upd ? "stop update" : "stop install") : "remove " + item_.title;
    dialog_.items = stop ? std::vector<std::string>{"stop", upd ? "keep updating" : "keep installing"}
                         : std::vector<std::string>{"remove", "cancel"};
    open_overlay(dialog_, 1, now);
}

void PortPage::dialog_action(PadAction a, bool repeat, int64_t now, Act& out)
{
    switch (a) {
    case PadAction::Up:
    case PadAction::Down:
        move_overlay(dialog_, std::clamp(dialog_.focus + (a == PadAction::Up ? -1 : 1), 0, 1), now);
        break;
    case PadAction::Accept:
    case PadAction::Back:
        if (repeat)
            break;
        if (a == PadAction::Accept && dialog_.focus == 0)
            out = stop_dialog_ ? Act::Stop : Act::Remove;
        hide_overlay(dialog_, now);
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
        dialog_action(a, repeat, now, out);
        return out;
    }
    if (working_) {
        if (a == PadAction::Back && !repeat)
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
                scroll_.to(to, now, ms_of(tok::motion_scroll), Curve::EaseIn);
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
    return r;
}

bool PortPage::live(const Action& a) const
{
    return (working_ || arc_ == 360) && ((cmd_ == HelperCmd::Install && a.kind == Kind::Install) ||
                                         (cmd_ == HelperCmd::Update && a.kind == Kind::Update) ||
                                         (cmd_ == HelperCmd::Remove && a.kind == Kind::Remove));
}

void PortPage::draw_row(Canvas& k, const Action& a, int top, fx alpha, bool focused, int64_t now)
{
    Row r = row_of(a);
    Rect keep = k.clip();
    k.set_clip(Rect{0, top, kPortRowClip, top + px(sz().rowHeight)}.intersect(keep));
    draw_list_row(k, fonts_, &glyphs_, r, top, alpha, focused);
    if (working_ && no_total_ && live(a))
        draw_dots(k, r, top, now);
    k.set_clip(keep);
}

void PortPage::draw_dots(Canvas& k, const Row& r, int top, int64_t now)
{
    int x0 = row_title_x64(fonts_, r, true) >> 6;
    int y = top + px(kPortDotsDrop);
    busy_dots(k, Rect{x0, y, kPortRowClip, y + px(kDotSide)}, kWhite, now - spin0_);
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
        int x = px(tok::hubEdge - tok::focus_barOffset);
        int y = kPortRowsTop + index_px(bar_.at(now), pitch);
        k.blend({x, y, x + px(tok::focus_barWidth), y + pitch}, kWhite, alpha256(pane));
    }
    ink.compose(c.surface(), theme().ink);
}

void PortPage::draw_column(Canvas& c, InkLayer& ink, int64_t now, fx pane)
{
    Rect area{kPortColX, 0, kPortColRight, kPortColBottom};
    Canvas& k = ink.begin(Rect{kPaneSplit, 0, kPortraitW, kPortColBottom});
    Rect keep = k.clip();
    k.set_clip(area.intersect(keep));
    int off = scroll_.at(now);
    for (const Line& l : lines_) {
        int base = l.base - off;
        if (l.text.empty() || base < -kPortBodyPitch || base > kPortColBottom + kPortBodyPitch)
            continue;
        fonts_.draw(k, l.role->style(), l.text.c_str(), kPortColX * 64, base, kWhite, alpha256(fx_mul(l.alpha, pane)));
    }
    k.set_clip(keep);
    ink.compose(c.surface(), theme().ink);
}

void PortPage::draw_dialog(Canvas& c, InkLayer& ink, int64_t now)
{
    const Theme& t = theme();
    fx a = dialog_.fade.at(now);
    Zoom z{dialog_.scale.at(now), kPortraitW * 32, kPortraitH * 32};
    int edge = px(tok::edge), pitch = px(sz().settingsPitch), top = px(tok::contentTop);
    c.blend(kScreen, t.scrim_dialog, t.scrim_dialog_a256 * alpha256(a) >> 8);
    Canvas& k = ink.begin(kScreen);
    TextStyle ts = role::dialogTitle.style(), is = sz().dialogItem.style();
    ts.tcase = Case::Mixed;
    Rect keep = k.clip();
    k.set_clip(Rect{0, 0, kPortDialogClip, kPortraitH}.intersect(keep));
    fonts_.draw(k, ts, dialog_.title.c_str(), edge * 64, px(tok::pivotBaseline), kWhite,
                alpha256(fx_mul(role::dialogTitle.alpha, a)), z);
    k.set_clip(keep);
    int asc = fonts_.ascent(is.px), desc = fonts_.descent(is.px);
    for (size_t i = 0; i < dialog_.items.size(); i++) {
        int base = top + (int)i * pitch + (pitch - asc - desc) / 2 + asc;
        fx ia = (int)i == dialog_.focus ? sz().dialogItem.alpha : tok::type_dialogItem_alphaDim;
        fonts_.draw(k, is, dialog_.items[i].c_str(), edge * 64, base, kWhite, alpha256(fx_mul(ia, a)), z);
    }
    int bx = edge - px(tok::focus_barOffset), by = top + index_px(dialog_.bar.at(now), pitch);
    Rect bar{bx, by, bx + px(tok::focus_barWidth), by + pitch};
    k.fill_aa(Rect64::of(bar, z), kWhite, alpha256(a));
    ink.compose(c.surface(), t.ink);
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
    blit_art(c, art_.get(), Rect{kPortArtX, kPortArtTop, kPortArtX + kStoreShotSide, kPortArtTop + kStoreShotSide},
             256);
    draw_rows(c, ink, now, rows_a);
    draw_column(c, ink, now, col_a);
    if (dialog_.shown || dialog_.fade.at(now) > 0)
        draw_dialog(c, ink, now);
    draw_toast_of(c, fonts_, ink, toast_, now, t);
    c.set_clip(keep);
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
    for (const Line& l : lines_) {
        if (l.text.empty())
            continue;
        TextStyle st = l.role->style();
        int y0 = std::max(0, l.base - off - fonts_.ascent(st.px));
        int y1 = std::min(kPortColBottom, l.base - off + fonts_.descent(st.px));
        if (y1 > y0)
            out.push_back(Rect{kPaneSplit, y0, kPortraitW, y1});
    }
    return out;
}

}
