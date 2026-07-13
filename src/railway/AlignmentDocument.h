#pragma once

#include "RailwayAlignment.h"

#include <QObject>
#include <QPointF>
#include <QJsonObject>
#include <QVector>

namespace aicad {
namespace cad { class Document; }

namespace railway {

// ============================================================================
//  ConstraintMode
// ============================================================================

enum class ConstraintMode {
    Fixed,    ///< 固定座標，不受 solver 移動
    Floating, ///< 依附鄰近元素自動計算 PC/PT
    Free      ///< 由前後鄰居幾何推導（solver pass 3）
};

// ============================================================================
//  ElementType  (用於 EditableElement::type)
// ============================================================================

enum class EditableElementType {
    Tangent,
    CircularArc,
    SpiralIn,
    SpiralOut
};

// ============================================================================
//  SpiralType  — 螺旋線過渡曲線類型
//
//  對應 RailwayAlignmentElement.h 中的 ElementType 過渡曲線子集：
//    Clothoid  → ClothoidElement  (Euler / Cornu — 曲率線性遞增，預設)
//    HalfSine  → HalfSineElement  (半正弦，曲率依升餘弦分佈)
//    Parabola  → ParabolaElement  (三次拋物線，路軌常用近似)
//    CubicJPN  → CubicJPNElement  (日本 JIS E 1301 三次拋物線)
//    CubicECI  → CubicECIElement  (CECI 三次拋物線)
// ============================================================================

enum class SpiralType {
    Clothoid = 0,  ///< 預設：Euler–Cornu clothoid（線性曲率）
    HalfSine,      ///< Half-sine（半正弦）
    Parabola,      ///< Cubic parabola（三次拋物線）
    CubicJPN,      ///< Japanese cubic parabola（JIS E 1301）
    CubicECI       ///< CECI cubic parabola
};

// ============================================================================
//  EditableElement
// ============================================================================

struct EditableElement
{
    EditableElementType type   = EditableElementType::Tangent;
    ConstraintMode      mode   = ConstraintMode::Fixed;
    double              radius = 0.0;   ///< 圓弧半徑 [m]，切線段為 0
    double              length = 0.0;   ///< 元素長度 [m]
    QPointF             startPI;        ///< 切線起點；Fixed CircularArc = 弧起點 (PC)
    QPointF             endPI;          ///< 切線終點；Fixed CircularArc = 弧終點 (PT)
    QPointF             arcCenter;      ///< 圓心（Fixed CircularArc 專用；其他元素忽略）
    bool                solved = false; ///< solver 是否已完成求解

    // Floating / SCS：依附的前後切線在 m_elems 中的 index（-1 = 未設定）
    int tangentIdxBefore = -1;
    int tangentIdxAfter  = -1;

    // SCS 螺旋線類型（僅 SpiralIn / SpiralOut 使用；其餘元素忽略）
    // spiralType1 = 入螺旋類型（SpiralIn 元素使用）
    // spiralType2 = 出螺旋類型（SpiralOut 元素使用；SpiralIn 元素也同時攜帶以便查詢）
    SpiralType spiralType1 = SpiralType::Clothoid;  ///< 入螺旋類型，預設 Clothoid
    SpiralType spiralType2 = SpiralType::Clothoid;  ///< 出螺旋類型，預設 Clothoid

