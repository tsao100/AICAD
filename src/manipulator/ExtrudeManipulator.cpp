#include "ExtrudeManipulator.h"

#include "cad/Extrude.h"
#include "view/CadView.h"
#include "core/Application.h"
#include "core/EventBus.h"
#include "AIS_ExtrudeManipulator.h"

#include <AIS_InteractiveContext.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <V3d_View.hxx>
#include <gp_Lin.hxx>
#include <BRepBndLib.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>

#include <QMouseEvent>
#include <QKeyEvent>
#include <QHBoxLayout>
#include <QDoubleValidator>
#include <QApplication>
#include <QDebug>

namespace aicad::manipulator {

// ───────────────────────────────────────────────────────────────────────────
ExtrudeManipulator::ExtrudeManipulator(cad::Extrude*  extrude,
                                       view::CadView* cadView,
                                       QObject*       parent)
    : QObject(parent)
    , m_extrude(extrude)
    , m_cadView(cadView)
{
    Q_ASSERT(extrude && cadView);

    // AIS context は CadView から取得（Document 経由）
    m_ctx = cadView->context();

    // Sketch 平面法線 → 拉伸方向
    auto* sketch = extrude->sketch();
    Q_ASSERT(sketch);

    const QVector3D n = sketch->plane()->normal();
//    const gp_Dir extDir(n.x(), n.y(), n.z());

    // 現有 shape 的 bounding box 中心作為 base
    // ── 新：找 profile face（最靠近 sketch 平面的面）＋ 計算形心 ────────
    // ── 取代原本 Bnd_Box bbox 那段，同時計算 centroid 與 profileFace ──────
    const gp_Vec extVec(n.x(), n.y(), n.z());
    const double sign    = extrude->isReversed() ? -1.0 : 1.0;
    const gp_Vec extDir  = extVec * sign;          // ✅ 含方向符號
    const gp_Dir extGDir(extDir);

    // ── 找底面（沿拉伸方向投影最小 = sketch 平面那側）────────────────────
    double      minProj = std::numeric_limits<double>::max();
    gp_Pnt      bottomCentroid;
    TopoDS_Face profileFace;

    for (TopExp_Explorer exp(extrude->shape(), TopAbs_FACE); exp.More(); exp.Next()) {
        TopoDS_Face f = TopoDS::Face(exp.Current());
        GProp_GProps gp;
        BRepGProp::SurfaceProperties(f, gp);
        double proj = gp.CentreOfMass().XYZ().Dot(extDir.XYZ());
        if (proj < minProj) {
            minProj        = proj;
            bottomCentroid = gp.CentreOfMass();
            profileFace    = f;
        }
    }

    // ── 箭桿起點 = 底面形心 + 沿拉伸方向偏移 height → 頂面位置 ─────────────
    m_bottomCentroid = bottomCentroid;                                    // ✅ 快取

    gp_Pnt base = m_bottomCentroid.Translated(gp_Vec(extGDir) * extrude->height());
    m_aisManip = new AIS_ExtrudeManipulator(base, extGDir,
                                            extrude->height(), profileFace);

    m_aisManip->SetSymmetric(extrude->isSymmetric());
    m_aisManip->SetReversed (extrude->isReversed());

    buildMiniWidget();

    // 監聽 Extrude 高度更新（外部改變時同步）
    connect(extrude, &cad::Extrude::heightChanged,
            this,    &ExtrudeManipulator::onExtrudeRebuilt);

    // 攔截 CadView 的滑鼠事件
    cadView->installEventFilter(this);
}

ExtrudeManipulator::~ExtrudeManipulator()
{
    hide();
    if (m_cadView)
        m_cadView->removeEventFilter(this);
    delete m_miniWidget;
}

// ── show / hide ──────────────────────────────────────────────────────────────
void ExtrudeManipulator::show()
{
    if (!m_cadView) return;

    m_aisManip->SetView(m_cadView->view());   // ← 新增

    // 透過 CadView overlay 機制顯示，可在 displayAllFeatures 後自動恢復
    m_cadView->addOverlayAIS(m_aisManip, {1, 2});

    positionMiniInput();
    m_miniWidget->show();
}

void ExtrudeManipulator::hide()
{
    if (m_cadView)
        m_cadView->removeOverlayAIS(m_aisManip);

    if (m_miniWidget)
        m_miniWidget->hide();
}

// ── build mini widget ────────────────────────────────────────────────────────
void ExtrudeManipulator::buildMiniWidget()
{
    m_miniWidget = new QFrame(m_cadView);
    m_miniWidget->setObjectName("ExtrudeManipMiniBox");
    m_miniWidget->setStyleSheet(
        "QFrame#ExtrudeManipMiniBox {"
        "  background: rgba(30,30,30,220);"
        "  border: 1px solid #f0a020;"
        "  border-radius: 4px;"
        "  padding: 2px 6px;"
        "}"
        "QLineEdit {"
        "  background: transparent;"
        "  color: #f0c040;"
        "  border: none;"
        "  font: bold 12px 'Consolas';"
        "  min-width: 70px;"
        "}"
        "QLabel { color: #aaaaaa; font: 10px; }"
        );
    m_miniWidget->setFrameShape(QFrame::NoFrame);

    auto* lay = new QHBoxLayout(m_miniWidget);
    lay->setContentsMargins(4, 2, 4, 2);
    lay->setSpacing(4);

    m_miniLabel = new QLabel("H:", m_miniWidget);
    m_miniEdit  = new QLineEdit(m_miniWidget);
    m_miniEdit->setValidator(new QDoubleValidator(0.01, 99999.0, 3, m_miniEdit));
    m_miniEdit->setText(QString::number(m_extrude->height(), 'f', 2));
    m_miniEdit->setAlignment(Qt::AlignRight);

    lay->addWidget(m_miniLabel);
    lay->addWidget(m_miniEdit);
    m_miniWidget->adjustSize();
    m_miniWidget->hide();

    connect(m_miniEdit, &QLineEdit::returnPressed,
            this,       &ExtrudeManipulator::onMiniInputConfirmed);

    // 即時預覽（每次文字改變就更新）
    connect(m_miniEdit, &QLineEdit::textEdited,
            this, [this](const QString& txt) {
                bool ok;
                double h = txt.toDouble(&ok);
                if (ok && h > 0.0) {
                    m_aisManip->SetHeight(h);
                    updateAIS();
                }
            });
}

// ── position mini input at arrow tip ────────────────────────────────────────
void ExtrudeManipulator::positionMiniInput()
{
    if (!m_cadView) return;
    auto view = m_cadView->view();
    if (view.IsNull()) return;

    gp_Pnt tip = m_aisManip->ArrowTipPosition();

    // ✅ Convert 直接給出 pixel 座標（原本用 Project 是錯的）
    Standard_Integer sx = 0, sy = 0;
    view->Convert(tip.X(), tip.Y(), tip.Z(), sx, sy);

    m_miniWidget->move(sx + 14, sy - m_miniWidget->height() / 2);
}

// ── updateAIS ───────────────────────────────────────────────────────────────
void ExtrudeManipulator::updateAIS()
{
    if (m_ctx.IsNull()) return;

    auto* sketch = m_extrude->sketch();
    if (sketch && sketch->plane() && !m_extrude->shape().IsNull()) {

        const QVector3D n   = sketch->plane()->normal();
        const gp_Vec extVec(n.x(), n.y(), n.z());
        const double sign   = m_extrude->isReversed() ? -1.0 : 1.0;
        const gp_Vec extDir = extVec * sign;         // ✅ 含方向符號

        m_aisManip->SetDir(gp_Dir(extDir));

        // 底面形心（XY 最準）+ 偏移至頂面
        double minProj = std::numeric_limits<double>::max();
        gp_Pnt bottomCentroid;

        for (TopExp_Explorer exp(m_extrude->shape(), TopAbs_FACE);
             exp.More(); exp.Next())
        {
            GProp_GProps gp;
            BRepGProp::SurfaceProperties(TopoDS::Face(exp.Current()), gp);
            double proj = gp.CentreOfMass().XYZ().Dot(extDir.XYZ());
            if (proj < minProj) {
                minProj        = proj;
                bottomCentroid = gp.CentreOfMass();
            }
        }
        m_bottomCentroid = bottomCentroid;                                    // ✅ 更新快取

        gp_Pnt newBase = bottomCentroid.Translated(extDir * m_extrude->height());
        m_aisManip->SetBase(newBase);
    }

    m_aisManip->SetView(m_cadView->view());
    m_ctx->Redisplay(m_aisManip, /*update=*/true);
    positionMiniInput();
}

// ── event filter ─────────────────────────────────────────────────────────────
bool ExtrudeManipulator::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != m_cadView) return false;

    switch (event->type()) {

    case QEvent::MouseButtonPress: {
        auto* me = static_cast<QMouseEvent*>(event);
        if (me->button() != Qt::LeftButton) break;

        int hitMode = 0;
        if (!pickManipulatorPart(me->pos(), hitMode)) break;

        if (hitMode == 1) {
            // 主箭頭拖曳開始
            m_dragMode = DragMode::Arrow;
            m_dragStart = screenToWorld(me->pos());
            m_heightAtDragStart = m_extrude->height();
            m_cadView->setCursor(Qt::SizeVerCursor);
            return true;   // 消耗事件

        } else if (hitMode == 2) {
            // 翻轉按鈕：toggling reverse / symmetric
            // 左鍵 → Reverse，Shift+左鍵 → Symmetric
            if (me->modifiers() & Qt::ShiftModifier) {
                m_extrude->setSymmetric(!m_extrude->isSymmetric());
                m_aisManip->SetSymmetric(m_extrude->isSymmetric());
            } else {
                m_extrude->setReversed(!m_extrude->isReversed());
                m_aisManip->SetReversed(m_extrude->isReversed());
            }
            updateAIS();
            return true;
        }
        break;
    }

    case QEvent::MouseMove: {
        if (m_dragMode != DragMode::Arrow) break;
        auto* me = static_cast<QMouseEvent*>(event);

        gp_Pnt worldNow = screenToWorld(me->pos());
        gp_Vec delta(m_dragStart, worldNow);

        // m_dir 已含方向符號（含 reversed），直接 Dot
        // ✅ 以 m_heightAtDragStart 為基準，避免每幀累積
        const QVector3D n = m_extrude->sketch()->plane()->normal();
        const double sign   = m_extrude->isReversed() ? -1.0 : 1.0;
        gp_Dir extDir(n.x(), n.y(), n.z());
        double proj = delta.Dot(gp_Vec(extDir) * sign);

        double newH = std::max(0.1, m_heightAtDragStart + proj);
        newH = std::round(newH * 10.0) / 10.0;

        m_aisManip->SetHeight(newH);
        m_aisManip->SetBase(computeShaftBase());
        m_miniEdit->setText(QString::number(newH, 'f', 2));
        updateAIS();
        m_extrude->setHeight(newH);
        return true;
    }

    case QEvent::MouseButtonRelease: {
        if (m_dragMode == DragMode::Arrow) {
            m_dragMode = DragMode::None;
            m_cadView->setCursor(Qt::ArrowCursor);
            // finalize：發 signal 但不 complete command（讓使用者可繼續微調）
            Q_EMIT heightConfirmed(m_extrude->height());
        }
        break;
    }

    case QEvent::KeyPress: {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Escape) {
            Q_EMIT cancelled();
            return true;
        }
        if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
            // 聚焦在 3D 視窗時按 Enter → 和 mini input confirm 相同
            m_miniEdit->hasFocus() ? onMiniInputConfirmed()
                                   : Q_EMIT heightConfirmed(m_extrude->height());
            return true;
        }
        break;
    }
    case QEvent::Wheel: {
        // 縮放後箭頭大小需重算
        // Wheel 事件先讓 CadView 處理（不 consume），之後觸發 updateAIS
        QMetaObject::invokeMethod(this, &ExtrudeManipulator::updateAIS,
                                  Qt::QueuedConnection);
        break;
    }

    default: break;
    }

    return false;
}

