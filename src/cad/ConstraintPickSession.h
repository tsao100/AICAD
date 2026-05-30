#pragma once
#include <QObject>
#include <QVector2D>
#include <QString>
#include <QVector>
#include "sketch/SketchConstraint.h"

namespace aicad::cad {

class Sketch;

/**
 * @brief 約束選點工作階段（Point-Pick Session）
 *
 * 管理「施加距離/角度約束前，讓使用者依序點選 N 個點」的狀態機。
 *
 * 流程：
 *   1. UIManager 呼叫 begin(sketch, type, value, expr)
 *   2. CadView 切換到 GetPoint 模式，顯示提示文字
 *   3. 使用者每點擊一個點，呼叫 feedPoint(planePt, geomUuid, geomHandle)
 *   4. 收齊 requiredCount 個點後 emit constraintReady(refs, value, paramExpr)
 *   5. UIManager 收到 signal 後呼叫 Sketch::constrainDistance / constrainRadius…
 *   6. CadView 切回 Sketching 模式
 *
 * 支援的約束類型及所需點數：
 *   FixedDistance  → 2 點（可以是端點、中心點、任意點）
 *   FixedRadius    → 1 點（圓/弧中心或 WholeGeom）
 *   FixedX / FixedY → 1 點
 *   FixedAngleDim  → 2 點（兩條線上各取一點，取線方向計算夾角）
 */
class ConstraintPickSession : public QObject {
    Q_OBJECT
public:
    explicit ConstraintPickSession(QObject* parent = nullptr);

    // ── 開始 ────────────────────────────────────────────────────────────────
    void begin(Sketch* sketch,
               ConstraintType type,
               double value,
               const QString& paramExpr = QString(),
               bool driving = true);

    void cancel();

    bool isActive() const { return m_active; }
    ConstraintType constraintType() const { return m_type; }

    /** 目前已收集的點數 */
    int pickedCount() const { return m_refs.size(); }

    /** 還需要幾個點 */
    int remaining() const { return m_required - m_refs.size(); }

    /** 提示文字（顯示在 status bar / SketchPanel） */
    QString promptText() const;

    // ── 餵入點 ──────────────────────────────────────────────────────────────
    /**
     * @param planePt    草圖平面 2D 座標（snap 校正後）
     * @param geomUuid   snap 到的草圖幾何 UUID（空 = 自由點）
     * @param geomHandle snap 到的端點 handle（-1 = WholeGeom）
     */
    // ── Phase 2：幾何選取 ───────────────────────────────────────────────────
    /**
     * 餵入幾何元素（用於幾何約束命令，如 PAR/TAN）
     */
    void feedGeom(const QString& geomUuid,
                  GeomHandle handle = GeomHandle::WholeGeom);

    // ── Phase 3B：距離子類型判斷 ─────────────────────────────────────────
    /**
     * 根據已選取的兩個 ref，判斷距離子類型
     * 若不平行（L2L 情況），回傳 DistanceMode::Invalid 並 emit warning
     */
    DistanceMode resolveDistanceMode() const;

    /**
     * 確認尺寸線偏移量（使用者在視窗中點擊後呼叫）
     */
    void confirmDimLineOffset(double offsetX, double offsetY);

    void feedPoint(const QVector2D& planePt,
                   const QString& geomUuid,
                   int geomHandle);

Q_SIGNALS:
    /** 所有點收齊，可施加約束 */
    void constraintReady(QList<GeomRef> refs,
                         double value,
                         QString paramExpr,
                         bool driving,
                         ConstraintType type);

    /** 需要再點第 N 個點（從 1 開始，用於更新 status bar 提示） */
    void promptChanged(QString text);

    /** 取消或完成 */
    void sessionEnded();

public:
    double dimLineOffsetX() const { return m_dimLineOffsetX; }
    double dimLineOffsetY() const { return m_dimLineOffsetY; }
private:
    int requiredPointCount(ConstraintType type) const;
    GeomRef makeRef(const QString& geomUuid, int geomHandle,
                    const QVector2D& planePt);

    Sketch*        m_sketch   = nullptr;
    ConstraintType m_type     = ConstraintType::FixedDistance;
    double         m_value    = 0.0;
    QString        m_paramExpr;
    bool           m_driving  = true;
    bool           m_active   = false;
    int            m_required = 0;
    QList<GeomRef> m_refs;
    double         m_dimLineOffsetX = 0.0;  ///< Phase 3B：確認的尺寸線偏移 X
    double         m_dimLineOffsetY = 0.0;  ///< Phase 3B：確認的尺寸線偏移 Y
};

} // namespace aicad::cad
