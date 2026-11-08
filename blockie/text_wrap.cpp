#include "text_wrap.h"

namespace fd {

std::vector<std::string> wrap_lines(const std::string& text, int avail, int max_lines,
                                    const std::function<int(const std::string&)>& measure)
{
    std::vector<std::string> lines;
    if (max_lines <= 0)
        return lines;
    std::string cur;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && text[i] == ' ')
            i++;
        size_t k = i;
        while (k < text.size() && text[k] != ' ')
            k++;
        if (k == i)
            break;
        std::string word = text.substr(i, k - i);
        i = k;
        if (cur.empty())
            cur = word;
        else if (measure(cur + " " + word) > avail) {
            lines.push_back(cur);
            cur = word;
        } else
            cur += " " + word;
    }
    if (!cur.empty())
        lines.push_back(cur);
    if ((int)lines.size() > max_lines) {
        lines.resize(max_lines);
        std::string& last = lines.back();
        std::string cand = last + "...";
        while (!last.empty() && measure(cand) > avail) {
            last.pop_back();
            cand = last + "...";
        }
        last = cand;
    }
    return lines;
}

}
