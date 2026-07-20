// src/cad/sketch/SketchPointAIS.cpp
#include "SketchPointAIS.h"
#include "ConstraintSolver.h"
#include <Graphic3d_ArrayOfPolylines.hxx>
#include <Graphic3d_ArrayOfPoints.hxx>
#include <Graphic3d_AspectLine3d.hxx>
#include <Graphic3d_AspectMarker3d.hxx>
#include <Graphic3d_Group.hxx>
#include <Quantity_Color.hxx>
#include <Select3D_SensitivePoint.hxx>
#include <SelectMgr_EntityOwner.hxx>
#include <gp_Vec.hxx>

namespace aicad::cad {

IMPLEMENT_STANDARD_RTTIEXT(SketchPointAIS, AIS_InteractiveObject)

SketchPointAIS::SketchPointAIS(const SketchPoint* pt, const gp_Ax3& sketchPlane)
    : m_uuid(pt->uuid)
    , m_origin(pt->origin)
    , m_plane(sketchPlane)
    , m_pos3D(toWorld(pt->pos))
    , m_status(SolveStatus::UnderConstrained)
{
    SetInfiniteState(Standard_False);
}

void SketchPointAIS::updatePosition(const QVector2D& newPos)
{
    m_pos3D = toWorld(newPos);
    SetToUpdate();
}

void SketchPointAIS::updateSolveStatus(SolveStatus status)
{
    if (m_status == status) return;
    m_status = status;
    SetToUpdate();
}

gp_Pnt SketchPointAIS::toWorld(const QVector2D& pos2D) const
{
    // 草圖平面 2D → 世界 3D
    // plane: XDirection = U axis, YDirection = V axis, Location = origin
    gp_Pnt origin = m_plane.Location();
    gp_Dir xDir   = m_plane.XDirection();
    gp_Dir yDir   = m_plane.YDirection();
    return gp_Pnt(origin.X() + pos2D.x() * xDir.X() + pos2D.y() * yDir.X(),
                  origin.Y() + pos2D.x() * xDir.Y() + pos2D.y() * yDir.Y(),
                  origin.Z() + pos2D.x() * xDir.Z() + pos2D.y() * yDir.Z());
}

void SketchPointAIS::Compute(const Handle(PrsMgr_PresentationManager)& /*pm*/,
                               const Handle(Prs3d_Presentation)& prs,
                               const Standard_Integer /*mode*/)
{
    prs->Clear();

    // Endpoint：固定為紅色實心圓點，螢幕空間固定像素大小，不受縮放與約束狀態影響
    if (m_origin == SketchPoint::Origin::Endpoint) {
        drawEndpointMarker(prs);
        return;
    }

    // ── 顏色 ──
    Quantity_Color color;
    if (m_origin == SketchPoint::Origin::Explicit) {
        color = Quantity_Color(Quantity_NOC_YELLOW);
    } else {
        switch (m_status) {
        case SolveStatus::FullyConstrained: color = Quantity_Color(Quantity_NOC_GREEN3); break;
        case SolveStatus::OverConstrained:  color = Quantity_Color(Quantity_NOC_RED);    break;
        default:                            color = Quantity_Color(Quantity_NOC_CYAN1);  break;
        }
    }

    // ── 大小（半邊長，mm） ──
    double halfSize;
    switch (m_origin) {
    case SketchPoint::Origin::Center:   halfSize = 1.5; break;
    case SketchPoint::Origin::Explicit: halfSize = 1.2; break;
    default:                            halfSize = 1.0; break;
    }

    drawSymbol(prs, color, halfSize);
}

void SketchPointAIS::drawEndpointMarker(const Handle(Prs3d_Presentation)& prs) const
{
    // Aspect_TOM_BALL：OCCT 內建的實心圓點 marker，大小以螢幕像素為單位
    // （由 Graphic3d_AspectMarker3d 的 scale 參數決定），因此不會隨視圖縮放而改變大小。
    Handle(Graphic3d_AspectMarker3d) markerAspect =
        new Graphic3d_AspectMarker3d(Aspect_TOM_BALL,
                                      Quantity_Color(Quantity_NOC_RED),
                                      kEndpointMarkerScale);

    Handle(Graphic3d_ArrayOfPoints) arr = new Graphic3d_ArrayOfPoints(1);
    arr->AddVertex(m_pos3D);

    Handle(Graphic3d_Group) grp = prs->NewGroup();
    grp->SetGroupPrimitivesAspect(markerAspect);
    grp->AddPrimitiveArray(arr);
}

void SketchPointAIS::drawSymbol(const Handle(Prs3d_Presentation)& prs,
                                const Quantity_Color& color,
                                double halfSize) const
{
    Handle(Graphic3d_Group) grp = prs->NewGroup();
    Handle(Graphic3d_AspectLine3d) aspect =
        new Graphic3d_AspectLine3d(color, Aspect_TOL_SOLID, 1.5f);
    grp->SetGroupPrimitivesAspect(aspect);

    gp_Pnt p = m_pos3D;
    gp_Dir xDir = m_plane.XDirection();
    gp_Dir yDir = m_plane.YDirection();

    auto offset = [&](double dx, double dy) -> gp_Pnt {
        return gp_Pnt(p.X() + dx * xDir.X() + dy * yDir.X(),
                      p.Y() + dx * xDir.Y() + dy * yDir.Y(),
                      p.Z() + dx * xDir.Z() + dy * yDir.Z());
    };

    if (m_origin == SketchPoint::Origin::Center) {
        // 加號 +
        Handle(Graphic3d_ArrayOfPolylines) seg = new Graphic3d_ArrayOfPolylines(4, 2);
        seg->AddBound(2);                          // ← 必須在 vertex 之前
        seg->AddVertex(offset(-halfSize, 0.0));
        seg->AddVertex(offset( halfSize, 0.0));
        seg->AddBound(2);                          // ← 必須在 vertex 之前
        seg->AddVertex(offset(0.0, -halfSize));
        seg->AddVertex(offset(0.0,  halfSize));
        grp->AddPrimitiveArray(seg);
    } else if (m_origin == SketchPoint::Origin::Explicit) {
        // 菱形 ◇
        Handle(Graphic3d_ArrayOfPolylines) seg = new Graphic3d_ArrayOfPolylines(5, 1);
        seg->AddBound(5);                          // ← 必須在 vertex 之前
        seg->AddVertex(offset( 0.0,       halfSize));
        seg->AddVertex(offset( halfSize,  0.0));
        seg->AddVertex(offset( 0.0,      -halfSize));
        seg->AddVertex(offset(-halfSize,  0.0));
        seg->AddVertex(offset( 0.0,       halfSize));
        grp->AddPrimitiveArray(seg);
    } else {
        // 正方形 □（Intersection，交點；Endpoint 已於 Compute() 提前分流至 drawEndpointMarker）
        Handle(Graphic3d_ArrayOfPolylines) seg = new Graphic3d_ArrayOfPolylines(5, 1);
        seg->AddBound(5);                          // ← 必須在 vertex 之前
        seg->AddVertex(offset(-halfSize, -halfSize));
        seg->AddVertex(offset( halfSize, -halfSize));
        seg->AddVertex(offset( halfSize,  halfSize));
        seg->AddVertex(offset(-halfSize,  halfSize));
        seg->AddVertex(offset(-halfSize, -halfSize));
        grp->AddPrimitiveArray(seg);
    }
}

void SketchPointAIS::ComputeSelection(const Handle(SelectMgr_Selection)& sel,
                                       const Standard_Integer /*mode*/)
{
    // Sensitivity 6 > 曲線典型 3，確保點優先被 snap 到
    Handle(SelectMgr_EntityOwner) owner = new SelectMgr_EntityOwner(this, 6);
    Handle(Select3D_SensitivePoint) pt  = new Select3D_SensitivePoint(owner, m_pos3D);
    sel->Add(pt);
}

} // namespace aicad::cad
