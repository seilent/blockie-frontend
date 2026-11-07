#pragma once
#include <cstdint>
#include <string>

#include "../gfx/present.h"

struct wl_display;
struct wl_registry;
struct wl_compositor;
struct wl_shm;
struct wl_surface;
struct wl_buffer;
struct wl_shm_pool;
struct wl_region;
struct wl_callback;
struct wl_array;
struct xdg_wm_base;
struct xdg_surface;
struct xdg_toplevel;

namespace fd {

class WaylandPresenter {
public:
    using S = SurfaceT<X888, Rows>;

    WaylandPresenter() = default;
    ~WaylandPresenter();
    WaylandPresenter(const WaylandPresenter&) = delete;
    WaylandPresenter& operator=(const WaylandPresenter&) = delete;

    bool open(const std::string& spec, bool screen);
    S draw_target() { return inner_.draw_target(); }
    S warp_target() { return inner_.warp_target(); }
    S screen() { return inner_.screen(); }
    void present(Rect dirty);
    void present_warp();
    void begin_full();
    void begin_cover(Rect r);
    void begin_frame(int64_t now, int64_t target);
    void flip();
    bool claim();
    bool ready();
    bool take_done(PanDone& d);
    int event_fd();
    void sleep(bool on);
    void flush();
    void stop();
    int64_t show_at(int64_t now_ms) { return inner_.show_at(now_ms); }
    int period_ms() { return inner_.period_ms(); }
    int64_t next_tick(int64_t now, int64_t fallback);
    wl_display* display() const { return dpy_; }

private:
    struct Buf {
        wl_buffer* wl = nullptr;
        uint32_t* px = nullptr;
        bool busy = false;
    };

    void bind_global(wl_registry* reg, uint32_t name, const char* iface, uint32_t version);
    bool make_pool();
    int pick_buf();
    void copy_rows(Buf& b, Rect r);

    static void reg_global(void* data, wl_registry* reg, uint32_t name, const char* iface, uint32_t version);
    static void reg_remove(void* data, wl_registry* reg, uint32_t name);
    static void wm_ping(void* data, xdg_wm_base* base, uint32_t serial);
    static void surf_configure(void* data, xdg_surface* surf, uint32_t serial);
    static void top_configure(void* data, xdg_toplevel* top, int32_t w, int32_t h, wl_array* states);
    static void top_close(void* data, xdg_toplevel* top);
    static void buf_release(void* data, wl_buffer* wl);

    PresenterT<X888, Rows> inner_;
    wl_display* dpy_ = nullptr;
    wl_registry* reg_ = nullptr;
    wl_compositor* comp_ = nullptr;
    wl_shm* shm_ = nullptr;
    wl_surface* surf_ = nullptr;
    xdg_wm_base* wm_ = nullptr;
    xdg_surface* xsurf_ = nullptr;
    xdg_toplevel* top_ = nullptr;
    wl_shm_pool* pool_ = nullptr;
    void* map_ = nullptr;
    size_t map_len_ = 0;
    int shm_fd_ = -1;
    Buf buf_[2];
    int w_ = kPortraitW;
    int h_ = kPortraitH;
    int stride_ = kPortraitW;
    Rect dmg_{};
    Rect owed_[2];
    bool full_ = false;
    bool configured_ = false;
    bool closed_ = false;
};

}
