#include "keyboard.h"

#include <algorithm>
#include <cstring>
#include <iterator>

#include "../lib/util.h"
#include "accent.h"
#include "roles.h"
#include "rows.h"
#include "size.h"

namespace fd {

namespace {

constexpr int ms(fx t) { return t >> kFxBits; }
constexpr int kNoKey = -2;

struct Slot {
    int code, col, row, rows, cols = 1;
};

const Slot kSlots[] = {{1, 0, 0, 1}, {2, 1, 0, 1}, {3, 2, 0, 1}, {Keyboard::Delete, 3, 0, 1},
                       {4, 0, 1, 1}, {5, 1, 1, 1}, {6, 2, 1, 1}, {Keyboard::Symbols, 3, 1, 1},
                       {7, 0, 2, 1}, {8, 1, 2, 1}, {9, 2, 2, 1}, {Keyboard::Cancel, 3, 2, 1},
                       {Keyboard::Shift, 0, 3, 1}, {0, 1, 3, 1}, {Keyboard::Space, 2, 3, 1}, {Keyboard::Done, 3, 3, 1}};

const char* const kLetters[10] = {"@-_/", ".,?!", "abc", "def", "ghi", "jkl", "mno", "pqrs", "tuv", "wxyz"};
const char* const kSymbols[10] = {"", "\"#$", "%&'", "()*", "+:;", "<=>", "[\\]", "^`{", "|}~", ""};
const char* const kKana[10][5] = {{"わ", "を", "ん", "ー", ""},     {"あ", "い", "う", "え", "お"}, {"か", "き", "く", "け", "こ"},
                                  {"さ", "し", "す", "せ", "そ"}, {"た", "ち", "つ", "て", "と"}, {"な", "に", "ぬ", "ね", "の"},
                                  {"は", "ひ", "ふ", "へ", "ほ"}, {"ま", "み", "む", "め", "も"}, {"や", "", "ゆ", "", "よ"},
                                  {"ら", "り", "る", "れ", "ろ"}};
const char* const kMarks[] = {"あぁ", "いぃ", "うぅゔ", "えぇ", "おぉ", "かが", "きぎ", "くぐ", "けげ", "こご", "さざ",
                              "しじ", "すず", "せぜ", "そぞ", "ただ", "ちぢ", "つっづ", "てで", "とど", "はばぱ", "ひびぴ",
                              "ふぶぷ", "へべぺ", "ほぼぽ", "やゃ", "ゆゅ", "よょ", "わゎ"};

bool letter(uint32_t c) { return c >= 'a' && c <= 'z'; }

uint32_t first(const char* s)
{
    const char* end = s + strlen(s);
    return *s ? utf8_next(s, end) : 0;
}

std::string utf8(uint32_t c)
{
    std::string s;
    utf8_put(s, c);
    return s;
}

void border(Canvas& c, Rect t, int bw, uint32_t rgb, int a)
{
    c.blend({t.x0, t.y0, t.x1, t.y0 + bw}, rgb, a);
    c.blend({t.x0, t.y1 - bw, t.x1, t.y1}, rgb, a);
    c.blend({t.x0, t.y0 + bw, t.x0 + bw, t.y1 - bw}, rgb, a);
    c.blend({t.x1 - bw, t.y0 + bw, t.x1, t.y1 - bw}, rgb, a);
}

Rect px_rect(const Keyboard::Key& k) { return {px(k.x), px(k.y), px(k.x + k.w), px(k.y + k.h)}; }

bool inside(const Keyboard::Key& k, fx x, fx y) { return x >= k.x && x < k.x + k.w && y >= k.y && y < k.y + k.h; }

void centered(Canvas& c, Fonts& f, const TextStyle& ts, const std::string& s, Rect r, uint32_t rgb, int a)
{
    int w = f.width64(ts, s.c_str());
    int base = r.y0 + (r.y1 - r.y0 - f.ascent(ts.px) - f.descent(ts.px)) / 2 + f.ascent(ts.px);
    Rect keep = c.clip();
    c.set_clip(r.intersect(keep));
    f.draw(c, ts, s.c_str(), (r.x0 + r.x1) * 32 - w / 2, base, rgb, a);
    c.set_clip(keep);
}

}

KbDir kb_dir(fx dx, fx dy)
{
    int64_t x = dx, y = dy, slop = std_u(tok::touchSlop);
    if (x * x + y * y <= slop * slop)
        return KbDir::Tap;
    if (axis_of(x, y) == Axis::Horizontal)
        return x < 0 ? KbDir::Left : KbDir::Right;
    return y < 0 ? KbDir::Up : KbDir::Down;
}

uint32_t kb_char(int page, int digit, KbDir d)
{
    if (digit < 0 || digit > 9)
        return 0;
    if (page == KbKana)
        return first(kKana[digit][(size_t)d]);
    if (d == KbDir::Down)
        return (uint32_t)('0' + digit);
    const char* s = page == KbSymbols ? kSymbols[digit] : kLetters[digit];
    if (!*s)
        return d == KbDir::Tap ? (uint32_t)('0' + digit) : 0;
    size_t i = (size_t)d;
    return i < strlen(s) ? (uint32_t)(unsigned char)s[i] : 0;
}

bool kb_find(uint32_t c, int& page, int& digit, KbDir& d)
{
    if (c >= 'A' && c <= 'Z')
        c = c - 'A' + 'a';
    for (int pg : {KbLetters, KbSymbols, KbKana})
        for (int k = 0; k < 10; k++)
            for (int i = 0; i < 5; i++)
                if (kb_char(pg, k, (KbDir)i) == c) {
                    page = pg;
                    digit = k;
                    d = (KbDir)i;
                    return true;
                }
    return false;
}

uint32_t kb_mark(uint32_t c)
{
    for (const char* m : kMarks) {
        const char* p = m;
        const char* end = m + strlen(m);
        uint32_t head = utf8_next(p, end), cur = head;
        while (cur != c && p < end)
            cur = utf8_next(p, end);
        if (cur == c)
            return p < end ? utf8_next(p, end) : head;
    }
    return c;
}

void Keyboard::open(KbRequest r)
{
    Scope scope(Fit::Kept, "kbd");
    req_ = std::move(r);
    text_ = req_.initial;
    while (text_.size() > req_.max)
        erase();
    refusal_.clear();
    word_.clear();
    open_ = true;
    hidden_ = false;
    page_ = KbLetters;
    shift_ = 0;
    shift_at_ = -1;
    order_.clear();
    if (password())
        order_.push_back(Toggle);
    for (const Key& k : keys())
        order_.push_back(k.code);
    focus_ = password() ? 1 : 0;
    cancel_touch();
}

void Keyboard::close()
{
    Scope scope(Fit::Kept, "kbd");
    open_ = false;
    cancel_touch();
}

void Keyboard::cancel_touch()
{
    Scope scope(Fit::Kept, "kbd");
    petal_ = PetalOff;
    petal_code_ = -1;
    down_ = kNoKey;
    rep_at_ = -1;
    reps_ = 0;
}

std::vector<Keyboard::Key> Keyboard::keys() const
{
    Scope scope(Fit::Kept, "kbd");
    std::vector<Slot> slots(std::begin(kSlots), std::end(kSlots));
    if (number()) {
        int minus = req_.extras.find('-') != std::string::npos, point = req_.extras.find('.') != std::string::npos;
        slots = {{1, 0, 0, 1}, {2, 1, 0, 1}, {3, 2, 0, 1}, {Delete, 3, 0, 1},
                 {4, 0, 1, 1}, {5, 1, 1, 1}, {6, 2, 1, 1}, {Cancel, 3, 1, 1},
                 {7, 0, 2, 1}, {8, 1, 2, 1}, {9, 2, 2, 1}, {Done, 3, 2, 2}};
        if (minus)
            slots.push_back({Minus, 0, 3, 1});
        slots.push_back({0, minus, 3, 1, 3 - minus - point});
        if (point)
            slots.push_back({Point, 2, 3, 1});
    }
    std::vector<Key> out;
    fx kh = std::min<fx>(tok::kb_keyH, (bottom_ + lift_ - tok::edge - tok::kb_top - 3 * tok::qpGutter) / 4);
    for (const Slot& sl : slots) {
        fx x = tok::edge + sl.col * (tok::kb_keyW + tok::qpGutter);
        fx y = tok::kb_top - lift_ + sl.row * (kh + tok::qpGutter);
        out.push_back({sl.code, x, y, sl.cols * tok::kb_keyW + (sl.cols - 1) * tok::qpGutter,
                       sl.rows * kh + (sl.rows - 1) * tok::qpGutter});
    }
    return out;
}

Keyboard::Key Keyboard::toggle_key() const
{
    return {Toggle, tok::canvasW - tok::edge - tok::kb_keyW, tok::kb_fieldTop - lift_, tok::kb_keyW, tok::kb_fieldH};
}

bool Keyboard::center(int code, fx& x, fx& y) const
{
    Scope scope(Fit::Kept, "kbd");
    std::vector<Key> ks = keys();
    if (code == Toggle && password())
        ks.push_back(toggle_key());
    for (const Key& k : ks)
        if (k.code == code) {
            x = k.x + k.w / 2;
            y = k.y + k.h / 2;
            return true;
        }
    return false;
}

bool Keyboard::focus_on(int code)
{
    Scope scope(Fit::Kept, "kbd");
    for (size_t i = 0; i < order_.size(); i++)
        if (order_[i] == code) {
            focus_ = (int)i;
            return true;
        }
    return false;
}

int Keyboard::hit(fx x, fx y) const
{
    for (const Key& k : keys())
        if (inside(k, x, y))
            return k.code;
    if (password() && inside(toggle_key(), x, y))
        return Toggle;
    return kNoKey;
}

void Keyboard::type(uint32_t c)
{
    if (text_.size() + utf8(c).size() > req_.max)
        return;
    if (number()) {
        size_t dot = text_.find('.');
        if (c == '-' ? !text_.empty()
                     : c == '.' ? dot != std::string::npos
                                : dot != std::string::npos && (int)(text_.size() - dot - 1) >= req_.decimals)
            return;
    }
    bool l = letter(c);
    if (shift_ && l)
        c = c - 'a' + 'A';
    if (shift_ == 1 && l)
        shift_ = 0;
    text_ += utf8(c);
}

void Keyboard::mark()
{
    const char* p = text_.data();
    const char* end = p + text_.size();
    size_t at = 0;
    uint32_t last = 0;
    while (p < end) {
        at = (size_t)(p - text_.data());
        last = utf8_next(p, end);
    }
    uint32_t to = kb_mark(last);
    if (text_.empty() || to == last || text_.size() - utf8(last).size() + utf8(to).size() > req_.max)
        return;
    text_ = text_.substr(0, at) + utf8(to);
}

void Keyboard::erase()
{
    while (!text_.empty() && ((unsigned char)text_.back() & 0xC0) == 0x80)
        text_.pop_back();
    if (!text_.empty())
        text_.pop_back();
}

Keyboard::Act Keyboard::commit()
{
    uint32_t c = kb_char(page_, petal_code_, pick_);
    petal_ = PetalOff;
    petal_code_ = -1;
    if (c)
        type(c);
    return Act::Redraw;
}

void Keyboard::step_pick(int d)
{
    int i = (int)pick_;
    for (int n = 0; n < 5; n++) {
        i = (i + d + 5) % 5;
        if (kb_char(page_, petal_code_, (KbDir)i)) {
            pick_ = (KbDir)i;
            return;
        }
    }
}

Keyboard::Act Keyboard::press(int code, int64_t now)
{
    switch (code) {
    case Shift:
        if (kana()) {
            mark();
            return Act::Redraw;
        }
        if (shift_ == 0) {
            shift_ = 1;
            shift_at_ = now;
        } else if (shift_ == 1 && shift_at_ >= 0 && now - shift_at_ <= ms(tok::input_knobDoubleMs)) {
            shift_ = 2;
        } else {
            shift_ = 0;
        }
        return Act::Redraw;
    case Symbols:
        page_ = password() ? (page_ == KbLetters ? KbSymbols : KbLetters) : (page_ + 1) % 3;
        return Act::Redraw;
    case Delete:
        erase();
        return Act::Redraw;
    case Space:
        type(' ');
        return Act::Redraw;
    case Toggle:
        hidden_ = !hidden_;
        return Act::Redraw;
    case Minus:
        type('-');
        return Act::Redraw;
    case Point:
        type('.');
        return Act::Redraw;
    case Cancel:
        return Act::Back;
    case Done:
        if (!ready())
            return Act::None;
        if (req_.validate) {
            refusal_ = req_.validate(text_);
            if (!refusal_.empty())
                return Act::Refused;
        }
        return Act::Done;
    default:
        if (number() && code >= 0 && code <= 9) {
            type((uint32_t)('0' + code));
            return Act::Redraw;
        }
        return Act::None;
    }
}

Keyboard::Act Keyboard::event(const Event& in, int64_t now)
{
    Scope scope(Fit::Kept, "kbd");
    Event e = scoped(in);
    switch (e.kind) {
    case Ev::KnobCw:
    case Ev::KnobCcw: {
        int d = e.kind == Ev::KnobCw ? 1 : -1;
        if (petal_ == PetalKnob) {
            step_pick(d);
            return Act::Redraw;
        }
        int to = std::clamp(focus_ + d, 0, (int)order_.size() - 1);
        if (to == focus_)
            return Act::None;
        focus_ = to;
        return Act::Redraw;
    }
    case Ev::KnobSelect: {
        if (petal_ == PetalKnob)
            return commit();
        int code = focus_code();
        if (code >= 0 && code <= 9 && !number()) {
            petal_ = PetalKnob;
            petal_code_ = code;
            pick_ = KbDir::Tap;
            return Act::Redraw;
        }
        if (code == Shift && kana()) {
            mark();
            return Act::Redraw;
        }
        if (code == Shift) {
            shift_ = (shift_ + 1) % 3;
            return Act::Redraw;
        }
        return press(code, now);
    }
    case Ev::KnobDouble:
        if (petal_ == PetalKnob)
            return commit();
        return Act::Back;
    case Ev::TouchDown: {
        cancel_touch();
        down_ = hit(e.x, e.y);
        down_x_ = e.x;
        down_y_ = e.y;
        if (down_ >= 0)
            focus_on(down_);
        if (down_ >= 0 && down_ <= 9 && !number()) {
            petal_ = PetalTouch;
            petal_code_ = down_;
            pick_ = KbDir::Tap;
        } else if (down_ == Delete) {
            rep_at_ = now + ms(tok::input_keyHoldMs);
        }
        return down_ >= 0 ? Act::Redraw : Act::None;
    }
    case Ev::TouchMove:
        if (down_ == Delete && kb_dir(e.x - down_x_, e.y - down_y_) != KbDir::Tap)
            rep_at_ = -1;
        if (petal_ != PetalTouch)
            return Act::None;
        if (kb_dir(e.x - down_x_, e.y - down_y_) == pick_)
            return Act::None;
        pick_ = kb_dir(e.x - down_x_, e.y - down_y_);
        return Act::Redraw;
    case Ev::TouchUp: {
        int code = down_;
        bool tap = kb_dir(e.x - down_x_, e.y - down_y_) == KbDir::Tap;
        bool repeated = reps_ > 0;
        down_ = kNoKey;
        rep_at_ = -1;
        reps_ = 0;
        if (petal_ == PetalTouch) {
            pick_ = kb_dir(e.x - down_x_, e.y - down_y_);
            return commit();
        }
        if (code < 0)
            return Act::None;
        if (!tap || (code == Delete && repeated))
            return Act::Redraw;
        Act a = press(code, now);
        return a == Act::None ? Act::Redraw : a;
    }
    default:
        return Act::None;
    }
}

bool Keyboard::tick(int64_t now)
{
    Scope scope(Fit::Kept, "kbd");
    if (rep_at_ < 0 || now < rep_at_)
        return false;
    bool had = !text_.empty();
    erase();
    reps_++;
    rep_at_ = now + ms(tok::input_volumeRepeatMs);
    return had;
}

std::vector<Keyboard::Cell> Keyboard::petal_cells() const
{
    Scope scope(Fit::Kept, "kbd");
    std::vector<Cell> out;
    if (petal_ == PetalOff)
        return out;
    std::vector<Key> ks = keys();
    const Key* k = nullptr;
    for (const Key& x : ks)
        if (x.code == petal_code_)
            k = &x;
    if (!k)
        return out;
    fx sx = k->w + tok::qpGutter, sy = k->h + tok::qpGutter;
    fx cx[5] = {k->x, k->x - sx, k->x, k->x + sx, k->x}, cy[5] = {k->y, k->y, k->y - sy, k->y, k->y + sy};
    fx x0 = k->x, x1 = k->x + k->w, y0 = k->y, y1 = k->y + k->h;
    for (int i = 0; i < 5; i++) {
        uint32_t ch = kb_char(page_, petal_code_, (KbDir)i);
        if (!ch)
            continue;
        if (shift_ && letter(ch))
            ch = ch - 'a' + 'A';
        out.push_back({{k->code, cx[i], cy[i], k->w, k->h}, ch, (KbDir)i});
        x0 = std::min(x0, cx[i]);
        x1 = std::max(x1, cx[i] + k->w);
        y0 = std::min(y0, cy[i]);
        y1 = std::max(y1, cy[i] + k->h);
    }
    auto snap = [](fx over, fx pitch) {
        return over > tok::edge ? (fx)(((int64_t)over + pitch - 1) / pitch * pitch) : 0;
    };
    fx dx = snap(-x0, sx) - snap(x1 - tok::canvasW, sx);
    fx dy = snap(-y0, sy) - snap(y1 - bottom_, sy);
    for (Cell& c : out) {
        c.k.x += dx;
        c.k.y += dy;
    }
    return out;
}

std::string Keyboard::label(int code) const
{
    if (code >= 0 && code <= 9 && number())
        return std::string(1, (char)('0' + code));
    if (code >= 0 && code <= 9 && kana())
        return kKana[code][0];
    if (code >= 0 && code <= 9) {
        std::string s = symbols() ? kSymbols[code] : kLetters[code];
        bool spaced = symbols() || code <= 1;
        std::string out;
        for (char ch : s) {
            if (spaced && !out.empty())
                out += ' ';
            out += shift_ && !symbols() && letter((unsigned char)ch) ? (char)(ch - 'a' + 'A') : ch;
        }
        return out;
    }
    return code == Shift && kana() ? "゛゜小"
           : code == Shift         ? (shift_ ? "SHIFT" : "shift")
           : code == Symbols       ? (page_ == KbLetters ? (password() ? "#+=" : "かな") : page_ == KbKana ? "#+=" : "abc")
           : code == Delete  ? "delete"
           : code == Space   ? "space"
           : code == Done    ? "done"
           : code == Minus   ? "-"
           : code == Point   ? "."
                             : "cancel";
}

void Keyboard::draw_key(Canvas& c, Fonts& f, const Key& k, bool bare)
{
    Rect r = px_rect(k);
    bool locked = k.code == Shift && shift_ == 2 && !kana();
    c.fill(r, down_ == k.code || locked ? tok::color_tilePressed : tok::color_tile);
    if (bare)
        return;
    TextStyle ls = role::listPrimary.style();
    ls.tcase = Case::Mixed;
    if (locked)
        accent_bold(ls);
    int a = alpha256(tok::alpha_full);
    std::string s = label(k.code);
    if (!s.empty())
        centered(c, f, ls, s, r, locked ? accent() : kWhite, k.code == Done && !ready() ? alpha256(tok::alpha_low) : a);
    if (k.code >= 0 && k.code <= 9 && !number() && !kana()) {
        TextStyle ds = role::sectionLabel.style();
        char d[2] = {(char)('0' + k.code), 0};
        int w = f.width64(ds, d);
        Rect keep = c.clip();
        c.set_clip(r.intersect(keep));
        f.draw(c, ds, d, (r.x1 - px(tok::letterInsetX)) * 64 - w, r.y1 - px(tok::letterInsetBaseline), kWhite,
               alpha256(role::sectionLabel.alpha));
        c.set_clip(keep);
    }
    if (focus_code() == k.code)
        border(c, r, px(tok::focus_border), kWhite, a);
}

void Keyboard::draw_field(Canvas& c, Fonts& f, bool bare)
{
    fx top = tok::kb_fieldTop - lift_;
    int x0 = px(tok::edge), x1 = kPortraitW - px(tok::edge), y0 = px(top), y1 = px(top + tok::kb_fieldH);
    Rect field{x0, y0, x1, y1};
    border(c, field, px(tok::kb_fieldBorder), tok::color_border, alpha256(tok::alpha_full));
    if (bare)
        return;
    TextStyle ts = role::listPrimary.style();
    ts.tcase = Case::Mixed;
    int pad = px(tok::kb_fieldPad);
    int box_top = px(top + tok::kb_fieldPad), base = box_top + f.ascent(ts.px);
    int right = x1 - pad;
    const char* word = password()                       ? (hidden_ ? "show" : "hide")
                       : number() && !req_.unit.empty() ? req_.unit.c_str()
                       : !word_.empty()                 ? word_.c_str()
                                                        : nullptr;
    if (word) {
        Key tk = toggle_key();
        TextStyle hs = role::listCount.style();
        int w = f.width64(hs, word);
        f.draw(c, hs, word, right * 64 - w, base, kWhite, alpha256(role::listCount.alpha));
        right = std::min(px(tk.x), right - (w + 63) / 64) - pad;
        if (focus_code() == Toggle)
            border(c, px_rect(tk), px(tok::focus_border), kWhite, alpha256(tok::alpha_full));
    }
    std::string shown = text_;
    if (password() && hidden_) {
        shown.clear();
        for (char ch : text_)
            if (((unsigned char)ch & 0xC0) != 0x80)
                shown += "\xE2\x80\xA2";
    }
    int cw = px(tok::kb_cursorW);
    int w = shown.empty() ? 0 : f.width64(ts, shown.c_str());
    int left = x0 + pad;
    int x64 = std::min(left * 64, (right - cw) * 64 - w);
    Rect clip{left, y0, right, y1};
    const std::string& hint = req_.hint.empty() ? req_.title : req_.hint;
    if (shown.empty())
        draw_clipped(c, f, ts, hint, (left + cw + px(tok::qpGutter)) * 64, base, alpha256(tok::alpha_low), clip);
    else
        draw_clipped(c, f, ts, shown, x64, base, alpha256(tok::alpha_full), clip);
    int cx = (x64 + w) / 64;
    c.fill({cx, box_top, cx + cw, box_top + f.ascent(ts.px) + f.descent(ts.px)}, accent());
}

void Keyboard::draw_petal(Canvas& c, Fonts& f, const std::vector<Cell>& cells)
{
    TextStyle gs = role::letterGlyph.style();
    gs.tcase = Case::Mixed;
    int g = px(tok::qpGutter);
    for (const Cell& cell : cells) {
        Rect r = px_rect(cell.k);
        c.fill({r.x0 - g, r.y0 - g, r.x1 + g, r.y1 + g}, tok::color_bgElevated);
    }
    for (const Cell& cell : cells) {
        Rect r = px_rect(cell.k);
        bool pick = cell.d == pick_;
        c.fill(r, pick ? (accent_off() ? tok::color_bg : accent()) : tok::color_tilePressed);
        if (pick && accent_off())
            border(c, r, px(tok::letterTileBorderFocused), kWhite, alpha256(tok::alpha_full));
        centered(c, f, gs, utf8(cell.ch), r, kWhite, alpha256(role::letterGlyph.alpha));
    }
}

void Keyboard::render(Canvas& c, Fonts& f, bool under, bool toast)
{
    Scope scope(Fit::Kept, "kbd");
    c.set_clip(kScreen);
    c.fill(kScreen, tok::color_bgElevated);
    draw_field(c, f, under || toast);
    std::vector<Cell> cells = under ? std::vector<Cell>() : petal_cells();
    for (const Key& k : keys()) {
        bool covered = false;
        for (const Cell& cell : cells)
            covered = covered || (k.x < cell.k.x + cell.k.w + tok::qpGutter && cell.k.x - tok::qpGutter < k.x + k.w &&
                                  k.y < cell.k.y + cell.k.h + tok::qpGutter && cell.k.y - tok::qpGutter < k.y + k.h);
        draw_key(c, f, k, under || covered);
    }
    draw_petal(c, f, cells);
}

void Keyboard::set_bottom(fx b, fx at)
{
    fx inset = (at == ppu() ? canvas_h() : fx_div(fx_int(kPortraitH), at)) - b;
    Scope scope(Fit::Kept, "kbd");
    bottom_ = u_from(b, at);
    lift_ = std::max<fx>(0, tok::canvasH - inset - bottom_);
}

bool Keyboard::secret(Rect& r) const
{
    Scope scope(Fit::Kept, "kbd");
    if (!password() || hidden_)
        return false;
    fx top = tok::kb_fieldTop - lift_;
    r = {px(tok::edge), px(top), kPortraitW - px(tok::edge), px(top + tok::kb_fieldH)};
    return true;
}

}
