#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <dirent.h>

#include "../gfx/canvas.h"
#include "../gfx/font.h"
#include "../gfx/glyph.h"
#include "../gfx/pixel.h"
#include "../gfx/png.h"
#include "../ui/roles.h"
#include "bt_model.h"
#include "fold.h"
#include "image.h"
#include "ink.h"
#include "screen_bt.h"
#include "screen_selector.h"
#include "screen_settings.h"
#include "theme.h"
#include "tile_grid.h"

using namespace fd;

namespace {

constexpr int kFrameMs = 1000 / 60;

std::shared_ptr<const Image> load_tile(const std::string& path)
{
    DecodedImage d;
    if (!load_png(path, d) || !d.ok())
        return nullptr;
    return std::make_shared<const Image>(cover_tile(d, kTileSide));
}

std::vector<std::string> pngs_in(const std::string& dir)
{
    std::vector<std::string> names;
    DIR* dp = opendir(dir.c_str());
    if (!dp)
        return names;
    for (struct dirent* e = readdir(dp); e; e = readdir(dp)) {
        std::string n = e->d_name;
        if (n.size() > 4 && n.compare(n.size() - 4, 4, ".png") == 0)
            names.push_back(n);
    }
    closedir(dp);
    std::sort(names.begin(), names.end());
    for (std::string& n : names)
        n = dir + "/" + n;
    return names;
}

std::vector<std::shared_ptr<const Image>> load_tiles(const std::string& dir)
{
    std::vector<std::shared_ptr<const Image>> tiles;
    for (const std::string& p : pngs_in(dir)) {
        if (tiles.size() >= 8)
            break;
        if (std::shared_ptr<const Image> img = load_tile(p))
            tiles.push_back(std::move(img));
        else
            fprintf(stderr, "blockie-shot: skip %s\n", p.c_str());
    }
    return tiles;
}

BtDevice shot_device(const char* mac, const char* alias, bool paired, bool connected, const char* uuid)
{
    BtDevice d;
    std::string tail = mac;
    std::replace(tail.begin(), tail.end(), ':', '_');
    d.path = "/org/bluez/hci0/dev_" + tail;
    d.adapter = "/org/bluez/hci0";
    d.address = mac;
    d.alias = alias;
    d.paired = paired;
    d.connected = connected;
    d.uuids = {uuid};
    return d;
}

int shot_bt(const std::string& out, const std::string& state, Fonts& fonts)
{
    const char* sink = "0000110b-0000-1000-8000-00805f9b34fb";
    const char* hid = "00001124-0000-1000-8000-00805f9b34fb";
    int64_t now = 1000000;
    BtState s;
    if (state != "unavailable") {
        s.up = true;
        s.adapter.path = "/org/bluez/hci0";
        s.adapter.powered = state != "off";
    }
    if (state != "unavailable" && state != "empty")
        s.devices = {shot_device("02:00:00:00:00:01", "Synth Headset", true, true, sink),
                     shot_device("02:00:00:00:00:02", "Synth Buds", true, false, sink),
                     shot_device("02:00:00:00:00:03", "Synth Pad", false, false, hid),
                     shot_device("02:00:00:00:00:04", "Synth Speaker", false, false, sink)};
    if (state == "searching") {
        s.searching = true;
        s.search_end = now + 88000;
    }
    if (state == "confirm") {
        s.pair_target = s.devices[3].path;
        s.prompt = BtPrompt{BtPrompt::Confirm, s.devices[3].path, "", 123456};
    }
    GlyphCache glyphs;
    BtPage page(fonts, glyphs);
    page.open(now);
    page.set_model(s, now);
    if (state == "sheet") {
        page.action(PadAction::Down, false, now);
        page.action(PadAction::Context, false, now);
    }
    page.land(now);
    Buffer buf;
    Canvas c(buf.surface());
    InkLayer ink;
    page.draw(c, ink, now + 1000);
    if (!write_png(out, buf.surface())) {
        fprintf(stderr, "blockie-shot: cannot write %s\n", out.c_str());
        return 1;
    }
    return 0;
}

std::string frame_path(const std::string& dir, int i)
{
    char name[32];
    snprintf(name, sizeof name, "/f%04d.png", i);
    return dir + name;
}

}

