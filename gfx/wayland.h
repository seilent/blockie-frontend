#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "mempresent.h"

struct wl_display;
struct wl_registry;
struct wl_compositor;
struct wl_shm;
struct wl_surface;
struct wl_buffer;
struct wl_shm_pool;
struct wl_callback;
struct wl_array;
struct wl_seat;
struct wl_keyboard;
struct wl_touch;
struct xdg_wm_base;
struct xdg_surface;
struct xdg_toplevel;
struct zwlr_layer_shell_v1;
struct zwlr_layer_surface_v1;

namespace fd {

enum class WlTouchKind : uint8_t { Down, Motion, Up, Frame, Cancel };

uint32_t layer_px(uint32_t rgb);

struct WlTouch {
    WlTouchKind kind;
    int32_t id = 0;
    int x = 0, y = 0;
    int64_t ms = 0;
};

class WaylandPresenter {
public:
    using S = SurfaceT<X888, Rows>;

    WaylandPresenter() = default;
    ~WaylandPresenter();
    WaylandPresenter(const WaylandPresenter&) = delete;
    WaylandPresenter& operator=(const WaylandPresenter&) = delete;

    void set_app_id(const std::string& id) { app_id_ = id; }
    void set_title(const std::string& title) { title_ = title; }
    void set_output(int w, int h);
    void set_connect_fd(int fd) { connect_fd_ = fd; }
    void set_keyboard(bool on) { kbd_on_ = on; }
    void set_layer_focus(bool on) { layer_focus_ = on; }
    void set_key_handler(std::function<void(uint32_t code, bool pressed)> f) { key_cb_ = std::move(f); }
    void set_keymap_handler(std::function<void(int fd, uint32_t size)> f) { keymap_cb_ = std::move(f); }
    void set_focus_handler(std::function<void(bool in)> f) { focus_cb_ = std::move(f); }

    bool open(const std::string& spec, bool screen);
    S draw_target() const { return inner_.draw_target(); }
    S warp_target() const { return inner_.warp_target(); }
    S screen() const { return inner_.screen(); }
    S last() const { return inner_.last(); }
    void begin_frame(int64_t, int64_t) {}
    void begin_full() { full_ = true; }
    void begin_cover(Rect r) { dmg_ = dmg_.unite(r); }
    void present(Rect dirty);
    void present_warp();
    void flip();
    bool posted() const { return posted_; }
    bool claim();
    bool ready() const;
    bool take_done(PanDone&) { return false; }
    int event_fd() const { return efd_; }
    int display_fd() const;
    bool pump();
    void flush();
    bool blocked() const { return blocked_; }
    bool lost() const { return lost_; }
    bool closed() const { return closed_; }
    void sleep(bool) {}
    void stop();
    int64_t show_at(int64_t now_ms) const { return inner_.show_at(now_ms); }
    int period_ms() const { return inner_.period_ms(); }
    void set_show_lead(int ms) { inner_.set_show_lead(ms); }
    int64_t next_tick(int64_t, int64_t fallback) const { return fallback; }
    PresentMode mode() const { return inner_.mode(); }
    uint32_t frame_presents() const { return inner_.frame_presents(); }
    uint32_t fulls() const { return inner_.fulls(); }
    uint32_t commits() const { return commits_; }

    bool activated() const { return activated_; }
    bool background() const { return background_; }
    uint32_t activations() const { return activations_; }
    bool layer_shell() const { return shell_ != nullptr; }
    bool show_layer();
    void hide_layer();
    bool layer_live() const { return live_ == &layer_; }
    bool layer_shown() const { return layer_.wl != nullptr; }
    bool take_touch(std::vector<WlTouch>& out);

private:
    struct Surf;
    struct Buf {
        Surf* surf = nullptr;
        wl_buffer* wl = nullptr;
        uint32_t* px = nullptr;
        bool busy = false;
    };
    struct Surf {
        WaylandPresenter* self = nullptr;
        wl_surface* wl = nullptr;
        xdg_surface* xdg = nullptr;
        xdg_toplevel* top = nullptr;
        zwlr_layer_surface_v1* layer = nullptr;
        wl_shm_pool* pool = nullptr;
        void* map = nullptr;
        size_t map_len = 0;
        int shm_fd = -1;
        Buf buf[2];
        Rect owed[2];
        wl_callback* cb = nullptr;
        uint32_t serial = 0;
        bool serial_due = false, configured = false, closed = false, fresh = false, argb = false;
    };

