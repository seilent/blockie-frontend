#include <memory>
#include <unistd.h>
#include <vector>

#include "../gfx/wayland.h"
#include "layer_present.h"

namespace fd {

namespace {

class WaylandLayer : public LayerPresenter {
public:
    bool open() override
    {
        p_->set_role(WlRole::Layer);
        p_->set_app_id("blockie-powermenu");
        p_->set_output(kPortraitW, kPortraitH);
        return p_->open("direct", true);
    }
    Surface frame() override { return p_->draw_target(); }
    bool ready() const override { return p_->claim(); }
    uint32_t present(const std::vector<AlphaRegion>& regions) override;
    int fd() const override { return p_->display_fd(); }
    bool pump(std::vector<PresentNote>& out) override;

private:
    std::unique_ptr<WaylandPresenter> p_ = std::make_unique<WaylandPresenter>();
    std::vector<WlAlpha> alpha_;
    std::vector<WlFeedback> fb_;
};

uint32_t WaylandLayer::present(const std::vector<AlphaRegion>& regions)
{
    alpha_.clear();
    Rect box{};
    for (const AlphaRegion& g : regions) {
        alpha_.push_back({g.r, region_a8(g.a256)});
        box = box.unite(g.r.intersect(kScreen));
    }
    if (!p_->set_alpha(alpha_.data(), (int)alpha_.size()))
        return 0;
    p_->present(box.empty() ? Rect{0, 0, 1, 1} : box);
    p_->flip();
    p_->flush();
    return p_->posted() ? p_->commits() : 0;
}

bool WaylandLayer::pump(std::vector<PresentNote>& out)
{
    bool ok = p_->pump();
    p_->flush();
    uint64_t v = 0;
    if (p_->event_fd() >= 0 && read(p_->event_fd(), &v, sizeof v) < 0)
        v = 0;
    p_->take_feedback(fb_);
    for (const WlFeedback& f : fb_)
        out.push_back({f.commit, f.presented});
    return ok && !p_->closed();
}

}

std::unique_ptr<LayerPresenter> make_layer_presenter() { return std::make_unique<WaylandLayer>(); }

}
