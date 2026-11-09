#include "wl_keys.h"

#include <linux/input-event-codes.h>
#include <sys/mman.h>
#include <unistd.h>

#ifdef BLOCKIE_HAVE_XKB
#include <xkbcommon/xkbcommon.h>
#endif

namespace fd {

bool evdev_action(uint32_t code, PadAction& a)
{
    if (code == KEY_UP) a = PadAction::Up;
    else if (code == KEY_DOWN) a = PadAction::Down;
    else if (code == KEY_LEFT) a = PadAction::Left;
    else if (code == KEY_RIGHT) a = PadAction::Right;
    else if (code == KEY_ENTER) a = PadAction::Accept;
    else if (code == KEY_ESC) a = PadAction::Back;
    else if (code == KEY_X) a = PadAction::Context;
    else return false;
    return true;
}

#ifdef BLOCKIE_HAVE_XKB
bool keysym_action(uint32_t sym, PadAction& a)
{
    if (sym == XKB_KEY_Up) a = PadAction::Up;
    else if (sym == XKB_KEY_Down) a = PadAction::Down;
    else if (sym == XKB_KEY_Left) a = PadAction::Left;
    else if (sym == XKB_KEY_Right) a = PadAction::Right;
    else if (sym == XKB_KEY_Return) a = PadAction::Accept;
    else if (sym == XKB_KEY_Escape) a = PadAction::Back;
    else if (sym == XKB_KEY_x) a = PadAction::Context;
    else return false;
    return true;
}
#endif

WlKeys::~WlKeys()
{
#ifdef BLOCKIE_HAVE_XKB
    if (state_)
        xkb_state_unref(state_);
    if (map_)
        xkb_keymap_unref(map_);
    if (ctx_)
        xkb_context_unref(ctx_);
#endif
}

void WlKeys::keymap(int fd, uint32_t size)
{
#ifdef BLOCKIE_HAVE_XKB
    char* str = static_cast<char*>(mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0));
    close(fd);
    if (str == MAP_FAILED)
        return;
    if (!ctx_)
        ctx_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    xkb_keymap* map = ctx_ ? xkb_keymap_new_from_string(ctx_, str, XKB_KEYMAP_FORMAT_TEXT_V1,
                                                        XKB_KEYMAP_COMPILE_NO_FLAGS)
                           : nullptr;
    munmap(str, size);
    if (!map)
        return;
    xkb_state* st = xkb_state_new(map);
    if (!st) {
        xkb_keymap_unref(map);
        return;
    }
    if (state_)
        xkb_state_unref(state_);
    if (map_)
        xkb_keymap_unref(map_);
    map_ = map;
    state_ = st;
#else
    (void)size;
    close(fd);
#endif
}

bool WlKeys::action(uint32_t code, PadAction& a) const
{
#ifdef BLOCKIE_HAVE_XKB
    if (state_)
        return keysym_action(xkb_state_key_get_one_sym(state_, code + 8), a);
#endif
    return evdev_action(code, a);
}

}