    // ── 建構線標記（僅供 seedFromRawPoints() 反推匯入線形使用；手動編輯 ──
    //    （AFC/SCS 等指令）建立的元素一律維持預設 false，不受影響）
    //
    //   isConstructionLine — 此 Fixed Tangent 是由線形起訖點（或 SS 交會點）
    //     的量測座標＋方位角虛擬延伸出來，用來錨定邊界緩和曲線的「建構
    //     線」，而非真正連續量測到的切線段。
    //   constructionIsArc  — 僅線形起訖點的建構線適用：建構線另一側（不在
    //     本檔案資料範圍內）的型別。false＝切線（T，曲率 0，SS 交會點恆為
    //     此值）；true＝圓弧（C，曲率非 0）。此圓弧本身的半徑（若原始資料
    //     碰巧有記錄）會存在對應的 Fixed SpiralIn/SpiralOut 元素自身的
    //     radius 欄位（Tangent 型別的 radius 恆為 0，無法承載），而非存在
    //     這個建構線 Tangent 元素上。
    bool isConstructionLine = false;
    bool constructionIsArc  = false;
};

// Forward declaration (AlignmentDocument is defined later in this file)
class AlignmentDocument;


// ============================================================================
//  HorizontalAlignmentEdit
// ============================================================================

class HorizontalAlignmentEdit : public QObject
{
    Q_OBJECT

public:
    explicit HorizontalAlignmentEdit(QObject* parent = nullptr);
    ~HorizontalAlignmentEdit() override = default;

    // ── 元素新增 ─────────────────────────────────────────────────────────────

    /** 新增 Fixed Tangent，回傳元素 index */
    int addFixedTangent(QPointF from, QPointF to);

    /** 新增 Fixed CircularArc（由三點外接圓確定），回傳元素 index。
     *  arcStart / arcEnd = 使用者指定的弧起終點（PC / PT）。
     *  arcCenter = 外接圓圓心（由命令層計算後傳入）。 */
    int addFixedCurve(QPointF arcStart, QPointF arcEnd,
                      QPointF arcCenter, double radius);

    /**
     * @brief 新增一個「Fixed 獨立緩和曲線」元素：座標／長度／半徑／類型皆
     *        直接取自原始資料，不依附任何 Tangent（tangentIdxBefore/After
     *        維持 -1），也不會被 solver 的 Floating 群組偵測（SCS/LC/CA/
     *        ACA 皆僅處理 mode==Floating 的緩和曲線）誤判為那些群組的一員。
     *
     *        用於 seedFromRawPoints() 反推線形起訖點的邊界緩和曲線群組中，
     *        該緩和曲線另一側銜接的是虛擬（不在檔案資料範圍內）圓弧、且該
     *        圓弧半徑恰好可從原始資料取得的情況：此時無法比照一般 SCS 建立
     *        Floating 群組（缺少可供 solver 依附的第二個 Tangent／圓弧元
     *        素），故直接以量測到的兩端座標建立為 Fixed 元素，確保 3D 顯示
     *        與資料表仍能正確呈現，但不會隨鄰近錨點連動（因為另一側本來
     *        就沒有可連動的對象）。
     *
     * @param dir         SpiralIn 或 SpiralOut（僅供方向標示，不影響求解）。
     * @param start       緩和曲線起點（量測座標）。
     * @param end         緩和曲線終點（量測座標）。
     * @param length      緩和曲線長度 [m]。
     * @param spiralType  緩和曲線類型。
     * @param radius      虛擬圓弧那一端的半徑。
     * @return 新元素的 index。
     */
    int addFixedSpiral(EditableElementType dir, QPointF start, QPointF end,
                       double length, SpiralType spiralType, double radius);

    /** 新增 Floating 圓弧（依附前後切線），回傳元素 index */
    int addFloatingCurve(int tangentIdxBefore, int tangentIdxAfter, double radius);

    /** 新增 SCS 組合（入螺旋＋圓弧＋出螺旋），建立 3 個 EditableElement，回傳入螺旋的 index */
    int addSCS(int tangentIdxBefore, int tangentIdxAfter, double radius, double spiralLength);

    /** 非對稱版本：L1（入螺旋）、L2（出螺旋）可獨立設定。
     *  L1=L2=0 退化為 AFC（單圓弧）；其中一方為 0 則省略對應螺旋段。
     *  回傳第一個新建元素的 index；失敗時回傳 -1。 */
    int addSCS(int tangentIdxBefore, int tangentIdxAfter,
               double radius, double spiralLength1, double spiralLength2);

