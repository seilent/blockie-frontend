#include "theme_setting.h"

#include <string>

namespace fd {

ThemeSetting::ThemeSetting()
{
    key_.on_value = [this](const std::string& v) {
        if ((v == "light" || v == "dark") && on_value)
            on_value(v == "light");
    };
    key_.on_fail = [this]() {
        if (on_fail)
            on_fail();
    };
}

}