    void bind_global(wl_registry* reg, uint32_t name, const char* iface, uint32_t version);
    bool make_pool(Surf& s);
    void drop_surf(Surf& s);
    int pick_buf(const Surf& s) const;
    void copy_rows(Buf& b, Rect r);
    void signal();
    void settle();
    void lose();
    void check();
    void cancel_touch();
    void seat_caps(uint32_t caps);

    static void reg_global(void* data, wl_registry* reg, uint32_t name, const char* iface, uint32_t version);
    static void reg_remove(void* data, wl_registry* reg, uint32_t name);
    static void wm_ping(void* data, xdg_wm_base* base, uint32_t serial);
    static void surf_configure(void* data, xdg_surface* surf, uint32_t serial);
    static void top_configure(void* data, xdg_toplevel* top, int32_t w, int32_t h, wl_array* states);
    static void top_close(void* data, xdg_toplevel* top);
    static void top_bounds(void* data, xdg_toplevel* top, int32_t w, int32_t h);
    static void top_caps(void* data, xdg_toplevel* top, wl_array* caps);
    static void layer_configure(void* data, zwlr_layer_surface_v1* ls, uint32_t serial, uint32_t w, uint32_t h);
    static void layer_closed(void* data, zwlr_layer_surface_v1* ls);
    static void buf_release(void* data, wl_buffer* wl);
    static void frame_done(void* data, wl_callback* cb, uint32_t time);
    static void on_seat_caps(void* data, wl_seat* seat, uint32_t caps);
    static void on_seat_name(void* data, wl_seat* seat, const char* name);
    static void touch_down(void* data, wl_touch* t, uint32_t serial, uint32_t time, wl_surface* surf, int32_t id,
                           int32_t x, int32_t y);
    static void touch_up(void* data, wl_touch* t, uint32_t serial, uint32_t time, int32_t id);
    static void touch_motion(void* data, wl_touch* t, uint32_t time, int32_t id, int32_t x, int32_t y);
    static void touch_frame(void* data, wl_touch* t);
    static void touch_cancel(void* data, wl_touch* t);
    static void touch_shape(void* data, wl_touch* t, int32_t id, int32_t major, int32_t minor);
    static void touch_orientation(void* data, wl_touch* t, int32_t id, int32_t orientation);
    static void kb_keymap(void* data, wl_keyboard* kb, uint32_t format, int fd, uint32_t size);
    static void kb_enter(void* data, wl_keyboard* kb, uint32_t serial, wl_surface* surf, wl_array* keys);
    static void kb_leave(void* data, wl_keyboard* kb, uint32_t serial, wl_surface* surf);
    static void kb_key(void* data, wl_keyboard* kb, uint32_t serial, uint32_t time, uint32_t key, uint32_t state);
    static void kb_modifiers(void* data, wl_keyboard* kb, uint32_t serial, uint32_t depressed, uint32_t latched,
                             uint32_t locked, uint32_t group);
    static void kb_repeat(void* data, wl_keyboard* kb, int32_t rate, int32_t delay);

    struct KeyEv {
        uint8_t kind;
        uint32_t code;
        bool pressed;
        int fd;
        uint32_t size;
    };

    PresenterT<X888, Rows> inner_;
    std::string app_id_, title_;
    wl_display* dpy_ = nullptr;
    wl_registry* reg_ = nullptr;
    wl_compositor* comp_ = nullptr;
    wl_shm* shm_ = nullptr;
    xdg_wm_base* wm_ = nullptr;
    zwlr_layer_shell_v1* shell_ = nullptr;
    wl_seat* seat_ = nullptr;
    wl_touch* touch_ = nullptr;
    wl_keyboard* kbd_ = nullptr;
    uint32_t comp_ver_ = 0, shell_ver_ = 0, seat_ver_ = 0;
    Surf top_, layer_;
    Surf* live_ = &top_;
    std::function<void(uint32_t, bool)> key_cb_;
    std::function<void(int, uint32_t)> keymap_cb_;
    std::function<void(bool)> focus_cb_;
    std::vector<WlTouch> touches_;
    std::vector<KeyEv> keys_;
    int connect_fd_ = -1;
    int efd_ = -1;
    int out_w_ = kPortraitW, out_h_ = kPortraitH, off_x_ = 0, off_y_ = 0, told_w_ = 0, told_h_ = 0;
    Rect dmg_{};
    uint32_t ping_ = 0, commits_ = 0, activations_ = 0;
    bool ping_due_ = false;
    bool full_ = false, posted_ = false, waiting_ = false, blocked_ = false, lost_ = false, closed_ = false;
    bool kbd_on_ = false, layer_focus_ = false;
    bool act_ = false, activated_ = false, ever_ = false, background_ = false;
};

}
