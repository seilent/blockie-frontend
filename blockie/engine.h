#pragma once
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "../gfx/canvas.h"
#include "rowruns.h"

namespace fd {
namespace eng {

enum class Ink { Art, Text };

constexpr uint16_t kRingBit = 0x8000;
constexpr uint16_t kIdMask = 0x7FFF;
constexpr int kFull = 255 << 8;
constexpr int kArtStep = 30;
constexpr int kTextStep = 63;
constexpr int kDimStep = 12;
constexpr int kRingStep = 30;
constexpr int kAgeMs = 100;
constexpr int kTailMs = 150;
constexpr int kFoldMaxMs = 800;
constexpr int kStepThirds = 250;
constexpr int kPageSteps = 3;
constexpr int kPaneSid = 0;
constexpr int kPageSid = 1;

void blend_row(uint32_t* p, int n, uint32_t ground, int a);

fx step_progress(int64_t t3, int steps);

struct Shade {
    int t = kFull;
    int end = kFull;
    int step = kArtStep;
    int sigma = 256;
    bool dep = false;
};

struct Block {
    uint16_t id = 0;
    Ink ink = Ink::Art;
    int z = 0;
    int source = 0;
    Rect src;
    bool tile = false;
};

struct Pose {
    int block = 0;
    Rect rect;
    Rect clip;
};

struct Frame {
    std::vector<Block> blocks;
    std::vector<Pose> poses;
    std::vector<Shade> shades;
    Rect ground;
};

class Instance {
public:
    explicit Instance(Rect motion, bool argb = false);
    Rect rect() const { return rect_; }
    bool argb() const { return argb_; }
    void begin(int64_t t0, bool reversed);
    void prepare(int rows);
    bool prepared() const { return prep_row_ >= rect_.y1 - rect_.y0; }
    void advance(int64_t t0, int64_t now);
    void skip();
    void compose(const Surface& dst, const Frame& f, const std::vector<Surface>& sources, uint32_t ground, int clip_x,
                 const Frame* start = nullptr);
    bool settled() const { return inited_ && settled_; }
    bool ready() const { return ready_; }
    void set_segments(bool on)
    {
        seg_ = on;
        seg_ok_ = false;
    }
    int segment_frames() const { return seg_frames_; }
    void set_verify(bool on) { verify_ = on; }
    int verify_scans() const { return verify_scans_; }
    int verify_fails() const { return verify_fails_; }
    int adopts() const { return adopts_; }
    int adopt_fails() const { return adopt_fails_; }
    int adopt_cleared() const { return adopt_cleared_; }
    int adopt_nostart() const { return adopt_nostart_; }
    int adopt_drawn() const { return adopt_drawn_; }
    int adopt_fail_x() const { return adopt_fail_x_; }
    int adopt_fail_y() const { return adopt_fail_y_; }
    void adopt_start(bool on, bool gap = true, bool rule = true)
    {
        adopt_ = on;
        adopt_gap_ = gap;
        adopt_rule_ = rule;
    }
    void adopt_source(int k) { adopt_src_ = k; }
    void keep_any(bool on) { keep_any_ = on; }
    void set_probe(bool on) { probe_ = on; }
    bool prepare_start(const Surface& dst, const Frame& start, const std::vector<Surface>& sources, uint32_t ground,
                       int clip_x, int rows);
    char start_key(const Surface& dst, const Frame& start, const std::vector<Surface>& sources, uint32_t ground,
                   int clip_x) const;
    bool start_ready() const { return start_ready_; }
    bool start_busy() const { return start_ready_ || s1_row_ >= 0; }
    void drop_start()
    {
        start_ready_ = false;
        s1_row_ = -1;
    }
    char ps() const { return ps_; }
    int adopt_struct() const { return adopt_struct_; }
    struct AdoptRef {
        int id = -1;
        int source = -1;
        int ident = -1;
        int t = -1;
        int s1 = -1;
    };
    const AdoptRef& adopt_ref() const { return adopt_ref_; }
    void keep_cover(bool on) { keep_cover_ = on; }
    bool cover_on() const { return keep_cover_; }
    void trust_dst(bool on)
    {
        trust_ = on;
        tdst_ = nullptr;
    }
    void invalidate()
    {
        tdst_ = nullptr;
        adopt_cleared_ += adopt_;
        adopt_ = false;
        if (clock_us)
            invalidated_ = true;
    }
    struct Times {
        int64_t begin = 0;
        int64_t build = 0;
        int64_t start = 0;
        int64_t pass = 0;
        int starts = 0;
        int passes = 0;
        uint32_t start_why = 0;
        uint32_t trust_why = 0;
        int64_t start_marks = 0;
        int64_t start_fills = 0;
        int64_t start_build = 0;
        int s1_starts = 0;
        int64_t start_warm = 0;
        int start_warms = 0;
    };
    std::function<int64_t()> clock_us;
    const Times& times() const { return times_; }
    void add_build(int64_t us) { times_.build += us; }
    int trusted_frames() const { return trust_frames_; }
    int held_frames() const { return held_frames_; }
    int seed_frames() const { return seed_frames_; }
    bool inited() const { return inited_; }
    int64_t last() const { return last_; }
    int64_t t0() const { return t0_; }
    bool reversed() const { return reversed_; }
    const std::vector<uint16_t>& e() const { return e_; }
    const std::vector<uint16_t>& ids() const { return ids_; }
    const std::vector<uint8_t>& age() const { return age_; }
    const std::vector<uint16_t>& held() const { return held_; }
    const std::vector<int16_t>& act_lo() const { return act_lo_; }
    const std::vector<int16_t>& act_hi() const { return act_hi_; }
    const std::vector<uint8_t>& cover() const
    {
        assert(keep_cover_);
        return cover_;
    }

private:
    struct Slot {
        int t = 0;
        int end = 0;
        int step = 0;
        int sigma = 256;
        int bdx = 0;
        int bdy = 0;
        int lut = 0;
        uint8_t kind = 0;
        uint8_t still = 0;
        bool dep = false;
        bool on = false;
        int pose = -1;
        bool keep = false;
    };

