#include "size.h"

namespace fd {

namespace {

constexpr Role sized(Role r, fx size)
{
    r.size = size;
    return r;
}

constexpr Role mixed(Role r)
{
    r.tcase = Case::Mixed;
    return r;
}

constexpr SizeTable kStandard{tok::rowHeight,
                              tok::rowTextInset,
                              tok::rowRing,
                              tok::songIndent,
                              tok::settingsPitch,
                              tok::settingsLabelBaselineInset,
                              tok::btStateDrop,
                              tok::btRowDrop,
                              tok::settingsTitlePitch,
                              tok::toastHeight,
                              tok::toastArtistBaseline,
                              tok::toastTitleBaseline,
                              tok::motion_toastTravel,
                              role::listPrimary,
                              role::listPlaylist,
                              role::listSecondary,
                              role::listCount,
                              role::settingsItem,
                              role::settingsValue,
                              role::settingsSub,
                              role::dialogItem,
                              role::toastArtist,
                              role::toastTitle,
                              role::npTitle,
                              role::npNext,
                              mixed(role::npAlbum),
                              tok::npLyricPitch,
                              tok::npNextPitch,
                              tok::npAlbumBaseline - tok::npTextTop};

constexpr SizeTable lyrics_large(SizeTable t)
{
    t.npLyric = sized(role::npTitle, tok::large_type_npTitle_size);
    t.npLyricNext = sized(role::npNext, tok::large_type_npNext_size);
    t.npPlainAlbum = sized(mixed(role::npAlbum), tok::large_type_npAlbum_size);
    t.npLyricPitch = tok::large_npLyricPitch;
    t.npNextPitch = tok::large_npNextPitch;
    t.npAlbumPitch = tok::large_npAlbumPitch;
    return t;
}

constexpr SizeTable kLarge = lyrics_large(kStandard);

constexpr fx kLargePpu = (fx)(((int64_t)tok::u * tok::size_largeScale + fx_int(100) - 1) / fx_int(100));

const SizeTable* g_size = &kStandard;
thread_local fx tl_page = tok::u;
fx g_bottom = tok::canvasH, g_bottom_ppu = tok::u;
thread_local int tl_kept = 0;
#ifndef FD_ALSA
thread_local std::vector<ScopeNote>* tl_notes = nullptr;
thread_local int tl_leaks = 0;
#endif

}

const SizeTable& sz() { return *g_size; }

bool size_large() { return g_size == &kLarge; }

void set_size(bool large)
{
    g_size = large ? &kLarge : &kStandard;
    tl_page = large ? kLargePpu : tok::u;
    tl_ppu = tl_page;
}

fx large_ppu() { return kLargePpu; }

fx page_ppu() { return tl_page; }

fx canvas_w() { return tl_ppu == tok::u ? tok::canvasW : fx_div(fx_int(kPortraitW), tl_ppu); }

fx canvas_h() { return tl_ppu == tok::u ? tok::canvasH : fx_div(fx_int(kPortraitH), tl_ppu); }

fx u_from(fx v, fx at)
{
    if (at == tl_ppu)
        return v;
    int p = fx_round(fx_mul(v, at));
    return p == kPortraitH ? canvas_h() : fx_div(fx_int(p), tl_ppu);
}

fx content_bottom() { return u_from(g_bottom, g_bottom_ppu); }

void set_content_bottom(fx bottom)
{
    g_bottom = bottom;
    g_bottom_ppu = tl_ppu;
}

Scope::Scope(Fit f, const char* site) : prev_(tl_ppu), kept_(f == Fit::Kept)
{
#ifndef FD_ALSA
    if (f == Fit::Page && tl_kept && tl_page != tok::u)
        tl_leaks++;
#endif
    tl_ppu = f == Fit::Page ? tl_page : tok::u;
    tl_kept += kept_;
#ifndef FD_ALSA
    if (tl_notes)
        tl_notes->push_back({site, f, tl_ppu});
#else
    (void)site;
#endif
}

Scope::~Scope()
{
    tl_ppu = prev_;
    tl_kept -= kept_;
}

#ifndef FD_ALSA
void scope_notes(std::vector<ScopeNote>* out) { tl_notes = out; }

int scope_leaks() { return tl_leaks; }
#endif

}
