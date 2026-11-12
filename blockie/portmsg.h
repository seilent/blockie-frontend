#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "../gfx/canvas.h"
#include "../gfx/font.h"
#include "../gfx/glyph.h"
#include "../gfx/wayland.h"
#include "glide.h"
#include "ink.h"
#include "input_pad.h"
#include "layer_present.h"
#include "theme.h"

namespace fd {

constexpr size_t kMsgTextCap = 65536;
constexpr size_t kMsgLineCap = 4096;
constexpr size_t kMsgLogLines = 200;
constexpr size_t kMsgOptionCap = 256;
constexpr uint32_t kMsgFrameCap = 65536;
constexpr size_t kMsgHeader = 8;
constexpr int kMsgTimeoutMax = 86400;
constexpr int kMsgBacklog = 16;
constexpr int64_t kMsgFrameMs = 16;

constexpr int kMsgDone = 0;
constexpr int kMsgCancel = 3;
constexpr int kMsgUsage = 64;

constexpr const char* kMsgAppId = "net.seilent.Blockie.PortMsg";
constexpr const char* kMsgSock = "/portmsg.sock";
constexpr const char* kMsgLock = "/portmsg.lock";

enum class MsgFrame : uint32_t { Open = 1, Title, Body, Line, End, Done, Answer };
enum class MsgHead { More, Ok, Refused };

std::string msg_frame(MsgFrame type, const std::string& body);
MsgHead msg_head(const std::string& buf, MsgFrame& type, uint32_t& len);
std::string msg_cap(const std::string& s, size_t cap);

enum class CallKind { Text, Progress, Set, Choose, Close };

struct MsgCall {
    CallKind kind = CallKind::Text;
    std::string title, text;
    bool wait = false;
    int timeout_s = -1;
    int64_t done = 0, total = -1;
};

bool parse_call(const std::vector<std::string>& args, MsgCall& out);
bool msg_quiet(CallKind k);
bool msg_options(const MsgCall& c, std::vector<std::string>& lines);

int msg_band(int row, int picked, int cap);

enum class MsgMode { None, Text, Progress, Choose };

struct MsgContent {
    MsgMode mode = MsgMode::None;
    std::string title;
    std::vector<std::string> lines;
    std::vector<std::string> values;
    int64_t done = -1, total = -1;
};

class MsgView {
public:
    void show(const MsgContent& c, int64_t now);
    void line(const std::string& l, int64_t now);
    void progress(int64_t done, int64_t total, int64_t now);
    bool move(bool down, int64_t now);
    void close(int picked, int64_t now);
    void land(int64_t now);
    bool shown() const { return cur_.c.mode != MsgMode::None; }
    const MsgContent& content() const { return cur_.c; }
    int focus() const { return focus_; }
    int top() const { return top_; }
    bool animating(int64_t now) const;
    int64_t deadline(int64_t now) const;
    void draw(Canvas& c, Fonts& f, GlyphCache& g, InkLayer& ink, const Theme& th, int64_t now,
              std::vector<AlphaRegion>& regions);

private:
    enum class Move { Still, In, Out, Part };
    struct Layer {
        MsgContent c;
        Move move = Move::Still;
        int64_t t0 = 0;
        int cap = 0;
        int picked = -1;
        bool swap = false;
        bool matched = false;
        int64_t link = 0;
        std::vector<char> same, cover;
        int focus = 0;
        int top = 0;
        int log_off = 0;
        bool laid = false;
        std::vector<std::string> rows;
    };

    int band_of(const Layer& l, int row) const;
    int bands(const Layer& l) const;
    int64_t end_of(const Layer& l) const;
    int dx_of(const Layer& l, int band, int64_t now) const;
    fx glow_of(const Layer& l, int band, int64_t now) const;
    int scrim_at(int64_t now) const;
    void scrim_to(int a256, int64_t now, int64_t hold = 0);
    bool ring(const MsgContent& c) const;
    void lay(Layer& l, Fonts& f) const;
    void match(Fonts& f);
    void draw_layer(Layer& l, Canvas& k, Fonts& f, GlyphCache& g, const Theme& th, int64_t now,
                    std::vector<AlphaRegion>& regions);

    Layer cur_;
    std::vector<Layer> gone_;
    int scrim_from_ = 0, scrim_target_ = 0;
    int64_t scrim_t0_ = 0;
    int focus_ = 0, top_ = 0;
    Glide bar_;
    std::vector<Glide> alpha_;
    Glide log_;
    int64_t line_at_ = 0;
    int64_t ring_at_ = -1;
    int64_t ring_done_ = 0, ring_total_ = -1;
    int64_t spin0_ = 0;
    int64_t links_ = 0;
};

class MsgCore {
public:
    MsgView view;
    bool dirty = true;
    std::function<void(int64_t client, int code, const std::string& value)> answer;

    void call(int64_t client, const MsgCall& c, const std::vector<std::string>& options, int64_t now);
    void line(int64_t client, const std::string& l, int64_t now);
    void done(int64_t client);
    void gone(int64_t client);
    void key(PadAction a, int64_t now);
    void tick(int64_t now);
    bool live() const { return live_; }
    uint64_t chooses() const { return chooses_; }
    bool ended(int64_t now) const { return ending_ && !view.shown() && !view.animating(now); }
    int64_t deadline() const { return timeout_at_; }

private:
    void replace();
    void show(int64_t client, CallKind kind, const MsgContent& m, int64_t now);
    void reply(int64_t client, int code, const std::string& value = std::string());

    int64_t owner_ = 0;
    CallKind owner_kind_ = CallKind::Text;
    bool live_ = false;
    bool ending_ = false;
    int64_t timeout_at_ = -1;
    uint64_t chooses_ = 0;
};

class MsgWindow {
public:
    virtual ~MsgWindow() = default;
    virtual bool open() = 0;
    virtual Surface frame() = 0;
    virtual bool ready() = 0;
    virtual void present(const std::vector<AlphaRegion>& regions) = 0;
    virtual int fd() const = 0;
    virtual int event_fd() const = 0;
    virtual bool pump() = 0;
    virtual bool activated() const = 0;
};

struct MsgServerOpts {
    std::function<bool(PadReader&)> open_pad;
};

int serve_portmsg(int listen_fd, MsgWindow& w, Fonts& fonts, const MsgServerOpts& o);
int run_portmsg_client(const MsgCall& call, const std::string& dir, int in_fd, int out_fd,
                       const std::function<void(int listen_fd)>& serve,
                       const std::function<void()>& no_server = std::function<void()>());

}
