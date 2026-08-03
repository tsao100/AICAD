/**
 * @file SketchGeom2DMath.h
 * @brief Phase 4 共用建設：TRIM/EXTEND（未來 FILLET/CHAMFER）共用的 2D
 *        解析幾何運算（見 AICAD_SketchEdit_Advanced_Commands_Plan.md §4）。
 *
 * Sketch 內部幾何存的是 QVector2D（平面座標），不是 Geom2d_Curve，因此
 * 交點運算改走純數學解析公式，而不是建立 OCCT Geom2d 物件呼叫
 * Geom2dAPI_InterCurveCurve——效能較好，也讓 TRIM/EXTEND 每次互動要重算
 * 的路徑單純、好除錯。本檔案完全不依賴 OCCT／Sketch，只操作 QVector2D，
 * 方便獨立驗證。
 *
 * Arc 的交點運算不在這裡：Arc 在幾何上等同「它所在的完整圓」，因此 Arc
 * 與其他幾何的交點＝用 Arc 的圓心/半徑呼叫這裡的 lineCircleIntersect()/
 * circleCircleIntersect()，再用 Arc 自己的起訖角度篩選「交點是否真的落在
 * Arc 的弧段範圍內」。這個「篩選」的步驟需要 OCCT（ElCLib::Parameter 取得
 * 點在圓上的角度），因此放在 TrimExtendHelper（會操作 Sketch/OCCT）而不是
 * 這個純數學檔案裡。
 */
#pragma once

#include <QVector2D>
#include <QVector>
#include <optional>

namespace aicad {
namespace cad {
namespace geom2d {

/**
 * @brief 兩條「無限長直線」（分別過 p1,p2 與 p3,p4）的交點。
 * @return 平行或重合時回傳 std::nullopt。
 */
std::optional<QVector2D> lineLineIntersect(const QVector2D& p1, const QVector2D& p2,
                                           const QVector2D& p3, const QVector2D& p4);

/**
 * @brief 「無限長直線」（過 lp1,lp2）與圓（center,radius）的交點。
 * @return 0~2 個交點（相切為 1 個，不相交為 0 個）。
 */
QVector<QVector2D> lineCircleIntersect(const QVector2D& lp1, const QVector2D& lp2,
                                       const QVector2D& center, double radius);

/**
 * @brief 兩個圓的交點。
 * @return 0~2 個交點（相切為 1 個，不相交／同心不同半徑為 0 個）。
 *         兩圓完全重合（同心同半徑）視為無限多交點，回傳空陣列並由呼叫端
 *         自行決定如何處理（TRIM/EXTEND 對此情形應視為「無法裁切/延伸」）。
 */
QVector<QVector2D> circleCircleIntersect(const QVector2D& c1, double r1,
                                         const QVector2D& c2, double r2);

/**
 * @brief 點 p 在直線 a→b 上的參數 t（p ≈ a + t·(b-a)）。
 *
 * 若 p 不完全共線（例如浮點誤差、或呼叫端傳入本來就不共線的點），回傳
 * p 在 a→b 方向上的投影參數（即 p 到直線 a-b 的垂足對應的 t 值）。
 * t=0 對應 a，t=1 對應 b；t<0 或 t>1 表示落在線段外側（TRIM/EXTEND 用
 * 這個判斷交點是否落在線段本身範圍內，或用來排序多個交點沿線的先後順序）。
 *
 * 若 a、b 重合（退化線段），回傳 0.0。
 */
double paramOnLine(const QVector2D& p, const QVector2D& a, const QVector2D& b);

/**
 * @brief 「無限長直線」（過 lp1,lp2）與橢圓（center/majorRadius/minorRadius/
 *        angleRad，angleRad 為長軸方向角，弧度）的交點。
 *
 * 內部做法：把直線轉換到橢圓的「單位圓局部座標系」（先平移到橢圓中心、
 * 再旋轉 -angleRad、再依 (1/majorRadius, 1/minorRadius) 縮放），此時橢圓
 * 變成單位圓，可直接呼叫 lineCircleIntersect() 求解，交點再反變換回原座標
 * 系。僅支援「直線 vs 橢圓」，不支援橢圓與圓/橢圓與橢圓（見
 * TrimExtendHelper.h 的 MVP 範圍說明）。
 *
 * @return 0~2 個交點。
 */
QVector<QVector2D> lineEllipseIntersect(const QVector2D& lp1, const QVector2D& lp2,
                                        const QVector2D& center,
                                        double majorRadius, double minorRadius,
                                        double angleRad);

/**
 * @brief 把角度（弧度）正規化到 [0, 2π) 範圍。
 */
double normalizeAngle(double angleRad);

/**
 * @brief 判斷 angle 是否落在從 start 逆時針掃到 end 的角度範圍內（處理
 *        跨越 0/2π 的情形）。三個角度都先內部正規化到 [0, 2π) 再比較。
 *
 * 用於判斷「圓上某交點的角度是否真的落在某個 Arc 的弧段範圍內」——呼叫端
 * 需自行確保 start/end 的意義與該 Arc 的 curve->FirstParameter()/
 * LastParameter() 一致（皆為逆時針遞增），這是 GC_MakeArcOfCircle／
 * Geom_TrimmedCurve 的標準慣例。
 */
bool angleInSweep(double angleRad, double startRad, double endRad);

} // namespace geom2d
} // namespace cad
} // namespace aicad
