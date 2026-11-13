#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "../gfx/canvas.h"
#include "rowruns.h"
#include "tile_grid.h"

namespace fd {

enum class GrowClock { None, G, G6, GS };

enum class LeftMove { Fade, T5, Stepper };

constexpr int kBlockFoldMs = 800;
constexpr int kBlockTailMs = 150;
constexpr int64_t block_left_for(int64_t d, int64_t pane_end) { return (d > pane_end ? d : pane_end) + kBlockTailMs; }

struct Block {
    int64_t id = 0;
    int game = 0;
    bool wide = false;
    int col = 0;
    int row = 0;
    Rect cell;
    bool art = false;
    bool focus = false;
};

struct BlockPose {
    int block = 0;
    Rect rect;
    Rect clip;
    bool top = false;
    Rect ring;
    int a256 = 256;
    bool beneath = false;
};

struct GlowShade {
    int t = 0;
    int end = 0;
    int step = 0;
    int sigma = 256;
    bool dep = false;
    const uint16_t* lut = nullptr;
};

struct GlowRun {
    uint8_t was;
    uint8_t age;
    uint8_t hid;
    uint16_t e;

    bool operator==(const GlowRun& o) const { return was == o.was && age == o.age && hid == o.hid && e == o.e; }
};

struct Glow {
    std::vector<uint16_t> e;
    std::vector<uint8_t> age;
    std::vector<uint8_t> ids;
    std::vector<uint8_t> cur;
    std::vector<uint8_t> spare;
    std::vector<int16_t> lo;
    std::vector<int16_t> hi;
    std::vector<int16_t> act_lo;
    std::vector<int16_t> act_hi;
    std::vector<uint16_t> back;
    std::vector<int> low;
    std::vector<int> low_now;
    std::vector<uint16_t> back_t;
    std::vector<GlowShade> shades;
    std::vector<BlockPose> poses;
    std::vector<Rect> covers;
    std::vector<int> spans;
    std::vector<int> tmp;
    std::vector<int64_t> sig_now;
    std::vector<uint8_t> hid;
    std::vector<uint8_t> hid_now;
    std::vector<Rect> ring_hid;
    std::vector<std::vector<std::pair<int, int>>> cover_rows;
    RowRuns<GlowRun> runs;
    RowRuns<GlowRun> runs_next;
    std::vector<std::pair<int, int>> cover_row;
    std::vector<std::pair<int, int>> ring_row;
    std::vector<int> bounds;
    std::vector<int> extra;
    bool runs_ok = false;
    int runs_built = 0;
    int run_pieces = 0;
    int runs_aged = 0;
    int64_t t0 = -1;
    int64_t last = -1;
    int64_t due = -1;
    int64_t due_t0 = -1;
    int64_t flip_s3 = 0;
    bool inited = false;
    bool ready = false;
    bool flip = false;
    bool settled = false;
    bool copied = false;
    bool seg = false;
    bool verify = false;
    bool dry = false;
    int verify_scans = 0;
    int back_scans = 0;
    int runs_scans = 0;
    int verify_fails = 0;
    int seg_frames = 0;
    int aging_frames = 0;
    int seg_hits[7] = {};
    int seed_frames = 0;
    uint64_t gen = 0;
    int prep_w = 0;
    int prep_h = 0;
    int prep_row = 0;
    uint64_t prep_target = 0;
    bool prep_fresh = false;
    char l2 = '-';
    struct Times {
        int64_t reset = 0;
        int64_t seed = 0;
        int64_t frame = 0;
        int64_t seed_raster = 0;
        int64_t seed_glow = 0;
        int64_t frame_raster = 0;
        int64_t frame_glow = 0;
        int64_t seed_past = 0;
        uint32_t seed_why = 0;
        int seed_marks = 0;
        int runs_builds = 0;
        int guard_seeds = 0;
        int guard_pass = 0;
    } times;
    std::function<int64_t()> clock_us;
    std::vector<int64_t> guard_sig;
    std::vector<int> guard_t;
    bool guard_open = false;
};

struct LeftPlan {
    Rect header;
    GridSpec grid = grid_spec(TileMode::Standard);
    int grid_y = 0;
    Rect band{0, kBandTop, kPaneSplitWide, kBandBottom};
    Rect focus_from;
    Rect focus_to;
    bool collapse = false;
    bool prepared = false;
    std::vector<Rect> kept;
    std::vector<Rect> leaves;
    std::vector<Rect> bands;
    GrowClock grow = GrowClock::None;
    int grow_q = 0;
    bool from_set = false;
    GridSpec from_grid = grid_spec(TileMode::Standard);
    int from_y = 0;
    Rect from_focus;
    bool redeal = false;
    uint32_t focus_ink = 0;
    std::vector<Block> blocks;
    std::vector<uint8_t> full_rows;
    bool glow_on = false;
    bool reversed = false;
    int64_t fold = kBlockFoldMs;
    int64_t pane_end = 0;
    mutable const std::vector<uint8_t>* cover = nullptr;
    mutable Rect cover_rect;
    mutable Glow glow;
    std::shared_ptr<Glow> shared;
    uint64_t gen = 0;
};

struct SwapLeg {
    int dx = 0;
    int a256 = 0;
};

constexpr int kStepThirds = 250;
constexpr int kStepBeats = 3;
constexpr int kSwapCells = 2;
constexpr uint32_t kBlockBg = 0x01000000;
constexpr int kGlowFull = 255 << 8;
constexpr int kGlowDim = (int)(((int64_t)tok::motion_quickplayAlpha * 255) >> kFxBits) << 8;
constexpr int kGlowStepFull = 30;
constexpr int kGlowStepDim = 18;
constexpr int kGlowDimStep = 12;
constexpr int kGlowRingStep = 30;
constexpr int kGlowAgeMs = 100;
constexpr int kGlowRing = 0x80;
constexpr int kSwapSigma = alpha256(tok::motion_quickplayAlpha);

constexpr int64_t swap_ms()
{
    return fx_round(tok::motion_swapOut) + std::max(fx_round(tok::motion_swapRows), fx_round(tok::motion_swapRowFade));
}

constexpr int64_t stepper_max_ms() { return ((int64_t)kStepBeats * kStepThirds + 2) / 3; }

fx cubic_out(fx t);
SwapLeg swap_leave(int64_t t, int dir);
SwapLeg swap_arrive(int64_t t, int dir);
int swap_header(int64_t t, bool collapse);
void swap_prepare(LeftPlan& p, const Surface& a, const Surface& b);
void swap_compose(const Surface& a, const Surface& b, const Surface& dst, const LeftPlan& p, int64_t t, int x_end,
                  uint32_t ground);

bool block_form(LeftMove m);
void block_rows(LeftPlan& p);
int block_beats(const LeftPlan& p);
int64_t block_thirds(const LeftPlan& p);
int64_t block_ms(const LeftPlan& p);
int64_t block_time(const LeftPlan& p, int64_t ft);
int64_t block_left_ms(const LeftPlan& p);
int block_rest(const LeftPlan& p, int64_t ft, int64_t tl);
Rect block_clip(const LeftPlan& p, int boundary);
std::vector<BlockPose> block_legs(const LeftPlan& p, int64_t tl, int boundary);
std::vector<BlockPose> block_poses(const LeftPlan& p, int64_t ft, int64_t tl, int boundary);
Rect block_box(const LeftPlan& p, const BlockPose& q);
Rect block_cover(const LeftPlan& p, const BlockPose& q);
void block_paint(const Surface& a, const Surface& b, const Surface& dst, const LeftPlan& p,
                 const std::vector<BlockPose>& poses, int x_end, uint32_t ground);
Rect block_glow_rect(const LeftPlan& p);
Rect glow_rect_of(Rect band);
bool glow_prepare(Glow& g, uint64_t gen, Rect gr, int rows);
int run_end(const uint8_t* p, int x, int end);
int cell_edge_after(int x);
int block_key(const LeftPlan& p, int block);
int block_sigma(const LeftPlan& p, const Block& k);
bool block_departs(const LeftPlan& p, int block);
void glow_reset(Glow& g);
Glow& glow_of(const LeftPlan& p);
const Glow& glow_view(const LeftPlan& p);
uint32_t block_shade(uint32_t src, uint32_t ground, int e, int sigma);
void block_advance(const LeftPlan& p, int64_t t0, int64_t now);
bool block_settled(const LeftPlan& p);
void block_skip(const LeftPlan& p);
void block_compose(const Surface& a, const Surface& b, const Surface& dst, const LeftPlan& p, int64_t ft, int64_t tl,
                   int boundary, int x_end, uint32_t ground);

}
