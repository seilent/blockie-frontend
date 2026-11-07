#pragma once
#include <cstdint>

namespace fd {

enum class NavOrder : uint8_t { BackLeft, BackRight };
enum class NavButton : uint8_t { Play, NowPlaying, Bluetooth, Next, Shuffle, Search, Settings };
constexpr int kNavButtons = 7;

}
