#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "bidi.h"
#include "canvas.h"

struct stbtt_fontinfo;
struct hb_blob_t;
struct hb_face_t;
struct hb_font_t;
struct hb_buffer_t;

namespace fd {

constexpr int kGlyphCap = 24;
constexpr int kPressGlyphCap = 48;
constexpr int kQuietMs = 500;

enum class Weight { Light, Regular, Bold, SemiLight };
enum class Case { Lower, Upper, Mixed };
enum class Need { Hold, Visible, Predicted, Warm };

struct TextStyle {
    Weight weight = Weight::Regular;
    int px = 12;
    int track64 = 0;
    Case tcase = Case::Mixed;
};

constexpr int64_t kWarmPassNs = 4000000;

struct WarmText {
    TextStyle st;
    std::string s;
    int bucket = 0;
    Dir dir = Dir::Auto;
    bool run = false;
};

struct RangedFace {
    std::string path;
    std::vector<std::pair<uint32_t, uint32_t>> ranges;
    uint32_t script = 0;
    bool joined = false;
};

struct TextLayout;
struct GlyphRun;
struct TextPrefix;

struct GlyphRaster {
    int w = 0, h = 0, xoff = 0, yoff = 0;
    std::vector<uint8_t> cov;
};

inline void warm_add(std::vector<WarmText>& out, const TextStyle& st, const std::string& s, int bucket = 0)
{
    if (!s.empty())
        out.push_back({st, s, bucket});
}

inline void warm_add(std::vector<WarmText>& out, const TextStyle& st, const std::string& s, Dir d)
{
    if (bidi_plain(s, d))
        warm_add(out, st, s);
    else if (!s.empty())
        out.push_back({st, s, 0, d, true});
}

class Fonts {
public:
    static constexpr uint64_t kCopyItem = 0xFFull << 56;
    static constexpr int kPrimFaces = 4;

    Fonts();
    ~Fonts();
    Fonts(const Fonts&) = delete;
    Fonts& operator=(const Fonts&) = delete;

    bool open(const std::string& dir, const std::vector<std::string>& fallback);
    bool open(const std::string& dir, const std::vector<std::string>& fallback, const std::vector<RangedFace>& ranged);
    void use_copy(const std::string& image, const std::string& dir, const std::string& build, bool crashed);
    uint64_t drop_copy();
    void set_copy_card(const std::string& dir);
    void set_copy_hold(bool hold);
    void set_copy_gap(int ms)
    {
        std::lock_guard<std::mutex> lk(mu_);
        copy_gap_ = ms;
    }
    void set_copy_write(std::function<ssize_t(int, const void*, size_t, off_t)> w) { copy_write_ = std::move(w); }
    void set_copy_need(std::function<uint64_t(const std::string& card)> f);
    void set_rootfs(std::function<bool(const std::string&)> f) { rootfs_ = std::move(f); }
    bool copying() const;
    uint32_t lock_tries() const { return lock_tries_.load(); }
    bool start_worker();
    bool worker() const { return th_.joinable(); }
    int event_fd() const { return efd_; }
    bool pump();
    void input();
    void set_busy(bool busy);
    void set_idle_ahead(bool pending);
    void set_ui(bool busy, bool ahead);
    bool warm_queued() const;
    void next_frame()
    {
        frame_++;
        boxes_.clear();
    }
    void shown(bool full);
    uint32_t owed_epoch() const { return owed_epoch_; }
    void set_need(Need n) { need_ = n; }
    bool warm(const TextStyle& st, const char* utf8, Need n, int bucket = 0);
    bool cached(const TextStyle& st, const char* utf8, int bucket = 0);
    void unhold();
    void preload();
    bool faces_ready() const;
    void drop_warm();
    void begin_frame(int raster_cap)
    {
        budget_ = raster_cap;
        press_budget_ = raster_cap < 0 ? -1 : kPressGlyphCap;
    }
    int budget() const { return budget_; }
    uint32_t deferrals() const { return deferrals_; }
    bool owes() const;
    int width64(const TextStyle& st, const char* utf8);
    void ink64(const TextStyle& st, const char* utf8, int& left64, int& right64);
    bool draw(Canvas& c, const TextStyle& st, const char* utf8, int x64, int baseline, uint32_t rgb, int a256);
    bool draw(Canvas& c, const TextStyle& st, const char* utf8, int x64, int baseline, uint32_t rgb, int a256,
              const Zoom& z);
    static int press_buckets(int px);
    static int press_bucket(int px, fx s);
    static std::vector<std::pair<size_t, size_t>> lock_ranges(const uint8_t* data, size_t len, bool shaped = false);
    size_t scaled_bytes() const { return scaled_.bytes; }
    size_t plain_bytes() const { return plain_.bytes; }
    size_t plain_count() const { return plain_.map.size(); }
    size_t scaled_count() const { return scaled_.map.size(); }
    size_t plain_nodes() const { return plain_.lru.size(); }
    uint32_t rasters() const { return rasters_; }
    uint32_t worker_rasters() const { return worker_rasters_.load(); }
    uint32_t boosts() const { return boosts_.load(); }
    uint32_t waits() const { return waits_.load(); }
    uint32_t walked() const { return walked_; }
    uint32_t rests() const;
    uint32_t fallback_misses() const { return misses_; }
    uint32_t fallback_rasters() const { return fallback_rasters_; }
    size_t queued();
    void set_gate(std::function<void(uint64_t key)> g) { gate_ = std::move(g); }
    int ascent(int px) const;
    int descent(int px) const;

