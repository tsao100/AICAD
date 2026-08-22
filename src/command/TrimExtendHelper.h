/**
 * @file TrimExtendHelper.h
 * @brief Phase 4 共用建設：TRIM/EXTEND 共用的裁切/延伸邏輯（見
 *        AICAD_SketchEdit_Advanced_Commands_Plan.md §3.6/§3.7/§4）。
 *
 * 範圍限制（v2，測試回饋後更新）：
 *  - 只支援 Line／Circle／Arc 三種幾何作為 TRIM/EXTEND 的「目標」（要被
 *    裁切/延伸的對象）。Polyline／Ellipse／Point 不支援作為目標；Spline
 *    也不支援作為目標——裁切/延伸 Spline 本身需要能表達「部分 Spline」，
 *    目前 Sketch 的資料模型沒有這個能力。
 *  - 剪切邊／邊界邊（cuttingUuids/boundaryUuids）除了 Line／Circle／Arc
 *    之外，額外支援：
 *      • Spline：沒有解析交點公式（NURBS 曲線一般需要數值疊代求解），
 *        改用其取樣點（SketchGeometry::points）折線近似，逐段求交——是
 *        「近似」而非精確的曲線交點，但足以應付大多數實務情境。
 *      • Ellipse：只在「目標是 Line」時支援（透過座標變換到橢圓的單位圓
 *        局部座標系求解，見 SketchGeom2DMath::lineEllipseIntersect()）。
 *        橢圓對 Circle/Arc 目標、或橢圓對橢圓，需要解四次方程式，複雜度
 *        明顯更高，本版不支援。
 *    Ellipse／Spline 若不符合上述可支援的組合，會被安全地忽略（不會找到
 *        交點，不會當機），不影響其餘剪切邊的正常運作。
 *  - 若要讓 Ellipse 本身可以被 TRIM 成「橢圓弧」，需要先在 Sketch 的
 *    幾何資料模型新增對應的「已裁切橢圓」型別（目前 SketchGeometryType
 *    沒有這個型別），屬於比 TrimExtendHelper 更底層的架構變更，不在本檔
 *    案範圍內。
 *  - TRIM 對 Circle 一律轉換成 Arc（圓沒有起訖點，裁掉一段後只能是弧）。
 *  - EXTEND 對 Circle 不支援（圓沒有「端點」可以延伸，語意上不成立）。
 *  - 被裁切/延伸影響的幾何，其原有的 SketchConstraint／SketchAnnotation
 *    會隨著 Sketch::removeGeometry() 一併清除（Phase 0 已確認該行為存在，
 *    見實作計畫 §5）。
 *
 * 幾何運算依據 SketchGeom2DMath 的純數學交點公式；Arc 的弧段範圍篩選則
 * 使用 OCCT 的 ElCLib::Parameter()／ElCLib::Value()，確保角度計算與該 Arc
 * 自己的 curve->FirstParameter()/LastParameter() 使用同一個參考座標系
 * （避免自行用 atan2 可能與 OCCT 內部的 gp_Ax2 X 方向假設不一致的風險，
 * 這是本檔案風險最高的部分，比照 OSnapDetector.cpp 既有、已驗證可行的
 * ElCLib 用法）。
 */
#pragma once

#include <QString>
#include <QStringList>
#include <QVector2D>

