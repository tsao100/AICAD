/**
 * @file SketchGeomTransformUtil.h
 * @brief Phase 0 共用建設：MOVE/COPY/ROTATE/MIRROR/STRETCH 共用的 2D 幾何
 *        變換工具（見 AICAD_SketchEdit_Advanced_Commands_Plan.md §2.2）。
 *
 * 設計依據（實際檢視既有程式碼後確認，非臆測）：
 *  - SketchLine::start/end 與 SketchCircle::center 在 ConstraintSolver 的
 *    變數打包（packVariables）中會與其 startUuid/endUuid/centerUuid 對應的
 *    SketchPoint 共用同一組 DOF；因此搬動這兩種幾何，只需要搬動其引用的
 *    SketchPoint（Sketch::movePoint），Sketch::syncGeometryFromPoints() 會
 *    自動同步 line->start/end、circle->center。
 *  - SketchArc 與 SketchEllipse 則不然：ConstraintSolver::packVariables()
 *    對 Arc 是直接從既有的 a->curve（OCCT Geom_TrimmedCurve）取得
 *    [cx, cy, r, t0, t1] 作為初始變數，對 Ellipse 是直接從既有的
 *    center/majorRadius/minorRadius/angle 欄位取值——兩者都完全不依賴
 *    startUuid/endUuid/centerUuid 對應的 SketchPoint。這與
 *    SketchGripProvider::computeGrips() 中 Arc/Ellipse 的 grip 拖曳實作
 *    完全一致：該處對 Arc 是直接以三個變換後的點呼叫
 *    GC_MakeArcOfCircle 重建 a->curve，對 Ellipse 是直接改寫
 *    center/angle 欄位，兩者事後才用 movePoint() 同步對應 SketchPoint
 *    (供 OSnap／其他幾何的共點/約束參考使用)，而不是反過來用
 *    movePoint() 驅動 curve/center 重建。本檔案的 applyToSelection() /
 *    cloneAndTransform() 對 Arc/Ellipse 的處理完全比照這個既有、已驗證
 *    可行的模式。
 *  - 所有變換完成後統一呼叫一次 Sketch::solveConstraints()，行為與既有
 *    SketchGripProvider::onGripDragEnd() 一致：讓選取範圍內的幾何變換
 *    完成後，若選取範圍與範圍外的幾何之間仍有約束關聯（例如共用端點的
 *    Coincident、Perpendicular…），約束求解器有機會依新的基準位置重新
 *    收斂；若選取範圍內外完全無約束關聯，這一步等同 no-op。
 */
#pragma once

#include <QString>
#include <QStringList>
#include <QSet>
#include <QVector2D>

namespace aicad {
namespace cad {

class Sketch;

namespace transform {

/**
 * @brief 2D 剛體／鏡射變換描述。
 *
 * MOVE / COPY 使用 translation()，ROTATE 使用 rotation()，
 * MIRROR 使用 mirror()。TRIM/EXTEND/FILLET/CHAMFER 會改變拓樸
 * （刪點/加點/圓退化為弧），不透過本類別驅動，另見
 * TrimExtendHelper（Phase 4）。
 */
class Transform2D {
public:
    enum class Kind { Translate, Rotate, Mirror, Scale };

    static Transform2D translation(const QVector2D& delta);
    static Transform2D rotation(const QVector2D& center, double angleRad);
    static Transform2D mirror(const QVector2D& axisP0, const QVector2D& axisP1);
    /// 以 center 為中心的等比縮放。factor 必須 > 0（呼叫端負責驗證；
    /// factor <= 0 在 apply() 中會被視同 1.0，不做縮放，避免產生退化幾何）。
    static Transform2D scale(const QVector2D& center, double factor);

    Kind kind() const { return m_kind; }
    bool isReflection() const { return m_kind == Kind::Mirror; }

    /// 變換一個 2D 點座標。
    QVector2D apply(const QVector2D& p) const;

    /// 變換一個「方向角」（弧度）。用於 SketchEllipse::angle 這類不是由
    /// 座標點定義、而是由純量角度欄位定義的朝向。
    ///   - Translate：角度不變。
    ///   - Rotate：angle + 旋轉角。
    ///   - Mirror：angle 依鏡射軸角度反射（2·軸角 − angle）。
    ///     ⚠️ 注意：鏡射一個非正圓橢圓在數學上會使其「手性」翻轉，單一
    ///     角度欄位無法完整表達真正的鏡像（詳見實作計畫 §3.4）。此處回傳
    ///     的角度是幾何學上長短軸方向鏡射對稱後的近似值，對正圓（majorRadius
    ///     == minorRadius）以及純旋轉/平移沒有這個限制。
    ///   - Scale：等比縮放不改變方向角。
    double applyAngle(double angleRad) const;

