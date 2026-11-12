#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "../gfx/wayland.h"
#include "frame_gate.h"
#include "portmsg.h"
#include "theme.h"

namespace fd {

namespace {

std::string env_or(const char* name, const char* fallback)
{
    const char* v = std::getenv(name);
    return v && *v ? v : fallback;
}

class WlMsgWindow : public MsgWindow {
public:
    bool open() override
    {
        p_.set_app_id(kMsgAppId);
        p_.set_title("Blockie");
        p_.set_output(kPortraitW, kPortraitH);
        p_.set_translucent(true);
        return p_.open("direct", true);
    }
    Surface frame() override { return p_.draw_target(); }
    bool ready() override { return p_.claim(); }
    void present(const std::vector<AlphaRegion>& regions) override
    {
        alpha_.clear();
        for (const AlphaRegion& g : regions)
            alpha_.push_back({g.r, region_a8(g.a256)});
        if (!p_.set_alpha(alpha_.data(), (int)alpha_.size()))
            return;
        p_.present(kScreen);
        p_.flip();
        p_.flush();
    }
    int fd() const override { return p_.display_fd(); }
    int event_fd() const override { return p_.event_fd(); }
    bool pump() override
    {
        bool ok = pump_presenter(p_, touch_);
        p_.flush();
        return ok && !p_.closed();
    }
    bool activated() const override { return p_.activated(); }

private:
    WaylandPresenter p_;
    std::vector<WlAlpha> alpha_;
    std::vector<WlTouch> touch_;
};

void serve(int lfd)
{
    set_theme_light(env_or("BLOCKIE_THEME", "dark") == "light");
    std::string font_dir = env_or("BLOCKIE_FONTS", "/usr/share/blockie/fonts");
    Fonts fonts;
    if (!fonts.open(font_dir, {}))
        std::fprintf(stderr, "blockie-portmsg: fonts not found in %s\n", font_dir.c_str());
    WlMsgWindow w;
    MsgServerOpts o;
    o.open_pad = [](PadReader& p) { return p.open(); };
    serve_portmsg(lfd, w, fonts, o);
}

}

}

int main(int argc, char** argv)
{
    using namespace fd;
    std::signal(SIGPIPE, SIG_IGN);
    std::vector<std::string> args(argv + 1, argv + argc);
    MsgCall call;
    if (!parse_call(args, call)) {
        std::fprintf(stderr, "blockie-portmsg: usage: --text T [--title T] [--timeout S] [--wait]"
                             " | --progress [--title T] | --progress-set DONE TOTAL | --choose --title Q | --close\n");
        return kMsgUsage;
    }
    std::string run = env_or("XDG_RUNTIME_DIR", "");
    if (run.empty()) {
        std::fprintf(stderr, "blockie-portmsg: no XDG_RUNTIME_DIR\n");
        return kMsgCancel;
    }
    return run_portmsg_client(call, run + "/blockie-ports", 0, 1, serve);
}
