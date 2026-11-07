#pragma once

#ifdef FD_BOARD_HEADER
#include FD_BOARD_HEADER
#elif defined(FD_BOARD_R1)
#include "r1.h"
#define FD_PIX16 1
#elif defined(FD_BOARD_V1)
#include "v1.h"
#define FD_PIX16 1
#elif defined(FD_BOARD_A105)
#include "a105.h"
#define FD_PREFETCH 1
#else
#include "ap80.h"
#define FD_ROT 1
#endif

static_assert(fd::board::kMem.glyphs_scaled <= fd::board::kMem.glyphs, "scaled glyphs fit the glyph cap");
static_assert(fd::board::kMem.read_chunk <= fd::board::kMem.read_ahead, "a read chunk fits the read ahead ring");
static_assert(fd::board::kMem.gate != fd::board::GateMode::Net ||
                  (fd::board::kMem.ledger && fd::board::kMem.gate_net_kb > 0),
              "a net gate needs the ledger and a net value");
static_assert(!fd::board::kMem.ledger || fd::board::kMem.ram_kb > 0, "a ledger board names its ram");

#ifndef FD_BOARD_HEADER
namespace fd::board {
constexpr bool kSwapKeys = kKeyPrev >= 0 && kKeyNext >= 0 && kKeyMap == KeyMap::Icons;
}

#ifdef FD_PREFETCH
static_assert(fd::board::kMem.prefetch > 0, "a prefetch board has a prefetch window");
#else
static_assert(fd::board::kMem.prefetch == 0, "a prefetch window needs FD_PREFETCH");
#endif
static_assert((fd::board::kJackKind == fd::board::JackKind::Switch) == (*fd::board::kJackSwitch != '\0'),
              "a switch jack names its path");
static_assert((fd::board::kJackKind == fd::board::JackKind::Extcon) == (*fd::board::kJackExtcon != '\0'),
              "an extcon jack names its class dir and device");
static_assert(!fd::board::kJackDetectMs || (fd::board::kPcmNeedsJack && fd::board::kOutputPortOff >= 0),
              "a codec jack detect wait needs the jack gate and a route off value");
static_assert(!fd::board::kPcmRateMax || fd::board::kPcmRateMax >= 192000, "a PCM ceiling keeps 192 kHz");
#endif