    bool covers(uint32_t cp) const;
    std::shared_ptr<const TextLayout> layout(const TextStyle& st, std::string_view s, Dir d);
    GlyphRun run(const TextStyle& st, const TextLayout& l, size_t b, size_t e);
    GlyphRun run(const TextStyle& st, std::string_view s, Dir d);
    int span64(const TextStyle& st, const TextLayout& l, size_t b, size_t e);
    bool prefix64(const TextStyle& st, const TextLayout& l, TextPrefix& p);
    bool draw(Canvas& c, const GlyphRun& r, int x64, int baseline, uint32_t rgb, int a256);
    bool draw(Canvas& c, const GlyphRun& r, int x64, int baseline, uint32_t rgb, int a256, const Zoom& z);
    bool warm(const TextStyle& st, std::string_view s, Dir d, Need n, int bucket = 0);
    bool cached(const TextStyle& st, std::string_view s, Dir d, int bucket = 0);
    GlyphRaster direct(int face, int px, int gid);
    bool glyph_box(int face, int px, int gid, Rect& box);
    void ask_direct(int face, int px, int gid);
    bool take_direct(int face, int px, int gid, GlyphRaster& out);
    void drop_direct(int face, int px, int gid);
    uint32_t direct_rasters() const { return direct_rasters_; }
    uint32_t ahead_rasters() const { return ahead_rasters_.load(); }
    size_t direct_held() const;
    size_t layout_entries() const { return lru_.size(); }
    size_t layout_bytes() const { return lru_bytes_; }
    uint32_t layouts() const { return layouts_; }
    uint32_t hb_objects() const { return hb_made_; }
    size_t face_count() const { return kPrimFaces + fall_.size(); }
    int face_state(int i) const;
    std::string face_file(int i) const;
#ifndef FD_ALSA
    struct DrawRec {
        int x64, base, w64, px;
        Rect clip;
    };
    void record_draws(std::vector<DrawRec>* out) { draws_ = out; }
#endif
    void set_text_runs(bool on) { runs_ = on; }

private:
#ifndef FD_ALSA
    std::vector<DrawRec>* draws_ = nullptr;
#endif
    struct Face {
        std::string path;
        std::vector<uint8_t> data;
        void* map = nullptr;
        size_t len = 0;
        stbtt_fontinfo* info = nullptr;
        int upem = 0, top = 0, bottom = 0;
        std::string image;
        bool copy = false;
        std::vector<std::pair<uint32_t, uint32_t>> ranges;
        uint32_t script = 0;
        bool ranged = false, joined = false;
        std::vector<int16_t> box;
        hb_blob_t* blob = nullptr;
        hb_face_t* hface = nullptr;
        hb_font_t* hfont = nullptr;
        std::atomic<int> state{0};
    };
    struct Layout {
        std::string text;
        Weight weight = Weight::Regular;
        Case tcase = Case::Mixed;
        Dir dir = Dir::Auto;
        size_t bytes = 0;
        std::shared_ptr<const TextLayout> l;
    };
    struct Copy {
        std::string image, dir, file, stamp, tmp, build, card;
        uint64_t size = 0, off = 0;
        int src = -1;
        int64_t next = 0, began = 0;
        bool want = false, hold = false, known = false, stop = false, mapped = false, checked = false, pass = true;
    };
    struct Glyph {
        int w = 0, h = 0, xoff = 0, yoff = 0;
        uint32_t seen = 0;
        std::vector<uint8_t> rot;
        std::list<uint64_t>::iterator lru;
    };
    struct Cache {
        std::unordered_map<uint64_t, Glyph> map;
        std::list<uint64_t> lru;
        size_t bytes = 0, cap = 0, dry_cost = 0;
        uint32_t rev = 0, dry_rev = 0, dry_mark = 0;
    };
    struct Job {
        uint64_t key = 0;
        Need need = Need::Warm;
        uint32_t seq = 0, asked = 0;
        int x = -1, y = -1;
        bool owed = false, kept = false, dropped = false;
    };
    struct Done {
        Job job;
        Glyph g;
    };
    struct Put {
        uint64_t key;
        int x, y;
    };

