#include "store_flow.h"

#include <algorithm>

#include "../ui/qr.h"

namespace fd {

HomeGame store_game(const StoreItem& it) { return HomeGame{it.id, it.title, it.porter, size_words(it.size)}; }

StoreFlow::StoreFlow(HomeFlow& flow, PortPage& pp, PortsHelper& helper, Children& kids)
    : flow_(flow), pp_(pp), helper_(helper), kids_(kids)
{
}

void StoreFlow::attach()
{
    flow_.store_entry = [this](int sys) { return entry(sys, host.now()); };
    flow_.store_empty = [this](int sys) { return entry(sys, host.now()); };
    flow_.store_moved = [this]() { preview_at_ = host.now() + kStorePreviewRestMs; };
    flow_.store_closed = [this]() {
        preview_at_ = -1;
        grid_due_ = false;
        leave_due_ = false;
        rest_due_ = false;
        shot(nullptr);
        host.closed();
    };
    flow_.open_port = [this](const HomeGame& g) {
        if (const StoreItem* it = item(g.id))
            open_port(*it, host.now());
    };
    flow_.port_context = [this](int i) {
        if (!host.is_ports(flow_.nav.focus) || i < 0 || i >= (int)flow_.home.sysgames.size())
            return false;
        int64_t gid = flow_.home.sysgames[(size_t)i].id;
        rebuild(host.now());
        for (const StoreItem& it : items)
            if (it.game_id == gid) {
                open_port(it, host.now());
                return true;
            }
        return false;
    };
    helper_.on_result = [this](const HelperResult& r) { result(r); };
    helper_.on_progress = [this](const HelperProgress& p) {
        if (p.what == "copy" && helper_.cmd() == HelperCmd::SteamGet)
            copying_ = helper_.name();
        bool here = helper_.cmd() != HelperCmd::Refresh && helper_.name() == pp_.item().name;
        Screen top = flow_.nav.top();
        if ((top == Screen::Port || top == Screen::SteamLogin) && here) {
            pp_.progress(p, host.now());
            host.redraw();
        }
        if (l2 && here && helper_.cmd() == HelperCmd::SteamGet)
            login(p.what == "login", host.now());
    };
    helper_.on_start = [this](HelperCmd c, const std::string& name) {
        if (c == HelperCmd::Remove && name == pp_.item().name && flow_.nav.top() == Screen::Port) {
            pp_.set_working(true, c, false, host.now());
            host.redraw();
        }
    };
    helper_.on_qr = [this](const std::string& url) {
        Screen top = flow_.nav.top();
        if ((top != Screen::Port && top != Screen::SteamLogin) || helper_.name() != pp_.item().name)
            return;
        QrGrid q;
        if (!url.empty() && qr_encode(url, q) && qr_module_px(q, kStoreShotSide) >= 2)
            pp_.set_qr(std::move(q));
        else
            pp_.clear_qr();
        host.redraw();
    };
}

std::vector<HomeGame> StoreFlow::games(int sys) const
{
    std::vector<HomeGame> list;
    if (sys >= 0 && sys < (int)rows.size())
        for (int i : rows[(size_t)sys].items)
            list.push_back(store_game(items[(size_t)i]));
    return list;
}

const StoreItem* StoreFlow::item(int64_t id) const
{
    for (const StoreItem& it : items)
        if (it.id == id)
            return &it;
    return nullptr;
}

SelectorModel StoreFlow::model() const
{
    SelectorModel sm;
    for (const StoreGenre& g : rows)
        sm.systems.push_back({g.word, (int)g.items.size(), count_words((int)g.items.size())});
    return sm;
}

std::vector<HomeGame> StoreFlow::preview(int k) const
{
    std::vector<HomeGame> list;
    if (k >= 0 && k < (int)rows.size())
        for (int i : rows[(size_t)k].items) {
            if ((int)list.size() >= kStorePreviewTiles)
                break;
            list.push_back(store_game(items[(size_t)i]));
        }
    return list;
}

void StoreFlow::deal()
{
    int k = flow_.nav.focus;
    flow_.model.section_label = k >= 0 && k < (int)rows.size() ? rows[(size_t)k].word : std::string();
    std::vector<HomeGame> want = flow_.home.deal_recent(flow_.model, preview(k));
    host.preview_art(flow_.home.recent, want);
    flow_.redraw();
}

void StoreFlow::set_catalog(bool ok, Catalog c, int64_t now)
{
    int64_t gen = catalog.gen + 1;
    have_catalog = ok;
    catalog = ok ? std::move(c) : Catalog{};
    catalog.gen = gen;
    updated_.clear();
    rebuild(now);
}

void StoreFlow::rebuild(int64_t now)
{
    items = store_items(have_catalog ? &catalog : nullptr, host.index());
    SteamState steam = host.steam ? host.steam() : SteamState{};
    for (StoreItem& it : items) {
        if (updated_.count(it.name))
            it.update = false;
        auto s = steam.ports.find(it.name);
        it.steam = it.installed && s != steam.ports.end() && !s->second;
    }
    std::vector<StoreGenre> was = rows;
    rows = store_genres(items, have_catalog);
    Nav& nav = flow_.nav;
    if (std::find(nav.stack.begin(), nav.stack.end(), Screen::Port) != nav.stack.end())
        for (const StoreItem& it : items)
            if (it.name == pp_.item().name) {
                bool art = it.shot != pp_.item().shot || it.cover != pp_.item().cover;
                pp_.set_item(it, host.online(), now);
                if (art)
                    shot(&it);
            }
    if (!nav.store)
        return;
    int old = nav.focus;
    std::string word = old >= 0 && old < (int)was.size() ? was[(size_t)old].word : std::string();
    int focus = -1;
    for (int i = 0; i < (int)rows.size(); i++)
        if (rows[(size_t)i].word == word)
            focus = i;
    if (focus < 0 && nav.system_view) {
        if (nav.stack.empty())
            flow_.regame(old, {});
        else
            leave_due_ = true;
    }
    flow_.reset_menu(model().systems, focus >= 0 ? focus : std::clamp(old - 1, 0, (int)rows.size() - 1));
    grid_due_ = true;
    settle(now);
}

void StoreFlow::online_changed(int64_t now)
{
    const Nav& nav = flow_.nav;
    for (Screen s : nav.stack)
        if (s == Screen::Port) {
            rebuild(now);
            return;
        }
}

int64_t StoreFlow::deadline() const
{
    const Nav& nav = flow_.nav;
    int64_t d = preview_at_;
    bool resting = !nav.system_view && preview_at_ >= 0 && !leave_due_;
    bool due = grid_due_ || leave_due_ || (rest_due_ && !nav.system_view);
    if (due && nav.store && nav.stack.empty() && !flow_.ctl.active() && !resting) {
        int64_t t = host.now();
        d = d < 0 ? t : std::min(d, t);
    }
    int64_t e = login_due_ ? pp_.overlays_end() : -1;
    if (e >= 0)
        d = d < 0 ? e : std::min(d, e);
    return d;
}

void StoreFlow::settle(int64_t now)
{
    Nav& nav = flow_.nav;
    if (leave_due_ && nav.store && nav.stack.empty() && !flow_.ctl.active()) {
        leave_due_ = false;
        if (nav.system_view)
            flow_.regame(nav.focus, {});
        host.redraw();
        return;
    }
    if (rest_due_ && !nav.system_view) {
        rest_due_ = false;
        grid_due_ = true;
    }
    if (!grid_due_ || !nav.store || !nav.stack.empty() || flow_.ctl.active())
        return;
    if (!nav.system_view && preview_at_ >= 0)
        return;
    grid_due_ = false;
    if (nav.system_view) {
        std::vector<HomeGame> list = games(nav.focus);
        bool same = list.size() == flow_.home.sysgames.size();
        for (size_t i = 0; same && i < list.size(); i++)
            same = list[i].id == flow_.home.sysgames[i].id;
        if (list.empty())
            flow_.regame(nav.focus, list);
        else if (!same)
            flow_.swap_tiles(now, [this, list]() { flow_.regame(flow_.nav.focus, list); });
        host.redraw();
        return;
    }
    std::vector<HomeGame> next = preview(nav.focus);
    std::string word = nav.focus >= 0 && nav.focus < (int)rows.size() ? rows[(size_t)nav.focus].word : std::string();
    bool same = next.size() == flow_.home.recent.size() && word == flow_.model.section_label;
    for (size_t i = 0; same && i < next.size(); i++)
        same = next[i].id == flow_.home.recent[i].id;
    if (!same)
        flow_.swap_tiles(now, [this]() { deal(); });
}

void StoreFlow::shot(const StoreItem* it)
{
    shot_due_ = false;
    host.shot(it);
}

void StoreFlow::tick(int64_t now)
{
    if (shot_due_)
        shot(nullptr);
    if (login_due_ && pp_.overlays_idle(now)) {
        login_due_ = false;
        pp_.set_login_wait(false);
        if (login_ready())
            login_push(now);
    }
    if (preview_at_ >= 0 && now >= preview_at_) {
        preview_at_ = -1;
        if (flow_.nav.system_view)
            rest_due_ = true;
        else
            grid_due_ = true;
    }
    settle(now);
}

bool StoreFlow::entry(int sys, int64_t now)
{
    if (!host.is_ports(sys))
        return false;
    rebuild(now);
    if (host.stale() && host.online() && !helper_.running())
        helper_.ask(kids_, HelperCmd::Refresh, std::string());
    flow_.enter_store(model());
    deal();
    return true;
}

void StoreFlow::open_port(const StoreItem& it, int64_t now)
{
    HelperCmd c = HelperCmd::Install;
    bool working = helper_.holds(it.name, c);
    pp_.open(it, host.online(), working);
    if (working)
        pp_.set_working(true, c, !(helper_.running() && helper_.name() == it.name), now);
    if (working && c == HelperCmd::SteamGet && copying_ == it.name)
        pp_.set_copying();
    flow_.nav.stack.push_back(Screen::Port);
    flow_.push(Screen::Port);
    shot(&it);
}

void StoreFlow::ask(HelperCmd c, int64_t now)
{
    pp_.set_working(true, c, helper_.running(), now);
    helper_.ask(kids_, c, pp_.item().name);
}

bool StoreFlow::login_ready() const
{
    return l2 && helper_.running() && helper_.cmd() == HelperCmd::SteamGet &&
           helper_.name() == pp_.item().name && !helper_.stopping() && flow_.nav.top() == Screen::Port;
}

void StoreFlow::login(bool on, int64_t now)
{
    if (!on) {
        login_due_ = false;
        pp_.set_login_wait(false);
        login_pop(now);
        return;
    }
    if (!login_ready())
        return;
    if (pp_.overlays_idle(now)) {
        login_due_ = false;
        pp_.set_login_wait(false);
        login_push(now);
        return;
    }
    login_due_ = true;
    pp_.set_login_wait(true);
}

void StoreFlow::login_push(int64_t now)
{
    flow_.redraw();
    flow_.page_change(now, [this]() {
        flow_.nav.stack.push_back(Screen::SteamLogin);
        flow_.push(Screen::SteamLogin);
    });
    host.redraw();
}

void StoreFlow::login_pop(int64_t now)
{
    if (flow_.nav.top() != Screen::SteamLogin)
        return;
    pp_.clear_qr();
    pp_.drop_login_dialog();
    flow_.redraw();
    flow_.page_change(now, [this]() {
        flow_.nav.stack.pop_back();
        flow_.pop(Screen::SteamLogin);
    });
    host.redraw();
}

void StoreFlow::toast(const StoreToast& t, int64_t now)
{
    if (!t.show)
        return;
    if (flow_.nav.top() == Screen::Port)
        pp_.toast(now, t.line1, t.line2);
    else
        host.toast(t.line1, t.line2);
}

void StoreFlow::result(const HelperResult& r)
{
    int64_t now = host.now();
    if (host.result)
        host.result(r);
    if (r.cmd == HelperCmd::SteamGet && r.name == copying_)
        copying_.clear();
    login_due_ = false;
    pp_.set_login_wait(false);
    if (r.cmd == HelperCmd::SteamGet && r.name == pp_.item().name)
        login_pop(now);
    if (r.cmd == HelperCmd::SteamLogout) {
        rebuild(now);
        host.redraw();
        return;
    }
    if (r.cmd == HelperCmd::Refresh) {
        if (r.done)
            host.reread();
        else if (r.why != "busy")
            toast(store_toast(r), now);
        host.redraw();
        return;
    }
    if (r.why == "cancel" && helper_.running()) {
        if (r.name == pp_.item().name) {
            pp_.finished(false, true, now);
            pp_.set_item(pp_.item(), host.online(), now);
        }
        host.redraw();
        return;
    }
    host.rescan();
    if (r.done && r.cmd == HelperCmd::Update)
        updated_.insert(r.name);
    if (r.name == pp_.item().name)
        pp_.finished(r.done, r.done || r.why == "cancel", now);
    rebuild(now);
    toast(store_toast(r), now);
    host.redraw();
}

bool StoreFlow::page_input(Screen s, PadAction a, bool repeat, int64_t now)
{
    if (s == Screen::SteamLogin) {
        if (pp_.login_action(a, repeat, now) != PortPage::Act::Stop)
            return false;
        login_due_ = false;
        pp_.clear_qr();
        pp_.drop_login_dialog();
        flow_.redraw();
        helper_.cancel(pp_.cmd(), pp_.item().name, now);
        return true;
    }
    switch (pp_.action(a, repeat, now)) {
    case PortPage::Act::Pop:
        shot_due_ = true;
        return true;
    case PortPage::Act::Install: ask(HelperCmd::Install, now); break;
    case PortPage::Act::Update: ask(HelperCmd::Update, now); break;
    case PortPage::Act::Remove: ask(HelperCmd::Remove, now); break;
    case PortPage::Act::SteamGet: ask(HelperCmd::SteamGet, now); break;
    case PortPage::Act::Stop:
        login_due_ = false;
        pp_.set_login_wait(false);
        pp_.clear_qr();
        helper_.cancel(pp_.cmd(), pp_.item().name, now);
        break;
    case PortPage::Act::Play: {
        const StoreItem& it = pp_.item();
        if (it.game_id >= 0)
            flow_.launch_page(HomeGame{it.game_id, it.title, "", ""});
        break;
    }
    case PortPage::Act::None: break;
    }
    return false;
}

int StoreFlow::seed_y(Screen s) const
{
    if (s == Screen::Port)
        return pp_.focus_y();
    const Nav& nav = flow_.nav;
    if (s != Screen::Home || !nav.system_view)
        return -1;
    bool large = nav.cols == kBigCols;
    int pitch = large ? kBigPitch : kGridRowPitch, side = large ? kBigTileSide : kTileSide;
    return kTileFirstTop + (nav.sys_focus / std::max(1, nav.cols) - nav.sys_scroll) * pitch + side / 2;
}

}
