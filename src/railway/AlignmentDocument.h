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
};

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

    // ── 元素操作 ─────────────────────────────────────────────────────────────

    void movePI(int idx, QPointF newPos);
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

Q_SIGNALS:
    void changed();

private:
    QVector<EditableElement>             m_elems;
    std::unique_ptr<HorizontalAlignment> m_result;
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

    /** 將求解結果同步寫入 OCAF Document（AIS 端使用） */
    void syncToOCAF(aicad::cad::Document* doc);

    QJsonObject toJson()              const;
    bool        fromJson(const QJsonObject& obj);

private:
    std::unique_ptr<HorizontalAlignmentEdit> m_horizontal;
    std::unique_ptr<VerticalAlignmentEdit>   m_vertical;
};

} // namespace railway
} // namespace aicad