#pragma once
#include <vector>

#include "../leftmove.h"

namespace fd {

inline bool rect_eq(const Rect& a, const Rect& b)
{
    return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1;
}

inline bool glow_all(const Glow& a, const Glow& b)
{
    auto rects = [](const std::vector<Rect>& x, const std::vector<Rect>& y) {
        if (x.size() != y.size())
            return false;
        for (size_t i = 0; i < x.size(); i++)
            if (!rect_eq(x[i], y[i]))
                return false;
        return true;
    };
    auto poses = [](const std::vector<BlockPose>& x, const std::vector<BlockPose>& y) {
        if (x.size() != y.size())
            return false;
        for (size_t i = 0; i < x.size(); i++) {
            const BlockPose &p = x[i], &q = y[i];
            if (p.block != q.block || !rect_eq(p.rect, q.rect) || !rect_eq(p.clip, q.clip) || p.top != q.top ||
                !rect_eq(p.ring, q.ring) || p.a256 != q.a256 || p.beneath != q.beneath)
                return false;
        }
        return true;
    };
    auto shades = [](const std::vector<GlowShade>& x, const std::vector<GlowShade>& y) {
        if (x.size() != y.size())
            return false;
        for (size_t i = 0; i < x.size(); i++)
            if (x[i].t != y[i].t || x[i].end != y[i].end || x[i].step != y[i].step || x[i].sigma != y[i].sigma ||
                x[i].dep != y[i].dep || x[i].lut != y[i].lut)
                return false;
        return true;
    };
    bool hits = true;
    for (int k = 0; k < 7; k++)
        hits = hits && a.seg_hits[k] == b.seg_hits[k];
    return a.e == b.e && a.age == b.age && a.ids == b.ids && a.cur == b.cur && a.spare == b.spare && a.lo == b.lo &&
           a.hi == b.hi && a.act_lo == b.act_lo && a.act_hi == b.act_hi && a.back == b.back && a.low == b.low &&
           a.low_now == b.low_now && a.back_t == b.back_t && shades(a.shades, b.shades) && poses(a.poses, b.poses) &&
           rects(a.covers, b.covers) && a.spans == b.spans && a.tmp == b.tmp &&
           a.sig_now == b.sig_now && a.hid == b.hid && a.hid_now == b.hid_now &&
           rects(a.ring_hid, b.ring_hid) && a.cover_rows == b.cover_rows && a.t0 == b.t0 &&
           a.last == b.last && a.due == b.due && a.due_t0 == b.due_t0 && a.flip_s3 == b.flip_s3 &&
           a.inited == b.inited && a.ready == b.ready && a.flip == b.flip && a.settled == b.settled &&
           a.copied == b.copied && a.seg == b.seg && a.verify == b.verify && a.dry == b.dry &&
           a.verify_scans == b.verify_scans && a.back_scans == b.back_scans && a.runs_scans == b.runs_scans &&
           a.verify_fails == b.verify_fails && a.seg_frames == b.seg_frames &&
           a.aging_frames == b.aging_frames && hits &&
           a.seed_frames == b.seed_frames && a.runs_ok == b.runs_ok && a.runs_built == b.runs_built &&
           a.run_pieces == b.run_pieces && a.runs.runs.size() == b.runs.runs.size() && a.runs.at == b.runs.at &&
           a.runs_next.runs.size() == b.runs_next.runs.size() && a.cover_row.size() == b.cover_row.size() &&
           a.ring_row.size() == b.ring_row.size() && a.bounds.size() == b.bounds.size() &&
           a.extra.size() == b.extra.size();
}

inline bool glow_eq(const Glow& a, const Glow& b)
{
    return a.e == b.e && a.age == b.age && a.ids == b.ids && a.hid == b.hid && a.act_lo == b.act_lo &&
           a.act_hi == b.act_hi && a.settled == b.settled && a.inited == b.inited;
}

}
