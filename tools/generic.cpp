#include <cstdint>
#include <string>
#include <type_traits>

#include "../board/kinds.h"
#include "../core/curves.h"
#include "../core/event.h"
#include "../core/fx.h"
#include "../core/gesture.h"
#include "../core/log.h"
#include "../core/memledger.h"
#include "../core/press.h"
#include "../core/prof.h"
#include "../core/scroll.h"
#include "../core/tween.h"
#include "../gfx/backdrop.h"
#include "../gfx/bidi.h"
#include "../gfx/canvas.h"
#include "../gfx/compose.h"
#include "../gfx/font.h"
#include "../gfx/fontkey.h"
#include "../gfx/glyph.h"
#include "../gfx/kernels.h"
#include "../gfx/mip.h"
#include "../gfx/pagealloc.h"
#include "../gfx/pairs.h"
#include "../gfx/pixel.h"
#include "../gfx/png.h"
#include "../gfx/present.h"
#include "../gfx/shape.h"
#include "../gfx/warp.h"
#include "../gfx/wrap.h"
#include "../gfx/xform.h"
#include "../lib/kana.h"
#include "../lib/section.h"
#include "../lib/util.h"
#include "../ui/accent.h"
#include "../ui/busydots.h"
#include "../ui/frame.h"
#include "../ui/hud.h"
#include "../ui/keyboard.h"
#include "../ui/list.h"
#include "../ui/navbar.h"
#include "../ui/navkind.h"
#include "../ui/overlay.h"
#include "../ui/pulldown.h"
#include "../ui/qr.h"
#include "../ui/roles.h"
#include "../ui/rows.h"
#include "../ui/scrollbar.h"
#include "../ui/size.h"

namespace fd {

namespace {

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

template <class P>
constexpr bool kMemSet = std::is_same_v<decltype(ref<P>().open(PresentMode::Copy)), void> &&
                         std::is_same_v<decltype(ref<P>().mode()), PresentMode> &&
                         std::is_same_v<decltype(ref<P>().draw_target()), typename P::S> &&
                         std::is_same_v<decltype(ref<P>().warp_target()), typename P::S> &&
                         std::is_same_v<decltype(ref<P>().screen()), typename P::S> &&
                         std::is_same_v<decltype(ref<P>().last()), typename P::S> &&
                         std::is_same_v<decltype(ref<P>().present(Rect{})), void> &&
                         std::is_same_v<decltype(ref<P>().present_warp()), void> &&
                         std::is_same_v<decltype(ref<P>().flip()), void> &&
                         std::is_same_v<decltype(ref<P>().show_at(int64_t{})), int64_t> &&
                         std::is_same_v<decltype(ref<P>().period_ms()), int> &&
                         std::is_same_v<decltype(ref<P>().set_show_lead(0)), void> &&
                         std::is_same_v<decltype(ref<P>().frame_presents()), uint32_t> &&
                         std::is_same_v<decltype(ref<P>().fulls()), uint32_t>;

#ifndef FD_BOARD_HEADER
static_assert(kCore<Presenter>, "the board presenter keeps the core set");
#endif
static_assert(kMemSet<PresenterT<X888, Rows>>, "the memory presenter keeps its set");

void mem_use()
{
    PresenterT<X888, Rows> p;
    p.open(PresentMode::Copy);
    p.present(kScreen);
    p.present_warp();
    p.flip();
    p.screen();
}

}

}

int main(int argc, char**)
{
    void (*volatile use)() = fd::mem_use;
    if (argc > 1)
        use();
    return 0;
}
