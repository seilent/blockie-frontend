#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace fd {

template <class P> struct RowRuns {
    struct Run {
        int16_t x0;
        int16_t x1;
        P p;
    };

    std::vector<Run> runs;
    std::vector<int> at;
    int row0 = 0;

    void begin(int rows)
    {
        runs.clear();
        at.assign((size_t)std::max(0, rows) + 1, 0);
        row0 = 0;
    }
    void row(int r) { at[(size_t)r] = row0 = (int)runs.size(); }
    void end(int rows) { at[(size_t)std::max(0, rows)] = (int)runs.size(); }
    void emit(int x0, int x1, const P& p)
    {
        if (x0 >= x1)
            return;
        if ((int)runs.size() > row0) {
            Run* last = runs.data() + runs.size() - 1;
            if (last->x1 == x0 && last->p == p) {
                last->x1 = (int16_t)x1;
                return;
            }
        }
        runs.push_back({(int16_t)x0, (int16_t)x1, p});
    }
    const Run* first(int r) const { return runs.data() + at[(size_t)r]; }
    const Run* last(int r) const { return runs.data() + at[(size_t)r + 1]; }
    template <class F> void walk(int r, int u, int v, int shift, F f) const
    {
        const Run* e = last(r);
        const Run* g = std::upper_bound(first(r), e, u - shift, [](int x, const Run& s) { return x < s.x1; });
        for (; g < e && g->x0 + shift < v; g++)
            f(std::max(u, g->x0 + shift), std::min(v, g->x1 + shift), *g);
    }
    void clear()
    {
        runs.clear();
        at.clear();
        row0 = 0;
    }
    void swap(RowRuns& o)
    {
        runs.swap(o.runs);
        at.swap(o.at);
        std::swap(row0, o.row0);
    }
};

template <class C, class F> void cut_at(int u, int v, const C& cuts, F f)
{
    for (int m = u; m < v;) {
        int n = v;
        for (int c : cuts)
            if (c > m && c < n)
                n = c;
        f(m, n);
        m = n;
    }
}

}
