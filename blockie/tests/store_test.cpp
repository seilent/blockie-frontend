#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <ftw.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../ring_spin.h"
#include "../store.h"
#include "sqlite3.h"

namespace {

int g_fail = 0;
int g_total = 0;

void check(bool ok, const char* what)
{
    g_total++;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        g_fail++;
}

int rm_cb(const char* p, const struct stat*, int, struct FTW*) { return remove(p); }
void rm_rf(const std::string& p) { nftw(p.c_str(), rm_cb, 16, FTW_DEPTH | FTW_PHYS); }

void write_file(const std::string& path, const std::string& body, mode_t mode)
{
    if (FILE* f = std::fopen(path.c_str(), "wb")) {
        std::fwrite(body.data(), 1, body.size(), f);
        std::fclose(f);
        chmod(path.c_str(), mode);
    }
}

void read_lines_into(const std::string& path, std::vector<std::string>& out)
{
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        char b[256];
        while (std::fgets(b, sizeof b, f)) {
            std::string l = b;
            while (!l.empty() && l.back() == '\n')
                l.pop_back();
            out.push_back(l);
        }
        std::fclose(f);
    }
}

int64_t now_ms()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

struct PortRow {
    std::string name, title, genres, porter, desc, inst;
    int64_t size = 0;
    int installed = 0, update = 0;
    std::string shot, cover;
};

void make_db(const std::string& path, int version, const std::vector<PortRow>& rows)
{
    std::string tmp = path + ".tmp";
    unlink(tmp.c_str());
    sqlite3* db = nullptr;
    sqlite3_open(tmp.c_str(), &db);
    sqlite3_exec(db,
                 "CREATE TABLE ports (name TEXT PRIMARY KEY, title TEXT, genres TEXT, porter TEXT, description TEXT,"
                 " inst TEXT, size INTEGER, availability TEXT, runtimes TEXT, installed INTEGER,"
                 " update_available INTEGER, screenshot TEXT, cover TEXT);",
                 nullptr, nullptr, nullptr);
    sqlite3_exec(db, ("PRAGMA user_version=" + std::to_string(version) + ";").c_str(), nullptr, nullptr, nullptr);
    sqlite3_exec(db, "BEGIN;", nullptr, nullptr, nullptr);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db, "INSERT INTO ports VALUES (?,?,?,?,?,?,?,'full','',?,?,?,?);", -1, &st, nullptr);
    for (const PortRow& r : rows) {
        sqlite3_reset(st);
        const std::string* t[] = {&r.name, &r.title, &r.genres, &r.porter, &r.desc, &r.inst};
        for (int i = 0; i < 6; i++)
            sqlite3_bind_text(st, i + 1, t[i]->c_str(), (int)t[i]->size(), SQLITE_TRANSIENT);
        sqlite3_bind_int64(st, 7, r.size);
        sqlite3_bind_int(st, 8, r.installed);
        sqlite3_bind_int(st, 9, r.update);
        sqlite3_bind_text(st, 10, r.shot.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 11, r.cover.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(st);
    }
    sqlite3_finalize(st);
    sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
    sqlite3_close(db);
    rename(tmp.c_str(), path.c_str());
}

std::string fd_list()
{
    std::vector<int> v;
    DIR* d = opendir("/proc/self/fd");
    if (!d)
        return "none";
    while (dirent* e = readdir(d)) {
        if (e->d_name[0] == '.')
            continue;
        int n = std::atoi(e->d_name);
        if (n != dirfd(d))
            v.push_back(n);
    }
    closedir(d);
    std::sort(v.begin(), v.end());
    std::string out;
    for (int n : v)
        out += (out.empty() ? "" : " ") + std::to_string(n);
    return out;
}

struct Run {
    fd::Children kids;
    fd::PortsHelper h;
    std::vector<fd::HelperResult> results;
    std::vector<std::string> done;

    explicit Run(const std::string& path)
    {
        h.path = path;
        h.on_result = [this](const fd::HelperResult& r) {
            results.push_back(r);
            if (r.done)
                done.push_back(r.name);
        };
    }

    bool settle(int64_t limit_ms)
    {
        int64_t until = now_ms() + limit_ms;
        while (h.running() && now_ms() < until) {
            h.tick(now_ms());
            int st = 0;
            pid_t r = waitpid(-1, &st, WNOHANG);
            if (r > 0)
                kids.reaped(r, st);
            else
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return !h.running();
    }
};

}

