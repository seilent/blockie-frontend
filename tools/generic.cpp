#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

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
#include "../gfx/mempresent.h"
#include "../gfx/mip.h"
#include "../gfx/pagealloc.h"
#include "../gfx/pairs.h"
#include "../gfx/pixel.h"
#include "../gfx/png.h"
#include "../gfx/present.h"
#include "../gfx/shape.h"
#include "../gfx/turn.h"
#include "../gfx/warp.h"
#include "../gfx/wayland.h"
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
#include "../ui/tilt.h"

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

template <class P>
constexpr bool kLayerSet = std::is_same_v<decltype(ref<P>().set_role(WlRole::Layer)), void> &&
                           std::is_same_v<decltype(ref<P>().set_translucent(false)), void> &&
                           std::is_same_v<decltype(ref<P>().set_alpha(ref<const WlAlpha*>(), 0)), bool> &&
                           std::is_same_v<decltype(ref<const P>().has_presentation()), bool> &&
                           std::is_same_v<decltype(ref<P>().take_feedback(ref<std::vector<WlFeedback>>())), bool> &&
                           std::is_same_v<decltype(ref<const P>().posted()), bool> &&
                           std::is_same_v<decltype(ref<const P>().commits()), uint32_t> &&
                           std::is_same_v<decltype(ref<const P>().lost()), bool> &&
                           std::is_same_v<decltype(ref<const P>().closed()), bool> &&
                           std::is_same_v<decltype(ref<const P>().display_fd()), int> &&
                           std::is_same_v<decltype(ref<const P>().event_fd()), int> &&
                           std::is_same_v<decltype(ref<P>().pump()), bool> &&
                           std::is_same_v<decltype(ref<P>().flush()), void> &&
                           std::is_same_v<decltype(ref<P>().set_app_id(ref<const std::string>())), void> &&
                           std::is_same_v<decltype(ref<P>().set_output(0, 0)), void> &&
                           std::is_same_v<decltype(ref<const P>().copy_ns()), uint64_t> &&
                           std::is_same_v<decltype(ref<const P>().copies()), uint32_t>;

#ifndef FD_BOARD_HEADER
static_assert(kCore<Presenter>, "the board presenter keeps the core set");
#endif
static_assert(kMemSet<PresenterT<X888, Rows>>, "the memory presenter keeps its set");
static_assert(std::is_same_v<decltype(ref<const FrameSources>().retained(ref<const Op>(), ref<RetainedOps>())), bool>,
              "frame sources keep the retained hook");
static_assert(kCore<WaylandPresenter>, "the wayland presenter keeps the core set");
static_assert(kLayerSet<WaylandPresenter>, "the wayland presenter keeps the layer set");
static_assert(premul_px(0xFFFFFF, 245) == 0xF5F5F5F5u && premul_px(0, 245) == 0xF5000000u &&
                  premul_px(0x0078D7, 0) == 0 && premul_px(0xFFFFFF, 0) == 0 &&
                  premul_px(0x0078D7, 255) == 0xFF0078D7u && premul_px(0x0078D7, 140) == 0x8C004276u &&
                  premul_px(0xFF0078D7, 140) == 0x8C004276u,
              "premul_px rounds per channel and reads no top byte");
static_assert(std::is_same_v<decltype(WlAlpha::r), Rect> && std::is_same_v<decltype(WlAlpha::a), uint8_t> &&
                  std::is_same_v<decltype(WlFeedback::commit), uint32_t> &&
                  std::is_same_v<decltype(WlFeedback::presented), bool> &&
                  std::is_same_v<decltype(WlFeedback::visible), bool> && kWlAlphaMax == 16,
              "the region and feedback records keep their fields");

void mem_use()
{
    PresenterT<X888, Rows> p;
    p.open(PresentMode::Copy);
    p.present(kScreen);
    p.present_warp();
    p.flip();
    p.screen();
#if defined(FD_BOARD_PB) || defined(FD_BOARD_HEADER)
    WaylandPresenter w;
    w.set_output(kPortraitW, kPortraitH);
    if (w.open("copy", true)) {
        w.present(kScreen);
        w.flip();
        w.pump();
        w.flush();
    }
#endif
}

void layer_use()
{
#if defined(FD_BOARD_PB) || defined(FD_BOARD_HEADER)
    WaylandPresenter l;
    l.set_role(WlRole::Layer);
    std::vector<WlFeedback> fb;
    if (l.open("direct", true)) {
        WlAlpha a{kScreen, 255};
        l.set_alpha(&a, 1);
        l.present(kScreen);
        l.flip();
        l.take_feedback(fb);
    }
#endif
}

}

}

int main(int argc, char** argv)
{
    void (*volatile use)() = fd::mem_use;
    void (*volatile layer)() = fd::layer_use;
    if (argc > 1 && std::string(argv[1]) == "--layer")
        layer();
    else if (argc > 1)
        use();
    return 0;
}
