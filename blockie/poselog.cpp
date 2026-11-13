#include "engine.h"

#include "dumpz.h"

namespace fd {
namespace eng {

bool PoseLog::open(const std::string& path, const std::string& dump_dir, const std::string& prefix)
{
    f_ = fopen(path.c_str(), "w");
    dump_ = dump_dir;
    prefix_ = prefix;
    return f_ != nullptr;
}

void PoseLog::surface(int sid, const char* kind, const char* lang, Rect r)
{
    if (f_)
        fprintf(f_, "surface %d %s %s %d %d %d %d\n", sid, kind, lang, r.x0, r.y0, r.x1, r.y1);
}

void PoseLog::row(int sid, const char* name, Rect r, Ink ink)
{
    if (f_)
        fprintf(f_, "row %d %s %d %d %d %d %s\n", sid, name, r.x0, r.y0, r.x1, r.y1, ink == Ink::Art ? "art" : "text");
}

void PoseLog::fold(int64_t t0, const char* form, const char* kind, bool reversed, int64_t d3, const Frame& f,
                   bool collapse, bool redeal, int sid, const std::vector<std::string>* names)
{
    if (!f_)
        return;
    fprintf(f_, "fold %lld %s %s %d %d %d %lld %zu", (long long)t0, form, kind, reversed ? 1 : 0, collapse ? 1 : 0,
            redeal ? 1 : 0, (long long)d3, f.blocks.size());
    if (sid >= 0)
        fprintf(f_, " %d %s", sid, form);
    fprintf(f_, "\n");
    for (size_t i = 0; i < f.blocks.size(); i++) {
        const Block& b = f.blocks[i];
        const Shade& h = i < f.shades.size() ? f.shades[i] : Shade{};
        fprintf(f_, "block %zu %d 1 %zu 0 0 %d %d %d %d 1 0 %d %d %d %d %s %d %d", i, (int)b.id, i, b.src.x0,
                b.src.y0, b.src.x1, b.src.y1, (int)b.id, h.sigma, h.dep ? 1 : 0, b.z,
                b.ink == Ink::Art ? "art" : "text", b.tile ? 1 : 0, b.source);
        if (names && i < names->size() && !(*names)[i].empty())
            fprintf(f_, " %s", (*names)[i].c_str());
        fprintf(f_, "\n");
    }
}

bool PoseLog::frame(int n, int png, int64_t tf, int64_t ft, int rest, const Frame& f, const Instance& in, int boundary,
                    int sid)
{
    if (!f_)
        return true;
    Rect r = in.rect();
    fprintf(f_, "frame %d %d %lld %lld %lld %d %d %zu 0 1 %d %d", n, png, (long long)tf, (long long)ft,
            (long long)(3 * ft), boundary >= 0 ? boundary : r.x1, rest, f.poses.size(), in.settled() ? 1 : 0,
            in.last() == tf ? 1 : 0);
    if (sid >= 0)
        fprintf(f_, " %d", sid);
    fprintf(f_, "\n");
    for (const Pose& q : f.poses)
        fprintf(f_, "pose %d %d %d %d %d %d %d %d %d %d 0 0 0 1 0 %d %d %d %d 256 %d 0 0 0 0 0\n", n, q.block,
                q.rect.x0, q.rect.y0, q.rect.x1, q.rect.y1, q.clip.x0, q.clip.y0, q.clip.x1, q.clip.y1, q.rect.x0,
                q.rect.y0, q.rect.x1, q.rect.y1, (int)kOne);
    if (dump_.empty())
        return true;
    char name[32];
    std::string base = dump_ + "/" + prefix_;
    snprintf(name, sizeof name, "e-%05d.z", n);
    bool ok = dump_z(base + name, in.e().data(), in.e().size() * sizeof(uint16_t));
    snprintf(name, sizeof name, "id-%05d.z", n);
    ok = ok && dump_z(base + name, in.ids().data(), in.ids().size() * sizeof(uint16_t));
    snprintf(name, sizeof name, "g-%05d.z", n);
    ok = ok && dump_z(base + name, in.age().data(), in.age().size());
    if (in.cover_on()) {
        snprintf(name, sizeof name, "c-%05d.z", n);
        ok = ok && dump_z(base + name, in.cover().data(), in.cover().size());
    }
    if (ok && prefix_.empty())
        fprintf(f_, "dump %d %d %d %d %d 16\n", n, r.x0, r.y0, r.x1, r.y1);
    else if (ok)
        fprintf(f_, "dump %d %d %d %d %d 16 %s\n", n, r.x0, r.y0, r.x1, r.y1, prefix_.c_str());
    return ok;
}

bool PoseLog::close()
{
    if (!f_)
        return true;
    bool ok = fclose(f_) == 0;
    f_ = nullptr;
    return ok;
}

}
}