    /** 完整版本：L1、L2 可獨立設定，並分別指定入螺旋（type1）與出螺旋（type2）的
     *  過渡曲線類型。未指定時預設為 SpiralType::Clothoid。
     *  回傳第一個新建元素的 index；失敗時回傳 -1。 */
    int addSCS(int tangentIdxBefore, int tangentIdxAfter,
               double radius, double spiralLength1, double spiralLength2,
               SpiralType type1, SpiralType type2);

    /**
     * @brief 在 Fixed Tangent 與 Fixed CircularArc 之間插入長度未知的 Clothoid
     *        （LC 群組 = 直線 → Clothoid → 弧）。
     *
     * Solver 以二分法求解 Clothoid 長度 Ls，使螺旋線的入端切線吻合直線、
     * 出端 SC 點落在弧上且切線相切。弧的 PC（startPI）會被更新為 SC 點。
     *
     * 建立 1 個 SpiralIn EditableElement（mode = Floating）。
     *
     * @param tangentIdx  Fixed Tangent 的 index（直線，位於螺旋之前）。
     * @param arcIdx      Fixed CircularArc 的 index（弧，位於螺旋之後）。
     * @param spiralType  Clothoid 類型，預設 Clothoid。
     * @return SpiralIn 元素的 index；失敗時回傳 -1。
     */
    int addLC(int tangentIdx, int arcIdx,
              SpiralType spiralType = SpiralType::Clothoid);

    /**
     * @brief 在 Fixed CircularArc 與 Fixed Tangent 之間插入長度未知的 Clothoid
     *        （CA 群組 = 弧 → Clothoid → 直線）。
     *
     * Solver 求解 Ls 使螺旋線的入端 CS 點落在弧上且切線相切、出端切線吻合直線。
     * 弧的 PT（endPI）會被更新為 CS 點。
     *
     * 建立 1 個 SpiralOut EditableElement（mode = Floating）。
     *
     * @param arcIdx      Fixed CircularArc 的 index（弧，位於螺旋之前）。
     * @param tangentIdx  Fixed Tangent 的 index（直線，位於螺旋之後）。
     * @param spiralType  Clothoid 類型，預設 Clothoid。
     * @return SpiralOut 元素的 index；失敗時回傳 -1。
     */
    int addCA(int arcIdx, int tangentIdx,
              SpiralType spiralType = SpiralType::Clothoid);

    /**
     * @brief 在兩個 Fixed CircularArc 之間插入長度未知的 Clothoid
     *        （ACA 群組 = 弧₁ → Clothoid → 弧₂）。
     *
     * 兩段弧均為 Fixed 模式，Clothoid 長度由求解器自動計算。
     * 弧₁ 末端（PT₁）與弧₂ 起始（PC₂）均將被修剪至螺旋切點。
     *
     * 插入 1 個 SpiralIn EditableElement（mode = Floating）於 arc1Idx 之後
     * （即 arc2Idx 之前）。
     *
     * @param arc1Idx     Fixed CircularArc₁ 的 index（位於螺旋之前）。
     * @param arc2Idx     Fixed CircularArc₂ 的 index（位於螺旋之後）。
     * @param spiralType  Clothoid 類型，預設 Clothoid。
     * @return SpiralIn 元素的 index；失敗時回傳 -1。
     */
    int addACA(int arc1Idx, int arc2Idx,
               SpiralType spiralType = SpiralType::Clothoid);

    void movePI(int idx, QPointF newPos);

    /**
     * @brief 移動 Fixed Tangent 的 startPI（from 端）。
     *
     * 對非 Tangent 元素，等同 movePI（移動 startPI）。
     * 連續拖曳時 mergeId=2，與 movePI(mergeId=1) 互不合併。
     */
    void moveStartPI(int idx, QPointF newPos);

    /**
     * @brief Grip drag 用：直接修改 PI 座標，不 push Undo。
     *
     * Undo 由 AlignmentGripProvider::onGripDragEnd() 統一推入一筆記錄。
     */
    void movePIDirect(int idx, QPointF newPos);

