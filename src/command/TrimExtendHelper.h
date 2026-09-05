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
#include <QPair>

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
 * @brief FILLET 的執行結果與後續要建立約束所需的相關 UUID。
 *
 * 圓角本身（移動端點、插入弧、呼叫 solveConstraints()）都已經在
 * filletAt() 內部完成；回傳這些 UUID 是為了讓呼叫端（FilletCommand）能
 * 額外疊加使用者要求的 Coincident／Tangent／FixedRadius 約束——弧的起訖點
 * 刻意「不」直接重用兩條線的端點 UUID（不像 chamferAt() 那樣），而是各自
 * 建立獨立的新點，事後用明確的 Coincident 約束把它們接回去，與
 * Sketch::addLineChainGeom() 對相鄰線段接點的既有慣例一致（見該函式文件
 * 註解），讓使用者在束制清單裡看得到、能編輯/刪除這個接合關係。
 */
struct FilletResult {
    bool    success = false;   ///< true 表示已成功套用（含半徑 0 的退化情形）
    QString arcUuid;           ///< 新插入的圓角弧 UUID；半徑 0（無插入弧）時為空
    QString line1PointUuid;    ///< line1 被移動到切點/交點的那個端點
    QString line2PointUuid;    ///< line2 被移動到切點/交點的那個端點
    QString arcStartUuid;      ///< 弧起點（與 line1PointUuid 座標重合，靠 Coincident 約束銜接）；arcUuid 為空時無意義
    QString arcEndUuid;        ///< 弧終點（與 line2PointUuid 座標重合）；arcUuid 為空時無意義
    QVector2D arcMidDir;       ///< 弧圓心→弧中點的方向（草圖平面局部座標，未正規化）；供呼叫端把半徑尺寸箭頭
                                ///< 畫在弧中點方向用（設進 FixedRadius 約束的 dimLineOffsetX/Y）；arcUuid 為空時無意義
    bool    removedExistingCoincident = false;  ///< true 表示兩線交點原本就有 Coincident 約束，已在套用圓角前移除
};

/**
 * @brief FILLET：在兩條直線之間插入一個圓角弧。
 *
 * MVP 範圍限制：只支援兩條「直線」（不支援弧參與，也就是說線-弧、
 * 弧-弧的圓角在 Phase 5 不提供——這幾種情形牽涉圓與圓/圓與直線的雙切線
 * 解，可能有 0~2 組解需要依點擊位置消歧，複雜度明顯更高，留待後續視
 * 需要再擴充，見實作計畫 §3.8）。
 *
 * 若兩線交點原本就存在 Coincident 約束（例如使用者先前手動把兩個獨立的
 * 端點約束成重合，而非透過連續畫線共用同一個點物件），會在套用圓角前
 * 自動偵測並移除——圓角一定要把兩個端點拉開到個別的切點，殘留的舊約束
 * 會與新的幾何位置衝突（over-constrain）。
 *
 * @param radius     圓角半徑。0 表示退化為單純延伸相交（不插入圓弧，兩線
 *                   直接在交點相接），比照 AutoCAD 對半徑 0 的行為。
 * @param clickPt1   使用者點擊 line1Uuid 的位置，決定 line1 保留哪一側
 *                   （離 clickPt1 較遠的端點視為保留端，較近的端點會被
 *                   移動到切點）。
 * @param clickPt2   同上，對應 line2Uuid。
 * @return 見 FilletResult 說明；success=false 表示兩線平行/共線、其中一個
 *         不是直線、或半徑不合理（負值）。
 */