// ── helpers ──────────────────────────────────────────────────────────────────
bool ExtrudeManipulator::pickManipulatorPart(const QPoint& screenPt, int& outMode)
{
    if (m_ctx.IsNull()) return false;

    m_ctx->MoveTo(screenPt.x(), screenPt.y(), m_cadView->view(), /*update=*/false);

    Handle(SelectMgr_EntityOwner) owner = m_ctx->DetectedOwner();
    if (owner.IsNull()) return false;
    if (owner->Selectable() != m_aisManip) return false;

    // 透過 dynamic_cast 取得 partMode
    Handle(ExtrudeOwner) eo = Handle(ExtrudeOwner)::DownCast(owner);
    if (eo.IsNull()) return false;

    outMode = eo->partMode();
    return true;
}

gp_Pnt ExtrudeManipulator::screenToWorld(const QPoint& screenPt) const
{
    auto view = m_cadView->view();
    if (view.IsNull()) return gp_Pnt();

    double xEye, yEye, zEye, xAt, yAt, zAt;
    view->Eye(xEye, yEye, zEye);
    view->At (xAt,  yAt,  zAt);

    double xWorld, yWorld, zWorld;
    view->Convert(screenPt.x(), screenPt.y(),
                  xWorld, yWorld, zWorld);
    return gp_Pnt(xWorld, yWorld, zWorld);
}

