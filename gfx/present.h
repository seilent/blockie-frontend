#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <linux/fb.h>
#include <mutex>
#include <pthread.h>
#include <string>
#include <vector>

#include "canvas.h"
#ifdef FD_BOARD_A105
#include <deque>
#include <memory>

#include "drm.h"
#include "glclient.h"

struct drm_event_vblank;
#endif

namespace fd {

enum class PresentMode { Direct, Copy, Warp };
enum class Pace { None, Kick, Wait, Te };

constexpr unsigned long kFbSleep = 0x80044620;
constexpr unsigned long kFbWaitDone = 0x80044630;
constexpr const char* kFlipMarker = "/sys/module/soc_fb/parameters/funeos";
constexpr uint16_t kPoison = 0xF81F;

using FbIoctl = int (*)(int fd, unsigned long req, void* arg);

uint16_t rgb565(uint32_t rgb);

extern std::atomic<uint32_t> g_present_check_fails;

struct PanDone {
    uint32_t seq;
    int64_t write_ns, pan_ns, end_ns;
    bool ok = true;
#ifdef FD_BOARD_A105
    int64_t gpu_ns = -1, ready_ns = -1, post_ns = -1;
#endif
};

struct PaceSeam {
    int64_t (*now)(void* ctx) = nullptr;
    void (*sleep_until)(void* ctx, int64_t ns) = nullptr;
    void (*wrote)(void* ctx, int r0, int r1, int px) = nullptr;
    int64_t (*cpu)(void* ctx) = nullptr;
    void* ctx = nullptr;
};

enum class PresentOp : uint8_t { Full, Cover, Dirty, Warp, Turn, Flip };

struct PresentCall {
    PresentOp op;
    Rect r;
};

struct PaceFrame {
    int64_t spin_ns = -1, wake_ns = -1, gate_ns = 0;
    bool late = false, unlocked = false, missed = false, heavy = false, deferred = false;
    int risky = 0, gates = 0, dropped = 0;
};

template <class F, class L> class PresenterT;

#ifdef FD_ROT
template <> class PresenterT<X888, Rot> {
public:
    using S = SurfaceT<X888, Rot>;

    ~PresenterT();

    bool open(const std::string& spec, bool screen);
    bool open_fb(const std::string& spec, int fd, FbIoctl io, void* map);
    PresentMode mode() const { return mode_; }
    bool kick() const { return kick_; }
    Pace pace() const { return pace_; }
    bool paced() const { return te_on(); }
    bool pace_tripped() const { return te_off_; }
    int64_t pace_period() const { return te_t_; }
    void set_pace_seam(const PaceSeam& s) { seam_ = s; }
    void set_tefail(const std::string& path) { tefail_ = path; }
    void begin_frame(int64_t now, int64_t target);
    int64_t next_tick(int64_t now, int64_t fallback) const;
    bool take_pace(PaceFrame& f);
    std::string describe() const;
    int64_t show_at(int64_t now_ms, bool = true) const { return now_ms + show_lead_ms_; }
    int period_ms() const { return 16; }
    void set_show_lead(int ms) { show_lead_ms_ = ms; }

    S draw_target() const { return mode_ == PresentMode::Direct ? fb_ : back_; }
    S warp_target() const { return mode_ == PresentMode::Copy ? back_ : fb_; }
    S turn_target() const { return te_on() && mode_ != PresentMode::Direct ? back_ : warp_target(); }
    S screen() const { return fb_; }
    S last() const { return {}; }

    bool pan_records() const { return false; }
    int64_t wait_ns() const { return 0; }
    uint32_t waits() const { return 0; }
    uint32_t pans() const { return pans_; }
    uint32_t posted_seq() const { return 0; }
    int event_fd() const { return -1; }
    bool ready() { return true; }
    bool claim() { return true; }
    bool take_done(PanDone&) { return false; }
    bool pacing() const { return false; }
    uint32_t frame_presents() const { return frame_presents_; }
    uint32_t fulls() const { return fulls_; }
    uint32_t diff_misses() const { return 0; }

    void begin_full() {}
    void begin_cover(Rect) {}
    void present(Rect portrait_dirty);
    void present_warp();
    void present_turn();
    bool fb_burst() const { return te_on() && mode_ != PresentMode::Direct; }
    bool burst_rows(const std::function<void(int, int)>& fn);
    void flip()
    {
        frame_presents_ = presents_;
        presents_ = 0;
    }
    bool page_free() { return true; }
    void sleep(bool on);
    void flush() {}
    void stop() {}

private:
    bool mode_from(const std::string& spec);
    bool attach(int fd, FbIoctl io, void* map);
    void copy_rows(Rect mem);
    void pan();
    bool te_on() const { return pace_ == Pace::Te && !te_off_ && !te_kick_ && fd_ >= 0; }
    int64_t clock_now() const;
    int64_t clock_cpu() const;
    void clock_sleep(int64_t ns);
    int64_t spin();
    void sample(int64_t l);
    int64_t l_after(int64_t t) const;
    bool fresh(int64_t now) const;
    int64_t due(int64_t pan) const;
    void note_work();
    int64_t work_pred() const;
    void sync();
    bool acquire();
    void wrote(int r0, int r1, Rect mem);
    bool heavy(Rect mem) const;
    bool burst(Rect mem, const std::function<void(int, int)>* fn = nullptr);
    void write(Rect mem);
    int64_t gate(int row_end, int64_t l) const;
    void trip(int64_t ns);
    void te_reset();

    PresentMode mode_ = PresentMode::Copy;
    bool kick_ = true;
    Pace pace_ = Pace::Kick;
    PaceSeam seam_;
    std::string tefail_;
    bool te_off_ = false;
    bool te_kick_ = false;
    bool te_logged_ = false;
    bool synced_ = false;
    int64_t te_l_ = -1;
    int64_t te_t_ = 16672600;
    std::vector<int64_t> te_iv_;
    int te_strikes_ = 0;
    bool te_ok_ = false;
    bool te_grid_ = false;
    int64_t te_pan_ = -1;
    int64_t te_last_pan_ = -1;
    int64_t te_sync_l_ = -1;
    int64_t te_sample_at_ = -1;
    int64_t spin_at_ = -1;
    int64_t frame_at_ = -1;
    int64_t work_[8] = {};
    int work_n_ = 0;
    int64_t kpx_ns_ = 51200;
    bool heavy_last_ = false;
    Rect pending_{};
    PaceFrame pf_;
    int fd_ = -1;
    FbIoctl io_ = nullptr;
    bool own_ = false;
    fb_var_screeninfo var_{};
    void* map_ = nullptr;
    size_t len_ = 0;
    uint32_t pans_ = 0;
    std::vector<uint32_t> back_buf_;
    std::vector<uint32_t> mem_fb_;
    uint32_t presents_ = 0;
    uint32_t frame_presents_ = 0;
    uint32_t fulls_ = 0;
    int show_lead_ms_ = 0;
    S back_;
    S fb_;
};

#endif

template <> class PresenterT<R565, Rows> {
public:
    using S = SurfaceT<R565, Rows>;

    ~PresenterT();

    void record(std::vector<PresentCall>* v) { rec_ = v; }
    void set_marker(const std::string& path) { marker_ = path; }
    bool open(const std::string& spec, bool screen);
    bool open_fb(const std::string& spec, int fd, FbIoctl io, void* map);
    bool open_fb(const std::string& spec, int fd, FbIoctl io, void* map, bool threaded);
    bool open_fb(const std::string& spec, int fd, FbIoctl io, void* map, bool threaded, bool flip);
    PresentMode mode() const { return mode_; }
    bool kick() const { return kick_; }
    std::string describe() const;
    bool flips() const { return two_; }
    void set_tefail(const std::string&) {}
    void begin_frame(int64_t, int64_t) {}
    int64_t next_tick(int64_t, int64_t fallback) const { return fallback; }
    bool take_pace(PaceFrame&) { return false; }
    int64_t show_at(int64_t now_ms, bool learn = true);
    int period_ms();
    void set_show_lead(int ms) { show_lead_ms_ = ms; }
    void set_clock(int64_t (*f)()) { clock_ = f; }

    S draw_target();
    S warp_target() { return draw_target(); }
    S turn_target() { return warp_target(); }
    S screen() const;
    S last() const;
    Rect flipped() const { return flipped_; }
    int shown_page() const { return shown_; }

    bool pan_records() const { return fd_ >= 0; }
    int64_t wait_ns() const { return wait_ns_; }
    uint32_t waits() const { return waits_; }
    uint32_t pans() const { return pans_; }
    uint32_t posted_seq() const { return seq_; }
    uint32_t failed_pans() const { return failed_pans_; }
    uint32_t poison_hits() const { return poison_hits_; }
    uint32_t diff_misses() const { return diff_misses_; }
    uint32_t frame_presents() const { return frame_presents_; }
    uint32_t fulls() const { return fulls_; }
    int event_fd() const { return efd_; }
    bool ready();
    bool claim();
    bool pacing();
    bool take_done(PanDone& d);

    void begin_full();
    void begin_cover(Rect r);
    void present(Rect portrait_dirty);
    void present_warp();
    void present_turn();
    bool fb_burst() const { return false; }
    bool burst_rows(const std::function<void(int, int)>&) { return false; }
    void flip();
    bool page_free();
    void sleep(bool on);
    void flush();
    void stop();

private:
    void note(PresentOp op, Rect r = {})
    {
        if (rec_)
            rec_->push_back({op, r});
    }

    std::vector<PresentCall>* rec_ = nullptr;
    enum class Phase { Idle, Writing, Panning };
    enum class Pages { Auto, One, Two };

    bool mode_from(const std::string& spec);
    bool attach(int fd, FbIoctl io, void* map, bool threaded);
    void start();
    static void* entry(void* self);
    void run();
    bool busy() const;
    void wait_free(std::unique_lock<std::mutex>& lk);
    void take_targets();
    void copy_rect(uint16_t* dst, const uint16_t* src, Rect r);
    void fill_rect(uint16_t* dst, Rect r, uint16_t v);
    void write_rows(Rect r, bool first);
    bool pan_to(int page, uint32_t seq, int64_t write_ns);
    void learn_pan(int64_t write_ns, int64_t t1, int64_t t2);
    int64_t clk() const;
    void check_post(int page, Rect allowed);
    void end_frame();

    PresentMode mode_ = PresentMode::Copy;
    bool kick_ = true;
    Pages want_pages_ = Pages::Auto;
    std::string marker_ = kFlipMarker;
    int fd_ = -1;
    FbIoctl io_ = nullptr;
    bool own_ = false;
    fb_var_screeninfo var_{};
    void* map_ = nullptr;
    size_t len_ = 0;
    bool two_ = false;
    bool rampage_ = false;
    bool check_ = false;
    int line_ = kPortraitW;
    uint16_t* page_[2] = {};
    std::vector<uint16_t> ram_;
    std::vector<uint16_t> back_;
    std::vector<uint16_t> rambuf_;
    int64_t free_delay_ns_ = 0;
    bool first_ = true;
    int shown_ = 0;
    int hidden_ = 1;
    int last_ = -1;
    Rect stale_[2]{};
    Rect saved_stale_{};
    bool open_frame_ = false;
    bool full_ = false;
    bool in_ram_ = false;
    Rect cover_{};
    Rect dirty_{};
    Rect flipped_{};
    uint32_t presents_ = 0;
    uint32_t frame_presents_ = 0;
    uint32_t fulls_ = 0;
    int64_t wait_ns_ = 0;
    uint32_t waits_ = 0;
    uint32_t seq_ = 0;
    std::atomic<uint32_t> pans_{0};
    uint32_t failed_pans_ = 0;
    bool failed_ = false;
    uint32_t poison_hits_ = 0;
    uint32_t diff_misses_ = 0;
    int efd_ = -1;
    bool thread_ = false;
    bool threaded_ = false;
    pthread_t tid_{};
    fb_var_screeninfo pan_var_{};
    std::mutex mu_;
    std::condition_variable cv_;
    Phase phase_ = Phase::Idle;
    bool posted_ = false;
    bool waiting_ = false;
    bool quit_ = false;
    uint32_t slot_seq_ = 0;
    int slot_page_ = 0;
    Rect slot_rect_{};
    bool slot_first_ = false;
    std::vector<PanDone> done_;
    int show_lead_ms_ = 0;
    int64_t (*clock_)() = nullptr;
    int64_t vsync_ns_ = -1, vsync_period_ns_ = 16666667;
    int64_t write_t0_ns_ = 0, pan_t1_ns_ = 0, pan_est_ns_ = 16700000, write_est_ns_ = 300000;
    int64_t rest_est_ns_ = 4000000, show_call_ns_ = 0;
};

template <> class PresenterT<X888, Rows> {
public:
    using S = SurfaceT<X888, Rows>;

    void open(PresentMode mode)
    {
        mode_ = mode;
        if (mode_ != PresentMode::Direct) {
            back_buf_.assign((size_t)kPortraitW * kPortraitH, 0);
            back_ = {back_buf_.data(), kPortraitW};
        }
        mem_fb_.assign((size_t)kPortraitW * kPortraitH, 0);
        fb_ = {mem_fb_.data(), kPortraitW};
    }
    PresentMode mode() const { return mode_; }
    int64_t show_at(int64_t now_ms, bool = true) const { return now_ms + show_lead_ms_; }
    int period_ms() const { return 16; }
    void set_show_lead(int ms) { show_lead_ms_ = ms; }

    S draw_target() const { return mode_ == PresentMode::Direct ? fb_ : back_; }
    S warp_target() const { return mode_ == PresentMode::Copy ? back_ : fb_; }
    S screen() const { return mode_ == PresentMode::Warp && !warped_ ? back_ : fb_; }
    S last() const { return {}; }
    uint32_t frame_presents() const { return frame_presents_; }
    uint32_t fulls() const { return fulls_; }

    void present(Rect dirty)
    {
        presents_++;
        bool full = covers(dirty);
        if (full)
            fulls_++;
        if (mode_ == PresentMode::Copy || (mode_ == PresentMode::Warp && warped_ && !full))
            copy_rows(dirty);
        if (full)
            warped_ = false;
    }
    void present_warp()
    {
        presents_++;
        fulls_++;
        if (mode_ == PresentMode::Copy)
            copy_rows(kScreen);
        warped_ = true;
    }
    void flip()
    {
        frame_presents_ = presents_;
        presents_ = 0;
    }

private:
    static bool covers(Rect r) { return r.x0 <= 0 && r.y0 <= 0 && r.x1 >= kPortraitW && r.y1 >= kPortraitH; }
    void copy_rows(Rect p)
    {
        p = p.intersect(kScreen);
        if (p.empty())
            return;
        for (int y = p.y0; y < p.y1; y++)
            copy_px(fb_.row(y) + p.x0, back_.row(y) + p.x0, p.x1 - p.x0);
    }

    PresentMode mode_ = PresentMode::Copy;
    std::vector<uint32_t> back_buf_;
    std::vector<uint32_t> mem_fb_;
    uint32_t presents_ = 0;
    uint32_t frame_presents_ = 0;
    uint32_t fulls_ = 0;
    int show_lead_ms_ = 0;
    bool warped_ = false;
    S back_;
    S fb_;
};

#ifdef FD_BOARD_A105
struct GpuSlotSize {
    int w = 0, h = 0;
};

struct GpuStats {
    bool up = false, gone = false;
    uint32_t frames = 0, flips = 0, drops = 0, skips = 0, late = 0, hung = 0, exits = 0, retries = 0, caps = 0;
    uint64_t upload = 0;
    int fences = 0, ready = 0;
};

class DrmPresenter {
public:
    using S = SurfaceT<X888, Rows>;

