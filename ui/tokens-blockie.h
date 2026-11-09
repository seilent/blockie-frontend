#pragma once
#include <cstdint>

namespace fd::tok {

struct Token {
    const char* name;
    const char* value;
    const char* unit;
    int64_t num;
};

inline constexpr Token kTokens[] = {
    {"u", "2.0", "px_per_u", 131072},
    {"canvasW", "480", "u", 31457280},
    {"canvasH", "360", "u", 23592960},
    {"minTextPx", "12", "px", 786432},
    {"capHeight", "0.70", "em", 45875},
    {"xHeight", "0.476", "em", 31195},
    {"tracking", "-0.5", "u", -32768},
    {"trackingHeader", "-1.0", "u", -65536},
    {"type.hubHeader.size", "128", "u", 8388608},
    {"type.hubHeader.style", "Light", "style", 0},
    {"type.hubHeader.case", "lower", "case", 0},
    {"type.hubHeader.alpha", "0.4", "alpha", 26214},
    {"type.pageHeader.size", "150", "u", 9830400},
    {"type.pageHeader.style", "Light", "style", 0},
    {"type.pageHeader.case", "lower", "case", 0},
    {"type.pageHeader.alpha", "0.27", "alpha", 17695},
    {"type.albumHeader.size", "143", "u", 9371648},
    {"type.albumHeader.style", "Bold", "style", 0},
    {"type.albumHeader.case", "upper", "case", 0},
    {"type.albumHeader.alpha", "1.0", "alpha", 65536},
    {"type.homeItem.size", "39.7", "u", 2601779},
    {"type.homeItem.style", "SemiLight", "style", 0},
    {"type.homeItem.case", "lower", "case", 0},
    {"type.homeItem.alpha", "1.0", "alpha", 65536},
    {"type.homeItem.alphaDim", "0.6", "alpha", 39322},
    {"type.homeSubtitle.size", "9", "u", 589824},
    {"type.homeSubtitle.style", "Regular", "style", 0},
    {"type.homeSubtitle.case", "upper", "case", 0},
    {"type.homeSubtitle.alpha", "0.6", "alpha", 39322},
    {"type.quickplayHeader.size", "34", "u", 2228224},
    {"type.quickplayHeader.style", "Light", "style", 0},
    {"type.quickplayHeader.case", "mixed", "case", 0},
    {"type.quickplayHeader.alpha", "0.6", "alpha", 39322},
    {"type.quickplayHint.size", "11", "u", 720896},
    {"type.quickplayHint.style", "Bold", "style", 0},
    {"type.quickplayHint.case", "upper", "case", 0},
    {"type.quickplayHint.alpha", "1.0", "alpha", 65536},
    {"type.qpHint.size", "11.7", "u", 766771},
    {"type.qpHint.style", "Bold", "style", 0},
    {"type.qpHint.case", "upper", "case", 0},
    {"type.qpHint.alpha", "1.0", "alpha", 65536},
    {"type.pivotActive.size", "22", "u", 1441792},
    {"type.pivotActive.style", "Bold", "style", 0},
    {"type.pivotActive.case", "upper", "case", 0},
    {"type.pivotActive.alpha", "1.0", "alpha", 65536},
    {"type.pivotInactive.size", "22", "u", 1441792},
    {"type.pivotInactive.style", "Light", "style", 0},
    {"type.pivotInactive.case", "upper", "case", 0},
    {"type.pivotInactive.alpha", "0.4", "alpha", 26214},
    {"type.sectionLabel.size", "8", "u", 524288},
    {"type.sectionLabel.style", "Regular", "style", 0},
    {"type.sectionLabel.case", "upper", "case", 0},
    {"type.sectionLabel.alpha", "0.6", "alpha", 39322},
    {"type.listPrimary.size", "14", "u", 917504},
    {"type.listPrimary.style", "Regular", "style", 0},
    {"type.listPrimary.case", "mixed", "case", 0},
    {"type.listPrimary.alpha", "1.0", "alpha", 65536},
    {"type.listPrimary.alphaDim", "0.85", "alpha", 55706},
    {"type.listPlaylist.size", "14", "u", 917504},
    {"type.listPlaylist.style", "Bold", "style", 0},
    {"type.listPlaylist.case", "upper", "case", 0},
    {"type.listPlaylist.alpha", "1.0", "alpha", 65536},
    {"type.listPlaylist.alphaDim", "0.85", "alpha", 55706},
    {"type.listSecondary.size", "11", "u", 720896},
    {"type.listSecondary.style", "Light", "style", 0},
    {"type.listSecondary.case", "mixed", "case", 0},
    {"type.listSecondary.alpha", "0.6", "alpha", 39322},
    {"type.listCount.size", "11", "u", 720896},
    {"type.listCount.style", "Light", "style", 0},
    {"type.listCount.case", "upper", "case", 0},
    {"type.listCount.alpha", "0.6", "alpha", 39322},
    {"type.listCollection.size", "18", "u", 1179648},
    {"type.listCollection.style", "Bold", "style", 0},
    {"type.listCollection.case", "upper", "case", 0},
    {"type.listCollection.alpha", "1.0", "alpha", 65536},
    {"type.listLibSecondary.size", "11", "u", 720896},
    {"type.listLibSecondary.style", "Light", "style", 0},
    {"type.listLibSecondary.case", "mixed", "case", 0},
    {"type.listLibSecondary.alpha", "0.4", "alpha", 26214},
    {"type.detailName.size", "11", "u", 720896},
    {"type.detailName.style", "Bold", "style", 0},
    {"type.detailName.case", "upper", "case", 0},
    {"type.detailName.alpha", "1.0", "alpha", 65536},
    {"type.detailTitle.size", "13", "u", 851968},
    {"type.detailTitle.style", "Bold", "style", 0},
    {"type.detailTitle.case", "upper", "case", 0},
    {"type.detailTitle.alpha", "1.0", "alpha", 65536},
    {"type.detailSub.size", "9", "u", 589824},
    {"type.detailSub.style", "Light", "style", 0},
    {"type.detailSub.case", "upper", "case", 0},
    {"type.detailSub.alpha", "0.6", "alpha", 39322},
    {"type.gridCaption.size", "9", "u", 589824},
    {"type.gridCaption.style", "Regular", "style", 0},
    {"type.gridCaption.case", "upper", "case", 0},
    {"type.gridCaption.alpha", "0.6", "alpha", 39322},
    {"type.letterGlyph.size", "20", "u", 1310720},
    {"type.letterGlyph.style", "Regular", "style", 0},
    {"type.letterGlyph.case", "lower", "case", 0},
    {"type.letterGlyph.alpha", "1.0", "alpha", 65536},
    {"type.boxCaption.size", "14.8", "u", 969933},
    {"type.boxCaption.style", "Bold", "style", 0},
    {"type.boxCaption.case", "upper", "case", 0},
    {"type.boxCaption.alpha", "1.0", "alpha", 65536},
    {"type.boxCaptionLib.size", "14.8", "u", 969933},
    {"type.boxCaptionLib.style", "Regular", "style", 0},
    {"type.boxCaptionLib.case", "upper", "case", 0},
    {"type.boxCaptionLib.alpha", "1.0", "alpha", 65536},
    {"type.settingsItem.size", "17", "u", 1114112},
    {"type.settingsItem.style", "Bold", "style", 0},
    {"type.settingsItem.case", "upper", "case", 0},
    {"type.settingsItem.alpha", "1.0", "alpha", 65536},
    {"type.settingsItem.alphaDim", "0.6", "alpha", 39322},
    {"type.albcolTitle.size", "17", "u", 1114112},
    {"type.albcolTitle.style", "Bold", "style", 0},
    {"type.albcolTitle.case", "upper", "case", 0},
    {"type.albcolTitle.alpha", "1.0", "alpha", 65536},
    {"type.albcolArtist.size", "11", "u", 720896},
    {"type.albcolArtist.style", "Light", "style", 0},
    {"type.albcolArtist.case", "upper", "case", 0},
    {"type.albcolArtist.alpha", "0.4", "alpha", 26214},
    {"type.settingsValue.size", "11", "u", 720896},
    {"type.settingsValue.style", "Light", "style", 0},
    {"type.settingsValue.case", "upper", "case", 0},
    {"type.settingsValue.alpha", "0.6", "alpha", 39322},
    {"type.settingsSub.size", "11", "u", 720896},
    {"type.settingsSub.style", "Light", "style", 0},
    {"type.settingsSub.case", "mixed", "case", 0},
    {"type.settingsSub.alpha", "1.0", "alpha", 65536},
    {"type.settingsSub.alphaDim", "0.6", "alpha", 39322},
    {"type.npArtist.size", "20", "u", 1310720},
    {"type.npArtist.style", "Bold", "style", 0},
    {"type.npArtist.case", "upper", "case", 0},
    {"type.npArtist.alpha", "1.0", "alpha", 65536},
    {"type.npAlbum.size", "20", "u", 1310720},
    {"type.npAlbum.style", "Light", "style", 0},
    {"type.npAlbum.case", "upper", "case", 0},
    {"type.npAlbum.alpha", "0.85", "alpha", 55706},
    {"type.npTitle.size", "26", "u", 1703936},
    {"type.npTitle.style", "Light", "style", 0},
    {"type.npTitle.case", "mixed", "case", 0},
    {"type.npTitle.alpha", "1.0", "alpha", 65536},
    {"type.npNext.size", "13", "u", 851968},
    {"type.npNext.style", "Light", "style", 0},
    {"type.npNext.case", "mixed", "case", 0},
    {"type.npNext.alpha", "0.6", "alpha", 39322},
    {"type.npCount.size", "9.45", "u", 619315},
    {"type.npCount.style", "Light", "style", 0},
    {"type.npCount.case", "upper", "case", 0},
    {"type.npCount.alpha", "0.6", "alpha", 39322},
    {"type.time.size", "13", "u", 851968},
    {"type.time.style", "Regular", "style", 0},
    {"type.time.case", "mixed", "case", 0},
    {"type.time.alpha", "0.85", "alpha", 55706},
    {"type.npViewArtist.size", "23.8", "u", 1559757},
    {"type.npViewArtist.style", "Bold", "style", 0},
    {"type.npViewArtist.case", "upper", "case", 0},
    {"type.npViewArtist.alpha", "1.0", "alpha", 65536},
    {"type.npViewAlbum.size", "23.8", "u", 1559757},
    {"type.npViewAlbum.style", "Regular", "style", 0},
    {"type.npViewAlbum.case", "upper", "case", 0},
    {"type.npViewAlbum.alpha", "0.85", "alpha", 55706},
    {"type.npViewTitle.size", "20.3", "u", 1330381},
    {"type.npViewTitle.style", "Regular", "style", 0},
    {"type.npViewTitle.case", "mixed", "case", 0},
    {"type.npViewTitle.alpha", "1.0", "alpha", 65536},
    {"type.npViewNext.size", "9.45", "u", 619315},
    {"type.npViewNext.style", "Light", "style", 0},
    {"type.npViewNext.case", "mixed", "case", 0},
    {"type.npViewNext.alpha", "0.6", "alpha", 39322},
    {"type.npViewTime.size", "13.5", "u", 884736},
    {"type.npViewTime.style", "Bold", "style", 0},
    {"type.npViewTime.case", "mixed", "case", 0},
    {"type.npViewTime.alpha", "0.85", "alpha", 55706},
    {"type.overlayNumber.size", "48", "u", 3145728},
    {"type.overlayNumber.style", "Light", "style", 0},
    {"type.overlayNumber.alpha", "0.6", "alpha", 39322},
    {"type.overlayLabel.size", "7", "u", 458752},
    {"type.overlayLabel.style", "Bold", "style", 0},
    {"type.overlayLabel.case", "upper", "case", 0},
    {"type.overlayLabel.alpha", "0.6", "alpha", 39322},
    {"type.jumpExit.size", "11.73", "u", 768737},
    {"type.jumpExit.style", "Bold", "style", 0},
    {"type.jumpExit.case", "upper", "case", 0},
    {"type.jumpExit.alpha", "0.6", "alpha", 39322},
    {"type.jumpCell.size", "26.2", "u", 1717043},
    {"type.jumpCell.style", "Bold", "style", 0},
    {"type.jumpCell.case", "lower", "case", 0},
    {"type.jumpCell.alpha", "1.0", "alpha", 65536},
    {"type.toastArtist.size", "10", "u", 655360},
    {"type.toastArtist.style", "Bold", "style", 0},
    {"type.toastArtist.case", "upper", "case", 0},
    {"type.toastArtist.alpha", "1.0", "alpha", 65536},
    {"type.toastTitle.size", "14", "u", 917504},
    {"type.toastTitle.style", "Light", "style", 0},
    {"type.toastTitle.case", "mixed", "case", 0},
    {"type.toastTitle.alpha", "1.0", "alpha", 65536},
    {"type.saverClock.size", "50", "u", 3276800},
    {"type.saverClock.style", "Light", "style", 0},
    {"type.saverClock.alpha", "1.0", "alpha", 65536},
    {"type.saverFourth.size", "117.3", "u", 7687373},
    {"type.saverFourth.style", "SemiLight", "style", 0},
    {"type.saverFourth.case", "upper", "case", 0},
    {"type.saverFourth.alpha", "1.0", "alpha", 65536},
    {"type.lockTitle.size", "32", "u", 2097152},
    {"type.lockTitle.style", "Light", "style", 0},
    {"type.lockTitle.alpha", "1.0", "alpha", 65536},
    {"type.lockArtist.size", "20", "u", 1310720},
    {"type.lockArtist.style", "Light", "style", 0},
    {"type.lockArtist.alpha", "0.85", "alpha", 55706},
    {"type.lockAlbum.size", "16", "u", 1048576},
    {"type.lockAlbum.style", "Light", "style", 0},
    {"type.lockAlbum.alpha", "0.6", "alpha", 39322},
    {"type.jumpLetter.size", "120", "u", 7864320},
    {"type.jumpLetter.style", "Light", "style", 0},
    {"type.jumpLetter.case", "lower", "case", 0},
    {"type.jumpLetter.alpha", "1.0", "alpha", 65536},
    {"type.dialogTitle.size", "22", "u", 1441792},
    {"type.dialogTitle.style", "Light", "style", 0},
    {"type.dialogTitle.case", "lower", "case", 0},
    {"type.dialogTitle.alpha", "1.0", "alpha", 65536},
    {"type.dialogItem.size", "17", "u", 1114112},
    {"type.dialogItem.style", "Bold", "style", 0},
    {"type.dialogItem.case", "upper", "case", 0},
    {"type.dialogItem.alpha", "1.0", "alpha", 65536},
    {"type.dialogItem.alphaDim", "0.6", "alpha", 39322},
    {"type.watermark.size", "14", "u", 917504},
    {"type.watermark.style", "Light", "style", 0},
    {"type.watermark.case", "lower", "case", 0},
    {"type.watermark.alpha", "0.08", "alpha", 5243},
    {"color.bg", "#111111", "rgb", 0x111111},
    {"color.bgElevated", "#181818", "rgb", 0x181818},
    {"color.tile", "#202020", "rgb", 0x202020},
    {"color.tilePressed", "#2A2A2A", "rgb", 0x2A2A2A},
    {"color.border", "#2C2C2C", "rgb", 0x2C2C2C},
    {"color.pageHeader", "#FFFFFF", "rgb", 0xFFFFFF},
    {"color.pivotInactive", "#FFFFFF", "rgb", 0xFFFFFF},
    {"alpha.full", "1.0", "alpha", 65536},
    {"alpha.high", "0.85", "alpha", 55706},
    {"alpha.mid", "0.6", "alpha", 39322},
    {"alpha.low", "0.4", "alpha", 26214},
    {"alpha.faint", "0.08", "alpha", 5243},
    {"scrim.sheet.alpha", "0.55", "alpha", 36045},
    {"scrim.overlay.alpha", "0.8", "alpha", 52429},
    {"color.black", "#000000", "rgb", 0x000000},
    {"dim.alpha", "0.6", "alpha", 39322},
    {"backdrop.stop0.pos", "0.0", "ratio", 0},
    {"backdrop.stop0.alpha", "0.15", "alpha", 9830},
    {"backdrop.stop1.pos", "0.55", "ratio", 36045},
    {"backdrop.stop1.alpha", "0.35", "alpha", 22938},
    {"backdrop.stop2.pos", "1.0", "ratio", 65536},
    {"backdrop.stop2.alpha", "0.75", "alpha", 49152},
    {"pageSurface.alphaTop", "0.15", "alpha", 9830},
    {"accent.hueDefault", "350", "deg", 22937600},
    {"accent.hueStep", "10", "deg", 655360},
    {"accent.hueCount", "36", "count", 2359296},
    {"accent.value", "0.98", "ratio", 64225},
    {"accent.satMax", "83", "pct", 5439488},
    {"accent.satMin", "40", "pct", 2621440},
    {"accent.lumFloor", "0.149", "ratio", 9765},
    {"accent.brightSat", "0.84", "ratio", 55050},
    {"accent.default", "#FA2A4D", "rgb", 0xFA2A4D},
    {"accent.brightDefault", "#FF4D6B", "rgb", 0xFF4D6B},
    {"accent.h0", "#FA2A2A", "rgb", 0xFA2A2A},
    {"accent.h10", "#FA4D2A", "rgb", 0xFA4D2A},
    {"accent.h20", "#FA702A", "rgb", 0xFA702A},
    {"accent.h30", "#FA922A", "rgb", 0xFA922A},
    {"accent.h40", "#FAB52A", "rgb", 0xFAB52A},
    {"accent.h50", "#FAD72A", "rgb", 0xFAD72A},
    {"accent.h60", "#FAFA2A", "rgb", 0xFAFA2A},
    {"accent.h70", "#D7FA2A", "rgb", 0xD7FA2A},
    {"accent.h80", "#B5FA2A", "rgb", 0xB5FA2A},
    {"accent.h90", "#92FA2A", "rgb", 0x92FA2A},
    {"accent.h100", "#70FA2A", "rgb", 0x70FA2A},
    {"accent.h110", "#4DFA2A", "rgb", 0x4DFA2A},
    {"accent.h120", "#2AFA2A", "rgb", 0x2AFA2A},
    {"accent.h130", "#2AFA4D", "rgb", 0x2AFA4D},
    {"accent.h140", "#2AFA70", "rgb", 0x2AFA70},
    {"accent.h150", "#2AFA92", "rgb", 0x2AFA92},
    {"accent.h160", "#2AFAB5", "rgb", 0x2AFAB5},
    {"accent.h170", "#2AFAD7", "rgb", 0x2AFAD7},
    {"accent.h180", "#2AFAFA", "rgb", 0x2AFAFA},
    {"accent.h190", "#2AD7FA", "rgb", 0x2AD7FA},
    {"accent.h200", "#2AB5FA", "rgb", 0x2AB5FA},
    {"accent.h210", "#2A92FA", "rgb", 0x2A92FA},
    {"accent.h220", "#2A70FA", "rgb", 0x2A70FA},
    {"accent.h230", "#395AFA", "rgb", 0x395AFA},
    {"accent.h240", "#5555FA", "rgb", 0x5555FA},
    {"accent.h250", "#6A4DFA", "rgb", 0x6A4DFA},
    {"accent.h260", "#7F41FA", "rgb", 0x7F41FA},
    {"accent.h270", "#932DFA", "rgb", 0x932DFA},
    {"accent.h280", "#B52AFA", "rgb", 0xB52AFA},
    {"accent.h290", "#D72AFA", "rgb", 0xD72AFA},
    {"accent.h300", "#FA2AFA", "rgb", 0xFA2AFA},
    {"accent.h310", "#FA2AD7", "rgb", 0xFA2AD7},
    {"accent.h320", "#FA2AB5", "rgb", 0xFA2AB5},
    {"accent.h330", "#FA2A92", "rgb", 0xFA2A92},
    {"accent.h340", "#FA2A70", "rgb", 0xFA2A70},
    {"accent.h350", "#FA2A4D", "rgb", 0xFA2A4D},
    {"focus.border", "2", "u", 131072},
    {"focus.barWidth", "2", "u", 131072},
    {"focus.barOffset", "8", "u", 524288},
    {"scrollbar.width", "4", "u", 262144},
    {"scrollbar.x", "236", "u", 15466496},
    {"scrollbar.min", "25", "u", 1638400},
    {"scrollbar.alpha", "0.2", "alpha", 13107},
    {"scrollbar.hold", "1500", "ms", 98304000},
    {"scrollbar.fade", "500", "ms", 32768000},
    {"edge", "16", "u", 1048576},
    {"hubEdge", "20", "u", 1310720},
    {"homeTextX", "52", "u", 3407872},
    {"homePitch", "55", "u", 3604480},
    {"homeFirstBaseline", "100.6", "u", 6592922},
    {"homeSubtitleX", "11.1", "u", 727450},
    {"homeSubtitleDrop", "3.5", "u", 229376},
    {"homeWatermarkBaseline", "410.66667", "u", 26913451},
    {"qpPeekScale", "0.30", "ratio", 19661},
    {"qpPeekX", "-3.3", "u", -216269},
    {"qpPeekY", "165.1", "u", 10819994},
    {"qpPeekAlpha", "0.4", "alpha", 26214},
    {"headerBand", "70", "u", 4587520},
    {"headerXHeightBottom", "66", "u", 4325376},
    {"pivotTop", "58", "u", 3801088},
    {"pivotBottom", "86", "u", 5636096},
    {"pivotBaseline", "79", "u", 5177344},
    {"pivotGap", "6.6", "u", 432538},
    {"pivotActiveGap", "7.7", "u", 504627},
    {"pivotLeadGap", "22", "u", 1441792},
    {"contentTop", "98", "u", 6422528},
    {"page.headerX", "-22", "u", -1441792},
    {"page.headerBaseline", "52", "u", 3407872},
    {"page.pivotBaseline", "85.5", "u", 5603328},
    {"page.contentTop", "120", "u", 7864320},
    {"page.countBaseline", "85.5", "u", 5603328},
    {"page.albumHeaderX", "-28", "u", -1835008},
    {"page.settingsHeaderX", "-17.3", "u", -1133773},
    {"page.settingsHeaderAlpha", "0.64", "alpha", 41943},
    {"page.rowTitle", "19", "u", 1245184},
    {"tileS", "53", "u", 3473408},
    {"gridTile", "65", "u", 4259840},
    {"gridX", "7", "u", 458752},
    {"gridGap", "15.5", "u", 1015808},
    {"gutter", "12", "u", 786432},
    {"gridCols", "3", "count", 196608},
    {"tileCaptionDrop", "11", "u", 720896},
    {"tileCaptionPitch", "11", "u", 720896},
    {"tileCaptionBand", "24", "u", 1572864},
    {"tileCellH", "89", "u", 5832704},
    {"tileRowPitch", "101", "u", 6619136},
    {"tileL", "116", "u", 7602176},
    {"tileLGutter", "8", "u", 524288},
    {"letterTileBorder", "1", "u", 65536},
    {"letterTileBorderAlpha", "0.4", "alpha", 26214},
    {"letterTileBorderFocused", "2", "u", 131072},
    {"boxLine", "1", "u", 65536},
    {"boxLineAlpha", "0.4", "alpha", 26214},
    {"boxInset", "1.7", "u", 111411},
    {"boxCaptionPitch", "12.1", "u", 792986},
    {"boxCaptionX", "3.75", "u", 245760},
    {"boxCaptionBase", "4.4", "u", 288358},
    {"boxLines", "3", "count", 196608},
    {"boxLinesPlaying", "2", "count", 131072},
    {"pressFrame", "6.2", "u", 406323},
    {"letterInsetX", "6", "u", 393216},
    {"letterInsetBaseline", "8", "u", 524288},
    {"rowHeight", "40", "u", 2621440},
    {"rowTextInset", "4", "u", 262144},
    {"rowCollection", "55", "u", 3604480},
    {"rowPlayRing", "33", "u", 2162688},
    {"rowPlayRingX", "26", "u", 1703936},
    {"rowCollectionTextX", "44.5", "u", 2916352},
    {"rowThumb", "20", "u", 1310720},
    {"rowThumbGap", "2", "u", 131072},
    {"rowThumbDrop", "16", "u", 1048576},
    {"dividerTileCollection", "51", "u", 3342336},
    {"dividerTile", "36", "u", 2359296},
    {"rowRing", "20", "u", 1310720},
    {"songIndent", "24", "u", 1572864},
    {"detailRow", "124", "u", 8126464},
    {"detailNameDrop", "15", "u", 983040},
    {"detailNameBaseline", "100.5", "u", 6586368},
    {"detailRingTop", "140.5", "u", 9207808},
    {"detailRing", "32", "u", 2097152},
    {"detailRingGap", "24", "u", 1572864},
    {"detailRingCount", "3", "count", 196608},
    {"detailRingInset", "8", "u", 524288},
    {"detailLabelDrop", "12", "u", 786432},
    {"detailLabelBaseline", "190.5", "u", 12484608},
    {"detailRowsTop", "202.5", "u", 13271040},
    {"detailTextX", "144", "u", 9437184},
    {"detailTitleDrop", "28", "u", 1835008},
    {"detailTitlePitch", "15", "u", 983040},
    {"detailTitleLines", "2", "count", 131072},
    {"detailSubPitch", "13", "u", 851968},
    {"detailRingX0", "20", "u", 1310720},
    {"detailRingX1", "76", "u", 4980736},
    {"detailRingX2", "132", "u", 8650752},
    {"albumArtX", "16", "u", 1048576},
    {"albumArtTop", "124", "u", 8126464},
    {"albcolTop", "99.5", "u", 6520832},
    {"albcolPitch", "119.5", "u", 7831552},
    {"albcolRing", "33", "u", 2162688},
    {"albcolRingInset", "20.7", "u", 1356595},
    {"albcolRingDrop", "22.5", "u", 1474560},
    {"albcolTextGap", "6", "u", 393216},
    {"albcolTitleTop", "26.9", "u", 1762918},
    {"albcolTitlePitch", "19.6", "u", 1284506},
    {"albcolTitleLines", "2", "count", 131072},
    {"albcolArtistPitch", "15", "u", 983040},
    {"albcolTrackDrop", "10", "u", 655360},
    {"albcolTrackPitch", "10", "u", 655360},
    {"albcolLetter", "61", "u", 3997696},
    {"albumRowsGap", "12", "u", 786432},
    {"albumRowsTop", "252", "u", 16515072},
    {"settingsPitch", "43", "u", 2818048},
    {"settingsLabelBaselineInset", "8", "u", 524288},
    {"settingsSwatch", "12", "u", 786432},
    {"settingsSwatchGap", "4", "u", 262144},
    {"settingsTitlePitch", "17", "u", 1114112},
    {"settingsSubPad", "0.3259", "ratio", 21358},
    {"settingsValueFocused", "0.85", "alpha", 55706},
    {"settingsValueDim", "0.4", "alpha", 26214},
    {"eqValueGap", "8", "u", 524288},
    {"queueCountBaseline", "79", "u", 5177344},
    {"queueRowsTop", "120", "u", 7864320},
    {"backRing", "64", "u", 4194304},
    {"backRingStroke", "4", "u", 262144},
    {"backRingX", "-6", "u", -393216},
    {"backRingY", "-15", "u", -983040},
    {"npBackRing", "68.7", "u", 4502323},
    {"npBackRingStroke", "5.2", "u", 340787},
    {"npBackRingX", "-3.9", "u", -255590},
    {"npBackRingY", "-14.4", "u", -943718},
    {"npBackGlyph", "42.5", "u", 2785280},
    {"touchSlop", "25", "u", 1638400},
    {"flingCap", "2000", "u_per_s", 131072000},
    {"flingStop", "1", "u_per_s", 65536},
    {"flingMin", "50", "u_per_s", 3276800},
    {"npTextTop", "86", "u", 5636096},
    {"npAlbumBaseline", "110", "u", 7208960},
    {"npArtistY", "111.33333", "u", 7296341},
    {"npAlbumY", "135.2", "u", 8860467},
    {"npFromBaseline", "91.33333", "u", 5985621},
    {"npArtTop", "140.5", "u", 9207808},
    {"npArt", "152.7", "u", 10007347},
    {"npProgressY", "293.2", "u", 19215155},
    {"npProgressThick", "2", "u", 131072},
    {"npTimeDrop", "14", "u", 917504},
    {"npBarHeight", "2.91", "u", 190710},
    {"npBarGap", "0.88", "u", 57672},
    {"npBarTrack", "0.15", "alpha", 9830},
    {"npTimeBaseline", "15.9", "u", 1042022},
    {"npTitleY", "332.7", "u", 21803827},
    {"npTextX", "14.3", "u", 937165},
    {"npArtX", "14.1", "u", 924058},
    {"npTextRight", "221.5", "u", 14516224},
    {"npNextIndent", "35.7", "u", 2339635},
    {"npNextPitch", "18", "u", 1179648},
    {"npNextDrop", "15.2", "u", 996147},
    {"npNextLinePitch", "11.8", "u", 773325},
    {"npNextCount", "3", "count", 196608},
    {"npNextNavCount", "2", "count", 131072},
    {"npRing", "29.7", "u", 1946419},
    {"npRingStroke", "2.5", "u", 163840},
    {"npRepeatOne", "20", "u", 1310720},
    {"npRepeatOneR", "15.5", "u", 1015808},
    {"npRepeatOneAt", "317", "deg", 20774912},
    {"npRepeatOneSweep", "270", "deg", 17694720},
    {"npRingY", "398.46667", "u", 26113912},
    {"npRingNavY", "387.81667", "u", 25415953},
    {"npRingX0", "31.2", "u", 2044723},
    {"npRingX1", "120", "u", 7864320},
    {"npRingX2", "208.8", "u", 13683917},
    {"npRingCx0", "31.9", "u", 2090598},
    {"npRingCx1", "119.9", "u", 7857766},
    {"npRingCx2", "207.4", "u", 13592166},
    {"npBtGlyph", "64", "u", 4194304},
    {"seekStepMs", "10000", "ms", 655360000},
    {"seekHoldMs", "400", "ms", 26214400},
    {"seekTickMs", "250", "ms", 16384000},
    {"seekTickStepMs", "1000", "ms", 65536000},
    {"seekEndMarginMs", "1000", "ms", 65536000},
    {"statusHeight", "12", "u", 786432},
    {"statusTop", "10", "u", 655360},
    {"statusGap", "8", "u", 524288},
    {"npStatusHeight", "11.5", "u", 753664},
    {"npStatusTop", "5.9", "u", 386662},
    {"npStatusRight", "13.7", "u", 897843},
    {"npStatusGap", "7.7", "u", 504627},
    {"npStatusText", "11.3", "u", 740557},
    {"npStatusBatteryW", "18.1", "u", 1186202},
    {"npStatusBatteryH", "7.7", "u", 504627},
    {"statusBatteryX", "6", "box", 393216},
    {"statusBatteryY", "12", "box", 786432},
    {"statusBatteryW", "16", "box", 1048576},
    {"statusBatteryH", "8", "box", 524288},
    {"saverArt", "56", "u", 3670016},
    {"saverBottomMargin", "24", "u", 1572864},
    {"saverFourthBaseline", "432.0", "u", 28311552},
    {"saverAlbumPitch", "16", "u", 1048576},
    {"saverClockBaseline", "51", "u", 3342336},
    {"lockArt", "56", "u", 3670016},
    {"lockBtGlyph", "19.33333", "u", 1267029},
    {"lockArtY", "346.66667", "u", 22719147},
    {"lockTextX", "80", "u", 5242880},
    {"lockTitleDrop", "22.4", "u", 1468006},
    {"lockArtistPitch", "20", "u", 1310720},
    {"lockAlbumPitch", "16", "u", 1048576},
    {"overlayRing", "64", "u", 4194304},
    {"overlayGlyph", "20", "u", 1310720},
    {"overlayGlyphOffset", "80", "u", 5242880},
    {"toastHeight", "40", "u", 2621440},
    {"toastArtistBaseline", "16", "u", 1048576},
    {"toastTitleBaseline", "34", "u", 2228224},
    {"jumpCols", "4", "count", 262144},
    {"jumpGap", "4.24", "u", 277873},
    {"jumpLeft", "10.01", "u", 656015},
    {"jumpPitch", "56.03", "u", 3671982},
    {"jumpExitCells", "2", "count", 131072},
    {"jumpCells", "38", "count", 2490368},
    {"jumpTop", "19.54", "u", 1280573},
    {"jumpExitCx", "23.3", "u", 1526989},
    {"jumpExitTop", "21.1", "u", 1382810},
    {"jumpLetterInsetX", "2.78", "u", 182190},
    {"jumpLetterInsetBaseline", "4.01", "u", 262799},
    {"jumpSearchGlyph", "17", "u", 1114112},
    {"kb.fieldTop", "16", "u", 1048576},
    {"kb.fieldH", "37", "u", 2424832},
    {"kb.fieldPad", "10", "u", 655360},
    {"kb.fieldBorder", "1", "u", 65536},
    {"kb.cursorW", "2", "u", 131072},
    {"kb.keyW", "50.5", "u", 3309568},
    {"kb.keyH", "85.91667", "u", 5630635},
    {"kb.top", "61", "u", 3997696},
    {"glyphBox", "32", "u", 2097152},
    {"glyphStroke", "4", "u", 262144},
    {"ringStrokeFocused", "4", "u", 262144},
    {"ringStrokeIdle", "3", "u", 196608},
    {"ringArcStart", "-90", "deg", -5898240},
    {"overlayPlusY", "72", "u", 4718592},
    {"overlayAnchorH", "480", "u", 31457280},
    {"overlayAnchorScale", "0.88893", "ratio", 58257},
    {"overlayRingY", "192", "u", 12582912},
    {"overlayMinusY", "330.66667", "u", 21670571},
    {"overlayBottomMargin", "24", "u", 1572864},
    {"overlayTextPitch", "18", "u", 1179648},
    {"overlayTitleBottom", "402.66667", "u", 26389163},
    {"overlayLit", "1.0", "alpha", 65536},
    {"overlayDim", "0.4", "alpha", 26214},
    {"vol.ring", "76.8", "u", 5033165},
    {"vol.ringStroke", "5.3", "u", 347341},
    {"vol.hit", "64", "u", 4194304},
    {"vol.plus", "41.2", "u", 2700083},
    {"vol.minus", "43.47", "u", 2848850},
    {"vol.pause", "41.6", "u", 2726298},
    {"vol.skip", "46", "u", 3014656},
    {"vol.skipX", "88.2", "u", 5780275},
    {"vol.pauseGap", "9.3", "u", 609485},
    {"vol.glowSkip", "4", "u", 262144},
    {"vol.glowRing", "3.1", "u", 203162},
    {"pressGlowDilate", "1.32", "u", 86508},
    {"pressGlow", "1.76", "u", 115343},
    {"pressGlowAlpha", "0.75", "alpha", 49152},
    {"vol.discAlpha", "0.47", "ratio", 30802},
    {"vol.number", "82.1", "u", 5380506},
    {"vol.label", "11.5", "u", 753664},
    {"vol.exit", "12.4", "u", 812646},
    {"vol.plusY", "96", "u", 6291456},
    {"vol.numberY", "92.45", "u", 6058803},
    {"vol.numberGap", "3", "u", 196608},
    {"vol.ringY", "206.23", "u", 13515489},
    {"vol.minusY", "309.35", "u", 20273562},
    {"vol.slideStride", "53.33333", "u", 3495253},
    {"vol.exitX", "51.18", "u", 3354132},
    {"vol.exitY", "40", "u", 2621440},
    {"vol.textX", "22.06", "u", 1445724},
    {"vol.textY", "337.78", "u", 22136750},
    {"vol.textGap", "18", "u", 1179648},
    {"vol.queueX", "187.06", "u", 12259164},
    {"vol.queueDrop", "13", "u", 851968},
    {"vol.queueRing", "31.3", "u", 2051277},
    {"vol.queueStroke", "3.53", "u", 231342},
    {"batteryLowPercent", "10", "pct", 655360},
    {"qpEdge", "60", "u", 3932160},
    {"qpTile", "77", "u", 5046272},
    {"qpGutter", "2", "u", 131072},
    {"qpW", "216", "u", 14155776},
    {"qpFirstBaseline", "35", "u", 2293760},
    {"qpContentDrop", "5", "u", 327680},
    {"qpSectionGap", "67.5", "u", 4423680},
    {"qpHintDrop", "16", "u", 1048576},
    {"qpHintPitch", "11", "u", 720896},
    {"qpHintW", "138", "u", 9043968},
    {"qpTilePad", "5", "u", 327680},
    {"qpTextX", "30.0", "u", 1966080},
    {"qpBaseline", "98.9", "u", 6481510},
    {"qpTileTop", "4.9", "u", 321126},
    {"qpHintBase", "11.8", "u", 773325},
    {"qpHintStep", "10.0", "u", 655360},
    {"qpHintGap", "71.4", "u", 4679270},
    {"qpEndPad", "68.8", "u", 4508877},
    {"qpPlayTop", "2.4", "u", 157286},
    {"qpPlayGap", "52.4", "u", 3434086},
    {"qpTiltAngle", "0", "deg", 0},
    {"qpTiltDepth", "7.8", "u", 511181},
    {"homeBarX", "44", "u", 2883584},
    {"homeBarTop", "27.79", "u", 1821245},
    {"homeBarH", "37.59", "u", 2463498},
    {"focusBandBottom", "386.66667", "u", 25340587},
    {"motion.perspectiveRatio", "2.3", "ratio", 150733},
    {"motion.perspective", "552", "u", 36175872},
    {"motion.homeEnter", "220", "ms", 14417920},
    {"motion.homeEnterPeek", "420", "ms", 27525120},
    {"motion.homeEnterStagger", "20", "ms", 1310720},
    {"motion.homeEnterShift", "24", "u", 1572864},
    {"motion.swap", "167", "ms", 10944512},
    {"motion.leave", "133", "ms", 8716288},
    {"motion.headerZoom", "1.34", "ratio", 87818},
    {"motion.headerZoomX", "92.4", "u", 6055526},
    {"motion.headerZoomY", "191.6", "u", 12556698},
    {"motion.headerDelay", "138", "ms", 9043968},
    {"motion.header", "135", "ms", 8847360},
    {"motion.pivotInX", "49.8", "u", 3263693},
    {"motion.pivotInY", "28.2", "u", 1848115},
    {"motion.pivotDelay", "167", "ms", 10944512},
    {"motion.pivotIn", "167", "ms", 10944512},
    {"motion.pivotFade", "67", "ms", 4390912},
    {"motion.groundDelay", "167", "ms", 10944512},
    {"motion.ground", "133", "ms", 8716288},
    {"motion.contentScale", "0.71", "ratio", 46531},
    {"motion.contentX", "0.5", "ratio", 32768},
    {"motion.contentY", "0.4875", "ratio", 31949},
    {"motion.contentDelay", "434", "ms", 28442624},
    {"motion.content", "200", "ms", 13107200},
    {"motion.t2Swap", "300", "ms", 19660800},
    {"motion.t2Word", "435", "ms", 28508160},
    {"motion.t2BaseDelay", "0", "ms", 0},
    {"motion.t2Base", "435", "ms", 28508160},
    {"motion.t2Peek", "167", "ms", 10944512},
    {"motion.t2PivotDelay", "300", "ms", 19660800},
    {"motion.t2GroundDelay", "300", "ms", 19660800},
    {"motion.t2Ground", "67", "ms", 4390912},
    {"motion.t2ContentDelay", "595", "ms", 38993920},
    {"motion.t2Content", "460", "ms", 30146560},
    {"motion.t2FadeDelay", "634", "ms", 41549824},
    {"motion.t2Fade", "100", "ms", 6553600},
    {"motion.t4hSwap", "300", "ms", 19660800},
    {"motion.t4hPivot", "67", "ms", 4390912},
    {"motion.t4hContent", "167", "ms", 10944512},
    {"motion.t4hWord", "233", "ms", 15269888},
    {"motion.t4hItems", "33", "ms", 2162688},
    {"motion.t4hSubDelay", "326", "ms", 21364736},
    {"motion.t4hSub", "220", "ms", 14417920},
    {"motion.t4hPeekDelay", "167", "ms", 10944512},
    {"motion.t4hPeek", "200", "ms", 13107200},
    {"motion.t4mDelay", "-12", "ms", -786432},
    {"motion.t4m", "360", "ms", 23592960},
    {"motion.morphPop", "400", "ms", 26214400},
    {"motion.morphPush", "450", "ms", 29491200},
    {"motion.t3mTileDelay", "-44", "ms", -2883584},
    {"motion.t3mTile", "595", "ms", 38993920},
    {"motion.t3mLabelDelay", "-16", "ms", -1048576},
    {"motion.t3mLabel", "510", "ms", 33423360},
    {"motion.t3mBaseDelay", "14", "ms", 917504},
    {"motion.t3mBase", "600", "ms", 39321600},
    {"motion.t3mFadeDelay", "500", "ms", 32768000},
    {"motion.t3mFade", "250", "ms", 16384000},
    {"motion.morphScale", "150", "ms", 9830400},
    {"motion.morphRowPenDelay", "138", "ms", 9043968},
    {"motion.morphRowPen", "332", "ms", 21757952},
    {"motion.morphRowPopDelay", "87", "ms", 5701632},
    {"motion.morphRowPop", "285", "ms", 18677760},
    {"motion.wordHoldMax", "167", "ms", 10944512},
    {"motion.t4hWordDelay", "67", "ms", 4390912},
    {"motion.t4hBox", "300", "ms", 19660800},
    {"motion.contentAfter", "50", "ms", 3276800},
    {"motion.rowsDy", "0.79", "ratio", 51773},
    {"motion.rowsYa", "35.3", "u", 2313421},
    {"motion.rowsDx", "20.5", "u", 1343488},
    {"motion.rows", "418", "ms", 27394048},
    {"motion.rowsFade", "134", "ms", 8781824},
    {"motion.rowsT2Alpha", "0.6", "alpha", 39322},
    {"motion.rowsT2Fade", "132", "ms", 8650752},
    {"motion.contentHubRows", "step", "mode", 0},
    {"motion.contentPageRows", "step", "mode", 0},
    {"motion.leaveFade", "fade", "mode", 0},
    {"motion.contentZoom", "zoom", "mode", 0},
    {"motion.chromeSlide", "slide", "mode", 0},
    {"motion.headerForm", "zoom", "mode", 0},
    {"motion.t3mForm", "fade", "mode", 0},
    {"motion.t3aFadeDelay", "500", "ms", 32768000},
    {"motion.t3aFade", "250", "ms", 16384000},
    {"motion.tileBack", "150", "ms", 9830400},
    {"motion.t3cFade", "200", "ms", 13107200},
    {"motion.wordForm", "morph", "mode", 0},
    {"motion.slices", "2", "count", 131072},
    {"motion.slicesAlbums", "4", "count", 262144},
    {"motion.slicesSettings", "2", "count", 131072},
    {"motion.pivotOut", "50", "ms", 3276800},
    {"motion.pivotStrip", "133", "ms", 8716288},
    {"motion.pivotLabel", "0", "ms", 0},
    {"motion.pivotRows", "150", "ms", 9830400},
    {"motion.pivotShift", "34", "u", 2228224},
    {"motion.pivotRowStagger", "0", "ms", 0},
    {"motion.pivotRowCap", "10", "count", 655360},
    {"motion.pivotHeaderParallax", "0", "ratio", 0},
    {"motion.pivotStripShift", "22.5", "u", 1474560},
    {"motion.pivotStripAlpha", "0.40", "alpha", 26214},
    {"motion.pivotRowInShift", "25", "u", 1638400},
    {"motion.pivotRowFade", "200", "ms", 13107200},
    {"motion.quickplay", "420", "ms", 27525120},
    {"motion.quickplayAngle", "-6", "deg", -393216},
    {"motion.quickplayScale", "0.445", "ratio", 29164},
    {"motion.quickplayAlpha", "0.6", "alpha", 39322},
    {"motion.quickplayMenuInset", "75.7", "u", 4961075},
    {"motion.quickplayMenuX", "164.3", "u", 10767565},
    {"motion.quickplayMenuY", "133.3", "u", 8735949},
    {"motion.nowPlayingIn", "300", "ms", 19660800},
    {"motion.nowPlayingScale", "0.9", "ratio", 58982},
    {"motion.nowPlayingOriginX", "0.4", "ratio", 26214},
    {"motion.nowPlayingOriginY", "0.5", "ratio", 32768},
    {"motion.nowPlayingFade", "300", "ms", 19660800},
    {"motion.npDark", "67", "ms", 4390912},
    {"motion.t8OutDelay", "-12", "ms", -786432},
    {"motion.t8Out", "162", "ms", 10616832},
    {"motion.t8OutAngle", "40.0", "deg", 2621440},
    {"motion.t8OutEnd", "167", "ms", 10944512},
    {"motion.t8OutHinge", "-151.6", "u", -9935258},
    {"motion.t8OutEye", "0.5029", "ratio", 32958},
    {"motion.t8InFade", "33", "ms", 2162688},
    {"motion.t8InDelay", "36", "ms", 2359296},
    {"motion.t8In", "295", "ms", 19333120},
    {"motion.t8InAngle", "-22.0", "deg", -1441792},
    {"motion.t8InHinge", "24.6", "u", 1612186},
    {"motion.t8InEye", "0.4642", "ratio", 30422},
    {"motion.t8SlideX", "-18.3", "u", -1199309},
    {"motion.t8SlideY", "3.5", "u", 229376},
    {"motion.t8SlideDelay", "38", "ms", 2490368},
    {"motion.t8Slide", "280", "ms", 18350080},
    {"motion.t8PageFadeDelay", "-15", "ms", -983040},
    {"motion.t8PageFade", "235", "ms", 15400960},
    {"motion.t8PageX", "-26.5", "u", -1736704},
    {"motion.t8Page", "300", "ms", 19660800},
    {"motion.t7Leave", "33", "ms", 2162688},
    {"motion.t7Fade", "333", "ms", 21823488},
    {"motion.t7Angle", "39.4", "deg", 2582118},
    {"motion.t7TurnDelay", "50", "ms", 3276800},
    {"motion.t7Turn", "400", "ms", 26214400},
    {"motion.t7Rest", "350", "ms", 22937600},
    {"motion.t7Hinge", "-89.1", "u", -5839258},
    {"motion.t7Eye", "0.5148", "ratio", 33738},
    {"motion.t7PaneLeft", "22.6", "u", 1481114},
    {"motion.t7PaneRight", "10.9", "u", 714342},
    {"motion.t7PaneBack", "6.2", "u", 406323},
    {"motion.t7PaneShift", "117", "ms", 7667712},
    {"motion.t7PaneStep", "50", "ms", 3276800},
    {"motion.t7PaneAngle", "82.1", "deg", 5380506},
    {"motion.t7PaneDelay", "69", "ms", 4521984},
    {"motion.t7PaneTurn", "215", "ms", 14090240},
    {"motion.t7PaneDraw", "133", "ms", 8716288},
    {"motion.t7PaneEnd", "334", "ms", 21889024},
    {"motion.t7PaneInk", "300", "ms", 19660800},
    {"motion.t7PaneHinge", "61.1", "u", 4004250},
    {"motion.t7RingX", "-116.5", "u", -7634944},
    {"motion.t7RingLeave", "300", "ms", 19660800},
    {"motion.t7RingMusicDelay", "200", "ms", 13107200},
    {"motion.t7RingMusic", "100", "ms", 6553600},
    {"motion.t7RingDark", "50", "ms", 3276800},
    {"motion.npIn", "turn", "mode", 0},
    {"motion.npPace", "2", "count", 131072},
    {"motion.npSlices", "2", "count", 131072},
    {"motion.npContentDelay", "200", "ms", 13107200},
    {"motion.trackHide", "500", "ms", 32768000},
    {"motion.trackShow", "400", "ms", 26214400},
    {"motion.trackArtScale", "0.8", "ratio", 52429},
    {"motion.trackArtOriginX", "0.2", "ratio", 13107},
    {"motion.trackArtOriginY", "0.5", "ratio", 32768},
    {"motion.trackShiftOut", "50", "u", 3276800},
    {"motion.trackShiftIn", "-20", "u", -1310720},
    {"motion.npSlideLand", "600", "ms", 39321600},
    {"motion.npSlideTextDelay", "300", "ms", 19660800},
    {"motion.npSlideText", "200", "ms", 13107200},
    {"motion.variantFade", "380", "ms", 24903680},
    {"motion.sheet", "380", "ms", 24903680},
    {"motion.sheetScrim", "0.55", "alpha", 36045},
    {"motion.dialog", "300", "ms", 19660800},
    {"motion.dialogHide", "150", "ms", 9830400},
    {"motion.dialogScale", "0.9", "ratio", 58982},
    {"motion.dialogScrim", "0.96", "alpha", 62915},
    {"motion.swivelIn", "350", "ms", 22937600},
    {"motion.swivelOut", "250", "ms", 16384000},
    {"motion.swivelAngle", "30", "deg", 1966080},
    {"motion.swivelDim", "0.4", "alpha", 26214},
    {"motion.swivelHingeY", "213.33333", "u", 13981013},
    {"motion.jumpHold", "350", "ms", 22937600},
    {"motion.jumpFade", "380", "ms", 24903680},
    {"motion.jumpIn", "300", "ms", 19660800},
    {"motion.jumpOut", "180", "ms", 11796480},
    {"jump.emptyAlpha", "0.06", "alpha", 3932},
    {"motion.saver", "380", "ms", 24903680},
    {"motion.focusCursor", "100", "ms", 6553600},
    {"motion.focusAlpha", "200", "ms", 13107200},
    {"motion.scroll", "100", "ms", 6553600},
    {"motion.page", "500", "ms", 32768000},
    {"motion.press", "100", "ms", 6553600},
    {"motion.pressScale", "0.96", "ratio", 62915},
    {"motion.touchReturnDelay", "200", "ms", 13107200},
    {"motion.pressGlow", "50", "ms", 3276800},
    {"motion.flingRetention", "0.95", "ratio", 62259},
    {"motion.flingTick", "16", "ms", 1048576},
    {"motion.flingWindow", "100", "ms", 6553600},
    {"motion.flingMaxStep", "33", "ms", 2162688},
    {"motion.listFlingRetention", "0.83", "ratio", 54395},
    {"motion.qpFlingRetention", "0.974", "ratio", 63832},
    {"motion.qpFlingStop", "50", "u_per_s", 3276800},
    {"motion.overscrollLead", "16", "ms", 1048576},
    {"motion.overscrollCap", "7.1", "u", 465306},
    {"motion.overscrollBackQp", "100", "ms", 6553600},
    {"motion.overscrollBackList", "120", "ms", 7864320},
    {"motion.overscrollLand", "0.1", "u", 6554},
    {"motion.toggle", "400", "ms", 26214400},
    {"motion.toggleScale", "0.7", "ratio", 45875},
    {"motion.toggleSwap", "200", "ms", 13107200},
    {"motion.idle", "5000", "ms", 327680000},
    {"motion.idleFade", "380", "ms", 24903680},
    {"motion.idleAlpha", "0.4", "alpha", 26214},
    {"motion.progress", "250", "ms", 16384000},
    {"motion.elapsedTick", "1000", "ms", 65536000},
    {"motion.valueChange", "200", "ms", 13107200},
    {"motion.valueDip", "0.2", "alpha", 13107},
    {"motion.pulse.t1", "200", "ms", 13107200},
    {"motion.pulse.s1", "1", "ratio", 65536},
    {"motion.pulse.t2", "400", "ms", 26214400},
    {"motion.pulse.s2", "1.4", "ratio", 91750},
    {"motion.pulse.t3", "600", "ms", 39321600},
    {"motion.pulse.s3", "0.8", "ratio", 52429},
    {"motion.pulse.t4", "800", "ms", 52428800},
    {"motion.pulse.s4", "1", "ratio", 65536},
    {"motion.volumeShow", "200", "ms", 13107200},
    {"motion.volumePop", "400", "ms", 26214400},
    {"motion.volumeHide", "1500", "ms", 98304000},
    {"motion.volumeFade", "380", "ms", 24903680},
    {"motion.volumeOpen", "260", "ms", 17039360},
    {"motion.volumeGroup", "298", "ms", 19529728},
    {"motion.volumeGroupLead", "19", "ms", 1245184},
    {"motion.volumeGroupScale", "0.8", "ratio", 52429},
    {"motion.volumeGroupFade", "260", "ms", 17039360},
    {"motion.volumeNumberAt", "11", "ms", 720896},
    {"motion.volumeNumberIn", "238", "ms", 15597568},
    {"motion.volumeExitAt", "78", "ms", 5111808},
    {"motion.volumeExitIn", "163", "ms", 10682368},
    {"motion.volumeTextAt", "130", "ms", 8519680},
    {"motion.volumeTextSlide", "151", "ms", 9895936},
    {"motion.volumeTextShift", "19", "u", 1245184},
    {"motion.volumeTextFadeAt", "109", "ms", 7143424},
    {"motion.volumeTextFadeIn", "151", "ms", 9895936},
    {"motion.volumeOpenEnd", "282", "ms", 18481152},
    {"motion.volumeCloseSlowK", "0.98", "ratio", 64225},
    {"motion.volumeCloseK", "0.96", "ratio", 62915},
    {"motion.volumeCloseSize", "0.92", "ratio", 60293},
    {"motion.volumeNumberHold", "700", "ms", 45875200},
    {"motion.volumeNumberFade", "520", "ms", 34078720},
    {"motion.volumeNumberBack", "365", "ms", 23920640},
    {"motion.volumeRestHold", "2094", "ms", 137232384},
    {"motion.volumeRestSlow", "650", "ms", 42598400},
    {"motion.volumeRestLevel", "0.36", "ratio", 23593},
    {"motion.volumeRestFast", "104", "ms", 6815744},
    {"motion.volumeScrimOut", "267", "ms", 17498112},
    {"motion.volumeTouchClose", "85", "ms", 5570560},
    {"motion.volumeCrossOut", "140", "ms", 9175040},
    {"motion.volumeCrossIn", "180", "ms", 11796480},
    {"motion.volumePressOff", "100", "ms", 6553600},
    {"motion.volumeTiltAngle", "0", "deg", 0},
    {"motion.volumeTiltDepth", "0", "u", 0},
    {"motion.volumeTiltHold", "200", "ms", 13107200},
    {"motion.volumeTiltReturn", "100", "ms", 6553600},
    {"motion.toastIn", "350", "ms", 22937600},
    {"motion.toastHold", "3500", "ms", 229376000},
    {"motion.toastOut", "250", "ms", 16384000},
    {"motion.toastTravel", "40", "u", 2621440},
    {"motion.toastRise", "100", "ms", 6553600},
    {"motion.lyricSlide", "350", "ms", 22937600},
    {"motion.listChange", "220", "ms", 14417920},
    {"motion.listChangeStagger", "50", "ms", 3276800},
    {"motion.listChangeCap", "10", "count", 655360},
    {"motion.listChangeShift", "-5", "u", -327680},
    {"motion.saverWordDelay", "645", "ms", 42270720},
    {"motion.saverWordEntry", "450", "ms", 29491200},
    {"motion.saverWordEntryShift", "88.9", "u", 5826150},
    {"motion.saverWordFadeIn", "170", "ms", 11141120},
    {"saverGiant", "285.3", "u", 18697421},
    {"saverGiantDigits", "184", "u", 12058624},
    {"saverGiantCjk", "181.3", "u", 11881677},
    {"saverLarge", "120.9", "u", 7923302},
    {"saverLargeBlack", "123.6", "u", 8100250},
    {"saverLargeCjk", "69.3", "u", 4541645},
    {"saverDigits", "74.7", "u", 4895539},
    {"saverRow", "50.2", "u", 3289907},
    {"saverRowE", "50.7", "u", 3322675},
    {"saverSmall", "25.3", "u", 1658061},
    {"saverSmallE", "26.3", "u", 1723597},
    {"saverB1Size", "227.6", "u", 14915994},
    {"saverF2Size", "118", "u", 7733248},
    {"saverS1Size", "44.4", "u", 2909798},
    {"saverS4Size", "105.5", "u", 6914048},
    {"saverS5Size", "57.2", "u", 3748659},
    {"saverTileA5", "127.1", "u", 8329626},
    {"saverTileB5", "125.3", "u", 8211661},
    {"saverTileB7", "126.2", "u", 8270643},
    {"saverTileD5", "133.3", "u", 8735949},
    {"saverTileD6", "126.2", "u", 8270643},
    {"saverTileE5", "54.7", "u", 3584819},
    {"saverTileE5Tall", "55.2", "u", 3617587},
    {"saverTileF4", "91.6", "u", 6003098},
    {"saverTileH1", "218.2", "u", 14299955},
    {"saverTileH7", "135.6", "u", 8886682},
    {"saverTileS3", "57.8", "u", 3787981},
    {"saverTileS", "54.7", "u", 3584819},
    {"saverTileM", "91.6", "u", 6003098},
    {"saverTileL", "126.7", "u", 8303411},
    {"saverTileK3", "219.6", "u", 14391706},
    {"saverTileK6", "83.6", "u", 5478810},
    {"saverTileP4", "100.4", "u", 6579814},
    {"saverTileR4", "175.1", "u", 11475354},
    {"saverTileS6", "53.3", "u", 3493069},
    {"saverR1Size", "77.4", "u", 5072486},
    {"saverR3Size", "100.4", "u", 6579814},
    {"motion.saverA1Speed", "-25", "u_per_s", -1638400},
    {"motion.saverA1In", "0", "s", 0},
    {"motion.saverA2Speed", "19", "u_per_s", 1245184},
    {"motion.saverA2In", "0", "s", 0},
    {"motion.saverA3Speed", "-29.8", "u_per_s", -1952973},
    {"motion.saverA3In", "5.24", "s", 343409},
    {"motion.saverA4Speed", "-11", "u_per_s", -720896},
    {"motion.saverA4In", "42.24", "s", 2768241},
    {"motion.saverA5Speed", "-19.3", "u_per_s", -1264845},
    {"motion.saverA5In", "35.24", "s", 2309489},
    {"motion.saverA6Speed", "8.6", "u_per_s", 563610},
    {"motion.saverA6In", "58.24", "s", 3816817},
    {"motion.saverB1Speed", "21.4", "u_per_s", 1402470},
    {"motion.saverB1In", "1.74", "s", 114033},
    {"motion.saverB2Speed", "-21", "u_per_s", -1376256},
    {"motion.saverB2In", "6.74", "s", 441713},
    {"motion.saverB3Speed", "28.6", "u_per_s", 1874330},
    {"motion.saverB3In", "57.74", "s", 3784049},
    {"motion.saverB4Speed", "-18.9", "u_per_s", -1238630},
    {"motion.saverB4In", "28.74", "s", 1883505},
    {"motion.saverB5Speed", "-22.8", "u_per_s", -1494221},
    {"motion.saverB5In", "48.74", "s", 3194225},
    {"motion.saverB6Speed", "14.2", "u_per_s", 930611},
    {"motion.saverB6In", "11.74", "s", 769393},
    {"motion.saverB7Speed", "45.8", "u_per_s", 3001549},
    {"motion.saverB7In", "0", "s", 0},
    {"motion.saverD1Speed", "-30.8", "u_per_s", -2018509},
    {"motion.saverD1In", "0", "s", 0},
    {"motion.saverD2Speed", "-39.6", "u_per_s", -2595226},
    {"motion.saverD2In", "31.74", "s", 2080113},
    {"motion.saverD3Speed", "35.3", "u_per_s", 2313421},
    {"motion.saverD3In", "18.74", "s", 1228145},
    {"motion.saverD4Speed", "26.8", "u_per_s", 1756365},
    {"motion.saverD4In", "40.74", "s", 2669937},
    {"motion.saverD5Speed", "26.4", "u_per_s", 1730150},
    {"motion.saverD5In", "42.74", "s", 2801009},
    {"motion.saverD6Speed", "28.1", "u_per_s", 1841562},
    {"motion.saverD6In", "13.74", "s", 900465},
    {"motion.saverE1Speed", "-37.9", "u_per_s", -2483814},
    {"motion.saverE1In", "0", "s", 0},
    {"motion.saverE2Speed", "37.1", "u_per_s", 2431386},
    {"motion.saverE2In", "1.96", "s", 128451},
    {"motion.saverE3Speed", "15.6", "u_per_s", 1022362},
    {"motion.saverE3In", "4.64", "s", 304087},
    {"motion.saverE4Speed", "-25.5", "u_per_s", -1671168},
    {"motion.saverE4In", "12.64", "s", 828375},
    {"motion.saverE5Speed", "-20", "u_per_s", -1310720},
    {"motion.saverE5In", "38.24", "s", 2506097},
    {"motion.saverE5On", "38.39", "s", 2515927},
    {"motion.saverE5Off", "52.92", "s", 3468165},
    {"motion.saverF1Speed", "33.5", "u_per_s", 2195456},
    {"motion.saverF1In", "0", "s", 0},
    {"motion.saverF2Speed", "-25.4", "u_per_s", -1664614},
    {"motion.saverF2In", "0", "s", 0},
    {"motion.saverF3Speed", "-18.7", "u_per_s", -1225523},
    {"motion.saverF3In", "37.64", "s", 2466775},
    {"motion.saverF4Speed", "22.6", "u_per_s", 1481114},
    {"motion.saverF4In", "31.64", "s", 2073559},
    {"motion.saverF5Speed", "30.9", "u_per_s", 2025062},
    {"motion.saverF5In", "38.64", "s", 2532311},
    {"motion.saverH1Speed", "-29.5", "u_per_s", -1933312},
    {"motion.saverH1In", "0", "s", 0},
    {"motion.saverH2Speed", "-38.5", "u_per_s", -2523136},
    {"motion.saverH2In", "0", "s", 0},
    {"motion.saverH3Speed", "-31.5", "u_per_s", -2064384},
    {"motion.saverH3In", "9.34", "s", 612106},
    {"motion.saverH4Speed", "-37.9", "u_per_s", -2483814},
    {"motion.saverH4In", "1.34", "s", 87818},
    {"motion.saverH5Speed", "-45.6", "u_per_s", -2988442},
    {"motion.saverH5In", "34.34", "s", 2250506},
    {"motion.saverH6Speed", "29", "u_per_s", 1900544},
    {"motion.saverH6In", "13.34", "s", 874250},
    {"motion.saverH7Speed", "14.3", "u_per_s", 937165},
    {"motion.saverH7In", "36.34", "s", 2381578},
    {"motion.saverH8Speed", "-31.5", "u_per_s", -2064384},
    {"motion.saverH8In", "38.34", "s", 2512650},
    {"motion.saverS1Speed", "42.0", "u_per_s", 2752512},
    {"motion.saverS1In", "0", "s", 0},
    {"motion.saverS2Speed", "-45.1", "u_per_s", -2955674},
    {"motion.saverS2In", "0", "s", 0},
    {"motion.saverS3Speed", "39.2", "u_per_s", 2569011},
    {"motion.saverS3In", "10.9", "s", 714342},
    {"motion.saverS4Speed", "-50.5", "u_per_s", -3309568},
    {"motion.saverS4In", "11.4", "s", 747110},
    {"motion.saverS5Speed", "-44.1", "u_per_s", -2890138},
    {"motion.saverS5In", "19.4", "s", 1271398},
    {"motion.saverS6Speed", "-14.3", "u_per_s", -937165},
    {"motion.saverS6In", "34.4", "s", 2254438},
    {"motion.saverS7Speed", "23.9", "u_per_s", 1566310},
    {"motion.saverS7In", "28.9", "s", 1893990},
    {"motion.saverK1Speed", "-35.8", "u_per_s", -2346189},
    {"motion.saverK1In", "0.30", "s", 19661},
    {"motion.saverK2Speed", "25.7", "u_per_s", 1684275},
    {"motion.saverK2In", "0", "s", 0},
    {"motion.saverK3Speed", "-24.5", "u_per_s", -1605632},
    {"motion.saverK3In", "1.30", "s", 85197},
    {"motion.saverK4Speed", "-21.6", "u_per_s", -1415578},
    {"motion.saverK4In", "2.30", "s", 150733},
    {"motion.saverK5Speed", "-50.3", "u_per_s", -3296461},
    {"motion.saverK5In", "38.80", "s", 2542797},
    {"motion.saverK6Speed", "18.4", "u_per_s", 1205862},
    {"motion.saverK6In", "55.80", "s", 3656909},
    {"motion.saverP1Speed", "-45.0", "u_per_s", -2949120},
    {"motion.saverP1In", "0", "s", 0},
    {"motion.saverP2Speed", "48.8", "u_per_s", 3198157},
    {"motion.saverP2In", "6.70", "s", 439091},
    {"motion.saverP3Speed", "-26.0", "u_per_s", -1703936},
    {"motion.saverP3In", "16.70", "s", 1094451},
    {"motion.saverP4Speed", "-22.9", "u_per_s", -1500774},
    {"motion.saverP4In", "27.20", "s", 1782579},
    {"motion.saverP5Speed", "39.9", "u_per_s", 2614886},
    {"motion.saverP5In", "33.20", "s", 2175795},
    {"motion.saverR1Speed", "-33.2", "u_per_s", -2175795},
    {"motion.saverR1In", "0", "s", 0},
    {"motion.saverR2Speed", "-48.9", "u_per_s", -3204710},
    {"motion.saverR2In", "0", "s", 0},
    {"motion.saverR3Speed", "-27.5", "u_per_s", -1802240},
    {"motion.saverR3In", "2.20", "s", 144179},
    {"motion.saverR4Speed", "-16.0", "u_per_s", -1048576},
    {"motion.saverR4In", "9.20", "s", 602931},
    {"motion.saverJ1Speed", "24.8", "u_per_s", 1625293},
    {"motion.saverJ1In", "0.00", "s", 0},
    {"motion.saverJ2Speed", "-29.3", "u_per_s", -1920205},
    {"motion.saverJ2In", "21.47", "s", 1407058},
    {"motion.saverJ3Speed", "-34.7", "u_per_s", -2274099},
    {"motion.saverJ3In", "6.75", "s", 442368},
    {"motion.saverJ4Speed", "26.7", "u_per_s", 1749811},
    {"motion.saverJ4In", "14.15", "s", 927334},
    {"motion.saverJ5Speed", "29.9", "u_per_s", 1959526},
    {"motion.saverJ5In", "20.47", "s", 1341522},
    {"motion.saverJ6Speed", "17.2", "u_per_s", 1127219},
    {"motion.saverJ6In", "33.73", "s", 2210529},
    {"motion.saverL1Speed", "20.2", "u_per_s", 1323827},
    {"motion.saverL1In", "0.00", "s", 0},
    {"motion.saverL2Speed", "-21.5", "u_per_s", -1409024},
    {"motion.saverL2In", "30.57", "s", 2003436},
    {"motion.saverL3Speed", "-15.5", "u_per_s", -1015808},
    {"motion.saverL3In", "6.59", "s", 431882},
    {"motion.saverL4Speed", "22.6", "u_per_s", 1481114},
    {"motion.saverL4In", "15.85", "s", 1038746},
    {"motion.saverL5Speed", "32.6", "u_per_s", 2136474},
    {"motion.saverL5In", "12.63", "s", 827720},
    {"motion.saverM1Speed", "-40.4", "u_per_s", -2647654},
    {"motion.saverM1In", "0.00", "s", 0},
    {"motion.saverM2Speed", "34.9", "u_per_s", 2287206},
    {"motion.saverM2In", "39.67", "s", 2599813},
    {"motion.saverM3Speed", "-42.2", "u_per_s", -2765619},
    {"motion.saverM3In", "13.74", "s", 900465},
    {"motion.saverM4Speed", "38.8", "u_per_s", 2542797},
    {"motion.saverM4In", "29.05", "s", 1903821},
    {"motion.saverM5Speed", "-23.1", "u_per_s", -1513882},
    {"motion.saverM5In", "30.58", "s", 2004091},
    {"motion.saverM6Speed", "-38.5", "u_per_s", -2523136},
    {"motion.saverM6In", "49.91", "s", 3270902},
    {"motion.saverN1Speed", "-36.9", "u_per_s", -2418278},
    {"motion.saverN1In", "0.00", "s", 0},
    {"motion.saverN2Speed", "27.9", "u_per_s", 1828454},
    {"motion.saverN2In", "10.22", "s", 669778},
    {"motion.saverN3Speed", "-24.4", "u_per_s", -1599078},
    {"motion.saverN3In", "15.17", "s", 994181},
    {"motion.saverN4Speed", "29.0", "u_per_s", 1900544},
    {"motion.saverN4In", "19.48", "s", 1276641},
    {"motion.saverN5Speed", "39.2", "u_per_s", 2569011},
    {"motion.saverN5In", "34.04", "s", 2230845},
    {"motion.saverN6Speed", "-47.8", "u_per_s", -3132621},
    {"motion.saverN6In", "37.84", "s", 2479882},
    {"motion.saverQ1Speed", "-42.6", "u_per_s", -2791834},
    {"motion.saverQ1In", "0.00", "s", 0},
    {"motion.saverQ2Speed", "-32.2", "u_per_s", -2110259},
    {"motion.saverQ2In", "19.32", "s", 1266156},
    {"motion.saverQ3Speed", "33.3", "u_per_s", 2182349},
    {"motion.saverQ3In", "31.43", "s", 2059796},
    {"motion.saverQ4Speed", "19.4", "u_per_s", 1271398},
    {"motion.saverQ4In", "33.53", "s", 2197422},
    {"motion.saverQ5Speed", "-43.6", "u_per_s", -2857370},
    {"motion.saverQ5In", "4.60", "s", 301466},
    {"motion.saverQ6Speed", "18.9", "u_per_s", 1238630},
    {"motion.saverQ6In", "46.92", "s", 3074949},
    {"motion.saverQ7Speed", "34.9", "u_per_s", 2287206},
    {"motion.saverQ7In", "58.00", "s", 3801088},
    {"motion.saverALength", "70.6", "s", 4626842},
    {"motion.saverBLength", "73.17", "s", 4795269},
    {"motion.saverDLength", "65.27", "s", 4277535},
    {"motion.saverELength", "82.97", "s", 5437522},
    {"motion.saverFLength", "66.07", "s", 4329964},
    {"motion.saverHLength", "66.37", "s", 4349624},
    {"motion.saverSLength", "64.8", "s", 4246733},
    {"motion.saverKLength", "83.4", "s", 5465702},
    {"motion.saverPLength", "66.1", "s", 4331930},
    {"motion.saverRLength", "49.2", "s", 3224371},
    {"motion.saverJLength", "66.30", "s", 4345037},
    {"motion.saverLLength", "53.24", "s", 3489137},
    {"motion.saverMLength", "74.37", "s", 4873912},
    {"motion.saverNLength", "61.31", "s", 4018012},
    {"motion.saverQLength", "82.45", "s", 5403443},
    {"motion.saverH8Stop", "48.06", "s", 3149660},
    {"motion.saverA3Stop", "26.82", "s", 1757676},
    {"motion.saverS5Stop", "37.99", "s", 2489713},
    {"motion.saverK4Stop", "51.72", "s", 3389522},
    {"motion.saverP2Stop", "45.07", "s", 2953708},
    {"motion.saverR2Stop", "33.11", "s", 2169897},
    {"saverD1Join", "863.7", "u", 56603443},
    {"saverB4Period", "641.6", "u", 42047898},
    {"saverE4Period", "1080.9", "u", 70837862},
    {"saverS4Period", "1447.6", "u", 94869914},
    {"saverE2Period", "1575.6", "u", 103258522},
    {"saverF1Gap", "373.3", "u", 24464589},
    {"saverF2Gap", "314.7", "u", 20624179},
    {"saverF1Trail", "-1181.7", "u", -77443891},
    {"saverF2Trail", "920.5", "u", 60325888},
    {"motion.saverWhoosh", "285", "ms", 18677760},
    {"motion.saverWhooshSpeed", "773.3", "u_per_s", 50678989},
    {"motion.saverPWhooshSpeed", "1438.2", "u_per_s", 94253875},
    {"motion.saverGapMin", "220", "ms", 14417920},
    {"motion.saverGapMax", "600", "ms", 39321600},
    {"motion.saverTileIn", "240", "ms", 15728640},
    {"motion.saverE1Entry", "284", "ms", 18612224},
    {"saverE1Shift", "20.1", "u", 1317274},
    {"motion.saverA1Entry", "400", "ms", 26214400},
    {"saverA1Shift", "118.0", "u", 7733248},
    {"motion.saverF1Entry", "245", "ms", 16056320},
    {"saverF1Shift", "266.2", "u", 17445683},
    {"motion.saverF2Entry", "245", "ms", 16056320},
    {"saverF2Shift", "357.8", "u", 23448781},
    {"motion.saverK1Entry", "334", "ms", 21889024},
    {"saverK1Shift", "77.9", "u", 5105254},
    {"motion.saverP1Entry", "184", "ms", 12058624},
    {"saverP1Shift", "53.4", "u", 3499622},
    {"motion.saverR1Entry", "284", "ms", 18612224},
    {"saverR1Shift", "25.4", "u", 1664614},
    {"motion.saverFFade", "250", "ms", 16384000},
    {"motion.saverNpIn", "250", "ms", 16384000},
    {"motion.saverNpContentIn", "230", "ms", 15073280},
    {"motion.saverNpBackDelay", "34", "ms", 2228224},
    {"motion.saverNpRingsDelay", "50", "ms", 3276800},
    {"motion.saverNpHold", "5468", "ms", 358350848},
    {"motion.saverNpOut", "210", "ms", 13762560},
    {"motion.saverNpRingsOut", "150", "ms", 9830400},
    {"motion.saverNpBlack", "350", "ms", 22937600},
    {"motion.saverFade", "250", "ms", 16384000},
    {"motion.saverFadeBack", "21", "ms", 1376256},
    {"motion.saverFadeRings", "38", "ms", 2490368},
    {"motion.saverFadeTop", "190", "ms", 12451840},
    {"motion.saverFadeTopShift", "16.57", "u", 1085932},
    {"motion.saverFadeForm", "staged", "mode", 0},
    {"motion.saverKeyWindow", "1500", "ms", 98304000},
    {"motion.saverLyrSpeedMin", "11.04", "u_per_s", 723517},
    {"motion.saverLyrSpeedMax", "48.89", "u_per_s", 3204055},
    {"motion.saverLyrSlide", "1564.44", "u_per_s", 102527140},
    {"motion.saverLyrSlideMin", "150", "ms", 9830400},
    {"motion.saverLyrLead", "40", "ms", 2621440},
    {"motion.saverLyrLook", "1000", "ms", 65536000},
    {"motion.saverLyrRead", "6000", "ms", 393216000},
    {"motion.saverLyrBreak", "2220", "ms", 145489920},
    {"motion.saverLyrScene", "6200", "ms", 406323200},
    {"motion.saverLyrSectionMin", "49.2", "s", 3224371},
    {"motion.saverLyrSectionMax", "83.4", "s", 5465702},
    {"motion.saverLyrGrace", "1000", "ms", 65536000},
    {"motion.saverLyrSlew", "30", "ms", 1966080},
    {"motion.saverLyrSeek", "250", "ms", 16384000},
    {"saverLyrGap", "1.78", "u", 116654},
    {"saverLyrSizeSum", "979.33", "u", 64181371},
    {"saverLyrSizeSumUp", "1335.33", "u", 87512187},
    {"motion.saverLyrPoolGiantDiagLo", "18.75", "u_per_s", 1228800},
    {"motion.saverLyrPoolGiantDiagHi", "30.84", "u_per_s", 2021130},
    {"motion.saverLyrPoolGiantUpLo", "35.83", "u_per_s", 2348155},
    {"motion.saverLyrPoolGiantUpHi", "45.00", "u_per_s", 2949120},
    {"motion.saverLyrPoolRowDiagLo", "19.03", "u_per_s", 1247150},
    {"motion.saverLyrPoolRowDiagHi", "39.58", "u_per_s", 2593915},
    {"motion.saverLyrPoolRowUpLo", "25.69", "u_per_s", 1683620},
    {"motion.saverLyrPoolRowUpHi", "44.08", "u_per_s", 2888827},
    {"motion.saverLyrPoolLargeLo", "18.87", "u_per_s", 1236664},
    {"motion.saverLyrPoolLargeHi", "48.89", "u_per_s", 3204055},
    {"motion.saverLyrPoolSmallLo", "11.04", "u_per_s", 723517},
    {"motion.saverLyrPoolSmallHi", "48.82", "u_per_s", 3199468},
    {"motion.npWakeZoom", "zoom", "mode", 0},
    {"motion.npWake", "410", "ms", 26869760},
    {"motion.npWakeScale", "1.448", "ratio", 94896},
    {"motion.npWakeTitleScale", "1.576", "ratio", 103285},
    {"motion.npWakeContentDelay", "84", "ms", 5505024},
    {"motion.npWakeContent", "340", "ms", 22282240},
    {"motion.npWakeNextDelay", "134", "ms", 8781824},
    {"motion.npWakeBackDelay", "158", "ms", 10354688},
    {"motion.npWakeBack", "210", "ms", 13762560},
    {"motion.npWakeRingsDelay", "168", "ms", 11010048},
    {"motion.npWakeRings", "215", "ms", 14090240},
    {"motion.npWakeLight", "150", "ms", 9830400},
    {"npWakeTitleX", "64.9", "u", 4253286},
    {"npWakeTitleDrop", "107", "u", 7012352},
    {"saver.aheadMs", "11.5", "ms", 753664},
    {"saverA1Across", "130", "u", 8519680},
    {"saverA2Across", "167.2", "u", 10957619},
    {"saverA3Across", "186.8", "u", 12242125},
    {"saverA4Across", "204.5", "u", 13402112},
    {"saverA5Across", "-87.9", "u", -5760614},
    {"saverA6Across", "-59.1", "u", -3873178},
    {"saverB1Across", "212.6", "u", 13932954},
    {"saverB2Across", "56.2", "u", 3683123},
    {"saverB3Across", "17.1", "u", 1120666},
    {"saverB4Across", "-120.7", "u", -7910195},
    {"saverB5Across", "0.2", "u", 13107},
    {"saverB6Across", "-133.2", "u", -8729395},
    {"saverB7Across", "224.1", "u", 14686618},
    {"saverD1Across", "109.5", "u", 7176192},
    {"saverD2Across", "-32.4", "u", -2123366},
    {"saverD3Across", "-16.7", "u", -1094451},
    {"saverD4Across", "-63.6", "u", -4168090},
    {"saverD5Across", "235.7", "u", 15446835},
    {"saverD6Across", "208.1", "u", 13638042},
    {"saverE1Across", "308.4", "u", 20211302},
    {"saverE2Across", "345.8", "u", 22662349},
    {"saverE3Across", "108", "u", 7077888},
    {"saverE5Across", "90.6", "u", 5937562},
    {"saverF1Across", "-36.3", "u", -2378957},
    {"saverF2Across", "55.3", "u", 3624141},
    {"saverF3Across", "206.4", "u", 13526630},
    {"saverF4Across", "129.9", "u", 8513126},
    {"saverF5Across", "67.1", "u", 4397466},
    {"saverH1Across", "324", "u", 21233664},
    {"saverH2Across", "306.7", "u", 20099891},
    {"saverH3Across", "48", "u", 3145728},
    {"saverH4Across", "320.0", "u", 20971520},
    {"saverH5Across", "280.9", "u", 18409062},
    {"saverH6Across", "321.8", "u", 21089485},
    {"saverH7Across", "178.7", "u", 11711283},
    {"saverH8Across", "196.4", "u", 12871270},
    {"saverS1Across", "2.7", "u", 176947},
    {"saverS2Across", "40", "u", 2621440},
    {"saverS3Across", "235.6", "u", 15440282},
    {"saverS4Across", "402.7", "u", 26391347},
    {"saverS5Across", "45.3", "u", 2968781},
    {"saverS6Across", "266.7", "u", 17478451},
    {"saverS7Across", "301.3", "u", 19745997},
    {"saverK1Across", "311.1", "u", 20388250},
    {"saverK2Across", "320.9", "u", 21030502},
    {"saverK3Across", "290.7", "u", 19051315},
    {"saverK4Across", "293.3", "u", 19221709},
    {"saverK5Across", "67.6", "u", 4430234},
    {"saverK6Across", "186.7", "u", 12235571},
    {"saverP1Across", "233.8", "u", 15322317},
    {"saverP2Across", "212.4", "u", 13919846},
    {"saverP3Across", "197.3", "u", 12930253},
    {"saverP4Across", "144.0", "u", 9437184},
    {"saverP5Across", "42.7", "u", 2798387},
    {"saverR1Across", "215.1", "u", 14096794},
    {"saverR2Across", "268.4", "u", 17589862},
    {"saverR3Across", "332.4", "u", 21784166},
    {"saverR4Across", "489.8", "u", 32099533},
    {"saverJ1Across", "212.9", "u", 13952614},
    {"saverJ2Across", "57.9", "u", 3794534},
    {"saverJ3Across", "12.3", "u", 806093},
    {"saverJ4Across", "-68.1", "u", -4463002},
    {"saverJ5Across", "26.8", "u", 1756365},
    {"saverJ6Across", "129.9", "u", 8513126},
    {"saverL1Across", "95.1", "u", 6232474},
    {"saverL2Across", "126.1", "u", 8264090},
    {"saverL3Across", "-30.6", "u", -2005402},
    {"saverL4Across", "-45.2", "u", -2962227},
    {"saverL5Across", "129.9", "u", 8513126},
    {"saverM1Across", "273.3", "u", 17910989},
    {"saverM2Across", "304.8", "u", 19975373},
    {"saverM3Across", "386.9", "u", 25355878},
    {"saverM4Across", "147.6", "u", 9673114},
    {"saverM5Across", "133.1", "u", 8722842},
    {"saverM6Across", "133.1", "u", 8722842},
    {"saverN1Across", "308.4", "u", 20211302},
    {"saverN2Across", "339.9", "u", 22275686},
    {"saverN3Across", "354.5", "u", 23232512},
    {"saverN4Across", "112.5", "u", 7372800},
    {"saverN5Across", "2.7", "u", 176947},
    {"saverN6Across", "40.0", "u", 2621440},
    {"saverQ1Across", "404.4", "u", 26502758},
    {"saverQ2Across", "156.2", "u", 10236723},
    {"saverQ3Across", "124.7", "u", 8172339},
    {"saverQ4Across", "278.8", "u", 18271437},
    {"saverQ5Across", "2.7", "u", 176947},
    {"saverQ6Across", "40.0", "u", 2621440},
    {"saverQ7Across", "278.8", "u", 18271437},
    {"motion.backdropFade", "2000", "ms", 131072000},
    {"motion.lockCommit", "510", "ms", 33423360},
    {"motion.lockSliverDelay", "232", "ms", 15204352},
    {"motion.lockSliver", "270", "ms", 17694720},
    {"motion.lockSliverAlpha", "0.08", "alpha", 5243},
    {"motion.decel.x1", "0.1", "ratio", 6554},
    {"motion.decel.y1", "0.9", "ratio", 58982},
    {"motion.decel.x2", "0.2", "ratio", 13107},
    {"motion.decel.y2", "1.0", "ratio", 65536},
    {"motion.strip.x1", "0.2", "ratio", 13107},
    {"motion.strip.y1", "0.8", "ratio", 52429},
    {"motion.strip.x2", "0.2", "ratio", 13107},
    {"motion.strip.y2", "1", "ratio", 65536},
    {"input.keyHoldMs", "400", "ms", 26214400},
    {"input.volumeHoldMs", "400", "ms", 26214400},
    {"input.volumeRepeatMs", "100", "ms", 6553600},
    {"input.remoteDoubleMs", "350", "ms", 22937600},
    {"input.remoteHoldMs", "500", "ms", 32768000},
    {"input.knobDoubleMs", "350", "ms", 22937600},
    {"input.knobHoldMs", "500", "ms", 32768000},
    {"input.knobHoldLongMs", "2000", "ms", 131072000},
    {"input.modeExitHoldMs", "2000", "ms", 131072000},
    {"input.searchDebounceMs", "150", "ms", 9830400},
    {"power.screenOffDefaultS", "300", "s", 19660800},
    {"history.rows", "100", "count", 6553600},
    {"history.tableCap", "2000", "count", 131072000},
    {"quickplay.tiles", "8", "count", 524288},
    {"bt.pairingS", "120", "s", 7864320},
    {"bt.discoverS", "120", "s", 7864320},
    {"btNameDrop", "16", "u", 1048576},
    {"btStateDrop", "36", "u", 2359296},
    {"btRowDrop", "60", "u", 3932160},
    {"btRingDrop", "90", "u", 5898240},
    {"eq.preampMin", "-24", "db", -1572864},
    {"eq.preampMax", "0", "db", 0},
    {"eq.gainMin", "-24", "db", -1572864},
    {"eq.gainMax", "12", "db", 786432},
    {"eq.qMin", "0.1", "ratio", 6554},
    {"eq.qMax", "10", "ratio", 655360},
    {"eq.freqMin", "20", "hz", 1310720},
    {"eq.freqMax", "20000", "hz", 1310720000},
    {"eq.freqPoints", "61", "count", 3997696},
    {"eq.bands", "10", "count", 655360},
    {"eq.profilesMax", "200", "count", 13107200},
    {"eq.importsMax", "200", "count", 13107200},
    {"settings.backupsMax", "200", "count", 13107200},
    {"settings.backupMaxKib", "2048", "kib", 134217728},
    {"eq.fileMaxKib", "64", "kib", 4194304},
    {"eq.importMs", "2500", "ms", 163840000},
    {"volume.stepDefault", "5", "pct", 327680},
    {"settings.volumeSteps", "2,5,10", "pct", 0},
    {"settings.saverSteps", "0,10,15,30,60,120,300", "s", 0},
    {"settings.screenOffSteps", "0,10,30,60,120,300,600,1800", "s", 0},
    {"settings.sleepSteps", "0,15,30,45,60,90,120", "min", 0},
    {"settings.idleSteps", "0,5,10,30,60", "min", 0},
    {"settings.saverStart", "5", "s", 327680},
    {"settings.screenOffDefault", "300", "s", 19660800},
    {"settings.idleDefault", "30", "min", 1966080},
    {"settings.writeDelay", "500", "ms", 32768000},
    {"settings.yearMin", "2000", "year", 131072000},
    {"settings.yearMax", "2099", "year", 137560064},
    {"settings.clockYearMin", "2025", "year", 132710400},
    {"update.batteryMin", "50", "pct", 3276800},
    {"volume.percentDbDivisor", "60", "db", 3932160},
    {"settings.filterCount", "4", "count", 262144},
    {"settings.balanceMax", "10", "db", 655360},
    {"settings.balanceStep", "0.5", "db", 32768},
    {"settings.maxVolumeSteps", "5,10,15,20,25,30,35,40,45,50,55,60,65,70,75,80,85,90,95,100", "pct", 0},
    {"settings.maxVolumeDefault", "100", "pct", 6553600},
    {"settings.powerOnSteps", "0,1,5,10,15,20,25,30,35,40,45,50,55,60,65,70,75,80,85,90,95,100", "pct", 0},
    {"settings.crossfadeSteps", "0,1,2,3,5", "s", 0},
    {"settings.crossfadeDefault", "0", "s", 0},
    {"settings.brightnessSteps", "10,15,20,25,30,35,40,45,50,55,60,65,70,75,80,85,90,95,100", "pct", 0},
    {"settings.brightnessDefault", "35", "pct", 2293760},
    {"homeRingX", "25.7", "u", 1684275},
    {"homeRingY", "91.6", "u", 6003098},
    {"homeRingSize", "35", "u", 2293760},
    {"homeRingStroke", "4", "u", 262144},
    {"homeRingHitBottom", "115.9", "u", 7595622},
    {"homeRingPressDim", "0.27", "alpha", 17695},
    {"npSeekBandX0", "6.1", "u", 399770},
    {"npSeekBandX1", "174.8", "u", 11455693},
    {"npSeekBandBottom", "318.49", "u", 20872561},
    {"npMarkHit", "54", "u", 3538944},
    {"npLyricPitch", "31.2", "u", 2044723},
    {"npLyricBottom", "362.66667", "u", 23767723},
    {"lockLyricBaseline", "330.66667", "u", 21670571},
    {"size.largeScale", "120", "pct", 7864320},
    {"large.type.npTitle.size", "32", "u", 2097152},
    {"large.type.npNext.size", "16", "u", 1048576},
    {"large.type.npAlbum.size", "22", "u", 1441792},
    {"large.npLyricPitch", "38.53333", "u", 2525320},
    {"large.npNextPitch", "21.66667", "u", 1419947},
    {"large.npAlbumPitch", "26.33333", "u", 1725781},
    {"wifi.levelHigh", "-60", "dbm", -3932160},
    {"wifi.levelMid", "-70", "dbm", -4587520},
    {"gesture.swipe", "45", "u", 2949120},
    {"gesture.edge", "16", "u", 1048576},
    {"gesture.edgeRight", "22", "u", 1441792},
    {"gesture.bottom", "14", "u", 917504},
    {"homeWatermarkGap", "36", "u", 2359296},
    {"navH", "36", "u", 2359296},
    {"navGlyph", "16", "u", 1048576},
    {"adjustExit", "54", "u", 3538944},
    {"adjustColumn", "47", "u", 3080192},
    {"adjustNumberDrop", "0.35", "ratio", 22938},
    {"backdrop.coverSigma", "6", "u", 393216},
    {"backdrop.coverGrid", "0.25", "ratio", 16384},
    {"size.keptTextMax", "16.8", "u", 1101005},
    {"npNextDropLarge", "16.52", "u", 1082655},
    {"npNextLinePitchLarge", "14.16", "u", 927990},
    {"npNextCountLarge", "2", "count", 131072},
    {"npNextNavCountLarge", "1", "count", 65536},
    {"npTimeBaselineLarge", "17.79", "u", 1165885},
    {"npTimePairGap", "2", "u", 131072},
    {"tileCaptionDropLarge", "13.2", "u", 865075},
    {"tileCaptionPitchLarge", "13.2", "u", 865075},
    {"tileCaptionBandLarge", "28.8", "u", 1887437},
    {"tileCellHLarge", "93.8", "u", 6147277},
    {"tileRowPitchLarge", "105.8", "u", 6933709},
    {"qpHintBaseLarge", "13.44", "u", 880804},
    {"qpHintStepLarge", "12.0", "u", 786432},
    {"detailTitlePitchLarge", "18", "u", 1179648},
    {"detailSubPitchLarge", "15.6", "u", 1022362},
    {"color.light.bg", "#FFFFFF", "rgb", 0xFFFFFF},
    {"color.light.bgElevated", "#F8F8F8", "rgb", 0xF8F8F8},
    {"color.light.tile", "#F0F0F0", "rgb", 0xF0F0F0},
    {"color.light.tilePressed", "#E6E6E6", "rgb", 0xE6E6E6},
    {"color.light.border", "#E4E4E4", "rgb", 0xE4E4E4},
    {"color.light.ink", "#111111", "rgb", 0x111111},
    {"input.repeatDelayMs", "300", "ms", 19660800},
    {"input.repeatIntervalMs", "80", "ms", 5242880},
    {"turnedLineStart", "340", "u", 22282240},
    {"motion.swapOut", "50", "ms", 3276800},
    {"motion.swapShift", "34", "u", 2228224},
    {"motion.swapRows", "150", "ms", 9830400},
    {"motion.swapRowInShift", "25", "u", 1638400},
    {"motion.swapRowFade", "200", "ms", 13107200},
};
inline constexpr int kTokenCount = 1442;

inline constexpr int32_t u = (int32_t)kTokens[0].num;
inline constexpr int32_t canvasW = (int32_t)kTokens[1].num;
inline constexpr int32_t canvasH = (int32_t)kTokens[2].num;
inline constexpr int32_t minTextPx = (int32_t)kTokens[3].num;
inline constexpr int32_t capHeight = (int32_t)kTokens[4].num;
inline constexpr int32_t xHeight = (int32_t)kTokens[5].num;
inline constexpr int32_t tracking = (int32_t)kTokens[6].num;
inline constexpr int32_t trackingHeader = (int32_t)kTokens[7].num;
inline constexpr int32_t type_hubHeader_size = (int32_t)kTokens[8].num;
inline constexpr const char* type_hubHeader_style = kTokens[9].value;
inline constexpr const char* type_hubHeader_case = kTokens[10].value;
inline constexpr int32_t type_hubHeader_alpha = (int32_t)kTokens[11].num;
inline constexpr int32_t type_pageHeader_size = (int32_t)kTokens[12].num;
inline constexpr const char* type_pageHeader_style = kTokens[13].value;
inline constexpr const char* type_pageHeader_case = kTokens[14].value;
inline constexpr int32_t type_pageHeader_alpha = (int32_t)kTokens[15].num;
inline constexpr int32_t type_albumHeader_size = (int32_t)kTokens[16].num;
inline constexpr const char* type_albumHeader_style = kTokens[17].value;
inline constexpr const char* type_albumHeader_case = kTokens[18].value;
inline constexpr int32_t type_albumHeader_alpha = (int32_t)kTokens[19].num;
inline constexpr int32_t type_homeItem_size = (int32_t)kTokens[20].num;
inline constexpr const char* type_homeItem_style = kTokens[21].value;
inline constexpr const char* type_homeItem_case = kTokens[22].value;
inline constexpr int32_t type_homeItem_alpha = (int32_t)kTokens[23].num;
inline constexpr int32_t type_homeItem_alphaDim = (int32_t)kTokens[24].num;
inline constexpr int32_t type_homeSubtitle_size = (int32_t)kTokens[25].num;
inline constexpr const char* type_homeSubtitle_style = kTokens[26].value;
inline constexpr const char* type_homeSubtitle_case = kTokens[27].value;
inline constexpr int32_t type_homeSubtitle_alpha = (int32_t)kTokens[28].num;
inline constexpr int32_t type_quickplayHeader_size = (int32_t)kTokens[29].num;
inline constexpr const char* type_quickplayHeader_style = kTokens[30].value;
inline constexpr const char* type_quickplayHeader_case = kTokens[31].value;
inline constexpr int32_t type_quickplayHeader_alpha = (int32_t)kTokens[32].num;
inline constexpr int32_t type_quickplayHint_size = (int32_t)kTokens[33].num;
inline constexpr const char* type_quickplayHint_style = kTokens[34].value;
inline constexpr const char* type_quickplayHint_case = kTokens[35].value;
inline constexpr int32_t type_quickplayHint_alpha = (int32_t)kTokens[36].num;
inline constexpr int32_t type_qpHint_size = (int32_t)kTokens[37].num;
inline constexpr const char* type_qpHint_style = kTokens[38].value;
inline constexpr const char* type_qpHint_case = kTokens[39].value;
inline constexpr int32_t type_qpHint_alpha = (int32_t)kTokens[40].num;
inline constexpr int32_t type_pivotActive_size = (int32_t)kTokens[41].num;
inline constexpr const char* type_pivotActive_style = kTokens[42].value;
inline constexpr const char* type_pivotActive_case = kTokens[43].value;
inline constexpr int32_t type_pivotActive_alpha = (int32_t)kTokens[44].num;
inline constexpr int32_t type_pivotInactive_size = (int32_t)kTokens[45].num;
inline constexpr const char* type_pivotInactive_style = kTokens[46].value;
inline constexpr const char* type_pivotInactive_case = kTokens[47].value;
inline constexpr int32_t type_pivotInactive_alpha = (int32_t)kTokens[48].num;
inline constexpr int32_t type_sectionLabel_size = (int32_t)kTokens[49].num;
inline constexpr const char* type_sectionLabel_style = kTokens[50].value;
inline constexpr const char* type_sectionLabel_case = kTokens[51].value;
inline constexpr int32_t type_sectionLabel_alpha = (int32_t)kTokens[52].num;
inline constexpr int32_t type_listPrimary_size = (int32_t)kTokens[53].num;
inline constexpr const char* type_listPrimary_style = kTokens[54].value;
inline constexpr const char* type_listPrimary_case = kTokens[55].value;
inline constexpr int32_t type_listPrimary_alpha = (int32_t)kTokens[56].num;
inline constexpr int32_t type_listPrimary_alphaDim = (int32_t)kTokens[57].num;
inline constexpr int32_t type_listPlaylist_size = (int32_t)kTokens[58].num;
inline constexpr const char* type_listPlaylist_style = kTokens[59].value;
inline constexpr const char* type_listPlaylist_case = kTokens[60].value;
inline constexpr int32_t type_listPlaylist_alpha = (int32_t)kTokens[61].num;
inline constexpr int32_t type_listPlaylist_alphaDim = (int32_t)kTokens[62].num;
inline constexpr int32_t type_listSecondary_size = (int32_t)kTokens[63].num;
inline constexpr const char* type_listSecondary_style = kTokens[64].value;
inline constexpr const char* type_listSecondary_case = kTokens[65].value;
inline constexpr int32_t type_listSecondary_alpha = (int32_t)kTokens[66].num;
inline constexpr int32_t type_listCount_size = (int32_t)kTokens[67].num;
inline constexpr const char* type_listCount_style = kTokens[68].value;
inline constexpr const char* type_listCount_case = kTokens[69].value;
inline constexpr int32_t type_listCount_alpha = (int32_t)kTokens[70].num;
inline constexpr int32_t type_listCollection_size = (int32_t)kTokens[71].num;
inline constexpr const char* type_listCollection_style = kTokens[72].value;
inline constexpr const char* type_listCollection_case = kTokens[73].value;
inline constexpr int32_t type_listCollection_alpha = (int32_t)kTokens[74].num;
inline constexpr int32_t type_listLibSecondary_size = (int32_t)kTokens[75].num;
inline constexpr const char* type_listLibSecondary_style = kTokens[76].value;
inline constexpr const char* type_listLibSecondary_case = kTokens[77].value;
inline constexpr int32_t type_listLibSecondary_alpha = (int32_t)kTokens[78].num;
inline constexpr int32_t type_detailName_size = (int32_t)kTokens[79].num;
inline constexpr const char* type_detailName_style = kTokens[80].value;
inline constexpr const char* type_detailName_case = kTokens[81].value;
inline constexpr int32_t type_detailName_alpha = (int32_t)kTokens[82].num;
inline constexpr int32_t type_detailTitle_size = (int32_t)kTokens[83].num;
inline constexpr const char* type_detailTitle_style = kTokens[84].value;
inline constexpr const char* type_detailTitle_case = kTokens[85].value;
inline constexpr int32_t type_detailTitle_alpha = (int32_t)kTokens[86].num;
inline constexpr int32_t type_detailSub_size = (int32_t)kTokens[87].num;
inline constexpr const char* type_detailSub_style = kTokens[88].value;
inline constexpr const char* type_detailSub_case = kTokens[89].value;
inline constexpr int32_t type_detailSub_alpha = (int32_t)kTokens[90].num;
inline constexpr int32_t type_gridCaption_size = (int32_t)kTokens[91].num;
inline constexpr const char* type_gridCaption_style = kTokens[92].value;
inline constexpr const char* type_gridCaption_case = kTokens[93].value;
inline constexpr int32_t type_gridCaption_alpha = (int32_t)kTokens[94].num;
inline constexpr int32_t type_letterGlyph_size = (int32_t)kTokens[95].num;
inline constexpr const char* type_letterGlyph_style = kTokens[96].value;
inline constexpr const char* type_letterGlyph_case = kTokens[97].value;
inline constexpr int32_t type_letterGlyph_alpha = (int32_t)kTokens[98].num;
inline constexpr int32_t type_boxCaption_size = (int32_t)kTokens[99].num;
inline constexpr const char* type_boxCaption_style = kTokens[100].value;
inline constexpr const char* type_boxCaption_case = kTokens[101].value;
inline constexpr int32_t type_boxCaption_alpha = (int32_t)kTokens[102].num;
inline constexpr int32_t type_boxCaptionLib_size = (int32_t)kTokens[103].num;
inline constexpr const char* type_boxCaptionLib_style = kTokens[104].value;
inline constexpr const char* type_boxCaptionLib_case = kTokens[105].value;
inline constexpr int32_t type_boxCaptionLib_alpha = (int32_t)kTokens[106].num;
inline constexpr int32_t type_settingsItem_size = (int32_t)kTokens[107].num;
inline constexpr const char* type_settingsItem_style = kTokens[108].value;
inline constexpr const char* type_settingsItem_case = kTokens[109].value;
inline constexpr int32_t type_settingsItem_alpha = (int32_t)kTokens[110].num;
inline constexpr int32_t type_settingsItem_alphaDim = (int32_t)kTokens[111].num;
inline constexpr int32_t type_albcolTitle_size = (int32_t)kTokens[112].num;
inline constexpr const char* type_albcolTitle_style = kTokens[113].value;
inline constexpr const char* type_albcolTitle_case = kTokens[114].value;
inline constexpr int32_t type_albcolTitle_alpha = (int32_t)kTokens[115].num;
inline constexpr int32_t type_albcolArtist_size = (int32_t)kTokens[116].num;
inline constexpr const char* type_albcolArtist_style = kTokens[117].value;
inline constexpr const char* type_albcolArtist_case = kTokens[118].value;
inline constexpr int32_t type_albcolArtist_alpha = (int32_t)kTokens[119].num;
inline constexpr int32_t type_settingsValue_size = (int32_t)kTokens[120].num;
inline constexpr const char* type_settingsValue_style = kTokens[121].value;
inline constexpr const char* type_settingsValue_case = kTokens[122].value;
inline constexpr int32_t type_settingsValue_alpha = (int32_t)kTokens[123].num;
inline constexpr int32_t type_settingsSub_size = (int32_t)kTokens[124].num;
inline constexpr const char* type_settingsSub_style = kTokens[125].value;
inline constexpr const char* type_settingsSub_case = kTokens[126].value;
inline constexpr int32_t type_settingsSub_alpha = (int32_t)kTokens[127].num;
inline constexpr int32_t type_settingsSub_alphaDim = (int32_t)kTokens[128].num;
inline constexpr int32_t type_npArtist_size = (int32_t)kTokens[129].num;
inline constexpr const char* type_npArtist_style = kTokens[130].value;
inline constexpr const char* type_npArtist_case = kTokens[131].value;
inline constexpr int32_t type_npArtist_alpha = (int32_t)kTokens[132].num;
inline constexpr int32_t type_npAlbum_size = (int32_t)kTokens[133].num;
inline constexpr const char* type_npAlbum_style = kTokens[134].value;
inline constexpr const char* type_npAlbum_case = kTokens[135].value;
inline constexpr int32_t type_npAlbum_alpha = (int32_t)kTokens[136].num;
inline constexpr int32_t type_npTitle_size = (int32_t)kTokens[137].num;
inline constexpr const char* type_npTitle_style = kTokens[138].value;
inline constexpr const char* type_npTitle_case = kTokens[139].value;
inline constexpr int32_t type_npTitle_alpha = (int32_t)kTokens[140].num;
inline constexpr int32_t type_npNext_size = (int32_t)kTokens[141].num;
inline constexpr const char* type_npNext_style = kTokens[142].value;
inline constexpr const char* type_npNext_case = kTokens[143].value;
inline constexpr int32_t type_npNext_alpha = (int32_t)kTokens[144].num;
inline constexpr int32_t type_npCount_size = (int32_t)kTokens[145].num;
inline constexpr const char* type_npCount_style = kTokens[146].value;
inline constexpr const char* type_npCount_case = kTokens[147].value;
inline constexpr int32_t type_npCount_alpha = (int32_t)kTokens[148].num;
inline constexpr int32_t type_time_size = (int32_t)kTokens[149].num;
inline constexpr const char* type_time_style = kTokens[150].value;
inline constexpr const char* type_time_case = kTokens[151].value;
inline constexpr int32_t type_time_alpha = (int32_t)kTokens[152].num;
inline constexpr int32_t type_npViewArtist_size = (int32_t)kTokens[153].num;
inline constexpr const char* type_npViewArtist_style = kTokens[154].value;
inline constexpr const char* type_npViewArtist_case = kTokens[155].value;
inline constexpr int32_t type_npViewArtist_alpha = (int32_t)kTokens[156].num;
inline constexpr int32_t type_npViewAlbum_size = (int32_t)kTokens[157].num;
inline constexpr const char* type_npViewAlbum_style = kTokens[158].value;
inline constexpr const char* type_npViewAlbum_case = kTokens[159].value;
inline constexpr int32_t type_npViewAlbum_alpha = (int32_t)kTokens[160].num;
inline constexpr int32_t type_npViewTitle_size = (int32_t)kTokens[161].num;
inline constexpr const char* type_npViewTitle_style = kTokens[162].value;
inline constexpr const char* type_npViewTitle_case = kTokens[163].value;
inline constexpr int32_t type_npViewTitle_alpha = (int32_t)kTokens[164].num;
inline constexpr int32_t type_npViewNext_size = (int32_t)kTokens[165].num;
inline constexpr const char* type_npViewNext_style = kTokens[166].value;
inline constexpr const char* type_npViewNext_case = kTokens[167].value;
inline constexpr int32_t type_npViewNext_alpha = (int32_t)kTokens[168].num;
inline constexpr int32_t type_npViewTime_size = (int32_t)kTokens[169].num;
inline constexpr const char* type_npViewTime_style = kTokens[170].value;
inline constexpr const char* type_npViewTime_case = kTokens[171].value;
inline constexpr int32_t type_npViewTime_alpha = (int32_t)kTokens[172].num;
inline constexpr int32_t type_overlayNumber_size = (int32_t)kTokens[173].num;
inline constexpr const char* type_overlayNumber_style = kTokens[174].value;
inline constexpr int32_t type_overlayNumber_alpha = (int32_t)kTokens[175].num;
inline constexpr int32_t type_overlayLabel_size = (int32_t)kTokens[176].num;
inline constexpr const char* type_overlayLabel_style = kTokens[177].value;
inline constexpr const char* type_overlayLabel_case = kTokens[178].value;
inline constexpr int32_t type_overlayLabel_alpha = (int32_t)kTokens[179].num;
inline constexpr int32_t type_jumpExit_size = (int32_t)kTokens[180].num;
inline constexpr const char* type_jumpExit_style = kTokens[181].value;
inline constexpr const char* type_jumpExit_case = kTokens[182].value;
inline constexpr int32_t type_jumpExit_alpha = (int32_t)kTokens[183].num;
inline constexpr int32_t type_jumpCell_size = (int32_t)kTokens[184].num;
inline constexpr const char* type_jumpCell_style = kTokens[185].value;
inline constexpr const char* type_jumpCell_case = kTokens[186].value;
inline constexpr int32_t type_jumpCell_alpha = (int32_t)kTokens[187].num;
inline constexpr int32_t type_toastArtist_size = (int32_t)kTokens[188].num;
inline constexpr const char* type_toastArtist_style = kTokens[189].value;
inline constexpr const char* type_toastArtist_case = kTokens[190].value;
inline constexpr int32_t type_toastArtist_alpha = (int32_t)kTokens[191].num;
inline constexpr int32_t type_toastTitle_size = (int32_t)kTokens[192].num;
inline constexpr const char* type_toastTitle_style = kTokens[193].value;
inline constexpr const char* type_toastTitle_case = kTokens[194].value;
inline constexpr int32_t type_toastTitle_alpha = (int32_t)kTokens[195].num;
inline constexpr int32_t type_saverClock_size = (int32_t)kTokens[196].num;
inline constexpr const char* type_saverClock_style = kTokens[197].value;
inline constexpr int32_t type_saverClock_alpha = (int32_t)kTokens[198].num;
inline constexpr int32_t type_saverFourth_size = (int32_t)kTokens[199].num;
inline constexpr const char* type_saverFourth_style = kTokens[200].value;
inline constexpr const char* type_saverFourth_case = kTokens[201].value;
inline constexpr int32_t type_saverFourth_alpha = (int32_t)kTokens[202].num;
inline constexpr int32_t type_lockTitle_size = (int32_t)kTokens[203].num;
inline constexpr const char* type_lockTitle_style = kTokens[204].value;
inline constexpr int32_t type_lockTitle_alpha = (int32_t)kTokens[205].num;
inline constexpr int32_t type_lockArtist_size = (int32_t)kTokens[206].num;
inline constexpr const char* type_lockArtist_style = kTokens[207].value;
inline constexpr int32_t type_lockArtist_alpha = (int32_t)kTokens[208].num;
inline constexpr int32_t type_lockAlbum_size = (int32_t)kTokens[209].num;
inline constexpr const char* type_lockAlbum_style = kTokens[210].value;
inline constexpr int32_t type_lockAlbum_alpha = (int32_t)kTokens[211].num;
inline constexpr int32_t type_jumpLetter_size = (int32_t)kTokens[212].num;
inline constexpr const char* type_jumpLetter_style = kTokens[213].value;
inline constexpr const char* type_jumpLetter_case = kTokens[214].value;
inline constexpr int32_t type_jumpLetter_alpha = (int32_t)kTokens[215].num;
inline constexpr int32_t type_dialogTitle_size = (int32_t)kTokens[216].num;
inline constexpr const char* type_dialogTitle_style = kTokens[217].value;
inline constexpr const char* type_dialogTitle_case = kTokens[218].value;
inline constexpr int32_t type_dialogTitle_alpha = (int32_t)kTokens[219].num;
inline constexpr int32_t type_dialogItem_size = (int32_t)kTokens[220].num;
inline constexpr const char* type_dialogItem_style = kTokens[221].value;
inline constexpr const char* type_dialogItem_case = kTokens[222].value;
inline constexpr int32_t type_dialogItem_alpha = (int32_t)kTokens[223].num;
inline constexpr int32_t type_dialogItem_alphaDim = (int32_t)kTokens[224].num;
inline constexpr int32_t type_watermark_size = (int32_t)kTokens[225].num;
inline constexpr const char* type_watermark_style = kTokens[226].value;
inline constexpr const char* type_watermark_case = kTokens[227].value;
inline constexpr int32_t type_watermark_alpha = (int32_t)kTokens[228].num;
inline constexpr uint32_t color_bg = (uint32_t)kTokens[229].num;
inline constexpr uint32_t color_bgElevated = (uint32_t)kTokens[230].num;
inline constexpr uint32_t color_tile = (uint32_t)kTokens[231].num;
inline constexpr uint32_t color_tilePressed = (uint32_t)kTokens[232].num;
inline constexpr uint32_t color_border = (uint32_t)kTokens[233].num;
inline constexpr uint32_t color_pageHeader = (uint32_t)kTokens[234].num;
inline constexpr uint32_t color_pivotInactive = (uint32_t)kTokens[235].num;
inline constexpr int32_t alpha_full = (int32_t)kTokens[236].num;
inline constexpr int32_t alpha_high = (int32_t)kTokens[237].num;
inline constexpr int32_t alpha_mid = (int32_t)kTokens[238].num;
inline constexpr int32_t alpha_low = (int32_t)kTokens[239].num;
inline constexpr int32_t alpha_faint = (int32_t)kTokens[240].num;
inline constexpr int32_t scrim_sheet_alpha = (int32_t)kTokens[241].num;
inline constexpr int32_t scrim_overlay_alpha = (int32_t)kTokens[242].num;
inline constexpr uint32_t color_black = (uint32_t)kTokens[243].num;
inline constexpr int32_t dim_alpha = (int32_t)kTokens[244].num;
inline constexpr int32_t backdrop_stop0_pos = (int32_t)kTokens[245].num;
inline constexpr int32_t backdrop_stop0_alpha = (int32_t)kTokens[246].num;
inline constexpr int32_t backdrop_stop1_pos = (int32_t)kTokens[247].num;
inline constexpr int32_t backdrop_stop1_alpha = (int32_t)kTokens[248].num;
inline constexpr int32_t backdrop_stop2_pos = (int32_t)kTokens[249].num;
inline constexpr int32_t backdrop_stop2_alpha = (int32_t)kTokens[250].num;
inline constexpr int32_t pageSurface_alphaTop = (int32_t)kTokens[251].num;
inline constexpr int32_t accent_hueDefault = (int32_t)kTokens[252].num;
inline constexpr int32_t accent_hueStep = (int32_t)kTokens[253].num;
inline constexpr int32_t accent_hueCount = (int32_t)kTokens[254].num;
inline constexpr int32_t accent_value = (int32_t)kTokens[255].num;
inline constexpr int32_t accent_satMax = (int32_t)kTokens[256].num;
inline constexpr int32_t accent_satMin = (int32_t)kTokens[257].num;
inline constexpr int32_t accent_lumFloor = (int32_t)kTokens[258].num;
inline constexpr int32_t accent_brightSat = (int32_t)kTokens[259].num;
inline constexpr uint32_t accent_default = (uint32_t)kTokens[260].num;
inline constexpr uint32_t accent_brightDefault = (uint32_t)kTokens[261].num;
inline constexpr uint32_t accent_h0 = (uint32_t)kTokens[262].num;
inline constexpr uint32_t accent_h10 = (uint32_t)kTokens[263].num;
inline constexpr uint32_t accent_h20 = (uint32_t)kTokens[264].num;
inline constexpr uint32_t accent_h30 = (uint32_t)kTokens[265].num;
inline constexpr uint32_t accent_h40 = (uint32_t)kTokens[266].num;
inline constexpr uint32_t accent_h50 = (uint32_t)kTokens[267].num;
inline constexpr uint32_t accent_h60 = (uint32_t)kTokens[268].num;
inline constexpr uint32_t accent_h70 = (uint32_t)kTokens[269].num;
inline constexpr uint32_t accent_h80 = (uint32_t)kTokens[270].num;
inline constexpr uint32_t accent_h90 = (uint32_t)kTokens[271].num;
inline constexpr uint32_t accent_h100 = (uint32_t)kTokens[272].num;
inline constexpr uint32_t accent_h110 = (uint32_t)kTokens[273].num;
inline constexpr uint32_t accent_h120 = (uint32_t)kTokens[274].num;
inline constexpr uint32_t accent_h130 = (uint32_t)kTokens[275].num;
inline constexpr uint32_t accent_h140 = (uint32_t)kTokens[276].num;
inline constexpr uint32_t accent_h150 = (uint32_t)kTokens[277].num;
inline constexpr uint32_t accent_h160 = (uint32_t)kTokens[278].num;
inline constexpr uint32_t accent_h170 = (uint32_t)kTokens[279].num;
inline constexpr uint32_t accent_h180 = (uint32_t)kTokens[280].num;
inline constexpr uint32_t accent_h190 = (uint32_t)kTokens[281].num;
inline constexpr uint32_t accent_h200 = (uint32_t)kTokens[282].num;
inline constexpr uint32_t accent_h210 = (uint32_t)kTokens[283].num;
inline constexpr uint32_t accent_h220 = (uint32_t)kTokens[284].num;
inline constexpr uint32_t accent_h230 = (uint32_t)kTokens[285].num;
inline constexpr uint32_t accent_h240 = (uint32_t)kTokens[286].num;
inline constexpr uint32_t accent_h250 = (uint32_t)kTokens[287].num;
inline constexpr uint32_t accent_h260 = (uint32_t)kTokens[288].num;
inline constexpr uint32_t accent_h270 = (uint32_t)kTokens[289].num;
inline constexpr uint32_t accent_h280 = (uint32_t)kTokens[290].num;
inline constexpr uint32_t accent_h290 = (uint32_t)kTokens[291].num;
inline constexpr uint32_t accent_h300 = (uint32_t)kTokens[292].num;
inline constexpr uint32_t accent_h310 = (uint32_t)kTokens[293].num;
inline constexpr uint32_t accent_h320 = (uint32_t)kTokens[294].num;
inline constexpr uint32_t accent_h330 = (uint32_t)kTokens[295].num;
inline constexpr uint32_t accent_h340 = (uint32_t)kTokens[296].num;
inline constexpr uint32_t accent_h350 = (uint32_t)kTokens[297].num;
inline constexpr int32_t focus_border = (int32_t)kTokens[298].num;
inline constexpr int32_t focus_barWidth = (int32_t)kTokens[299].num;
inline constexpr int32_t focus_barOffset = (int32_t)kTokens[300].num;
inline constexpr int32_t scrollbar_width = (int32_t)kTokens[301].num;
inline constexpr int32_t scrollbar_x = (int32_t)kTokens[302].num;
inline constexpr int32_t scrollbar_min = (int32_t)kTokens[303].num;
inline constexpr int32_t scrollbar_alpha = (int32_t)kTokens[304].num;
inline constexpr int32_t scrollbar_hold = (int32_t)kTokens[305].num;
inline constexpr int32_t scrollbar_fade = (int32_t)kTokens[306].num;
inline constexpr int32_t edge = (int32_t)kTokens[307].num;
inline constexpr int32_t hubEdge = (int32_t)kTokens[308].num;
inline constexpr int32_t homeTextX = (int32_t)kTokens[309].num;
inline constexpr int32_t homePitch = (int32_t)kTokens[310].num;
inline constexpr int32_t homeFirstBaseline = (int32_t)kTokens[311].num;
inline constexpr int32_t homeSubtitleX = (int32_t)kTokens[312].num;
inline constexpr int32_t homeSubtitleDrop = (int32_t)kTokens[313].num;
inline constexpr int32_t homeWatermarkBaseline = (int32_t)kTokens[314].num;
inline constexpr int32_t qpPeekScale = (int32_t)kTokens[315].num;
inline constexpr int32_t qpPeekX = (int32_t)kTokens[316].num;
inline constexpr int32_t qpPeekY = (int32_t)kTokens[317].num;
inline constexpr int32_t qpPeekAlpha = (int32_t)kTokens[318].num;
inline constexpr int32_t headerBand = (int32_t)kTokens[319].num;
inline constexpr int32_t headerXHeightBottom = (int32_t)kTokens[320].num;
inline constexpr int32_t pivotTop = (int32_t)kTokens[321].num;
inline constexpr int32_t pivotBottom = (int32_t)kTokens[322].num;
inline constexpr int32_t pivotBaseline = (int32_t)kTokens[323].num;
inline constexpr int32_t pivotGap = (int32_t)kTokens[324].num;
inline constexpr int32_t pivotActiveGap = (int32_t)kTokens[325].num;
inline constexpr int32_t pivotLeadGap = (int32_t)kTokens[326].num;
inline constexpr int32_t contentTop = (int32_t)kTokens[327].num;
inline constexpr int32_t page_headerX = (int32_t)kTokens[328].num;
inline constexpr int32_t page_headerBaseline = (int32_t)kTokens[329].num;
inline constexpr int32_t page_pivotBaseline = (int32_t)kTokens[330].num;
inline constexpr int32_t page_contentTop = (int32_t)kTokens[331].num;
inline constexpr int32_t page_countBaseline = (int32_t)kTokens[332].num;
inline constexpr int32_t page_albumHeaderX = (int32_t)kTokens[333].num;
inline constexpr int32_t page_settingsHeaderX = (int32_t)kTokens[334].num;
inline constexpr int32_t page_settingsHeaderAlpha = (int32_t)kTokens[335].num;
inline constexpr int32_t page_rowTitle = (int32_t)kTokens[336].num;
inline constexpr int32_t tileS = (int32_t)kTokens[337].num;
inline constexpr int32_t gridTile = (int32_t)kTokens[338].num;
inline constexpr int32_t gridX = (int32_t)kTokens[339].num;
inline constexpr int32_t gridGap = (int32_t)kTokens[340].num;
inline constexpr int32_t gutter = (int32_t)kTokens[341].num;
inline constexpr int32_t gridCols = (int32_t)kTokens[342].num;
inline constexpr int32_t tileCaptionDrop = (int32_t)kTokens[343].num;
inline constexpr int32_t tileCaptionPitch = (int32_t)kTokens[344].num;
inline constexpr int32_t tileCaptionBand = (int32_t)kTokens[345].num;
inline constexpr int32_t tileCellH = (int32_t)kTokens[346].num;
inline constexpr int32_t tileRowPitch = (int32_t)kTokens[347].num;
inline constexpr int32_t tileL = (int32_t)kTokens[348].num;
inline constexpr int32_t tileLGutter = (int32_t)kTokens[349].num;
inline constexpr int32_t letterTileBorder = (int32_t)kTokens[350].num;
inline constexpr int32_t letterTileBorderAlpha = (int32_t)kTokens[351].num;
inline constexpr int32_t letterTileBorderFocused = (int32_t)kTokens[352].num;
inline constexpr int32_t boxLine = (int32_t)kTokens[353].num;
inline constexpr int32_t boxLineAlpha = (int32_t)kTokens[354].num;
inline constexpr int32_t boxInset = (int32_t)kTokens[355].num;
inline constexpr int32_t boxCaptionPitch = (int32_t)kTokens[356].num;
inline constexpr int32_t boxCaptionX = (int32_t)kTokens[357].num;
inline constexpr int32_t boxCaptionBase = (int32_t)kTokens[358].num;
inline constexpr int32_t boxLines = (int32_t)kTokens[359].num;
inline constexpr int32_t boxLinesPlaying = (int32_t)kTokens[360].num;
inline constexpr int32_t pressFrame = (int32_t)kTokens[361].num;
inline constexpr int32_t letterInsetX = (int32_t)kTokens[362].num;
inline constexpr int32_t letterInsetBaseline = (int32_t)kTokens[363].num;
inline constexpr int32_t rowHeight = (int32_t)kTokens[364].num;
inline constexpr int32_t rowTextInset = (int32_t)kTokens[365].num;
inline constexpr int32_t rowCollection = (int32_t)kTokens[366].num;
inline constexpr int32_t rowPlayRing = (int32_t)kTokens[367].num;
inline constexpr int32_t rowPlayRingX = (int32_t)kTokens[368].num;
inline constexpr int32_t rowCollectionTextX = (int32_t)kTokens[369].num;
inline constexpr int32_t rowThumb = (int32_t)kTokens[370].num;
inline constexpr int32_t rowThumbGap = (int32_t)kTokens[371].num;
inline constexpr int32_t rowThumbDrop = (int32_t)kTokens[372].num;
inline constexpr int32_t dividerTileCollection = (int32_t)kTokens[373].num;
inline constexpr int32_t dividerTile = (int32_t)kTokens[374].num;
inline constexpr int32_t rowRing = (int32_t)kTokens[375].num;
inline constexpr int32_t songIndent = (int32_t)kTokens[376].num;
inline constexpr int32_t detailRow = (int32_t)kTokens[377].num;
inline constexpr int32_t detailNameDrop = (int32_t)kTokens[378].num;
inline constexpr int32_t detailNameBaseline = (int32_t)kTokens[379].num;
inline constexpr int32_t detailRingTop = (int32_t)kTokens[380].num;
inline constexpr int32_t detailRing = (int32_t)kTokens[381].num;
inline constexpr int32_t detailRingGap = (int32_t)kTokens[382].num;
inline constexpr int32_t detailRingCount = (int32_t)kTokens[383].num;
inline constexpr int32_t detailRingInset = (int32_t)kTokens[384].num;
inline constexpr int32_t detailLabelDrop = (int32_t)kTokens[385].num;
inline constexpr int32_t detailLabelBaseline = (int32_t)kTokens[386].num;
inline constexpr int32_t detailRowsTop = (int32_t)kTokens[387].num;
inline constexpr int32_t detailTextX = (int32_t)kTokens[388].num;
inline constexpr int32_t detailTitleDrop = (int32_t)kTokens[389].num;
inline constexpr int32_t detailTitlePitch = (int32_t)kTokens[390].num;
inline constexpr int32_t detailTitleLines = (int32_t)kTokens[391].num;
inline constexpr int32_t detailSubPitch = (int32_t)kTokens[392].num;
inline constexpr int32_t detailRingX0 = (int32_t)kTokens[393].num;
inline constexpr int32_t detailRingX1 = (int32_t)kTokens[394].num;
inline constexpr int32_t detailRingX2 = (int32_t)kTokens[395].num;
inline constexpr int32_t albumArtX = (int32_t)kTokens[396].num;
inline constexpr int32_t albumArtTop = (int32_t)kTokens[397].num;
inline constexpr int32_t albcolTop = (int32_t)kTokens[398].num;
inline constexpr int32_t albcolPitch = (int32_t)kTokens[399].num;
inline constexpr int32_t albcolRing = (int32_t)kTokens[400].num;
inline constexpr int32_t albcolRingInset = (int32_t)kTokens[401].num;
inline constexpr int32_t albcolRingDrop = (int32_t)kTokens[402].num;
inline constexpr int32_t albcolTextGap = (int32_t)kTokens[403].num;
inline constexpr int32_t albcolTitleTop = (int32_t)kTokens[404].num;
inline constexpr int32_t albcolTitlePitch = (int32_t)kTokens[405].num;
inline constexpr int32_t albcolTitleLines = (int32_t)kTokens[406].num;
inline constexpr int32_t albcolArtistPitch = (int32_t)kTokens[407].num;
inline constexpr int32_t albcolTrackDrop = (int32_t)kTokens[408].num;
inline constexpr int32_t albcolTrackPitch = (int32_t)kTokens[409].num;
inline constexpr int32_t albcolLetter = (int32_t)kTokens[410].num;
inline constexpr int32_t albumRowsGap = (int32_t)kTokens[411].num;
inline constexpr int32_t albumRowsTop = (int32_t)kTokens[412].num;
inline constexpr int32_t settingsPitch = (int32_t)kTokens[413].num;
inline constexpr int32_t settingsLabelBaselineInset = (int32_t)kTokens[414].num;
inline constexpr int32_t settingsSwatch = (int32_t)kTokens[415].num;
inline constexpr int32_t settingsSwatchGap = (int32_t)kTokens[416].num;
inline constexpr int32_t settingsTitlePitch = (int32_t)kTokens[417].num;
inline constexpr int32_t settingsSubPad = (int32_t)kTokens[418].num;
inline constexpr int32_t settingsValueFocused = (int32_t)kTokens[419].num;
inline constexpr int32_t settingsValueDim = (int32_t)kTokens[420].num;
inline constexpr int32_t eqValueGap = (int32_t)kTokens[421].num;
inline constexpr int32_t queueCountBaseline = (int32_t)kTokens[422].num;
inline constexpr int32_t queueRowsTop = (int32_t)kTokens[423].num;
inline constexpr int32_t backRing = (int32_t)kTokens[424].num;
inline constexpr int32_t backRingStroke = (int32_t)kTokens[425].num;
inline constexpr int32_t backRingX = (int32_t)kTokens[426].num;
inline constexpr int32_t backRingY = (int32_t)kTokens[427].num;
inline constexpr int32_t npBackRing = (int32_t)kTokens[428].num;
inline constexpr int32_t npBackRingStroke = (int32_t)kTokens[429].num;
inline constexpr int32_t npBackRingX = (int32_t)kTokens[430].num;
inline constexpr int32_t npBackRingY = (int32_t)kTokens[431].num;
inline constexpr int32_t npBackGlyph = (int32_t)kTokens[432].num;
inline constexpr int32_t touchSlop = (int32_t)kTokens[433].num;
inline constexpr int32_t flingCap = (int32_t)kTokens[434].num;
inline constexpr int32_t flingStop = (int32_t)kTokens[435].num;
inline constexpr int32_t flingMin = (int32_t)kTokens[436].num;
inline constexpr int32_t npTextTop = (int32_t)kTokens[437].num;
inline constexpr int32_t npAlbumBaseline = (int32_t)kTokens[438].num;
inline constexpr int32_t npArtistY = (int32_t)kTokens[439].num;
inline constexpr int32_t npAlbumY = (int32_t)kTokens[440].num;
inline constexpr int32_t npFromBaseline = (int32_t)kTokens[441].num;
inline constexpr int32_t npArtTop = (int32_t)kTokens[442].num;
inline constexpr int32_t npArt = (int32_t)kTokens[443].num;
inline constexpr int32_t npProgressY = (int32_t)kTokens[444].num;
inline constexpr int32_t npProgressThick = (int32_t)kTokens[445].num;
inline constexpr int32_t npTimeDrop = (int32_t)kTokens[446].num;
inline constexpr int32_t npBarHeight = (int32_t)kTokens[447].num;
inline constexpr int32_t npBarGap = (int32_t)kTokens[448].num;
inline constexpr int32_t npBarTrack = (int32_t)kTokens[449].num;
inline constexpr int32_t npTimeBaseline = (int32_t)kTokens[450].num;
inline constexpr int32_t npTitleY = (int32_t)kTokens[451].num;
inline constexpr int32_t npTextX = (int32_t)kTokens[452].num;
inline constexpr int32_t npArtX = (int32_t)kTokens[453].num;
inline constexpr int32_t npTextRight = (int32_t)kTokens[454].num;
inline constexpr int32_t npNextIndent = (int32_t)kTokens[455].num;
inline constexpr int32_t npNextPitch = (int32_t)kTokens[456].num;
inline constexpr int32_t npNextDrop = (int32_t)kTokens[457].num;
inline constexpr int32_t npNextLinePitch = (int32_t)kTokens[458].num;
inline constexpr int32_t npNextCount = (int32_t)kTokens[459].num;
inline constexpr int32_t npNextNavCount = (int32_t)kTokens[460].num;
inline constexpr int32_t npRing = (int32_t)kTokens[461].num;
inline constexpr int32_t npRingStroke = (int32_t)kTokens[462].num;
inline constexpr int32_t npRepeatOne = (int32_t)kTokens[463].num;
inline constexpr int32_t npRepeatOneR = (int32_t)kTokens[464].num;
inline constexpr int32_t npRepeatOneAt = (int32_t)kTokens[465].num;
inline constexpr int32_t npRepeatOneSweep = (int32_t)kTokens[466].num;
inline constexpr int32_t npRingY = (int32_t)kTokens[467].num;
inline constexpr int32_t npRingNavY = (int32_t)kTokens[468].num;
inline constexpr int32_t npRingX0 = (int32_t)kTokens[469].num;
inline constexpr int32_t npRingX1 = (int32_t)kTokens[470].num;
inline constexpr int32_t npRingX2 = (int32_t)kTokens[471].num;
inline constexpr int32_t npRingCx0 = (int32_t)kTokens[472].num;
inline constexpr int32_t npRingCx1 = (int32_t)kTokens[473].num;
inline constexpr int32_t npRingCx2 = (int32_t)kTokens[474].num;
inline constexpr int32_t npBtGlyph = (int32_t)kTokens[475].num;
inline constexpr int32_t seekStepMs = (int32_t)kTokens[476].num;
inline constexpr int32_t seekHoldMs = (int32_t)kTokens[477].num;
inline constexpr int32_t seekTickMs = (int32_t)kTokens[478].num;
inline constexpr int32_t seekTickStepMs = (int32_t)kTokens[479].num;
inline constexpr int32_t seekEndMarginMs = (int32_t)kTokens[480].num;
inline constexpr int32_t statusHeight = (int32_t)kTokens[481].num;
inline constexpr int32_t statusTop = (int32_t)kTokens[482].num;
inline constexpr int32_t statusGap = (int32_t)kTokens[483].num;
inline constexpr int32_t npStatusHeight = (int32_t)kTokens[484].num;
inline constexpr int32_t npStatusTop = (int32_t)kTokens[485].num;
inline constexpr int32_t npStatusRight = (int32_t)kTokens[486].num;
inline constexpr int32_t npStatusGap = (int32_t)kTokens[487].num;
inline constexpr int32_t npStatusText = (int32_t)kTokens[488].num;
inline constexpr int32_t npStatusBatteryW = (int32_t)kTokens[489].num;
inline constexpr int32_t npStatusBatteryH = (int32_t)kTokens[490].num;
inline constexpr int32_t statusBatteryX = (int32_t)kTokens[491].num;
inline constexpr int32_t statusBatteryY = (int32_t)kTokens[492].num;
inline constexpr int32_t statusBatteryW = (int32_t)kTokens[493].num;
inline constexpr int32_t statusBatteryH = (int32_t)kTokens[494].num;
inline constexpr int32_t saverArt = (int32_t)kTokens[495].num;
inline constexpr int32_t saverBottomMargin = (int32_t)kTokens[496].num;
inline constexpr int32_t saverFourthBaseline = (int32_t)kTokens[497].num;
inline constexpr int32_t saverAlbumPitch = (int32_t)kTokens[498].num;
inline constexpr int32_t saverClockBaseline = (int32_t)kTokens[499].num;
inline constexpr int32_t lockArt = (int32_t)kTokens[500].num;
inline constexpr int32_t lockBtGlyph = (int32_t)kTokens[501].num;
inline constexpr int32_t lockArtY = (int32_t)kTokens[502].num;
inline constexpr int32_t lockTextX = (int32_t)kTokens[503].num;
inline constexpr int32_t lockTitleDrop = (int32_t)kTokens[504].num;
inline constexpr int32_t lockArtistPitch = (int32_t)kTokens[505].num;
inline constexpr int32_t lockAlbumPitch = (int32_t)kTokens[506].num;
inline constexpr int32_t overlayRing = (int32_t)kTokens[507].num;
inline constexpr int32_t overlayGlyph = (int32_t)kTokens[508].num;
inline constexpr int32_t overlayGlyphOffset = (int32_t)kTokens[509].num;
inline constexpr int32_t toastHeight = (int32_t)kTokens[510].num;
inline constexpr int32_t toastArtistBaseline = (int32_t)kTokens[511].num;
inline constexpr int32_t toastTitleBaseline = (int32_t)kTokens[512].num;
inline constexpr int32_t jumpCols = (int32_t)kTokens[513].num;
inline constexpr int32_t jumpGap = (int32_t)kTokens[514].num;
inline constexpr int32_t jumpLeft = (int32_t)kTokens[515].num;
inline constexpr int32_t jumpPitch = (int32_t)kTokens[516].num;
inline constexpr int32_t jumpExitCells = (int32_t)kTokens[517].num;
inline constexpr int32_t jumpCells = (int32_t)kTokens[518].num;
inline constexpr int32_t jumpTop = (int32_t)kTokens[519].num;
inline constexpr int32_t jumpExitCx = (int32_t)kTokens[520].num;
inline constexpr int32_t jumpExitTop = (int32_t)kTokens[521].num;
inline constexpr int32_t jumpLetterInsetX = (int32_t)kTokens[522].num;
inline constexpr int32_t jumpLetterInsetBaseline = (int32_t)kTokens[523].num;
inline constexpr int32_t jumpSearchGlyph = (int32_t)kTokens[524].num;
inline constexpr int32_t kb_fieldTop = (int32_t)kTokens[525].num;
inline constexpr int32_t kb_fieldH = (int32_t)kTokens[526].num;
inline constexpr int32_t kb_fieldPad = (int32_t)kTokens[527].num;
inline constexpr int32_t kb_fieldBorder = (int32_t)kTokens[528].num;
inline constexpr int32_t kb_cursorW = (int32_t)kTokens[529].num;
inline constexpr int32_t kb_keyW = (int32_t)kTokens[530].num;
inline constexpr int32_t kb_keyH = (int32_t)kTokens[531].num;
inline constexpr int32_t kb_top = (int32_t)kTokens[532].num;
inline constexpr int32_t glyphBox = (int32_t)kTokens[533].num;
inline constexpr int32_t glyphStroke = (int32_t)kTokens[534].num;
inline constexpr int32_t ringStrokeFocused = (int32_t)kTokens[535].num;
inline constexpr int32_t ringStrokeIdle = (int32_t)kTokens[536].num;
inline constexpr int32_t ringArcStart = (int32_t)kTokens[537].num;
inline constexpr int32_t overlayPlusY = (int32_t)kTokens[538].num;
inline constexpr int32_t overlayAnchorH = (int32_t)kTokens[539].num;
inline constexpr int32_t overlayAnchorScale = (int32_t)kTokens[540].num;
inline constexpr int32_t overlayRingY = (int32_t)kTokens[541].num;
inline constexpr int32_t overlayMinusY = (int32_t)kTokens[542].num;
inline constexpr int32_t overlayBottomMargin = (int32_t)kTokens[543].num;
inline constexpr int32_t overlayTextPitch = (int32_t)kTokens[544].num;
inline constexpr int32_t overlayTitleBottom = (int32_t)kTokens[545].num;
inline constexpr int32_t overlayLit = (int32_t)kTokens[546].num;
inline constexpr int32_t overlayDim = (int32_t)kTokens[547].num;
inline constexpr int32_t vol_ring = (int32_t)kTokens[548].num;
inline constexpr int32_t vol_ringStroke = (int32_t)kTokens[549].num;
inline constexpr int32_t vol_hit = (int32_t)kTokens[550].num;
inline constexpr int32_t vol_plus = (int32_t)kTokens[551].num;
inline constexpr int32_t vol_minus = (int32_t)kTokens[552].num;
inline constexpr int32_t vol_pause = (int32_t)kTokens[553].num;
inline constexpr int32_t vol_skip = (int32_t)kTokens[554].num;
inline constexpr int32_t vol_skipX = (int32_t)kTokens[555].num;
inline constexpr int32_t vol_pauseGap = (int32_t)kTokens[556].num;
inline constexpr int32_t vol_glowSkip = (int32_t)kTokens[557].num;
inline constexpr int32_t vol_glowRing = (int32_t)kTokens[558].num;
inline constexpr int32_t pressGlowDilate = (int32_t)kTokens[559].num;
inline constexpr int32_t pressGlow = (int32_t)kTokens[560].num;
inline constexpr int32_t pressGlowAlpha = (int32_t)kTokens[561].num;
inline constexpr int32_t vol_discAlpha = (int32_t)kTokens[562].num;
inline constexpr int32_t vol_number = (int32_t)kTokens[563].num;
inline constexpr int32_t vol_label = (int32_t)kTokens[564].num;
inline constexpr int32_t vol_exit = (int32_t)kTokens[565].num;
inline constexpr int32_t vol_plusY = (int32_t)kTokens[566].num;
inline constexpr int32_t vol_numberY = (int32_t)kTokens[567].num;
inline constexpr int32_t vol_numberGap = (int32_t)kTokens[568].num;
inline constexpr int32_t vol_ringY = (int32_t)kTokens[569].num;
inline constexpr int32_t vol_minusY = (int32_t)kTokens[570].num;
inline constexpr int32_t vol_slideStride = (int32_t)kTokens[571].num;
inline constexpr int32_t vol_exitX = (int32_t)kTokens[572].num;
inline constexpr int32_t vol_exitY = (int32_t)kTokens[573].num;
inline constexpr int32_t vol_textX = (int32_t)kTokens[574].num;
inline constexpr int32_t vol_textY = (int32_t)kTokens[575].num;
inline constexpr int32_t vol_textGap = (int32_t)kTokens[576].num;
inline constexpr int32_t vol_queueX = (int32_t)kTokens[577].num;
inline constexpr int32_t vol_queueDrop = (int32_t)kTokens[578].num;
inline constexpr int32_t vol_queueRing = (int32_t)kTokens[579].num;
inline constexpr int32_t vol_queueStroke = (int32_t)kTokens[580].num;
inline constexpr int32_t batteryLowPercent = (int32_t)kTokens[581].num;
inline constexpr int32_t qpEdge = (int32_t)kTokens[582].num;
inline constexpr int32_t qpTile = (int32_t)kTokens[583].num;
inline constexpr int32_t qpGutter = (int32_t)kTokens[584].num;
inline constexpr int32_t qpW = (int32_t)kTokens[585].num;
inline constexpr int32_t qpFirstBaseline = (int32_t)kTokens[586].num;
inline constexpr int32_t qpContentDrop = (int32_t)kTokens[587].num;
inline constexpr int32_t qpSectionGap = (int32_t)kTokens[588].num;
inline constexpr int32_t qpHintDrop = (int32_t)kTokens[589].num;
inline constexpr int32_t qpHintPitch = (int32_t)kTokens[590].num;
inline constexpr int32_t qpHintW = (int32_t)kTokens[591].num;
inline constexpr int32_t qpTilePad = (int32_t)kTokens[592].num;
inline constexpr int32_t qpTextX = (int32_t)kTokens[593].num;
inline constexpr int32_t qpBaseline = (int32_t)kTokens[594].num;
inline constexpr int32_t qpTileTop = (int32_t)kTokens[595].num;
inline constexpr int32_t qpHintBase = (int32_t)kTokens[596].num;
inline constexpr int32_t qpHintStep = (int32_t)kTokens[597].num;
inline constexpr int32_t qpHintGap = (int32_t)kTokens[598].num;
inline constexpr int32_t qpEndPad = (int32_t)kTokens[599].num;
inline constexpr int32_t qpPlayTop = (int32_t)kTokens[600].num;
inline constexpr int32_t qpPlayGap = (int32_t)kTokens[601].num;
inline constexpr int32_t qpTiltAngle = (int32_t)kTokens[602].num;
inline constexpr int32_t qpTiltDepth = (int32_t)kTokens[603].num;
inline constexpr int32_t homeBarX = (int32_t)kTokens[604].num;
inline constexpr int32_t homeBarTop = (int32_t)kTokens[605].num;
inline constexpr int32_t homeBarH = (int32_t)kTokens[606].num;
inline constexpr int32_t focusBandBottom = (int32_t)kTokens[607].num;
inline constexpr int32_t motion_perspectiveRatio = (int32_t)kTokens[608].num;
inline constexpr int32_t motion_perspective = (int32_t)kTokens[609].num;
inline constexpr int32_t motion_homeEnter = (int32_t)kTokens[610].num;
inline constexpr int32_t motion_homeEnterPeek = (int32_t)kTokens[611].num;
inline constexpr int32_t motion_homeEnterStagger = (int32_t)kTokens[612].num;
inline constexpr int32_t motion_homeEnterShift = (int32_t)kTokens[613].num;
inline constexpr int32_t motion_swap = (int32_t)kTokens[614].num;
inline constexpr int32_t motion_leave = (int32_t)kTokens[615].num;
inline constexpr int32_t motion_headerZoom = (int32_t)kTokens[616].num;
inline constexpr int32_t motion_headerZoomX = (int32_t)kTokens[617].num;
inline constexpr int32_t motion_headerZoomY = (int32_t)kTokens[618].num;
inline constexpr int32_t motion_headerDelay = (int32_t)kTokens[619].num;
inline constexpr int32_t motion_header = (int32_t)kTokens[620].num;
inline constexpr int32_t motion_pivotInX = (int32_t)kTokens[621].num;
inline constexpr int32_t motion_pivotInY = (int32_t)kTokens[622].num;
inline constexpr int32_t motion_pivotDelay = (int32_t)kTokens[623].num;
inline constexpr int32_t motion_pivotIn = (int32_t)kTokens[624].num;
inline constexpr int32_t motion_pivotFade = (int32_t)kTokens[625].num;
inline constexpr int32_t motion_groundDelay = (int32_t)kTokens[626].num;
inline constexpr int32_t motion_ground = (int32_t)kTokens[627].num;
inline constexpr int32_t motion_contentScale = (int32_t)kTokens[628].num;
inline constexpr int32_t motion_contentX = (int32_t)kTokens[629].num;
inline constexpr int32_t motion_contentY = (int32_t)kTokens[630].num;
inline constexpr int32_t motion_contentDelay = (int32_t)kTokens[631].num;
inline constexpr int32_t motion_content = (int32_t)kTokens[632].num;
inline constexpr int32_t motion_t2Swap = (int32_t)kTokens[633].num;
inline constexpr int32_t motion_t2Word = (int32_t)kTokens[634].num;
inline constexpr int32_t motion_t2BaseDelay = (int32_t)kTokens[635].num;
inline constexpr int32_t motion_t2Base = (int32_t)kTokens[636].num;
inline constexpr int32_t motion_t2Peek = (int32_t)kTokens[637].num;
inline constexpr int32_t motion_t2PivotDelay = (int32_t)kTokens[638].num;
inline constexpr int32_t motion_t2GroundDelay = (int32_t)kTokens[639].num;
inline constexpr int32_t motion_t2Ground = (int32_t)kTokens[640].num;
inline constexpr int32_t motion_t2ContentDelay = (int32_t)kTokens[641].num;
inline constexpr int32_t motion_t2Content = (int32_t)kTokens[642].num;
inline constexpr int32_t motion_t2FadeDelay = (int32_t)kTokens[643].num;
inline constexpr int32_t motion_t2Fade = (int32_t)kTokens[644].num;
inline constexpr int32_t motion_t4hSwap = (int32_t)kTokens[645].num;
inline constexpr int32_t motion_t4hPivot = (int32_t)kTokens[646].num;
inline constexpr int32_t motion_t4hContent = (int32_t)kTokens[647].num;
inline constexpr int32_t motion_t4hWord = (int32_t)kTokens[648].num;
inline constexpr int32_t motion_t4hItems = (int32_t)kTokens[649].num;
inline constexpr int32_t motion_t4hSubDelay = (int32_t)kTokens[650].num;
inline constexpr int32_t motion_t4hSub = (int32_t)kTokens[651].num;
inline constexpr int32_t motion_t4hPeekDelay = (int32_t)kTokens[652].num;
inline constexpr int32_t motion_t4hPeek = (int32_t)kTokens[653].num;
inline constexpr int32_t motion_t4mDelay = (int32_t)kTokens[654].num;
inline constexpr int32_t motion_t4m = (int32_t)kTokens[655].num;
inline constexpr int32_t motion_morphPop = (int32_t)kTokens[656].num;
inline constexpr int32_t motion_morphPush = (int32_t)kTokens[657].num;
inline constexpr int32_t motion_t3mTileDelay = (int32_t)kTokens[658].num;
inline constexpr int32_t motion_t3mTile = (int32_t)kTokens[659].num;
inline constexpr int32_t motion_t3mLabelDelay = (int32_t)kTokens[660].num;
inline constexpr int32_t motion_t3mLabel = (int32_t)kTokens[661].num;
inline constexpr int32_t motion_t3mBaseDelay = (int32_t)kTokens[662].num;
inline constexpr int32_t motion_t3mBase = (int32_t)kTokens[663].num;
inline constexpr int32_t motion_t3mFadeDelay = (int32_t)kTokens[664].num;
inline constexpr int32_t motion_t3mFade = (int32_t)kTokens[665].num;
inline constexpr int32_t motion_morphScale = (int32_t)kTokens[666].num;
inline constexpr int32_t motion_morphRowPenDelay = (int32_t)kTokens[667].num;
inline constexpr int32_t motion_morphRowPen = (int32_t)kTokens[668].num;
inline constexpr int32_t motion_morphRowPopDelay = (int32_t)kTokens[669].num;
inline constexpr int32_t motion_morphRowPop = (int32_t)kTokens[670].num;
inline constexpr int32_t motion_wordHoldMax = (int32_t)kTokens[671].num;
inline constexpr int32_t motion_t4hWordDelay = (int32_t)kTokens[672].num;
inline constexpr int32_t motion_t4hBox = (int32_t)kTokens[673].num;
inline constexpr int32_t motion_contentAfter = (int32_t)kTokens[674].num;
inline constexpr int32_t motion_rowsDy = (int32_t)kTokens[675].num;
inline constexpr int32_t motion_rowsYa = (int32_t)kTokens[676].num;
inline constexpr int32_t motion_rowsDx = (int32_t)kTokens[677].num;
inline constexpr int32_t motion_rows = (int32_t)kTokens[678].num;
inline constexpr int32_t motion_rowsFade = (int32_t)kTokens[679].num;
inline constexpr int32_t motion_rowsT2Alpha = (int32_t)kTokens[680].num;
inline constexpr int32_t motion_rowsT2Fade = (int32_t)kTokens[681].num;
inline constexpr const char* motion_contentHubRows = kTokens[682].value;
inline constexpr const char* motion_contentPageRows = kTokens[683].value;
inline constexpr const char* motion_leaveFade = kTokens[684].value;
inline constexpr const char* motion_contentZoom = kTokens[685].value;
inline constexpr const char* motion_chromeSlide = kTokens[686].value;
inline constexpr const char* motion_headerForm = kTokens[687].value;
inline constexpr const char* motion_t3mForm = kTokens[688].value;
inline constexpr int32_t motion_t3aFadeDelay = (int32_t)kTokens[689].num;
inline constexpr int32_t motion_t3aFade = (int32_t)kTokens[690].num;
inline constexpr int32_t motion_tileBack = (int32_t)kTokens[691].num;
inline constexpr int32_t motion_t3cFade = (int32_t)kTokens[692].num;
inline constexpr const char* motion_wordForm = kTokens[693].value;
inline constexpr int32_t motion_slices = (int32_t)kTokens[694].num;
inline constexpr int32_t motion_slicesAlbums = (int32_t)kTokens[695].num;
inline constexpr int32_t motion_slicesSettings = (int32_t)kTokens[696].num;
inline constexpr int32_t motion_pivotOut = (int32_t)kTokens[697].num;
inline constexpr int32_t motion_pivotStrip = (int32_t)kTokens[698].num;
inline constexpr int32_t motion_pivotLabel = (int32_t)kTokens[699].num;
inline constexpr int32_t motion_pivotRows = (int32_t)kTokens[700].num;
inline constexpr int32_t motion_pivotShift = (int32_t)kTokens[701].num;
inline constexpr int32_t motion_pivotRowStagger = (int32_t)kTokens[702].num;
inline constexpr int32_t motion_pivotRowCap = (int32_t)kTokens[703].num;
inline constexpr int32_t motion_pivotHeaderParallax = (int32_t)kTokens[704].num;
inline constexpr int32_t motion_pivotStripShift = (int32_t)kTokens[705].num;
inline constexpr int32_t motion_pivotStripAlpha = (int32_t)kTokens[706].num;
inline constexpr int32_t motion_pivotRowInShift = (int32_t)kTokens[707].num;
inline constexpr int32_t motion_pivotRowFade = (int32_t)kTokens[708].num;
inline constexpr int32_t motion_quickplay = (int32_t)kTokens[709].num;
inline constexpr int32_t motion_quickplayAngle = (int32_t)kTokens[710].num;
inline constexpr int32_t motion_quickplayScale = (int32_t)kTokens[711].num;
inline constexpr int32_t motion_quickplayAlpha = (int32_t)kTokens[712].num;
inline constexpr int32_t motion_quickplayMenuInset = (int32_t)kTokens[713].num;
inline constexpr int32_t motion_quickplayMenuX = (int32_t)kTokens[714].num;
inline constexpr int32_t motion_quickplayMenuY = (int32_t)kTokens[715].num;
inline constexpr int32_t motion_nowPlayingIn = (int32_t)kTokens[716].num;
inline constexpr int32_t motion_nowPlayingScale = (int32_t)kTokens[717].num;
inline constexpr int32_t motion_nowPlayingOriginX = (int32_t)kTokens[718].num;
inline constexpr int32_t motion_nowPlayingOriginY = (int32_t)kTokens[719].num;
inline constexpr int32_t motion_nowPlayingFade = (int32_t)kTokens[720].num;
inline constexpr int32_t motion_npDark = (int32_t)kTokens[721].num;
inline constexpr int32_t motion_t8OutDelay = (int32_t)kTokens[722].num;
inline constexpr int32_t motion_t8Out = (int32_t)kTokens[723].num;
inline constexpr int32_t motion_t8OutAngle = (int32_t)kTokens[724].num;
inline constexpr int32_t motion_t8OutEnd = (int32_t)kTokens[725].num;
inline constexpr int32_t motion_t8OutHinge = (int32_t)kTokens[726].num;
inline constexpr int32_t motion_t8OutEye = (int32_t)kTokens[727].num;
inline constexpr int32_t motion_t8InFade = (int32_t)kTokens[728].num;
inline constexpr int32_t motion_t8InDelay = (int32_t)kTokens[729].num;
inline constexpr int32_t motion_t8In = (int32_t)kTokens[730].num;
inline constexpr int32_t motion_t8InAngle = (int32_t)kTokens[731].num;
inline constexpr int32_t motion_t8InHinge = (int32_t)kTokens[732].num;
inline constexpr int32_t motion_t8InEye = (int32_t)kTokens[733].num;
inline constexpr int32_t motion_t8SlideX = (int32_t)kTokens[734].num;
inline constexpr int32_t motion_t8SlideY = (int32_t)kTokens[735].num;
inline constexpr int32_t motion_t8SlideDelay = (int32_t)kTokens[736].num;
inline constexpr int32_t motion_t8Slide = (int32_t)kTokens[737].num;
inline constexpr int32_t motion_t8PageFadeDelay = (int32_t)kTokens[738].num;
inline constexpr int32_t motion_t8PageFade = (int32_t)kTokens[739].num;
inline constexpr int32_t motion_t8PageX = (int32_t)kTokens[740].num;
inline constexpr int32_t motion_t8Page = (int32_t)kTokens[741].num;
inline constexpr int32_t motion_t7Leave = (int32_t)kTokens[742].num;
inline constexpr int32_t motion_t7Fade = (int32_t)kTokens[743].num;
inline constexpr int32_t motion_t7Angle = (int32_t)kTokens[744].num;
inline constexpr int32_t motion_t7TurnDelay = (int32_t)kTokens[745].num;
inline constexpr int32_t motion_t7Turn = (int32_t)kTokens[746].num;
inline constexpr int32_t motion_t7Rest = (int32_t)kTokens[747].num;
inline constexpr int32_t motion_t7Hinge = (int32_t)kTokens[748].num;
inline constexpr int32_t motion_t7Eye = (int32_t)kTokens[749].num;
inline constexpr int32_t motion_t7PaneLeft = (int32_t)kTokens[750].num;
inline constexpr int32_t motion_t7PaneRight = (int32_t)kTokens[751].num;
inline constexpr int32_t motion_t7PaneBack = (int32_t)kTokens[752].num;
inline constexpr int32_t motion_t7PaneShift = (int32_t)kTokens[753].num;
inline constexpr int32_t motion_t7PaneStep = (int32_t)kTokens[754].num;
inline constexpr int32_t motion_t7PaneAngle = (int32_t)kTokens[755].num;
inline constexpr int32_t motion_t7PaneDelay = (int32_t)kTokens[756].num;
inline constexpr int32_t motion_t7PaneTurn = (int32_t)kTokens[757].num;
inline constexpr int32_t motion_t7PaneDraw = (int32_t)kTokens[758].num;
inline constexpr int32_t motion_t7PaneEnd = (int32_t)kTokens[759].num;
inline constexpr int32_t motion_t7PaneInk = (int32_t)kTokens[760].num;
inline constexpr int32_t motion_t7PaneHinge = (int32_t)kTokens[761].num;
inline constexpr int32_t motion_t7RingX = (int32_t)kTokens[762].num;
inline constexpr int32_t motion_t7RingLeave = (int32_t)kTokens[763].num;
inline constexpr int32_t motion_t7RingMusicDelay = (int32_t)kTokens[764].num;
inline constexpr int32_t motion_t7RingMusic = (int32_t)kTokens[765].num;
inline constexpr int32_t motion_t7RingDark = (int32_t)kTokens[766].num;
inline constexpr const char* motion_npIn = kTokens[767].value;
inline constexpr int32_t motion_npPace = (int32_t)kTokens[768].num;
inline constexpr int32_t motion_npSlices = (int32_t)kTokens[769].num;
inline constexpr int32_t motion_npContentDelay = (int32_t)kTokens[770].num;
inline constexpr int32_t motion_trackHide = (int32_t)kTokens[771].num;
inline constexpr int32_t motion_trackShow = (int32_t)kTokens[772].num;
inline constexpr int32_t motion_trackArtScale = (int32_t)kTokens[773].num;
inline constexpr int32_t motion_trackArtOriginX = (int32_t)kTokens[774].num;
inline constexpr int32_t motion_trackArtOriginY = (int32_t)kTokens[775].num;
inline constexpr int32_t motion_trackShiftOut = (int32_t)kTokens[776].num;
inline constexpr int32_t motion_trackShiftIn = (int32_t)kTokens[777].num;
inline constexpr int32_t motion_npSlideLand = (int32_t)kTokens[778].num;
inline constexpr int32_t motion_npSlideTextDelay = (int32_t)kTokens[779].num;
inline constexpr int32_t motion_npSlideText = (int32_t)kTokens[780].num;
inline constexpr int32_t motion_variantFade = (int32_t)kTokens[781].num;
inline constexpr int32_t motion_sheet = (int32_t)kTokens[782].num;
inline constexpr int32_t motion_sheetScrim = (int32_t)kTokens[783].num;
inline constexpr int32_t motion_dialog = (int32_t)kTokens[784].num;
inline constexpr int32_t motion_dialogHide = (int32_t)kTokens[785].num;
inline constexpr int32_t motion_dialogScale = (int32_t)kTokens[786].num;
inline constexpr int32_t motion_dialogScrim = (int32_t)kTokens[787].num;
inline constexpr int32_t motion_swivelIn = (int32_t)kTokens[788].num;
inline constexpr int32_t motion_swivelOut = (int32_t)kTokens[789].num;
inline constexpr int32_t motion_swivelAngle = (int32_t)kTokens[790].num;
inline constexpr int32_t motion_swivelDim = (int32_t)kTokens[791].num;
inline constexpr int32_t motion_swivelHingeY = (int32_t)kTokens[792].num;
inline constexpr int32_t motion_jumpHold = (int32_t)kTokens[793].num;
inline constexpr int32_t motion_jumpFade = (int32_t)kTokens[794].num;
inline constexpr int32_t motion_jumpIn = (int32_t)kTokens[795].num;
inline constexpr int32_t motion_jumpOut = (int32_t)kTokens[796].num;
inline constexpr int32_t jump_emptyAlpha = (int32_t)kTokens[797].num;
inline constexpr int32_t motion_saver = (int32_t)kTokens[798].num;
inline constexpr int32_t motion_focusCursor = (int32_t)kTokens[799].num;
inline constexpr int32_t motion_focusAlpha = (int32_t)kTokens[800].num;
inline constexpr int32_t motion_scroll = (int32_t)kTokens[801].num;
inline constexpr int32_t motion_page = (int32_t)kTokens[802].num;
inline constexpr int32_t motion_press = (int32_t)kTokens[803].num;
inline constexpr int32_t motion_pressScale = (int32_t)kTokens[804].num;
inline constexpr int32_t motion_touchReturnDelay = (int32_t)kTokens[805].num;
inline constexpr int32_t motion_pressGlow = (int32_t)kTokens[806].num;
inline constexpr int32_t motion_flingRetention = (int32_t)kTokens[807].num;
inline constexpr int32_t motion_flingTick = (int32_t)kTokens[808].num;
inline constexpr int32_t motion_flingWindow = (int32_t)kTokens[809].num;
inline constexpr int32_t motion_flingMaxStep = (int32_t)kTokens[810].num;
inline constexpr int32_t motion_listFlingRetention = (int32_t)kTokens[811].num;
inline constexpr int32_t motion_qpFlingRetention = (int32_t)kTokens[812].num;
inline constexpr int32_t motion_qpFlingStop = (int32_t)kTokens[813].num;
inline constexpr int32_t motion_overscrollLead = (int32_t)kTokens[814].num;
inline constexpr int32_t motion_overscrollCap = (int32_t)kTokens[815].num;
inline constexpr int32_t motion_overscrollBackQp = (int32_t)kTokens[816].num;
inline constexpr int32_t motion_overscrollBackList = (int32_t)kTokens[817].num;
inline constexpr int32_t motion_overscrollLand = (int32_t)kTokens[818].num;
inline constexpr int32_t motion_toggle = (int32_t)kTokens[819].num;
inline constexpr int32_t motion_toggleScale = (int32_t)kTokens[820].num;
inline constexpr int32_t motion_toggleSwap = (int32_t)kTokens[821].num;
inline constexpr int32_t motion_idle = (int32_t)kTokens[822].num;
inline constexpr int32_t motion_idleFade = (int32_t)kTokens[823].num;
inline constexpr int32_t motion_idleAlpha = (int32_t)kTokens[824].num;
inline constexpr int32_t motion_progress = (int32_t)kTokens[825].num;
inline constexpr int32_t motion_elapsedTick = (int32_t)kTokens[826].num;
inline constexpr int32_t motion_valueChange = (int32_t)kTokens[827].num;
inline constexpr int32_t motion_valueDip = (int32_t)kTokens[828].num;
inline constexpr int32_t motion_pulse_t1 = (int32_t)kTokens[829].num;
inline constexpr int32_t motion_pulse_s1 = (int32_t)kTokens[830].num;
inline constexpr int32_t motion_pulse_t2 = (int32_t)kTokens[831].num;
inline constexpr int32_t motion_pulse_s2 = (int32_t)kTokens[832].num;
inline constexpr int32_t motion_pulse_t3 = (int32_t)kTokens[833].num;
inline constexpr int32_t motion_pulse_s3 = (int32_t)kTokens[834].num;
inline constexpr int32_t motion_pulse_t4 = (int32_t)kTokens[835].num;
inline constexpr int32_t motion_pulse_s4 = (int32_t)kTokens[836].num;
inline constexpr int32_t motion_volumeShow = (int32_t)kTokens[837].num;
inline constexpr int32_t motion_volumePop = (int32_t)kTokens[838].num;
inline constexpr int32_t motion_volumeHide = (int32_t)kTokens[839].num;
inline constexpr int32_t motion_volumeFade = (int32_t)kTokens[840].num;
inline constexpr int32_t motion_volumeOpen = (int32_t)kTokens[841].num;
inline constexpr int32_t motion_volumeGroup = (int32_t)kTokens[842].num;
inline constexpr int32_t motion_volumeGroupLead = (int32_t)kTokens[843].num;
inline constexpr int32_t motion_volumeGroupScale = (int32_t)kTokens[844].num;
inline constexpr int32_t motion_volumeGroupFade = (int32_t)kTokens[845].num;
inline constexpr int32_t motion_volumeNumberAt = (int32_t)kTokens[846].num;
inline constexpr int32_t motion_volumeNumberIn = (int32_t)kTokens[847].num;
inline constexpr int32_t motion_volumeExitAt = (int32_t)kTokens[848].num;
inline constexpr int32_t motion_volumeExitIn = (int32_t)kTokens[849].num;
inline constexpr int32_t motion_volumeTextAt = (int32_t)kTokens[850].num;
inline constexpr int32_t motion_volumeTextSlide = (int32_t)kTokens[851].num;
inline constexpr int32_t motion_volumeTextShift = (int32_t)kTokens[852].num;
inline constexpr int32_t motion_volumeTextFadeAt = (int32_t)kTokens[853].num;
inline constexpr int32_t motion_volumeTextFadeIn = (int32_t)kTokens[854].num;
inline constexpr int32_t motion_volumeOpenEnd = (int32_t)kTokens[855].num;
inline constexpr int32_t motion_volumeCloseSlowK = (int32_t)kTokens[856].num;
inline constexpr int32_t motion_volumeCloseK = (int32_t)kTokens[857].num;
inline constexpr int32_t motion_volumeCloseSize = (int32_t)kTokens[858].num;
inline constexpr int32_t motion_volumeNumberHold = (int32_t)kTokens[859].num;
inline constexpr int32_t motion_volumeNumberFade = (int32_t)kTokens[860].num;
inline constexpr int32_t motion_volumeNumberBack = (int32_t)kTokens[861].num;
inline constexpr int32_t motion_volumeRestHold = (int32_t)kTokens[862].num;
inline constexpr int32_t motion_volumeRestSlow = (int32_t)kTokens[863].num;
inline constexpr int32_t motion_volumeRestLevel = (int32_t)kTokens[864].num;
inline constexpr int32_t motion_volumeRestFast = (int32_t)kTokens[865].num;
inline constexpr int32_t motion_volumeScrimOut = (int32_t)kTokens[866].num;
inline constexpr int32_t motion_volumeTouchClose = (int32_t)kTokens[867].num;
inline constexpr int32_t motion_volumeCrossOut = (int32_t)kTokens[868].num;
inline constexpr int32_t motion_volumeCrossIn = (int32_t)kTokens[869].num;
inline constexpr int32_t motion_volumePressOff = (int32_t)kTokens[870].num;
inline constexpr int32_t motion_volumeTiltAngle = (int32_t)kTokens[871].num;
inline constexpr int32_t motion_volumeTiltDepth = (int32_t)kTokens[872].num;
inline constexpr int32_t motion_volumeTiltHold = (int32_t)kTokens[873].num;
inline constexpr int32_t motion_volumeTiltReturn = (int32_t)kTokens[874].num;
inline constexpr int32_t motion_toastIn = (int32_t)kTokens[875].num;
inline constexpr int32_t motion_toastHold = (int32_t)kTokens[876].num;
inline constexpr int32_t motion_toastOut = (int32_t)kTokens[877].num;
inline constexpr int32_t motion_toastTravel = (int32_t)kTokens[878].num;
inline constexpr int32_t motion_toastRise = (int32_t)kTokens[879].num;
inline constexpr int32_t motion_lyricSlide = (int32_t)kTokens[880].num;
inline constexpr int32_t motion_listChange = (int32_t)kTokens[881].num;
inline constexpr int32_t motion_listChangeStagger = (int32_t)kTokens[882].num;
inline constexpr int32_t motion_listChangeCap = (int32_t)kTokens[883].num;
inline constexpr int32_t motion_listChangeShift = (int32_t)kTokens[884].num;
inline constexpr int32_t motion_saverWordDelay = (int32_t)kTokens[885].num;
inline constexpr int32_t motion_saverWordEntry = (int32_t)kTokens[886].num;
inline constexpr int32_t motion_saverWordEntryShift = (int32_t)kTokens[887].num;
inline constexpr int32_t motion_saverWordFadeIn = (int32_t)kTokens[888].num;
inline constexpr int32_t saverGiant = (int32_t)kTokens[889].num;
inline constexpr int32_t saverGiantDigits = (int32_t)kTokens[890].num;
inline constexpr int32_t saverGiantCjk = (int32_t)kTokens[891].num;
inline constexpr int32_t saverLarge = (int32_t)kTokens[892].num;
inline constexpr int32_t saverLargeBlack = (int32_t)kTokens[893].num;
inline constexpr int32_t saverLargeCjk = (int32_t)kTokens[894].num;
inline constexpr int32_t saverDigits = (int32_t)kTokens[895].num;
inline constexpr int32_t saverRow = (int32_t)kTokens[896].num;
inline constexpr int32_t saverRowE = (int32_t)kTokens[897].num;
inline constexpr int32_t saverSmall = (int32_t)kTokens[898].num;
inline constexpr int32_t saverSmallE = (int32_t)kTokens[899].num;
inline constexpr int32_t saverB1Size = (int32_t)kTokens[900].num;
inline constexpr int32_t saverF2Size = (int32_t)kTokens[901].num;
inline constexpr int32_t saverS1Size = (int32_t)kTokens[902].num;
inline constexpr int32_t saverS4Size = (int32_t)kTokens[903].num;
inline constexpr int32_t saverS5Size = (int32_t)kTokens[904].num;
inline constexpr int32_t saverTileA5 = (int32_t)kTokens[905].num;
inline constexpr int32_t saverTileB5 = (int32_t)kTokens[906].num;
inline constexpr int32_t saverTileB7 = (int32_t)kTokens[907].num;
inline constexpr int32_t saverTileD5 = (int32_t)kTokens[908].num;
inline constexpr int32_t saverTileD6 = (int32_t)kTokens[909].num;
inline constexpr int32_t saverTileE5 = (int32_t)kTokens[910].num;
inline constexpr int32_t saverTileE5Tall = (int32_t)kTokens[911].num;
inline constexpr int32_t saverTileF4 = (int32_t)kTokens[912].num;
inline constexpr int32_t saverTileH1 = (int32_t)kTokens[913].num;
inline constexpr int32_t saverTileH7 = (int32_t)kTokens[914].num;
inline constexpr int32_t saverTileS3 = (int32_t)kTokens[915].num;
inline constexpr int32_t saverTileS = (int32_t)kTokens[916].num;
inline constexpr int32_t saverTileM = (int32_t)kTokens[917].num;
inline constexpr int32_t saverTileL = (int32_t)kTokens[918].num;
inline constexpr int32_t saverTileK3 = (int32_t)kTokens[919].num;
inline constexpr int32_t saverTileK6 = (int32_t)kTokens[920].num;
inline constexpr int32_t saverTileP4 = (int32_t)kTokens[921].num;
inline constexpr int32_t saverTileR4 = (int32_t)kTokens[922].num;
inline constexpr int32_t saverTileS6 = (int32_t)kTokens[923].num;
inline constexpr int32_t saverR1Size = (int32_t)kTokens[924].num;
inline constexpr int32_t saverR3Size = (int32_t)kTokens[925].num;
inline constexpr int32_t motion_saverA1Speed = (int32_t)kTokens[926].num;
inline constexpr int32_t motion_saverA1In = (int32_t)kTokens[927].num;
inline constexpr int32_t motion_saverA2Speed = (int32_t)kTokens[928].num;
inline constexpr int32_t motion_saverA2In = (int32_t)kTokens[929].num;
inline constexpr int32_t motion_saverA3Speed = (int32_t)kTokens[930].num;
inline constexpr int32_t motion_saverA3In = (int32_t)kTokens[931].num;
inline constexpr int32_t motion_saverA4Speed = (int32_t)kTokens[932].num;
inline constexpr int32_t motion_saverA4In = (int32_t)kTokens[933].num;
inline constexpr int32_t motion_saverA5Speed = (int32_t)kTokens[934].num;
inline constexpr int32_t motion_saverA5In = (int32_t)kTokens[935].num;
inline constexpr int32_t motion_saverA6Speed = (int32_t)kTokens[936].num;
inline constexpr int32_t motion_saverA6In = (int32_t)kTokens[937].num;
inline constexpr int32_t motion_saverB1Speed = (int32_t)kTokens[938].num;
inline constexpr int32_t motion_saverB1In = (int32_t)kTokens[939].num;
inline constexpr int32_t motion_saverB2Speed = (int32_t)kTokens[940].num;
inline constexpr int32_t motion_saverB2In = (int32_t)kTokens[941].num;
inline constexpr int32_t motion_saverB3Speed = (int32_t)kTokens[942].num;
inline constexpr int32_t motion_saverB3In = (int32_t)kTokens[943].num;
inline constexpr int32_t motion_saverB4Speed = (int32_t)kTokens[944].num;
inline constexpr int32_t motion_saverB4In = (int32_t)kTokens[945].num;
inline constexpr int32_t motion_saverB5Speed = (int32_t)kTokens[946].num;
inline constexpr int32_t motion_saverB5In = (int32_t)kTokens[947].num;
inline constexpr int32_t motion_saverB6Speed = (int32_t)kTokens[948].num;
inline constexpr int32_t motion_saverB6In = (int32_t)kTokens[949].num;
inline constexpr int32_t motion_saverB7Speed = (int32_t)kTokens[950].num;
inline constexpr int32_t motion_saverB7In = (int32_t)kTokens[951].num;
inline constexpr int32_t motion_saverD1Speed = (int32_t)kTokens[952].num;
inline constexpr int32_t motion_saverD1In = (int32_t)kTokens[953].num;
inline constexpr int32_t motion_saverD2Speed = (int32_t)kTokens[954].num;
inline constexpr int32_t motion_saverD2In = (int32_t)kTokens[955].num;
inline constexpr int32_t motion_saverD3Speed = (int32_t)kTokens[956].num;
inline constexpr int32_t motion_saverD3In = (int32_t)kTokens[957].num;
inline constexpr int32_t motion_saverD4Speed = (int32_t)kTokens[958].num;
inline constexpr int32_t motion_saverD4In = (int32_t)kTokens[959].num;
inline constexpr int32_t motion_saverD5Speed = (int32_t)kTokens[960].num;
inline constexpr int32_t motion_saverD5In = (int32_t)kTokens[961].num;
inline constexpr int32_t motion_saverD6Speed = (int32_t)kTokens[962].num;
inline constexpr int32_t motion_saverD6In = (int32_t)kTokens[963].num;
inline constexpr int32_t motion_saverE1Speed = (int32_t)kTokens[964].num;
inline constexpr int32_t motion_saverE1In = (int32_t)kTokens[965].num;
inline constexpr int32_t motion_saverE2Speed = (int32_t)kTokens[966].num;
inline constexpr int32_t motion_saverE2In = (int32_t)kTokens[967].num;
inline constexpr int32_t motion_saverE3Speed = (int32_t)kTokens[968].num;
inline constexpr int32_t motion_saverE3In = (int32_t)kTokens[969].num;
inline constexpr int32_t motion_saverE4Speed = (int32_t)kTokens[970].num;
inline constexpr int32_t motion_saverE4In = (int32_t)kTokens[971].num;
inline constexpr int32_t motion_saverE5Speed = (int32_t)kTokens[972].num;
inline constexpr int32_t motion_saverE5In = (int32_t)kTokens[973].num;
inline constexpr int32_t motion_saverE5On = (int32_t)kTokens[974].num;
inline constexpr int32_t motion_saverE5Off = (int32_t)kTokens[975].num;
inline constexpr int32_t motion_saverF1Speed = (int32_t)kTokens[976].num;
inline constexpr int32_t motion_saverF1In = (int32_t)kTokens[977].num;
inline constexpr int32_t motion_saverF2Speed = (int32_t)kTokens[978].num;
inline constexpr int32_t motion_saverF2In = (int32_t)kTokens[979].num;
inline constexpr int32_t motion_saverF3Speed = (int32_t)kTokens[980].num;
inline constexpr int32_t motion_saverF3In = (int32_t)kTokens[981].num;
inline constexpr int32_t motion_saverF4Speed = (int32_t)kTokens[982].num;
inline constexpr int32_t motion_saverF4In = (int32_t)kTokens[983].num;
inline constexpr int32_t motion_saverF5Speed = (int32_t)kTokens[984].num;
inline constexpr int32_t motion_saverF5In = (int32_t)kTokens[985].num;
inline constexpr int32_t motion_saverH1Speed = (int32_t)kTokens[986].num;
inline constexpr int32_t motion_saverH1In = (int32_t)kTokens[987].num;
inline constexpr int32_t motion_saverH2Speed = (int32_t)kTokens[988].num;
inline constexpr int32_t motion_saverH2In = (int32_t)kTokens[989].num;
inline constexpr int32_t motion_saverH3Speed = (int32_t)kTokens[990].num;
inline constexpr int32_t motion_saverH3In = (int32_t)kTokens[991].num;
inline constexpr int32_t motion_saverH4Speed = (int32_t)kTokens[992].num;
inline constexpr int32_t motion_saverH4In = (int32_t)kTokens[993].num;
inline constexpr int32_t motion_saverH5Speed = (int32_t)kTokens[994].num;
inline constexpr int32_t motion_saverH5In = (int32_t)kTokens[995].num;
inline constexpr int32_t motion_saverH6Speed = (int32_t)kTokens[996].num;
inline constexpr int32_t motion_saverH6In = (int32_t)kTokens[997].num;
inline constexpr int32_t motion_saverH7Speed = (int32_t)kTokens[998].num;
inline constexpr int32_t motion_saverH7In = (int32_t)kTokens[999].num;
inline constexpr int32_t motion_saverH8Speed = (int32_t)kTokens[1000].num;
inline constexpr int32_t motion_saverH8In = (int32_t)kTokens[1001].num;
inline constexpr int32_t motion_saverS1Speed = (int32_t)kTokens[1002].num;
inline constexpr int32_t motion_saverS1In = (int32_t)kTokens[1003].num;
inline constexpr int32_t motion_saverS2Speed = (int32_t)kTokens[1004].num;
inline constexpr int32_t motion_saverS2In = (int32_t)kTokens[1005].num;
inline constexpr int32_t motion_saverS3Speed = (int32_t)kTokens[1006].num;
inline constexpr int32_t motion_saverS3In = (int32_t)kTokens[1007].num;
inline constexpr int32_t motion_saverS4Speed = (int32_t)kTokens[1008].num;
inline constexpr int32_t motion_saverS4In = (int32_t)kTokens[1009].num;
inline constexpr int32_t motion_saverS5Speed = (int32_t)kTokens[1010].num;
inline constexpr int32_t motion_saverS5In = (int32_t)kTokens[1011].num;
inline constexpr int32_t motion_saverS6Speed = (int32_t)kTokens[1012].num;
inline constexpr int32_t motion_saverS6In = (int32_t)kTokens[1013].num;
inline constexpr int32_t motion_saverS7Speed = (int32_t)kTokens[1014].num;
inline constexpr int32_t motion_saverS7In = (int32_t)kTokens[1015].num;
inline constexpr int32_t motion_saverK1Speed = (int32_t)kTokens[1016].num;
inline constexpr int32_t motion_saverK1In = (int32_t)kTokens[1017].num;
inline constexpr int32_t motion_saverK2Speed = (int32_t)kTokens[1018].num;
inline constexpr int32_t motion_saverK2In = (int32_t)kTokens[1019].num;
inline constexpr int32_t motion_saverK3Speed = (int32_t)kTokens[1020].num;
inline constexpr int32_t motion_saverK3In = (int32_t)kTokens[1021].num;
inline constexpr int32_t motion_saverK4Speed = (int32_t)kTokens[1022].num;
inline constexpr int32_t motion_saverK4In = (int32_t)kTokens[1023].num;
inline constexpr int32_t motion_saverK5Speed = (int32_t)kTokens[1024].num;
inline constexpr int32_t motion_saverK5In = (int32_t)kTokens[1025].num;
inline constexpr int32_t motion_saverK6Speed = (int32_t)kTokens[1026].num;
inline constexpr int32_t motion_saverK6In = (int32_t)kTokens[1027].num;
inline constexpr int32_t motion_saverP1Speed = (int32_t)kTokens[1028].num;
inline constexpr int32_t motion_saverP1In = (int32_t)kTokens[1029].num;
inline constexpr int32_t motion_saverP2Speed = (int32_t)kTokens[1030].num;
inline constexpr int32_t motion_saverP2In = (int32_t)kTokens[1031].num;
inline constexpr int32_t motion_saverP3Speed = (int32_t)kTokens[1032].num;
inline constexpr int32_t motion_saverP3In = (int32_t)kTokens[1033].num;
inline constexpr int32_t motion_saverP4Speed = (int32_t)kTokens[1034].num;
inline constexpr int32_t motion_saverP4In = (int32_t)kTokens[1035].num;
inline constexpr int32_t motion_saverP5Speed = (int32_t)kTokens[1036].num;
inline constexpr int32_t motion_saverP5In = (int32_t)kTokens[1037].num;
inline constexpr int32_t motion_saverR1Speed = (int32_t)kTokens[1038].num;
inline constexpr int32_t motion_saverR1In = (int32_t)kTokens[1039].num;
inline constexpr int32_t motion_saverR2Speed = (int32_t)kTokens[1040].num;
inline constexpr int32_t motion_saverR2In = (int32_t)kTokens[1041].num;
inline constexpr int32_t motion_saverR3Speed = (int32_t)kTokens[1042].num;
inline constexpr int32_t motion_saverR3In = (int32_t)kTokens[1043].num;
inline constexpr int32_t motion_saverR4Speed = (int32_t)kTokens[1044].num;
inline constexpr int32_t motion_saverR4In = (int32_t)kTokens[1045].num;
inline constexpr int32_t motion_saverJ1Speed = (int32_t)kTokens[1046].num;
inline constexpr int32_t motion_saverJ1In = (int32_t)kTokens[1047].num;
inline constexpr int32_t motion_saverJ2Speed = (int32_t)kTokens[1048].num;
inline constexpr int32_t motion_saverJ2In = (int32_t)kTokens[1049].num;
inline constexpr int32_t motion_saverJ3Speed = (int32_t)kTokens[1050].num;
inline constexpr int32_t motion_saverJ3In = (int32_t)kTokens[1051].num;
inline constexpr int32_t motion_saverJ4Speed = (int32_t)kTokens[1052].num;
inline constexpr int32_t motion_saverJ4In = (int32_t)kTokens[1053].num;
inline constexpr int32_t motion_saverJ5Speed = (int32_t)kTokens[1054].num;
inline constexpr int32_t motion_saverJ5In = (int32_t)kTokens[1055].num;
inline constexpr int32_t motion_saverJ6Speed = (int32_t)kTokens[1056].num;
inline constexpr int32_t motion_saverJ6In = (int32_t)kTokens[1057].num;
inline constexpr int32_t motion_saverL1Speed = (int32_t)kTokens[1058].num;
inline constexpr int32_t motion_saverL1In = (int32_t)kTokens[1059].num;
inline constexpr int32_t motion_saverL2Speed = (int32_t)kTokens[1060].num;
inline constexpr int32_t motion_saverL2In = (int32_t)kTokens[1061].num;
inline constexpr int32_t motion_saverL3Speed = (int32_t)kTokens[1062].num;
inline constexpr int32_t motion_saverL3In = (int32_t)kTokens[1063].num;
inline constexpr int32_t motion_saverL4Speed = (int32_t)kTokens[1064].num;
inline constexpr int32_t motion_saverL4In = (int32_t)kTokens[1065].num;
inline constexpr int32_t motion_saverL5Speed = (int32_t)kTokens[1066].num;
inline constexpr int32_t motion_saverL5In = (int32_t)kTokens[1067].num;
inline constexpr int32_t motion_saverM1Speed = (int32_t)kTokens[1068].num;
inline constexpr int32_t motion_saverM1In = (int32_t)kTokens[1069].num;
inline constexpr int32_t motion_saverM2Speed = (int32_t)kTokens[1070].num;
inline constexpr int32_t motion_saverM2In = (int32_t)kTokens[1071].num;
inline constexpr int32_t motion_saverM3Speed = (int32_t)kTokens[1072].num;
inline constexpr int32_t motion_saverM3In = (int32_t)kTokens[1073].num;
inline constexpr int32_t motion_saverM4Speed = (int32_t)kTokens[1074].num;
inline constexpr int32_t motion_saverM4In = (int32_t)kTokens[1075].num;
inline constexpr int32_t motion_saverM5Speed = (int32_t)kTokens[1076].num;
inline constexpr int32_t motion_saverM5In = (int32_t)kTokens[1077].num;
inline constexpr int32_t motion_saverM6Speed = (int32_t)kTokens[1078].num;
inline constexpr int32_t motion_saverM6In = (int32_t)kTokens[1079].num;
inline constexpr int32_t motion_saverN1Speed = (int32_t)kTokens[1080].num;
inline constexpr int32_t motion_saverN1In = (int32_t)kTokens[1081].num;
inline constexpr int32_t motion_saverN2Speed = (int32_t)kTokens[1082].num;
inline constexpr int32_t motion_saverN2In = (int32_t)kTokens[1083].num;
inline constexpr int32_t motion_saverN3Speed = (int32_t)kTokens[1084].num;
inline constexpr int32_t motion_saverN3In = (int32_t)kTokens[1085].num;
inline constexpr int32_t motion_saverN4Speed = (int32_t)kTokens[1086].num;
inline constexpr int32_t motion_saverN4In = (int32_t)kTokens[1087].num;
inline constexpr int32_t motion_saverN5Speed = (int32_t)kTokens[1088].num;
inline constexpr int32_t motion_saverN5In = (int32_t)kTokens[1089].num;
inline constexpr int32_t motion_saverN6Speed = (int32_t)kTokens[1090].num;
inline constexpr int32_t motion_saverN6In = (int32_t)kTokens[1091].num;
inline constexpr int32_t motion_saverQ1Speed = (int32_t)kTokens[1092].num;
inline constexpr int32_t motion_saverQ1In = (int32_t)kTokens[1093].num;
inline constexpr int32_t motion_saverQ2Speed = (int32_t)kTokens[1094].num;
inline constexpr int32_t motion_saverQ2In = (int32_t)kTokens[1095].num;
inline constexpr int32_t motion_saverQ3Speed = (int32_t)kTokens[1096].num;
inline constexpr int32_t motion_saverQ3In = (int32_t)kTokens[1097].num;
inline constexpr int32_t motion_saverQ4Speed = (int32_t)kTokens[1098].num;
inline constexpr int32_t motion_saverQ4In = (int32_t)kTokens[1099].num;
inline constexpr int32_t motion_saverQ5Speed = (int32_t)kTokens[1100].num;
inline constexpr int32_t motion_saverQ5In = (int32_t)kTokens[1101].num;
inline constexpr int32_t motion_saverQ6Speed = (int32_t)kTokens[1102].num;
inline constexpr int32_t motion_saverQ6In = (int32_t)kTokens[1103].num;
inline constexpr int32_t motion_saverQ7Speed = (int32_t)kTokens[1104].num;
inline constexpr int32_t motion_saverQ7In = (int32_t)kTokens[1105].num;
inline constexpr int32_t motion_saverALength = (int32_t)kTokens[1106].num;
inline constexpr int32_t motion_saverBLength = (int32_t)kTokens[1107].num;
inline constexpr int32_t motion_saverDLength = (int32_t)kTokens[1108].num;
inline constexpr int32_t motion_saverELength = (int32_t)kTokens[1109].num;
inline constexpr int32_t motion_saverFLength = (int32_t)kTokens[1110].num;
inline constexpr int32_t motion_saverHLength = (int32_t)kTokens[1111].num;
inline constexpr int32_t motion_saverSLength = (int32_t)kTokens[1112].num;
inline constexpr int32_t motion_saverKLength = (int32_t)kTokens[1113].num;
inline constexpr int32_t motion_saverPLength = (int32_t)kTokens[1114].num;
inline constexpr int32_t motion_saverRLength = (int32_t)kTokens[1115].num;
inline constexpr int32_t motion_saverJLength = (int32_t)kTokens[1116].num;
inline constexpr int32_t motion_saverLLength = (int32_t)kTokens[1117].num;
inline constexpr int32_t motion_saverMLength = (int32_t)kTokens[1118].num;
inline constexpr int32_t motion_saverNLength = (int32_t)kTokens[1119].num;
inline constexpr int32_t motion_saverQLength = (int32_t)kTokens[1120].num;
inline constexpr int32_t motion_saverH8Stop = (int32_t)kTokens[1121].num;
inline constexpr int32_t motion_saverA3Stop = (int32_t)kTokens[1122].num;
inline constexpr int32_t motion_saverS5Stop = (int32_t)kTokens[1123].num;
inline constexpr int32_t motion_saverK4Stop = (int32_t)kTokens[1124].num;
inline constexpr int32_t motion_saverP2Stop = (int32_t)kTokens[1125].num;
inline constexpr int32_t motion_saverR2Stop = (int32_t)kTokens[1126].num;
inline constexpr int32_t saverD1Join = (int32_t)kTokens[1127].num;
inline constexpr int32_t saverB4Period = (int32_t)kTokens[1128].num;
inline constexpr int32_t saverE4Period = (int32_t)kTokens[1129].num;
inline constexpr int32_t saverS4Period = (int32_t)kTokens[1130].num;
inline constexpr int32_t saverE2Period = (int32_t)kTokens[1131].num;
inline constexpr int32_t saverF1Gap = (int32_t)kTokens[1132].num;
inline constexpr int32_t saverF2Gap = (int32_t)kTokens[1133].num;
inline constexpr int32_t saverF1Trail = (int32_t)kTokens[1134].num;
inline constexpr int32_t saverF2Trail = (int32_t)kTokens[1135].num;
inline constexpr int32_t motion_saverWhoosh = (int32_t)kTokens[1136].num;
inline constexpr int32_t motion_saverWhooshSpeed = (int32_t)kTokens[1137].num;
inline constexpr int32_t motion_saverPWhooshSpeed = (int32_t)kTokens[1138].num;
inline constexpr int32_t motion_saverGapMin = (int32_t)kTokens[1139].num;
inline constexpr int32_t motion_saverGapMax = (int32_t)kTokens[1140].num;
inline constexpr int32_t motion_saverTileIn = (int32_t)kTokens[1141].num;
inline constexpr int32_t motion_saverE1Entry = (int32_t)kTokens[1142].num;
inline constexpr int32_t saverE1Shift = (int32_t)kTokens[1143].num;
inline constexpr int32_t motion_saverA1Entry = (int32_t)kTokens[1144].num;
inline constexpr int32_t saverA1Shift = (int32_t)kTokens[1145].num;
inline constexpr int32_t motion_saverF1Entry = (int32_t)kTokens[1146].num;
inline constexpr int32_t saverF1Shift = (int32_t)kTokens[1147].num;
inline constexpr int32_t motion_saverF2Entry = (int32_t)kTokens[1148].num;
inline constexpr int32_t saverF2Shift = (int32_t)kTokens[1149].num;
inline constexpr int32_t motion_saverK1Entry = (int32_t)kTokens[1150].num;
inline constexpr int32_t saverK1Shift = (int32_t)kTokens[1151].num;
inline constexpr int32_t motion_saverP1Entry = (int32_t)kTokens[1152].num;
inline constexpr int32_t saverP1Shift = (int32_t)kTokens[1153].num;
inline constexpr int32_t motion_saverR1Entry = (int32_t)kTokens[1154].num;
inline constexpr int32_t saverR1Shift = (int32_t)kTokens[1155].num;
inline constexpr int32_t motion_saverFFade = (int32_t)kTokens[1156].num;
inline constexpr int32_t motion_saverNpIn = (int32_t)kTokens[1157].num;
inline constexpr int32_t motion_saverNpContentIn = (int32_t)kTokens[1158].num;
inline constexpr int32_t motion_saverNpBackDelay = (int32_t)kTokens[1159].num;
inline constexpr int32_t motion_saverNpRingsDelay = (int32_t)kTokens[1160].num;
inline constexpr int32_t motion_saverNpHold = (int32_t)kTokens[1161].num;
inline constexpr int32_t motion_saverNpOut = (int32_t)kTokens[1162].num;
inline constexpr int32_t motion_saverNpRingsOut = (int32_t)kTokens[1163].num;
inline constexpr int32_t motion_saverNpBlack = (int32_t)kTokens[1164].num;
inline constexpr int32_t motion_saverFade = (int32_t)kTokens[1165].num;
inline constexpr int32_t motion_saverFadeBack = (int32_t)kTokens[1166].num;
inline constexpr int32_t motion_saverFadeRings = (int32_t)kTokens[1167].num;
inline constexpr int32_t motion_saverFadeTop = (int32_t)kTokens[1168].num;
inline constexpr int32_t motion_saverFadeTopShift = (int32_t)kTokens[1169].num;
inline constexpr const char* motion_saverFadeForm = kTokens[1170].value;
inline constexpr int32_t motion_saverKeyWindow = (int32_t)kTokens[1171].num;
inline constexpr int32_t motion_saverLyrSpeedMin = (int32_t)kTokens[1172].num;
inline constexpr int32_t motion_saverLyrSpeedMax = (int32_t)kTokens[1173].num;
inline constexpr int32_t motion_saverLyrSlide = (int32_t)kTokens[1174].num;
inline constexpr int32_t motion_saverLyrSlideMin = (int32_t)kTokens[1175].num;
inline constexpr int32_t motion_saverLyrLead = (int32_t)kTokens[1176].num;
inline constexpr int32_t motion_saverLyrLook = (int32_t)kTokens[1177].num;
inline constexpr int32_t motion_saverLyrRead = (int32_t)kTokens[1178].num;
inline constexpr int32_t motion_saverLyrBreak = (int32_t)kTokens[1179].num;
inline constexpr int32_t motion_saverLyrScene = (int32_t)kTokens[1180].num;
inline constexpr int32_t motion_saverLyrSectionMin = (int32_t)kTokens[1181].num;
inline constexpr int32_t motion_saverLyrSectionMax = (int32_t)kTokens[1182].num;
inline constexpr int32_t motion_saverLyrGrace = (int32_t)kTokens[1183].num;
inline constexpr int32_t motion_saverLyrSlew = (int32_t)kTokens[1184].num;
inline constexpr int32_t motion_saverLyrSeek = (int32_t)kTokens[1185].num;
inline constexpr int32_t saverLyrGap = (int32_t)kTokens[1186].num;
inline constexpr int32_t saverLyrSizeSum = (int32_t)kTokens[1187].num;
inline constexpr int32_t saverLyrSizeSumUp = (int32_t)kTokens[1188].num;
inline constexpr int32_t motion_saverLyrPoolGiantDiagLo = (int32_t)kTokens[1189].num;
inline constexpr int32_t motion_saverLyrPoolGiantDiagHi = (int32_t)kTokens[1190].num;
inline constexpr int32_t motion_saverLyrPoolGiantUpLo = (int32_t)kTokens[1191].num;
inline constexpr int32_t motion_saverLyrPoolGiantUpHi = (int32_t)kTokens[1192].num;
inline constexpr int32_t motion_saverLyrPoolRowDiagLo = (int32_t)kTokens[1193].num;
inline constexpr int32_t motion_saverLyrPoolRowDiagHi = (int32_t)kTokens[1194].num;
inline constexpr int32_t motion_saverLyrPoolRowUpLo = (int32_t)kTokens[1195].num;
inline constexpr int32_t motion_saverLyrPoolRowUpHi = (int32_t)kTokens[1196].num;
inline constexpr int32_t motion_saverLyrPoolLargeLo = (int32_t)kTokens[1197].num;
inline constexpr int32_t motion_saverLyrPoolLargeHi = (int32_t)kTokens[1198].num;
inline constexpr int32_t motion_saverLyrPoolSmallLo = (int32_t)kTokens[1199].num;
inline constexpr int32_t motion_saverLyrPoolSmallHi = (int32_t)kTokens[1200].num;
inline constexpr const char* motion_npWakeZoom = kTokens[1201].value;
inline constexpr int32_t motion_npWake = (int32_t)kTokens[1202].num;
inline constexpr int32_t motion_npWakeScale = (int32_t)kTokens[1203].num;
inline constexpr int32_t motion_npWakeTitleScale = (int32_t)kTokens[1204].num;
inline constexpr int32_t motion_npWakeContentDelay = (int32_t)kTokens[1205].num;
inline constexpr int32_t motion_npWakeContent = (int32_t)kTokens[1206].num;
inline constexpr int32_t motion_npWakeNextDelay = (int32_t)kTokens[1207].num;
inline constexpr int32_t motion_npWakeBackDelay = (int32_t)kTokens[1208].num;
inline constexpr int32_t motion_npWakeBack = (int32_t)kTokens[1209].num;
inline constexpr int32_t motion_npWakeRingsDelay = (int32_t)kTokens[1210].num;
inline constexpr int32_t motion_npWakeRings = (int32_t)kTokens[1211].num;
inline constexpr int32_t motion_npWakeLight = (int32_t)kTokens[1212].num;
inline constexpr int32_t npWakeTitleX = (int32_t)kTokens[1213].num;
inline constexpr int32_t npWakeTitleDrop = (int32_t)kTokens[1214].num;
inline constexpr int32_t saver_aheadMs = (int32_t)kTokens[1215].num;
inline constexpr int32_t saverA1Across = (int32_t)kTokens[1216].num;
inline constexpr int32_t saverA2Across = (int32_t)kTokens[1217].num;
inline constexpr int32_t saverA3Across = (int32_t)kTokens[1218].num;
inline constexpr int32_t saverA4Across = (int32_t)kTokens[1219].num;
inline constexpr int32_t saverA5Across = (int32_t)kTokens[1220].num;
inline constexpr int32_t saverA6Across = (int32_t)kTokens[1221].num;
inline constexpr int32_t saverB1Across = (int32_t)kTokens[1222].num;
inline constexpr int32_t saverB2Across = (int32_t)kTokens[1223].num;
inline constexpr int32_t saverB3Across = (int32_t)kTokens[1224].num;
inline constexpr int32_t saverB4Across = (int32_t)kTokens[1225].num;
inline constexpr int32_t saverB5Across = (int32_t)kTokens[1226].num;
inline constexpr int32_t saverB6Across = (int32_t)kTokens[1227].num;
inline constexpr int32_t saverB7Across = (int32_t)kTokens[1228].num;
inline constexpr int32_t saverD1Across = (int32_t)kTokens[1229].num;
inline constexpr int32_t saverD2Across = (int32_t)kTokens[1230].num;
inline constexpr int32_t saverD3Across = (int32_t)kTokens[1231].num;
inline constexpr int32_t saverD4Across = (int32_t)kTokens[1232].num;
inline constexpr int32_t saverD5Across = (int32_t)kTokens[1233].num;
inline constexpr int32_t saverD6Across = (int32_t)kTokens[1234].num;
inline constexpr int32_t saverE1Across = (int32_t)kTokens[1235].num;
inline constexpr int32_t saverE2Across = (int32_t)kTokens[1236].num;
inline constexpr int32_t saverE3Across = (int32_t)kTokens[1237].num;
inline constexpr int32_t saverE5Across = (int32_t)kTokens[1238].num;
inline constexpr int32_t saverF1Across = (int32_t)kTokens[1239].num;
inline constexpr int32_t saverF2Across = (int32_t)kTokens[1240].num;
inline constexpr int32_t saverF3Across = (int32_t)kTokens[1241].num;
inline constexpr int32_t saverF4Across = (int32_t)kTokens[1242].num;
inline constexpr int32_t saverF5Across = (int32_t)kTokens[1243].num;
inline constexpr int32_t saverH1Across = (int32_t)kTokens[1244].num;
inline constexpr int32_t saverH2Across = (int32_t)kTokens[1245].num;
inline constexpr int32_t saverH3Across = (int32_t)kTokens[1246].num;
inline constexpr int32_t saverH4Across = (int32_t)kTokens[1247].num;
inline constexpr int32_t saverH5Across = (int32_t)kTokens[1248].num;
inline constexpr int32_t saverH6Across = (int32_t)kTokens[1249].num;
inline constexpr int32_t saverH7Across = (int32_t)kTokens[1250].num;
inline constexpr int32_t saverH8Across = (int32_t)kTokens[1251].num;
inline constexpr int32_t saverS1Across = (int32_t)kTokens[1252].num;
inline constexpr int32_t saverS2Across = (int32_t)kTokens[1253].num;
inline constexpr int32_t saverS3Across = (int32_t)kTokens[1254].num;
inline constexpr int32_t saverS4Across = (int32_t)kTokens[1255].num;
inline constexpr int32_t saverS5Across = (int32_t)kTokens[1256].num;
inline constexpr int32_t saverS6Across = (int32_t)kTokens[1257].num;
inline constexpr int32_t saverS7Across = (int32_t)kTokens[1258].num;
inline constexpr int32_t saverK1Across = (int32_t)kTokens[1259].num;
inline constexpr int32_t saverK2Across = (int32_t)kTokens[1260].num;
inline constexpr int32_t saverK3Across = (int32_t)kTokens[1261].num;
inline constexpr int32_t saverK4Across = (int32_t)kTokens[1262].num;
inline constexpr int32_t saverK5Across = (int32_t)kTokens[1263].num;
inline constexpr int32_t saverK6Across = (int32_t)kTokens[1264].num;
inline constexpr int32_t saverP1Across = (int32_t)kTokens[1265].num;
inline constexpr int32_t saverP2Across = (int32_t)kTokens[1266].num;
inline constexpr int32_t saverP3Across = (int32_t)kTokens[1267].num;
inline constexpr int32_t saverP4Across = (int32_t)kTokens[1268].num;
inline constexpr int32_t saverP5Across = (int32_t)kTokens[1269].num;
inline constexpr int32_t saverR1Across = (int32_t)kTokens[1270].num;
inline constexpr int32_t saverR2Across = (int32_t)kTokens[1271].num;
inline constexpr int32_t saverR3Across = (int32_t)kTokens[1272].num;
inline constexpr int32_t saverR4Across = (int32_t)kTokens[1273].num;
inline constexpr int32_t saverJ1Across = (int32_t)kTokens[1274].num;
inline constexpr int32_t saverJ2Across = (int32_t)kTokens[1275].num;
inline constexpr int32_t saverJ3Across = (int32_t)kTokens[1276].num;
inline constexpr int32_t saverJ4Across = (int32_t)kTokens[1277].num;
inline constexpr int32_t saverJ5Across = (int32_t)kTokens[1278].num;
inline constexpr int32_t saverJ6Across = (int32_t)kTokens[1279].num;
inline constexpr int32_t saverL1Across = (int32_t)kTokens[1280].num;
inline constexpr int32_t saverL2Across = (int32_t)kTokens[1281].num;
inline constexpr int32_t saverL3Across = (int32_t)kTokens[1282].num;
inline constexpr int32_t saverL4Across = (int32_t)kTokens[1283].num;
inline constexpr int32_t saverL5Across = (int32_t)kTokens[1284].num;
inline constexpr int32_t saverM1Across = (int32_t)kTokens[1285].num;
inline constexpr int32_t saverM2Across = (int32_t)kTokens[1286].num;
inline constexpr int32_t saverM3Across = (int32_t)kTokens[1287].num;
inline constexpr int32_t saverM4Across = (int32_t)kTokens[1288].num;
inline constexpr int32_t saverM5Across = (int32_t)kTokens[1289].num;
inline constexpr int32_t saverM6Across = (int32_t)kTokens[1290].num;
inline constexpr int32_t saverN1Across = (int32_t)kTokens[1291].num;
inline constexpr int32_t saverN2Across = (int32_t)kTokens[1292].num;
inline constexpr int32_t saverN3Across = (int32_t)kTokens[1293].num;
inline constexpr int32_t saverN4Across = (int32_t)kTokens[1294].num;
inline constexpr int32_t saverN5Across = (int32_t)kTokens[1295].num;
inline constexpr int32_t saverN6Across = (int32_t)kTokens[1296].num;
inline constexpr int32_t saverQ1Across = (int32_t)kTokens[1297].num;
inline constexpr int32_t saverQ2Across = (int32_t)kTokens[1298].num;
inline constexpr int32_t saverQ3Across = (int32_t)kTokens[1299].num;
inline constexpr int32_t saverQ4Across = (int32_t)kTokens[1300].num;
inline constexpr int32_t saverQ5Across = (int32_t)kTokens[1301].num;
inline constexpr int32_t saverQ6Across = (int32_t)kTokens[1302].num;
inline constexpr int32_t saverQ7Across = (int32_t)kTokens[1303].num;
inline constexpr int32_t motion_backdropFade = (int32_t)kTokens[1304].num;
inline constexpr int32_t motion_lockCommit = (int32_t)kTokens[1305].num;
inline constexpr int32_t motion_lockSliverDelay = (int32_t)kTokens[1306].num;
inline constexpr int32_t motion_lockSliver = (int32_t)kTokens[1307].num;
inline constexpr int32_t motion_lockSliverAlpha = (int32_t)kTokens[1308].num;
inline constexpr int32_t motion_decel_x1 = (int32_t)kTokens[1309].num;
inline constexpr int32_t motion_decel_y1 = (int32_t)kTokens[1310].num;
inline constexpr int32_t motion_decel_x2 = (int32_t)kTokens[1311].num;
inline constexpr int32_t motion_decel_y2 = (int32_t)kTokens[1312].num;
inline constexpr int32_t motion_strip_x1 = (int32_t)kTokens[1313].num;
inline constexpr int32_t motion_strip_y1 = (int32_t)kTokens[1314].num;
inline constexpr int32_t motion_strip_x2 = (int32_t)kTokens[1315].num;
inline constexpr int32_t motion_strip_y2 = (int32_t)kTokens[1316].num;
inline constexpr int32_t input_keyHoldMs = (int32_t)kTokens[1317].num;
inline constexpr int32_t input_volumeHoldMs = (int32_t)kTokens[1318].num;
inline constexpr int32_t input_volumeRepeatMs = (int32_t)kTokens[1319].num;
inline constexpr int32_t input_remoteDoubleMs = (int32_t)kTokens[1320].num;
inline constexpr int32_t input_remoteHoldMs = (int32_t)kTokens[1321].num;
inline constexpr int32_t input_knobDoubleMs = (int32_t)kTokens[1322].num;
inline constexpr int32_t input_knobHoldMs = (int32_t)kTokens[1323].num;
inline constexpr int32_t input_knobHoldLongMs = (int32_t)kTokens[1324].num;
inline constexpr int32_t input_modeExitHoldMs = (int32_t)kTokens[1325].num;
inline constexpr int32_t input_searchDebounceMs = (int32_t)kTokens[1326].num;
inline constexpr int32_t power_screenOffDefaultS = (int32_t)kTokens[1327].num;
inline constexpr int32_t history_rows = (int32_t)kTokens[1328].num;
inline constexpr int32_t history_tableCap = (int32_t)kTokens[1329].num;
inline constexpr int32_t quickplay_tiles = (int32_t)kTokens[1330].num;
inline constexpr int32_t bt_pairingS = (int32_t)kTokens[1331].num;
inline constexpr int32_t bt_discoverS = (int32_t)kTokens[1332].num;
inline constexpr int32_t btNameDrop = (int32_t)kTokens[1333].num;
inline constexpr int32_t btStateDrop = (int32_t)kTokens[1334].num;
inline constexpr int32_t btRowDrop = (int32_t)kTokens[1335].num;
inline constexpr int32_t btRingDrop = (int32_t)kTokens[1336].num;
inline constexpr int32_t eq_preampMin = (int32_t)kTokens[1337].num;
inline constexpr int32_t eq_preampMax = (int32_t)kTokens[1338].num;
inline constexpr int32_t eq_gainMin = (int32_t)kTokens[1339].num;
inline constexpr int32_t eq_gainMax = (int32_t)kTokens[1340].num;
inline constexpr int32_t eq_qMin = (int32_t)kTokens[1341].num;
inline constexpr int32_t eq_qMax = (int32_t)kTokens[1342].num;
inline constexpr int32_t eq_freqMin = (int32_t)kTokens[1343].num;
inline constexpr int32_t eq_freqMax = (int32_t)kTokens[1344].num;
inline constexpr int32_t eq_freqPoints = (int32_t)kTokens[1345].num;
inline constexpr int32_t eq_bands = (int32_t)kTokens[1346].num;
inline constexpr int32_t eq_profilesMax = (int32_t)kTokens[1347].num;
inline constexpr int32_t eq_importsMax = (int32_t)kTokens[1348].num;
inline constexpr int32_t settings_backupsMax = (int32_t)kTokens[1349].num;
inline constexpr int32_t settings_backupMaxKib = (int32_t)kTokens[1350].num;
inline constexpr int32_t eq_fileMaxKib = (int32_t)kTokens[1351].num;
inline constexpr int32_t eq_importMs = (int32_t)kTokens[1352].num;
inline constexpr int32_t volume_stepDefault = (int32_t)kTokens[1353].num;
inline constexpr const char* settings_volumeSteps = kTokens[1354].value;
inline constexpr const char* settings_saverSteps = kTokens[1355].value;
inline constexpr const char* settings_screenOffSteps = kTokens[1356].value;
inline constexpr const char* settings_sleepSteps = kTokens[1357].value;
inline constexpr const char* settings_idleSteps = kTokens[1358].value;
inline constexpr int32_t settings_saverStart = (int32_t)kTokens[1359].num;
inline constexpr int32_t settings_screenOffDefault = (int32_t)kTokens[1360].num;
inline constexpr int32_t settings_idleDefault = (int32_t)kTokens[1361].num;
inline constexpr int32_t settings_writeDelay = (int32_t)kTokens[1362].num;
inline constexpr int32_t settings_yearMin = (int32_t)kTokens[1363].num;
inline constexpr int32_t settings_yearMax = (int32_t)kTokens[1364].num;
inline constexpr int32_t settings_clockYearMin = (int32_t)kTokens[1365].num;
inline constexpr int32_t update_batteryMin = (int32_t)kTokens[1366].num;
inline constexpr int32_t volume_percentDbDivisor = (int32_t)kTokens[1367].num;
inline constexpr int32_t settings_filterCount = (int32_t)kTokens[1368].num;
inline constexpr int32_t settings_balanceMax = (int32_t)kTokens[1369].num;
inline constexpr int32_t settings_balanceStep = (int32_t)kTokens[1370].num;
inline constexpr const char* settings_maxVolumeSteps = kTokens[1371].value;
inline constexpr int32_t settings_maxVolumeDefault = (int32_t)kTokens[1372].num;
inline constexpr const char* settings_powerOnSteps = kTokens[1373].value;
inline constexpr const char* settings_crossfadeSteps = kTokens[1374].value;
inline constexpr int32_t settings_crossfadeDefault = (int32_t)kTokens[1375].num;
inline constexpr const char* settings_brightnessSteps = kTokens[1376].value;
inline constexpr int32_t settings_brightnessDefault = (int32_t)kTokens[1377].num;
inline constexpr int32_t homeRingX = (int32_t)kTokens[1378].num;
inline constexpr int32_t homeRingY = (int32_t)kTokens[1379].num;
inline constexpr int32_t homeRingSize = (int32_t)kTokens[1380].num;
inline constexpr int32_t homeRingStroke = (int32_t)kTokens[1381].num;
inline constexpr int32_t homeRingHitBottom = (int32_t)kTokens[1382].num;
inline constexpr int32_t homeRingPressDim = (int32_t)kTokens[1383].num;
inline constexpr int32_t npSeekBandX0 = (int32_t)kTokens[1384].num;
inline constexpr int32_t npSeekBandX1 = (int32_t)kTokens[1385].num;
inline constexpr int32_t npSeekBandBottom = (int32_t)kTokens[1386].num;
inline constexpr int32_t npMarkHit = (int32_t)kTokens[1387].num;
inline constexpr int32_t npLyricPitch = (int32_t)kTokens[1388].num;
inline constexpr int32_t npLyricBottom = (int32_t)kTokens[1389].num;
inline constexpr int32_t lockLyricBaseline = (int32_t)kTokens[1390].num;
inline constexpr int32_t size_largeScale = (int32_t)kTokens[1391].num;
inline constexpr int32_t large_type_npTitle_size = (int32_t)kTokens[1392].num;
inline constexpr int32_t large_type_npNext_size = (int32_t)kTokens[1393].num;
inline constexpr int32_t large_type_npAlbum_size = (int32_t)kTokens[1394].num;
inline constexpr int32_t large_npLyricPitch = (int32_t)kTokens[1395].num;
inline constexpr int32_t large_npNextPitch = (int32_t)kTokens[1396].num;
inline constexpr int32_t large_npAlbumPitch = (int32_t)kTokens[1397].num;
inline constexpr int32_t wifi_levelHigh = (int32_t)kTokens[1398].num;
inline constexpr int32_t wifi_levelMid = (int32_t)kTokens[1399].num;
inline constexpr int32_t gesture_swipe = (int32_t)kTokens[1400].num;
inline constexpr int32_t gesture_edge = (int32_t)kTokens[1401].num;
inline constexpr int32_t gesture_edgeRight = (int32_t)kTokens[1402].num;
inline constexpr int32_t gesture_bottom = (int32_t)kTokens[1403].num;
inline constexpr int32_t homeWatermarkGap = (int32_t)kTokens[1404].num;
inline constexpr int32_t navH = (int32_t)kTokens[1405].num;
inline constexpr int32_t navGlyph = (int32_t)kTokens[1406].num;
inline constexpr int32_t adjustExit = (int32_t)kTokens[1407].num;
inline constexpr int32_t adjustColumn = (int32_t)kTokens[1408].num;
inline constexpr int32_t adjustNumberDrop = (int32_t)kTokens[1409].num;
inline constexpr int32_t backdrop_coverSigma = (int32_t)kTokens[1410].num;
inline constexpr int32_t backdrop_coverGrid = (int32_t)kTokens[1411].num;
inline constexpr int32_t size_keptTextMax = (int32_t)kTokens[1412].num;
inline constexpr int32_t npNextDropLarge = (int32_t)kTokens[1413].num;
inline constexpr int32_t npNextLinePitchLarge = (int32_t)kTokens[1414].num;
inline constexpr int32_t npNextCountLarge = (int32_t)kTokens[1415].num;
inline constexpr int32_t npNextNavCountLarge = (int32_t)kTokens[1416].num;
inline constexpr int32_t npTimeBaselineLarge = (int32_t)kTokens[1417].num;
inline constexpr int32_t npTimePairGap = (int32_t)kTokens[1418].num;
inline constexpr int32_t tileCaptionDropLarge = (int32_t)kTokens[1419].num;
inline constexpr int32_t tileCaptionPitchLarge = (int32_t)kTokens[1420].num;
inline constexpr int32_t tileCaptionBandLarge = (int32_t)kTokens[1421].num;
inline constexpr int32_t tileCellHLarge = (int32_t)kTokens[1422].num;
inline constexpr int32_t tileRowPitchLarge = (int32_t)kTokens[1423].num;
inline constexpr int32_t qpHintBaseLarge = (int32_t)kTokens[1424].num;
inline constexpr int32_t qpHintStepLarge = (int32_t)kTokens[1425].num;
inline constexpr int32_t detailTitlePitchLarge = (int32_t)kTokens[1426].num;
inline constexpr int32_t detailSubPitchLarge = (int32_t)kTokens[1427].num;
inline constexpr uint32_t color_light_bg = (uint32_t)kTokens[1428].num;
inline constexpr uint32_t color_light_bgElevated = (uint32_t)kTokens[1429].num;
inline constexpr uint32_t color_light_tile = (uint32_t)kTokens[1430].num;
inline constexpr uint32_t color_light_tilePressed = (uint32_t)kTokens[1431].num;
inline constexpr uint32_t color_light_border = (uint32_t)kTokens[1432].num;
inline constexpr uint32_t color_light_ink = (uint32_t)kTokens[1433].num;
inline constexpr int32_t input_repeatDelayMs = (int32_t)kTokens[1434].num;
inline constexpr int32_t input_repeatIntervalMs = (int32_t)kTokens[1435].num;
inline constexpr int32_t turnedLineStart = (int32_t)kTokens[1436].num;
inline constexpr int32_t motion_swapOut = (int32_t)kTokens[1437].num;
inline constexpr int32_t motion_swapShift = (int32_t)kTokens[1438].num;
inline constexpr int32_t motion_swapRows = (int32_t)kTokens[1439].num;
inline constexpr int32_t motion_swapRowInShift = (int32_t)kTokens[1440].num;
inline constexpr int32_t motion_swapRowFade = (int32_t)kTokens[1441].num;

}
