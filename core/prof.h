#pragma once
#include <cstdint>
#include <cstring>
#include <ctime>
#include <sys/resource.h>

namespace fd::prof {

enum Sec : uint8_t {
    Rest,
    Events,
    Page,
    Layer0,
    Layer1,
    Ground,
    Fill,
    Project,
    ProjectSprite,
    DrawSprite,
    Swivel,
    Scrim,
    ScrimShifted,
    ScrimScaled,
    Mix,
    BlendScaled,
    BlendScaledPremul,
    MixScaledPremul,
    MipQp,
    MipMenu,
    MipSprite,
    MipBuild,
    Coverage,
    Warp,
    BlendBand,
    BlendShifted,
    Shade,
    Backdrop,
    Glyph,
    Copy,
    Wait,
    PanPress,
    kSecs
};

enum Src : uint8_t { Input, Timer, Art, Library, Link, Bt, kSrcs };

inline constexpr const char* kSecName[kSecs] = {"rest", "events", "page", "layer0", "layer1", "ground", "fill",
                                                "project", "project_sprite", "draw_sprite", "swivel", "scrim",
                                                "scrim_shifted", "scrim_scaled", "mix", "blend_scaled",
                                                "blend_scaled_premul", "mix_scaled_premul", "mip_qp", "mip_menu",
                                                "mip_sprite", "mip_build", "coverage_scaled", "warp", "blend_band",
                                                "blend_shifted", "shade_backdrop", "backdrop", "glyph", "copy", "wait",
                                                "pan_press"};
inline constexpr const char* kSrcName[kSrcs] = {"input", "timer", "art", "library", "link", "bt"};

struct Acc {
    int64_t ns[kSecs];
    uint32_t n[kSecs];
    int64_t src[kSrcs];
};

struct Frame {
    Acc work{}, ev{};
    int64_t work_ns = 0, ev_ns = 0, cpu_ns = -1;
    long nivcsw = -1;
};

constexpr int kSlices = 32;
constexpr int kDepth = 16;

struct State {
    int depth = 0, skip = 0, src_depth = 0;
    Sec stack[kDepth];
    Src src_stack[kDepth];
    int64_t at = 0, src_at = 0;
    Acc* acc = nullptr;
    Acc work;
    struct Slice {
        int64_t start, end;
        Acc a;
    } slices[kSlices];
    int nslices = 0;
    bool ev_open = false;
    int64_t pan_end = -1, cpu0 = -1, res_ns = 0;
    long csw0 = -1;
};

inline bool g_on = false;
inline State g;
inline thread_local bool tl_main = false;

inline int64_t clock_ns(clockid_t id)
{
    timespec t;
    clock_gettime(id, &t);
    return (int64_t)t.tv_sec * 1000000000 + t.tv_nsec;
}

inline int64_t mono_ns() { return clock_ns(CLOCK_MONOTONIC); }

inline long nivcsw()
{
    rusage u;
    return getrusage(RUSAGE_THREAD, &u) == 0 ? u.ru_nivcsw : -1;
}

constexpr int64_t kCoarseNs = 1000000;

inline int64_t cpu_resolution()
{
    int64_t best = -1;
    for (int k = 0; k < 3; k++) {
        int64_t t0 = mono_ns(), a = clock_ns(CLOCK_THREAD_CPUTIME_ID), b = a;
        while (b == a && mono_ns() - t0 < 30000000)
            b = clock_ns(CLOCK_THREAD_CPUTIME_ID);
        if (b > a && (best < 0 || b - a < best))
            best = b - a;
    }
    return best;
}

inline void set_main() { tl_main = true; }

inline void set(bool on)
{
    if (on && !g_on && !g.ev_open && !g.acc) {
        g.depth = g.skip = g.src_depth = 0;
        g.nslices = 0;
        g.pan_end = g.cpu0 = -1;
        g.csw0 = -1;
        if (!g.res_ns)
            g.res_ns = cpu_resolution();
    }
    g_on = on;
}

inline bool coarse() { return g.res_ns < 0 || g.res_ns >= kCoarseNs; }

inline bool enter_at(Sec s, int64_t t)
{
    if (!tl_main || !g.acc)
        return false;
    if (g.depth == kDepth) {
        g.skip++;
        return true;
    }
    if (g.depth)
        g.acc->ns[g.stack[g.depth - 1]] += t - g.at;
    g.stack[g.depth++] = s;
    g.acc->n[s]++;
    g.at = t;
    return true;
}

inline void leave_at(int64_t t)
{
    if (g.skip) {
        g.skip--;
        return;
    }
    if (!g.depth || !g.acc)
        return;
    g.acc->ns[g.stack[--g.depth]] += t - g.at;
    g.at = t;
}

inline bool enter(Sec s) { return tl_main && g.acc && enter_at(s, mono_ns()); }
inline void leave() { leave_at(mono_ns()); }

struct Scope {
    bool in;
    explicit Scope(Sec s) : in(g_on && enter(s)) {}
    ~Scope()
    {
        if (in)
            leave();
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};

inline bool src_enter_at(Src s, int64_t t)
{
    if (!tl_main || !g.ev_open || g.src_depth == kDepth)
        return false;
    Acc& a = g.slices[g.nslices - 1].a;
    if (g.src_depth)
        a.src[g.src_stack[g.src_depth - 1]] += t - g.src_at;
    g.src_stack[g.src_depth++] = s;
    g.src_at = t;
    return true;
}

inline void src_leave_at(int64_t t)
{
    if (!g.src_depth || !g.ev_open)
        return;
    g.slices[g.nslices - 1].a.src[g.src_stack[--g.src_depth]] += t - g.src_at;
    g.src_at = t;
}

inline bool src_begin(Src s) { return g_on && s < kSrcs && tl_main && g.ev_open && src_enter_at(s, mono_ns()); }

inline void src_end(bool in)
{
    if (in)
        src_leave_at(mono_ns());
}

struct Source {
    bool in;
    explicit Source(Src s) : in(src_begin(s)) {}
    ~Source() { src_end(in); }
    Source(const Source&) = delete;
    Source& operator=(const Source&) = delete;
};

inline void ev_open_at(int64_t t)
{
    if (!g_on || !tl_main || g.ev_open || g.acc)
        return;
    if (g.nslices == kSlices) {
        g.nslices--;
    } else {
        memset(&g.slices[g.nslices].a, 0, sizeof(Acc));
        g.slices[g.nslices].start = t;
    }
    g.nslices++;
    g.acc = &g.slices[g.nslices - 1].a;
    g.ev_open = true;
    g.depth = g.skip = g.src_depth = 0;
    enter_at(Events, t);
}

inline void ev_close_at(int64_t t)
{
    if (!g.ev_open)
        return;
    while (g.src_depth)
        src_leave_at(t);
    while (g.depth)
        leave_at(t);
    g.slices[g.nslices - 1].end = t;
    g.acc = nullptr;
    g.ev_open = false;
    g.skip = 0;
}

inline void ev_open()
{
    if (g_on)
        ev_open_at(mono_ns());
}

inline void ev_close()
{
    if (g.ev_open)
        ev_close_at(mono_ns());
}

inline void idle()
{
    if (!g.ev_open)
        g.nslices = 0;
}

inline void pan_returned(int64_t t)
{
    if (t > g.pan_end)
        g.pan_end = t;
}

inline bool frame_begin_at(int64_t t)
{
    if (!g_on || !tl_main)
        return false;
    ev_close_at(t);
    memset(&g.work, 0, sizeof g.work);
    g.acc = &g.work;
    g.depth = g.skip = 0;
    enter_at(Rest, t);
    g.cpu0 = clock_ns(CLOCK_THREAD_CPUTIME_ID);
    g.csw0 = nivcsw();
    return true;
}

inline void frame_end_at(int64_t t, Frame& out)
{
    out = Frame{};
    if (g.acc != &g.work)
        return;
    while (g.depth)
        leave_at(t);
    g.acc = nullptr;
    g.skip = 0;
    out.cpu_ns = clock_ns(CLOCK_THREAD_CPUTIME_ID) - g.cpu0;
    long c = nivcsw();
    out.nivcsw = c >= 0 && g.csw0 >= 0 ? c - g.csw0 : -1;
    out.work = g.work;
    for (int s = 0; s < kSecs; s++)
        out.work_ns += g.work.ns[s];
    for (int i = 0; i < g.nslices; i++) {
        const State::Slice& sl = g.slices[i];
        if (sl.end <= g.pan_end)
            continue;
        out.ev_ns += sl.end - sl.start;
        for (int s = 0; s < kSecs; s++) {
            out.ev.ns[s] += sl.a.ns[s];
            out.ev.n[s] += sl.a.n[s];
        }
        for (int s = 0; s < kSrcs; s++)
            out.ev.src[s] += sl.a.src[s];
    }
    g.nslices = 0;
}

}
