/**
 * @file VAlignTheme.h
 * @brief Colour-scheme enum and Theme struct shared by VAlignEditorDockWidget,
 *        VAlignPropertiesPanel, and VAlignProfileView.
 *
 * Kept as a standalone header with no widget dependencies so that both
 * VAlignEditorDockWidget.cpp and VAlignProfileView.cpp can include it
 * without creating a circular dependency.
 */
#pragma once

#include <QColor>

namespace aicad {
namespace ui {

// ─────────────────────────────────────────────────────────────────────────────
//  ColourScheme enum
// ─────────────────────────────────────────────────────────────────────────────

enum class ColorScheme {
    A_OriginalDark = 0,  // 原始深藍黑
    B_SlateDark,         // 石板灰深色
    C_ForestDark,        // 深林綠深色
    D_CharcoalAmber,     // 木炭琥珀深色
    E_LightSteel,        // 淺鋼藍亮色
    F_WarmIvory,         // 暖米白亮色
    G_MintWhite,         // 薄荷白亮色
    H_PaperWhite,        // 純白紙質亮色
};

// ─────────────────────────────────────────────────────────────────────────────
//  Theme struct
// ─────────────────────────────────────────────────────────────────────────────

struct Theme {
    // Backgrounds
    QColor bg;             // main background
    QColor bgPanel;        // sidebar / header background
    QColor bgInput;        // spinbox background
    QColor bgListSel;      // list item selected background
    QColor bgListHov;      // list item hover background
    QColor bgApply;        // apply button background
    QColor bgDelete;       // delete button background
    QColor bgScrollHandle; // scrollbar handle
    // Borders
    QColor border;
    QColor borderInput;
    QColor borderApply;
    QColor borderDelete;
    // Text
    QColor textMain;
    QColor textTitle;
    QColor textSub;
    QColor textInput;
    QColor textApply;
    QColor textDelete;
    QColor textListSel;
    QColor textListNorm;
    QColor textCursor;
    // Accent colours (for data values)
    QColor accentGradePos;
    QColor accentGradeNeg;
    QColor accentGradeZero;
    QColor accentVcType;
    QColor accentKval;
    QColor accentLen;
    QColor accentDg;
    QColor accentHTangent;
    QColor accentHCircular;
    QColor accentHSpiral;
    QColor hintIcon;
    QColor hintText;
    // Toolbar
    QColor tbBg;
    QColor tbBorder;
    QColor tbBtnBorder;
    QColor tbBtnText;
    QColor tbBtnCheckedBg;
    QColor tbBtnCheckedBorder;
    QColor tbBtnCheckedText;
    QColor tbSeparator;
    // Splitter
    QColor splitterHandle;
    // dark or light background?
    bool dark = true;
};

// ─────────────────────────────────────────────────────────────────────────────
//  Factory — returns a fully populated Theme for the given scheme
// ─────────────────────────────────────────────────────────────────────────────

Theme makeTheme(ColorScheme s);

} // namespace ui
} // namespace aicad
