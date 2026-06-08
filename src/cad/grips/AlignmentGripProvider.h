// src/cad/grips/AlignmentGripProvider.h
#pragma once

#include "GripProvider.h"
#include "GripPoint.h"

#include <QVector>
#include <QString>
#include <QPointF>

namespace aicad {
namespace railway { class HorizontalAlignmentEdit; }

namespace cad {

/**
 * @brief IGripProvider 實作：為 HorizontalAlignmentEdit 提供 PI 端點與中間點 Grip。
 *
 * Grip 配置規則（依任務規格）：
 *   ─ 每個 Fixed / Floating 元素的 startPI / endPI
 *       → GripType::Vertex（對應橙色方塊）
 *   ─ Floating / Free 元素的幾何中心點
 *       → GripType::Midpoint（對應三角形）
 *
 * 拖曳流程：
 *   onGripDrag()  → movePI(idx, newPos) + solve()
 *   onGripDragEnd() → (solve 已在 drag 中呼叫) 記錄快照供 Undo
 *
 * 生命週期：
 *   GripManager::attachProvider(std::make_unique<AlignmentGripProvider>(edit));
 */
class AlignmentGripProvider : public IGripProvider
{
public:
    /**
     * @param edit  正在編輯中的水平線形。呼叫者確保 edit 的生命期長於本物件。
     */
    explicit AlignmentGripProvider(railway::HorizontalAlignmentEdit* edit);

    // ── IGripProvider interface ──────────────────────────────────────────────

    QVector<GripPoint> computeGrips() const override;

    void onGripDragBegin(const QString& gripId) override;

    /**
     * Grip 即時拖曳：
     *   1. 解析 gripId → element index + PI side（start / end / mid）
     *   2. 呼叫 m_edit->movePI(idx, newPos)
     *   3. 呼叫 m_edit->solve()      ← 觸發 changed() → AlignmentRenderer::refresh()
     */
    void onGripDrag(const QString& gripId, const gp_Pnt& newPos) override;

    /**
     * Grip 拖曳結束：移動距離過小時視為取消。
     */
    void onGripDragEnd(const QString& gripId,
                       const gp_Pnt& startPos,
                       const gp_Pnt& endPos) override;

private:
    // ── 輔助結構 ──────────────────────────────────────────────────────────────

    /// GripId 解析結果
    struct ParsedGrip {
        int     elemIdx  = -1;  ///< m_edit->elements() 中的 index
        bool    isStart  = true; ///< true = startPI, false = endPI（midpoint 時無意義）
        bool    isMid    = false; ///< true = Midpoint grip（Float/Free 中心點）
    };

    /// 從 gripId 字串還原 ParsedGrip
    static ParsedGrip parseGripId(const QString& gripId);

    /// 把 gp_Pnt 轉為 QPointF（XY 平面，Z 忽略）
    static QPointF toQPointF(const gp_Pnt& p);

    railway::HorizontalAlignmentEdit* m_edit = nullptr;

    /// 拖曳開始時的快照（用於 Undo：end 時比對是否真正移動）
    struct Snapshot {
        int     elemIdx;
        QPointF startPI;
        QPointF endPI;
    };
    QVector<Snapshot> m_snapshots;
};

} // namespace cad
} // namespace aicad
