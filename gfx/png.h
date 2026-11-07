#pragma once
#include <string>

#include "canvas.h"

namespace fd {

template <class F = LiveF, class L = LiveL> bool write_png(const std::string& path, const SurfaceT<F, L>& s);

}
