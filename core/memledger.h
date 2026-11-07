#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../board/board.h"

namespace fd::mem {

enum class Class : int { None, Decode, Tiles, Rings };
enum class Count : int { TileHits, TileMisses, PendingFrames, Bursts, BurstBytes, Kinds };

constexpr int kSlots = 32;

struct Slot {
    std::atomic<const void*> owner{nullptr};
    std::atomic<int> cls{0};
    std::atomic<size_t> cap{0}, bytes{0};
};

inline Slot g_slots[kSlots] = {};
inline std::atomic<size_t> g_counts[(int)Count::Kinds] = {};
inline std::atomic<bool> g_pending{false};

constexpr size_t pages(size_t n) { return (n + 4095) & ~(size_t)4095; }

inline Slot* find(const void* owner)
{
    for (Slot& s : g_slots)
        if (s.owner.load(std::memory_order_acquire) == owner)
            return &s;
    return nullptr;
}

inline void set(const void* owner, Class c, size_t cap, size_t bytes)
{
    Slot* s = find(owner);
    for (int i = 0; !s && i < kSlots; i++) {
        const void* none = nullptr;
        if (g_slots[i].owner.compare_exchange_strong(none, owner, std::memory_order_acq_rel))
            s = &g_slots[i];
    }
    if (!s)
        return;
    s->cap.store(cap, std::memory_order_relaxed);
    s->bytes.store(bytes, std::memory_order_relaxed);
    s->cls.store((int)c, std::memory_order_release);
}

inline void drop(const void* owner)
{
    Slot* s = find(owner);
    if (!s)
        return;
    s->cls.store(0, std::memory_order_relaxed);
    s->bytes.store(0, std::memory_order_relaxed);
    s->cap.store(0, std::memory_order_relaxed);
    s->owner.store(nullptr, std::memory_order_release);
}

inline size_t live(Class c)
{
    size_t sum = 0;
    for (const Slot& s : g_slots)
        if (s.cls.load(std::memory_order_acquire) == (int)c) {
            size_t b = s.bytes.load(std::memory_order_relaxed), cap = s.cap.load(std::memory_order_relaxed);
            sum += b < cap ? b : cap;
        }
    return sum;
}

inline bool over_cap()
{
    for (const Slot& s : g_slots)
        if (s.cls.load(std::memory_order_acquire) != 0 &&
            s.bytes.load(std::memory_order_relaxed) > pages(s.cap.load(std::memory_order_relaxed)))
            return true;
    return false;
}

constexpr size_t statics() { return board::kMem.glyphs + board::kMem.glyphs_scaled + board::kMem.search_bytes; }

inline size_t charge() { return live(Class::Decode) + live(Class::Tiles) + live(Class::Rings) + statics(); }

inline void count(Count k, size_t n = 1) { g_counts[(int)k].fetch_add(n, std::memory_order_relaxed); }

inline size_t take(Count k) { return g_counts[(int)k].exchange(0, std::memory_order_relaxed); }

inline void pending() { g_pending.store(true, std::memory_order_relaxed); }

inline void frame()
{
    if (g_pending.exchange(false, std::memory_order_relaxed))
        count(Count::PendingFrames);
}

inline std::atomic<long> g_anon{-1}, g_swap{0};
inline int g_beats = 0, g_shadow_over = 0;
inline bool g_shadow_said = false, g_safe_said = false, g_fill = false, g_filling = false;
inline int64_t g_fill_at = 0;
inline std::atomic<size_t> g_decode_peak{0}, g_decode_cap{0};

inline void decode_peak(size_t bytes, size_t cap)
{
    size_t p = g_decode_peak.load(std::memory_order_relaxed);
    while (bytes > p)
        if (g_decode_peak.compare_exchange_weak(p, bytes, std::memory_order_relaxed)) {
            g_decode_cap.store(cap, std::memory_order_relaxed);
            break;
        }
}

inline long net_kb(long anon_kb, long swap_kb) { return anon_kb + swap_kb - (long)(charge() / 1024); }

inline long meminfo_kb(const char* path, const char* key)
{
    FILE* f = fopen(path, "r");
    if (!f)
        return -1;
    char line[256];
    long v = -1;
    size_t n = strlen(key);
    while (fgets(line, sizeof line, f))
        if (strncmp(line, key, n) == 0 && sscanf(line + n, "%ld", &v) == 1)
            break;
    fclose(f);
    return v;
}

inline void start_line(long mem_total_kb)
{
    const board::MemBudget& m = board::kMem;
    printf("funeos: mem budget gate %s, total %ld kB, net %ld kB, tiles %zu, glyphs %zu and %zu, search %zu of %zu "
           "bytes, prefetch %zu, ram %ld kB, MemTotal %ld kB\n",
           m.gate == board::GateMode::Net ? "net" : "total", m.gate_total_kb, m.gate_net_kb, m.tiles, m.glyphs,
           m.glyphs_scaled, m.search, m.search_bytes, m.prefetch, m.ram_kb, mem_total_kb);
    if (mem_total_kb >= 0 && mem_total_kb < m.ram_kb)
        printf("funeos: mem MemTotal %ld kB under the %ld kB this board is sized for\n", mem_total_kb, m.ram_kb);
}

inline bool ten_s_beat() { return ++g_beats % 5 == 0; }

inline void line(uint32_t reads)
{
    long anon = g_anon.load(std::memory_order_relaxed), swap = g_swap.load(std::memory_order_relaxed);
    unsigned long long hits = take(Count::TileHits), misses = take(Count::TileMisses),
                       frames = take(Count::PendingFrames), bursts = take(Count::Bursts),
                       burst_kb = take(Count::BurstBytes) / 1024;
    if (anon < 0)
        return;
    printf("funeos: mem anon %ld kB, swap %ld kB, net %ld kB, decode %zu kB, tiles %zu kB, rings %zu kB, static %zu "
           "kB, tiles %llu hits %llu misses, pending frames %llu, prefetch %llu bursts %llu kB, reads %u\n",
           anon, swap, net_kb(anon, swap), live(Class::Decode) / 1024, live(Class::Tiles) / 1024,
           live(Class::Rings) / 1024, statics() / 1024, hits, misses, frames, bursts, burst_kb, reads);
}

inline long gate_use(long anon_kb, long swap_kb, board::GateMode mode, long total_kb, long net_gate_kb, long* limit)
{
    if (mode == board::GateMode::Net && net_gate_kb > 0) {
        long held = (long)(charge() / 1024);
        if (held <= anon_kb + swap_kb && !over_cap()) {
            *limit = net_gate_kb;
            return anon_kb + swap_kb - held;
        }
        if (!g_safe_said) {
            fprintf(stderr,
                    "funeos: mem charge %ld kB against anon %ld kB, a slot over its cap: %d, the total gate decides\n",
                    held, anon_kb + swap_kb, over_cap() ? 1 : 0);
            g_safe_said = true;
        }
    }
    *limit = total_kb;
    return anon_kb + swap_kb;
}

inline void shadow(long net, long gate_kb)
{
    if (gate_kb <= 0 || net <= gate_kb) {
        g_shadow_over = 0;
        return;
    }
    if (++g_shadow_over >= 2 && !g_shadow_said) {
        fprintf(stderr, "funeos: mem net %ld kB over %ld kB, a net gate would exit\n", net, gate_kb);
        g_shadow_said = true;
    }
}

}
