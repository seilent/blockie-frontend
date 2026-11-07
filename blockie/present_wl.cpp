#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "present_wl.h"

#include <cstring>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

#include "xdg-shell-client-protocol.h"

namespace fd {

namespace {

wl_registry_listener g_reg_l{};
xdg_wm_base_listener g_wm_l{};
xdg_surface_listener g_surf_l{};
xdg_toplevel_listener g_top_l{};
wl_buffer_listener g_buf_l{};

void top_bounds(void*, xdg_toplevel*, int32_t, int32_t) {}
void top_caps(void*, xdg_toplevel*, wl_array*) {}

template <class T> T& ref();

template <class P>
constexpr bool kCore = std::is_same_v<typename P::S, decltype(ref<P>().draw_target())> &&
                       std::is_same_v<decltype(ref<P>().open(ref<const std::string>(), true)), bool> &&
                       std::is_same_v<decltype(ref<P>().screen()), typename P::S> &&
                       std::is_same_v<decltype(ref<P>().warp_target()), typename P::S> &&
                       std::is_same_v<decltype(ref<P>().present(Rect{})), void> &&
                       std::is_same_v<decltype(ref<P>().present_warp()), void> &&
                       std::is_same_v<decltype(ref<P>().begin_full()), void> &&
                       std::is_same_v<decltype(ref<P>().begin_cover(Rect{})), void> &&
                       std::is_same_v<decltype(ref<P>().begin_frame(int64_t{}, int64_t{})), void> &&
                       std::is_same_v<decltype(ref<P>().flip()), void> &&
                       std::is_same_v<decltype(ref<P>().claim()), bool> &&
                       std::is_same_v<decltype(ref<P>().ready()), bool> &&
                       std::is_same_v<decltype(ref<P>().event_fd()), int> &&
                       std::is_same_v<decltype(ref<P>().take_done(ref<PanDone>())), bool> &&
                       std::is_same_v<decltype(ref<P>().sleep(true)), void> &&
                       std::is_same_v<decltype(ref<P>().flush()), void> &&
                       std::is_same_v<decltype(ref<P>().stop()), void> &&
                       std::is_same_v<decltype(ref<P>().show_at(int64_t{})), int64_t> &&
                       std::is_same_v<decltype(ref<P>().period_ms()), int> &&
                       std::is_same_v<decltype(ref<P>().next_tick(int64_t{}, int64_t{})), int64_t>;

static_assert(kCore<WaylandPresenter>, "the wayland presenter keeps the core set");

}

WaylandPresenter::~WaylandPresenter() { stop(); }

void WaylandPresenter::reg_global(void* data, wl_registry* reg, uint32_t name, const char* iface, uint32_t version)
{
    static_cast<WaylandPresenter*>(data)->bind_global(reg, name, iface, version);
}

void WaylandPresenter::reg_remove(void*, wl_registry*, uint32_t) {}

void WaylandPresenter::wm_ping(void*, xdg_wm_base* base, uint32_t serial) { xdg_wm_base_pong(base, serial); }

void WaylandPresenter::surf_configure(void* data, xdg_surface* surf, uint32_t serial)
{
    xdg_surface_ack_configure(surf, serial);
    static_cast<WaylandPresenter*>(data)->configured_ = true;
}

void WaylandPresenter::top_configure(void*, xdg_toplevel*, int32_t, int32_t, wl_array*) {}

void WaylandPresenter::top_close(void* data, xdg_toplevel*)
{
    static_cast<WaylandPresenter*>(data)->closed_ = true;
}

void WaylandPresenter::buf_release(void* data, wl_buffer*) { static_cast<Buf*>(data)->busy = false; }

void WaylandPresenter::bind_global(wl_registry* reg, uint32_t name, const char* iface, uint32_t version)
{
    if (std::strcmp(iface, wl_compositor_interface.name) == 0) {
        uint32_t v = version < 4 ? version : 4;
        comp_ = static_cast<wl_compositor*>(wl_registry_bind(reg, name, &wl_compositor_interface, v));
    } else if (std::strcmp(iface, wl_shm_interface.name) == 0) {
        shm_ = static_cast<wl_shm*>(wl_registry_bind(reg, name, &wl_shm_interface, 1));
    } else if (std::strcmp(iface, xdg_wm_base_interface.name) == 0) {
        uint32_t v = version < 2 ? version : 2;
        wm_ = static_cast<xdg_wm_base*>(wl_registry_bind(reg, name, &xdg_wm_base_interface, v));
    }
}

bool WaylandPresenter::make_pool()
{
    size_t frame = (size_t)stride_ * h_ * 4;
    map_len_ = frame * 2;
    shm_fd_ = memfd_create("blockie-frontend", MFD_CLOEXEC);
    if (shm_fd_ < 0)
        return false;
    if (ftruncate(shm_fd_, (off_t)map_len_) < 0)
        return false;
    map_ = mmap(nullptr, map_len_, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
    if (map_ == MAP_FAILED) {
        map_ = nullptr;
        return false;
    }
    pool_ = wl_shm_create_pool(shm_, shm_fd_, (int32_t)map_len_);
    g_buf_l.release = buf_release;
    for (int i = 0; i < 2; i++) {
        size_t off = (size_t)i * frame;
        buf_[i].wl = wl_shm_pool_create_buffer(pool_, (int32_t)off, w_, h_, stride_ * 4, WL_SHM_FORMAT_XRGB8888);
        buf_[i].px = reinterpret_cast<uint32_t*>(static_cast<char*>(map_) + off);
        buf_[i].busy = false;
        wl_buffer_add_listener(buf_[i].wl, &g_buf_l, &buf_[i]);
    }
    return true;
}

bool WaylandPresenter::open(const std::string&, bool)
{
    dpy_ = wl_display_connect(nullptr);
    if (!dpy_)
        return false;
    reg_ = wl_display_get_registry(dpy_);
    g_reg_l.global = reg_global;
    g_reg_l.global_remove = reg_remove;
    wl_registry_add_listener(reg_, &g_reg_l, this);
    wl_display_roundtrip(dpy_);
    if (!comp_ || !shm_ || !wm_) {
        stop();
        return false;
    }
    g_wm_l.ping = wm_ping;
    xdg_wm_base_add_listener(wm_, &g_wm_l, this);
    surf_ = wl_compositor_create_surface(comp_);
    xsurf_ = xdg_wm_base_get_xdg_surface(wm_, surf_);
    g_surf_l.configure = surf_configure;
    xdg_surface_add_listener(xsurf_, &g_surf_l, this);
    top_ = xdg_surface_get_toplevel(xsurf_);
    g_top_l.configure = top_configure;
    g_top_l.close = top_close;
    g_top_l.configure_bounds = top_bounds;
    g_top_l.wm_capabilities = top_caps;
    xdg_toplevel_add_listener(top_, &g_top_l, this);
    xdg_toplevel_set_app_id(top_, "net.seilent.Blockie.Frontend");
    xdg_toplevel_set_title(top_, "Blockie");
    xdg_toplevel_set_fullscreen(top_, nullptr);
    wl_surface_commit(surf_);
    wl_display_roundtrip(dpy_);
    if (!make_pool()) {
        stop();
        return false;
    }
    wl_region* rgn = wl_compositor_create_region(comp_);
    wl_region_add(rgn, 0, 0, w_, h_);
    wl_surface_set_opaque_region(surf_, rgn);
    wl_region_destroy(rgn);
    inner_.open(PresentMode::Copy);
    return true;
}

void WaylandPresenter::begin_frame(int64_t, int64_t)
{
    dmg_ = {};
    full_ = false;
}

void WaylandPresenter::begin_full() { full_ = true; }

void WaylandPresenter::begin_cover(Rect r) { dmg_ = dmg_.unite(r); }

void WaylandPresenter::present(Rect dirty)
{
    dmg_ = dmg_.unite(dirty);
    inner_.present(dirty);
}

void WaylandPresenter::present_warp()
{
    full_ = true;
    inner_.present_warp();
}

void WaylandPresenter::copy_rows(Buf& b, Rect r)
{
    S src = inner_.screen();
    int n = r.x1 - r.x0;
    for (int y = r.y0; y < r.y1; y++)
        std::memcpy(b.px + (size_t)y * stride_ + r.x0, src.row(y) + r.x0, (size_t)n * 4);
}

int WaylandPresenter::pick_buf()
{
    if (!buf_[0].busy)
        return 0;
    if (!buf_[1].busy)
        return 1;
    return -1;
}

void WaylandPresenter::flip()
{
    inner_.flip();
    Rect region = (full_ ? kScreen : dmg_).intersect(kScreen);
    int bi = pick_buf();
    if (bi < 0)
        bi = 0;
    Buf& b = buf_[bi];
    Rect paint = region.unite(owed_[bi]).intersect(kScreen);
    if (b.px && !paint.empty())
        copy_rows(b, paint);
    owed_[bi] = {};
    owed_[bi ^ 1] = owed_[bi ^ 1].unite(region);
    if (surf_ && b.wl) {
        wl_surface_attach(surf_, b.wl, 0, 0);
        if (!paint.empty())
            wl_surface_damage_buffer(surf_, paint.x0, paint.y0, paint.x1 - paint.x0, paint.y1 - paint.y0);
        wl_surface_commit(surf_);
        b.busy = true;
    }
    if (dpy_)
        wl_display_flush(dpy_);
    dmg_ = {};
    full_ = false;
}

bool WaylandPresenter::claim() { return pick_buf() >= 0; }

bool WaylandPresenter::ready() { return configured_ && !closed_; }

bool WaylandPresenter::take_done(PanDone&) { return false; }

int WaylandPresenter::event_fd() { return dpy_ ? wl_display_get_fd(dpy_) : -1; }

void WaylandPresenter::sleep(bool) {}

void WaylandPresenter::flush()
{
    if (dpy_)
        wl_display_flush(dpy_);
}

int64_t WaylandPresenter::next_tick(int64_t, int64_t fallback) { return fallback; }

void WaylandPresenter::stop()
{
    for (int i = 0; i < 2; i++) {
        if (buf_[i].wl)
            wl_buffer_destroy(buf_[i].wl);
        buf_[i] = {};
    }
    if (pool_)
        wl_shm_pool_destroy(pool_);
    pool_ = nullptr;
    if (map_)
        munmap(map_, map_len_);
    map_ = nullptr;
    map_len_ = 0;
    if (shm_fd_ >= 0)
        close(shm_fd_);
    shm_fd_ = -1;
    if (top_)
        xdg_toplevel_destroy(top_);
    top_ = nullptr;
    if (xsurf_)
        xdg_surface_destroy(xsurf_);
    xsurf_ = nullptr;
    if (surf_)
        wl_surface_destroy(surf_);
    surf_ = nullptr;
    if (wm_)
        xdg_wm_base_destroy(wm_);
    wm_ = nullptr;
    if (shm_)
        wl_shm_destroy(shm_);
    shm_ = nullptr;
    if (comp_)
        wl_compositor_destroy(comp_);
    comp_ = nullptr;
    if (reg_)
        wl_registry_destroy(reg_);
    reg_ = nullptr;
    if (dpy_)
        wl_display_disconnect(dpy_);
    dpy_ = nullptr;
}

}
