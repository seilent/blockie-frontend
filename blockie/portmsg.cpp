#include "portmsg.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <utility>

#include <fcntl.h>
#include <poll.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "repeat.h"

namespace fd {

namespace {

constexpr const char* kKinds[] = {"text", "progress", "set", "choose", "close"};

int64_t now_ms()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

bool number(const std::string& s, int64_t& out)
{
    if (s.empty() || s.size() > 18)
        return false;
    for (char c : s)
        if (c < '0' || c > '9')
            return false;
    out = std::strtoll(s.c_str(), nullptr, 10);
    return true;
}

bool send_all(int fd, const std::string& s)
{
    size_t off = 0;
    while (off < s.size()) {
        ssize_t w = send(fd, s.data() + off, s.size() - off, MSG_NOSIGNAL);
        if (w < 0 && errno == EINTR)
            continue;
        if (w <= 0)
            return false;
        off += (size_t)w;
    }
    return true;
}

bool read_full(int fd, std::string& buf, size_t n)
{
    char tmp[4096];
    while (buf.size() < n) {
        ssize_t r = read(fd, tmp, std::min(sizeof tmp, n - buf.size()));
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            return false;
        buf.append(tmp, (size_t)r);
    }
    return true;
}

bool read_frame(int fd, MsgFrame& type, std::string& body)
{
    std::string head;
    uint32_t len = 0;
    if (!read_full(fd, head, kMsgHeader) || msg_head(head, type, len) != MsgHead::Ok)
        return false;
    body.clear();
    return read_full(fd, body, len);
}

sockaddr_un sock_addr(const std::string& path, bool& ok)
{
    sockaddr_un a = {};
    a.sun_family = AF_UNIX;
    ok = path.size() < sizeof a.sun_path;
    if (ok)
        std::memcpy(a.sun_path, path.c_str(), path.size() + 1);
    return a;
}

int connect_to(const std::string& path)
{
    bool ok = false;
    sockaddr_un a = sock_addr(path, ok);
    if (!ok) {
        errno = ENAMETOOLONG;
        return -1;
    }
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -1;
    if (connect(fd, (const sockaddr*)&a, sizeof a) != 0) {
        int e = errno;
        close(fd);
        errno = e;
        return -1;
    }
    return fd;
}

int listen_on(const std::string& path)
{
    bool ok = false;
    sockaddr_un a = sock_addr(path, ok);
    int fd = ok ? socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0) : -1;
    if (fd < 0)
        return -1;
    if (bind(fd, (const sockaddr*)&a, sizeof a) != 0 || listen(fd, kMsgBacklog) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

class LineIn {
public:
    explicit LineIn(int fd) : fd_(fd) {}

    bool next(std::string& out)
    {
        for (;;) {
            size_t nl = buf_.find('\n');
            if (nl != std::string::npos || (eof_ && (!buf_.empty() || long_))) {
                size_t n = nl == std::string::npos ? buf_.size() : nl;
                out = long_ ? kept_ : msg_cap(buf_.substr(0, n), kMsgLineCap);
                buf_.erase(0, nl == std::string::npos ? n : n + 1);
                long_ = false;
                kept_.clear();
                return true;
            }
            if (eof_)
                return false;
            if (!long_ && buf_.size() > kMsgLineCap) {
                kept_ = msg_cap(buf_, kMsgLineCap);
                long_ = true;
            }
            if (long_)
                buf_.clear();
            char tmp[4096];
            ssize_t r = read(fd_, tmp, sizeof tmp);
            if (r < 0 && errno == EINTR)
                continue;
            if (r <= 0)
                eof_ = true;
            else
                buf_.append(tmp, (size_t)r);
        }
    }

private:
    int fd_;
    std::string buf_, kept_;
    bool eof_ = false, long_ = false;
};

int talk(int fd, const MsgCall& call, const std::vector<std::string>& options, int in_fd, int out_fd)
{
    std::string head = std::string(kKinds[(int)call.kind]) + " " + (call.wait ? "1" : "0") + " " +
                       std::to_string(call.timeout_s);
    std::string out = msg_frame(MsgFrame::Open, head);
    bool titled = call.kind == CallKind::Text || call.kind == CallKind::Progress || call.kind == CallKind::Choose;
    if (titled)
        out += msg_frame(MsgFrame::Title, msg_cap(call.title, kMsgLineCap));
    if (call.kind == CallKind::Text)
        out += msg_frame(MsgFrame::Body, msg_cap(call.text, kMsgTextCap));
    if (call.kind == CallKind::Set)
        out += msg_frame(MsgFrame::Body, std::to_string(call.done) + " " + std::to_string(call.total));
    for (const std::string& o : options)
        out += msg_frame(MsgFrame::Line, o);
    out += msg_frame(MsgFrame::End, "");
    bool ok = send_all(fd, out);
    if (call.kind == CallKind::Progress) {
        LineIn in(in_fd);
        std::string l;
        while (in.next(l))
            ok = ok && send_all(fd, msg_frame(MsgFrame::Line, l));
        ok = ok && send_all(fd, msg_frame(MsgFrame::Done, ""));
    }
    if (!ok)
        return kMsgCancel;
    MsgFrame t;
    std::string body;
    while (read_frame(fd, t, body)) {
        if (t != MsgFrame::Answer)
            continue;
        size_t tab = body.find('\t');
        int code = std::atoi(body.substr(0, tab).c_str());
        if (code != kMsgDone)
            return kMsgCancel;
        if (call.kind == CallKind::Choose && tab != std::string::npos) {
            std::string v = body.substr(tab + 1) + "\n";
            size_t off = 0;
            while (off < v.size()) {
                ssize_t w = write(out_fd, v.data() + off, v.size() - off);
                if (w < 0 && errno == EINTR)
                    continue;
                if (w <= 0)
                    return kMsgCancel;
                off += (size_t)w;
            }
        }
        return kMsgDone;
    }
    return kMsgCancel;
}

struct Conn {
    int64_t id = 0;
    int fd = -1;
    std::string head, body;
    MsgFrame type = MsgFrame::Open;
    uint32_t want = 0;
    bool in_body = false;
    MsgCall call;
    std::vector<std::string> options;
    bool ended = false;
};

class Server {
public:
    Server(int lfd, std::string dir, MsgWindow& w, Fonts& f, const MsgServerOpts& o)
        : lfd_(lfd), dir_(std::move(dir)), w_(w), f_(f), o_(o)
    {
        core_.answer = [this](int64_t id, int code, const std::string& value) {
            for (Conn& c : conns_)
                if (c.id == id)
                    send_all(c.fd, msg_frame(MsgFrame::Answer, std::to_string(code) + "\t" + value));
        };
    }
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    int run();

private:
    void drop(size_t i);
    bool readable(Conn& c, int64_t now);
    void frame(Conn& c, int64_t now);
    void open_pad();
    void close_pad();
    void sync_pad();
    bool finish();

    int lfd_;
    std::string dir_;
    MsgWindow& w_;
    Fonts& f_;
    MsgServerOpts o_;
    MsgCore core_;
    GlyphCache glyphs_;
    InkLayer ink_;
    PadReader pad_;
    Repeat repeat_;
    std::vector<Conn> conns_;
    int64_t next_id_ = 1;
    uint64_t chooses_ = 0;
};

void Server::drop(size_t i)
{
    core_.gone(conns_[i].id);
    close(conns_[i].fd);
    conns_.erase(conns_.begin() + (long)i);
    sync_pad();
}

void Server::open_pad()
{
    if (!pad_.is_open() && o_.open_pad && !o_.open_pad(pad_))
        return;
    pad_.poll_edges();
    repeat_.release_all();
}

void Server::close_pad()
{
    pad_.close();
    repeat_.release_all();
}

void Server::sync_pad()
{
    if (core_.live() && core_.chooses() != chooses_) {
        chooses_ = core_.chooses();
        close_pad();
        open_pad();
    } else if (!core_.live() && pad_.is_open()) {
        close_pad();
    }
}

void Server::frame(Conn& c, int64_t now)
{
    const std::string& b = c.body;
    switch (c.type) {
    case MsgFrame::Open: {
        size_t s1 = b.find(' '), s2 = s1 == std::string::npos ? s1 : b.find(' ', s1 + 1);
        std::string kind = b.substr(0, s1);
        c.call = MsgCall{};
        for (int k = 0; k < 5; k++)
            if (kind == kKinds[k])
                c.call.kind = (CallKind)k;
        c.call.wait = s1 != std::string::npos && b.compare(s1 + 1, 1, "1") == 0;
        long t = s2 == std::string::npos ? -1 : std::strtol(b.c_str() + s2 + 1, nullptr, 10);
        c.call.timeout_s = (int)std::clamp<long>(t, -1, kMsgTimeoutMax);
        c.options.clear();
        c.ended = false;
        break;
    }
    case MsgFrame::Title:
        c.call.title = b;
        break;
    case MsgFrame::Body:
        if (c.call.kind == CallKind::Text)
            c.call.text = b;
        else if (c.call.kind == CallKind::Set) {
            size_t sp = b.find(' ');
            c.call.done = std::atoll(b.c_str());
            c.call.total = sp == std::string::npos ? -1 : std::atoll(b.c_str() + sp + 1);
        }
        break;
    case MsgFrame::Line:
        if (!c.ended && c.call.kind == CallKind::Choose && c.options.size() < kMsgOptionCap)
            c.options.push_back(msg_cap(b, kMsgLineCap));
        else if (c.ended && c.call.kind == CallKind::Progress)
            core_.line(c.id, b, now);
        break;
    case MsgFrame::End:
        if (!c.ended) {
            c.ended = true;
            core_.call(c.id, c.call, c.options, now);
            sync_pad();
        }
        break;
    case MsgFrame::Done:
        if (c.call.kind == CallKind::Progress)
            core_.done(c.id);
        break;
    case MsgFrame::Answer:
        break;
    }
}

bool Server::readable(Conn& c, int64_t now)
{
    char tmp[4096];
    for (;;) {
        size_t need = c.in_body ? c.want - c.body.size() : kMsgHeader - c.head.size();
        if (c.in_body && need == 0) {
            frame(c, now);
            c.in_body = false;
            c.head.clear();
            c.body.clear();
            continue;
        }
        ssize_t r = read(c.fd, tmp, std::min(sizeof tmp, need));
        if (r < 0 && errno == EINTR)
            continue;
        if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return true;
        if (r <= 0)
            return false;
        if (c.in_body) {
            c.body.append(tmp, (size_t)r);
            continue;
        }
        c.head.append(tmp, (size_t)r);
        if (c.head.size() < kMsgHeader)
            continue;
        if (msg_head(c.head, c.type, c.want) != MsgHead::Ok)
            return false;
        c.in_body = true;
    }
}

int Server::run()
{
    if (!w_.open())
        return 1;
    std::vector<AlphaRegion> regions;
    for (;;) {
        int64_t now = now_ms();
        if (core_.ended(now) && conns_.empty() && finish())
            return 0;
        int64_t due = core_.view.deadline(now);
        std::vector<pollfd> fds;
        fds.push_back({lfd_, POLLIN, 0});
        int wi = w_.fd() >= 0 ? (int)fds.size() : -1;
        if (wi >= 0)
            fds.push_back({w_.fd(), POLLIN, 0});
        int ei = w_.event_fd() >= 0 ? (int)fds.size() : -1;
        if (ei >= 0)
            fds.push_back({w_.event_fd(), POLLIN, 0});
        int pi = pad_.is_open() ? (int)fds.size() : -1;
        if (pi >= 0)
            fds.push_back({pad_.fd(), POLLIN, 0});
        size_t ci = fds.size();
        for (const Conn& c : conns_)
            fds.push_back({c.fd, POLLIN, 0});
        int timeout = -1;
        auto until = [&](int64_t when) {
            if (when < 0)
                return;
            int ms = (int)std::max<int64_t>(0, when - now);
            timeout = timeout < 0 ? ms : std::min(timeout, ms);
        };
        until(due);
        until(core_.deadline());
        until(repeat_.deadline());
        if (core_.dirty)
            until(now + (w_.ready() ? 0 : kMsgFrameMs));
        if (poll(fds.data(), (nfds_t)fds.size(), timeout) < 0 && errno != EINTR)
            return 1;
        now = now_ms();
        if (due >= 0 && now >= due)
            core_.dirty = true;
        if (wi >= 0 && (fds[(size_t)wi].revents & (POLLIN | POLLHUP | POLLERR)) && !w_.pump())
            return 0;
        if (ei >= 0 && (fds[(size_t)ei].revents & POLLIN)) {
            uint64_t n = 0;
            ssize_t r = read(w_.event_fd(), &n, sizeof n);
            (void)r;
        }
        for (size_t i = conns_.size(); i-- > 0;) {
            short rv = fds[ci + i].revents;
            if (rv & (POLLIN | POLLHUP | POLLERR))
                if (!readable(conns_[i], now))
                    drop(i);
        }
        if (fds[0].revents & POLLIN) {
            int c = accept4(lfd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
            if (c >= 0) {
                Conn n;
                n.id = next_id_++;
                n.fd = c;
                conns_.push_back(std::move(n));
            }
        }
        if (pi >= 0 && pad_.is_open() && (fds[(size_t)pi].revents & (POLLIN | POLLHUP | POLLERR))) {
            std::vector<PadEdge> edges = pad_.poll_edges();
            if (!w_.activated()) {
                edges.clear();
                repeat_.release_all();
            }
            for (const PadEdge& e : edges) {
                if (!e.down) {
                    repeat_.release(e.action, kFromPad, now);
                    continue;
                }
                core_.key(e.action, now);
                if (is_direction(e.action) && core_.live())
                    repeat_.press(e.action, kFromPad, now);
            }
            sync_pad();
        }
        PadAction ra = PadAction::Up;
        if (repeat_.due(now, ra))
            core_.key(ra, now);
        core_.tick(now);
        sync_pad();
        if ((core_.dirty || core_.view.animating(now)) && w_.ready()) {
            Surface s = w_.frame();
            Canvas c(s);
            core_.view.draw(c, f_, glyphs_, ink_, theme(), now, regions);
            w_.present(regions);
            core_.dirty = false;
        }
    }
}

bool Server::finish()
{
    int lock = open((dir_ + kMsgLock).c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (lock < 0)
        return false;
    int rc;
    while ((rc = flock(lock, LOCK_EX)) != 0 && errno == EINTR) {
    }
    int c = rc == 0 ? accept4(lfd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC) : -1;
    bool done = rc == 0 && c < 0;
    if (c >= 0) {
        Conn n;
        n.id = next_id_++;
        n.fd = c;
        conns_.push_back(std::move(n));
    }
    if (done) {
        unlink((dir_ + kMsgSock).c_str());
        close(lfd_);
    }
    close(lock);
    return done;
}

}

std::string msg_frame(MsgFrame type, const std::string& body)
{
    uint32_t h[2] = {(uint32_t)type, (uint32_t)body.size()};
    std::string out((const char*)h, sizeof h);
    out += body;
    return out;
}

MsgHead msg_head(const std::string& buf, MsgFrame& type, uint32_t& len)
{
    if (buf.size() < kMsgHeader)
        return MsgHead::More;
    uint32_t h[2];
    std::memcpy(h, buf.data(), sizeof h);
    if (h[1] > kMsgFrameCap)
        return MsgHead::Refused;
    type = (MsgFrame)h[0];
    len = h[1];
    return MsgHead::Ok;
}

std::string msg_cap(const std::string& s, size_t cap)
{
    if (s.size() <= cap)
        return s;
    size_t cut = cap;
    while (cut > 0 && ((unsigned char)s[cut] & 0xC0) == 0x80)
        cut--;
    return s.substr(0, cut);
}

bool parse_call(const std::vector<std::string>& args, MsgCall& out)
{
    out = MsgCall{};
    int modes = 0;
    bool titled = false, timed = false;
    for (size_t i = 0; i < args.size(); i++) {
        const std::string& a = args[i];
        bool more = i + 1 < args.size();
        int64_t v = 0;
        if (a == "--text" && more) {
            out.kind = CallKind::Text;
            out.text = args[++i];
            modes++;
        } else if (a == "--title" && more) {
            out.title = args[++i];
            titled = true;
        } else if (a == "--timeout" && more && number(args[i + 1], v) && v <= kMsgTimeoutMax) {
            out.timeout_s = (int)v;
            timed = true;
            i++;
        } else if (a == "--wait") {
            out.wait = true;
        } else if (a == "--progress") {
            out.kind = CallKind::Progress;
            modes++;
        } else if (a == "--progress-set" && i + 2 < args.size() && number(args[i + 1], out.done) &&
                   (args[i + 2] == "-" || number(args[i + 2], out.total))) {
            out.kind = CallKind::Set;
            if (args[i + 2] == "-")
                out.total = -1;
            i += 2;
            modes++;
        } else if (a == "--choose") {
            out.kind = CallKind::Choose;
            modes++;
        } else if (a == "--close") {
            out.kind = CallKind::Close;
            modes++;
        } else {
            return false;
        }
    }
    if (modes != 1)
        return false;
    if ((out.wait || timed) && out.kind != CallKind::Text)
        return false;
    if (titled && (out.kind == CallKind::Set || out.kind == CallKind::Close))
        return false;
    return out.kind != CallKind::Choose || titled;
}

bool msg_quiet(CallKind k) { return k == CallKind::Close || k == CallKind::Set; }

bool msg_options(const MsgCall& c, std::vector<std::string>& lines)
{
    if (c.kind != CallKind::Choose) {
        lines.clear();
        return true;
    }
    if (lines.size() > kMsgOptionCap)
        lines.resize(kMsgOptionCap);
    for (std::string& l : lines)
        l = msg_cap(l, kMsgLineCap);
    return !lines.empty();
}

void MsgCore::reply(int64_t client, int code, const std::string& value)
{
    if (client && answer)
        answer(client, code, value);
}

void MsgCore::replace()
{
    if (owner_ && owner_kind_ != CallKind::Progress)
        reply(owner_, kMsgCancel);
    owner_ = 0;
    live_ = false;
    timeout_at_ = -1;
}

void MsgCore::show(int64_t client, CallKind kind, const MsgContent& m, int64_t now)
{
    replace();
    view.show(m, now);
    ending_ = false;
    owner_ = client;
    owner_kind_ = kind;
    dirty = true;
}

void MsgCore::call(int64_t client, const MsgCall& c, const std::vector<std::string>& options, int64_t now)
{
    MsgContent m;
    m.title = msg_cap(c.title, kMsgLineCap);
    switch (c.kind) {
    case CallKind::Text:
        m.mode = MsgMode::Text;
        m.lines = {msg_cap(c.text, kMsgTextCap)};
        show(client, c.kind, m, now);
        if (c.timeout_s >= 0)
            timeout_at_ = now + (int64_t)std::min(c.timeout_s, kMsgTimeoutMax) * 1000;
        if (!c.wait) {
            owner_ = 0;
            reply(client, kMsgDone);
        }
        break;
    case CallKind::Progress:
        m.mode = MsgMode::Progress;
        show(client, c.kind, m, now);
        break;
    case CallKind::Set:
        if (view.content().mode != MsgMode::Progress) {
            m.mode = MsgMode::Progress;
            m.title = view.content().title;
            show(client, c.kind, m, now);
            owner_ = 0;
        }
        view.progress(c.done, c.total, now);
        dirty = true;
        reply(client, kMsgDone);
        break;
    case CallKind::Choose: {
        m.mode = MsgMode::Choose;
        std::vector<std::string> opts = options;
        msg_options(c, opts);
        for (const std::string& l : opts) {
            size_t tab = l.find('\t');
            m.values.push_back(l.substr(0, tab));
            m.lines.push_back(tab == std::string::npos ? l : l.substr(tab + 1));
        }
        show(client, c.kind, m, now);
        live_ = !m.values.empty();
        chooses_++;
        break;
    }
    case CallKind::Close:
        replace();
        view.close(-1, now);
        ending_ = true;
        dirty = true;
        reply(client, kMsgDone);
        break;
    }
}

void MsgCore::line(int64_t client, const std::string& l, int64_t now)
{
    if (owner_ != client || owner_kind_ != CallKind::Progress || view.content().mode != MsgMode::Progress)
        return;
    view.line(msg_cap(l, kMsgLineCap), now);
    dirty = true;
}

void MsgCore::done(int64_t client)
{
    reply(client, owner_ != client ? kMsgCancel : kMsgDone);
    if (owner_ == client)
        owner_ = 0;
}

void MsgCore::gone(int64_t client)
{
    if (owner_ == client) {
        owner_ = 0;
        live_ = false;
    }
}

void MsgCore::key(PadAction a, int64_t now)
{
    if (!live_)
        return;
    if (a == PadAction::Up || a == PadAction::Down) {
        if (view.move(a == PadAction::Down, now))
            dirty = true;
        return;
    }
    if (a == PadAction::Accept && owner_) {
        view.land(now);
        int k = view.focus();
        const std::vector<std::string>& v = view.content().values;
        reply(owner_, kMsgDone, k >= 0 && k < (int)v.size() ? v[(size_t)k] : std::string());
        owner_ = 0;
        live_ = false;
        view.close(k, now);
        dirty = true;
    }
}

void MsgCore::tick(int64_t now)
{
    if (timeout_at_ < 0 || now < timeout_at_)
        return;
    if (owner_ && owner_kind_ == CallKind::Text) {
        reply(owner_, kMsgDone);
        owner_ = 0;
    }
    replace();
    view.close(-1, now);
    ending_ = true;
    dirty = true;
}

int serve_portmsg(int listen_fd, MsgWindow& w, Fonts& fonts, const MsgServerOpts& o)
{
    int fl = fcntl(listen_fd, F_GETFL);
    if (fl >= 0)
        fcntl(listen_fd, F_SETFL, fl | O_NONBLOCK);
    sockaddr_un a = {};
    socklen_t len = sizeof a;
    std::string dir;
    if (getsockname(listen_fd, (sockaddr*)&a, &len) == 0) {
        std::string path(a.sun_path, strnlen(a.sun_path, sizeof a.sun_path));
        size_t tail = std::strlen(kMsgSock);
        if (path.size() > tail && path.compare(path.size() - tail, tail, kMsgSock) == 0)
            dir = path.substr(0, path.size() - tail);
    }
    Server s(listen_fd, dir, w, fonts, o);
    return s.run();
}

int run_portmsg_client(const MsgCall& call, const std::string& dir, int in_fd, int out_fd,
                       const std::function<void(int listen_fd)>& serve, const std::function<void()>& no_server)
{
    std::vector<std::string> options;
    if (call.kind == CallKind::Choose) {
        LineIn in(in_fd);
        std::string l;
        while (in.next(l))
            if (options.size() < kMsgOptionCap)
                options.push_back(l);
    }
    if (!msg_options(call, options))
        return kMsgUsage;
    mkdir(dir.c_str(), 0700);
    std::string sock = dir + kMsgSock;
    int lock = open((dir + kMsgLock).c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (lock < 0)
        return kMsgCancel;
    int rc;
    while ((rc = flock(lock, LOCK_EX)) != 0 && errno == EINTR) {
    }
    if (rc != 0) {
        close(lock);
        return kMsgCancel;
    }
    int fd = connect_to(sock);
    bool none = fd < 0 && (errno == ECONNREFUSED || errno == ENOENT);
    bool quiet = msg_quiet(call.kind);
    if (none && !quiet) {
        if (no_server)
            no_server();
        unlink(sock.c_str());
        int lfd = listen_on(sock);
        pid_t pid = lfd >= 0 ? fork() : -1;
        if (pid == 0) {
            close(lock);
            if (lfd < 3) {
                int moved = fcntl(lfd, F_DUPFD_CLOEXEC, 3);
                close(lfd);
                lfd = moved;
            }
            int nul = open("/dev/null", O_RDWR | O_CLOEXEC);
            if (nul >= 0) {
                dup2(nul, in_fd);
                dup2(nul, out_fd);
                dup2(nul, 2);
                if (nul > 2)
                    close(nul);
            }
            if (lfd > 3)
                close_range(3, (unsigned)lfd - 1, 0);
            close_range((unsigned)lfd + 1, ~0U, 0);
            serve(lfd);
            _exit(0);
        }
        if (lfd >= 0)
            close(lfd);
        if (pid > 0)
            fd = connect_to(sock);
    }
    flock(lock, LOCK_UN);
    close(lock);
    if (none && quiet)
        return kMsgDone;
    if (fd < 0)
        return kMsgCancel;
    rc = talk(fd, call, options, in_fd, out_fd);
    close(fd);
    return rc;
}

}
