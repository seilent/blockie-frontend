#pragma once
#include <cstddef>
#include <cstdint>

namespace fd::board {

enum class VolumeLaw { Attenuation, Index };
enum class Gadget { AndroidUsb, Configfs };
enum class UpdateKind { Ap80Iso, V1Ota, A105Ab, None };
enum class KeyMap { Nav, Icons };
enum class JackKind { None, Switch, Extcon };
enum class Display { Fbdev, Drm, Wayland };
enum class LedKind { Pattern, Pwm2 };

struct EqCost {
    double band, pcm, dsd[4], out[5], pipe[5], ldac, budget;
};

struct LedPwm {
    int red = 0, blue = 0;
    bool breathe = false;
};

struct CodecTable {
    const char* file;
    const char* port;
    size_t bytes;
};

enum class GateMode { Total, Net };

struct MemBudget {
    GateMode gate;
    long gate_total_kb, gate_net_kb;
    bool ledger;
    size_t tiles, glyphs, glyphs_scaled, search, search_bytes;
    uint32_t read_ahead, read_chunk;
    size_t prefetch, art_source, bt_art_source;
    long ram_kb;
};

}
