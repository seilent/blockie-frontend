#pragma once
#include <cstdint>

#include "input_pad.h"

struct xkb_context;
struct xkb_keymap;
struct xkb_state;

namespace fd {

bool evdev_action(uint32_t code, PadAction& a);
bool keysym_action(uint32_t sym, PadAction& a);

class WlKeys {
public:
    WlKeys() = default;
    ~WlKeys();
    WlKeys(const WlKeys&) = delete;
    WlKeys& operator=(const WlKeys&) = delete;

    void keymap(int fd, uint32_t size);
    bool action(uint32_t code, PadAction& a) const;

private:
    xkb_context* ctx_ = nullptr;
    xkb_keymap* map_ = nullptr;
    xkb_state* state_ = nullptr;
};

}
