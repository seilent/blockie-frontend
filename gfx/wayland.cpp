#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "wayland.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

#include <presentation-time-client-protocol.h>
#include <wlr-layer-shell-unstable-v1-client-protocol.h>
#include <xdg-shell-client-protocol.h>

#include "../ui/tokens.h"

namespace fd {

namespace {

wl_registry_listener g_reg_l{};
xdg_wm_base_listener g_wm_l{};
xdg_surface_listener g_surf_l{};
xdg_toplevel_listener g_top_l{};
zwlr_layer_surface_v1_listener g_layer_l{};
wl_buffer_listener g_buf_l{};
wl_callback_listener g_frame_l{};
wl_seat_listener g_seat_l{};
wl_touch_listener g_touch_l{};
wl_keyboard_listener g_kbd_l{};
wp_presentation_listener g_pres_l{};
wp_presentation_feedback_listener g_fb_l{};

enum : uint8_t { kKey, kKeymap, kEnter, kLeave };

constexpr uint32_t kLayerAlpha = (uint32_t)(((int64_t)tok::scrim_overlay_alpha * 255 + 32768) >> 16);
constexpr uint32_t kLayerSide = kLayerAlpha << 24;

int64_t mono_ms()
{
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

uint64_t mono_ns()
{
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000 + (uint64_t)t.tv_nsec;
}

bool parse_mode(const std::string& spec, PresentMode& mode)
{
    std::string m = spec.substr(0, spec.find('+'));
    if (m == "direct")
        mode = PresentMode::Direct;
    else if (m == "copy")
        mode = PresentMode::Copy;
    else if (m == "warp")
        mode = PresentMode::Warp;
    else
        return false;
    return true;
}

}

WaylandPresenter::~WaylandPresenter() { stop(); }

void WaylandPresenter::set_role(WlRole role)
{
    if (dpy_) {
        if (!role_told_)
            fprintf(stderr, "funeos: wayland: set_role after open\n");
        role_told_ = true;
        return;
    }
    role_ = role;
}

void WaylandPresenter::set_translucent(bool on)
{
    if (dpy_) {
        if (!trans_told_)
            fprintf(stderr, "funeos: wayland: set_translucent after open\n");
        trans_told_ = true;
        return;
    }
    translucent_ = on;
}

bool WaylandPresenter::set_alpha(const WlAlpha* v, int n)
{
    bool regions = role_ == WlRole::Layer || translucent_;
    if (!regions || n < 0 || n > kWlAlphaMax) {
        if (!alpha_told_ && !regions)
            fprintf(stderr, "funeos: wayland: set_alpha outside the layer role\n");
        else if (!alpha_told_)
            fprintf(stderr, "funeos: wayland: set_alpha with %d regions refused\n", n);
        alpha_told_ = true;
        return false;
    }
    WlAlpha next[kWlAlphaMax];
    int m = 0;
    Rect box{};
    for (int i = 0; i < n; i++) {
        Rect r = v[i].r.intersect(kScreen);
        if (r.empty())
            continue;
        next[m++] = {r, v[i].a};
        box = box.unite(r);
    }
    bool same = m == alpha_n_;
    for (int i = 0; same && i < m; i++) {
        Rect a = next[i].r, b = alpha_[i].r;
        same = next[i].a == alpha_[i].a && a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1;
    }
    if (same)
        return true;
    for (int i = 0; i < alpha_n_; i++)
        box = box.unite(alpha_[i].r);
    std::copy(next, next + m, alpha_);
    alpha_n_ = m;
    dmg_ = dmg_.unite(box);
    return true;
}

void WaylandPresenter::set_output(int w, int h)
{
    out_w_ = w < kPortraitW ? kPortraitW : w;
    out_h_ = h < kPortraitH ? kPortraitH : h;
    off_x_ = (out_w_ - kPortraitW) / 2;
    off_y_ = (out_h_ - kPortraitH) / 2;
}

void WaylandPresenter::reg_global(void* data, wl_registry* reg, uint32_t name, const char* iface, uint32_t version)
{
    static_cast<WaylandPresenter*>(data)->bind_global(reg, name, iface, version);
}

void WaylandPresenter::reg_remove(void*, wl_registry*, uint32_t) {}

void WaylandPresenter::wm_ping(void* data, xdg_wm_base*, uint32_t serial)
{
    auto* self = static_cast<WaylandPresenter*>(data);
    self->ping_ = serial;
    self->ping_due_ = true;
}

void WaylandPresenter::surf_configure(void* data, xdg_surface*, uint32_t serial)
{
    auto* s = static_cast<Surf*>(data);
    s->serial = serial;
    s->serial_due = true;
}

void WaylandPresenter::top_configure(void* data, xdg_toplevel*, int32_t w, int32_t h, wl_array* states)
{
    auto* self = static_cast<Surf*>(data)->self;
    if (w > 0 && h > 0 && (w != self->out_w_ || h != self->out_h_) && (w != self->told_w_ || h != self->told_h_)) {
        fprintf(stderr, "funeos: wayland configure %dx%d, the output is %dx%d\n", w, h, self->out_w_, self->out_h_);
        self->told_w_ = w;
        self->told_h_ = h;
    }
    bool act = false;
    if (states && states->data) {
        const uint32_t* s = static_cast<const uint32_t*>(states->data);
        for (size_t i = 0; i < states->size / sizeof(uint32_t); i++)
            if (s[i] == XDG_TOPLEVEL_STATE_ACTIVATED)
                act = true;
    }
    self->act_ = act;
}

void WaylandPresenter::top_close(void* data, xdg_toplevel*) { static_cast<Surf*>(data)->closed = true; }

void WaylandPresenter::top_bounds(void*, xdg_toplevel*, int32_t, int32_t) {}

void WaylandPresenter::top_caps(void*, xdg_toplevel*, wl_array*) {}

void WaylandPresenter::layer_configure(void* data, zwlr_layer_surface_v1*, uint32_t serial, uint32_t w, uint32_t h)
{
    auto* s = static_cast<Surf*>(data);
    s->serial = serial;
    s->serial_due = true;
    s->cw = (int)w;
    s->ch = (int)h;
}

void WaylandPresenter::layer_closed(void* data, zwlr_layer_surface_v1*) { static_cast<Surf*>(data)->closed = true; }

void WaylandPresenter::buf_release(void* data, wl_buffer*)
{
    auto* b = static_cast<Buf*>(data);
    b->busy = false;
    if (b->surf->self->waiting_)
        b->surf->self->signal();
}

void WaylandPresenter::pres_clock(void*, wp_presentation*, uint32_t) {}

void WaylandPresenter::fb_sync(void*, struct wp_presentation_feedback*, wl_output*) {}

void WaylandPresenter::fb_presented(void* data, struct wp_presentation_feedback*, uint32_t, uint32_t, uint32_t,
                                    uint32_t, uint32_t, uint32_t, uint32_t)
{
    auto* f = static_cast<Fb*>(data);
    f->self->owe_feedback(f, true);
}

void WaylandPresenter::fb_discarded(void* data, struct wp_presentation_feedback*)
{
    auto* f = static_cast<Fb*>(data);
    f->self->owe_feedback(f, false);
}

void WaylandPresenter::owe_feedback(Fb* f, bool presented)
{
    wp_presentation_feedback_destroy(f->wl);
    if (fq_n_ == kFbMax) {
        std::copy(fq_ + 1, fq_ + kFbMax, fq_);
        fq_n_--;
    }
    fq_[fq_n_++] = {f->commit, presented, f->visible};
    *f = {};
}

void WaylandPresenter::ask_feedback(wl_surface* surf)
{
    Fb* f = nullptr;
    for (Fb& o : fb_)
        if (!o.wl && !f)
            f = &o;
    if (!f) {
        f = &fb_[0];
        for (Fb& o : fb_)
            if (o.commit < f->commit)
                f = &o;
        wp_presentation_feedback_destroy(f->wl);
    }
    bool vis = false;
    for (int i = 0; i < alpha_n_; i++)
        vis = vis || alpha_[i].a > 0;
    *f = {this, wp_presentation_feedback(pres_, surf), commits_ + 1, vis};
    g_fb_l.sync_output = fb_sync;
    g_fb_l.presented = fb_presented;
    g_fb_l.discarded = fb_discarded;
    wp_presentation_feedback_add_listener(f->wl, &g_fb_l, f);
}

bool WaylandPresenter::take_feedback(std::vector<WlFeedback>& out)
{
    out.assign(fq_, fq_ + fq_n_);
    fq_n_ = 0;
    return !out.empty();
}

void WaylandPresenter::frame_done(void* data, wl_callback* cb, uint32_t)
{
    auto* s = static_cast<Surf*>(data);
    wl_callback_destroy(cb);
    if (s->cb == cb)
        s->cb = nullptr;
    s->self->signal();
}

void WaylandPresenter::on_seat_caps(void* data, wl_seat*, uint32_t caps)
{
    static_cast<WaylandPresenter*>(data)->seat_caps(caps);
}

void WaylandPresenter::on_seat_name(void*, wl_seat*, const char*) {}

void WaylandPresenter::touch_down(void* data, wl_touch*, uint32_t, uint32_t, wl_surface*, int32_t id, int32_t x,
                                  int32_t y)
{
    static_cast<WaylandPresenter*>(data)->touches_.push_back(
        {WlTouchKind::Down, id, wl_fixed_to_int(x), wl_fixed_to_int(y), mono_ms()});
}

void WaylandPresenter::touch_up(void* data, wl_touch*, uint32_t, uint32_t, int32_t id)
{
    static_cast<WaylandPresenter*>(data)->touches_.push_back({WlTouchKind::Up, id, 0, 0, mono_ms()});
}

void WaylandPresenter::touch_motion(void* data, wl_touch*, uint32_t, int32_t id, int32_t x, int32_t y)
{
    static_cast<WaylandPresenter*>(data)->touches_.push_back(
        {WlTouchKind::Motion, id, wl_fixed_to_int(x), wl_fixed_to_int(y), mono_ms()});
}

void WaylandPresenter::touch_frame(void* data, wl_touch*)
{
    static_cast<WaylandPresenter*>(data)->touches_.push_back({WlTouchKind::Frame, 0, 0, 0, mono_ms()});
}

void WaylandPresenter::touch_cancel(void* data, wl_touch*) { static_cast<WaylandPresenter*>(data)->cancel_touch(); }

void WaylandPresenter::touch_shape(void*, wl_touch*, int32_t, int32_t, int32_t) {}

void WaylandPresenter::touch_orientation(void*, wl_touch*, int32_t, int32_t) {}

void WaylandPresenter::kb_keymap(void* data, wl_keyboard*, uint32_t format, int fd, uint32_t size)
{
    auto* self = static_cast<WaylandPresenter*>(data);
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 || !self->keymap_cb_) {
        close(fd);
        return;
    }
    self->keys_.push_back({kKeymap, 0, false, fd, size});
}

void WaylandPresenter::kb_enter(void* data, wl_keyboard*, uint32_t, wl_surface*, wl_array*)
{
    static_cast<WaylandPresenter*>(data)->keys_.push_back({kEnter, 0, false, -1, 0});
}

void WaylandPresenter::kb_leave(void* data, wl_keyboard*, uint32_t, wl_surface*)
{
    static_cast<WaylandPresenter*>(data)->keys_.push_back({kLeave, 0, false, -1, 0});
}

void WaylandPresenter::kb_key(void* data, wl_keyboard*, uint32_t, uint32_t, uint32_t key, uint32_t state)
{
    static_cast<WaylandPresenter*>(data)->keys_.push_back(
        {kKey, key, state == WL_KEYBOARD_KEY_STATE_PRESSED, -1, 0});
}

void WaylandPresenter::kb_modifiers(void*, wl_keyboard*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) {}

void WaylandPresenter::kb_repeat(void*, wl_keyboard*, int32_t, int32_t) {}

void WaylandPresenter::seat_caps(uint32_t caps)
{
    bool has_touch = caps & WL_SEAT_CAPABILITY_TOUCH;
    if (has_touch && !touch_) {
        touch_ = wl_seat_get_touch(seat_);
        g_touch_l.down = touch_down;
        g_touch_l.up = touch_up;
        g_touch_l.motion = touch_motion;
        g_touch_l.frame = touch_frame;
        g_touch_l.cancel = touch_cancel;
        g_touch_l.shape = touch_shape;
        g_touch_l.orientation = touch_orientation;
        wl_touch_add_listener(touch_, &g_touch_l, this);
    } else if (!has_touch && touch_) {
        if (seat_ver_ >= 3)
            wl_touch_release(touch_);
        else
            wl_touch_destroy(touch_);
        touch_ = nullptr;
        cancel_touch();
    }
    bool has_kb = kbd_on_ && (caps & WL_SEAT_CAPABILITY_KEYBOARD);
    if (has_kb && !kbd_) {
        kbd_ = wl_seat_get_keyboard(seat_);
        g_kbd_l.keymap = kb_keymap;
        g_kbd_l.enter = kb_enter;
        g_kbd_l.leave = kb_leave;
        g_kbd_l.key = kb_key;
        g_kbd_l.modifiers = kb_modifiers;
        g_kbd_l.repeat_info = kb_repeat;
        wl_keyboard_add_listener(kbd_, &g_kbd_l, this);
    } else if (!has_kb && kbd_) {
        if (seat_ver_ >= 3)
            wl_keyboard_release(kbd_);
        else
            wl_keyboard_destroy(kbd_);
        kbd_ = nullptr;
    }
}

void WaylandPresenter::bind_global(wl_registry* reg, uint32_t name, const char* iface, uint32_t version)
{
    if (std::strcmp(iface, wl_compositor_interface.name) == 0 && !comp_) {
        comp_ver_ = version < 4 ? version : 4;
        comp_ = static_cast<wl_compositor*>(wl_registry_bind(reg, name, &wl_compositor_interface, comp_ver_));
    } else if (std::strcmp(iface, wl_shm_interface.name) == 0 && !shm_) {
        shm_ = static_cast<wl_shm*>(wl_registry_bind(reg, name, &wl_shm_interface, 1));
    } else if (std::strcmp(iface, xdg_wm_base_interface.name) == 0 && !wm_ && role_ == WlRole::Toplevel) {
        uint32_t v = version < 2 ? version : 2;
        wm_ = static_cast<xdg_wm_base*>(wl_registry_bind(reg, name, &xdg_wm_base_interface, v));
    } else if (std::strcmp(iface, zwlr_layer_shell_v1_interface.name) == 0 && !shell_) {
        shell_ver_ = version < 4 ? version : 4;
        shell_ = static_cast<zwlr_layer_shell_v1*>(
            wl_registry_bind(reg, name, &zwlr_layer_shell_v1_interface, shell_ver_));
    } else if (std::strcmp(iface, wp_presentation_interface.name) == 0 && !pres_ && role_ == WlRole::Layer) {
        pres_ = static_cast<wp_presentation*>(wl_registry_bind(reg, name, &wp_presentation_interface, 1));
        g_pres_l.clock_id = pres_clock;
        wp_presentation_add_listener(pres_, &g_pres_l, this);
    } else if (std::strcmp(iface, wl_seat_interface.name) == 0 && !seat_ && role_ == WlRole::Toplevel) {
        seat_ver_ = version < 5 ? version : 5;
        seat_ = static_cast<wl_seat*>(wl_registry_bind(reg, name, &wl_seat_interface, seat_ver_));
        g_seat_l.capabilities = on_seat_caps;
        g_seat_l.name = on_seat_name;
        wl_seat_add_listener(seat_, &g_seat_l, this);
    }
}

bool WaylandPresenter::make_pool(Surf& s)
{
    size_t frame = (size_t)out_w_ * out_h_ * 4;
    s.map_len = frame * 2;
    s.shm_fd = memfd_create("funeos-wl", MFD_CLOEXEC);
    if (s.shm_fd >= 0 && ftruncate(s.shm_fd, (off_t)s.map_len) == 0)
        s.map = mmap(nullptr, s.map_len, PROT_READ | PROT_WRITE, MAP_SHARED, s.shm_fd, 0);
    else
        s.map = MAP_FAILED;
    if (s.map == MAP_FAILED) {
        s.map = nullptr;
        if (s.shm_fd >= 0)
            ::close(s.shm_fd);
        s.shm_fd = -1;
        return false;
    }
    s.pool = wl_shm_create_pool(shm_, s.shm_fd, (int32_t)s.map_len);
    g_buf_l.release = buf_release;
    for (int i = 0; i < 2; i++) {
        Buf& b = s.buf[i];
        size_t off = (size_t)i * frame;
        b.surf = &s;
        b.wl = wl_shm_pool_create_buffer(s.pool, (int32_t)off, out_w_, out_h_, out_w_ * 4,
                                         s.px == Px::Opaque ? WL_SHM_FORMAT_XRGB8888 : WL_SHM_FORMAT_ARGB8888);
        b.px = reinterpret_cast<uint32_t*>(static_cast<char*>(s.map) + off);
        b.busy = false;
        std::fill(b.px, b.px + (size_t)out_w_ * out_h_, s.px == Px::Layer ? kLayerSide : 0u);
        s.owed[i] = kScreen;
        wl_buffer_add_listener(b.wl, &g_buf_l, &b);
    }
    if (s.px == Px::Regions)
        arow_.assign((size_t)out_w_, 0);
    s.fresh = true;
    return true;
}

void WaylandPresenter::drop_surf(Surf& s)
{
    if (s.cb)
        wl_callback_destroy(s.cb);
    s.cb = nullptr;
    for (Buf& b : s.buf) {
        if (b.wl)
            wl_buffer_destroy(b.wl);
        b = {};
    }
    if (s.pool)
        wl_shm_pool_destroy(s.pool);
    if (s.map)
        munmap(s.map, s.map_len);
    if (s.shm_fd >= 0)
        close(s.shm_fd);
    if (s.layer)
        zwlr_layer_surface_v1_destroy(s.layer);
    if (s.top)
        xdg_toplevel_destroy(s.top);
    if (s.xdg)
        xdg_surface_destroy(s.xdg);
    if (s.wl)
        wl_surface_destroy(s.wl);
    WaylandPresenter* self = s.self;
    s = {};
    s.self = self;
}

bool WaylandPresenter::open(const std::string& spec, bool screen)
{
    PresentMode mode;
    if (!parse_mode(spec, mode)) {
        fprintf(stderr, "funeos: FD_PRESENT %s: mode is direct, copy or warp\n", spec.c_str());
        return false;
    }
    inner_.open(mode);
    if (!screen)
        return true;
    top_.self = layer_.self = this;
    lost_ = closed_ = blocked_ = false;
    commits_ = 0;
    fq_n_ = alpha_n_ = 0;
    efd_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    dpy_ = connect_fd_ >= 0 ? wl_display_connect_to_fd(connect_fd_) : wl_display_connect(nullptr);
    connect_fd_ = -1;
    if (efd_ < 0 || !dpy_) {
        fprintf(stderr, "funeos: wayland: no display: %s\n", strerror(errno));
        stop();
        return false;
    }
    reg_ = wl_display_get_registry(dpy_);
    g_reg_l.global = reg_global;
    g_reg_l.global_remove = reg_remove;
    wl_registry_add_listener(reg_, &g_reg_l, this);
    wl_display_roundtrip(dpy_);
    if (role_ == WlRole::Layer)
        return open_layer();
    if (!comp_ || !shm_ || !wm_) {
        fprintf(stderr, "funeos: wayland: no %s\n", !comp_ ? "wl_compositor" : !shm_ ? "wl_shm" : "xdg_wm_base");
        stop();
        return false;
    }
    g_wm_l.ping = wm_ping;
    xdg_wm_base_add_listener(wm_, &g_wm_l, this);
    top_.wl = wl_compositor_create_surface(comp_);
    top_.xdg = xdg_wm_base_get_xdg_surface(wm_, top_.wl);
    g_surf_l.configure = surf_configure;
    xdg_surface_add_listener(top_.xdg, &g_surf_l, &top_);
    top_.top = xdg_surface_get_toplevel(top_.xdg);
    g_top_l.configure = top_configure;
    g_top_l.close = top_close;
    g_top_l.configure_bounds = top_bounds;
    g_top_l.wm_capabilities = top_caps;
    xdg_toplevel_add_listener(top_.top, &g_top_l, &top_);
    if (!app_id_.empty())
        xdg_toplevel_set_app_id(top_.top, app_id_.c_str());
    if (!title_.empty())
        xdg_toplevel_set_title(top_.top, title_.c_str());
    xdg_toplevel_set_fullscreen(top_.top, nullptr);
    wl_surface_commit(top_.wl);
    wl_display_roundtrip(dpy_);
    top_.px = translucent_ ? Px::Regions : Px::Opaque;
    if (!top_.serial_due || !make_pool(top_)) {
        fprintf(stderr, "funeos: wayland: %s\n", top_.serial_due ? "no shm pool" : "no configure");
        stop();
        return false;
    }
    if (!translucent_) {
        wl_region* rgn = wl_compositor_create_region(comp_);
        wl_region_add(rgn, 0, 0, out_w_, out_h_);
        wl_surface_set_opaque_region(top_.wl, rgn);
        wl_region_destroy(rgn);
    }
    settle();
    full_ = true;
    flip();
    flush();
    check();
    return !lost_;
}

bool WaylandPresenter::open_layer()
{
    if (!comp_ || !shm_ || !shell_) {
        fprintf(stderr, "funeos: wayland: no %s\n",
                !comp_ ? "wl_compositor" : !shm_ ? "wl_shm" : "zwlr_layer_shell_v1");
        stop();
        return false;
    }
    live_ = &layer_;
    layer_.px = Px::Regions;
    layer_.wl = wl_compositor_create_surface(comp_);
    layer_.layer = zwlr_layer_shell_v1_get_layer_surface(shell_, layer_.wl, nullptr, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
                                                         app_id_.empty() ? "funeos" : app_id_.c_str());
    g_layer_l.configure = layer_configure;
    g_layer_l.closed = layer_closed;
    zwlr_layer_surface_v1_add_listener(layer_.layer, &g_layer_l, &layer_);
    zwlr_layer_surface_v1_set_anchor(layer_.layer,
                                     ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                                         ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_size(layer_.layer, 0, 0);
    zwlr_layer_surface_v1_set_exclusive_zone(layer_.layer, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(layer_.layer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
    wl_surface_set_input_region(layer_.wl, nullptr);
    wl_surface_commit(layer_.wl);
    wl_display_roundtrip(dpy_);
    if (layer_.closed || !layer_.serial_due) {
        fprintf(stderr, "funeos: wayland: %s\n", layer_.closed ? "closed at open" : "no configure");
        stop();
        return false;
    }
    if (layer_.cw > 0 && layer_.ch > 0)
        set_output(layer_.cw, layer_.ch);
    settle();
    if (!make_pool(layer_)) {
        fprintf(stderr, "funeos: wayland: no shm pool\n");
        stop();
        return false;
    }
    layer_.configured = true;
    full_ = true;
    flip();
    flush();
    check();
    return !lost_;
}

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

uint32_t layer_px(uint32_t rgb)
{
    uint32_t m = std::max({(rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF});
    return ((kLayerAlpha + ((255 - kLayerAlpha) * m + 127) / 255) << 24) | (rgb & 0xFFFFFF);
}

void WaylandPresenter::copy_rows(Buf& b, Rect r)
{
    S src = inner_.screen();
    int n = r.x1 - r.x0;
    for (int y = r.y0; y < r.y1; y++) {
        uint32_t* d = b.px + (size_t)(y + off_y_) * out_w_ + off_x_ + r.x0;
        const uint32_t* s = src.row(y) + r.x0;
        if (b.surf->px == Px::Opaque)
            std::memcpy(d, s, (size_t)n * 4);
        else if (b.surf->px == Px::Layer)
            for (int x = 0; x < n; x++)
                d[x] = layer_px(s[x]);
        else
            region_row(d, s, y, r.x0, r.x1);
    }
}

void WaylandPresenter::region_row(uint32_t* d, const uint32_t* s, int y, int x0, int x1)
{
    uint8_t* a = arow_.data();
    std::memset(a + x0, 0, (size_t)(x1 - x0));
    for (int i = 0; i < alpha_n_; i++) {
        const Rect& g = alpha_[i].r;
        int l = std::max(g.x0, x0), h = std::min(g.x1, x1);
        if (y >= g.y0 && y < g.y1 && l < h)
            std::memset(a + l, alpha_[i].a, (size_t)(h - l));
    }
    for (int x = x0; x < x1;) {
        uint8_t v = a[x];
        int e = x + 1;
        while (e < x1 && a[e] == v)
            e++;
        uint32_t* o = d + (x - x0);
        const uint32_t* p = s + (x - x0);
        int n = e - x;
        if (v == 0)
            std::fill(o, o + n, 0u);
        else if (v == 255)
            for (int i = 0; i < n; i++)
                o[i] = 0xFF000000u | (p[i] & 0xFFFFFF);
        else
            for (int i = 0; i < n; i++)
                o[i] = premul_px(p[i], v);
        x = e;
    }
}

int WaylandPresenter::pick_buf(const Surf& s) const
{
    if (s.buf[0].wl && !s.buf[0].busy)
        return 0;
    if (s.buf[1].wl && !s.buf[1].busy)
        return 1;
    return -1;
}

void WaylandPresenter::flip()
{
    inner_.flip();
    posted_ = false;
    Rect region = (full_ ? kScreen : dmg_).intersect(kScreen);
    if (!dpy_ || region.empty()) {
        dmg_ = {};
        full_ = false;
        return;
    }
    Surf& s = *live_;
    int bi = pick_buf(s);
    if (lost_ || closed_ || !s.configured || s.cb || bi < 0)
        return;
    Buf& b = s.buf[bi];
    Rect paint = region.unite(s.owed[bi]).intersect(kScreen);
    if (s.px == Px::Regions) {
        uint64_t t0 = mono_ns();
        copy_rows(b, paint);
        copy_ns_ += mono_ns() - t0;
        copies_++;
    } else {
        copy_rows(b, paint);
    }
    s.owed[bi] = {};
    s.owed[bi ^ 1] = s.owed[bi ^ 1].unite(region);
    wl_surface_attach(s.wl, b.wl, 0, 0);
    int dx = paint.x0 + off_x_, dy = paint.y0 + off_y_, dw = paint.x1 - paint.x0, dh = paint.y1 - paint.y0;
    if (s.fresh) {
        dx = dy = 0;
        dw = out_w_;
        dh = out_h_;
        s.fresh = false;
    }
    if (comp_ver_ >= 4)
        wl_surface_damage_buffer(s.wl, dx, dy, dw, dh);
    else
        wl_surface_damage(s.wl, dx, dy, dw, dh);
    s.cb = wl_surface_frame(s.wl);
    g_frame_l.done = frame_done;
    wl_callback_add_listener(s.cb, &g_frame_l, &s);
    if (pres_)
        ask_feedback(s.wl);
    wl_surface_commit(s.wl);
    b.busy = true;
    commits_++;
    posted_ = true;
    waiting_ = false;
    dmg_ = {};
    full_ = false;
}

bool WaylandPresenter::claim()
{
    if (!dpy_)
        return true;
    const Surf& s = *live_;
    if (lost_ || closed_ || !s.configured || s.cb || pick_buf(s) < 0) {
        waiting_ = true;
        return false;
    }
    return true;
}

bool WaylandPresenter::ready() const { return !dpy_ || (!lost_ && !closed_ && live_->configured && !live_->cb); }

int WaylandPresenter::display_fd() const { return dpy_ ? wl_display_get_fd(dpy_) : -1; }

void WaylandPresenter::signal()
{
    uint64_t one = 1;
    if (efd_ >= 0 && write(efd_, &one, sizeof one) < 0 && errno != EAGAIN)
        fprintf(stderr, "funeos: wayland: frame event: %s\n", strerror(errno));
}

void WaylandPresenter::lose()
{
    if (lost_)
        return;
    lost_ = true;
    int err = dpy_ ? wl_display_get_error(dpy_) : 0;
    if (err == EPROTO) {
        const wl_interface* iface = nullptr;
        uint32_t id = 0;
        uint32_t code = wl_display_get_protocol_error(dpy_, &iface, &id);
        fprintf(stderr, "funeos: wayland: protocol error %u on %s %u\n", code, iface ? iface->name : "?", id);
    } else {
        fprintf(stderr, "funeos: wayland: connection lost: %s\n", strerror(err ? err : EPIPE));
    }
    signal();
}

void WaylandPresenter::check()
{
    if (dpy_ && wl_display_get_error(dpy_) != 0)
        lose();
}

void WaylandPresenter::cancel_touch()
{
    if (!touches_.empty() && touches_.back().kind == WlTouchKind::Cancel)
        return;
    touches_.push_back({WlTouchKind::Cancel, 0, 0, 0, mono_ms()});
}

void WaylandPresenter::settle()
{
    if (lost_)
        return;
    if (ping_due_) {
        xdg_wm_base_pong(wm_, ping_);
        ping_due_ = false;
    }
    if (top_.serial_due) {
        xdg_surface_ack_configure(top_.xdg, top_.serial);
        top_.serial_due = false;
        top_.configured = true;
    }
    if ((top_.closed || (role_ == WlRole::Layer && layer_.closed)) && !closed_) {
        closed_ = true;
        signal();
    }
    if (act_ != activated_) {
        activated_ = act_;
        if (activated_ && background_) {
            background_ = false;
            activations_++;
        } else if (!activated_ && ever_) {
            background_ = true;
            cancel_touch();
        }
        ever_ = ever_ || activated_;
    }
    if (role_ == WlRole::Layer) {
        if (layer_.serial_due && !layer_.closed) {
            zwlr_layer_surface_v1_ack_configure(layer_.layer, layer_.serial);
            layer_.serial_due = false;
            int w = layer_.cw, h = layer_.ch;
            if (w > 0 && h > 0 && (w != out_w_ || h != out_h_) && (w != told_w_ || h != told_h_)) {
                fprintf(stderr, "funeos: wayland: layer configure %dx%d, the buffer is %dx%d\n", w, h, out_w_, out_h_);
                told_w_ = w;
                told_h_ = h;
            }
        }
    } else if (layer_.closed) {
        hide_layer();
    } else if (layer_.serial_due) {
        zwlr_layer_surface_v1_ack_configure(layer_.layer, layer_.serial);
        layer_.serial_due = false;
        if (!layer_.configured && make_pool(layer_)) {
            layer_.configured = true;
            if (top_.cb)
                wl_callback_destroy(top_.cb);
            top_.cb = nullptr;
            live_ = &layer_;
            full_ = true;
            signal();
        }
    }
}

bool WaylandPresenter::pump()
{
    if (!dpy_ || lost_)
        return false;
    while (wl_display_prepare_read(dpy_) != 0) {
        if (wl_display_dispatch_pending(dpy_) < 0) {
            lose();
            return false;
        }
    }
    if (wl_display_read_events(dpy_) < 0 || wl_display_dispatch_pending(dpy_) < 0) {
        lose();
        return false;
    }
    check();
    settle();
    std::vector<KeyEv> keys;
    keys.swap(keys_);
    for (const KeyEv& k : keys) {
        if (k.kind == kKeymap && keymap_cb_)
            keymap_cb_(k.fd, k.size);
        else if (k.kind == kKeymap)
            close(k.fd);
        else if (k.kind == kKey && key_cb_)
            key_cb_(k.code, k.pressed);
        else if ((k.kind == kEnter || k.kind == kLeave) && focus_cb_)
            focus_cb_(k.kind == kEnter);
    }
    return !lost_;
}

void WaylandPresenter::flush()
{
    blocked_ = false;
    if (!dpy_ || lost_)
        return;
    if (wl_display_flush(dpy_) < 0) {
        if (errno == EAGAIN)
            blocked_ = true;
        else
            lose();
    }
    check();
}

bool WaylandPresenter::show_layer()
{
    if (!shell_ || lost_ || role_ == WlRole::Layer)
        return false;
    if (layer_.wl)
        return true;
    layer_.self = this;
    layer_.wl = wl_compositor_create_surface(comp_);
    layer_.layer = zwlr_layer_shell_v1_get_layer_surface(shell_, layer_.wl, nullptr, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
                                                         "funeos");
    g_layer_l.configure = layer_configure;
    g_layer_l.closed = layer_closed;
    zwlr_layer_surface_v1_add_listener(layer_.layer, &g_layer_l, &layer_);
    layer_.px = Px::Layer;
    zwlr_layer_surface_v1_set_size(layer_.layer, (uint32_t)out_w_, (uint32_t)out_h_);
    zwlr_layer_surface_v1_set_exclusive_zone(layer_.layer, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        layer_.layer, layer_focus_ && shell_ver_ >= 4 ? ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND
                                                      : ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
    wl_surface_commit(layer_.wl);
    return true;
}

void WaylandPresenter::hide_layer()
{
    if (!layer_.wl || role_ == WlRole::Layer)
        return;
    bool was = live_ == &layer_;
    live_ = &top_;
    drop_surf(layer_);
    if (!was)
        return;
    top_.owed[0] = top_.owed[1] = kScreen;
    full_ = true;
    cancel_touch();
    signal();
}

bool WaylandPresenter::take_touch(std::vector<WlTouch>& out)
{
    out.clear();
    out.swap(touches_);
    return !out.empty();
}

void WaylandPresenter::stop()
{
    for (const KeyEv& k : keys_)
        if (k.kind == kKeymap)
            close(k.fd);
    keys_.clear();
    for (Fb& f : fb_) {
        if (f.wl)
            wp_presentation_feedback_destroy(f.wl);
        f = {};
    }
    fq_n_ = 0;
    live_ = &top_;
    drop_surf(layer_);
    drop_surf(top_);
    if (kbd_)
        wl_keyboard_destroy(kbd_);
    kbd_ = nullptr;
    if (touch_)
        wl_touch_destroy(touch_);
    touch_ = nullptr;
    if (seat_)
        wl_seat_destroy(seat_);
    seat_ = nullptr;
    if (shell_ && shell_ver_ >= 3)
        zwlr_layer_shell_v1_destroy(shell_);
    else if (shell_)
        wl_proxy_destroy(reinterpret_cast<wl_proxy*>(shell_));
    shell_ = nullptr;
    if (wm_)
        xdg_wm_base_destroy(wm_);
    wm_ = nullptr;
    if (pres_)
        wp_presentation_destroy(pres_);
    pres_ = nullptr;
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
    if (connect_fd_ >= 0)
        close(connect_fd_);
    connect_fd_ = -1;
    if (efd_ >= 0)
        close(efd_);
    efd_ = -1;
}

}