    /**
     * @brief Grip drag 用：直接修改 startPI，不 push Undo。
     */
    void moveStartPIDirect(int idx, QPointF newPos);
    void setRadius(int idx, double radius);
    void setConstraintMode(int idx, ConstraintMode mode);
    void removeElement(int idx);

    /** 直接設定元素長度（主要供 SpiralIn/SpiralOut 緩和曲線長度編輯使用）。 */
    void setLength(int idx, double length);

    /** 設定緩和曲線類型（SpiralIn → spiralType1；SpiralOut → spiralType2）。
     *  @param isExit  true = 設定 SCS 出口類型（SpiralIn.spiralType2）；
     *                 false（預設）= 設定入口類型（SpiralIn.spiralType1）。 */
    void setSpiralType(int idx, SpiralType type, bool isExit = false);

    // ── 求解 ─────────────────────────────────────────────────────────────────

    /** 執行求解；結果存入 m_result，並 emit changed() */
    void solve();

    // ── 查詢 ─────────────────────────────────────────────────────────────────

    const HorizontalAlignment* result() const;

    const QVector<EditableElement>& elements() const { return m_elems; }

    /**
     * @brief 若目前尚無元素資料，嘗試從稠密的 TS/SC/CS/CC/TC/ST 關鍵點序列
     *        （例如 ALD 匯入、尚未經過任何編輯器的情況）反推出可互動編輯的
     *        元素鏈（Tangent/CircularArc/SpiralIn/SpiralOut），供資料表對話框
     *        與 grip 編輯使用。這是「由稠密關鍵點反推可編輯元素鏈」的唯一
     *        實作，與 VerticalAlignmentEdit::seedFromDensePoints() 對應。
     *
     * 反推規則：
     *   1. 兩個真正 Tangent（或 SS 虛擬零長度切線）之間夾有 C／SCS／SC／CS
     *      等曲線群組（一段或多段皆可）時，兩側 Tangent 的座標一律取為
     *      「兩切線（各自依記錄方位角延伸為無限直線）之交點」（IP），而非
     *      原始資料中量測到的 TS/ST 座標。曲線群組本身一律建為 Floating
     *      （依附前後 Fixed Tangent，由 solver 反算 PC/PT），半徑與螺旋
     *      長度取自原始關鍵點資料。
     *   2. 線形起點或終點若不是以真正 Tangent 開始/結束（即該端的 C／S
     *      群組只有一側有 Tangent，常見於線形本身即以曲線起訖），將該端點
     *      本身（座標＋方位角，皆為量測所得）視為一條固定不動的「虛擬
     *      Tangent 直線」（建構線，isConstructionLine=true），與相鄰的真正
     *      Tangent 依規則 1 計算 IP 作為該群組另一側的角點；solver 在此處
     *      只會沿這條虛擬直線的方向調整面向曲線群組那一端的角點，虛擬直線
     *      本身（起點/終點座標與方位角）永遠固定。若該端最外側元素本身是
     *      緩和曲線（S）：
     *        - 群組內若仍有圓弧（C）成員 → 半徑正常取自該圓弧，不受影響。
     *        - 群組內完全沒有圓弧成員（裸露 S）→ 讀取線形起訖點自身 tsc
     *          的另一側字元，記錄「建構線另一側是 T 還是 C」
     *          （constructionIsArc）。若為 C 且原始資料剛好記錄了半徑，
     *          直接以量測到的兩端座標建立為 Fixed SpiralIn/SpiralOut
     *          （見 addFixedSpiral()）；若半徑不可考（原始資料未記錄，
     *          此為目前最常見的情況），記錄警告並略過該群組。
     *   3. "SS"（兩段緩和曲線直接相接的虛擬零長度切線點，複合反向曲線
     *      常見）等同規則 1 的一個 Tangent 錨點（同樣標記為建構線，但
     *      constructionIsArc 恆為 false），兩側同樣依 IP 計算角點。
     *   4. 複合曲線（CC：弧-弧直接相接，可能夾帶 Egg 型緩和曲線）目前的
     *      可編輯元素型別尚無對應（EditableElementType 沒有 Egg），退化
     *      為個別的 Fixed CircularArc（座標/半徑取自原始資料，不會隨鄰近
     *      錨點連動），中間的 Egg 緩和曲線會被略過並記錄警告。
     *
     * @param rawPts 稠密關鍵點序列（例如 tcl->horizontal()->rawPoints()）。
     * @return 已有元素資料（不覆蓋）或 rawPts 不足兩點時回傳 false；
     *         成功建立新元素鏈時回傳 true。
     */
    bool seedFromRawPoints(const QVector<AlignmentPoint>& rawPts);