gp_Pnt ExtrudeManipulator::computeShaftBase() const
{
    auto* sketch = m_extrude->sketch();
    Q_ASSERT(sketch && sketch->plane());

    const QVector3D n  = sketch->plane()->normal();
    const double sign  = m_extrude->isReversed() ? -1.0 : 1.0;
    const gp_Vec extDir(n.x() * sign, n.y() * sign, n.z() * sign);

    // ✅ 直接使用快取的底面形心，加上高度偏移到頂面
    return m_bottomCentroid.Translated(extDir * m_extrude->height());
}

// ── slots ────────────────────────────────────────────────────────────────────
void ExtrudeManipulator::onMiniInputConfirmed()
{
    bool ok;
    double h = m_miniEdit->text().toDouble(&ok);
    if (!ok || h <= 0.0) return;

    m_extrude->setHeight(h);
    m_aisManip->SetHeight(h);
    updateAIS();
    Q_EMIT heightConfirmed(h);
}

void ExtrudeManipulator::onExtrudeRebuilt()
{
    if (!m_aisManip.IsNull()) {
        m_aisManip->SetHeight(m_extrude->height());
        m_miniEdit->setText(QString::number(m_extrude->height(), 'f', 2));
        updateAIS();
    }
}

} // namespace aicad::manipulator
