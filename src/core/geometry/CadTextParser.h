/**
 * @file CadTextParser.h
 * @brief 解析 AutoCAD 端（tools/AICAD-CopyEmf.lsp 的 CPA 指令）輸出的「原始座標文字」。
 *
 * 為什麼不用 EMF：剪貼簿的 EMF 只是預覽圖，座標是螢幕像素加世界轉換，不含圖面原始座標
 * （見 1.emf 的分析）。Inventor 貼上能保持原尺寸，是因為它讀 AutoCAD 的私有剪貼簿格式。
 * VirtualBox 共用剪貼簿只傳文字／點陣圖，所以改由 AutoLISP 直接輸出原始座標成純文字。
 *
 * 格式（ASCII，每行一個圖元，數字以空白分隔，座標為圖面 WCS 的原始值）：
 *   AICAD-CAD1                         ← 第一行，固定
 *   L  x1 y1 x2 y2                     線
 *   C  cx cy r                         圓
 *   A  x1 y1 x2 y2 x3 y3               圓弧：起點、弧上任一中間點、終點（WCS，不受 OCS 影響）
 *   P  closed n  x y bulge ...         LWPOLYLINE：n 個頂點，每點接「頂點座標＋到下一點的 bulge」
 *   E  cx cy ax ay ratio p0 p1         橢圓：中心、長軸端點向量、短／長軸比、起訖參數（弧度）
 *   S  closed n  x y ...               已取樣的曲線（樣條等）→ 折線
 * bulge 會還原成真正的圓弧（不是折線近似）。其他行視為看不懂，計入 unknownRecords。
 *
 * 純標準 C++，輸出沿用 emf::Result / emf::Primitive，座標單位 = 圖面單位（不縮放）。
 */
#ifndef AICAD_CORE_GEOMETRY_CADTEXTPARSER_H
#define AICAD_CORE_GEOMETRY_CADTEXTPARSER_H

#include "EmfVectorParser.h"
#include <string>

namespace aicad {
namespace core {
namespace cadtext {

/// 文字是否以 "AICAD-CAD1" 開頭
bool looksLikeCadText(const std::string& text);

emf::Result parse(const std::string& text);

} // namespace cadtext
} // namespace core
} // namespace aicad

#endif
