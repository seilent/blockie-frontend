#pragma once
#include <vector>

#include "../gfx/canvas.h"
#include "roles.h"

namespace fd {

struct SizeTable {
    fx rowHeight, rowTextInset, rowRing, songIndent;
    fx settingsPitch, settingsLabelBaselineInset, btStateDrop, btRowDrop, settingsTitlePitch;
    fx toastHeight, toastArtistBaseline, toastTitleBaseline, toastTravel;
    Role listPrimary, listPlaylist, listSecondary, listCount;
    Role settingsItem, settingsValue, settingsSub, dialogItem, toastArtist, toastTitle;
    Role npLyric, npLyricNext, npPlainAlbum;
    fx npLyricPitch, npNextPitch, npAlbumPitch;
};

const SizeTable& sz();
bool size_large();
void set_size(bool large);
fx large_ppu();
fx page_ppu();
fx canvas_w();
fx canvas_h();
fx u_from(fx v, fx at);
fx content_bottom();
void set_content_bottom(fx bottom);
Role kept_text(const Role& r);

enum class Fit : uint8_t { Std, Page, Kept };

class Scope {
public:
    Scope(Fit f, const char* site);
    ~Scope();
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    fx prev_;
    bool kept_;
};

#ifndef FD_ALSA
struct ScopeNote {
    const char* site;
    Fit fit;
    fx ppu;
};
void scope_notes(std::vector<ScopeNote>* out);
int scope_leaks();
#endif

}
