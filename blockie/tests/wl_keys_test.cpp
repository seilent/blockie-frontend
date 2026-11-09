#include <cstdio>
#include <cstring>

#include <dirent.h>
#include <linux/input-event-codes.h>
#include <sys/mman.h>
#include <unistd.h>

#ifdef BLOCKIE_KEYS_XKB
#include <xkbcommon/xkbcommon.h>
#endif

#include "../wl_keys.h"

using namespace fd;

namespace {

int g_fail = 0;
int g_total = 0;

void check(bool ok, const char* what)
{
    g_total++;
    if (ok) {
        std::printf("PASS %s\n", what);
    } else {
        std::printf("FAIL %s\n", what);
        g_fail++;
    }
}

int fd_count()
{
    DIR* d = opendir("/proc/self/fd");
    if (!d)
        return -1;
    int n = 0;
    while (readdir(d))
        n++;
    closedir(d);
    return n;
}

int keymap_fd(const char* text, uint32_t& size)
{
    size = (uint32_t)std::strlen(text) + 1;
    int fd = memfd_create("keymap", MFD_CLOEXEC);
    if (fd >= 0 && write(fd, text, size) != (ssize_t)size) {
        close(fd);
        fd = -1;
    }
    return fd;
}

const char* keymap_text(const char* sym)
{
    static char buf[512];
    std::snprintf(buf, sizeof buf,
                  "xkb_keymap {\n"
                  "xkb_keycodes \"t\" { minimum = 8; maximum = 255; <K1> = %d; };\n"
                  "xkb_types \"t\" { type \"ONE_LEVEL\" { modifiers = none; level_name[Level1] = \"Any\"; }; };\n"
                  "xkb_compatibility \"t\" { };\n"
                  "xkb_symbols \"t\" { key <K1> { [ %s ] }; };\n"
                  "};\n",
                  KEY_UP + 8, sym);
    return buf;
}

bool acts(uint32_t code, PadAction want)
{
    PadAction a = PadAction::Up;
    return evdev_action(code, a) && a == want;
}

struct Guarded {
    WlKeys keys;
    unsigned char tail[32];
};

}

int main()
{
    PadAction a = PadAction::Up;
    check(acts(KEY_UP, PadAction::Up) && acts(KEY_DOWN, PadAction::Down) && acts(KEY_LEFT, PadAction::Left) &&
              acts(KEY_RIGHT, PadAction::Right) && acts(KEY_ENTER, PadAction::Accept) &&
              acts(KEY_ESC, PadAction::Back) && acts(KEY_X, PadAction::Context),
          "K1 the evdev codes map to Up, Down, Left, Right, Accept, Back, Context");
    check(!evdev_action(KEY_A, a) && !evdev_action(KEY_UP + 8, a) && !evdev_action(0, a),
          "K1 any other evdev code maps to none");
#ifdef BLOCKIE_KEYS_XKB
    auto sym_acts = [](uint32_t sym, PadAction want) {
        PadAction s = PadAction::Up;
        return keysym_action(sym, s) && s == want;
    };
    check(sym_acts(XKB_KEY_Up, PadAction::Up) && sym_acts(XKB_KEY_Down, PadAction::Down) &&
              sym_acts(XKB_KEY_Left, PadAction::Left) && sym_acts(XKB_KEY_Right, PadAction::Right) &&
              sym_acts(XKB_KEY_Return, PadAction::Accept) && sym_acts(XKB_KEY_Escape, PadAction::Back) &&
              sym_acts(XKB_KEY_x, PadAction::Context),
          "K1 the keysyms map to Up, Down, Left, Right, Accept, Back, Context");
    check(!keysym_action(XKB_KEY_a, a) && !keysym_action(XKB_KEY_X, a) && !keysym_action(XKB_KEY_space, a),
          "K1 any other keysym maps to none");
#endif

    Guarded g;
    std::memset(g.tail, 0xA5, sizeof g.tail);
    WlKeys& keys = g.keys;
    check(keys.action(KEY_DOWN, a) && a == PadAction::Down, "K2 with no keymap a key maps by its evdev code");
    int before = fd_count();
    uint32_t size = 0;
    int f1 = keymap_fd(keymap_text("Up"), size);
    keys.keymap(f1, size);
    bool first = keys.action(KEY_UP, a);
    PadAction a1 = a;
    int f2 = keymap_fd(keymap_text("Down"), size);
    keys.keymap(f2, size);
    bool second = keys.action(KEY_UP, a);
    PadAction a2 = a;
    int f3 = keymap_fd("xkb_keymap { not a keymap };\n", size);
    keys.keymap(f3, size);
    bool third = keys.action(KEY_UP, a);
    PadAction a3 = a;
    int after = fd_count();
    check(f1 >= 0 && f2 >= 0 && f3 >= 0 && before > 0 && before == after,
          "K2 every keymap fd is closed, the fd count is unchanged after the keymaps");
    bool intact = true;
    for (unsigned char b : g.tail)
        intact = intact && b == 0xA5;
    check(intact, "K2 the WlKeys this test sees has the layout wl_keys.o writes: the bytes after it are untouched");
#ifdef BLOCKIE_KEYS_XKB
    check(first && a1 == PadAction::Up && second && a2 == PadAction::Down,
          "K2 the latest keymap wins: KEY_UP reads Up, then Down");
    check(third && a3 == PadAction::Down, "K2 a keymap that does not compile keeps the one before");
#else
    check(first && a1 == PadAction::Up && second && a2 == PadAction::Up && third && a3 == PadAction::Up,
          "K2 without xkbcommon a key maps by its evdev code whatever keymap came");
#endif

    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
