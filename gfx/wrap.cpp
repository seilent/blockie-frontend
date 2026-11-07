#include "wrap.h"

#include <algorithm>

#include "../lib/util.h"

namespace fd {

std::vector<std::pair<size_t, size_t>> wrap_ranges(std::string_view s, int width64, size_t max_rows,
                                                   const std::function<int(size_t, size_t)>& width,
                                                   const std::function<size_t(size_t, size_t)>& step)
{
    std::vector<std::pair<size_t, size_t>> out;
    size_t lb = 0, le = 0, i = 0;
    bool line = false;
    while (i < s.size() && out.size() < max_rows) {
        size_t sp = s.find(' ', i);
        size_t we = sp == std::string_view::npos ? s.size() : sp;
        size_t next = sp == std::string_view::npos ? s.size() : sp + 1;
        size_t cb = line ? lb : i;
        if (width(cb, we) <= width64) {
            lb = cb;
            le = we;
            line = le > lb;
            i = next;
            continue;
        }
        if (line) {
            out.push_back({lb, le});
            line = false;
            if (out.size() >= max_rows)
                break;
        }
        size_t p = i, pb = i, pe = i;
        bool part = false;
        while (p < we) {
            size_t q = step(p, we);
            if (part && width(pb, q) > width64) {
                out.push_back({pb, pe});
                part = false;
                if (out.size() >= max_rows)
                    break;
                continue;
            }
            if (!part)
                pb = p;
            part = true;
            pe = q;
            p = q;
        }
        lb = pb;
        le = pe;
        line = part;
        i = next;
    }
    if (line && out.size() < max_rows)
        out.push_back({lb, le});
    return out;
}

std::vector<std::string> wrap_lines(Fonts& f, const TextStyle& st, const std::string& s, int width64, size_t max_lines)
{
    std::vector<std::string> out;
    auto width = [&](size_t b, size_t e) { return f.width64(st, s.substr(b, e - b).c_str()); };
    auto step = [&](size_t p, size_t end) {
        const char* q = s.data() + p;
        utf8_next(q, s.data() + end);
        return (size_t)(q - s.data());
    };
    for (const std::pair<size_t, size_t>& r : wrap_ranges(s, width64, max_lines, width, step))
        out.push_back(s.substr(r.first, r.second - r.first));
    return out;
}

std::vector<GlyphRun> wrap_runs(Fonts& f, const TextStyle& st, const std::string& s, Dir d, int width64, size_t max_rows)
{
    std::vector<GlyphRun> out;
    if (bidi_plain(s, d)) {
        for (std::string& row : wrap_lines(f, st, s, width64, max_rows)) {
            GlyphRun r;
            r.st = st;
            r.is_plain = true;
            r.width64 = f.width64(st, row.c_str());
            r.plain = std::move(row);
            out.push_back(std::move(r));
        }
        return out;
    }
    for (std::string_view piece : bidi_pieces(s)) {
        if (out.size() >= max_rows)
            break;
        std::shared_ptr<const TextLayout> l = f.layout(st, piece, d);
        auto at = [&](size_t byte) {
            return (size_t)(std::lower_bound(l->g.begin(), l->g.end(), byte,
                                             [](const TextLayout::G& g, size_t b) { return g.byte < b; }) -
                            l->g.begin());
        };
        TextPrefix pre;
        f.prefix64(st, *l, pre);
        auto width = [&](size_t b, size_t e) {
            int w = 0;
            return pre.span64(*l, b, e, w) ? w : f.span64(st, *l, b, e);
        };
        auto step = [&](size_t p, size_t end) {
            size_t i = at(p + 1);
            return std::max(p + 1, std::min(end, i < l->g.size() ? (size_t)l->g[i].byte : end));
        };
        for (const std::pair<size_t, size_t>& r : wrap_ranges(piece, width64, max_rows - out.size(), width, step))
            out.push_back(f.run(st, *l, r.first, r.second));
    }
    return out;
}

}
