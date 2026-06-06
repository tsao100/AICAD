#include "DimPreviewOverlay.h"
#include "cad/Sketch.h"
#include "cad/sketch/SketchConstraint.h"

#include <QDebug>
#include <cmath>

#include <Graphic3d_ArrayOfPolylines.hxx>
#include <Graphic3d_ArrayOfSegments.hxx>
#include <Graphic3d_Group.hxx>
#include <Graphic3d_AspectLine3d.hxx>
#include <Graphic3d_Text.hxx>
#include <TCollection_ExtendedString.hxx>
#include <Graphic3d_AspectText3d.hxx>
#include <Prs3d_Presentation.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Quantity_Color.hxx>
#include <Aspect_TypeOfLine.hxx>
#include <Graphic3d_DisplayPriority.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <Precision.hxx>

namespace aicad::view {

using CT = cad::ConstraintType;
using DM = cad::DistanceMode;

// ─────────────────────────────────────────────────────────────────────────────
DimPreviewOverlay::DimPreviewOverlay(QObject* parent)
    : QObject(parent)
{}

DimPreviewOverlay::~DimPreviewOverlay()
{
    clearPrs();
}

void DimPreviewOverlay::setContext(const Handle(AIS_InteractiveContext)& ctx)
{
    m_context = ctx;
}

void DimPreviewOverlay::setSketch(cad::Sketch* sk)
{
    m_sketch = sk;
}

// ─────────────────────────────────────────────────────────────────────────────
void DimPreviewOverlay::setPreview(const PreviewInfo& info)
{
    m_info = info;
    if (m_info.valid) rebuild();
}

void DimPreviewOverlay::clearPreview()
{
    m_info.valid = false;
    clearPrs();
}

void DimPreviewOverlay::setMousePlanePt(const QVector2D& pt)
{
    m_mouse = pt;
    if (m_info.valid) rebuild();
}

// ─────────────────────────────────────────────────────────────────────────────
void DimPreviewOverlay::clearPrs()
{
    if (!m_prs.IsNull()) {
        m_prs->Clear();
        m_prs->Erase();
        m_prs.Nullify();
    }
    if (!m_context.IsNull())
        m_context->UpdateCurrentViewer();
}

// ─────────────────────────────────────────────────────────────────────────────
gp_Pnt DimPreviewOverlay::toWorld(const QVector2D& pt) const
{
    if (!m_sketch || !m_sketch->plane()) return gp_Pnt(pt.x(), pt.y(), 0);
    // Sketch::toWorld() is private; replicate via plane() like RubberBand::planeToWorld()
    auto* pl = m_sketch->plane();
    QVector3D w = pl->origin() + pl->xAxis() * pt.x() + pl->yAxis() * pt.y();
    return gp_Pnt(w.x(), w.y(), w.z());
}

// ─────────────────────────────────────────────────────────────────────────────
// helpers：在 Graphic3d_Group 中加入一條線段
void DimPreviewOverlay::addLine(const Handle(Graphic3d_Group)& grp,
                                const gp_Pnt& a, const gp_Pnt& b)
{
    Handle(Graphic3d_ArrayOfSegments) seg = new Graphic3d_ArrayOfSegments(2);
    seg->AddVertex(a);
    seg->AddVertex(b);
    grp->AddPrimitiveArray(seg);
}

// 在 tip 處加箭頭，dir 為箭頭方向（世界座標），size 為箭頭半長（mm）
void DimPreviewOverlay::addArrow3D(const Handle(Graphic3d_Group)& grp,
                                   const gp_Pnt& tip, const gp_Vec& dir, double size)
{
    gp_Vec u = dir;
    if (u.Magnitude() < Precision::Confusion()) return;
    u.Normalize();

    // 在草圖平面內求一個垂直 u 的向量
    // 用草圖法向量 n 叉 u 取垂直
    gp_Vec n(0, 0, 1);
    if (m_sketch && m_sketch->plane()) {
        auto* pl = m_sketch->plane();
        QVector3D nq = pl->normal();
        n = gp_Vec(nq.x(), nq.y(), nq.z());
    }
    gp_Vec perp = n.Crossed(u);
    if (perp.Magnitude() < Precision::Confusion()) {
        // fallback
        perp = gp_Vec(u.Y(), -u.X(), 0);
    }
    perp.Normalize();

    double len = size * 2.5;
    gp_Pnt base(tip.X() - u.X()*len, tip.Y() - u.Y()*len, tip.Z() - u.Z()*len);
    gp_Pnt w1(base.X() + perp.X()*size, base.Y() + perp.Y()*size, base.Z() + perp.Z()*size);
    gp_Pnt w2(base.X() - perp.X()*size, base.Y() - perp.Y()*size, base.Z() - perp.Z()*size);

    addLine(grp, tip, w1);
    addLine(grp, tip, w2);
}

// ─────────────────────────────────────────────────────────────────────────────
// 主要繪製：重建 OCCT Presentation
// ─────────────────────────────────────────────────────────────────────────────
void DimPreviewOverlay::rebuild()
{
    if (m_context.IsNull() || !m_sketch) return;

    // 清除舊 presentation
    clearPrs();

    // 取得兩端點的草圖座標
    if (m_info.refs.size() < 2 &&
        m_info.type != CT::FixedLength &&
        m_info.type != CT::FixedDiameter &&
        m_info.type != CT::FixedRadius)
        return;

    // ── 計算幾何 ─────────────────────────────────────────────────────────────
    QVector2D A, B;

    if (m_info.type == CT::FixedLength) {
        // 單線段：取 start/end
        if (m_info.refs.isEmpty()) return;
        auto* geom = m_sketch->findGeometry(m_info.refs[0].geomUuid);
        auto* ln = dynamic_cast<const cad::SketchLine*>(geom);
        if (!ln) return;
        A = ln->start;
        B = ln->end;
    } else if (m_info.type == CT::FixedDiameter || m_info.type == CT::FixedRadius) {
        // 圓/弧：用 mouse 方向決定尺寸線端點
        if (m_info.refs.isEmpty()) return;
        auto* geom = m_sketch->findGeometry(m_info.refs[0].geomUuid);
        QVector2D center; float r = 0;
        if (auto* c = dynamic_cast<const cad::SketchCircle*>(geom)) {
            center = c->center; r = c->radius;
        } else if (auto* a = dynamic_cast<const cad::SketchArc*>(geom)) {
            cad::GeomRef cr(m_info.refs[0].geomUuid, cad::GeomHandle::Center);
            cad::GeomRef sr(m_info.refs[0].geomUuid, cad::GeomHandle::Start);
            center = cr.resolvePosition(m_sketch);
            r = (sr.resolvePosition(m_sketch) - center).length();
        }
        QVector2D dir = m_mouse - center;
        if (dir.length() < 1e-3f) dir = QVector2D(1, 0);
        dir.normalize();
        B = center + dir * r;
        A = (m_info.type == CT::FixedDiameter) ? center - dir * r : center;
    } else {
        // FixedDistance / HorizDist / VertDist
        if (m_info.refs.size() < 2) return;
        A = m_info.refs[0].resolvePosition(m_sketch);
        B = m_info.refs[1].resolvePosition(m_sketch);
    }

    // ── 計算尺寸線端點 dA/dB（草圖平面 2D）──────────────────────────────────
    // 規則：
    //   延伸線 A→dA, B→dB 垂直於兩點連線（或水平/垂直方向）
    //   偏移距離及方向由滑鼠決定
    QVector2D dA, dB;

    if (m_info.type == CT::FixedHorizDist) {
        // 水平距離：尺寸線水平（草圖 Y 固定 = 滑鼠 Y 座標）
        // 尺寸線 Y 直接用滑鼠的 Y，兩端 X 各自對齊 A、B
        float dimY = m_mouse.y();
        // 最小偏移保護：若滑鼠 Y 太靠近兩點，強制偏移
        float minY = std::min(A.y(), B.y());
        float maxY = std::max(A.y(), B.y());
        if (dimY >= minY - 5.f && dimY <= maxY + 5.f) {
            // 滑鼠在兩點 Y 範圍內，依偏移方向強制移出
            float midY = (A.y() + B.y()) * 0.5f;
            dimY = (m_mouse.y() >= midY) ? maxY + 20.f : minY - 20.f;
        }
        dA = QVector2D(A.x(), dimY);   // 延伸線 A→dA：垂直，長度 |A.y - dimY|
        dB = QVector2D(B.x(), dimY);   // 延伸線 B→dB：垂直，長度 |B.y - dimY|（不等長）
    } else if (m_info.type == CT::FixedVertDist) {
        // 垂直距離：尺寸線垂直（草圖 X 固定 = 滑鼠 X 座標）
        float dimX = m_mouse.x();
        float minX = std::min(A.x(), B.x());
        float maxX = std::max(A.x(), B.x());
        if (dimX >= minX - 5.f && dimX <= maxX + 5.f) {
            float midX = (A.x() + B.x()) * 0.5f;
            dimX = (m_mouse.x() >= midX) ? maxX + 20.f : minX - 20.f;
        }
        dA = QVector2D(dimX, A.y());   // 延伸線 A→dA：水平，長度 |A.x - dimX|
        dB = QVector2D(dimX, B.y());   // 延伸線 B→dB：水平，長度 |B.x - dimX|（不等長）
    } else {
        // FixedLength / FixedDistance(PointToPoint)
        // 延伸線垂直於 AB 連線，偏移到滑鼠一側
        QVector2D ab = B - A;
        float abLen = ab.length();
        QVector2D perpDir;
        if (abLen < 1e-4f) {
            perpDir = QVector2D(0, 1);
        } else {
            perpDir = QVector2D(-ab.y(), ab.x()) / abLen;  // 左法向量（垂直 AB）
        }
        QVector2D mid = (A + B) * 0.5f;
        float dot = QVector2D::dotProduct(m_mouse - mid, perpDir);
        if (dot < 0) perpDir = -perpDir;          // 朝滑鼠那側
        float offset = std::abs(dot);
        if (offset < 5.f) offset = 20.f;          // 最小偏移 20 草圖單位
        dA = A + perpDir * offset;
        dB = B + perpDir * offset;
    }

    // ── 建立 OCCT Presentation ────────────────────────────────────────────────
    m_prs = new Prs3d_Presentation(
        m_context->MainPrsMgr()->StructureManager());

    // 顏色：尺寸線綠色，虛線延伸線也綠色
    Quantity_Color lineCol(0.0, 0.85, 0.0, Quantity_TOC_RGB);

    // ── 延伸線（虛線）────────────────────────────────────────────────────────
    {
        Handle(Graphic3d_AspectLine3d) dashAsp =
            new Graphic3d_AspectLine3d(lineCol, Aspect_TOL_DOT, 1.2f);
        Handle(Graphic3d_Group) grp = m_prs->NewGroup();
        grp->SetGroupPrimitivesAspect(dashAsp);
        addLine(grp, toWorld(A), toWorld(dA));
        addLine(grp, toWorld(B), toWorld(dB));
    }

    // ── 尺寸線（實線）+ 箭頭 ─────────────────────────────────────────────────
    {
        Handle(Graphic3d_AspectLine3d) solidAsp =
            new Graphic3d_AspectLine3d(lineCol, Aspect_TOL_SOLID, 1.8f);
        Handle(Graphic3d_Group) grp = m_prs->NewGroup();
        grp->SetGroupPrimitivesAspect(solidAsp);

        gp_Pnt wdA = toWorld(dA);
        gp_Pnt wdB = toWorld(dB);
        addLine(grp, wdA, wdB);

        // 箭頭方向
        gp_Vec dirAB(wdA, wdB);
        if (dirAB.Magnitude() > Precision::Confusion()) {
            addArrow3D(grp, wdA, gp_Vec(wdB, wdA));  // 指向 A 端
            addArrow3D(grp, wdB, dirAB);              // 指向 B 端
        }
    }

    // ── 數值文字（紅色，字高 36，平行尺寸線，居中）──────────────────────────
    {
        QString label = QString::number(m_info.value, 'f', 2);
        QVector2D midDim = (dA + dB) * 0.5f;
        gp_Pnt wMid = toWorld(midDim);

        Handle(Graphic3d_Text) gtext = new Graphic3d_Text(36.0f);
        gtext->SetText(TCollection_ExtendedString(
            label.toUtf8().constData(), Standard_True));
        gtext->SetPosition(wMid);

        // 文字方向：X 軸對齊尺寸線方向
        gp_Vec dimVec(toWorld(dA), toWorld(dB));
        if (dimVec.Magnitude() > Precision::Confusion()) {
            dimVec.Normalize();
            gp_Vec zAxis(0, 0, 1);
            if (m_sketch && m_sketch->plane()) {
                auto* pl = m_sketch->plane();
                QVector3D nq = pl->normal();
                zAxis = gp_Vec(nq.x(), nq.y(), nq.z());
                zAxis.Normalize();
            }
            // 確保 dimVec 和 zAxis 不平行
            gp_Vec side = zAxis.Crossed(dimVec);
            if (side.Magnitude() > Precision::Confusion()) {
                gtext->SetOrientation(gp_Ax2(wMid, gp_Dir(zAxis), gp_Dir(dimVec)));
            }
        }
        gtext->SetHorizontalAlignment(Graphic3d_HTA_CENTER);
        gtext->SetVerticalAlignment(Graphic3d_VTA_CENTER);

        Handle(Graphic3d_AspectText3d) txtAsp = new Graphic3d_AspectText3d();
        txtAsp->SetColor(Quantity_Color(Quantity_NOC_RED));
        Handle(Graphic3d_Group) txtGrp = m_prs->NewGroup();
        txtGrp->SetGroupPrimitivesAspect(txtAsp);
        txtGrp->AddText(gtext);
    }

    // 置頂顯示
    m_prs->SetZLayer(Graphic3d_ZLayerId_Top);
    m_prs->SetDisplayPriority(Graphic3d_DisplayPriority_Topmost);
    m_prs->Display();
    m_context->UpdateCurrentViewer();
}

} // namespace aicad::view