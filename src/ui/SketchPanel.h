// src/ui/SketchPanel.h
#pragma once
#include <QDockWidget>
#include <QButtonGroup>
#include <QVBoxLayout>
#include "cad/Sketch.h"
#include "cad/sketch/SketchConstraint.h"

QT_BEGIN_NAMESPACE
class QLabel; class QToolButton; class QComboBox;
class QSpinBox; class QDoubleSpinBox; class QTreeWidget;
class QTreeWidgetItem; class QStackedWidget; class QPushButton;
class QGroupBox; class QLineEdit; class QSplitter;
QT_END_NAMESPACE

namespace aicad::ui {

/**
 * @brief 草圖編輯面板 Dock
 *
 * 提供三個功能區：
 *   1. 建構幾何（建構線 / 中心線）
 *   2. 約束工具
 *   3. 求解器狀態 + DOF 顯示
 */
class SketchPanel : public QDockWidget {
    Q_OBJECT
public:
    explicit SketchPanel(QWidget* parent = nullptr);
    ~SketchPanel() override;

    /** 切換到「正在編輯草圖」模式，並綁定 Sketch 物件 */
    void setActiveSketch(cad::Sketch* sketch);
    /** 離開草圖編輯模式 */
    void clearSketch();

Q_SIGNALS:
    // ── 建構幾何 ────────────────────────────────────────────────
    void requestAddConstructionLine();
    void requestAddCenterline();
    void requestAddConstructionCircle();

    // ── 約束 ────────────────────────────────────────────────────
    void requestConstraint(cad::ConstraintType type);
    /** 帶數值的約束（距離/半徑/角度） */
    void requestConstraintWithValue(cad::ConstraintType type, double value);
    /** 刪除約束 */
    void requestRemoveConstraint(const QString& uuid);

    // ── Solver ──────────────────────────────────────────────────
    void requestSolve();

private Q_SLOTS:
    void onConstraintAdded(const QString& uuid);
    void onConstraintRemoved(const QString& uuid);
    void onConstraintSolved(cad::SolveResult result);
    void onGeometryChanged();
    void onConstraintItemClicked(QTreeWidgetItem* item, int col);
    void onRemoveConstraintClicked();
    void onSolveClicked();

private:
    void setupUI();
    void setupConstructionGroup(QWidget* parent, QVBoxLayout* layout);
    void setupConstraintGroup(QWidget* parent, QVBoxLayout* layout);
    void setupSolverGroup(QWidget* parent, QVBoxLayout* layout);
    void refreshConstraintList();
    void updateDofLabel(int dof, cad::SolveStatus status);
    QString constraintTypeName(cad::ConstraintType t) const;

    cad::Sketch* m_sketch = nullptr;

    // 建構幾何按鈕
    QToolButton* m_btnConstrLine    = nullptr;
    QToolButton* m_btnCenterline    = nullptr;
    QToolButton* m_btnConstrCircle  = nullptr;

    // 約束按鈕（幾何型）
    QToolButton* m_btnCoincident    = nullptr;
    QToolButton* m_btnHorizontal    = nullptr;
    QToolButton* m_btnVertical      = nullptr;
    QToolButton* m_btnParallel      = nullptr;
    QToolButton* m_btnPerpendicular = nullptr;
    QToolButton* m_btnTangent       = nullptr;
    QToolButton* m_btnEqualLen      = nullptr;
    QToolButton* m_btnEqualRad      = nullptr;
    QToolButton* m_btnConcentric    = nullptr;
    QToolButton* m_btnFixed         = nullptr;
    // 尺寸型約束
    QToolButton* m_btnDistance      = nullptr;
    QToolButton* m_btnRadius        = nullptr;
    QToolButton* m_btnAngle         = nullptr;
    QDoubleSpinBox* m_valueInput    = nullptr;

    // 約束清單
    QTreeWidget* m_constraintTree   = nullptr;
    QPushButton* m_btnRemove        = nullptr;

    // Solver 狀態
    QPushButton* m_btnSolve         = nullptr;
    QLabel*      m_dofLabel         = nullptr;
    QLabel*      m_statusLabel      = nullptr;
    QLabel*      m_residualLabel    = nullptr;
};

} // namespace aicad::ui
