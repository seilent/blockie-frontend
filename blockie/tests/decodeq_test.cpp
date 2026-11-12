#include <cstdio>
#include <vector>

#include "../decodeq.h"

using namespace fd;

namespace {

int g_fail = 0;
int g_total = 0;

void check(bool ok, const char* what)
{
    g_total++;
    if (ok) {
        std::printf("PASS %s\n", what);
    } else {
        std::printf("FAIL %s\n", what);
        g_fail++;
    }
}

DecodeJob job(int64_t id, int64_t gen)
{
    DecodeJob j;
    j.game_id = id;
    j.path = "/synthetic/cover-" + std::to_string(id) + ".png";
    j.gen = gen;
    return j;
}

std::vector<int64_t> drain(DecodeQueue& q)
{
    std::vector<int64_t> ids;
    DecodeJob j;
    while (q.pop(j)) {
        ids.push_back(j.game_id);
        q.done(j.game_id);
    }
    return ids;
}

}

int main()
{
    {
        DecodeQueue q;
        q.set_window(1, {10, 11});
        check(q.push(job(10, 1)), "a new game id is queued");
        check(!q.push(job(10, 1)) && q.size() == 1, "a second job for a queued id is dropped");
        DecodeJob j;
        check(q.pop(j) && j.game_id == 10 && q.pending(10), "a popped job stays pending while it decodes");
        check(!q.push(job(10, 1)) && q.size() == 0, "a job for an id in flight is dropped");
        q.done(10);
        check(!q.pending(10) && q.push(job(10, 1)), "after the decode finishes the id can be queued again");
    }
    {
        DecodeQueue q;
        q.set_window(1, {20, 21});
        q.push(job(1, kRecentGen));
        q.push(job(20, 1));
        q.push(job(2, kRecentGen));
        q.set_window(2, {30});
        q.push(job(30, 2));
        std::vector<int64_t> got = drain(q);
        check(got.size() == 3 && got[0] == 30 && got[1] == 1 && got[2] == 2,
              "recent jobs survive a window change and the current window goes first");
        check(!q.pending(20), "a job of the old window is skipped");
    }
    {
        DecodeQueue q;
        q.set_window(3, {40, 41, 42});
        q.push(job(40, 3));
        q.push(job(41, 3));
        q.set_window(4, {41, 42});
        q.push(job(41, 4));
        q.push(job(42, 4));
        std::vector<int64_t> got = drain(q);
        check(got.size() == 2 && got[0] == 41 && got[1] == 42,
              "a window change skips the old generation and keeps a re-requested id");
    }
    {
        DecodeQueue q;
        q.set_window(5, {50});
        q.push(job(50, 5));
        q.push(job(50, kRecentGen));
        q.set_window(kRecentGen, {});
        std::vector<int64_t> got = drain(q);
        check(got.size() == 1 && got[0] == 50, "a system job requested again by recent becomes a recent job");
    }
    {
        DecodeQueue q;
        q.push(job(55, kRecentGen));
        DecodeJob again = job(55, 9);
        again.path = "/synthetic/cover-55-new.png";
        q.push(again);
        q.set_window(10, {});
        DecodeJob j;
        check(q.pop(j) && j.game_id == 55 && j.gen == kRecentGen,
              "a queued recent job pushed again for a window stays recent and survives the window change");
        check(j.path == "/synthetic/cover-55-new.png", "a deduped push replaces the queued path");
    }
    {
        DecodeQueue q;
        q.set_window(6, {60, 61});
        q.set_recent({70});
        DecodeJob j;
        q.push(job(60, 6));
        q.pop(j);
        q.set_window(7, {61, 60});
        check(q.accept(60), "a result from an older generation is taken while its id is in the keep set");
        check(q.accept(70), "a recent result is taken in any window");
        q.set_window(8, {80});
        check(!q.accept(60) && !q.accept(61), "a result whose id left the keep set is dropped");
        q.set_recent({});
        check(!q.accept(70), "a recent result is dropped once the game leaves recent");
    }
    {
        DecodeQueue q;
        check(!q.pending(1), "nothing is pending in an empty queue");
        DecodeJob j;
        check(!q.pop(j), "an empty queue pops nothing");
        q.set_window(9, {90});
        q.push(job(90, 9));
        q.set_window(10, {});
        check(!q.pop(j) && q.size() == 0, "a queue of stale jobs pops nothing and empties");
    }

    {
        DecodeQueue q;
        DecodeJob a = job(100, kRecentGen);
        q.push(a);
        check(q.current(100, a.path, 0), "a pushed cover is the current result for its id");
        DecodeJob j;
        q.pop(j);
        DecodeJob b = a;
        b.path = "/synthetic/blockie-test-cover-100-new.jpg";
        check(q.push(b) && q.size() == 1, "a new cover path for an id in flight is queued again");
        check(!q.current(100, a.path, 0) && q.current(100, b.path, 0),
              "a result for the old cover path is not current once the path changed");
        DecodeJob c = b;
        c.index_gen = 2;
        q.push(c);
        check(!q.current(100, b.path, 0) && q.current(100, b.path, 2),
              "a result of an older index generation is not current");
    }
    {
        DecodeQueue q;
        DecodeJob a = job(120, kRecentGen);
        DecodeJob j;
        q.push(a);
        q.pop(j);
        a.path = "/synthetic/blockie-test-cover-120-new.jpg";
        q.push(a);
        q.pop(j);
        check(q.busy() == 1, "two jobs in flight for one id count as one busy id");
        q.done(120);
        check(q.pending(120), "an id stays pending while its second job still decodes");
        q.done(120);
        check(!q.pending(120) && q.busy() == 0, "the id is free once both jobs are done");
    }
    {
        DecodeQueue q;
        q.push(job(110, kRecentGen));
        q.push(job(111, kRecentGen));
        q.forget({110});
        std::vector<int64_t> got = drain(q);
        check(got.size() == 1 && got[0] == 111, "forget drops the queued jobs of removed ids");
        check(!q.current(110, job(110, kRecentGen).path, 0), "a removed id has no current result");
    }

    std::printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
