/**
 * @file AlignmentQuickCalc.h
 * @brief 由「部分已知資料」正算出完整平面線形關鍵點序列。
 *
 * 移植自使用者提供的 Excel VBA（CommandButton5_Click）：給定第一點的完整
 * 座標／里程／方位角，以及後續每一列的「點位代碼（TSC）＋長度＋半徑／緩和
 * 曲線類型」，依序（逐列）正算出每一點的 Easting/Northing/Chainage/
 * ContinuousChainage/Azimuth，補齊成一份完整的平面線形資料表。
 *
 * 與 VBA 版本的對應關係（Select Case left(code,2)）：
 *   "TT"/"CT"/"ST"（第 2 字元＝T）→ 切線（Tangent）          —— tx/ty
 *   "TC"/"CC"/"SC"（第 2 字元＝C）→ 圓弧（CircularArc）       —— CX/CY/CA
 *   "TS"/"SS"     （第 1 字元＝T或S，第 2 字元＝S）→ 正向緩和曲線
 *                   （切線／緩和曲線→圓弧，半徑取「下一列」的 radius 欄）
 *                                                              —— sx/sy/sa
 *   "CS"          （第 1 字元＝C，第 2 字元＝S）→
 *       若下一列為 "SC" → 複合曲線（圓→緩和→圓，Egg）         —— EX/EY/Ea
 *       否則             → 反算緩和曲線（圓→緩和→切線，反向）  —— else 分支
 *
 * 與 VBA 逐格手刻三角函數不同，本移植版直接重用
 * RailwayAlignmentElement.h 既有的元素類別（TangentElement/
 * CircularArcElement/EggTransitionElement/各種 TransitionElement 子類別）
 * 之 localFrame()/worldXY()/worldAzimuth()，確保與應用程式其餘所有正算路徑
 * （ALD 匯入、資料表顯示、3D 算圖…）使用完全相同的一份幾何公式，不會出現
 * 兩套實作各自維護、逐漸失準的風險。
 *
 * "CS"（反算）分支的推導見 .cpp 檔內 computeReversedSpiralNext() 的註解：
 * 利用 TransitionElement::localFrame() 對「反向元素」提供的 R 正負號翻轉，
 * 直接反解出遠端（切線側）座標與方位角的閉式公式，等價於 VBA else 分支的
 * OALFA/x/y 三角幾何，但可套用到全部 18 種緩和曲線族（不僅 Clothoid）。
 *
 * @author AICAD Team
 */
#pragma once

#include "RailwayAlignment.h"

#include <QVector>
#include <QString>

