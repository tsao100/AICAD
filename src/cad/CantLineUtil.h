/**
 * @file CantLineUtil.h
 * @brief 在斷面草圖（測站的 SketchInstance）中找出「受 cant 約束的直線」（軌道面斷線）。
 *
 * 辨識規則（與 TrackSlabPlan 相同）：
 *   1. 參數表中，`cant` 與所有（遞移）依賴 cant 的參數 → 名稱集合
 *   2. 運算式依賴該集合的尺寸標註所參照的點（參照到線則取其兩端點）
 *   3. 兩端點都屬於這些點的直線（優先建構線）
 *
 * 回傳的座標是「斷面草圖座標」乘上 instance 的 scale（→ 公尺），尚未放到世界座標；
 * 組件定位需要在斷面平面內對兩個測站的斷線做內插，所以在此保留 (u, v) 形式。
 */
#pragma once

#include <QSet>
#include <QString>
#include <QVector2D>

namespace aicad {
namespace core { class ParameterStore; }
namespace cad {

class SketchInstance;

struct CantLineUV {
    bool      ok = false;
    QVector2D a;          ///< 斷線起點（斷面座標 × scale，單位 m）
    QVector2D b;          ///< 斷線終點
};

/// `cant` 本身 + 所有（遞移）依賴 cant 的參數名稱
QSet<QString> cantDependentNames(const core::ParameterStore* store);

class Sketch;
struct SketchLine;

/// 在任一草圖（主草圖或測站檢視草圖）中找「受 cant 約束的直線」。
/// 角色為 Centerline 的線（= 組件座標線）一律不算，避免被使用者加的 cant 運算式尺寸誤認。
SketchLine* locateCantLine(Sketch* sketch, const QSet<QString>& cantNames, QString* err = nullptr);

/// 取某測站的軌道面斷線；找不到時 ok = false 並在 err 說明原因
CantLineUV findCantLineUV(SketchInstance* inst, const QSet<QString>& cantNames, QString* err = nullptr);

} // namespace cad
} // namespace aicad
