#include "qr.h"

#include <algorithm>

#include "qrcodegen.h"

namespace fd {

bool qr_encode(const std::string& text, QrGrid& out)
{
    out = QrGrid();
    std::vector<uint8_t> tmp(qrcodegen_BUFFER_LEN_MAX), code(qrcodegen_BUFFER_LEN_MAX);
    if (text.empty() || text.size() > tmp.size())
        return false;
    std::copy(text.begin(), text.end(), tmp.begin());
    if (!qrcodegen_encodeBinary(tmp.data(), text.size(), code.data(), qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN,
                                qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, false))
        return false;
    out.size = qrcodegen_getSize(code.data());
    out.version = (out.size - 17) / 4;
    out.dark.resize((size_t)(out.size * out.size));
    for (int y = 0; y < out.size; y++)
        for (int x = 0; x < out.size; x++)
            out.dark[(size_t)(y * out.size + x)] = qrcodegen_getModule(code.data(), x, y) ? 1 : 0;
    return true;
}

int qr_module_px(const QrGrid& q, int box)
{
    int n = q.size + 2 * kQrQuiet;
    return q.size > 0 && box > 0 ? box / n : 0;
}

Rect qr_draw(Canvas& c, const QrGrid& q, Rect box, uint32_t ink, uint32_t paper)
{
    int side = std::min(box.x1 - box.x0, box.y1 - box.y0), m = qr_module_px(q, side);
    if (m <= 0)
        return {};
    int full = m * (q.size + 2 * kQrQuiet);
    int x0 = box.x0 + (box.x1 - box.x0 - full) / 2, y0 = box.y0 + (box.y1 - box.y0 - full) / 2;
    Rect all{x0, y0, x0 + full, y0 + full};
    c.fill(all, paper);
    int ox = x0 + kQrQuiet * m, oy = y0 + kQrQuiet * m;
    for (int y = 0; y < q.size; y++)
        for (int x = 0; x < q.size;) {
            if (!q.at(x, y)) {
                x++;
                continue;
            }
            int run = x;
            while (run < q.size && q.at(run, y))
                run++;
            c.fill({ox + x * m, oy + y * m, ox + run * m, oy + (y + 1) * m}, ink);
            x = run;
        }
    return all;
}

}