    ~DrmPresenter();

    bool open(const std::string& spec, bool screen);
    bool open_drm(const std::string& spec, int fd, DrmIoctl io, DrmMap map);
    void record(std::vector<PresentCall>* v) { rec_ = v; }
    void set_tefail(const std::string&) {}
    void begin_frame(int64_t, int64_t) { scope_ = on_; }
    int64_t next_tick(int64_t, int64_t fallback) const { return fallback; }
    bool take_pace(PaceFrame&) { return false; }
    PresentMode mode() const { return mem_.mode(); }
    bool kick() const { return kick_; }
    std::string describe() const;
    int64_t show_at(int64_t now_ms, bool learn = true) const { return mem_.show_at(now_ms, learn); }
    int period_ms() const { return mem_.period_ms(); }
    void set_show_lead(int ms) { mem_.set_show_lead(ms); }

    S draw_target() const { return mem_.draw_target(); }
    S warp_target() const { return mem_.warp_target(); }
    S turn_target() const { return warp_target(); }
    S screen() const;
    S last() const { return mem_.last(); }
    S shown(uint32_t* seq) const;

    bool pan_records() const { return on_; }
    int64_t convert_ns() const { return convert_ns_; }
    int64_t wait_ns() const { return wait_ns_; }
    uint32_t waits() const { return waits_; }
    uint32_t pans() const { return pans_; }
    uint32_t posted_seq() const { return seq_; }
    uint32_t missed() const { return missed_; }
    uint32_t busy() const { return busy_; }
    uint32_t stale() const { return stale_; }
    uint32_t failed() const { return failed_; }
    uint32_t blind() const { return blind_; }
    bool deaf() const { return deaf_; }
    int event_fd() const { return efd_; }
    bool ready();
    bool claim();
    bool take_done(PanDone& d);
    bool pacing() const { return false; }
    uint32_t frame_presents() const { return mem_.frame_presents(); }
    uint32_t fulls() const { return mem_.fulls(); }
    uint32_t diff_misses() const { return 0; }