    /// 本次變換對「長度」的等比縮放倍率——Translate/Rotate/Mirror 皆保長度
    /// （回傳 1.0），只有 Scale 會回傳實際 factor（factor<=0 時視同 1.0）。
    /// 供 SketchCircle::radius、SketchEllipse::majorRadius/minorRadius 這類
    /// 「不是由座標點定義、而是獨立純量欄位」的半徑值使用；Line/Arc/
    /// Polyline/Spline 等純粹由端點座標定義形狀的幾何則不需要，直接靠
    /// apply() 逐點變換即可自動反映縮放後的正確形狀。
    double linearScaleFactor() const { return (m_kind == Kind::Scale && m_factor > 0.0) ? m_factor : 1.0; }

private:
    Kind      m_kind = Kind::Translate;
    QVector2D m_delta;
    QVector2D m_center;
    double    m_angleRad = 0.0;
    QVector2D m_axisP0, m_axisP1;
    double    m_axisAngle = 0.0;   ///< mirror() 時預先算好的鏡射軸方向角（弧度）
    double    m_factor = 1.0;      ///< scale() 專用
};

/**
 * @brief 收集 geomUuids（含間接）所引用的所有 SketchPoint UUID。
 *
 * 主要供 STRETCH（Phase 3，窗選範圍內的點才搬動）與需要「以點為單位」
 * 而非「以幾何為單位」操作的呼叫端使用。applyToSelection()/
 * cloneAndTransform() 本身內部各幾何型別分派處理，不透過本函式驅動。
 */
QSet<QString> collectReferencedPointUuids(cad::Sketch* sketch,
                                          const QStringList& geomUuids);

/**
 * @brief 對選取範圍內的每個幾何「原地」套用 xf，完成後統一求解約束並
 *        請求重建（Q_EMIT sketch->rebuildRequested()）。
 *
 * 幾何型別分派方式：
 *  - Point：直接 movePoint()。
 *  - Line／Circle：搬動其引用的 SketchPoint（syncGeometryFromPoints 會
 *    自動同步 start/end/center）。
 *  - Arc：以變換後的起點/中點/終點三點呼叫 GC_MakeArcOfCircle 重建
 *    curve，並同步 startUuid/endUuid/centerUuid 三個 SketchPoint
 *    （既有 SketchGripProvider 的 Arc 拖曳並未同步 centerUuid，這裡
 *    額外補上，屬於順帶修正的既有小缺口）。
 *  - Ellipse：直接改寫 center/angle 欄位，並同步 centerUuid 的
 *    SketchPoint。
 *  - Polyline／Spline（舊格式相容）／Polygon：直接改寫 points[] 陣列，
 *    並同步各自的 vertexUuids／controlPointUuids 對應點（若有）。
 *
 * 選取範圍內部共用的 SketchPoint（例如兩條相鄰線段的共同端點）只會被
 * 搬動一次，不會重複套用變換。
 *
 * @param solveAfter 完成搬動後是否呼叫 Sketch::solveConstraints()。預設
 *        true（既有行為不變）。MOVE 的即時預覽（見 MoveCommand /
 *        SketchTransformCommandBase）在滑鼠移動的每一幀都會呼叫本函式套
 *        用「當次增量位移」，若每幀都完整跑一次約束求解器，在複雜草圖
 *        上會造成明顯延遲；因此預覽階段改傳 false，只搬動幾何本身（供
 *        即時視覺回饋），等使用者按下第二點確認時才以 solveAfter=true
 *        的一般路徑（commit()）正式求解一次——與既有 Grip 拖曳
 *        （SketchGripProvider::onDrag 只搬點、onGripDragEnd 才
 *        solveConstraints()）採用相同的「拖曳中輕量、放開時才求解」分工。
 *        rebuildRequested() 無論 solveAfter 為何都照樣送出，確保拖曳中
 *        畫面仍會即時重繪。
 */
void applyToSelection(cad::Sketch* sketch,
                      const QStringList& geomUuids,
                      const Transform2D& xf,
                      bool solveAfter = true);

/**
 * @brief 複製 geomUuids 對應的幾何（連同其專屬 SketchPoint），套用 xf
 *        後插入 sketch，回傳新建立的頂層幾何 UUID 清單。
 *
 * 選取範圍內部共用的端點（例如矩形相鄰兩邊共用的角點）複製後仍然共用
 * 同一個新端點，不會在角落裂開——內部以 old→new 點 UUID 對照表確保。
 *
 * 選取範圍「內部」的約束（SketchConstraint）也會一併複製到新幾何上——
 * 只要一筆約束引用的所有幾何都在這次複製範圍內，就會複製出一份對應的
 * 新約束並改綁新幾何；只要有任一 ref 指向複製範圍外的既有幾何，整筆
 * 約束就會被跳過（不會讓複製品去綁定一個沒被複製的物件）。GDIM 標註
 * 在 m_constraints 中對應的隱含約束（SketchConstraint::implicitOf 非空）
 * 一律不複製，因為那是標註自己維護的衍生資料。
 *
 * 絕對座標／絕對角度型的尺寸約束（FixedX/FixedY/CoordinateDim/
 * FixedAngleDim）複製時會以新幾何變換後的實際位置/角度重新取值，而不是
 * 沿用舊值，否則求解器會把複製品拉回舊的絕對位置/角度，抵銷掉這次複製
 * 的位移。其餘尺寸約束（距離/半徑/長度/弧長…）在純平移下數值不變，直接
 * 沿用（目前 COPY 唯一會用到的 xf 就是 translation()；若未來有指令改用
 * cloneAndTransform() 搭配旋轉/鏡射複製，這部分需要另外檢視）。
 *
 * 新複製出來的幾何不會複製原本掛在來源幾何上的 SketchAnnotation
 * （GDIM 標註）；是否要連同標註一起複製是獨立的設計問題，留待後續視
 * 需要再擴充。
 *
 * @return 新建立的頂層幾何 UUID 清單（若複製對象本身是獨立 Point，
 *         回傳的即是新 Point 的 UUID）。找不到的來源 UUID 會被略過。
 */
QStringList cloneAndTransform(cad::Sketch* sketch,
                              const QStringList& geomUuids,
                              const Transform2D& xf);

/**
 * @brief STRETCH 專用：對 sketch 內所有幾何做「窗選矩形範圍內的點才搬動」
 *        的變換（見實作計畫 §3.5，Phase 3）。
 *
 * 與 applyToSelection()/cloneAndTransform() 不同，本函式不是對「呼叫端已
 * 選好的幾何清單」操作，而是對「整個 sketch」依每個幾何自身的定義點是否
 * 落在 [rectMin, rectMax] 矩形範圍內，逐幾何決定要不要變換、要整體搬動
 * 還是只搬動落在窗內的那一端：
 *
 *  - Line／Polyline／Spline：逐點判斷。只有落在窗內的端點/控制點會被搬動
 *    （真正的局部拉伸——這是 STRETCH 與 MOVE 最主要的行為差異）。
 *  - Point（獨立點）／Circle／Ellipse／Arc：這幾種幾何沒有獨立於形狀本身
 *    的「局部端點」可以拉伸（Circle/Ellipse 只有圓心一個參考點；Arc 需要
 *    三點共同定義 curve，只搬動 start 或 end 而不搬 center 會讓圓弧形狀
 *    不合理地扭曲）。因此只有在「定義該幾何的所有點都落在窗內」時才整體
 *    搬動（等同 MOVE），否則完全不受影響。此為 MVP 範圍限制，與多數
 *    CAD 系統對這幾種幾何的 STRETCH 行為相近。
 *
 * @param rectMin 矩形範圍左下角（sketch 平面座標，x/y 個別取 min）。
 * @param rectMax 矩形範圍右上角（x/y 個別取 max）。呼叫端負責先對使用者
 *                點選的兩個角點做 min/max 正規化，本函式不判斷窗選/穿越
 *                窗選方向（見 StretchCommand 的呼叫端說明）。
 * @param delta   位移向量。
 * @param solveAfter 是否在搬動後跑一次完整的 solveConstraints()。
 *                StretchCommand 的即時預覽（滑鼠移動中）每一幀都要呼叫一
 *                次本函式，傳 false 只做輕量座標搬動、不重新解約束——比
 *                照 SketchTransformCommandBase 的 MOVE/COPY 即時預覽分工
 *                （見該檔案說明）。因為「哪些點落在窗內」是根據呼叫當下
 *                的座標判斷，預覽端必須在算下一幀前先用相反的 delta 呼叫
 *                一次本函式復原，才能保證每一幀都是從原始（未搬動）座標
 *                重新判斷落在窗內的點集合，不會因為上一幀已經搬出窗外而
 *                在下一幀被誤判為不受影響。正式送出（確認第二點）時才傳
 *                true（預設值），確保只跑一次完整求解。
 * @return 實際受影響（全部或部分搬動）的頂層幾何 UUID 清單。
 */
QStringList stretchWithinRect(cad::Sketch* sketch,
                              const QVector2D& rectMin, const QVector2D& rectMax,
                              const QVector2D& delta, bool solveAfter = true);

} // namespace transform
} // namespace cad
} // namespace aicad
