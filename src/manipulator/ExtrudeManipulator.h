#pragma once

#include <QObject>
#include <QPointer>
#include <QFrame>
#include <QLineEdit>
#include <QLabel>

#include <AIS_InteractiveContext.hxx>
#include <gp_Pnt.hxx>

#include "AIS_ExtrudeManipulator.h"

namespace aicad {
namespace cad   { class Extrude; }
namespace view  { class CadView; }

namespace manipulator {

/// 拉伸操控器協調器
/// - 在 3D 視窗中顯示 AIS_ExtrudeManipulator
/// - 攔截滑鼠事件以實現拖曳
/// - 提供浮動 mini input box
class ExtrudeManipulator : public QObject
{
    Q_OBJECT

public:
    explicit ExtrudeManipulator(cad::Extrude*  extrude,
                                view::CadView* cadView,
                                QObject*       parent = nullptr);
    ~ExtrudeManipulator() override;

    void show();
    void hide();

Q_SIGNALS:
    /// 使用者確認新高度
    void heightConfirmed(double height);
    /// 使用者取消
    void cancelled();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private Q_SLOTS:
    void onMiniInputConfirmed();
    void onExtrudeRebuilt();

private:
    // ── helpers ──
    void updateAIS();
    void positionMiniInput();
    bool pickManipulatorPart(const QPoint& screenPt, int& outMode);
    gp_Pnt screenToWorld(const QPoint& screenPt) const;

    // 拖曳狀態
    enum class DragMode { None, Arrow, Flip };

    // ── data ──
    cad::Extrude*   m_extrude;
    view::CadView*  m_cadView;

    Handle(AIS_ExtrudeManipulator) m_aisManip;
    Handle(AIS_InteractiveContext)  m_ctx;

    DragMode    m_dragMode   = DragMode::None;
    gp_Pnt      m_dragStart;
    double      m_heightAtDragStart = 0.0;

    // Mini input widget（父為 CadView）
    QFrame*     m_miniWidget  = nullptr;
    QLineEdit*  m_miniEdit    = nullptr;
    QLabel*     m_miniLabel   = nullptr;

    // 建置 mini input widget
    void buildMiniWidget();
};

} // namespace manipulator
} // namespace aicad