    bool load(Face& f, bool mapped);
    bool load_face(Face& f, bool worker);
    void wait_face(Face& f);
    void lock_face(Face& f);
    int64_t copy_at(int64_t now);
    void copy_item();
    void copy_end();
    bool resolve(Weight w, uint32_t cp, int& face, int& gid, bool ranged = false);
    bool resolve_ready(Weight w, uint32_t cp, int& face, int& gid, bool ranged = false);
    static bool in_ranges(const Face& f, uint32_t cp);
    hb_font_t* hb(Face& f);
    std::shared_ptr<const TextLayout> lay(const TextStyle& st, std::string_view s, Dir d, bool ready);
    std::shared_ptr<const TextLayout> find_layout(const TextStyle& st, std::string_view s, Dir d, bool ready, int keep);
    bool draw_run(Canvas& c, const GlyphRun& r, int x64, int baseline, uint32_t rgb, int a, const Zoom* z);
    void drop_hb();
    int place(const TextStyle& st, const TextLayout& l, const std::vector<int>& order,
              std::vector<std::pair<int, int>>& at, std::vector<std::pair<int, int>>* starts = nullptr);
    Face& face(int i);
    Glyph* glyph(int face, int px, int gid, int x, int y, bool& pending, bool& miss);
    Glyph* find(Cache& c, uint64_t key);
    Glyph make(int face, int px, int gid, float factor);
    Glyph* insert(Cache& c, uint64_t key, Glyph&& g, bool any);
    Glyph* raster(Cache& c, uint64_t key, int face, int px, int gid, float factor);
    Glyph* claim(Cache& c, uint64_t key, int face, int px, int gid, float factor);
    Glyph* scaled(int face, int px, int gid, int bucket, int x, int y, bool& pending, bool& miss, bool& subst,
                  uint64_t& got);
    void ask(uint64_t key, Need n, bool owed, bool kept, int x, int y);
    void note(const Rect& clip, int x0, int x1, int baseline, int px);
    bool outside(const Rect& clip, int baseline, const TextStyle& st, const char* s) const;
    void boost();
    void signal();
    void run();
    int pick(int64_t now, int64_t& until) const;
    static float factor(int px, int bucket);

    Face prim_[kPrimFaces];
    int prim_at_[kPrimFaces] = {0, 1, 2, 3};
    std::deque<Face> fall_;
    Cache plain_, scaled_;
    int budget_ = -1, press_budget_ = -1;
    uint32_t deferrals_ = 0;
    Need need_ = Need::Visible;
    uint32_t frame_ = 1, mark_ = 1, owed_epoch_ = 0, rasters_ = 0;
    bool owed_in_ = false;
    std::vector<Rect> boxes_;
    std::vector<std::list<uint64_t>::iterator> drop_;
    uint32_t walked_ = 0, misses_ = 0, fallback_rasters_ = 0;

    std::mutex face_mu_;
    std::condition_variable face_cv_;
    std::thread th_;
    mutable std::mutex mu_;
    std::condition_variable cv_, done_cv_;
    std::vector<Job> jobs_;
    std::vector<int> loads_;
    std::vector<Done> done_, direct_done_;
    Job busy_;
    bool busy_on_ = false, busy_nice_ = false, loading_ = false, quit_ = false, ui_busy_ = false, idle_ahead_ = false;
    uint32_t seq_ = 0, rests_ = 0;
    int64_t last_input_ = 0;
    std::atomic<int> tid_{0};
    std::atomic<uint32_t> worker_rasters_{0}, boosts_{0}, waits_{0}, ahead_rasters_{0};
    uint32_t direct_rasters_ = 0;
    std::function<void(uint64_t key)> gate_;
    int efd_ = -1;

    std::mutex copy_mu_;
    Copy copy_;
    int copy_gap_ = 250;
    std::function<ssize_t(int, const void*, size_t, off_t)> copy_write_;
    std::function<uint64_t(const std::string&)> copy_need_;
    std::function<bool(const std::string&)> rootfs_;
    std::atomic<uint32_t> lock_tries_{0};

    hb_buffer_t* hb_buf_ = nullptr;
    uint32_t hb_made_ = 0, layouts_ = 0;
    std::list<Layout> lru_;
    std::unordered_map<uint64_t, std::vector<std::list<Layout>::iterator>> lru_map_;
    size_t lru_bytes_ = 0, lru_high_ = 0;
    std::vector<Put> run_puts_;
    bool runs_ = false;
    std::vector<uint8_t> mask_;
};

}