    void begin_full() { note(PresentOp::Full); }
    void begin_cover(Rect r) { note(PresentOp::Cover, r); }
    void present(Rect portrait_dirty);
    void present_warp();
    void present_turn();
    bool fb_burst() const { return false; }
    bool burst_rows(const std::function<void(int, int)>&) { return false; }
    void flip();
    void sleep(bool on);
    void flush();
    void stop();

    void gpu_open(const GlConfig& c, const std::vector<GpuSlotSize>& slots, bool nogpu);
    bool gpu_up() const { return gpu_up_.load(); }
    uint32_t* gpu_layer(int slot, int* stride);
    bool gpu_layers(int slot, uint32_t** px, int* stride);
    const uint32_t* gpu_layer_now(int slot, int* stride, bool other = false) const;
    void gpu_layer_done(int slot, int64_t bytes = 0);
    void gpu_files();
    void gpu_retry(int64_t now, bool idle);
    int64_t gpu_retry_at(bool idle) const;
    bool gpu_present(const GlFrameCmd& cmd);
    GpuStats gpu_stats() const;
    int gpu_fails() const { return gl_ ? gl_->fail_count() : 0; }

private:
    void note(PresentOp op, Rect r = {})
    {
        if (rec_)
            rec_->push_back({op, r});
    }
    struct Buf {
        uint32_t handle = 0, fb = 0, pitch = 0;
        uint64_t size = 0;
        uint32_t* px = nullptr;
        int fd = -1, w = 0, h = 0;
        uint32_t id = 0, last = 0;
    };
    enum class GState { Free, Gpu, Ready, Flipping, Shown };
    struct Slot {
        Buf b[2];
        int cur = -1, writing = -1;
    };