    // ── 起始里程（僅影響顯示/輸出的里程偏移，不影響幾何解算）───────────────────
    //
    //  solve() 產生的 AlignmentPoint 序列一律以 chainage = 0 為起點；下列兩個
    //  偏移量讓使用者可在資料表第一列設定實際的「起始里程」與「起始連續里程」，
    //  solve() 完成後會將其加總套用到每一點的 chainage / contChainage。
    //  contChainage 定義為 chainage + (m_startContChainage - m_startChainage)，
    //  亦即「連續里程」與「里程」之間維持一個固定常數差（對應現場常見的里程續
    //  接慣例；本應用未支援里程中途重置）。

    double startChainage()           const { return m_startChainage; }
    double startContinuousChainage() const { return m_startContChainage; }

    void setStartChainage(double chainage)            { m_startChainage     = chainage; }
    void setStartContinuousChainage(double contChain) { m_startContChainage = contChain; }

    // ── 序列化 ───────────────────────────────────────────────────────────────

    QJsonObject toJson()                        const;
    bool        fromJson(const QJsonObject& obj);
    AlignmentDocument* parentDocument() const { return m_parentDoc; }

Q_SIGNALS:
    void changed();

private:
    /**
     * @brief Insert a contiguous group of new elements at @p pos, keeping
     *        m_elems in alignment (chainage) order, and fix up every other
     *        element's tangentIdxBefore/After references that pointed at
     *        or past @p pos.
     *
     * AlignmentSolver walks m_elems in storage-index order and assumes that
     * order matches physical alignment order. Without this fix-up, simply
     * appending new elements (e.g. a curve dropped between two existing
     * tangents) would desynchronise storage order from alignment order and
     * scramble the emitted TS/SC/CS/ST/TT point sequence (e.g. producing
     * "TTC" instead of the correct "TCT" for a bare float curve).
     *
     * @return The index at which the first inserted element now lives.
     */
    int insertElementsOrdered(int pos, const QVector<EditableElement>& newElems);

    /**
     * @brief Remove any existing element(s) already occupying the gap
     *        strictly between tangentIdxBefore and tangentIdxAfter (i.e. a
     *        prior AFC arc or SCS spiral/arc/spiral group), so a new
     *        AFC/SCS call replaces rather than stacks on top of it.
     *
     * Matching is purely positional: everything between the two Tangent
     * indices is treated as the old floating group, since insertElementsOrdered()
     * always places a new AFC/SCS group immediately after tangentIdxBefore.
     * As a safety check, removal only proceeds if every element in that gap
     * is Floating; if a Fixed element is found there (e.g. from an LC/CA/ACA
     * group), the gap is left untouched and 0 is returned.
     *
     * After removal, every remaining element's tangentIdxBefore/After that
     * pointed at or past the removed range is shifted down, mirroring the
     * fix-up insertElementsOrdered() performs on insert.
     *
     * @return Number of elements removed (0 if nothing occupied the gap,
     *         or if a non-Floating element was found there).
     */
    int removeFloatingBetween(int tangentIdxBefore, int tangentIdxAfter);

    QVector<EditableElement>             m_elems;
    std::unique_ptr<HorizontalAlignment> m_result;
    double                                m_startChainage     = 0.0;
    double                                m_startContChainage = 0.0;
    
