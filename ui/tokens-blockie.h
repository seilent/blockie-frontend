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
    {"type.settingsItem.size", "17", "u", 1114112},
    {"type.settingsItem.style", "Bold", "style", 0},
    {"type.settingsItem.case", "upper", "case", 0},
    {"type.settingsItem.alpha", "1.0", "alpha", 65536},
    {"type.settingsItem.alphaDim", "0.6", "alpha", 39322},
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
    {"type.jumpExit.size", "11", "u", 720896},
    {"type.jumpExit.style", "Bold", "style", 0},
    {"type.jumpExit.case", "upper", "case", 0},
    {"type.jumpExit.alpha", "0.6", "alpha", 39322},
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
    {"type.saverWord.size", "286.2", "u", 18756403},
    {"type.saverWord.style", "SemiLight", "style", 0},
    {"type.saverWord.case", "upper", "case", 0},
    {"type.saverWord.alpha", "1.0", "alpha", 65536},
    {"type.saverTitle.size", "48.9", "u", 3204710},
    {"type.saverTitle.style", "Bold", "style", 0},
    {"type.saverTitle.case", "upper", "case", 0},
    {"type.saverTitle.alpha", "1.0", "alpha", 65536},
    {"type.saverLabel.size", "23.5", "u", 1540096},
    {"type.saverLabel.style", "SemiLight", "style", 0},
    {"type.saverLabel.case", "upper", "case", 0},
    {"type.saverLabel.alpha", "1.0", "alpha", 65536},
    {"type.saverFourth.size", "117.3", "u", 7687373},
    {"type.saverFourth.style", "SemiLight", "style", 0},
    {"type.saverFourth.case", "upper", "case", 0},
    {"type.saverFourth.alpha", "1.0", "alpha", 65536},
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
    {"homeFirstBaseline", "96.4", "u", 6317670},
    {"homeSubtitleX", "11.1", "u", 727450},
    {"homeSubtitleDrop", "3.5", "u", 229376},
    {"homeWatermarkBaseline", "410.66667", "u", 26913451},
    {"qpPeekScale", "0.30", "ratio", 19661},
    {"qpPeekX", "-14.3", "u", -937165},
    {"qpPeekY", "189", "u", 12386304},
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
    {"letterInsetX", "6", "u", 393216},
    {"letterInsetBaseline", "8", "u", 524288},
    {"rowHeight", "40", "u", 2621440},
    {"rowTextInset", "4", "u", 262144},
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
    {"npBarHeight", "2.6", "u", 170394},
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
    {"saverWordBaseline", "309.3", "u", 20270285},
    {"saverTitleBaseline", "346.7", "u", 22721331},
    {"saverLabelBaseline", "106.7", "u", 6992691},
    {"saverFourthBaseline", "432.0", "u", 28311552},
    {"saverAlbumPitch", "16", "u", 1048576},
    {"saverClockBaseline", "51", "u", 3342336},
    {"overlayRing", "64", "u", 4194304},
    {"overlayGlyph", "20", "u", 1310720},
    {"overlayGlyphOffset", "80", "u", 5242880},
    {"toastHeight", "40", "u", 2621440},
    {"toastArtistBaseline", "16", "u", 1048576},
    {"toastTitleBaseline", "34", "u", 2228224},
    {"jumpCols", "4", "count", 262144},
    {"jumpGap", "4", "u", 262144},
    {"jumpExitCells", "2", "count", 131072},
    {"jumpCells", "38", "count", 2490368},
    {"jumpTop", "16", "u", 1048576},
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
    {"overlayNumberBaseline", "142.22667", "u", 9320967},
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
    {"vol.ringStroke", "6.8", "u", 445645},
    {"vol.hit", "64", "u", 4194304},
    {"vol.plus", "41.2", "u", 2700083},
    {"vol.minus", "43.47", "u", 2848850},
    {"vol.pause", "41.6", "u", 2726298},
    {"vol.skip", "46", "u", 3014656},
    {"vol.number", "82.1", "u", 5380506},
    {"vol.label", "11.5", "u", 753664},
    {"vol.exit", "12.4", "u", 812646},
    {"batteryLowPercent", "10", "pct", 655360},
    {"qpEdge", "60", "u", 3932160},
    {"qpTile", "77", "u", 5046272},
    {"qpGutter", "2", "u", 131072},
    {"qpW", "216", "u", 14155776},
    {"qpFirstBaseline", "35", "u", 2293760},
    {"qpContentDrop", "5", "u", 327680},
    {"qpSectionGap", "70", "u", 4587520},
    {"qpHintDrop", "16", "u", 1048576},
    {"qpHintPitch", "11", "u", 720896},
    {"qpHintW", "140", "u", 9175040},
    {"qpTilePad", "5", "u", 327680},
    {"homeBarX", "44", "u", 2883584},
    {"homeBarTop", "27.79", "u", 1821245},
    {"homeBarH", "37.59", "u", 2463498},
    {"focusBandBottom", "386.66667", "u", 25340587},
    {"motion.perspectiveRatio", "2.3", "ratio", 150733},
    {"motion.perspective", "552", "u", 36175872},
    {"motion.turnstileOut", "250", "ms", 16384000},
    {"motion.turnstileOutHold", "240", "ms", 15728640},
    {"motion.turnstileIn", "350", "ms", 22937600},
    {"motion.turnstileInFade", "10", "ms", 655360},
    {"motion.turnstileOutAngle", "50", "deg", 3276800},
    {"motion.turnstileInAngle", "-80", "deg", -5242880},
    {"motion.headerOut", "250", "ms", 16384000},
    {"motion.headerOutShift", "-0.25", "ratio_h", -16384},
    {"motion.headerOutDistance", "-80", "u", -5242880},
    {"motion.homeEnter", "220", "ms", 14417920},
    {"motion.homeEnterPeek", "420", "ms", 27525120},
    {"motion.homeEnterStagger", "20", "ms", 1310720},
    {"motion.homeEnterShift", "24", "u", 1572864},
    {"motion.homeOut", "250", "ms", 16384000},
    {"motion.homeOutStagger", "28", "ms", 1835008},
    {"motion.homeOutShift", "-0.25", "ratio_h", -16384},
    {"motion.homeOutDistance", "-80", "u", -5242880},
    {"motion.pivotOut", "300", "ms", 19660800},
    {"motion.pivotStrip", "380", "ms", 24903680},
    {"motion.pivotLabel", "200", "ms", 13107200},
    {"motion.pivotRows", "380", "ms", 24903680},
    {"motion.pivotShift", "34", "u", 2228224},
    {"motion.pivotRowStagger", "22", "ms", 1441792},
    {"motion.pivotRowCap", "10", "count", 655360},
    {"motion.pivotHeaderParallax", "0.2", "ratio", 13107},
    {"motion.quickplay", "420", "ms", 27525120},
    {"motion.quickplayAngle", "-12", "deg", -786432},
    {"motion.quickplayScale", "0.44", "ratio", 28836},
    {"motion.quickplayAlpha", "0.6", "alpha", 39322},
    {"motion.quickplayMenuInset", "84", "u", 5505024},
    {"motion.quickplayMenuX", "156", "u", 10223616},
    {"motion.quickplayMenuY", "94", "u", 6160384},
    {"motion.nowPlayingIn", "300", "ms", 19660800},
    {"motion.nowPlayingScale", "0.9", "ratio", 58982},
    {"motion.nowPlayingOriginX", "0.4", "ratio", 26214},
    {"motion.nowPlayingOriginY", "0.5", "ratio", 32768},
    {"motion.nowPlayingOut", "250", "ms", 16384000},
    {"motion.nowPlayingFade", "300", "ms", 19660800},
    {"motion.trackHide", "500", "ms", 32768000},
    {"motion.trackShow", "400", "ms", 26214400},
    {"motion.trackArtScale", "0.8", "ratio", 52429},
    {"motion.trackArtOriginX", "0.2", "ratio", 13107},
    {"motion.trackArtOriginY", "0.5", "ratio", 32768},
    {"motion.trackShiftOut", "50", "u", 3276800},
    {"motion.trackShiftIn", "-20", "u", -1310720},
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
    {"motion.saver", "380", "ms", 24903680},
    {"motion.focusCursor", "100", "ms", 6553600},
    {"motion.focusAlpha", "200", "ms", 13107200},
    {"motion.scroll", "100", "ms", 6553600},
    {"motion.page", "500", "ms", 32768000},
    {"motion.press", "100", "ms", 6553600},
    {"motion.pressScale", "0.96", "ratio", 62915},
    {"motion.touchReturnDelay", "200", "ms", 13107200},
    {"motion.flingRetention", "0.95", "ratio", 62259},
    {"motion.flingTick", "16", "ms", 1048576},
    {"motion.flingWindow", "100", "ms", 6553600},
    {"motion.flingMaxStep", "33", "ms", 2162688},
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
    {"motion.toastIn", "350", "ms", 22937600},
    {"motion.toastHold", "3500", "ms", 229376000},
    {"motion.toastOut", "250", "ms", 16384000},
    {"motion.toastTravel", "40", "u", 2621440},
    {"motion.lyricSlide", "350", "ms", 22937600},
    {"motion.listChange", "220", "ms", 14417920},
    {"motion.listChangeStagger", "50", "ms", 3276800},
    {"motion.listChangeCap", "10", "count", 655360},
    {"motion.listChangeShift", "-5", "u", -327680},
    {"motion.saverWordSpeed", "37.6", "u_per_s", 2464154},
    {"motion.saverTitleSpeed", "37.3", "u_per_s", 2444493},
    {"motion.saverLabelSpeed", "15.1", "u_per_s", 989594},
    {"motion.saverFourthSpeed", "25.8", "u_per_s", 1690829},
    {"motion.saverWordDelay", "645", "ms", 42270720},
    {"motion.saverTitleDelay", "2610", "ms", 171048960},
    {"motion.saverLabelDelay", "3040", "ms", 199229440},
    {"motion.saverFourthDelay", "12640", "ms", 828375040},
    {"motion.saverWordEntry", "450", "ms", 29491200},
    {"motion.saverWordEntryShift", "88.9", "u", 5826150},
    {"motion.saverWordFadeIn", "170", "ms", 11141120},
    {"motion.backdropFade", "2000", "ms", 131072000},
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
    {"power.saverDefaultS", "30", "s", 1966080},
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
    {"settings.saverDefault", "30", "s", 1966080},
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
    {"homeRingY", "87.4", "u", 5727846},
    {"homeRingSize", "35", "u", 2293760},
    {"homeRingStroke", "4", "u", 262144},
    {"homeRingHitBottom", "111.7", "u", 7320371},
    {"npSeekBandX0", "6.1", "u", 399770},
    {"npSeekBandX1", "174.8", "u", 11455693},
    {"npSeekBandBottom", "318.49", "u", 20872561},
    {"npMarkHit", "54", "u", 3538944},
    {"npLyricPitch", "31.2", "u", 2044723},
    {"npLyricBottom", "362.66667", "u", 23767723},
    {"saverLyricBaseline", "330.66667", "u", 21670571},
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
};
inline constexpr int kTokenCount = 716;

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
inline constexpr int32_t type_pivotActive_size = (int32_t)kTokens[37].num;
inline constexpr const char* type_pivotActive_style = kTokens[38].value;
inline constexpr const char* type_pivotActive_case = kTokens[39].value;
inline constexpr int32_t type_pivotActive_alpha = (int32_t)kTokens[40].num;
inline constexpr int32_t type_pivotInactive_size = (int32_t)kTokens[41].num;
inline constexpr const char* type_pivotInactive_style = kTokens[42].value;
inline constexpr const char* type_pivotInactive_case = kTokens[43].value;
inline constexpr int32_t type_pivotInactive_alpha = (int32_t)kTokens[44].num;
inline constexpr int32_t type_sectionLabel_size = (int32_t)kTokens[45].num;
inline constexpr const char* type_sectionLabel_style = kTokens[46].value;
inline constexpr const char* type_sectionLabel_case = kTokens[47].value;
inline constexpr int32_t type_sectionLabel_alpha = (int32_t)kTokens[48].num;
inline constexpr int32_t type_listPrimary_size = (int32_t)kTokens[49].num;
inline constexpr const char* type_listPrimary_style = kTokens[50].value;
inline constexpr const char* type_listPrimary_case = kTokens[51].value;
inline constexpr int32_t type_listPrimary_alpha = (int32_t)kTokens[52].num;
inline constexpr int32_t type_listPrimary_alphaDim = (int32_t)kTokens[53].num;
inline constexpr int32_t type_listPlaylist_size = (int32_t)kTokens[54].num;
inline constexpr const char* type_listPlaylist_style = kTokens[55].value;
inline constexpr const char* type_listPlaylist_case = kTokens[56].value;
inline constexpr int32_t type_listPlaylist_alpha = (int32_t)kTokens[57].num;
inline constexpr int32_t type_listPlaylist_alphaDim = (int32_t)kTokens[58].num;
inline constexpr int32_t type_listSecondary_size = (int32_t)kTokens[59].num;
inline constexpr const char* type_listSecondary_style = kTokens[60].value;
inline constexpr const char* type_listSecondary_case = kTokens[61].value;
inline constexpr int32_t type_listSecondary_alpha = (int32_t)kTokens[62].num;
inline constexpr int32_t type_listCount_size = (int32_t)kTokens[63].num;
inline constexpr const char* type_listCount_style = kTokens[64].value;
inline constexpr const char* type_listCount_case = kTokens[65].value;
inline constexpr int32_t type_listCount_alpha = (int32_t)kTokens[66].num;
inline constexpr int32_t type_detailName_size = (int32_t)kTokens[67].num;
inline constexpr const char* type_detailName_style = kTokens[68].value;
inline constexpr const char* type_detailName_case = kTokens[69].value;
inline constexpr int32_t type_detailName_alpha = (int32_t)kTokens[70].num;
inline constexpr int32_t type_detailTitle_size = (int32_t)kTokens[71].num;
inline constexpr const char* type_detailTitle_style = kTokens[72].value;
inline constexpr const char* type_detailTitle_case = kTokens[73].value;
inline constexpr int32_t type_detailTitle_alpha = (int32_t)kTokens[74].num;
inline constexpr int32_t type_detailSub_size = (int32_t)kTokens[75].num;
inline constexpr const char* type_detailSub_style = kTokens[76].value;
inline constexpr const char* type_detailSub_case = kTokens[77].value;
inline constexpr int32_t type_detailSub_alpha = (int32_t)kTokens[78].num;
inline constexpr int32_t type_gridCaption_size = (int32_t)kTokens[79].num;
inline constexpr const char* type_gridCaption_style = kTokens[80].value;
inline constexpr const char* type_gridCaption_case = kTokens[81].value;
inline constexpr int32_t type_gridCaption_alpha = (int32_t)kTokens[82].num;
inline constexpr int32_t type_letterGlyph_size = (int32_t)kTokens[83].num;
inline constexpr const char* type_letterGlyph_style = kTokens[84].value;
inline constexpr const char* type_letterGlyph_case = kTokens[85].value;
inline constexpr int32_t type_letterGlyph_alpha = (int32_t)kTokens[86].num;
inline constexpr int32_t type_settingsItem_size = (int32_t)kTokens[87].num;
inline constexpr const char* type_settingsItem_style = kTokens[88].value;
inline constexpr const char* type_settingsItem_case = kTokens[89].value;
inline constexpr int32_t type_settingsItem_alpha = (int32_t)kTokens[90].num;
inline constexpr int32_t type_settingsItem_alphaDim = (int32_t)kTokens[91].num;
inline constexpr int32_t type_settingsValue_size = (int32_t)kTokens[92].num;
inline constexpr const char* type_settingsValue_style = kTokens[93].value;
inline constexpr const char* type_settingsValue_case = kTokens[94].value;
inline constexpr int32_t type_settingsValue_alpha = (int32_t)kTokens[95].num;
inline constexpr int32_t type_settingsSub_size = (int32_t)kTokens[96].num;
inline constexpr const char* type_settingsSub_style = kTokens[97].value;
inline constexpr const char* type_settingsSub_case = kTokens[98].value;
inline constexpr int32_t type_settingsSub_alpha = (int32_t)kTokens[99].num;
inline constexpr int32_t type_settingsSub_alphaDim = (int32_t)kTokens[100].num;
inline constexpr int32_t type_npArtist_size = (int32_t)kTokens[101].num;
inline constexpr const char* type_npArtist_style = kTokens[102].value;
inline constexpr const char* type_npArtist_case = kTokens[103].value;
inline constexpr int32_t type_npArtist_alpha = (int32_t)kTokens[104].num;
inline constexpr int32_t type_npAlbum_size = (int32_t)kTokens[105].num;
inline constexpr const char* type_npAlbum_style = kTokens[106].value;
inline constexpr const char* type_npAlbum_case = kTokens[107].value;
inline constexpr int32_t type_npAlbum_alpha = (int32_t)kTokens[108].num;
inline constexpr int32_t type_npTitle_size = (int32_t)kTokens[109].num;
inline constexpr const char* type_npTitle_style = kTokens[110].value;
inline constexpr const char* type_npTitle_case = kTokens[111].value;
inline constexpr int32_t type_npTitle_alpha = (int32_t)kTokens[112].num;
inline constexpr int32_t type_npNext_size = (int32_t)kTokens[113].num;
inline constexpr const char* type_npNext_style = kTokens[114].value;
inline constexpr const char* type_npNext_case = kTokens[115].value;
inline constexpr int32_t type_npNext_alpha = (int32_t)kTokens[116].num;
inline constexpr int32_t type_npCount_size = (int32_t)kTokens[117].num;
inline constexpr const char* type_npCount_style = kTokens[118].value;
inline constexpr const char* type_npCount_case = kTokens[119].value;
inline constexpr int32_t type_npCount_alpha = (int32_t)kTokens[120].num;
inline constexpr int32_t type_time_size = (int32_t)kTokens[121].num;
inline constexpr const char* type_time_style = kTokens[122].value;
inline constexpr const char* type_time_case = kTokens[123].value;
inline constexpr int32_t type_time_alpha = (int32_t)kTokens[124].num;
inline constexpr int32_t type_npViewArtist_size = (int32_t)kTokens[125].num;
inline constexpr const char* type_npViewArtist_style = kTokens[126].value;
inline constexpr const char* type_npViewArtist_case = kTokens[127].value;
inline constexpr int32_t type_npViewArtist_alpha = (int32_t)kTokens[128].num;
inline constexpr int32_t type_npViewAlbum_size = (int32_t)kTokens[129].num;
inline constexpr const char* type_npViewAlbum_style = kTokens[130].value;
inline constexpr const char* type_npViewAlbum_case = kTokens[131].value;
inline constexpr int32_t type_npViewAlbum_alpha = (int32_t)kTokens[132].num;
inline constexpr int32_t type_npViewTitle_size = (int32_t)kTokens[133].num;
inline constexpr const char* type_npViewTitle_style = kTokens[134].value;
inline constexpr const char* type_npViewTitle_case = kTokens[135].value;
inline constexpr int32_t type_npViewTitle_alpha = (int32_t)kTokens[136].num;
inline constexpr int32_t type_npViewNext_size = (int32_t)kTokens[137].num;
inline constexpr const char* type_npViewNext_style = kTokens[138].value;
inline constexpr const char* type_npViewNext_case = kTokens[139].value;
inline constexpr int32_t type_npViewNext_alpha = (int32_t)kTokens[140].num;
inline constexpr int32_t type_npViewTime_size = (int32_t)kTokens[141].num;
inline constexpr const char* type_npViewTime_style = kTokens[142].value;
inline constexpr const char* type_npViewTime_case = kTokens[143].value;
inline constexpr int32_t type_npViewTime_alpha = (int32_t)kTokens[144].num;
inline constexpr int32_t type_overlayNumber_size = (int32_t)kTokens[145].num;
inline constexpr const char* type_overlayNumber_style = kTokens[146].value;
inline constexpr int32_t type_overlayNumber_alpha = (int32_t)kTokens[147].num;
inline constexpr int32_t type_overlayLabel_size = (int32_t)kTokens[148].num;
inline constexpr const char* type_overlayLabel_style = kTokens[149].value;
inline constexpr const char* type_overlayLabel_case = kTokens[150].value;
inline constexpr int32_t type_overlayLabel_alpha = (int32_t)kTokens[151].num;
inline constexpr int32_t type_jumpExit_size = (int32_t)kTokens[152].num;
inline constexpr const char* type_jumpExit_style = kTokens[153].value;
inline constexpr const char* type_jumpExit_case = kTokens[154].value;
inline constexpr int32_t type_jumpExit_alpha = (int32_t)kTokens[155].num;
inline constexpr int32_t type_toastArtist_size = (int32_t)kTokens[156].num;
inline constexpr const char* type_toastArtist_style = kTokens[157].value;
inline constexpr const char* type_toastArtist_case = kTokens[158].value;
inline constexpr int32_t type_toastArtist_alpha = (int32_t)kTokens[159].num;
inline constexpr int32_t type_toastTitle_size = (int32_t)kTokens[160].num;
inline constexpr const char* type_toastTitle_style = kTokens[161].value;
inline constexpr const char* type_toastTitle_case = kTokens[162].value;
inline constexpr int32_t type_toastTitle_alpha = (int32_t)kTokens[163].num;
inline constexpr int32_t type_saverClock_size = (int32_t)kTokens[164].num;
inline constexpr const char* type_saverClock_style = kTokens[165].value;
inline constexpr int32_t type_saverClock_alpha = (int32_t)kTokens[166].num;
inline constexpr int32_t type_saverWord_size = (int32_t)kTokens[167].num;
inline constexpr const char* type_saverWord_style = kTokens[168].value;
inline constexpr const char* type_saverWord_case = kTokens[169].value;
inline constexpr int32_t type_saverWord_alpha = (int32_t)kTokens[170].num;
inline constexpr int32_t type_saverTitle_size = (int32_t)kTokens[171].num;
inline constexpr const char* type_saverTitle_style = kTokens[172].value;
inline constexpr const char* type_saverTitle_case = kTokens[173].value;
inline constexpr int32_t type_saverTitle_alpha = (int32_t)kTokens[174].num;
inline constexpr int32_t type_saverLabel_size = (int32_t)kTokens[175].num;
inline constexpr const char* type_saverLabel_style = kTokens[176].value;
inline constexpr const char* type_saverLabel_case = kTokens[177].value;
inline constexpr int32_t type_saverLabel_alpha = (int32_t)kTokens[178].num;
inline constexpr int32_t type_saverFourth_size = (int32_t)kTokens[179].num;
inline constexpr const char* type_saverFourth_style = kTokens[180].value;
inline constexpr const char* type_saverFourth_case = kTokens[181].value;
inline constexpr int32_t type_saverFourth_alpha = (int32_t)kTokens[182].num;
inline constexpr int32_t type_jumpLetter_size = (int32_t)kTokens[183].num;
inline constexpr const char* type_jumpLetter_style = kTokens[184].value;
inline constexpr const char* type_jumpLetter_case = kTokens[185].value;
inline constexpr int32_t type_jumpLetter_alpha = (int32_t)kTokens[186].num;
inline constexpr int32_t type_dialogTitle_size = (int32_t)kTokens[187].num;
inline constexpr const char* type_dialogTitle_style = kTokens[188].value;
inline constexpr const char* type_dialogTitle_case = kTokens[189].value;
inline constexpr int32_t type_dialogTitle_alpha = (int32_t)kTokens[190].num;
inline constexpr int32_t type_dialogItem_size = (int32_t)kTokens[191].num;
inline constexpr const char* type_dialogItem_style = kTokens[192].value;
inline constexpr const char* type_dialogItem_case = kTokens[193].value;
inline constexpr int32_t type_dialogItem_alpha = (int32_t)kTokens[194].num;
inline constexpr int32_t type_dialogItem_alphaDim = (int32_t)kTokens[195].num;
inline constexpr int32_t type_watermark_size = (int32_t)kTokens[196].num;
inline constexpr const char* type_watermark_style = kTokens[197].value;
inline constexpr const char* type_watermark_case = kTokens[198].value;
inline constexpr int32_t type_watermark_alpha = (int32_t)kTokens[199].num;
inline constexpr uint32_t color_bg = (uint32_t)kTokens[200].num;
inline constexpr uint32_t color_bgElevated = (uint32_t)kTokens[201].num;
inline constexpr uint32_t color_tile = (uint32_t)kTokens[202].num;
inline constexpr uint32_t color_tilePressed = (uint32_t)kTokens[203].num;
inline constexpr uint32_t color_border = (uint32_t)kTokens[204].num;
inline constexpr uint32_t color_pageHeader = (uint32_t)kTokens[205].num;
inline constexpr uint32_t color_pivotInactive = (uint32_t)kTokens[206].num;
inline constexpr int32_t alpha_full = (int32_t)kTokens[207].num;
inline constexpr int32_t alpha_high = (int32_t)kTokens[208].num;
inline constexpr int32_t alpha_mid = (int32_t)kTokens[209].num;
inline constexpr int32_t alpha_low = (int32_t)kTokens[210].num;
inline constexpr int32_t alpha_faint = (int32_t)kTokens[211].num;
inline constexpr int32_t scrim_sheet_alpha = (int32_t)kTokens[212].num;
inline constexpr int32_t scrim_overlay_alpha = (int32_t)kTokens[213].num;
inline constexpr uint32_t color_black = (uint32_t)kTokens[214].num;
inline constexpr int32_t dim_alpha = (int32_t)kTokens[215].num;
inline constexpr int32_t backdrop_stop0_pos = (int32_t)kTokens[216].num;
inline constexpr int32_t backdrop_stop0_alpha = (int32_t)kTokens[217].num;
inline constexpr int32_t backdrop_stop1_pos = (int32_t)kTokens[218].num;
inline constexpr int32_t backdrop_stop1_alpha = (int32_t)kTokens[219].num;
inline constexpr int32_t backdrop_stop2_pos = (int32_t)kTokens[220].num;
inline constexpr int32_t backdrop_stop2_alpha = (int32_t)kTokens[221].num;
inline constexpr int32_t pageSurface_alphaTop = (int32_t)kTokens[222].num;
inline constexpr int32_t accent_hueDefault = (int32_t)kTokens[223].num;
inline constexpr int32_t accent_hueStep = (int32_t)kTokens[224].num;
inline constexpr int32_t accent_hueCount = (int32_t)kTokens[225].num;
inline constexpr int32_t accent_value = (int32_t)kTokens[226].num;
inline constexpr int32_t accent_satMax = (int32_t)kTokens[227].num;
inline constexpr int32_t accent_satMin = (int32_t)kTokens[228].num;
inline constexpr int32_t accent_lumFloor = (int32_t)kTokens[229].num;
inline constexpr int32_t accent_brightSat = (int32_t)kTokens[230].num;
inline constexpr uint32_t accent_default = (uint32_t)kTokens[231].num;
inline constexpr uint32_t accent_brightDefault = (uint32_t)kTokens[232].num;
inline constexpr uint32_t accent_h0 = (uint32_t)kTokens[233].num;
inline constexpr uint32_t accent_h10 = (uint32_t)kTokens[234].num;
inline constexpr uint32_t accent_h20 = (uint32_t)kTokens[235].num;
inline constexpr uint32_t accent_h30 = (uint32_t)kTokens[236].num;
inline constexpr uint32_t accent_h40 = (uint32_t)kTokens[237].num;
inline constexpr uint32_t accent_h50 = (uint32_t)kTokens[238].num;
inline constexpr uint32_t accent_h60 = (uint32_t)kTokens[239].num;
inline constexpr uint32_t accent_h70 = (uint32_t)kTokens[240].num;
inline constexpr uint32_t accent_h80 = (uint32_t)kTokens[241].num;
inline constexpr uint32_t accent_h90 = (uint32_t)kTokens[242].num;
inline constexpr uint32_t accent_h100 = (uint32_t)kTokens[243].num;
inline constexpr uint32_t accent_h110 = (uint32_t)kTokens[244].num;
inline constexpr uint32_t accent_h120 = (uint32_t)kTokens[245].num;
inline constexpr uint32_t accent_h130 = (uint32_t)kTokens[246].num;
inline constexpr uint32_t accent_h140 = (uint32_t)kTokens[247].num;
inline constexpr uint32_t accent_h150 = (uint32_t)kTokens[248].num;
inline constexpr uint32_t accent_h160 = (uint32_t)kTokens[249].num;
inline constexpr uint32_t accent_h170 = (uint32_t)kTokens[250].num;
inline constexpr uint32_t accent_h180 = (uint32_t)kTokens[251].num;
inline constexpr uint32_t accent_h190 = (uint32_t)kTokens[252].num;
inline constexpr uint32_t accent_h200 = (uint32_t)kTokens[253].num;
inline constexpr uint32_t accent_h210 = (uint32_t)kTokens[254].num;
inline constexpr uint32_t accent_h220 = (uint32_t)kTokens[255].num;
inline constexpr uint32_t accent_h230 = (uint32_t)kTokens[256].num;
inline constexpr uint32_t accent_h240 = (uint32_t)kTokens[257].num;
inline constexpr uint32_t accent_h250 = (uint32_t)kTokens[258].num;
inline constexpr uint32_t accent_h260 = (uint32_t)kTokens[259].num;
inline constexpr uint32_t accent_h270 = (uint32_t)kTokens[260].num;
inline constexpr uint32_t accent_h280 = (uint32_t)kTokens[261].num;
inline constexpr uint32_t accent_h290 = (uint32_t)kTokens[262].num;
inline constexpr uint32_t accent_h300 = (uint32_t)kTokens[263].num;
inline constexpr uint32_t accent_h310 = (uint32_t)kTokens[264].num;
inline constexpr uint32_t accent_h320 = (uint32_t)kTokens[265].num;
inline constexpr uint32_t accent_h330 = (uint32_t)kTokens[266].num;
inline constexpr uint32_t accent_h340 = (uint32_t)kTokens[267].num;
inline constexpr uint32_t accent_h350 = (uint32_t)kTokens[268].num;
inline constexpr int32_t focus_border = (int32_t)kTokens[269].num;
inline constexpr int32_t focus_barWidth = (int32_t)kTokens[270].num;
inline constexpr int32_t focus_barOffset = (int32_t)kTokens[271].num;
inline constexpr int32_t scrollbar_width = (int32_t)kTokens[272].num;
inline constexpr int32_t scrollbar_x = (int32_t)kTokens[273].num;
inline constexpr int32_t scrollbar_min = (int32_t)kTokens[274].num;
inline constexpr int32_t scrollbar_alpha = (int32_t)kTokens[275].num;
inline constexpr int32_t scrollbar_hold = (int32_t)kTokens[276].num;
inline constexpr int32_t scrollbar_fade = (int32_t)kTokens[277].num;
inline constexpr int32_t edge = (int32_t)kTokens[278].num;
inline constexpr int32_t hubEdge = (int32_t)kTokens[279].num;
inline constexpr int32_t homeTextX = (int32_t)kTokens[280].num;
inline constexpr int32_t homePitch = (int32_t)kTokens[281].num;
inline constexpr int32_t homeFirstBaseline = (int32_t)kTokens[282].num;
inline constexpr int32_t homeSubtitleX = (int32_t)kTokens[283].num;
inline constexpr int32_t homeSubtitleDrop = (int32_t)kTokens[284].num;
inline constexpr int32_t homeWatermarkBaseline = (int32_t)kTokens[285].num;
inline constexpr int32_t qpPeekScale = (int32_t)kTokens[286].num;
inline constexpr int32_t qpPeekX = (int32_t)kTokens[287].num;
inline constexpr int32_t qpPeekY = (int32_t)kTokens[288].num;
inline constexpr int32_t qpPeekAlpha = (int32_t)kTokens[289].num;
inline constexpr int32_t headerBand = (int32_t)kTokens[290].num;
inline constexpr int32_t headerXHeightBottom = (int32_t)kTokens[291].num;
inline constexpr int32_t pivotTop = (int32_t)kTokens[292].num;
inline constexpr int32_t pivotBottom = (int32_t)kTokens[293].num;
inline constexpr int32_t pivotBaseline = (int32_t)kTokens[294].num;
inline constexpr int32_t pivotGap = (int32_t)kTokens[295].num;
inline constexpr int32_t pivotActiveGap = (int32_t)kTokens[296].num;
inline constexpr int32_t pivotLeadGap = (int32_t)kTokens[297].num;
inline constexpr int32_t contentTop = (int32_t)kTokens[298].num;
inline constexpr int32_t page_headerX = (int32_t)kTokens[299].num;
inline constexpr int32_t page_headerBaseline = (int32_t)kTokens[300].num;
inline constexpr int32_t page_pivotBaseline = (int32_t)kTokens[301].num;
inline constexpr int32_t page_contentTop = (int32_t)kTokens[302].num;
inline constexpr int32_t page_countBaseline = (int32_t)kTokens[303].num;
inline constexpr int32_t page_albumHeaderX = (int32_t)kTokens[304].num;
inline constexpr int32_t page_rowTitle = (int32_t)kTokens[305].num;
inline constexpr int32_t tileS = (int32_t)kTokens[306].num;
inline constexpr int32_t gridTile = (int32_t)kTokens[307].num;
inline constexpr int32_t gridX = (int32_t)kTokens[308].num;
inline constexpr int32_t gridGap = (int32_t)kTokens[309].num;
inline constexpr int32_t gutter = (int32_t)kTokens[310].num;
inline constexpr int32_t gridCols = (int32_t)kTokens[311].num;
inline constexpr int32_t tileCaptionDrop = (int32_t)kTokens[312].num;
inline constexpr int32_t tileCaptionPitch = (int32_t)kTokens[313].num;
inline constexpr int32_t tileCaptionBand = (int32_t)kTokens[314].num;
inline constexpr int32_t tileCellH = (int32_t)kTokens[315].num;
inline constexpr int32_t tileRowPitch = (int32_t)kTokens[316].num;
inline constexpr int32_t tileL = (int32_t)kTokens[317].num;
inline constexpr int32_t tileLGutter = (int32_t)kTokens[318].num;
inline constexpr int32_t letterTileBorder = (int32_t)kTokens[319].num;
inline constexpr int32_t letterTileBorderAlpha = (int32_t)kTokens[320].num;
inline constexpr int32_t letterTileBorderFocused = (int32_t)kTokens[321].num;
inline constexpr int32_t letterInsetX = (int32_t)kTokens[322].num;
inline constexpr int32_t letterInsetBaseline = (int32_t)kTokens[323].num;
inline constexpr int32_t rowHeight = (int32_t)kTokens[324].num;
inline constexpr int32_t rowTextInset = (int32_t)kTokens[325].num;
inline constexpr int32_t rowRing = (int32_t)kTokens[326].num;
inline constexpr int32_t songIndent = (int32_t)kTokens[327].num;
inline constexpr int32_t detailRow = (int32_t)kTokens[328].num;
inline constexpr int32_t detailNameDrop = (int32_t)kTokens[329].num;
inline constexpr int32_t detailNameBaseline = (int32_t)kTokens[330].num;
inline constexpr int32_t detailRingTop = (int32_t)kTokens[331].num;
inline constexpr int32_t detailRing = (int32_t)kTokens[332].num;
inline constexpr int32_t detailRingGap = (int32_t)kTokens[333].num;
inline constexpr int32_t detailRingCount = (int32_t)kTokens[334].num;
inline constexpr int32_t detailRingInset = (int32_t)kTokens[335].num;
inline constexpr int32_t detailLabelDrop = (int32_t)kTokens[336].num;
inline constexpr int32_t detailLabelBaseline = (int32_t)kTokens[337].num;
inline constexpr int32_t detailRowsTop = (int32_t)kTokens[338].num;
inline constexpr int32_t detailTextX = (int32_t)kTokens[339].num;
inline constexpr int32_t detailTitleDrop = (int32_t)kTokens[340].num;
inline constexpr int32_t detailTitlePitch = (int32_t)kTokens[341].num;
inline constexpr int32_t detailTitleLines = (int32_t)kTokens[342].num;
inline constexpr int32_t detailSubPitch = (int32_t)kTokens[343].num;
inline constexpr int32_t detailRingX0 = (int32_t)kTokens[344].num;
inline constexpr int32_t detailRingX1 = (int32_t)kTokens[345].num;
inline constexpr int32_t detailRingX2 = (int32_t)kTokens[346].num;
inline constexpr int32_t albumArtX = (int32_t)kTokens[347].num;
inline constexpr int32_t albumArtTop = (int32_t)kTokens[348].num;
inline constexpr int32_t albumRowsGap = (int32_t)kTokens[349].num;
inline constexpr int32_t albumRowsTop = (int32_t)kTokens[350].num;
inline constexpr int32_t settingsPitch = (int32_t)kTokens[351].num;
inline constexpr int32_t settingsLabelBaselineInset = (int32_t)kTokens[352].num;
inline constexpr int32_t settingsSwatch = (int32_t)kTokens[353].num;
inline constexpr int32_t settingsSwatchGap = (int32_t)kTokens[354].num;
inline constexpr int32_t settingsTitlePitch = (int32_t)kTokens[355].num;
inline constexpr int32_t settingsSubPad = (int32_t)kTokens[356].num;
inline constexpr int32_t settingsValueFocused = (int32_t)kTokens[357].num;
inline constexpr int32_t settingsValueDim = (int32_t)kTokens[358].num;
inline constexpr int32_t eqValueGap = (int32_t)kTokens[359].num;
inline constexpr int32_t queueCountBaseline = (int32_t)kTokens[360].num;
inline constexpr int32_t queueRowsTop = (int32_t)kTokens[361].num;
inline constexpr int32_t backRing = (int32_t)kTokens[362].num;
inline constexpr int32_t backRingStroke = (int32_t)kTokens[363].num;
inline constexpr int32_t backRingX = (int32_t)kTokens[364].num;
inline constexpr int32_t backRingY = (int32_t)kTokens[365].num;
inline constexpr int32_t npBackRing = (int32_t)kTokens[366].num;
inline constexpr int32_t npBackRingStroke = (int32_t)kTokens[367].num;
inline constexpr int32_t npBackRingX = (int32_t)kTokens[368].num;
inline constexpr int32_t npBackRingY = (int32_t)kTokens[369].num;
inline constexpr int32_t npBackGlyph = (int32_t)kTokens[370].num;
inline constexpr int32_t touchSlop = (int32_t)kTokens[371].num;
inline constexpr int32_t flingCap = (int32_t)kTokens[372].num;
inline constexpr int32_t flingStop = (int32_t)kTokens[373].num;
inline constexpr int32_t flingMin = (int32_t)kTokens[374].num;
inline constexpr int32_t npTextTop = (int32_t)kTokens[375].num;
inline constexpr int32_t npAlbumBaseline = (int32_t)kTokens[376].num;
inline constexpr int32_t npArtistY = (int32_t)kTokens[377].num;
inline constexpr int32_t npAlbumY = (int32_t)kTokens[378].num;
inline constexpr int32_t npFromBaseline = (int32_t)kTokens[379].num;
inline constexpr int32_t npArtTop = (int32_t)kTokens[380].num;
inline constexpr int32_t npArt = (int32_t)kTokens[381].num;
inline constexpr int32_t npProgressY = (int32_t)kTokens[382].num;
inline constexpr int32_t npProgressThick = (int32_t)kTokens[383].num;
inline constexpr int32_t npTimeDrop = (int32_t)kTokens[384].num;
inline constexpr int32_t npBarHeight = (int32_t)kTokens[385].num;
inline constexpr int32_t npTimeBaseline = (int32_t)kTokens[386].num;
inline constexpr int32_t npTitleY = (int32_t)kTokens[387].num;
inline constexpr int32_t npTextX = (int32_t)kTokens[388].num;
inline constexpr int32_t npArtX = (int32_t)kTokens[389].num;
inline constexpr int32_t npTextRight = (int32_t)kTokens[390].num;
inline constexpr int32_t npNextIndent = (int32_t)kTokens[391].num;
inline constexpr int32_t npNextPitch = (int32_t)kTokens[392].num;
inline constexpr int32_t npNextDrop = (int32_t)kTokens[393].num;
inline constexpr int32_t npNextLinePitch = (int32_t)kTokens[394].num;
inline constexpr int32_t npNextCount = (int32_t)kTokens[395].num;
inline constexpr int32_t npNextNavCount = (int32_t)kTokens[396].num;
inline constexpr int32_t npRing = (int32_t)kTokens[397].num;
inline constexpr int32_t npRingStroke = (int32_t)kTokens[398].num;
inline constexpr int32_t npRingY = (int32_t)kTokens[399].num;
inline constexpr int32_t npRingNavY = (int32_t)kTokens[400].num;
inline constexpr int32_t npRingX0 = (int32_t)kTokens[401].num;
inline constexpr int32_t npRingX1 = (int32_t)kTokens[402].num;
inline constexpr int32_t npRingX2 = (int32_t)kTokens[403].num;
inline constexpr int32_t npRingCx0 = (int32_t)kTokens[404].num;
inline constexpr int32_t npRingCx1 = (int32_t)kTokens[405].num;
inline constexpr int32_t npRingCx2 = (int32_t)kTokens[406].num;
inline constexpr int32_t npBtGlyph = (int32_t)kTokens[407].num;
inline constexpr int32_t seekStepMs = (int32_t)kTokens[408].num;
inline constexpr int32_t seekHoldMs = (int32_t)kTokens[409].num;
inline constexpr int32_t seekTickMs = (int32_t)kTokens[410].num;
inline constexpr int32_t seekTickStepMs = (int32_t)kTokens[411].num;
inline constexpr int32_t seekEndMarginMs = (int32_t)kTokens[412].num;
inline constexpr int32_t statusHeight = (int32_t)kTokens[413].num;
inline constexpr int32_t statusTop = (int32_t)kTokens[414].num;
inline constexpr int32_t statusGap = (int32_t)kTokens[415].num;
inline constexpr int32_t npStatusHeight = (int32_t)kTokens[416].num;
inline constexpr int32_t npStatusTop = (int32_t)kTokens[417].num;
inline constexpr int32_t npStatusRight = (int32_t)kTokens[418].num;
inline constexpr int32_t npStatusGap = (int32_t)kTokens[419].num;
inline constexpr int32_t npStatusText = (int32_t)kTokens[420].num;
inline constexpr int32_t npStatusBatteryW = (int32_t)kTokens[421].num;
inline constexpr int32_t npStatusBatteryH = (int32_t)kTokens[422].num;
inline constexpr int32_t statusBatteryX = (int32_t)kTokens[423].num;
inline constexpr int32_t statusBatteryY = (int32_t)kTokens[424].num;
inline constexpr int32_t statusBatteryW = (int32_t)kTokens[425].num;
inline constexpr int32_t statusBatteryH = (int32_t)kTokens[426].num;
inline constexpr int32_t saverArt = (int32_t)kTokens[427].num;
inline constexpr int32_t saverBottomMargin = (int32_t)kTokens[428].num;
inline constexpr int32_t saverWordBaseline = (int32_t)kTokens[429].num;
inline constexpr int32_t saverTitleBaseline = (int32_t)kTokens[430].num;
inline constexpr int32_t saverLabelBaseline = (int32_t)kTokens[431].num;
inline constexpr int32_t saverFourthBaseline = (int32_t)kTokens[432].num;
inline constexpr int32_t saverAlbumPitch = (int32_t)kTokens[433].num;
inline constexpr int32_t saverClockBaseline = (int32_t)kTokens[434].num;
inline constexpr int32_t overlayRing = (int32_t)kTokens[435].num;
inline constexpr int32_t overlayGlyph = (int32_t)kTokens[436].num;
inline constexpr int32_t overlayGlyphOffset = (int32_t)kTokens[437].num;
inline constexpr int32_t toastHeight = (int32_t)kTokens[438].num;
inline constexpr int32_t toastArtistBaseline = (int32_t)kTokens[439].num;
inline constexpr int32_t toastTitleBaseline = (int32_t)kTokens[440].num;
inline constexpr int32_t jumpCols = (int32_t)kTokens[441].num;
inline constexpr int32_t jumpGap = (int32_t)kTokens[442].num;
inline constexpr int32_t jumpExitCells = (int32_t)kTokens[443].num;
inline constexpr int32_t jumpCells = (int32_t)kTokens[444].num;
inline constexpr int32_t jumpTop = (int32_t)kTokens[445].num;
inline constexpr int32_t jumpSearchGlyph = (int32_t)kTokens[446].num;
inline constexpr int32_t kb_fieldTop = (int32_t)kTokens[447].num;
inline constexpr int32_t kb_fieldH = (int32_t)kTokens[448].num;
inline constexpr int32_t kb_fieldPad = (int32_t)kTokens[449].num;
inline constexpr int32_t kb_fieldBorder = (int32_t)kTokens[450].num;
inline constexpr int32_t kb_cursorW = (int32_t)kTokens[451].num;
inline constexpr int32_t kb_keyW = (int32_t)kTokens[452].num;
inline constexpr int32_t kb_keyH = (int32_t)kTokens[453].num;
inline constexpr int32_t kb_top = (int32_t)kTokens[454].num;
inline constexpr int32_t glyphBox = (int32_t)kTokens[455].num;
inline constexpr int32_t glyphStroke = (int32_t)kTokens[456].num;
inline constexpr int32_t ringStrokeFocused = (int32_t)kTokens[457].num;
inline constexpr int32_t ringStrokeIdle = (int32_t)kTokens[458].num;
inline constexpr int32_t ringArcStart = (int32_t)kTokens[459].num;
inline constexpr int32_t overlayPlusY = (int32_t)kTokens[460].num;
inline constexpr int32_t overlayNumberBaseline = (int32_t)kTokens[461].num;
inline constexpr int32_t overlayAnchorH = (int32_t)kTokens[462].num;
inline constexpr int32_t overlayAnchorScale = (int32_t)kTokens[463].num;
inline constexpr int32_t overlayRingY = (int32_t)kTokens[464].num;
inline constexpr int32_t overlayMinusY = (int32_t)kTokens[465].num;
inline constexpr int32_t overlayBottomMargin = (int32_t)kTokens[466].num;
inline constexpr int32_t overlayTextPitch = (int32_t)kTokens[467].num;
inline constexpr int32_t overlayTitleBottom = (int32_t)kTokens[468].num;
inline constexpr int32_t overlayLit = (int32_t)kTokens[469].num;
inline constexpr int32_t overlayDim = (int32_t)kTokens[470].num;
inline constexpr int32_t vol_ring = (int32_t)kTokens[471].num;
inline constexpr int32_t vol_ringStroke = (int32_t)kTokens[472].num;
inline constexpr int32_t vol_hit = (int32_t)kTokens[473].num;
inline constexpr int32_t vol_plus = (int32_t)kTokens[474].num;
inline constexpr int32_t vol_minus = (int32_t)kTokens[475].num;
inline constexpr int32_t vol_pause = (int32_t)kTokens[476].num;
inline constexpr int32_t vol_skip = (int32_t)kTokens[477].num;
inline constexpr int32_t vol_number = (int32_t)kTokens[478].num;
inline constexpr int32_t vol_label = (int32_t)kTokens[479].num;
inline constexpr int32_t vol_exit = (int32_t)kTokens[480].num;
inline constexpr int32_t batteryLowPercent = (int32_t)kTokens[481].num;
inline constexpr int32_t qpEdge = (int32_t)kTokens[482].num;
inline constexpr int32_t qpTile = (int32_t)kTokens[483].num;
inline constexpr int32_t qpGutter = (int32_t)kTokens[484].num;
inline constexpr int32_t qpW = (int32_t)kTokens[485].num;
inline constexpr int32_t qpFirstBaseline = (int32_t)kTokens[486].num;
inline constexpr int32_t qpContentDrop = (int32_t)kTokens[487].num;
inline constexpr int32_t qpSectionGap = (int32_t)kTokens[488].num;
inline constexpr int32_t qpHintDrop = (int32_t)kTokens[489].num;
inline constexpr int32_t qpHintPitch = (int32_t)kTokens[490].num;
inline constexpr int32_t qpHintW = (int32_t)kTokens[491].num;
inline constexpr int32_t qpTilePad = (int32_t)kTokens[492].num;
inline constexpr int32_t homeBarX = (int32_t)kTokens[493].num;
inline constexpr int32_t homeBarTop = (int32_t)kTokens[494].num;
inline constexpr int32_t homeBarH = (int32_t)kTokens[495].num;
inline constexpr int32_t focusBandBottom = (int32_t)kTokens[496].num;
inline constexpr int32_t motion_perspectiveRatio = (int32_t)kTokens[497].num;
inline constexpr int32_t motion_perspective = (int32_t)kTokens[498].num;
inline constexpr int32_t motion_turnstileOut = (int32_t)kTokens[499].num;
inline constexpr int32_t motion_turnstileOutHold = (int32_t)kTokens[500].num;
inline constexpr int32_t motion_turnstileIn = (int32_t)kTokens[501].num;
inline constexpr int32_t motion_turnstileInFade = (int32_t)kTokens[502].num;
inline constexpr int32_t motion_turnstileOutAngle = (int32_t)kTokens[503].num;
inline constexpr int32_t motion_turnstileInAngle = (int32_t)kTokens[504].num;
inline constexpr int32_t motion_headerOut = (int32_t)kTokens[505].num;
inline constexpr int32_t motion_headerOutShift = (int32_t)kTokens[506].num;
inline constexpr int32_t motion_headerOutDistance = (int32_t)kTokens[507].num;
inline constexpr int32_t motion_homeEnter = (int32_t)kTokens[508].num;
inline constexpr int32_t motion_homeEnterPeek = (int32_t)kTokens[509].num;
inline constexpr int32_t motion_homeEnterStagger = (int32_t)kTokens[510].num;
inline constexpr int32_t motion_homeEnterShift = (int32_t)kTokens[511].num;
inline constexpr int32_t motion_homeOut = (int32_t)kTokens[512].num;
inline constexpr int32_t motion_homeOutStagger = (int32_t)kTokens[513].num;
inline constexpr int32_t motion_homeOutShift = (int32_t)kTokens[514].num;
inline constexpr int32_t motion_homeOutDistance = (int32_t)kTokens[515].num;
inline constexpr int32_t motion_pivotOut = (int32_t)kTokens[516].num;
inline constexpr int32_t motion_pivotStrip = (int32_t)kTokens[517].num;
inline constexpr int32_t motion_pivotLabel = (int32_t)kTokens[518].num;
inline constexpr int32_t motion_pivotRows = (int32_t)kTokens[519].num;
inline constexpr int32_t motion_pivotShift = (int32_t)kTokens[520].num;
inline constexpr int32_t motion_pivotRowStagger = (int32_t)kTokens[521].num;
inline constexpr int32_t motion_pivotRowCap = (int32_t)kTokens[522].num;
inline constexpr int32_t motion_pivotHeaderParallax = (int32_t)kTokens[523].num;
inline constexpr int32_t motion_quickplay = (int32_t)kTokens[524].num;
inline constexpr int32_t motion_quickplayAngle = (int32_t)kTokens[525].num;
inline constexpr int32_t motion_quickplayScale = (int32_t)kTokens[526].num;
inline constexpr int32_t motion_quickplayAlpha = (int32_t)kTokens[527].num;
inline constexpr int32_t motion_quickplayMenuInset = (int32_t)kTokens[528].num;
inline constexpr int32_t motion_quickplayMenuX = (int32_t)kTokens[529].num;
inline constexpr int32_t motion_quickplayMenuY = (int32_t)kTokens[530].num;
inline constexpr int32_t motion_nowPlayingIn = (int32_t)kTokens[531].num;
inline constexpr int32_t motion_nowPlayingScale = (int32_t)kTokens[532].num;
inline constexpr int32_t motion_nowPlayingOriginX = (int32_t)kTokens[533].num;
inline constexpr int32_t motion_nowPlayingOriginY = (int32_t)kTokens[534].num;
inline constexpr int32_t motion_nowPlayingOut = (int32_t)kTokens[535].num;
inline constexpr int32_t motion_nowPlayingFade = (int32_t)kTokens[536].num;
inline constexpr int32_t motion_trackHide = (int32_t)kTokens[537].num;
inline constexpr int32_t motion_trackShow = (int32_t)kTokens[538].num;
inline constexpr int32_t motion_trackArtScale = (int32_t)kTokens[539].num;
inline constexpr int32_t motion_trackArtOriginX = (int32_t)kTokens[540].num;
inline constexpr int32_t motion_trackArtOriginY = (int32_t)kTokens[541].num;
inline constexpr int32_t motion_trackShiftOut = (int32_t)kTokens[542].num;
inline constexpr int32_t motion_trackShiftIn = (int32_t)kTokens[543].num;
inline constexpr int32_t motion_variantFade = (int32_t)kTokens[544].num;
inline constexpr int32_t motion_sheet = (int32_t)kTokens[545].num;
inline constexpr int32_t motion_sheetScrim = (int32_t)kTokens[546].num;
inline constexpr int32_t motion_dialog = (int32_t)kTokens[547].num;
inline constexpr int32_t motion_dialogHide = (int32_t)kTokens[548].num;
inline constexpr int32_t motion_dialogScale = (int32_t)kTokens[549].num;
inline constexpr int32_t motion_dialogScrim = (int32_t)kTokens[550].num;
inline constexpr int32_t motion_swivelIn = (int32_t)kTokens[551].num;
inline constexpr int32_t motion_swivelOut = (int32_t)kTokens[552].num;
inline constexpr int32_t motion_swivelAngle = (int32_t)kTokens[553].num;
inline constexpr int32_t motion_swivelDim = (int32_t)kTokens[554].num;
inline constexpr int32_t motion_swivelHingeY = (int32_t)kTokens[555].num;
inline constexpr int32_t motion_jumpHold = (int32_t)kTokens[556].num;
inline constexpr int32_t motion_jumpFade = (int32_t)kTokens[557].num;
inline constexpr int32_t motion_saver = (int32_t)kTokens[558].num;
inline constexpr int32_t motion_focusCursor = (int32_t)kTokens[559].num;
inline constexpr int32_t motion_focusAlpha = (int32_t)kTokens[560].num;
inline constexpr int32_t motion_scroll = (int32_t)kTokens[561].num;
inline constexpr int32_t motion_page = (int32_t)kTokens[562].num;
inline constexpr int32_t motion_press = (int32_t)kTokens[563].num;
inline constexpr int32_t motion_pressScale = (int32_t)kTokens[564].num;
inline constexpr int32_t motion_touchReturnDelay = (int32_t)kTokens[565].num;
inline constexpr int32_t motion_flingRetention = (int32_t)kTokens[566].num;
inline constexpr int32_t motion_flingTick = (int32_t)kTokens[567].num;
inline constexpr int32_t motion_flingWindow = (int32_t)kTokens[568].num;
inline constexpr int32_t motion_flingMaxStep = (int32_t)kTokens[569].num;
inline constexpr int32_t motion_toggle = (int32_t)kTokens[570].num;
inline constexpr int32_t motion_toggleScale = (int32_t)kTokens[571].num;
inline constexpr int32_t motion_toggleSwap = (int32_t)kTokens[572].num;
inline constexpr int32_t motion_idle = (int32_t)kTokens[573].num;
inline constexpr int32_t motion_idleFade = (int32_t)kTokens[574].num;
inline constexpr int32_t motion_idleAlpha = (int32_t)kTokens[575].num;
inline constexpr int32_t motion_progress = (int32_t)kTokens[576].num;
inline constexpr int32_t motion_elapsedTick = (int32_t)kTokens[577].num;
inline constexpr int32_t motion_valueChange = (int32_t)kTokens[578].num;
inline constexpr int32_t motion_valueDip = (int32_t)kTokens[579].num;
inline constexpr int32_t motion_pulse_t1 = (int32_t)kTokens[580].num;
inline constexpr int32_t motion_pulse_s1 = (int32_t)kTokens[581].num;
inline constexpr int32_t motion_pulse_t2 = (int32_t)kTokens[582].num;
inline constexpr int32_t motion_pulse_s2 = (int32_t)kTokens[583].num;
inline constexpr int32_t motion_pulse_t3 = (int32_t)kTokens[584].num;
inline constexpr int32_t motion_pulse_s3 = (int32_t)kTokens[585].num;
inline constexpr int32_t motion_pulse_t4 = (int32_t)kTokens[586].num;
inline constexpr int32_t motion_pulse_s4 = (int32_t)kTokens[587].num;
inline constexpr int32_t motion_volumeShow = (int32_t)kTokens[588].num;
inline constexpr int32_t motion_volumePop = (int32_t)kTokens[589].num;
inline constexpr int32_t motion_volumeHide = (int32_t)kTokens[590].num;
inline constexpr int32_t motion_volumeFade = (int32_t)kTokens[591].num;
inline constexpr int32_t motion_toastIn = (int32_t)kTokens[592].num;
inline constexpr int32_t motion_toastHold = (int32_t)kTokens[593].num;
inline constexpr int32_t motion_toastOut = (int32_t)kTokens[594].num;
inline constexpr int32_t motion_toastTravel = (int32_t)kTokens[595].num;
inline constexpr int32_t motion_lyricSlide = (int32_t)kTokens[596].num;
inline constexpr int32_t motion_listChange = (int32_t)kTokens[597].num;
inline constexpr int32_t motion_listChangeStagger = (int32_t)kTokens[598].num;
inline constexpr int32_t motion_listChangeCap = (int32_t)kTokens[599].num;
inline constexpr int32_t motion_listChangeShift = (int32_t)kTokens[600].num;
inline constexpr int32_t motion_saverWordSpeed = (int32_t)kTokens[601].num;
inline constexpr int32_t motion_saverTitleSpeed = (int32_t)kTokens[602].num;
inline constexpr int32_t motion_saverLabelSpeed = (int32_t)kTokens[603].num;
inline constexpr int32_t motion_saverFourthSpeed = (int32_t)kTokens[604].num;
inline constexpr int32_t motion_saverWordDelay = (int32_t)kTokens[605].num;
inline constexpr int32_t motion_saverTitleDelay = (int32_t)kTokens[606].num;
inline constexpr int32_t motion_saverLabelDelay = (int32_t)kTokens[607].num;
inline constexpr int32_t motion_saverFourthDelay = (int32_t)kTokens[608].num;
inline constexpr int32_t motion_saverWordEntry = (int32_t)kTokens[609].num;
inline constexpr int32_t motion_saverWordEntryShift = (int32_t)kTokens[610].num;
inline constexpr int32_t motion_saverWordFadeIn = (int32_t)kTokens[611].num;
inline constexpr int32_t motion_backdropFade = (int32_t)kTokens[612].num;
inline constexpr int32_t motion_decel_x1 = (int32_t)kTokens[613].num;
inline constexpr int32_t motion_decel_y1 = (int32_t)kTokens[614].num;
inline constexpr int32_t motion_decel_x2 = (int32_t)kTokens[615].num;
inline constexpr int32_t motion_decel_y2 = (int32_t)kTokens[616].num;
inline constexpr int32_t motion_strip_x1 = (int32_t)kTokens[617].num;
inline constexpr int32_t motion_strip_y1 = (int32_t)kTokens[618].num;
inline constexpr int32_t motion_strip_x2 = (int32_t)kTokens[619].num;
inline constexpr int32_t motion_strip_y2 = (int32_t)kTokens[620].num;
inline constexpr int32_t input_keyHoldMs = (int32_t)kTokens[621].num;
inline constexpr int32_t input_volumeHoldMs = (int32_t)kTokens[622].num;
inline constexpr int32_t input_volumeRepeatMs = (int32_t)kTokens[623].num;
inline constexpr int32_t input_remoteDoubleMs = (int32_t)kTokens[624].num;
inline constexpr int32_t input_remoteHoldMs = (int32_t)kTokens[625].num;
inline constexpr int32_t input_knobDoubleMs = (int32_t)kTokens[626].num;
inline constexpr int32_t input_knobHoldMs = (int32_t)kTokens[627].num;
inline constexpr int32_t input_knobHoldLongMs = (int32_t)kTokens[628].num;
inline constexpr int32_t input_modeExitHoldMs = (int32_t)kTokens[629].num;
inline constexpr int32_t input_searchDebounceMs = (int32_t)kTokens[630].num;
inline constexpr int32_t power_screenOffDefaultS = (int32_t)kTokens[631].num;
inline constexpr int32_t power_saverDefaultS = (int32_t)kTokens[632].num;
inline constexpr int32_t history_rows = (int32_t)kTokens[633].num;
inline constexpr int32_t history_tableCap = (int32_t)kTokens[634].num;
inline constexpr int32_t quickplay_tiles = (int32_t)kTokens[635].num;
inline constexpr int32_t bt_pairingS = (int32_t)kTokens[636].num;
inline constexpr int32_t bt_discoverS = (int32_t)kTokens[637].num;
inline constexpr int32_t btNameDrop = (int32_t)kTokens[638].num;
inline constexpr int32_t btStateDrop = (int32_t)kTokens[639].num;
inline constexpr int32_t btRowDrop = (int32_t)kTokens[640].num;
inline constexpr int32_t btRingDrop = (int32_t)kTokens[641].num;
inline constexpr int32_t eq_preampMin = (int32_t)kTokens[642].num;
inline constexpr int32_t eq_preampMax = (int32_t)kTokens[643].num;
inline constexpr int32_t eq_gainMin = (int32_t)kTokens[644].num;
inline constexpr int32_t eq_gainMax = (int32_t)kTokens[645].num;
inline constexpr int32_t eq_qMin = (int32_t)kTokens[646].num;
inline constexpr int32_t eq_qMax = (int32_t)kTokens[647].num;
inline constexpr int32_t eq_freqMin = (int32_t)kTokens[648].num;
inline constexpr int32_t eq_freqMax = (int32_t)kTokens[649].num;
inline constexpr int32_t eq_freqPoints = (int32_t)kTokens[650].num;
inline constexpr int32_t eq_bands = (int32_t)kTokens[651].num;
inline constexpr int32_t eq_profilesMax = (int32_t)kTokens[652].num;
inline constexpr int32_t eq_importsMax = (int32_t)kTokens[653].num;
inline constexpr int32_t settings_backupsMax = (int32_t)kTokens[654].num;
inline constexpr int32_t settings_backupMaxKib = (int32_t)kTokens[655].num;
inline constexpr int32_t eq_fileMaxKib = (int32_t)kTokens[656].num;
inline constexpr int32_t eq_importMs = (int32_t)kTokens[657].num;
inline constexpr int32_t volume_stepDefault = (int32_t)kTokens[658].num;
inline constexpr const char* settings_volumeSteps = kTokens[659].value;
inline constexpr const char* settings_saverSteps = kTokens[660].value;
inline constexpr const char* settings_screenOffSteps = kTokens[661].value;
inline constexpr const char* settings_sleepSteps = kTokens[662].value;
inline constexpr const char* settings_idleSteps = kTokens[663].value;
inline constexpr int32_t settings_saverDefault = (int32_t)kTokens[664].num;
inline constexpr int32_t settings_screenOffDefault = (int32_t)kTokens[665].num;
inline constexpr int32_t settings_idleDefault = (int32_t)kTokens[666].num;
inline constexpr int32_t settings_writeDelay = (int32_t)kTokens[667].num;
inline constexpr int32_t settings_yearMin = (int32_t)kTokens[668].num;
inline constexpr int32_t settings_yearMax = (int32_t)kTokens[669].num;
inline constexpr int32_t settings_clockYearMin = (int32_t)kTokens[670].num;
inline constexpr int32_t update_batteryMin = (int32_t)kTokens[671].num;
inline constexpr int32_t volume_percentDbDivisor = (int32_t)kTokens[672].num;
inline constexpr int32_t settings_filterCount = (int32_t)kTokens[673].num;
inline constexpr int32_t settings_balanceMax = (int32_t)kTokens[674].num;
inline constexpr int32_t settings_balanceStep = (int32_t)kTokens[675].num;
inline constexpr const char* settings_maxVolumeSteps = kTokens[676].value;
inline constexpr int32_t settings_maxVolumeDefault = (int32_t)kTokens[677].num;
inline constexpr const char* settings_powerOnSteps = kTokens[678].value;
inline constexpr const char* settings_crossfadeSteps = kTokens[679].value;
inline constexpr int32_t settings_crossfadeDefault = (int32_t)kTokens[680].num;
inline constexpr const char* settings_brightnessSteps = kTokens[681].value;
inline constexpr int32_t settings_brightnessDefault = (int32_t)kTokens[682].num;
inline constexpr int32_t homeRingX = (int32_t)kTokens[683].num;
inline constexpr int32_t homeRingY = (int32_t)kTokens[684].num;
inline constexpr int32_t homeRingSize = (int32_t)kTokens[685].num;
inline constexpr int32_t homeRingStroke = (int32_t)kTokens[686].num;
inline constexpr int32_t homeRingHitBottom = (int32_t)kTokens[687].num;
inline constexpr int32_t npSeekBandX0 = (int32_t)kTokens[688].num;
inline constexpr int32_t npSeekBandX1 = (int32_t)kTokens[689].num;
inline constexpr int32_t npSeekBandBottom = (int32_t)kTokens[690].num;
inline constexpr int32_t npMarkHit = (int32_t)kTokens[691].num;
inline constexpr int32_t npLyricPitch = (int32_t)kTokens[692].num;
inline constexpr int32_t npLyricBottom = (int32_t)kTokens[693].num;
inline constexpr int32_t saverLyricBaseline = (int32_t)kTokens[694].num;
inline constexpr int32_t size_largeScale = (int32_t)kTokens[695].num;
inline constexpr int32_t large_type_npTitle_size = (int32_t)kTokens[696].num;
inline constexpr int32_t large_type_npNext_size = (int32_t)kTokens[697].num;
inline constexpr int32_t large_type_npAlbum_size = (int32_t)kTokens[698].num;
inline constexpr int32_t large_npLyricPitch = (int32_t)kTokens[699].num;
inline constexpr int32_t large_npNextPitch = (int32_t)kTokens[700].num;
inline constexpr int32_t large_npAlbumPitch = (int32_t)kTokens[701].num;
inline constexpr int32_t wifi_levelHigh = (int32_t)kTokens[702].num;
inline constexpr int32_t wifi_levelMid = (int32_t)kTokens[703].num;
inline constexpr int32_t gesture_swipe = (int32_t)kTokens[704].num;
inline constexpr int32_t gesture_edge = (int32_t)kTokens[705].num;
inline constexpr int32_t gesture_edgeRight = (int32_t)kTokens[706].num;
inline constexpr int32_t gesture_bottom = (int32_t)kTokens[707].num;
inline constexpr int32_t homeWatermarkGap = (int32_t)kTokens[708].num;
inline constexpr int32_t navH = (int32_t)kTokens[709].num;
inline constexpr int32_t navGlyph = (int32_t)kTokens[710].num;
inline constexpr int32_t adjustExit = (int32_t)kTokens[711].num;
inline constexpr int32_t adjustColumn = (int32_t)kTokens[712].num;
inline constexpr int32_t adjustNumberDrop = (int32_t)kTokens[713].num;
inline constexpr int32_t backdrop_coverSigma = (int32_t)kTokens[714].num;
inline constexpr int32_t backdrop_coverGrid = (int32_t)kTokens[715].num;

}
