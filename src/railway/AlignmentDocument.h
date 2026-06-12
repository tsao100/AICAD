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
<<<<<<< HEAD
=======
<<<<<<< HEAD

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
=======
>>>>>>> 6bc721d (H Alignment grips dbg1.)
>>>>>>> 56324d0 (H Alignment grips dbg1.)
    void setRadius(int idx, double radius);
    void setConstraintMode(int idx, ConstraintMode mode);
    void removeElement(int idx);

    // ── 求解 ─────────────────────────────────────────────────────────────────

    /** 執行求解；結果存入 m_result，並 emit changed() */
    void solve();

    // ── 查詢 ─────────────────────────────────────────────────────────────────

    const HorizontalAlignment* result() const;

    const QVector<EditableElement>& elements() const { return m_elems; }

    // ── 序列化 ───────────────────────────────────────────────────────────────

    QJsonObject toJson()                        const;
    bool        fromJson(const QJsonObject& obj);
    AlignmentDocument* parentDocument() const { return m_parentDoc; }

Q_SIGNALS:
    void changed();

private:
    QVector<EditableElement>             m_elems;
    std::unique_ptr<HorizontalAlignment> m_result;
    
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
    void solve();

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

private:
    std::unique_ptr<HorizontalAlignmentEdit> m_horizontal;
    std::unique_ptr<VerticalAlignmentEdit>   m_vertical;
    QString                                  m_activeTclId;
};

} // namespace railway
} // namespace aicad