int main(int argc, char** argv)
{
    std::string out;
    std::string font_dir;
    std::string tiles_dir;
    std::string recent_dir;
    std::string header;
    std::string page;
    std::string fold_dir;
    std::string page_flip;
    std::string frames_dir;
    std::string bt_sub;
    std::string bt_state;
    int tile_focus = -1;
    int sys_focus = 0;
    int systems = 6;
    int menu_top = 0;
    int sheet_focus = -1;
    bool items = false;
    bool sheet = false;
    bool light = false;
    Pane focus = Pane::Selector;
    ArtFit fit = ArtFit::Cover;
    bool scatter = false;
    bool detail = false;
    std::string game_system;
    std::string game_title;
    if (const char* env = getenv("BLOCKIE_FONTS"))
        font_dir = env;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--focus" && i + 1 < argc)
            focus = std::string(argv[++i]) == "details" ? Pane::Details : Pane::Selector;
        else if (a == "--fonts" && i + 1 < argc)
            font_dir = argv[++i];
        else if (a == "--tiles" && i + 1 < argc)
            tiles_dir = argv[++i];
        else if (a == "--recent" && i + 1 < argc)
            recent_dir = argv[++i];
        else if (a == "--header" && i + 1 < argc)
            header = argv[++i];
        else if (a == "--tilefocus" && i + 1 < argc)
            tile_focus = atoi(argv[++i]);
        else if (a == "--sysfocus" && i + 1 < argc)
            sys_focus = atoi(argv[++i]);
        else if (a == "--systems" && i + 1 < argc)
            systems = atoi(argv[++i]);
        else if (a == "--menutop" && i + 1 < argc)
            menu_top = atoi(argv[++i]);
        else if (a == "--items")
            items = true;
        else if (a == "--btsub" && i + 1 < argc)
            bt_sub = argv[++i];
        else if (a == "--bt" && i + 1 < argc)
            bt_state = argv[++i];
        else if (a == "--light")
            light = true;
        else if (a == "--page" && i + 1 < argc)
            page = argv[++i];
        else if (a == "--sheet")
            sheet = true;
        else if (a == "--sheetfocus" && i + 1 < argc)
            sheet_focus = atoi(argv[++i]);
        else if (a == "--fold" && i + 1 < argc)
            fold_dir = argv[++i];
        else if (a == "--pageflip" && i + 1 < argc)
            page_flip = argv[++i];
        else if (a == "--frames" && i + 1 < argc)
            frames_dir = argv[++i];
        else if (a == "--scatter")
            scatter = true;
        else if (a == "--gamedetail" && i + 1 < argc) {
            std::string v = argv[++i];
            size_t bar = v.find('|');
            game_system = bar == std::string::npos ? std::string() : v.substr(0, bar);
            game_title = bar == std::string::npos ? v : v.substr(bar + 1);
            detail = true;
            focus = Pane::Details;
        }
        else if (out.empty())
            out = a;
        else {
            fprintf(stderr, "blockie-shot: unexpected argument %s\n", a.c_str());
            return 2;
        }
    }
    bool sequence = !fold_dir.empty() || !page_flip.empty();
    if ((out.empty() && !sequence) || (sequence && frames_dir.empty())) {
        fprintf(stderr,
                "usage: blockie-shot OUT.png [--focus selector|details] [--fonts DIR] [--tiles DIR] [--header TEXT]"
                " [--tilefocus N] [--sysfocus N] [--systems N] [--menutop N] [--items] [--btsub TEXT]"
                " [--light] [--page settings] [--sheet] [--sheetfocus N] [--scatter] [--gamedetail SYSTEM|TITLE]"
                " [--bt unavailable|off|empty|list|searching|sheet|confirm]\n"
                "       blockie-shot --fold ab|ba --frames DIR [--recent DIR] [same options]\n"
                "       blockie-shot --pageflip push|pop --frames DIR [same options]\n");
        return 2;
    }
    if (font_dir.empty())
        font_dir = "fonts";
    set_theme_light(light);

    Fonts fonts;
    if (!fonts.open(font_dir, {})) {
        fprintf(stderr, "blockie-shot: fonts not found in %s\n", font_dir.c_str());
        return 1;
    }
    fonts.begin_frame(-1);
    if (!bt_state.empty())
        return shot_bt(out, bt_state, fonts);

    static const char* const kNames[] = {"GB", "GBC", "NES", "SNES", "Genesis", "GBA", "PS1", "N64", "PSP", "DC"};
    static const int kCounts[] = {28, 19, 63, 47, 35, 52, 31, 22, 14, 9};
    SelectorModel model;
    systems = std::max(1, std::min(systems, 10));
    for (int i = 0; i < systems; i++)
        model.systems.push_back({kNames[i], kCounts[i], ""});
    if (items) {
        model.systems.push_back({"bluetooth", 0, bt_sub.empty() ? "not connected" : bt_sub});
        model.systems.push_back({"settings", 0, "display"});
    }
    int count = (int)model.systems.size();
    model.focus = sys_focus < 0 || sys_focus >= count ? 0 : sys_focus;
    model.menu_top_px = menu_top * kMenuPitchPx;

    if (!tiles_dir.empty())
        model.tiles = load_tiles(tiles_dir);

    model.section_label = header;
    if (focus == Pane::Details && tile_focus < 0)
        tile_focus = 0;
    model.tile_focus = tile_focus;
    model.game_system = game_system;
    model.game_title = game_title;

    InkLayer ink;
    SettingsView sv;
    sv.light = light;
    sv.sheet = sheet ? kOne : 0;
    sv.sheet_focus = sheet_focus >= 0 ? sheet_focus : (light ? 1 : 0);

    if (!fold_dir.empty()) {
        SelectorModel menu = model;
        menu.section_label.clear();
        menu.game_system.clear();
        menu.game_title.clear();
        menu.tile_focus = -1;
        if (!recent_dir.empty())
            menu.tiles = load_tiles(recent_dir);
        Buffer sel;
        Buffer det;
        Canvas sc(sel.surface());
        draw_selector(sc, fonts, menu, Pane::Selector, fit, scatter, false);
        Canvas dc(det.surface());
        draw_selector(dc, fonts, model, Pane::Details, fit, scatter, true);
        bool ab = fold_dir == "ab";
        FoldPlan plan = fold_plan(ab ? FoldDir::ToDetails : FoldDir::ToSelector, model.focus - menu_top,
                                  detail_title_bottom(fonts, model));
        int64_t dur = fold_duration(plan);
        Buffer outb;
        int n = 0;
        for (int64_t t = 0;; t += kFrameMs) {
            int64_t tt = std::min(t, dur);
            fold_compose(ab ? sel.surface() : det.surface(), ab ? det.surface() : sel.surface(), outb.surface(), plan, tt);
            if (!write_png(frame_path(frames_dir, n++), outb.surface()))
                return 1;
            if (tt >= dur)
                break;
        }
        fprintf(stderr, "blockie-shot: fold %s row %d rings %d %lld ms %d frames\n", fold_dir.c_str(),
                model.focus - menu_top, plan.maxring + 1, (long long)dur, n);
        return 0;
    }

    if (!page_flip.empty()) {
        bool push = page_flip == "push";
        Buffer menub;
        Buffer pageb;
        Canvas mc(menub.surface());
        draw_selector(mc, fonts, model, Pane::Selector, fit, scatter, false);
        Canvas pc(pageb.surface());
        draw_settings(pc, fonts, ink, sv, theme());
        FoldPlan plan = fold_plan(push ? FoldDir::PagePush : FoldDir::PagePop, model.focus - menu_top, 0);
        int64_t dur = fold_duration(plan);
        Buffer outb;
        int n = 0;
        for (int64_t t = 0;; t += kFrameMs) {
            int64_t tt = std::min(t, dur);
            fold_compose(push ? menub.surface() : pageb.surface(), push ? pageb.surface() : menub.surface(),
                         outb.surface(), plan, tt);
            if (!write_png(frame_path(frames_dir, n++), outb.surface()))
                return 1;
            if (tt >= dur)
                break;
        }
        fprintf(stderr, "blockie-shot: page %s %lld ms %d frames\n", page_flip.c_str(), (long long)dur, n);
        return 0;
    }

    Buffer buf;
    Canvas c(buf.surface());
    if (page == "settings")
        draw_settings(c, fonts, ink, sv, theme());
    else
        draw_selector(c, fonts, model, focus, fit, scatter, detail);

    if (!write_png(out, buf.surface())) {
        fprintf(stderr, "blockie-shot: cannot write %s\n", out.c_str());
        return 1;
    }
    return 0;
}
