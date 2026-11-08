#include "screen_bt.h"

#include <algorithm>
#include <cstdio>

#include "../ui/roles.h"
#include "../ui/size.h"
#include "theme.h"

namespace fd {

namespace {

constexpr int ms_of(fx t) { return t >> kFxBits; }
int px64(pos64 u) { return (int)((((u * ppu()) >> kFxBits) + kHalf) >> kFxBits); }
int index_px(fx at, int pitch) { return (int)(((int64_t)at * pitch + kHalf) >> kFxBits); }

const char* const kConfirmItems[] = {"pair", "keep unpaired"};
constexpr int kConfirmKeep = 1;

}

void BtPage::Rows::fill(int row, Row& out) const
{
    const BtRow& b = v[(size_t)row];
    out.primary = b.name;
    out.secondary = b.state;
    out.bold = true;
    out.ring = true;
    out.ring_toggle = b.kind == BtRow::Paired || b.kind == BtRow::Found;
    out.ring_on = b.connected;
    out.ring_bare = false;
    out.ring_scale = kOne;
    out.glyph = "bluetooth";
    out.arc = 0;
    out.arc_cv = nullptr;
}

uint64_t BtPage::Rows::key(int row, int) const { return bt_row_key(v[(size_t)row]); }

bool BtPage::Rows::find(uint64_t k, int& row, int& col) const
{
    for (int r = 0; r < count(); r++)
        if (key(r, 0) == k) {
            row = r;
            col = 0;
            return true;
        }
    return false;
}

BtPage::BtPage(Fonts& f, GlyphCache& g) : fonts_(f), glyphs_(g)
{
    rows_.v = bt_rows(state_);
    list_.attach(&rows_, sz().rowHeight, tok::contentTop, canvas_h());
    close();
}

void BtPage::open(int64_t now)
{
    list_.set_focus(0, 0, now, false);
    list_.scroll().set(0);
    list_.invalidate();
    close();
}

void BtPage::close()
{
    for (Overlay* o : {&sheet_, &dialog_}) {
        o->shown = false;
        o->fade.set(0);
        o->scale.set(kOne);
    }
    dirty_ = true;
}

void BtPage::set_model(const BtState& s, int64_t now)
{
    std::vector<BtRow> v = bt_rows(s);
    if (v != rows_.v) {
        int focus = bt_refocus(rows_.v, list_.focus_row(), v);
        rows_.v = std::move(v);
        list_.reset_model(&rows_, now, rows_.key(focus, 0));
        dirty_ = true;
    }
    if (s.searching != state_.searching || s.search_end != state_.search_end ||
        bt_available(s) != bt_available(state_))
        dirty_ = true;
    BtPrompt was = state_.prompt;
    state_ = s;
    if (state_.prompt == was)
        return;
    if (state_.prompt.kind == BtPrompt::None) {
        if (dialog_.shown)
            hide(dialog_, ms_of(tok::motion_dialogHide), now);
        return;
    }
    show_dialog(now);
}

void BtPage::send(BtCmd::Type t, const std::string& path, bool yes)
{
    if (!send_)
        return;
    BtCmd c;
    c.type = t;
    c.path = path;
    c.yes = yes;
    send_(c);
}

BtPage::Act BtPage::action(PadAction a, bool repeat, int64_t now)
{
    for (Overlay* o : {&sheet_, &dialog_})
        if (!o->fade.done(now) || !o->scale.done(now)) {
            o->fade.set(o->fade.to);
            o->scale.set(o->scale.to);
            o->bar.set(fx_int(o->focus));
        }
    dirty_ = true;
    if (dialog_.shown) {
        dialog_action(a, repeat, now);
        return Act::None;
    }
    if (sheet_.shown) {
        sheet_action(a, repeat, now);
        return Act::None;
    }
    switch (a) {
    case PadAction::Up:
        list_.move(-1, now);
        break;
    case PadAction::Down:
        list_.move(1, now);
        break;
    case PadAction::Accept:
        if (!repeat)
            accept();
        break;
    case PadAction::Context: {
        int r = list_.focus_row();
        if (!repeat && r >= 0 && r < rows_.count() && rows_.v[(size_t)r].kind == BtRow::Paired)
            open_sheet(rows_.v[(size_t)r], now);
        break;
    }
    case PadAction::Back:
        if (repeat)
            break;
        if (!state_.searching)
            return Act::Pop;
        send(BtCmd::StopSearch);
        break;
    default:
        break;
    }
    return Act::None;
}

void BtPage::accept()
{
    int r = list_.focus_row();
    if (r < 0 || r >= rows_.count())
        return;
    const BtRow& b = rows_.v[(size_t)r];
    switch (b.kind) {
    case BtRow::Search:
        if (bt_available(state_))
            send(state_.searching ? BtCmd::StopSearch : BtCmd::Search);
        break;
    case BtRow::Power:
        send(BtCmd::PowerOn);
        break;
    case BtRow::Found:
        if (b.path != state_.pair_target)
            send(BtCmd::Pair, b.path);
        break;
    case BtRow::Paired:
        if (b.path != state_.connect_target)
            send(b.connected ? BtCmd::Disconnect : BtCmd::Connect, b.path);
        break;
    }
}

void BtPage::open_sheet(const BtRow& r, int64_t now)
{
    sheet_.shown = true;
    sheet_.title = r.name;
    sheet_.path = r.path;
    sheet_.connected = r.connected;
    sheet_.items = {r.connected ? "disconnect" : "connect", "forget"};
    sheet_.focus = 0;
    sheet_.bar.set(0);
    sheet_.scale.set(kOne);
    sheet_.fade.run(now, sheet_.fade.at(now), kOne, ms_of(tok::motion_sheet), Curve::Decel);
}

void BtPage::show_dialog(int64_t now)
{
    const BtPrompt& p = state_.prompt;
    char t[48];
    if (p.kind == BtPrompt::Pin)
        snprintf(t, sizeof t, "code %s", p.pin.c_str());
    else
        snprintf(t, sizeof t, "code %06u", (unsigned)p.passkey);
    dialog_.title = t;
    dialog_.path = p.path;
    bool fresh = !dialog_.shown;
    dialog_.items.clear();
    if (p.kind == BtPrompt::Confirm)
        dialog_.items.assign(std::begin(kConfirmItems), std::end(kConfirmItems));
    dialog_.focus = p.kind == BtPrompt::Confirm ? kConfirmKeep : 0;
    dialog_.bar.set(fx_int(dialog_.focus));
    dialog_.shown = true;
    dirty_ = true;
    if (!fresh)
        return;
    dialog_.fade.run(now, 0, kOne, ms_of(tok::motion_dialog), Curve::Pop);
    dialog_.scale.run(now, tok::motion_dialogScale, kOne, ms_of(tok::motion_dialog), Curve::BezierPop);
}

void BtPage::hide(Overlay& o, int ms, int64_t now)
{
    o.shown = false;
    o.fade.run(now, o.fade.at(now), 0, ms, &o == &sheet_ ? Curve::Decel : Curve::Pop);
    o.scale.set(o.scale.at(now));
    dirty_ = true;
}

void BtPage::move(Overlay& o, int delta, int64_t now)
{
    int n = (int)o.items.size();
    if (n == 0)
        return;
    int to = std::clamp(o.focus + delta, 0, n - 1);
    if (to == o.focus)
        return;
    o.focus = to;
    if (o.fade.done(now))
        o.bar.run(now, o.bar.at(now), fx_int(to), ms_of(tok::motion_focusCursor), Curve::Sine);
    else
        o.bar.set(fx_int(to));
}

void BtPage::sheet_action(PadAction a, bool repeat, int64_t now)
{
    switch (a) {
    case PadAction::Up:
        move(sheet_, -1, now);
        break;
    case PadAction::Down:
        move(sheet_, 1, now);
        break;
    case PadAction::Accept:
        if (repeat)
            break;
        if (sheet_.focus == 0)
            send(sheet_.connected ? BtCmd::Disconnect : BtCmd::Connect, sheet_.path);
        else
            send(BtCmd::Forget, sheet_.path);
        hide(sheet_, ms_of(tok::motion_sheet), now);
        break;
    case PadAction::Back:
        if (!repeat)
            hide(sheet_, ms_of(tok::motion_sheet), now);
        break;
    default:
        break;
    }
}

void BtPage::dialog_action(PadAction a, bool repeat, int64_t now)
{
    bool confirm = state_.prompt.kind == BtPrompt::Confirm;
    switch (a) {
    case PadAction::Up:
        move(dialog_, -1, now);
        break;
    case PadAction::Down:
        move(dialog_, 1, now);
        break;
    case PadAction::Accept:
    case PadAction::Back:
        if (repeat)
            break;
        if (confirm)
            send(BtCmd::Answer, dialog_.path, a == PadAction::Accept && dialog_.focus == 0);
        else
            send(BtCmd::Dismiss, dialog_.path);
        hide(dialog_, ms_of(tok::motion_dialogHide), now);
        break;
    default:
        break;
    }
}

void BtPage::land(int64_t now)
{
    for (Overlay* o : {&sheet_, &dialog_}) {
        o->fade.set(o->fade.to);
        o->scale.set(o->scale.to);
        o->bar.set(fx_int(o->focus));
    }
    list_.scroll().set(list_.scroll().target());
    list_.reset_model(&rows_, now, list_.focus_key());
    dirty_ = true;
}

bool BtPage::visible(const Overlay& o, int64_t now) const { return o.shown || o.fade.at(now) > 0; }

bool BtPage::moving(int64_t at) const
{
    if (list_.animating(at))
        return true;
    for (const Overlay* o : {&sheet_, &dialog_})
        if (!o->fade.done(at) || !o->scale.done(at) || !o->bar.done(at))
            return true;
    return false;
}

bool BtPage::animating(int64_t now) const { return moving(now) || (drawn_at_ >= 0 && moving(drawn_at_)); }

bool BtPage::dirty(int64_t now) const
{
    return dirty_ || retry_ || animating(now) || (state_.searching && bt_seconds(state_, now) != drawn_secs_);
}

int64_t BtPage::deadline(int64_t now) const { return bt_tick_at(state_, now); }

void BtPage::draw(Canvas& c, InkLayer& ink, int64_t now)
{
    list_.step(now);
    retry_ = false;
    const Theme& t = theme();
    Rect keep = c.clip();
    c.set_clip(kScreen);
    draw_page_ground(c, t);
    draw_page_chrome(c, fonts_, "bluetooth", nullptr, t);
    fx dim = visible(dialog_, now) ? dialog_.fade.at(now) : 0;
    Canvas& k = ink.begin(kScreen);
    draw_rows(k, now, kOne - dim);
    ink.compose(c.surface(), t.ink);
    draw_sweep(c, now);
    if (visible(sheet_, now))
        draw_sheet(c, ink, now);
    if (visible(dialog_, now))
        draw_dialog(c, ink, now);
    c.set_clip(keep);
    dirty_ = false;
    drawn_secs_ = bt_seconds(state_, now);
    drawn_at_ = now;
}

void BtPage::draw_rows(Canvas& k, int64_t now, fx bar_alpha)
{
    ListView& l = list_;
    int n = rows_.count();
    Rect keep = k.clip();
    k.set_clip(Rect{0, px(l.top()), kPortraitW, px(l.bottom())}.intersect(keep));
    if (n > 0) {
        int x = px(tok::hubEdge - tok::focus_barOffset);
        int y = px64((pos64)l.top() + l.bar_top(now) - l.offset());
        k.blend({x, y, x + px(tok::focus_barWidth), y + px(l.pitch())}, kWhite, alpha256(bar_alpha));
    }
    for (int r = l.first_visible(); r <= l.last_visible() && r < n; r++) {
        int top = px64((pos64)l.top() + (pos64)r * l.pitch() - l.offset());
        Row row = l.slot(r);
        if (rows_.v[(size_t)r].kind == BtRow::Search && state_.searching)
            row.secondary = "searching " + std::to_string(bt_seconds(state_, now));
        fx a = l.row_alpha(r, now, tok::type_listPlaylist_alpha, tok::type_listPlaylist_alphaDim);
        if (!draw_list_row(k, fonts_, &glyphs_, row, top, a, r == l.focus_row()))
            retry_ = true;
    }
    k.set_clip(keep);
}

void BtPage::draw_sweep(Canvas& c, int64_t now)
{
    ListView& l = list_;
    if (!state_.searching || rows_.v.empty() || rows_.v[0].kind != BtRow::Search || l.first_visible() > 0)
        return;
    int sweep = 360 * std::clamp(bt_seconds(state_, now), 0, kBtSearchS) / kBtSearchS;
    if (sweep <= 0)
        return;
    int top = px64((pos64)l.top() - l.offset());
    int d = px(sz().rowRing), cx = px(tok::hubEdge) + d / 2, cy = top + px(sz().rowHeight) / 2;
    int stroke = px(l.focus_row() == 0 ? tok::ringStrokeFocused : tok::ringStrokeIdle);
    Rect keep = c.clip();
    c.set_clip(Rect{0, px(l.top()), kPortraitW, px(l.bottom())}.intersect(keep));
    draw_centered(c, glyphs_.arc(d, stroke, tok::ringArcStart >> kFxBits, sweep), cx, cy, theme().ink, 256);
    c.set_clip(keep);
}

void BtPage::draw_sheet(Canvas& c, InkLayer& ink, int64_t now)
{
    const Theme& t = theme();
    fx p = sheet_.fade.at(now);
    int edge = px(tok::edge), pitch = px(sz().settingsPitch);
    TextStyle ts = role::dialogTitle.style(), is = sz().dialogItem.style();
    int title_h = fonts_.ascent(ts.px) + fonts_.descent(ts.px);
    int h = edge + title_h + (int)sheet_.items.size() * pitch + edge;
    int y = kPortraitH - h + (int)(((int64_t)(kOne - p) * h) >> kFxBits);
    c.blend(kScreen, t.scrim_sheet, t.scrim_sheet_a256 * alpha256(p) >> 8);
    Rect panel{0, y, kPortraitW, y + h};
    c.blend(panel, t.bg_elevated, alpha256(p));
    Canvas& k = ink.begin(panel);
    bool ok = fonts_.draw(k, ts, sheet_.title.c_str(), edge * 64, y + edge + fonts_.ascent(ts.px), kWhite,
                          alpha256(fx_mul(role::dialogTitle.alpha, p)));
    int rows_top = y + edge + title_h;
    int asc = fonts_.ascent(is.px), desc = fonts_.descent(is.px);
    for (size_t i = 0; i < sheet_.items.size(); i++) {
        int base = rows_top + (int)i * pitch + (pitch - asc - desc) / 2 + asc;
        fx a = (int)i == sheet_.focus ? sz().dialogItem.alpha : tok::type_dialogItem_alphaDim;
        ok = fonts_.draw(k, is, sheet_.items[i].c_str(), edge * 64, base, kWhite, alpha256(fx_mul(a, p))) && ok;
    }
    int bx = edge - px(tok::focus_barOffset), by = rows_top + index_px(sheet_.bar.at(now), pitch);
    k.blend({bx, by, bx + px(tok::focus_barWidth), by + pitch}, kWhite, alpha256(p));
    ink.compose(c.surface(), t.ink);
    if (!ok)
        retry_ = true;
}

void BtPage::draw_dialog(Canvas& c, InkLayer& ink, int64_t now)
{
    const Theme& t = theme();
    fx a = dialog_.fade.at(now);
    Zoom z{dialog_.scale.at(now), kPortraitW * 32, kPortraitH * 32};
    int edge = px(tok::edge), pitch = px(sz().settingsPitch), top = px(tok::contentTop);
    c.blend(kScreen, t.scrim_dialog, t.scrim_dialog_a256 * alpha256(a) >> 8);
    Canvas& k = ink.begin(kScreen);
    TextStyle ts = role::dialogTitle.style(), is = sz().dialogItem.style();
    bool ok = fonts_.draw(k, ts, dialog_.title.c_str(), edge * 64, px(tok::pivotBaseline), kWhite,
                          alpha256(fx_mul(role::dialogTitle.alpha, a)), z);
    int asc = fonts_.ascent(is.px), desc = fonts_.descent(is.px);
    for (size_t i = 0; i < dialog_.items.size(); i++) {
        int base = top + (int)i * pitch + (pitch - asc - desc) / 2 + asc;
        fx ia = (int)i == dialog_.focus ? sz().dialogItem.alpha : tok::type_dialogItem_alphaDim;
        ok = fonts_.draw(k, is, dialog_.items[i].c_str(), edge * 64, base, kWhite, alpha256(fx_mul(ia, a)), z) && ok;
    }
    if (!dialog_.items.empty()) {
        int bx = edge - px(tok::focus_barOffset), by = top + index_px(dialog_.bar.at(now), pitch);
        Rect bar{bx, by, bx + px(tok::focus_barWidth), by + pitch};
        k.fill_aa(Rect64::of(bar, z), kWhite, alpha256(a));
    }
    ink.compose(c.surface(), t.ink);
    if (!ok)
        retry_ = true;
}

}
