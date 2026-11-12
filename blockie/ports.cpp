#include "ports.h"

namespace fd {

void PortsHelper::line(const std::string& l)
{
    size_t t = l.find('\t');
    if (t == std::string::npos || l.compare(0, t, "done") != 0 || l.find('\t', t + 1) != std::string::npos)
        return;
    if (on_done)
        on_done(l.substr(t + 1));
}

}