FilletResult filletAt(cad::Sketch* sketch, const QString& line1Uuid, const QString& line2Uuid,
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

/**
 * @brief OFFSET 的執行結果。
 */
struct OffsetResult {
    bool    success = false;   ///< true 表示已成功建立偏移曲線
    QString newCurveUuid;      ///< 新建立的偏移曲線 UUID

    // 供呼叫端疊加約束用（見 offsetAt() 文件「約束處理」說明）：
    QString sourceRefPointUuid; ///< 僅 Line：來源線的起點 UUID；其餘型別為空
    QString newRefPointUuid;    ///< 僅 Line：新線的起點 UUID；其餘型別為空
    double  newRadius = 0.0;    ///< 僅 Circle／Arc：偏移後的新半徑；Line 為 0
};

/**
 * @brief OFFSET：以指定距離、朝點擊那一側，建立來源曲線的平行/同心偏移
 *        複製本。
 *
 * MVP 範圍限制：只支援 Line／Circle／Arc 三種基本曲線（不支援 Polyline／
 * Spline／Ellipse——這幾種的偏移涉及可變曲率/自相交處理，複雜度明顯更高，
 * 留待後續視需要再擴充）。
 *
 * 偏移出來的新曲線是完全獨立的一份幾何——但呼叫端（OffsetCommand）會另外
 * 疊加約束把新舊曲線關聯起來，讓偏移距離維持參數化可編輯（見下方「約束
 * 處理」）；曲線本身不共用點，兩者之間純粹靠約束銜接，這點與 filletAt()
 * 讓弧共用點／靠約束銜接line端點的作法一致。
 *
 * 約束處理（由呼叫端根據這裡回傳的 OffsetResult 疊加，offsetAt() 本身
 * 只負責建立幾何，不加約束）：
 *   - Line → Line：Parallel（兩線平行）＋ FixedDistance（來源線起點 ↔
 *     新線起點，值＝偏移距離）。
 *     ⚠️ 已知限制：這個系統目前沒有「線對線垂直距離」這種專用約束型別，
 *     只能用「兩個對應端點的點對點距離」近似——建立當下兩點連線确實垂直
 *     於兩線（構造保證），但 Parallel＋FixedDistance 這兩條約束本身並不
 *     嚴格鎖定「連線必須垂直」，理論上之後如果使用者對其中一條線做其他
 *     編輯，兩線仍會維持平行、這兩個對應點也仍維持這個距離，但視覺上的
 *     「垂直間距」在極端案例下可能會跟這個點對點距離出現些微不一致。若
 *     需要嚴格保證，之後可以再新增一個真正的「線對線垂直距離」約束型別
 *     （需要新的 ConstraintType 列舉值＋對應的 Equation 子類別）。
 *   - Circle → Circle／Arc → Arc：Concentric（同圓心）＋ FixedRadius
 *     （新曲線的半徑＝偏移後的新半徑）。
 *
 * @param distance  偏移距離，必須 > 0（呼叫端應在呼叫前自行驗證，這裡
 *                  仍會再檢查一次防呆）。
 * @param sidePt    使用者點擊決定偏移方向的位置：
 *                  - Line：與來源線的法向量比較，決定往哪一側平移。
 *                  - Circle／Arc：與圓心的距離跟原半徑比較，落在圓外
 *                    （距離 > 半徑）就往外擴（新半徑 = 原半徑 + distance），
 *                    落在圓內就往內縮（新半徑 = 原半徑 − distance）；內縮
 *                    到半徑 ≤ 0（超過圓心）視為無效結果。
 * @return 見 OffsetResult 說明；success=false 表示來源幾何型別不支援、
 *         距離不合理、或內縮超過圓心等退化情形。
 */
OffsetResult offsetAt(cad::Sketch* sketch, const QString& curveUuid,
                      double distance, const QVector2D& sidePt);

/**
 * @brief 連續鏈（一串首尾相連的 Line／Arc）裡的其中一段偏移結果。
 */
struct OffsetChainSegment {
    QString sourceUuid;          ///< 這段對應的來源曲線（Line 或 Arc）
    QString newUuid;             ///< 這段對應的新偏移曲線
    bool    isArc = false;       ///< true＝這段是 Arc，約束處理走 Concentric＋FixedRadius；
                                  ///< false＝這段是 Line，走 Parallel（＋視情況 FixedDistance）
    QString sourceRefPointUuid;  ///< 僅 Line 且該段有未被轉角調整的端點時才有值；供 FixedDistance 用（見 offsetChainAt() 文件）
    QString newRefPointUuid;     ///< 與 sourceRefPointUuid 同時為空或同時有值
    double  newRadius = 0.0;     ///< 僅 Arc；偏移後的新半徑，供 FixedRadius 用
};

/**
 * @brief OFFSET 對「連續鏈」的執行結果。
 */
struct OffsetChainResult {
    bool success = false;
    QVector<OffsetChainSegment>     segments;  ///< 依鏈順序排列，每段一筆
    QVector<QPair<QString,QString>> joints;    ///< 新鏈內部每個轉角的兩個點 UUID（供 Coincident 約束用）
};

/**
 * @brief OFFSET：以指定距離、朝點擊那一側，對一條「連續鏈」（一串首尾
 *        相連的 Line／Arc，例如已經倒過圓角的多邊形）整批建立偏移複製
 *        本，內部轉角處理成相接（Line-Line 取無限延伸線交點；Line-Arc
 *        取線與圓的交點；Arc-Arc 取兩圓的交點；多解時取離原始素樸偏移
 *        角點最近的那一個，避免選到幾何上不合理的另一側）。
 *
 * 連續鏈的偵測：從 startUuid 這一段出發，往兩個方向各自延伸——「相連」的
 * 判定同時考慮字面上共用同一個 SketchPoint，以及透過明確 Coincident 約束
 * 連結的情形（只找一步，不做遞移傳遞）。只要某個端點恰好連到「另外一條
 * Line 或 Arc」就繼續延伸；恰好連到「鏈不支援的幾何」（Circle／Polyline／
 * Spline／Ellipse 等）、連到 2 個以上其他鏈成員（分岔/T 字路口）、或延伸
 * 到端點沒有其他鏈成員相連，都視為鏈的終點，停止往那個方向延伸。若延伸
 * 過程繞回起點，判定為封閉環（例如矩形四邊、圓角矩形），額外處理「最後
 * 一段↔第一段」這個 wrap-around 轉角。
 *
 * 單一、沒有相連鄰居的曲線，鏈長度自然就是 1，計算結果等同單曲線偏移
 * （跟 offsetAt() 用同一套素樸偏移＋參考點邏輯，只是包在鏈的資料結構
 * 裡）。
 *
 * Arc 段的偏移方向：鏈的統一偏移方向以「使用者點擊、對應到的那一段」判斷
 * 出一個正負號（沿用 Line 情形的左右手判斷），其餘每一段（不論 Line 或
 * Arc）都套用同一個正負號，確保整條鏈偏移到一致的同一側。對 Arc 而言，
 * 「這個正負號該對應到半徑變大還是變小」還跟這段 Arc 沿鏈行進方向是順時
 * 針還是逆時針掃過有關（用 from/to 相對圓心的外積正負號判斷），內部已經
 * 處理好，呼叫端不需要關心。
 *
 * 偏移出來的每一段新曲線都是完全獨立的幾何（不與來源共用點、彼此之間也
 * 不共用點），事後由呼叫端依 OffsetChainResult 疊加約束把新舊幾何、以及
 * 鏈內各段彼此關聯起來：
 *   - 每個 Line 段：Parallel（來源 ↔ 對應的新線）。
 *   - 每個 Arc 段：Concentric（來源 ↔ 對應的新弧，圓心在偏移過程中本來
 *     就不會變）＋ FixedRadius（新弧的半徑＝newRadius）——這兩個是 Arc
 *     整體的性質，不受轉角調整影響，每一個 Arc 段都會提供，沒有 Line
 *     情形那種「只有沒被調整過的端點才提供參考」的限制。
 *   - 鏈內部每個轉角：Coincident（前一段新曲線的端點 ↔ 後一段新曲線的
 *     端點，見 joints）。
 *   - FixedDistance（僅 Line 段）：只在「這一段有未被轉角調整過的端點」
 *     時才提供參考點（見 sourceRefPointUuid／newRefPointUuid 說明）——
 *     開放鏈的頭尾兩段各自的外側端點（如果那一端剛好是 Line）沒有被轉角
 *     調整過，可以放心當作 FixedDistance 的參考點；鏈中間的 Line 段（兩端
 *     都被轉角調整過）、以及封閉環裡的 Line 段，沒有提供參考點，這些段
 *     只靠 Parallel＋Coincident 維持形狀，實際的垂直間距不是每一段都獨立
 *     鎖定的——這跟 offsetAt() 單線情形一樣，屬於已知限制（見該函式
 *     文件），若要嚴格保證每段間距都精確鎖定，需要新增專用的「線對線
 *     垂直距離」約束型別。
 *
 * @param distance  偏移距離，必須 > 0。
 * @param sidePt    使用者點擊決定偏移方向的位置（只需相對 startUuid 這
 *                  一段判斷一次）。
 * @return 見 OffsetChainResult 說明；success=false 表示 startUuid 不是
 *         Line 或 Arc、距離不合理、或鏈上有退化線段/弧（長度或半徑為 0）
 *         等情形。
 */
OffsetChainResult offsetChainAt(cad::Sketch* sketch, const QString& startUuid,
                                double distance, const QVector2D& sidePt);

} // namespace trimext
} // namespace command
} // namespace aicad