namespace aicad {
namespace cad { class Sketch; }

namespace command {
namespace trimext {

/**
 * @brief 對 targetUuid 執行一次 TRIM。
 *
 * @param cuttingUuids 剪切邊 UUID 清單（可包含 targetUuid 自己，會被忽略；
 *                     也可包含不支援的型別，會被忽略而不報錯）。
 * @param clickPt      使用者點擊的位置（sketch 平面座標），決定要刪除
 *                     哪一段。
 * @return true 表示有實際刪除/重建幾何；false 表示找不到可裁切的交點、
 *              或目標型別不支援 TRIM（呼叫端應印出對應提示訊息）。
 */
bool trimAt(cad::Sketch* sketch, const QString& targetUuid,
           const QStringList& cuttingUuids, const QVector2D& clickPt);

/**
 * @brief 對 targetUuid 執行一次 EXTEND。
 *
 * @param boundaryUuids 邊界邊 UUID 清單（可包含 targetUuid 自己，會被忽略）。
 * @param clickPt       使用者點擊的位置，決定要延伸哪一端（離 clickPt
 *                      較近的端點）。
 * @return true 表示有實際延伸；false 表示找不到邊界方向上的交點、或目標
 *              型別不支援 EXTEND（例如 Circle）。
 */
bool extendAt(cad::Sketch* sketch, const QString& targetUuid,
             const QStringList& boundaryUuids, const QVector2D& clickPt);

/**
 * @brief 一段可以直接拿去畫成橡皮筋多段線預覽的取樣座標（sketch 平面）。
 *
 * Arc 型的結果會取樣成多點折線近似（純視覺預覽用途，不是精確的弧形頂點
 * 資料，不影響 trimAt()/extendAt() 實際執行時的結果——那兩個函式内部走
 * 的是 OCCT 的 GC_MakeArcOfCircle／addArcGeom()，仍然是精確弧）。
 */
struct PreviewSegment {
    bool               valid = false;
    QVector<QVector2D> points;   ///< 至少 2 點；points.isEmpty() 等同 valid=false。
};

/**
 * @brief TRIM 的滑鼠 hover 即時預覽：計算「如果在 hoverPt 點下去，會被
 *        刪除的那一段」，純運算、不修改 sketch（供 TrimCommand 在
 *        GEOM_HOVER 時呼叫，畫成橡皮筋多段線疊層）。
 *
 * 與 trimAt() 共用同一套「找出 hoverPt 所在區間」的核心運算（見 .cpp 內
 * computeLineTrimRange()/computeCircleTrimRange()/computeArcTrimRange()），
 * 確保預覽結果與實際點擊後的行為完全一致，不會有預覽跟實際結果對不上的
 * 情形。
 *
 * @return valid=false 表示這個 targetUuid/hoverPt 組合點下去不會有效果
 *         （沒有交點、或目標型別不支援 TRIM），呼叫端應隱藏預覽。
 */
PreviewSegment previewTrimAt(cad::Sketch* sketch, const QString& targetUuid,
                             const QStringList& cuttingUuids, const QVector2D& hoverPt);

/**
 * @brief EXTEND 的滑鼠 hover 即時預覽：計算「如果在 hoverPt 點下去，會
 *        新增的延伸線段」（從目前端點到新端點），純運算、不修改 sketch。
 *
 * 與 extendAt() 共用同一套核心運算（computeLineExtend()/
 * computeArcExtend()），理由同上。
 *
 * @return valid=false 表示這個 targetUuid/hoverPt 組合點下去不會有效果。
 */
PreviewSegment previewExtendAt(cad::Sketch* sketch, const QString& targetUuid,
                               const QStringList& boundaryUuids, const QVector2D& hoverPt);

/**
 * @brief FILLET：在兩條直線之間插入一個圓角弧。
 *
 * MVP 範圍限制：只支援兩條「直線」（不支援弧參與，也就是說線-弧、
 * 弧-弧的圓角在 Phase 5 不提供——這幾種情形牽涉圓與圓/圓與直線的雙切線
 * 解，可能有 0~2 組解需要依點擊位置消歧，複雜度明顯更高，留待後續視
 * 需要再擴充，見實作計畫 §3.8）。
 *
 * @param radius     圓角半徑。0 表示退化為單純延伸相交（不插入圓弧，兩線
 *                   直接在交點相接），比照 AutoCAD 對半徑 0 的行為。
 * @param clickPt1   使用者點擊 line1Uuid 的位置，決定 line1 保留哪一側
 *                   （離 clickPt1 較遠的端點視為保留端，較近的端點會被
 *                   移動到切點）。
 * @param clickPt2   同上，對應 line2Uuid。
 * @return true 表示成功套用；false 表示兩線平行/共線、其中一個不是直線、
 *              或半徑不合理（負值）。
 */
bool filletAt(cad::Sketch* sketch, const QString& line1Uuid, const QString& line2Uuid,
             double radius, const QVector2D& clickPt1, const QVector2D& clickPt2);

/**
 * @brief CHAMFER：在兩條直線之間插入一條倒角線。
 *
 * MVP 範圍限制與 filletAt() 相同（只支援兩條直線）。
 *
 * @param dist1/dist2 各自從交點沿線退回的倒角距離（可不同，對應
 *                    AutoCAD 的不等距倒角）。兩者皆為 0 時退化為單純
 *                    延伸相交，不插入倒角線。
 */
bool chamferAt(cad::Sketch* sketch, const QString& line1Uuid, const QString& line2Uuid,
              double dist1, double dist2,
              const QVector2D& clickPt1, const QVector2D& clickPt2);

} // namespace trimext
} // namespace command
} // namespace aicad