    struct Seg {
        uint16_t id;
        uint16_t e;
        uint16_t held;
        uint8_t age;

        bool operator==(const Seg& o) const { return id == o.id && e == o.e && held == o.held && age == o.age; }
    };
    using Run = RowRuns<Seg>::Run;

    void pass(const Surface& dst, const Frame& f, const std::vector<Surface>& sources, uint32_t ground, int clip_x,
              bool write = true);
    void seg_build();
    void s1_emit_row(int ry);
    void s1_finish();

    Rect rect_;
    bool argb_ = false;
    std::vector<uint16_t> e_;
    std::vector<uint16_t> ids_;
    std::vector<uint16_t> next_;
    std::vector<uint8_t> age_;
    std::vector<uint16_t> held_;
    std::vector<int16_t> lo_;
    std::vector<int16_t> hi_;
    std::vector<int16_t> act_lo_;
    std::vector<int16_t> act_hi_;
    std::vector<int64_t> sig_;
    std::vector<int64_t> sig_now_;
    std::vector<int> slot_;
    std::vector<uint8_t> cover_;
    std::vector<uint16_t> back_;
    std::vector<uint8_t> kind_;
    std::vector<uint16_t> e_prev_;
    std::vector<uint8_t> age_prev_;
    std::vector<int32_t> ox_;
    std::vector<int32_t> oy_;
    std::vector<uint8_t> has_o_;
    std::vector<int> bdx_;
    std::vector<int> bdy_;
    std::vector<Slot> tab_;
    RowRuns<Seg> segs_;
    RowRuns<Seg> nsegs_;
    std::vector<std::vector<std::pair<int, int>>> covs_;
    struct S1Span {
        int u, v;
        uint16_t id;
    };
    std::vector<std::vector<S1Span>> s1_;
    bool fresh_ = true;
    std::vector<int> lut_sigma_;
    std::vector<uint16_t> lut_;
    std::vector<size_t> order_;
    std::vector<uint8_t> still_;
    std::vector<uint8_t> posed_;
    bool back_due_ = false;
    int xr_ = -1;
    int64_t t0_ = -1;
    int64_t last_ = -1;
    int64_t due_ = -1;
    bool reversed_ = false;
    bool inited_ = false;
    bool settled_ = false;
    bool ready_ = false;
    bool seg_ = false;
    bool seg_ok_ = false;
    int seg_frames_ = 0;
    bool trust_ = false;
    int trust_frames_ = 0;
    int held_frames_ = 0;
    bool verify_ = false;
    int verify_scans_ = 0;
    int verify_fails_ = 0;
    bool adopt_ = false;
    bool adopt_gap_ = true;
    bool adopt_rule_ = true;
    bool last_s1_ = false;
    bool keep_any_ = false;
    bool probe_ = false;
    bool s1_defer_ = false;
    int s1_row_ = -1;
    int s1_gh_ = 0;
    int s1_gw_ = 0;
    int s1_mx0_ = 0;
    int s1_my0_ = 0;
    int s1_y0_ = 0;
    int s1_y1_ = 0;
    bool start_ready_ = false;
    char ps_ = '-';
    Frame seed_;
    std::vector<const void*> seed_src_;
    uint32_t seed_ground_ = 0;
    int seed_clip_ = 0;
    int seed_w_ = 0;
    int seed_h_ = 0;
    std::vector<uint8_t> owns_;
    std::vector<uint16_t> owned_;
    int adopt_src_ = 0;
    int adopt_struct_ = 0;
    AdoptRef adopt_ref_;
    int adopts_ = 0;
    int adopt_fails_ = 0;
    int adopt_cleared_ = 0;
    int adopt_nostart_ = 0;
    int adopt_drawn_ = 0;
    int adopt_fail_x_ = -1;
    int adopt_fail_y_ = -1;
    bool keep_cover_ = true;
    int seed_frames_ = 0;
    const void* tdst_ = nullptr;
    bool invalidated_ = false;
    uint32_t pass_why_ = 0;
    int64_t phase_[3] = {};
    int prep_row_ = 0;
    size_t prep_size_ = 0;
    Times times_;
    int tw_ = 0;
    int th_ = 0;
    struct Fill {
        int out = 0;
        int in = 0;
        int y0 = 0;
        int y1 = 0;
        int at(int y) const { return y >= y0 && y < y1 ? in : out; }
    };
    Fill tfill_;
    Fill sfill_;
    bool start_fill_ = false;
    int tend_ = 0;
    uint32_t tground_ = 0;
    std::vector<const void*> tsrc_;
    std::vector<int32_t> tsx_;
    std::vector<int32_t> tsy_;
    std::vector<int> tsi_;
    std::vector<int> tsg_;
    std::vector<int> pose_at_;
};

class PoseLog {
public:
    bool open(const std::string& path, const std::string& dump_dir, const std::string& prefix = "");
    bool on() const { return f_ != nullptr; }
    void surface(int sid, const char* kind, const char* lang, Rect r);
    void row(int sid, const char* name, Rect r, Ink ink);
    void fold(int64_t t0, const char* form, const char* kind, bool reversed, int64_t d3, const Frame& f,
              bool collapse = false, bool redeal = false, int sid = -1,
              const std::vector<std::string>* names = nullptr);
    bool frame(int n, int png, int64_t tf, int64_t ft, int rest, const Frame& f, const Instance& in, int boundary = -1,
               int sid = -1);
    bool close();

private:
    FILE* f_ = nullptr;
    std::string dump_;
    std::string prefix_;
};

}
}