namespace aicad {
namespace railway {

/**
 * @brief 正算：由 @p pts[0] 的完整資料＋每列的 TSC／length／radius／
 *        curveType，依序算出 @p pts 其餘各點的 easting/northing/chainage/
 *        contChainage/azimuth，就地覆寫。
 *
 * 呼叫前的必要條件（呼叫端／對話框負責備妥，本函式只讀取，不做 UI 提示）：
 *   - pts.size() >= 2。
 *   - pts[0] 的 easting/northing/chainage/contChainage/azimuth 已填妥（起點）。
 *   - pts[i].tsc（i = 0..size-2）為合法 2 字元代碼，第 2 字元 ∈ {T,C,S}。
 *   - pts[i].length（i = 0..size-2）> 0：本點至下一點的元素長度。
 *   - 「圓弧」列（tsc[1]=='C'）：pts[i].radius 為該弧之帶號半徑（非 0）。
 *   - 「緩和曲線」列（tsc[1]=='S'）：pts[i].curveType 為曲線類型 token
 *     （與 AlignmentPoint::curveType／spiralTypeName() 慣例相同，例如
 *     "SPIRAL"（預設 Clothoid）、"BLOSS"、"HALFSINE"…）；正向（TS/SS）
 *     情形另外要求下一列（pts[i+1]）的 radius 欄已填妥目標圓弧半徑。
 *   - 最後一列（pts.last()）僅需 tsc 的第 1 字元有意義；length/radius/
 *     curveType 不使用。
 *
 * @param pts       就地讀寫的關鍵點序列。
 * @param errorOut  非 nullptr 時，失敗時填入可直接顯示給使用者的錯誤訊息
 *                  （含 1-based 列號）。
 * @return 全部列成功正算完成回傳 true；任何一列資料不合法回傳 false，
 *         此時 @p pts 可能已被部分覆寫至失敗列為止（呼叫端應視為不可用，
 *         不應嘗試部分套用）。
 */
bool computeQuickAlignmentTable(QVector<AlignmentPoint>& pts, QString* errorOut = nullptr);

/**
 * @brief 由已知起點（曲率 0：切線或緩和曲線的低曲率端）正算出「正向」緩和
 *        曲線遠端（沿曲率 0→targetRadius）的座標與方位角。對應 VBA
 *        "TS"/"SS" 分支（sx/sy/sa），亦即 AlignmentQuickTableDialog／
 *        AlignmentDrawElementDialog 共用的「由切線端出發銜接圓弧」正算。
 *
 * @param start        已知起點（僅讀取 easting/northing/azimuth/chainage）。
 * @param targetRadius 遠端（圓弧側）的帶號半徑，必須非 0。
 * @param length       緩和曲線長度，必須 > 0。
 * @param curveType    曲線類型 token（同 AlignmentPoint::curveType 慣例；
 *                      空字串或未知 token 視為 "SPIRAL"（Clothoid））。
 * @param[out] outE/outN/outAz  遠端座標與方位角。
 */
void computeForwardSpiralEndpoint(const AlignmentPoint& start, double targetRadius,
                                   double length, const QString& curveType,
                                   double& outE, double& outN, double& outAz);

/**
 * @brief 由已知的圓弧側端點（曲率 R1）反算出緩和曲線遠端（切線側，曲率 0）
 *        的座標與方位角。對應 VBA "CS"（非複合曲線）else 分支，見 .cpp 內
 *        computeReversedSpiralEndpoint() 的完整推導註解。
 *
 * @param start     已知的圓弧側端點（僅讀取 easting/northing/azimuth）。
 * @param R1        入弧（起點所在圓弧）之帶號半徑，必須非 0。
 * @param length    緩和曲線長度，必須 > 0。
 * @param curveType 曲線類型 token，同上。
 * @param[out] outE/outN/outAz  遠端（切線側）座標與方位角。
 */
void computeReversedSpiralEndpoint(const AlignmentPoint& start, double R1,
                                    double length, const QString& curveType,
                                    double& outE, double& outN, double& outAz);

/**
 * @brief 由 tsc[0]（抵達本點的線元類型）及相鄰列的 radius 欄，反推
 *        @p pts[idx] 所在曲率（0 = 切線）。
 *
 * 對應 AlignmentQuickTableDialog／AlignmentDrawElementDialog 共用的資料
 * 儲存慣例：圓弧的半徑存在「起始該圓弧」的列；緩和曲線抵達後的曲率存在
 * 抵達點自身（正向緩和曲線的目標半徑依慣例存在「下一列」，也就是抵達點
 * 自己）。@p idx <= 0 或超出範圍時，一律視為位於曲率 0（文件/序列起點的
 * 保守假設）。
 */
double curvatureAtPoint(const QVector<AlignmentPoint>& pts, int idx);

/**
 * @brief 反轉一整條關鍵點序列的方向（終點變起點、起點變終點），供
 *        ALIGNMENTDRAWCHAIN 從既有線形的「起點」端接續繪製使用：先反轉、
 *        以既有「從終點接續」的邏輯計算新線元、再反轉回來即可，不需要
 *        另外實作一套「從起點反向接續」的計算路徑。
 *
 * 僅正確處理直線／圓弧／單純緩和曲線（切線↔圓弧）三種線元（與
 * AlignmentDrawElementDialog 的範圍限制一致）；不支援複合曲線（Egg）。
 *
 * 反轉規則：
 *   - 座標不變，方位角加 π（walking 方向相反）。
 *   - 里程／連續里程重新從 0 起算（原終點變成新起點，里程 0）。
 *   - 圓弧半徑正負號反轉（方向相反，左右彎互換）。
 *   - 緩和曲線類型不變；起始列半徑恆為 0（比照既有慣例）；抵達曲率透過
 *     curvatureAtPoint() 對原始序列反推後取負號寫入對應列。
 *   - tsc 的抵達／離開字元對調並重新串接。
 *
 * @param pts 原始序列（至少 2 點；只讀）。
 * @return 反轉後的序列；@p pts.size() < 2 時原樣回傳。
 */
QVector<AlignmentPoint> reverseAlignmentPoints(const QVector<AlignmentPoint>& pts);

} // namespace railway
} // namespace aicad
