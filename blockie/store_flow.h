#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

#include "children.h"
#include "homeflow.h"
#include "ports.h"
#include "screen_store.h"
#include "store.h"

namespace fd {

struct StoreHost {
    std::function<int64_t()> now;
    std::function<bool(int sys)> is_ports;
    std::function<bool()> online;
    std::function<bool()> stale;
    std::function<std::vector<IndexPort>()> index;
    std::function<SteamState()> steam;
    std::function<void()> rescan;
    std::function<void()> reread;
    std::function<void(const std::vector<HomeGame>& shown, const std::vector<HomeGame>& want)> preview_art;
    std::function<void(const StoreItem* it)> shot;
    std::function<void(const std::string& line1, const std::string& line2)> toast;
    std::function<void()> closed;
    std::function<void()> redraw;
    std::function<void(const HelperResult& r)> result;
};

class StoreFlow {
public:
    StoreFlow(HomeFlow& flow, PortPage& pp, PortsHelper& helper, Children& kids);
    StoreFlow(const StoreFlow&) = delete;
    StoreFlow& operator=(const StoreFlow&) = delete;

    StoreHost host;
    Catalog catalog;
    bool have_catalog = false;
    std::vector<StoreItem> items;
    std::vector<StoreGenre> rows;
    bool l2 = false;

    void attach();
    void set_catalog(bool ok, Catalog c, int64_t now);
    void rebuild(int64_t now);
    std::vector<HomeGame> games(int sys) const;
    const StoreItem* item(int64_t id) const;
    bool entry(int sys, int64_t now);
    bool page_input(Screen s, PadAction a, bool repeat, int64_t now);
    int seed_y(Screen s) const;
    void tick(int64_t now);
    int64_t deadline() const;
    void online_changed(int64_t now);
    SelectorModel model() const;

private:
    std::vector<HomeGame> preview(int k) const;
    void deal();
    void settle(int64_t now);
    void open_port(const StoreItem& it, int64_t now);
    void ask(HelperCmd c, int64_t now);
    bool login_ready() const;
    void login(bool on, int64_t now);
    void login_push(int64_t now);
    void login_pop(int64_t now);
    void shot(const StoreItem* it);
    void result(const HelperResult& r);
    void toast(const StoreToast& t, int64_t now);

    HomeFlow& flow_;
    PortPage& pp_;
    PortsHelper& helper_;
    Children& kids_;
    std::unordered_set<std::string> updated_;
    std::string copying_;
    bool login_due_ = false;
    bool shot_due_ = false;
    int64_t preview_at_ = -1;
    bool grid_due_ = false;
    bool leave_due_ = false;
    bool rest_due_ = false;
};

HomeGame store_game(const StoreItem& it);

}
