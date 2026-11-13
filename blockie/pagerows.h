#pragma once
#include <string>
#include <vector>

#include "pageform.h"
#include "screen_bt.h"
#include "screen_settings.h"

namespace fd {

inline std::vector<PageRow> settings_page_rows(const SettingsView& v)
{
    std::vector<Rect> rs = settings_regions((int)v.rows.size());
    std::vector<PageRow> out;
    for (int i = 0; i < (int)rs.size(); i++) {
        std::string name = i == 0 ? "header" : i == 1 ? "pivots" : "row" + std::to_string(i - 2);
        out.push_back({"settings-" + name, rs[(size_t)i], eng::Ink::Text, i - 2 == v.focus});
    }
    return out;
}

inline PageHead head_rows(const char* page, const char* word, const std::vector<Rect>& letters,
                          const std::vector<Rect>& tabs, bool layer)
{
    PageHead h;
    h.word = word;
    h.layer = layer;
    for (size_t k = 0; k < letters.size(); k++)
        h.letters.push_back({std::string(page) + "-header-l" + std::to_string(k), letters[k], eng::Ink::Text});
    for (size_t j = 0; j < tabs.size(); j++)
        h.tabs.push_back({std::string(page) + "-pivots-t" + std::to_string(j), tabs[j], eng::Ink::Text});
    return h;
}

inline PageHead settings_page_head(Fonts& f, const SettingsView& v)
{
    return head_rows("settings", settings_word(), page_letter_cuts(f, settings_word()), settings_tab_cuts(f, v), true);
}

inline PageHead bluetooth_page_head(Fonts& f)
{
    return head_rows("bluetooth", bluetooth_word(), page_letter_cuts(f, bluetooth_word()), {}, false);
}

inline std::vector<PageRow> bluetooth_page_rows(const BtPage& b)
{
    std::vector<Rect> rs = b.regions();
    std::vector<PageRow> out;
    for (int i = 0; i < (int)rs.size(); i++)
        out.push_back({"bluetooth-" + (i == 0 ? std::string("header") : "row" + std::to_string(i - 1)), rs[(size_t)i],
                       eng::Ink::Text});
    return out;
}

}