    bool drm_attach(const std::string& spec, int fd, DrmIoctl io, DrmMap map, bool own);
    bool drm_setup();
    bool drm_buffer(Buf& b, int w, int h, bool scanout, bool prime);
    void drm_free(Buf& b);
    Buf& drm_buf(int i) { return i < 2 ? buf_[i] : gbuf_[i - 2]; }
    int drm_page(int b, uint32_t seq);
    void gpu_tick();
    bool gpu_kept(const GlClient& g) const;
    bool gpu_busy() const;
    void gpu_flip();
    void gpu_retire();
    void gpu_lost(const GlClient& g);
    int drm_io(unsigned long req, void* arg);
    bool drm_start();
    static void* drm_entry(void* self);
    void drm_run();
    void drm_lost(const char* why);
    void drm_done(const drm_event_vblank& e);
    void drm_frame(Rect portrait);
    void drm_post(std::unique_lock<std::mutex>& lk, int b);
    void drm_flip(int b);
    bool drm_expire();
    void drm_settle(std::unique_lock<std::mutex>& lk);
    void drm_dpms(uint64_t v);
    void drm_close();

    std::vector<PresentCall>* rec_ = nullptr;
    PresenterT<X888, Rows> mem_;
    bool kick_ = false;
    bool on_ = false;
    bool own_drm_ = false;
    bool master_ = false;
    int dfd_ = -1;
    int efd_ = -1;
    int qfd_ = -1;
    DrmIoctl dio_ = nullptr;
    DrmMap dmap_ = nullptr;
    uint32_t crtc_ = 0, conn_ = 0, dpms_prop_ = 0;
    int64_t period_ns_ = 0;
    Buf buf_[2];
    Buf gbuf_[2];
    GState gst_[2] = {GState::Free, GState::Free};
    uint32_t gseq_[2] = {};
    int64_t gpost_[2] = {}, gready_[2] = {}, flip_gpu_ns_ = -1, flip_ready_ns_ = -1, flip_post_ns_ = -1;
    std::vector<Slot> slots_;
    std::unique_ptr<GlClient> gl_;
    std::atomic<GlClient*> glp_{nullptr};
    int retry_buf_ = 1;
    int gfd_ = -1;
    std::vector<std::pair<uint32_t, GlFrameCmd>> mail_, mail_out_;
    std::string gpu_off_;
    std::atomic<bool> gpu_up_{false};
    bool gpu_gone_ = false, gpu_imported_ = false, gpu_keep_ = false, cpu_full_ = false;
    int64_t gpu_retry_at_ = -1;
    bool gpu_retry_wait_ = false, gpu_prepped_ = false;
    std::atomic<bool> gpu_park_{false};
    uint32_t cpu_seq_ = 0;
    int cpu_last_ = 0;
    GpuStats gstats_;
    Rect owed_[2];
    int front_ = 0;
    bool pending_ = false;
    int pend_buf_ = 0;
    uint32_t pend_seq_ = 0, front_seq_ = 0;
    uint32_t seq_ = 0;
    int64_t flip_ns_ = 0;
    int64_t conv_ns_ = 0;
    int64_t convert_ns_ = 0;
    int64_t wait_ns_ = 0;
    uint32_t waits_ = 0;
    bool retry_ = false;
    bool dark_ = false;
    bool scope_ = false, held_ = false;
    int scope_buf_ = 0;
    int last_err_ = 0;
    bool dthread_ = false;
    pthread_t dtid_{};
    mutable std::mutex mu_;
    std::condition_variable cv_;
    bool waiting_ = false;
    std::vector<PanDone> done_;
    std::atomic<bool> deaf_{false};
    std::atomic<uint32_t> pans_{0}, missed_{0}, busy_{0}, stale_{0}, failed_{0}, blind_{0};
};

static_assert(board::kDisplay == board::Display::Drm, "the a105 presents through drm");
using Presenter = DrmPresenter;
#elif !defined(FD_BOARD_HEADER)
using Presenter = PresenterT<LiveF, LiveL>;
#endif

}
