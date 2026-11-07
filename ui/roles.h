#pragma once
#include "../core/fx.h"
#include "../gfx/font.h"
#include "tokens.h"

namespace fd {

struct Role {
    fx size;
    Weight weight;
    Case tcase;
    fx alpha;
    fx tracking;

    TextStyle style() const { return {weight, text_px(size), fx_round(to_px(tracking) * 64), tcase}; }
    TextStyle std_style() const { return {weight, std_text_px(size), fx_round(std_to_px(tracking) * 64), tcase}; }
};

constexpr Weight weight_of(std::string_view s)
{
    if (s == "Light")
        return Weight::Light;
    if (s == "Bold")
        return Weight::Bold;
    return s == "SemiLight" ? Weight::SemiLight : Weight::Regular;
}
constexpr Case case_of(const char* s) { return s[0] == 'l' ? Case::Lower : s[0] == 'u' ? Case::Upper : Case::Mixed; }

#define FD_ROLE(n, track) \
    Role { tok::type_##n##_size, weight_of(tok::type_##n##_style), case_of(tok::type_##n##_case), tok::type_##n##_alpha, track }

namespace role {
inline constexpr Role hubHeader = FD_ROLE(hubHeader, tok::trackingHeader);
inline constexpr Role pageHeader = FD_ROLE(pageHeader, tok::trackingHeader);
inline constexpr Role albumHeader =
    FD_ROLE(albumHeader, (fx)((int64_t)tok::tracking * tok::type_albumHeader_size / tok::type_pivotActive_size));
inline constexpr Role homeItem = FD_ROLE(homeItem, tok::tracking);
inline constexpr Role homeSubtitle = FD_ROLE(homeSubtitle, tok::tracking);
inline constexpr Role pivotActive = FD_ROLE(pivotActive, tok::tracking);
inline constexpr Role pivotInactive = FD_ROLE(pivotInactive, tok::tracking);
inline constexpr Role watermark = FD_ROLE(watermark, tok::tracking);
inline constexpr Role quickplayHeader = FD_ROLE(quickplayHeader, tok::tracking);
inline constexpr Role quickplayHint = FD_ROLE(quickplayHint, tok::tracking);
inline constexpr Role sectionLabel = FD_ROLE(sectionLabel, tok::tracking);
inline constexpr Role settingsItem = FD_ROLE(settingsItem, tok::tracking);
inline constexpr Role pageRowTitle{tok::page_rowTitle, settingsItem.weight, settingsItem.tcase, settingsItem.alpha,
                                   settingsItem.tracking};
inline constexpr Role settingsValue = FD_ROLE(settingsValue, tok::tracking);
inline constexpr Role settingsSub = FD_ROLE(settingsSub, tok::tracking);
inline constexpr Role detailName = FD_ROLE(detailName, tok::tracking);
inline constexpr Role detailTitle = FD_ROLE(detailTitle, tok::tracking);
inline constexpr Role detailSub = FD_ROLE(detailSub, tok::tracking);
inline constexpr Role listPrimary = FD_ROLE(listPrimary, tok::tracking);
inline constexpr Role listSecondary = FD_ROLE(listSecondary, tok::tracking);
inline constexpr Role listCount = FD_ROLE(listCount, tok::tracking);
inline constexpr Role listPlaylist = FD_ROLE(listPlaylist, tok::tracking);
inline constexpr Role letterGlyph = FD_ROLE(letterGlyph, tok::tracking);
inline constexpr Role gridCaption = FD_ROLE(gridCaption, tok::tracking);
inline constexpr Role dialogTitle = FD_ROLE(dialogTitle, tok::tracking);
inline constexpr Role dialogItem = FD_ROLE(dialogItem, tok::tracking);
inline constexpr Role overlayLabel = FD_ROLE(overlayLabel, tok::tracking);
inline constexpr Role jumpExit = FD_ROLE(jumpExit, tok::tracking);
inline constexpr Role npArtist = FD_ROLE(npArtist, tok::tracking);
inline constexpr Role npAlbum = FD_ROLE(npAlbum, tok::tracking);
inline constexpr Role npTitle = FD_ROLE(npTitle, tok::tracking);
inline constexpr Role npNext = FD_ROLE(npNext, tok::tracking);
inline constexpr Role npCount = FD_ROLE(npCount, tok::tracking);
inline constexpr Role time = FD_ROLE(time, tok::tracking);
inline constexpr Role toastArtist = FD_ROLE(toastArtist, tok::tracking);
inline constexpr Role toastTitle = FD_ROLE(toastTitle, tok::tracking);
#define FD_ROLE_MIXED(n) \
    Role { tok::type_##n##_size, weight_of(tok::type_##n##_style), Case::Mixed, tok::type_##n##_alpha, tok::tracking }
inline constexpr Role saverClock = FD_ROLE_MIXED(saverClock);
#undef FD_ROLE_MIXED
inline constexpr Role saverWord = FD_ROLE(saverWord, tok::tracking);
inline constexpr Role saverTitle = FD_ROLE(saverTitle, tok::tracking);
inline constexpr Role saverLabel = FD_ROLE(saverLabel, tok::tracking);
inline constexpr Role saverFourth = FD_ROLE(saverFourth, tok::tracking);
inline constexpr Role overlayNumber{tok::type_overlayNumber_size, weight_of(tok::type_overlayNumber_style), Case::Mixed,
                                    tok::overlayLit, tok::tracking};
inline constexpr Role volNumber{tok::vol_number, overlayNumber.weight, overlayNumber.tcase, overlayNumber.alpha,
                                overlayNumber.tracking};
inline constexpr Role volLabel{tok::vol_label, overlayLabel.weight, overlayLabel.tcase, overlayLabel.alpha,
                               overlayLabel.tracking};
inline constexpr Role volExit{tok::vol_exit, overlayLabel.weight, overlayLabel.tcase, overlayLabel.alpha,
                              overlayLabel.tracking};
}

#undef FD_ROLE

constexpr uint32_t kWhite = 0xFFFFFF;

}
