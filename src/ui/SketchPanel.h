// src/ui/SketchPanel.h
#pragma once
#include "../command/ConstraintCommands.h"
#include <QDockWidget>
#include <QButtonGroup>
#include <QVBoxLayout>
#include "cad/Sketch.h"
#include "cad/SketchInstance.h"
#include "cad/sketch/SketchConstraint.h"
#include "cad/sketch/ConstraintOverlayManager.h"
#include <memory>

#include <QCheckBox>
QT_BEGIN_NAMESPACE
class QLabel; class QToolButton; class QComboBox;
class QSpinBox; class QDoubleSpinBox; class QTreeWidget;
class QTreeWidgetItem; class QStackedWidget; class QPushButton;
class QGroupBox; class QLineEdit; class QSplitter; class QCheckBox;
QT_END_NAMESPACE

namespace aicad::cad { class ConstraintPickSession; }

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

    /** Phase: 顯示選點提示（點擊尺寸按鈕後進入選點狀態） */
    void showPickPrompt(const QString& text);
    void clearPickPrompt();

    // ── Phase 6 ─────────────────────────────────────────────────────────
    /** 進入 master 草圖的約束覆蓋顯示模式 */
    void enterSketchMode(cad::Sketch* sketch,
                         const Handle(AIS_InteractiveContext)& ctx,
                         const gp_Trsf& toWorld);
    /** 進入 SketchInstance 的約束覆蓋顯示模式 */
    void enterInstanceMode(cad::SketchInstance* inst,
                           const Handle(AIS_InteractiveContext)& ctx,
                           const gp_Trsf& toWorld);
    /** 離開覆蓋模式（detach overlay） */
    void exitOverlayMode();

    /// ✅ Task E: 取得 ConstraintOverlayManager（供 UIManager PlaceDimLine 預覽用）
    cad::ConstraintOverlayManager* overlay() const { return m_overlay.get(); }

Q_SIGNALS:
    // ── 建構幾何 ────────────────────────────────────────────────
    void requestAddConstructionLine();
    void requestAddCenterline();
    void requestAddConstructionCircle();
    /// ⚠️ 新增：切換選取的幾何在「建構／一般」間來回切換（對應 CONSTRUCTION
    /// 命令，別名 CT）。見 ConstructionToggleCommand.h 檔頭說明。
    void requestToggleConstruction();

    // ── 約束 ────────────────────────────────────────────────────
    void requestConstraint(cad::ConstraintType type);
    /** 帶數值的約束（距離/半徑/角度）— 相容舊介面 */
    void requestConstraintWithValue(cad::ConstraintType type, double value);
    /** 帶數值 + 參數表達式 + driving 旗標的完整尺寸約束 */
    void requestConstraintWithValueAndExpr(cad::ConstraintType type,
                                           double value,
                                           const QString& paramExpr,
                                           bool driving);
    /** 刪除約束 */
    void requestRemoveConstraint(const QString& uuid);
    /** 編輯尺寸約束（從清單雙擊觸發） */
    void requestEditConstraint(const QString& uuid,
                               const QString& newExpr,
                               double         newValue,
                               bool           isLiteralNumber);

    // ── Solver ──────────────────────────────────────────────────
    void requestSolve();

    void regionDetectionRequested();  // 使用者點選「偵測區域」
    void regionSelected(const QString& regionUuid);

    // Phase 6：尺寸線點擊後向外廣播（UIManager 可轉發給 ParameterPanel）
    void dimensionConstraintClicked(const QString& constraintUuid,
                                    cad::ConstraintOverlayManager::Mode mode,
                                    const QString& instanceId);

private Q_SLOTS:
    void onConstraintAdded(const QString& uuid);
    void onConstraintRemoved(const QString& uuid);
    void onConstraintSolved(cad::SolveResult result);
    void onGeometryChanged();
    void onConstraintItemClicked(QTreeWidgetItem* item, int col);
    void onRemoveConstraintClicked();
    void onSolveClicked();
    // Phase 6：尺寸線點擊處理
    void onDimensionClicked(const QString& uuid,
                            cad::ConstraintOverlayManager::Mode mode,
                            const QString& instanceId);
    // 約束清單雙擊 → inline 編輯
    void onConstraintItemDoubleClicked(QTreeWidgetItem* item, int col);

    // Phase 6
    void onConstraintReadyFromSession(const QList<cad::GeomRef>& refs,
                                      double value,
                                      const QString& paramExpr,
                                      bool driving,
                                      cad::ConstraintType type);
    // 覆蓋層顯示/隱藏切換
    void onToggleOverlay();

private:
    void setupUI();
    void setupConstructionGroup(QWidget* parent, QVBoxLayout* layout);
    void setupConstraintGroup(QWidget* parent, QVBoxLayout* layout);
    void setupSolverGroup(QWidget* parent, QVBoxLayout* layout);
    void refreshConstraintList();
    void updateDofLabel(int dof, cad::SolveStatus status);
    QString constraintTypeName(cad::ConstraintType t) const;

    cad::Sketch* m_sketch = nullptr;

    bool m_pickingActive = false;
    cad::ConstraintPickSession* m_pickSession = nullptr;  ///< Phase 6：從 UIManager 注入
public:
    void setPickSession(cad::ConstraintPickSession* session) {
        m_pickSession = session;
    }
private:  // 選點進行中，防止尺寸按鈕重複觸發

    // Phase 6：約束覆蓋管理員
    std::unique_ptr<cad::ConstraintOverlayManager> m_overlay;

    // 建構幾何按鈕
    QToolButton* m_btnConstrLine    = nullptr;
    QToolButton* m_btnCenterline    = nullptr;
    QToolButton* m_btnConstrCircle  = nullptr;
    QToolButton* m_btnToggleConstruction = nullptr;  ///< 選取的幾何 建構⇄一般 來回切換

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
    QToolButton* m_btnGeneralDim    = nullptr;  ///< GDIM：一般尺寸（取代舊5個按鈕）
    QDoubleSpinBox* m_valueInput    = nullptr;
    // paramExpr 輸入：可輸入參數名（如 "width"）取代純數值
    QLineEdit*   m_exprInput        = nullptr;
    QCheckBox*   m_drivingCheck     = nullptr;  // Driving / 量測模式

    // 缺少的幾何約束按鈕
    QToolButton* m_btnMidpoint      = nullptr;
    QToolButton* m_btnPointOnCurve  = nullptr;
    QToolButton* m_btnCollinear     = nullptr;
    QToolButton* m_btnSymmetric     = nullptr;
    QToolButton* m_btnSlope         = nullptr;

    // 約束清單
    QTreeWidget* m_constraintTree   = nullptr;
    QPushButton* m_btnRemove        = nullptr;
    QPushButton* m_btnToggleOverlay = nullptr;  // 顯示/隱藏約束符號

    // Solver 狀態
    QPushButton* m_btnSolve         = nullptr;
    QLabel*      m_dofLabel         = nullptr;
    QLabel*      m_statusLabel      = nullptr;
    QLabel*      m_residualLabel    = nullptr;
};

} // namespace aicad::ui
