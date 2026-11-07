#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "../core/event.h"
#include "../core/fx.h"
#include "../gfx/canvas.h"
#include "../gfx/font.h"

namespace fd {

enum class KbDir : uint8_t { Tap, Left, Up, Right, Down };
enum class KbKind : uint8_t { Text, Password, Number };
enum class KbTarget : uint8_t { DevEcho, WifiPassword, WifiSsid, EqRename, EqValue, ClockField, Search, LyricsOffset, Balance, SvcSearch };

struct KbRequest {
    KbTarget target = KbTarget::DevEcho;
    std::string title, initial, hint;
    KbKind kind = KbKind::Text;
    size_t min = 0, max = 64;
    std::string extras, unit;
    int decimals = 0;
    std::function<std::string(const std::string&)> validate;
};

KbDir kb_dir(fx dx, fx dy);
enum KbPage : int { KbLetters, KbKana, KbSymbols };

uint32_t kb_char(int page, int digit, KbDir d);
bool kb_find(uint32_t c, int& page, int& digit, KbDir& d);
uint32_t kb_mark(uint32_t c);

class Keyboard {
public:
    enum class Act { None, Redraw, Done, Back, Refused };
    enum Code : int { Shift = 10, Symbols, Delete, Space, Done, Cancel, Toggle, Minus, Point };
    struct Key {
        int code;
        fx x, y, w, h;
    };

    void open(KbRequest r);
    void close();
    bool active() const { return open_; }
    const KbRequest& request() const { return req_; }
    const std::string& text() const { return text_; }
    const std::string& refusal() const { return refusal_; }
    const std::string& word() const { return word_; }
    void set_word(std::string w) { word_ = std::move(w); }
    bool edited() const { return !text_.empty() && text_ != req_.initial; }
    bool ready() const { return text_.size() >= req_.min && text_.size() <= req_.max; }
    Act event(const Event& e, int64_t now);
    bool tick(int64_t now);
    int64_t wake_at() const { return rep_at_; }
    void cancel_touch();
    void render(Canvas& c, Fonts& f, bool under = false, bool toast = false);
    bool secret(Rect& r) const;

    struct Cell {
        Key k;
        uint32_t ch;
        KbDir d;
    };
    std::vector<Key> keys() const;
    void set_bottom(fx b, fx at);
    std::vector<Cell> petal_cells() const;
    bool center(int code, fx& x, fx& y) const;
    int focus_code() const { return focus_ >= 0 && focus_ < (int)order_.size() ? order_[(size_t)focus_] : -1; }
    bool focus_on(int code);
    bool petal() const { return petal_ != PetalOff; }
    KbDir pick() const { return pick_; }
    int petal_code() const { return petal_code_; }
    int shift() const { return shift_; }
    bool symbols() const { return page_ == KbSymbols; }
    int page() const { return page_; }
    bool hidden() const { return hidden_; }

private:
    fx bottom_ = tok::canvasH, lift_ = 0;
    enum Petal { PetalOff, PetalTouch, PetalKnob };
    std::string label(int code) const;
    bool password() const { return req_.kind == KbKind::Password; }
    bool number() const { return req_.kind == KbKind::Number; }
    Key toggle_key() const;
    int hit(fx x, fx y) const;
    void type(uint32_t c);
    void mark();
    bool kana() const { return page_ == KbKana; }
    void erase();
    Act press(int code, int64_t now);
    Act commit();
    void step_pick(int d);
    void draw_key(Canvas& c, Fonts& f, const Key& k, bool bare);
    void draw_field(Canvas& c, Fonts& f, bool bare);
    void draw_petal(Canvas& c, Fonts& f, const std::vector<Cell>& cells);

    KbRequest req_;
    std::string text_, refusal_, word_;
    bool open_ = false, hidden_ = false;
    int page_ = KbLetters;
    int shift_ = 0;
    int64_t shift_at_ = -1;
    std::vector<int> order_;
    int focus_ = 0;
    Petal petal_ = PetalOff;
    int petal_code_ = -1;
    KbDir pick_ = KbDir::Tap;
    int down_ = -2;
    fx down_x_ = 0, down_y_ = 0;
    int64_t rep_at_ = -1;
    int reps_ = 0;
};

}