int main()
{
    if (std::getenv("BLOCKIE_TEST_FDS")) {
        std::string list = fd_list();
        std::printf(list == "0 1 2" ? "done\tblockie-test-fds\n" : "fail\tblockie-test-fds\tother\n");
        return 0;
    }
    char tmpl[] = "/tmp/blockie-store-test.XXXXXX";
    char* root_c = mkdtemp(tmpl);
    if (!root_c)
        return 1;
    std::string root = root_c;
    std::string ports = root + "/ports";
    std::string images = ports + "/PortMaster/config/images_blockie-test";
    mkdir(ports.c_str(), 0755);
    mkdir((ports + "/PortMaster").c_str(), 0755);
    mkdir((ports + "/PortMaster/config").c_str(), 0755);
    mkdir(images.c_str(), 0755);
    mkdir((ports + "/.blockie").c_str(), 0755);
    std::string shot = images + "/blockie-test-shot.png";
    write_file(shot, "\x89PNG", 0644);
    write_file(root + "/blockie-test-outside.png", "\x89PNG", 0644);
    symlink((root + "/blockie-test-outside.png").c_str(), (images + "/blockie-test-link.png").c_str());
    mkdir((images + "/blockie-test-dir.png").c_str(), 0755);
    write_file(ports + "/PortMaster/config/blockie-test-shot.png", "\x89PNG", 0644);
    std::string db = ports + fd::kCatalogDb;

    {
        check(fd::confined_image(shot, ports) == shot, "an image inside the images dir is used");
        check(fd::confined_image(images + "/../../../../blockie-test-outside.png", ports).empty(),
              "a path with .. out of the images dir gives no art");
        check(fd::confined_image(root + "/blockie-test-outside.png", ports).empty(),
              "an absolute path elsewhere gives no art");
        check(fd::confined_image(images + "/blockie-test-link.png", ports).empty(), "a symlink out gives no art");
        check(fd::confined_image(images + "/blockie-test-dir.png", ports).empty(), "a directory gives no art");
        std::string sib = root + "/blockie-test-" + std::string(48, 'x');
        mkdir(sib.c_str(), 0755);
        write_file(sib + "/blockie-test-shot.png", "\x89PNG", 0644);
        check(sib.size() > images.size() - std::string("blockie-test").size() &&
                  fd::confined_image(sib + "/blockie-test-shot.png", ports).empty(),
              "a regular file in a sibling tree as deep as the images dir gives no art");
        std::string bare = ports + "/PortMaster/config/images_";
        mkdir(bare.c_str(), 0755);
        write_file(bare + "/blockie-test-shot.png", "\x89PNG", 0644);
        check(fd::confined_image(bare + "/blockie-test-shot.png", ports).empty(),
              "a file in an images dir with no source name gives no art");
        check(fd::confined_image(ports + "/PortMaster/config/blockie-test-shot.png", ports).empty(),
              "a file beside the images dirs gives no art");
    }

    {
        std::string a = "blockie-test-\xc3\xa9";
        check(fd::cap_text(a, a.size() - 1) == "blockie-test-", "a cut lands on the last whole code point");
        check(fd::cap_text("blockie\x01test\ttitle", 64) == "blockie test title", "control characters become spaces");
        check(fd::cap_body("blockie\ntest\x02", 64) == "blockie\ntest ", "a body keeps its line breaks");
        std::vector<std::string> g = fd::cap_list("action\tpuzzle", 64);
        check(g.size() == 2 && g[0] == "action" && g[1] == "puzzle", "genres split on tabs");
    }

    {
        PortRow big;
        big.name = std::string(300, 'n');
        big.title = std::string(255, 't') + "\xe6\x97\xa5";
        big.porter = std::string(400, 'p');
        big.genres = "action";
        big.desc = std::string(70000, 'd');
        big.inst = std::string(70000, 'i');
        big.shot = shot;
        big.cover = images + "/blockie-test-link.png";
        PortRow b2;
        b2.name = "blockie-test-two.zip";
        b2.title = "blockie test two";
        b2.genres = "puzzle\tother";
        b2.porter = "blockie-test-a\tblockie-test-b";
        b2.size = 5000;
        make_db(db, fd::kCatalogVersion, {big, b2});
        fd::Catalog c;
        bool ok = fd::read_catalog(db, ports, c);
        check(ok && c.ports.size() == 2, "a catalogue reads every row");
        const fd::StorePort* p = ok && c.ports.size() == 2 ? &c.ports[0] : nullptr;
        check(p && p->name.size() == fd::kNameCap && p->title.size() == 255 && p->porter.size() == fd::kTextCap &&
                  p->description.size() == fd::kBodyCap && p->inst.size() == fd::kBodyCap,
              "every string past its cap is cut");
        check(p && p->screenshot == shot && p->cover.empty(), "catalogue image paths are confined");
        check(ok && c.ports[1].porter == "blockie-test-a, blockie-test-b" && c.ports[1].genres.size() == 2,
              "porters join with a comma, genres stay a list");
    }

    {
        make_db(db, fd::kCatalogVersion + 1, {});
        fd::Catalog c;
        check(!fd::read_catalog(db, ports, c), "another user_version is no catalogue");
        write_file(db, std::string(4096, 'x'), 0644);
        check(!fd::read_catalog(db, ports, c), "a file that is not a database is no catalogue");
        std::vector<PortRow> rows;
        for (int i = 0; i < 400; i++)
            rows.push_back({"blockie-test-" + std::to_string(i) + ".zip", "blockie test", "action", "", "", ""});
        make_db(db, fd::kCatalogVersion, rows);
        int f = open(db.c_str(), O_RDWR | O_CLOEXEC);
        std::string junk(4096, '\xff');
        ssize_t w = f >= 0 ? pwrite(f, junk.data(), junk.size(), 4096) : -1;
        if (f >= 0)
            close(f);
        check(w == 4096 && !fd::read_catalog(db, ports, c), "a corrupt catalogue is no catalogue");
        unlink(db.c_str());
        check(!fd::read_catalog(db, ports, c), "a missing catalogue is no catalogue");
    }

    {
        auto rows_of = [](const char* tag) {
            std::vector<PortRow> v;
            for (int i = 0; i < 3000; i++)
                v.push_back({"blockie-test-" + std::to_string(i) + ".zip", std::string("blockie-test-") + tag,
                             "action", "", std::string(200, 'd'), ""});
            return v;
        };
        make_db(db, fd::kCatalogVersion, rows_of("old"));
        std::vector<PortRow> fresh = rows_of("new");
        std::atomic<bool> stop{false};
        std::atomic<int> mixed{0}, reads{0};
        std::thread reader([&]() {
            while (!stop.load()) {
                fd::Catalog c;
                if (!fd::read_catalog(db, ports, c))
                    continue;
                reads++;
                for (const fd::StorePort& p : c.ports)
                    if (p.title != c.ports[0].title)
                        mixed++;
            }
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        make_db(db, fd::kCatalogVersion, fresh);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        stop.store(true);
        reader.join();
        fd::Catalog c;
        bool ok = fd::read_catalog(db, ports, c);
        check(reads.load() > 0 && mixed.load() == 0, "a catalogue replaced mid read leaves each read whole");
        check(ok && !c.ports.empty() && c.ports[0].title == "blockie-test-new", "the next read gives the new one");
    }

    {
        fd::Catalog c;
        auto port = [](const char* name, const char* title, std::vector<std::string> g, bool upd) {
            fd::StorePort p;
            p.name = name;
            p.title = title;
            p.genres = std::move(g);
            p.update = upd;
            return p;
        };
        c.ports.push_back(port("blockie-test-a.zip", "blockie test a", {"puzzle"}, true));
        c.ports.push_back(port("blockie-test-b.zip", "blockie test b", {"action", "rpg"}, false));
        c.ports.push_back(port("blockie-test-c.zip", "blockie test c", {"other"}, true));
        std::vector<fd::IndexPort> idx = {{7, "blockie-test-a", "blockie test a", "blockie-test-a", ""},
                                          {8, "blockie-test-hand", "blockie test hand", "blockie-test-hand", ""}};
        std::vector<fd::StoreItem> items = fd::store_items(&c, idx);
        std::vector<fd::StoreGenre> g = fd::store_genres(items, true);
        std::vector<std::string> words;
        for (const fd::StoreGenre& x : g)
            words.push_back(x.word + ":" + std::to_string(x.items.size()));
        std::vector<std::string> want = {"all:3",   "installed:2", "updates:1", "action:1",
                                         "puzzle:1", "rpg:1",       "other:1"};
        check(words == want, "genres: all, installed, updates, then harbourmaster order with ports");
        int b_inst = 0;
        for (const fd::StoreItem& it : items)
            if (it.name == "blockie-test-b.zip" && it.installed)
                b_inst++;
        check(b_inst == 0, "installed follows the index, not the catalogue flag");
        c.ports[0].update = false;
        std::vector<fd::StoreGenre> g2 = fd::store_genres(fd::store_items(&c, idx), true);
        bool upd = false;
        for (const fd::StoreGenre& x : g2)
            if (x.word == fd::kStoreUpdates)
                upd = true;
        check(!upd, "updates shows only while a port has one");
        std::vector<fd::StoreGenre> none = fd::store_genres(fd::store_items(nullptr, {}), false);
        check(none.size() == 1 && none[0].word == fd::kStoreInstalled && none[0].items.empty(),
              "no catalogue and nothing installed leaves installed at 0 alone");
        int64_t id = -1;
        for (const fd::StoreItem& it : fd::store_items(&c, idx))
            if (it.name == "blockie-test-c.zip")
                id = it.id;
        c.ports.push_back(port("blockie-test-0.zip", "blockie test 0", {"other"}, false));
        bool same = false;
        for (const fd::StoreItem& it : fd::store_items(&c, idx))
            if (it.name == "blockie-test-c.zip")
                same = it.id == id;
        check(same, "a port keeps its id when the catalogue changes");
        std::vector<fd::IndexPort> full = {
            {9, "blockie-test-a", "blockie test a", "/blockie-test/roms/ports/blockie-test-a", ""},
            {10, "blockie-test-hand", "blockie test hand", "/blockie-test/roms/ports/blockie-test-hand", ""},
            {11, "blockie-test-hand-b", "blockie test hand b", "/blockie-test/roms/ports/blockie-test-hand", ""}};
        int a_inst = 0, extra = 0;
        std::string hand;
        for (const fd::StoreItem& it : fd::store_items(&c, full)) {
            if (it.name == "blockie-test-a.zip" && it.installed && it.game_id == 9)
                a_inst++;
            if (it.port < 0) {
                extra++;
                hand = it.name;
            }
        }
        check(a_inst == 1, "an index dir written as a full path matches its catalogue port");
        check(extra == 1 && hand == "blockie-test-hand", "a port only in the index is named by its dir");
    }

    {
        check(fd::size_words(0).empty() && fd::size_words(-1).empty(), "an unknown size draws no words");
        auto w64 = [](const std::string& t) { return (int)t.size() * 64; };
        std::vector<std::string> bl = fd::body_lines("aaa bbb ccc\n\ndddddddd \xe6\x97\xa5\xe6\x97\xa5", 7 * 64, w64);
        check(bl.size() == 6 && bl[0] == "aaa bbb" && bl[1] == "ccc" && bl[2].empty() && bl[3] == "ddddddd" &&
                  bl[4] == "d \xe6\x97\xa5" && bl[5] == "\xe6\x97\xa5",
              "body lines wrap at spaces, cut long words, keep empty lines and split CJK per code point");
        check(fd::size_words(4900) == "5 kb" && fd::size_words(8700000) == "9 mb" &&
                  fd::size_words(909115392) == "867 mb" && fd::size_words(1610612736) == "1.5 gb",
              "size words");
        check(fd::count_words(1) == "1 port" && fd::count_words(0) == "0 ports", "count words");
        check(fd::progress_words({"download", 1048576, 2097152}) == "1 mb of 2 mb" &&
                  fd::progress_words({"runtime", 1024, 2048}) == "runtime 1 kb of 2 kb" &&
                  fd::progress_words({"download", 5, -1}) == "download" &&
                  fd::progress_words({"download", 0, 2097152}) == "0 kb of 2 mb" &&
                  fd::progress_words({"wait", 1, 600}) == "waiting" &&
                  fd::progress_words({"unpack", 1, 2}) == "unpacking" &&
                  fd::progress_words({"remove", 0, -1}) == "removing",
              "progress words");
        check(fd::progress_arc({"download", 50, 100}) == 180 && fd::progress_arc({"download", 100, 100}) == 356 &&
                  fd::progress_arc({"download", 1, -1}) == 0,
              "the arc holds at 99 percent until done");
        check(fd::ring_arc(0, -1, 7).sweep == 0 && fd::ring_arc(1, 2, 7).start == 7,
              "with no total the store ring draws no arc");
        fd::StoreToast md = fd::store_toast({fd::HelperCmd::Update, "x", false, "md5"});
        fd::StoreToast rt = fd::store_toast({fd::HelperCmd::Install, "x", false, "runtime"});
        fd::StoreToast nd = fd::store_toast({fd::HelperCmd::Refresh, "x", false, "nodata"});
        check(md.line1 == "update failed" && md.line2 == "download damaged" && rt.line2 == "runtime download failed" &&
                  nd.line1 == "refresh failed" && nd.line2 == "no card",
              "update, md5, runtime and nodata toast words");
        fd::StoreToast t = fd::store_toast({fd::HelperCmd::Install, "x", false, "space"});
        check(t.show && t.line1 == "install failed" && t.line2 == "card full", "a full card toast");
        check(!fd::store_toast({fd::HelperCmd::Install, "x", false, "cancel"}).show, "a cancel shows no toast");
        fd::StoreToast o = fd::store_toast({fd::HelperCmd::Remove, "x", false, "other"});
        check(o.show && o.line1 == "remove failed" && o.line2.empty(), "other is a one line toast");
    }

    std::string bin = root + "/bin";
    mkdir(bin.c_str(), 0755);
    {
        std::string fast = bin + "/blockie-test-fast";
        write_file(fast, "#!/bin/sh\nprintf 'done\\t%s\\n' \"$2\"\n", 0755);
        Run r(fast);
        int ok = 0;
        for (int i = 0; i < 200; i++) {
            r.results.clear();
            r.h.ask(r.kids, fd::HelperCmd::Install, "blockie-test-port.zip");
            if (r.settle(5000) && r.results.size() == 1 && r.results[0].done &&
                r.results[0].name == "blockie-test-port.zip")
                ok++;
        }
        check(ok == 200, "200 helpers that write done and exit at once all read as done");
        check(r.done.size() == 200, "every run reports its own result once");
    }

    {
        std::string quiet = bin + "/blockie-test-quiet";
        write_file(quiet, "#!/bin/sh\nexit 0\n", 0755);
        Run r(quiet);
        r.h.ask(r.kids, fd::HelperCmd::Install, "blockie-test-port.zip");
        r.settle(5000);
        check(r.results.size() == 1 && !r.results[0].done && r.results[0].why == "other" && r.done.empty(),
              "a helper with no result line is fail other");
        std::string last = bin + "/blockie-test-last";
        write_file(last,
                   "#!/bin/sh\nprintf 'progress\\tdownload\\tx\\t2\\nprogress\\tdownload\\t1\\t2\\n"
                   "fail\\t%s\\tnetwork\\n"
                   "done\\tblockie-test-other\\n"
                   "done\\t%s\\n' \"$2\" \"$2\"\nexit 1\n",
                   0755);
        Run l(last);
        std::vector<fd::HelperProgress> prog;
        l.h.on_progress = [&](const fd::HelperProgress& p) { prog.push_back(p); };
        l.h.ask(l.kids, fd::HelperCmd::Update, "blockie-test-port.zip");
        l.settle(5000);
        check(l.results.size() == 1 && l.results[0].done && prog.size() == 1 && prog[0].total == 2,
              "the last done or fail for the command wins, whatever the exit status");
        std::string other = bin + "/blockie-test-other";
        write_file(other, "#!/bin/sh\nprintf 'fail\\t%s\\tnetwork\\ndone\\trefresh\\n' \"$2\"\nexit 0\n", 0755);
        Run o(other);
        o.h.ask(o.kids, fd::HelperCmd::Install, "blockie-test-port.zip");
        o.settle(5000);
        check(o.results.size() == 1 && !o.results[0].done && o.results[0].why == "network" && o.done.empty(),
              "a done line for another command is ignored");
        std::string empty_why = bin + "/blockie-test-nowhy";
        write_file(empty_why, "#!/bin/sh\nprintf 'fail\\t%s\\t\\n' \"$2\"\nexit 1\n", 0755);
        Run e(empty_why);
        e.h.ask(e.kids, fd::HelperCmd::Install, "blockie-test-port.zip");
        e.settle(5000);
        check(e.results.size() == 1 && e.results[0].why == "other", "a fail line with no reason is ignored");
        std::string busy = bin + "/blockie-test-busy";
        write_file(busy, "#!/bin/sh\nprintf 'fail\\t%s\\tbusy\\n' \"$2\"\nexit 1\n", 0755);
        Run b(busy);
        b.h.ask(b.kids, fd::HelperCmd::Install, "blockie-test-port.zip");
        b.settle(5000);
        check(b.results.size() == 1 && b.results[0].why == "busy" &&
                  fd::store_toast(b.results[0]).line2 == "a game is running",
              "a held lock gives busy and its toast");
    }

    {
        std::string log = root + "/blockie-test-runs";
        std::string slowref = bin + "/blockie-test-slowref";
        write_file(slowref, "#!/bin/sh\necho \"$1\" >> \"" + log + "\"\nsleep 0.2\nprintf 'done\\trefresh\\n'\n", 0755);
        Run q(slowref);
        q.h.ask(q.kids, fd::HelperCmd::Refresh, "");
        q.h.ask(q.kids, fd::HelperCmd::Install, "blockie-test-x.zip");
        bool dropped = q.h.cancel(fd::HelperCmd::Install, "blockie-test-x.zip", now_ms());
        fd::HelperCmd qc = fd::HelperCmd::Refresh;
        bool after = q.results.size() == 1 && q.results[0].why == "cancel" && q.h.running() &&
                     !q.h.holds("blockie-test-x.zip", qc);
        q.settle(5000);
        std::vector<std::string> runs;
        read_lines_into(log, runs);
        check(dropped && after && q.results.size() == 2 && q.results[1].cmd == fd::HelperCmd::Refresh &&
                  q.results[1].done && runs.size() == 1 && runs[0] == "refresh",
              "cancel drops a queued install, the running refresh finishes and nothing else runs");
        Run w(slowref);
        w.h.ask(w.kids, fd::HelperCmd::Refresh, "");
        bool kept = !w.h.cancel(fd::HelperCmd::Install, "blockie-test-x.zip", now_ms());
        w.settle(5000);
        check(kept && w.results.size() == 1 && w.results[0].cmd == fd::HelperCmd::Refresh && w.results[0].done,
              "cancel for an op that is not running leaves the running refresh to finish");
        std::string slowins = bin + "/blockie-test-slowins";
        write_file(slowins, "#!/bin/sh\nsleep 0.2\nprintf 'done\\t%s\\n' \"$2\"\n", 0755);
        Run v(slowins);
        v.h.ask(v.kids, fd::HelperCmd::Install, "blockie-test-y.zip");
        fd::HelperCmd vc = fd::HelperCmd::Refresh;
        bool held = v.h.holds("blockie-test-y.zip", vc) && vc == fd::HelperCmd::Install;
        bool other = !v.h.holds("blockie-test-z.zip", vc);
        v.settle(5000);
        check(held && other && v.results.size() == 1 && v.results[0].done,
              "a port page opened during its running install sees the install");
        std::string rm = bin + "/blockie-test-rm";
        write_file(rm, "#!/bin/sh\n[ \"$1\" = uninstall ] && printf 'done\\t%s\\n' \"$2\"\nexit 0\n", 0755);
        Run u(rm);
        u.h.ask(u.kids, fd::HelperCmd::Remove, "blockie-test-port.zip");
        u.settle(5000);
        check(u.results.size() == 1 && u.results[0].done && u.results[0].cmd == fd::HelperCmd::Remove,
              "remove runs the helper's uninstall with the port name");
        std::string slow = bin + "/blockie-test-slow";
        write_file(slow, "#!/bin/sh\nsleep 0.2\nprintf 'done\\t%s\\n' \"$2\"\n", 0755);
        Run r(slow);
        bool first = r.h.ask(r.kids, fd::HelperCmd::Install, "blockie-test-a.zip");
        bool second = r.h.ask(r.kids, fd::HelperCmd::Install, "blockie-test-b.zip");
        bool third = r.h.ask(r.kids, fd::HelperCmd::Install, "blockie-test-c.zip");
        fd::HelperCmd bc = fd::HelperCmd::Refresh;
        bool queued =
            r.h.holds("blockie-test-c.zip", bc) && bc == fd::HelperCmd::Install && r.h.op == fd::HelperOp::Install;
        r.settle(5000);
        r.settle(5000);
        check(first && !second && !third && queued, "a second ask waits while one helper runs");
        check(r.results.size() == 3 && r.results[0].cmd == fd::HelperCmd::Install &&
                  r.results[0].name == "blockie-test-b.zip" && !r.results[0].done && r.results[0].why == "cancel" &&
                  r.results[1].name == "blockie-test-a.zip" && r.results[1].done &&
                  r.results[2].name == "blockie-test-c.zip" && r.results[2].done && r.h.op == fd::HelperOp::None,
              "helpers run in turn, a newer ask replaces the pending one");
    }

    {
        std::string stall = bin + "/blockie-test-stall";
        write_file(stall, "#!/bin/sh\ntrap '' TERM\nexec sleep 60\n", 0755);
        Run r(stall);
        r.h.stop_ms = 500;
        r.h.ask(r.kids, fd::HelperCmd::Install, "blockie-test-port.zip");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        int64_t t0 = now_ms();
        r.h.cancel(fd::HelperCmd::Install, "blockie-test-port.zip", t0);
        bool waits = r.results.empty() && r.h.running();
        bool ended = r.settle(15000);
        int64_t took = now_ms() - t0;
        check(waits, "stop leaves the row until the reap");
        check(fd::kHelperStopMs == 10000 && ended && took <= r.h.stop_ms + 1000 && r.results.size() == 1 &&
                  !r.results[0].done && r.results[0].why == "cancel",
              "a helper that ignores SIGTERM is killed at its deadline and reads as a cancel");
    }

    {
        int leak = open("/dev/null", O_RDONLY);
        setenv("BLOCKIE_TEST_FDS", "1", 1);
        Run r("/proc/self/exe");
        r.h.ask(r.kids, fd::HelperCmd::Install, "blockie-test-fds");
        r.settle(5000);
        unsetenv("BLOCKIE_TEST_FDS");
        if (leak >= 0)
            close(leak);
        check(leak >= 3 && r.results.size() == 1 && r.results[0].done, "a helper child holds fds 0 to 2 only");
    }

    {
        std::string st = ports + fd::kSteamTsv;
        fd::SteamState none = fd::read_steam(st);
        check(!none.signed_in && none.ports.empty(), "sf1: a missing steam.tsv is no Steam rows");
        write_file(st,
                   "signed\t1\nport\tblockie-test-a.zip\tget\nport\tblockie-test-b.zip\tgot\n"
                   "account\tblockie-test-x\naccount\tblockie-test-y.zip\tget\n"
                   "port\tblockie-test-c.zip\tmaybe\nport\t\tget\n"
                   "port\tblockie-test-d.zip\tget\textra\nport\t" + std::string(fd::kNameCap + 1, 'n') + "\tget\n"
                   "port\tblockie-test-e.zip\tget",
                   0644);
        fd::SteamState s = fd::read_steam(st);
        check(s.signed_in && s.ports.size() == 3 && !s.ports.at("blockie-test-a.zip") &&
                  s.ports.at("blockie-test-b.zip") && s.ports.count("blockie-test-e.zip"),
              "sf1: steam.tsv gives signed and get or got per port, unknown keys and bad lines dropped");
        write_file(st, "port\tblockie-test-a.zip\tget\nport\tblockie-test-b.zip\tget\nsigned\t1\n", 0644);
        fd::SteamState late = fd::read_steam(st);
        check(!late.signed_in && late.ports.empty(), "sf1: a steam.tsv whose line 1 is not signed is no Steam rows");
        const std::string cut = "port\tblockie-test-g.zip\tget";
        std::string big = "signed\t0\n";
        while (big.size() + 36 + cut.size() < fd::kSteamTsvCap)
            big += "port\tblockie-test-f.zip\tget\n";
        big += "pad\t" + std::string(fd::kSteamTsvCap - big.size() - cut.size() - 5, 'x') + "\n";
        big += cut + "\nport\tblockie-test-h.zip\tget\n";
        write_file(st, big, 0644);
        fd::SteamState capped = fd::read_steam(st);
        check(big.find(cut) + cut.size() == fd::kSteamTsvCap && !capped.signed_in && capped.ports.size() == 1 &&
                  capped.ports.count("blockie-test-f.zip"),
              "sf1: steam.tsv reads to its cap and drops the line its cap cuts and the rest");
        unlink(st.c_str());
        const std::string body = "signed\t1\nport\tblockie-test-a.zip\tget\n";
        bool made = mkfifo(st.c_str(), 0600) == 0;
        fd::SteamState idle = fd::read_steam(st);
        int w = open(st.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        bool fed = w >= 0 && write(w, body.data(), body.size()) == (ssize_t)body.size();
        fd::SteamState fifo = fd::read_steam(st);
        if (w >= 0)
            close(w);
        unlink(st.c_str());
        std::string real = ports + "/blockie-test-steam.tsv";
        write_file(real, body, 0644);
        bool linked = symlink(real.c_str(), st.c_str()) == 0;
        fd::SteamState sym = fd::read_steam(st);
        unlink(real.c_str());
        check(made && fed && linked && !idle.signed_in && !fifo.signed_in && fifo.ports.empty() && !sym.signed_in &&
                  sym.ports.empty(),
              "sf1: a FIFO or a symlink at the steam.tsv path is no Steam rows and the read does not block");
        unlink(st.c_str());
    }

    {
        std::string st = ports + fd::kSteamTsv;
        auto steam_of = [&st](const std::string& body) {
            write_file(st, body, 0644);
            return fd::read_steam(st);
        };
        fd::SteamState old = steam_of("signed\t1\nport\tblockie-test-a.zip\tget\n");
        fd::SteamState zero = steam_of("signed\t1\nbound\t0\nport\tblockie-test-a.zip\tget\n");
        fd::SteamState ahead = steam_of("signed\t0\nbound\t1700000300\nport\tblockie-test-a.zip\tget\n");
        check(old.signed_in && old.bound_until == 0 && old.ports.size() == 1 && zero.signed_in &&
                  zero.bound_until == 0 && zero.ports.size() == 1 && !ahead.signed_in &&
                  ahead.bound_until == 1700000300 && ahead.ports.size() == 1 && !ahead.ports.at("blockie-test-a.zip"),
              "bfe: steam.tsv's bound line gives its end in wall seconds, 0 or no line is none, port lines still read");
        bool bad = steam_of("signed\t1\n").bound_until == 0 && steam_of("signed\t1\nbound\n").bound_until == 0;
        for (const char* v : {"", "-5", "12a", " 5", "5 ", "1\t2", "0x10", "1234567890123"})
            bad = bad && steam_of(std::string("signed\t1\nbound\t") + v + "\n").bound_until == 0;
        check(bad, "bfe: a bound line that is not 1 to 12 digits is no bound");
        fd::SteamState twice = steam_of("signed\t1\nbound\t100\nbound\t200\n");
        fd::SteamState first = steam_of("signed\t1\nbound\tx\nbound\t200\n");
        fd::SteamState wide = steam_of("signed\t1\nbound\t999999999999\n");
        check(twice.bound_until == 100 && first.bound_until == 0 && wide.bound_until == 999999999999,
              "bfe: the first bound line counts, up to 12 digits");
        unlink(st.c_str());
    }

    {
        check(fd::progress_words({"login", 0, -1}) == "signing in" &&
                  fd::progress_words({"login", 1, 300}) == "signing in 299" &&
                  fd::progress_words({"login", 299, 300}) == "signing in 1" &&
                  fd::progress_words({"check", 0, -1}) == "checking" &&
                  fd::progress_words({"copy", 3, 9}) == "copying" &&
                  fd::progress_words({"download", 0, 6012954214}) == "0 kb of 5.6 gb",
              "sf1: steam progress words from the helper's lines");
        check(fd::progress_arc({"login", 150, 300}) == 180 && fd::progress_arc({"login", 0, -1}) == 0 &&
                  fd::progress_arc({"copy", 3, 9}) == 0 && fd::progress_arc({"check", 0, -1}) == 0,
              "sf1: the sign in arc runs over 300 s, check and copy draw the no total form");
        struct Want {
            const char* why;
            const char* line2;
        };
        bool all = true;
        for (const Want& w : {Want{"network", "download failed"}, Want{"space", "card full"},
                              Want{"owned", "not in your library"}, Want{"expired", "sign in timed out"},
                              Want{"login", "sign in failed"}, Want{"exists", "files already there"},
                              Want{"busy", "a game is running"}, Want{"nodata", "no card"}, Want{"other", ""}}) {
            fd::StoreToast t = fd::store_toast({fd::HelperCmd::SteamGet, "blockie-test-port.zip", false, w.why});
            all = all && t.show && t.line1 == "get files failed" && t.line2 == w.line2;
        }
        check(all, "sf1: a failed get toasts get files failed with its reason");
        check(!fd::store_toast({fd::HelperCmd::SteamGet, "blockie-test-port.zip", false, "cancel"}).show &&
                  !fd::store_toast({fd::HelperCmd::SteamGet, "blockie-test-port.zip", true, ""}).show,
              "sf1: a stopped or done get shows no toast");
        fd::StoreToast so = fd::store_toast({fd::HelperCmd::SteamLogout, "", false, "other"});
        check(so.show && so.line1 == "sign out failed" && so.line2.empty(), "sf1: a failed sign out toasts one line");
        check(fd::helper_op(fd::HelperCmd::SteamGet) == fd::HelperOp::SteamGet &&
                  fd::helper_op(fd::HelperCmd::SteamLogout) == fd::HelperOp::None,
              "sf1: a get holds the launch refusal, a sign out does not");
    }

    {
        const std::string u38 = "https://s.team/q/1/1111111111111111111";
        const std::string u512 = "https://s.team/q/1/" + std::string(493, '3');
        check(u38.size() == 38 && u512.size() == 512 && fd::helper_qr_url(u38) && fd::helper_qr_url(u512) &&
                  !fd::helper_qr_url(u512 + "3") && !fd::helper_qr_url("http://s.team/q/1/1") &&
                  !fd::helper_qr_url("https://s.team/q 1") && !fd::helper_qr_url("https://s.team/q\x7f") &&
                  !fd::helper_qr_url(""),
              "sf1: a qr url is 1 to 512 bytes of 0x21 to 0x7E starting https://");
        fd::Children kids;
        fd::PortsHelper h;
        std::vector<std::string> argv;
        std::vector<std::string> codes;
        std::vector<fd::HelperResult> results;
        h.spawn = [&](const std::vector<std::string>& a, int* out) {
            argv = a;
            *out = -1;
            return (pid_t)1;
        };
        h.signal_child = [](pid_t, int) {};
        h.on_qr = [&](const std::string& u) { codes.push_back(u); };
        h.on_result = [&](const fd::HelperResult& r) { results.push_back(r); };
        h.ask(kids, fd::HelperCmd::Install, "blockie-test-port.zip");
        h.line("qr\t" + u38);
        h.finish();
        check(codes.empty(), "sf1: a qr line outside a get is ignored");
        h.ask(kids, fd::HelperCmd::SteamGet, "blockie-test-port.zip");
        bool args = argv.size() == 3 && argv[1] == "steam-get" && argv[2] == "blockie-test-port.zip";
        h.line("qr\t" + u38);
        h.line("qr\t" + u38 + "\tx");
        h.line("qr\thttps://s.team/q 1");
        h.line("qr\t" + u512 + "3");
        h.line("qr\thttp://s.team/q/1/1111111111111111111");
        check(args && codes.size() == 5 && codes[0] == u38 && codes[1].empty() && codes[2].empty() &&
                  codes[3].empty() && codes[4].empty(),
              "sf1: a good qr line shows its code, a third field, a space, 513 bytes or http clear it");
        h.stop(0);
        h.line("qr\t" + u38);
        check(codes.size() == 5, "sf1: a qr line after stop is dropped");
        h.finish();
        check(results.size() == 2 && results[1].cmd == fd::HelperCmd::SteamGet && results[1].why == "cancel",
              "sf1: a stopped get reads as a cancel");
        h.ask(kids, fd::HelperCmd::SteamLogout, "");
        bool bare = argv.size() == 2 && argv[1] == "steam-logout";
        h.line("fail\tsteam-logout\tother");
        h.line("done\t");
        h.finish();
        bool failed = results.size() == 3 && !results[2].done && results[2].why == "other";
        h.ask(kids, fd::HelperCmd::SteamLogout, "");
        h.line("done\tsteam-logout");
        h.finish();
        check(bare && failed && results.size() == 4 && results[3].cmd == fd::HelperCmd::SteamLogout &&
                  results[3].done,
              "sf1: steam-logout runs with no name and its done line is matched by its word");
    }

    {
        const std::string u38 = "https://s.team/q/1/1111111111111111111";
        int pp[2] = {-1, -1};
        bool piped = pipe2(pp, O_NONBLOCK | O_CLOEXEC) == 0;
        fd::Children kids;
        fd::PortsHelper h;
        std::vector<std::string> codes;
        std::vector<fd::HelperProgress> prog;
        std::vector<fd::HelperResult> results;
        h.spawn = [&](const std::vector<std::string>&, int* out) {
            *out = pp[0];
            return (pid_t)1;
        };
        h.signal_child = [](pid_t, int) {};
        h.on_qr = [&](const std::string& u) { codes.push_back(u); };
        h.on_progress = [&](const fd::HelperProgress& p) { prog.push_back(p); };
        h.on_result = [&](const fd::HelperResult& r) { results.push_back(r); };
        h.ask(kids, fd::HelperCmd::SteamGet, "blockie-test-port.zip");
        auto put = [&](const std::string& s) {
            bool ok = write(pp[1], s.data(), s.size()) == (ssize_t)s.size();
            h.readable();
            return ok;
        };
        const std::string longqr = "qr\thttps://s.team/q/1/" + std::string(4080, '4');
        const std::string wide = "progress\t" + std::string(4090, 'w') + "\t9\t300";
        bool wrote = piped && put(longqr + "\n") && put(longqr) &&
                     put("progress\tlogin\t9\t300\nprogress\tlogin\t1\t300\n") && put(wide + "\n") &&
                     put(wide.substr(0, 2048)) && put(wide.substr(2048) + "\n") &&
                     put("qr\t" + u38 + "\ndone\tblockie-test-port.zip\n");
        if (pp[1] >= 0)
            close(pp[1]);
        h.finish();
        check(wrote && codes.size() == 3 && codes[0].empty() && codes[1].empty() && codes[2] == u38 &&
                  prog.size() == 1 && prog[0].what == "login" && prog[0].done == 1 && results.size() == 1 &&
                  results[0].done,
              "sf1: a helper line over the 4096 cap is dropped to its newline however the reads split it, a qr line "
              "also clears the code");
    }

    {
        fd::Children kids;
        fd::PortsHelper h;
        std::vector<fd::HelperResult> results;
        std::vector<std::vector<std::string>> runs;
        h.spawn = [&](const std::vector<std::string>& a, int* out) {
            runs.push_back(a);
            *out = -1;
            return (pid_t)1;
        };
        h.signal_child = [](pid_t, int) {};
        h.on_result = [&](const fd::HelperResult& r) { results.push_back(r); };
        h.ask(kids, fd::HelperCmd::Refresh, "");
        h.ask(kids, fd::HelperCmd::SteamLogout, "");
        h.ask(kids, fd::HelperCmd::SteamLogout, "");
        bool kept = results.empty();
        h.ask(kids, fd::HelperCmd::Install, "blockie-test-port.zip");
        bool told = results.size() == 1 && results[0].cmd == fd::HelperCmd::SteamLogout && !results[0].done &&
                    results[0].why == "cancel";
        h.finish();
        check(kept && told && runs.size() == 2 && runs[1].size() == 3 && runs[1][1] == "install",
              "sf1: a queued ask replaced by another is answered as a cancel, the same ask again keeps it");
    }

    {
        int a[2] = {-1, -1}, b[2] = {-1, -1};
        bool piped = pipe2(a, O_NONBLOCK | O_CLOEXEC) == 0 && pipe2(b, O_NONBLOCK | O_CLOEXEC) == 0;
        int* next = a;
        fd::Children kids;
        fd::PortsHelper h;
        std::vector<fd::HelperResult> results;
        h.spawn = [&](const std::vector<std::string>&, int* out) {
            *out = next[0];
            next = b;
            return (pid_t)1;
        };
        h.signal_child = [](pid_t, int) {};
        h.on_result = [&](const fd::HelperResult& r) { results.push_back(r); };
        h.ask(kids, fd::HelperCmd::Install, "blockie-test-port.zip");
        const std::string big(5000, 'x'), done = "done\tblockie-test-port.zip\n";
        bool wrote = piped && write(a[1], big.data(), big.size()) == (ssize_t)big.size();
        h.readable();
        if (a[1] >= 0)
            close(a[1]);
        h.finish();
        h.ask(kids, fd::HelperCmd::Install, "blockie-test-port.zip");
        wrote = wrote && write(b[1], done.data(), done.size()) == (ssize_t)done.size();
        if (b[1] >= 0)
            close(b[1]);
        h.finish();
        check(wrote && results.size() == 2 && !results[0].done && results[1].done,
              "sf1: a new helper run reads its first line after the last run ended inside an over cap line");
    }

    rm_rf(root);
    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