    // Step 17: back-pointer to AlignmentDocument for Undo push
    friend class AlignmentDocument;
    AlignmentDocument* m_parentDoc = nullptr;
};

// ============================================================================
//  VerticalAlignmentEdit
// ============================================================================

class VerticalAlignmentEdit : public QObject
{
    Q_OBJECT

public:
    explicit VerticalAlignmentEdit(QObject* parent = nullptr);
    ~VerticalAlignmentEdit() override = default;

    int  addVip(double chainage, double elevation, double lvc);
    void moveVip(int idx, double newChainage, double newElevation);
    void removeVip(int idx);
    void setKValue(int vipIdx, double K);

    /** 直接設定 VIP 的豎曲線長度 Lvc（端點 VIP 無豎曲線，呼叫無效果）。 */
    void setLvc(int vipIdx, double lvc);

    /**
     * @brief 若目前尚無 VIP 資料，嘗試從稠密的 VerticalAlignmentPoint 序列
     *        （例如 ALD 匯入或舊格式 TCL::vertical()->points()）反推出 PVI 清單。
     *
     *  這是「由稠密曲線點反推 VIP」的唯一實作，供資料表對話框
     *  （AlignmentDataTableDialog）與縱斷面繪圖編輯器
     *  （VAlignEditorDockWidget）共用，避免兩處各自維護一份、可能
     *  產生不同結果的還原邏輯。
     *
     * @param rawPts 稠密點序列（solve() 輸出格式：中間 VIP 以 lvc>0 的
     *               「VC exit」記錄標示）。
     * @return 已有 VIP 資料（不覆蓋）或 rawPts 不足兩點時回傳 false；
     *         成功建立新 VIP 清單時回傳 true。
     */
    bool seedFromDensePoints(const QVector<VerticalAlignmentPoint>& rawPts);

    void solve();

    // ── 查詢（供資料表 UI 使用） ────────────────────────────────────────────
    int    vipCount()              const { return m_vips.size(); }
    double vipChainage(int idx)    const;
    double vipElevation(int idx)   const;
    double vipLvc(int idx)         const;

    const VerticalAlignment* result() const;

    QJsonObject toJson()                        const;
    bool        fromJson(const QJsonObject& obj);

Q_SIGNALS:
    void changed();

private:
    struct VipRecord {
        double chainage  = 0.0;
        double elevation = 0.0;
        double lvc       = 0.0;
    };

    QVector<VipRecord>                m_vips;
    std::unique_ptr<VerticalAlignment> m_result;
};

// ============================================================================
//  AlignmentDocument
// ============================================================================

class AlignmentDocument : public QObject
{
    Q_OBJECT

public:
    explicit AlignmentDocument(QObject* parent = nullptr);
    ~AlignmentDocument() override = default;

    HorizontalAlignmentEdit* horizontal() const { return m_horizontal.get(); }
    VerticalAlignmentEdit*   vertical()   const { return m_vertical.get();   }

    /** Track which TCL this edit session belongs to (set by UIManager). */
    void    setActiveTclId(const QString& id) { m_activeTclId = id; }
    QString activeTclId()             const { return m_activeTclId; }

    /** 將求解結果同步寫入 OCAF Document（AIS 端使用） */
    void syncToOCAF(aicad::cad::Document* doc);

    QJsonObject toJson()              const;
    bool        fromJson(const QJsonObject& obj);

    // Phase 4: 此文件建立時所依據的 TM2 Project Origin（供多檔疊圖衝突偵測）
    bool    hasDocOrigin() const;
    double  docOriginE()   const;
    double  docOriginN()   const;
    QString docEpsgCode()  const;

private:
    std::unique_ptr<HorizontalAlignmentEdit> m_horizontal;
    std::unique_ptr<VerticalAlignmentEdit>   m_vertical;

    // Phase 4: 文件內嵌 origin（開檔衝突偵測用）
    bool    m_hasDocOrigin = false;
    double  m_docOriginE   = 0.0;
    double  m_docOriginN   = 0.0;
    QString m_docEpsgCode  = QStringLiteral("EPSG:3826");
    QString                                  m_activeTclId;
};

} // namespace railway
} // namespace aicad