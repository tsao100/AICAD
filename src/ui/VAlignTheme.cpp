/**
 * @file VAlignTheme.cpp
 * @brief Implementation of makeTheme() — the sole source of truth for all
 *        colour-scheme definitions used by VAlignEditorDockWidget,
 *        VAlignPropertiesPanel, and VAlignProfileView.
 */

#include "VAlignTheme.h"

namespace aicad {
namespace ui {

Theme makeTheme(ColorScheme s)
{
    using CS = ColorScheme;
    Theme t;

    switch (s) {

        // ── A: Original dark blue-black (原始) ───────────────────────────────────
    case CS::A_OriginalDark:
        t.dark              = true;
        t.bg                = QColor("#050810");
        t.bgPanel           = QColor("#04070e");
        t.bgInput           = QColor("#020509");
        t.bgListSel         = QColor("#081628");
        t.bgListHov         = QColor("#060e1c");
        t.bgApply           = QColor("#061428");
        t.bgDelete          = QColor("#140810");
        t.bgScrollHandle    = QColor("#0e2438");
        t.border            = QColor("#0b1a2c");
        t.borderInput       = QColor("#0b1c2e");
        t.borderApply       = QColor("#144268");
        t.borderDelete      = QColor("#320e1c");
        t.textMain          = QColor("#2a5070");
        t.textTitle         = QColor("#0e3458");
        t.textSub           = QColor("#0b1c30");
        t.textInput         = QColor("#1470b8");
        t.textApply         = QColor("#1878c0");
        t.textDelete        = QColor("#b03050");
        t.textListSel       = QColor("#1868b0");
        t.textListNorm      = QColor("#123450");
        t.textCursor        = QColor("#105888");
        t.accentGradePos    = QColor("#38c858");
        t.accentGradeNeg    = QColor("#d84040");
        t.accentGradeZero   = QColor("#4488a0");
        t.accentVcType      = QColor("#00c8b8");
        t.accentKval        = QColor("#7830c8");
        t.accentLen         = QColor("#3a6088");
        t.accentDg          = QColor("#485898");
        t.accentHTangent    = QColor("#1c60c8");
        t.accentHCircular   = QColor("#00a8cc");
        t.accentHSpiral     = QColor("#6828d8");
        t.hintIcon          = QColor(255,255,255,15);
        t.hintText          = QColor("#0b1e2e");
        t.tbBg              = QColor("#04070f");
        t.tbBorder          = QColor("#0c1c2e");
        t.tbBtnBorder       = QColor("#0a1828");
        t.tbBtnText         = QColor("#1c3c58");
        t.tbBtnCheckedBg    = QColor(20,100,180,90);
        t.tbBtnCheckedBorder= QColor("#1a6ab0");
        t.tbBtnCheckedText  = QColor("#48b8ff");
        t.tbSeparator       = QColor("#0e2034");
        t.splitterHandle    = QColor("#0b1a2c");
        break;

        // ── B: Slate neutral mid-dark ────────────────────────────────────────────
    case CS::B_SlateDark:
        t.dark              = true;
        t.bg                = QColor("#1a1a1f");
        t.bgPanel           = QColor("#141418");
        t.bgInput           = QColor("#111116");
        t.bgListSel         = QColor("#232340");
        t.bgListHov         = QColor("#1c1c28");
        t.bgApply           = QColor("#232338");
        t.bgDelete          = QColor("#28181e");
        t.bgScrollHandle    = QColor("#38385a");
        t.border            = QColor("#2e2e38");
        t.borderInput       = QColor("#353548");
        t.borderApply       = QColor("#3a3a60");
        t.borderDelete      = QColor("#482030");
        t.textMain          = QColor("#8a8a9a");
        t.textTitle         = QColor("#c0c0d4");
        t.textSub           = QColor("#50505e");
        t.textInput         = QColor("#9090c8");
        t.textApply         = QColor("#8090d8");
        t.textDelete        = QColor("#e0607a");
        t.textListSel       = QColor("#a0a8e0");
        t.textListNorm      = QColor("#686880");
        t.textCursor        = QColor("#6878a8");
        t.accentGradePos    = QColor("#52d68a");
        t.accentGradeNeg    = QColor("#e0607a");
        t.accentGradeZero   = QColor("#7888a8");
        t.accentVcType      = QColor("#38c8c0");
        t.accentKval        = QColor("#b87bde");
        t.accentLen         = QColor("#7080b8");
        t.accentDg          = QColor("#8888b8");
        t.accentHTangent    = QColor("#7b8cde");
        t.accentHCircular   = QColor("#40c8d8");
        t.accentHSpiral     = QColor("#9878de");
        t.hintIcon          = QColor(255,255,255,18);
        t.hintText          = QColor("#383848");
        t.tbBg              = QColor("#141418");
        t.tbBorder          = QColor("#2e2e38");
        t.tbBtnBorder       = QColor("#282838");
        t.tbBtnText         = QColor("#585870");
        t.tbBtnCheckedBg    = QColor(80,90,180,90);
        t.tbBtnCheckedBorder= QColor("#5060b0");
        t.tbBtnCheckedText  = QColor("#a0b0ff");
        t.tbSeparator       = QColor("#2e2e48");
        t.splitterHandle    = QColor("#2a2a38");
        break;

        // ── C: Forest teal-green dark ────────────────────────────────────────────
    case CS::C_ForestDark:
        t.dark              = true;
        t.bg                = QColor("#101814");
        t.bgPanel           = QColor("#0c1410");
        t.bgInput           = QColor("#0a100d");
        t.bgListSel         = QColor("#142018");
        t.bgListHov         = QColor("#101a14");
        t.bgApply           = QColor("#101e16");
        t.bgDelete          = QColor("#1c1010");
        t.bgScrollHandle    = QColor("#2a4830");
        t.border            = QColor("#1e2e24");
        t.borderInput       = QColor("#203028");
        t.borderApply       = QColor("#1e4830");
        t.borderDelete      = QColor("#401818");
        t.textMain          = QColor("#5a8a72");
        t.textTitle         = QColor("#8fc8a8");
        t.textSub           = QColor("#2e4a38");
        t.textInput         = QColor("#60a880");
        t.textApply         = QColor("#38b880");
        t.textDelete        = QColor("#e06060");
        t.textListSel       = QColor("#70c898");
        t.textListNorm      = QColor("#3a6050");
        t.textCursor        = QColor("#4a8868");
        t.accentGradePos    = QColor("#a8e060");
        t.accentGradeNeg    = QColor("#e06060");
        t.accentGradeZero   = QColor("#608880");
        t.accentVcType      = QColor("#40d8b8");
        t.accentKval        = QColor("#80a8ff");
        t.accentLen         = QColor("#508878");
        t.accentDg          = QColor("#70a888");
        t.accentHTangent    = QColor("#4898e0");
        t.accentHCircular   = QColor("#30d0c0");
        t.accentHSpiral     = QColor("#9080f0");
        t.hintIcon          = QColor(255,255,255,12);
        t.hintText          = QColor("#203828");
        t.tbBg              = QColor("#0c1410");
        t.tbBorder          = QColor("#1e2e24");
        t.tbBtnBorder       = QColor("#182820");
        t.tbBtnText         = QColor("#3a6050");
        t.tbBtnCheckedBg    = QColor(40,140,100,90);
        t.tbBtnCheckedBorder= QColor("#2a7856");
        t.tbBtnCheckedText  = QColor("#60e0a8");
        t.tbSeparator       = QColor("#1e3828");
        t.splitterHandle    = QColor("#1a2e22");
        break;

        // ── D: Charcoal warm amber ───────────────────────────────────────────────
    case CS::D_CharcoalAmber:
        t.dark              = true;
        t.bg                = QColor("#1c1814");
        t.bgPanel           = QColor("#171310");
        t.bgInput           = QColor("#12100d");
        t.bgListSel         = QColor("#221c14");
        t.bgListHov         = QColor("#1a1510");
        t.bgApply           = QColor("#201810");
        t.bgDelete          = QColor("#201010");
        t.bgScrollHandle    = QColor("#403020");
        t.border            = QColor("#302820");
        t.borderInput       = QColor("#382e20");
        t.borderApply       = QColor("#483010");
        t.borderDelete      = QColor("#401818");
        t.textMain          = QColor("#8a7860");
        t.textTitle         = QColor("#c8a878");
        t.textSub           = QColor("#4a3c2c");
        t.textInput         = QColor("#b09060");
        t.textApply         = QColor("#c89840");
        t.textDelete        = QColor("#d86060");
        t.textListSel       = QColor("#d8b070");
        t.textListNorm      = QColor("#605040");
        t.textCursor        = QColor("#988060");
        t.accentGradePos    = QColor("#88cc60");
        t.accentGradeNeg    = QColor("#d86060");
        t.accentGradeZero   = QColor("#808868");
        t.accentVcType      = QColor("#50c8b0");
        t.accentKval        = QColor("#c878e0");
        t.accentLen         = QColor("#a08850");
        t.accentDg          = QColor("#b09060");
        t.accentHTangent    = QColor("#7898d8");
        t.accentHCircular   = QColor("#40c0c8");
        t.accentHSpiral     = QColor("#b070e0");
        t.hintIcon          = QColor(255,255,255,12);
        t.hintText          = QColor("#362c20");
        t.tbBg              = QColor("#171310");
        t.tbBorder          = QColor("#302820");
        t.tbBtnBorder       = QColor("#282018");
        t.tbBtnText         = QColor("#6a5840");
        t.tbBtnCheckedBg    = QColor(160,100,20,90);
        t.tbBtnCheckedBorder= QColor("#705020");
        t.tbBtnCheckedText  = QColor("#e8b060");
        t.tbSeparator       = QColor("#382c18");
        t.splitterHandle    = QColor("#2a2218");
        break;

        // ── E: Light steel blue ──────────────────────────────────────────────────
    case CS::E_LightSteel:
        t.dark              = false;
        t.bg                = QColor("#f0f4f8");
        t.bgPanel           = QColor("#e4ecf2");
        t.bgInput           = QColor("#ffffff");
        t.bgListSel         = QColor("#deeaf6");
        t.bgListHov         = QColor("#edf4fa");
        t.bgApply           = QColor("#deeaf6");
        t.bgDelete          = QColor("#f6e0e0");
        t.bgScrollHandle    = QColor("#a0c0d8");
        t.border            = QColor("#c8d4de");
        t.borderInput       = QColor("#b0c4d8");
        t.borderApply       = QColor("#90b8e0");
        t.borderDelete      = QColor("#e0a0a0");
        t.textMain          = QColor("#3a5468");
        t.textTitle         = QColor("#1a3650");
        t.textSub           = QColor("#607888");
        t.textInput         = QColor("#1050a0");
        t.textApply         = QColor("#1060b0");
        t.textDelete        = QColor("#b02030");
        t.textListSel       = QColor("#1050a0");
        t.textListNorm      = QColor("#3a5468");
        t.textCursor        = QColor("#3a6888");
        t.accentGradePos    = QColor("#1a8840");
        t.accentGradeNeg    = QColor("#c02030");
        t.accentGradeZero   = QColor("#3878a0");
        t.accentVcType      = QColor("#0898a0");
        t.accentKval        = QColor("#6030b8");
        t.accentLen         = QColor("#1860a8");
        t.accentDg          = QColor("#2868a0");
        t.accentHTangent    = QColor("#1060b8");
        t.accentHCircular   = QColor("#0090b0");
        t.accentHSpiral     = QColor("#6030c0");
        t.hintIcon          = QColor(0,0,0,20);
        t.hintText          = QColor("#7090a8");
        t.tbBg              = QColor("#e4ecf2");
        t.tbBorder          = QColor("#c8d4de");
        t.tbBtnBorder       = QColor("#b8c8d8");
        t.tbBtnText         = QColor("#3a5468");
        t.tbBtnCheckedBg    = QColor(20,100,180,38);
        t.tbBtnCheckedBorder= QColor("#80b0d8");
        t.tbBtnCheckedText  = QColor("#1060a8");
        t.tbSeparator       = QColor("#b8c8d8");
        t.splitterHandle    = QColor("#c0d0dc");
        break;

        // ── F: Warm ivory earth tones ────────────────────────────────────────────
    case CS::F_WarmIvory:
        t.dark              = false;
        t.bg                = QColor("#f8f4ec");
        t.bgPanel           = QColor("#f0e8d8");
        t.bgInput           = QColor("#ffffff");
        t.bgListSel         = QColor("#e8ddc8");
        t.bgListHov         = QColor("#f4edd8");
        t.bgApply           = QColor("#e8ddc8");
        t.bgDelete          = QColor("#f4e0d8");
        t.bgScrollHandle    = QColor("#c0a870");
        t.border            = QColor("#d8d0c0");
        t.borderInput       = QColor("#c8b898");
        t.borderApply       = QColor("#c0a870");
        t.borderDelete      = QColor("#d8a898");
        t.textMain          = QColor("#5a4a32");
        t.textTitle         = QColor("#2c200e");
        t.textSub           = QColor("#8a7860");
        t.textInput         = QColor("#7a5c28");
        t.textApply         = QColor("#806020");
        t.textDelete        = QColor("#b02828");
        t.textListSel       = QColor("#806020");
        t.textListNorm      = QColor("#5a4a32");
        t.textCursor        = QColor("#7a6848");
        t.accentGradePos    = QColor("#2a8040");
        t.accentGradeNeg    = QColor("#b82828");
        t.accentGradeZero   = QColor("#507868");
        t.accentVcType      = QColor("#208878");
        t.accentKval        = QColor("#7030a8");
        t.accentLen         = QColor("#a06018");
        t.accentDg          = QColor("#806848");
        t.accentHTangent    = QColor("#3060b8");
        t.accentHCircular   = QColor("#1898a0");
        t.accentHSpiral     = QColor("#6030a8");
        t.hintIcon          = QColor(0,0,0,18);
        t.hintText          = QColor("#9a8870");
        t.tbBg              = QColor("#f0e8d8");
        t.tbBorder          = QColor("#d8d0c0");
        t.tbBtnBorder       = QColor("#c8b898");
        t.tbBtnText         = QColor("#5a4a32");
        t.tbBtnCheckedBg    = QColor(140,100,20,38);
        t.tbBtnCheckedBorder= QColor("#c0a060");
        t.tbBtnCheckedText  = QColor("#806020");
        t.tbSeparator       = QColor("#c8b898");
        t.splitterHandle    = QColor("#d0c8b0");
        break;

        // ── G: Mint white teal accent ────────────────────────────────────────────
    case CS::G_MintWhite:
        t.dark              = false;
        t.bg                = QColor("#f2f9f6");
        t.bgPanel           = QColor("#e4f2ec");
        t.bgInput           = QColor("#ffffff");
        t.bgListSel         = QColor("#d8eee6");
        t.bgListHov         = QColor("#eaf5f0");
        t.bgApply           = QColor("#d8eee6");
        t.bgDelete          = QColor("#f6e0e0");
        t.bgScrollHandle    = QColor("#88c8b0");
        t.border            = QColor("#c0d8d0");
        t.borderInput       = QColor("#a8d0c0");
        t.borderApply       = QColor("#88c8b0");
        t.borderDelete      = QColor("#e0a0a0");
        t.textMain          = QColor("#2a5040");
        t.textTitle         = QColor("#103828");
        t.textSub           = QColor("#5a8070");
        t.textInput         = QColor("#0a6848");
        t.textApply         = QColor("#0a6848");
        t.textDelete        = QColor("#b82828");
        t.textListSel       = QColor("#0a6848");
        t.textListNorm      = QColor("#2a5040");
        t.textCursor        = QColor("#3a7860");
        t.accentGradePos    = QColor("#1a7840");
        t.accentGradeNeg    = QColor("#c02828");
        t.accentGradeZero   = QColor("#308878");
        t.accentVcType      = QColor("#0a8878");
        t.accentKval        = QColor("#5838a8");
        t.accentLen         = QColor("#1878a0");
        t.accentDg          = QColor("#287868");
        t.accentHTangent    = QColor("#2068b8");
        t.accentHCircular   = QColor("#0898a0");
        t.accentHSpiral     = QColor("#5828b8");
        t.hintIcon          = QColor(0,0,0,18);
        t.hintText          = QColor("#6a9888");
        t.tbBg              = QColor("#e4f2ec");
        t.tbBorder          = QColor("#c0d8d0");
        t.tbBtnBorder       = QColor("#a8d0c0");
        t.tbBtnText         = QColor("#2a5040");
        t.tbBtnCheckedBg    = QColor(10,120,100,38);
        t.tbBtnCheckedBorder= QColor("#60b8a0");
        t.tbBtnCheckedText  = QColor("#0a6848");
        t.tbSeparator       = QColor("#a8d0c0");
        t.splitterHandle    = QColor("#b8d8cc");
        break;

        // ── H: Paper white classic CAD ───────────────────────────────────────────
    case CS::H_PaperWhite:
    default:
        t.dark              = false;
        t.bg                = QColor("#ffffff");
        t.bgPanel           = QColor("#f4f4f4");
        t.bgInput           = QColor("#ffffff");
        t.bgListSel         = QColor("#e8f0f8");
        t.bgListHov         = QColor("#f0f4f8");
        t.bgApply           = QColor("#e8f0f8");
        t.bgDelete          = QColor("#f8e8e8");
        t.bgScrollHandle    = QColor("#a0b8c8");
        t.border            = QColor("#c8c8c8");
        t.borderInput       = QColor("#b0b0b0");
        t.borderApply       = QColor("#90b0d8");
        t.borderDelete      = QColor("#d89090");
        t.textMain          = QColor("#303030");
        t.textTitle         = QColor("#101010");
        t.textSub           = QColor("#606060");
        t.textInput         = QColor("#101010");
        t.textApply         = QColor("#1050a0");
        t.textDelete        = QColor("#b02020");
        t.textListSel       = QColor("#1050a0");
        t.textListNorm      = QColor("#303030");
        t.textCursor        = QColor("#505050");
        t.accentGradePos    = QColor("#1a7030");
        t.accentGradeNeg    = QColor("#b82020");
        t.accentGradeZero   = QColor("#307888");
        t.accentVcType      = QColor("#007888");
        t.accentKval        = QColor("#5820a0");
        t.accentLen         = QColor("#1050a0");
        t.accentDg          = QColor("#204880");
        t.accentHTangent    = QColor("#1050b8");
        t.accentHCircular   = QColor("#0090a8");
        t.accentHSpiral     = QColor("#5820b8");
        t.hintIcon          = QColor(0,0,0,20);
        t.hintText          = QColor("#909090");
        t.tbBg              = QColor("#f4f4f4");
        t.tbBorder          = QColor("#c8c8c8");
        t.tbBtnBorder       = QColor("#b8b8b8");
        t.tbBtnText         = QColor("#303030");
        t.tbBtnCheckedBg    = QColor(16,80,160,30);
        t.tbBtnCheckedBorder= QColor("#80a8d8");
        t.tbBtnCheckedText  = QColor("#1050a0");
        t.tbSeparator       = QColor("#c0c0c0");
        t.splitterHandle    = QColor("#c8c8c8");
        break;
    }

    return t;
}

} // namespace ui
} // namespace aicad
