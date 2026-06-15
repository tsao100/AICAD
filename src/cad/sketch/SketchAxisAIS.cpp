// src/cad/sketch/SketchAxisAIS.cpp
#include "SketchAxisAIS.h"

#include <Graphic3d_ArrayOfPolylines.hxx>
#include <Graphic3d_AspectLine3d.hxx>
#include <Graphic3d_Group.hxx>
#include <Quantity_Color.hxx>
#include <Select3D_SensitiveSegment.hxx>
#include <Select3D_SensitivePoint.hxx>
#include <SelectMgr_EntityOwner.hxx>

namespace aicad::cad {

// ══════════════════════════════════════════════════════════════════════════
// SketchAxisAIS
// ══════════════════════════════════════════════════════════════════════════

IMPLEMENT_STANDARD_RTTIEXT(SketchAxisAIS, AIS_InteractiveObject)

SketchAxisAIS::SketchAxisAIS(const gp_Ax3&  plane,
                              double         halfLen,
                              AxisType       axis,
                              const QString& uuid)
    : m_plane(plane)
    , m_halfLen(halfLen)
    , m_axis(axis)
    , m_uuid(uuid)
{
    // 計算軸線的世界座標端點
    gp_Pnt  org  = plane.Location();
    gp_Dir  dir  = (axis == AxisType::X) ? plane.XDirection()
                                          : plane.YDirection();

    m_p0 = gp_Pnt(org.X() - halfLen * dir.X(),
                  org.Y() - halfLen * dir.Y(),
                  org.Z() - halfLen * dir.Z());
    m_p1 = gp_Pnt(org.X() + halfLen * dir.X(),
                  org.Y() + halfLen * dir.Y(),
                  org.Z() + halfLen * dir.Z());

    SetInfiniteState(Standard_False);
}

void SketchAxisAIS::Compute(const Handle(PrsMgr_PresentationManager)& /*pm*/,
                             const Handle(Prs3d_Presentation)&         prs,
                             const Standard_Integer                    /*mode*/)
{
    prs->Clear();

    // X 軸：紅色，Y 軸：綠色（參考 CAD 慣例，帶透明感的細虛線）
    Quantity_Color color = (m_axis == AxisType::X)
        ? Quantity_Color(0.8, 0.2, 0.2, Quantity_TOC_sRGB)   // 紅
        : Quantity_Color(0.2, 0.75, 0.2, Quantity_TOC_sRGB);  // 綠

    Handle(Graphic3d_Group) grp = prs->NewGroup();
    Handle(Graphic3d_AspectLine3d) aspect =
        new Graphic3d_AspectLine3d(color, Aspect_TOL_DOTDASH, 1.0f);
    grp->SetGroupPrimitivesAspect(aspect);

    Handle(Graphic3d_ArrayOfPolylines) seg =
        new Graphic3d_ArrayOfPolylines(2, 1);
    seg->AddBound(2);
    seg->AddVertex(m_p0);
    seg->AddVertex(m_p1);
    grp->AddPrimitiveArray(seg);

    // 正端畫箭頭小標記（短 tick）
    gp_Dir  dir  = (m_axis == AxisType::X) ? m_plane.XDirection()
                                            : m_plane.YDirection();
    gp_Dir  perp = (m_axis == AxisType::X) ? m_plane.YDirection()
                                            : m_plane.XDirection();
    double  tk   = m_halfLen * 0.04;   // tick 大小 = 軸長 4%

    Handle(Graphic3d_AspectLine3d) solidAsp =
        new Graphic3d_AspectLine3d(color, Aspect_TOL_SOLID, 1.5f);
    Handle(Graphic3d_Group) grp2 = prs->NewGroup();
    grp2->SetGroupPrimitivesAspect(solidAsp);

    // 箭頭：兩斜線
    Handle(Graphic3d_ArrayOfPolylines) arr =
        new Graphic3d_ArrayOfPolylines(4, 2);
    arr->AddBound(2);
    arr->AddVertex(gp_Pnt(m_p1.X() - tk * dir.X() + tk * perp.X(),
                           m_p1.Y() - tk * dir.Y() + tk * perp.Y(),
                           m_p1.Z() - tk * dir.Z() + tk * perp.Z()));
    arr->AddVertex(m_p1);
    arr->AddBound(2);
    arr->AddVertex(gp_Pnt(m_p1.X() - tk * dir.X() - tk * perp.X(),
                           m_p1.Y() - tk * dir.Y() - tk * perp.Y(),
                           m_p1.Z() - tk * dir.Z() - tk * perp.Z()));
    arr->AddVertex(m_p1);
    grp2->AddPrimitiveArray(arr);
}

void SketchAxisAIS::ComputeSelection(const Handle(SelectMgr_Selection)& sel,
                                      const Standard_Integer             /*mode*/)
{
    // 可選取的線段（Sensitivity 4，略高於一般曲線的 3）
    Handle(SelectMgr_EntityOwner) owner = new SelectMgr_EntityOwner(this, 4);
    Handle(Select3D_SensitiveSegment) seg =
        new Select3D_SensitiveSegment(owner, m_p0, m_p1);
    sel->Add(seg);
}

// ══════════════════════════════════════════════════════════════════════════
// SketchOriginAIS
// ══════════════════════════════════════════════════════════════════════════

IMPLEMENT_STANDARD_RTTIEXT(SketchOriginAIS, AIS_InteractiveObject)

SketchOriginAIS::SketchOriginAIS(const gp_Ax3&  plane,
                                  double         size,
                                  const QString& uuid)
    : m_plane(plane)
    , m_size(size)
    , m_uuid(uuid)
    , m_origin3D(plane.Location())
{
    SetInfiniteState(Standard_False);
}

void SketchOriginAIS::Compute(const Handle(PrsMgr_PresentationManager)& /*pm*/,
                               const Handle(Prs3d_Presentation)&         prs,
                               const Standard_Integer                    /*mode*/)
{
    prs->Clear();

    // 原點：白色十字 + 外圍小圓（近似用八邊形）
    Quantity_Color color(0.9, 0.9, 0.9, Quantity_TOC_sRGB);

    Handle(Graphic3d_Group) grp = prs->NewGroup();
    Handle(Graphic3d_AspectLine3d) aspect =
        new Graphic3d_AspectLine3d(color, Aspect_TOL_SOLID, 1.5f);
    grp->SetGroupPrimitivesAspect(aspect);

    gp_Dir xd = m_plane.XDirection();
    gp_Dir yd = m_plane.YDirection();
    gp_Pnt p  = m_origin3D;
    double s  = m_size;

    auto offset = [&](double dx, double dy) -> gp_Pnt {
        return gp_Pnt(p.X() + dx * xd.X() + dy * yd.X(),
                      p.Y() + dx * xd.Y() + dy * yd.Y(),
                      p.Z() + dx * xd.Z() + dy * yd.Z());
    };

    // 十字
    Handle(Graphic3d_ArrayOfPolylines) cross =
        new Graphic3d_ArrayOfPolylines(4, 2);
    cross->AddBound(2);
    cross->AddVertex(offset(-s, 0.0));
    cross->AddVertex(offset( s, 0.0));
    cross->AddBound(2);
    cross->AddVertex(offset(0.0, -s));
    cross->AddVertex(offset(0.0,  s));
    grp->AddPrimitiveArray(cross);

    // 小圓（八邊形近似，半徑 = s * 0.5）
    const int N = 8;
    double r = s * 0.5;
    Handle(Graphic3d_ArrayOfPolylines) circ =
        new Graphic3d_ArrayOfPolylines(N + 1, 1);
    circ->AddBound(N + 1);
    for (int i = 0; i <= N; ++i) {
        double ang = 2.0 * M_PI * i / N;
        circ->AddVertex(offset(r * std::cos(ang), r * std::sin(ang)));
    }
    grp->AddPrimitiveArray(circ);
}

void SketchOriginAIS::ComputeSelection(const Handle(SelectMgr_Selection)& sel,
                                        const Standard_Integer             /*mode*/)
{
    // 用 SensitivePoint 選取原點（Sensitivity 7，高於軸線和曲線，優先被選到）
    Handle(SelectMgr_EntityOwner) owner = new SelectMgr_EntityOwner(this, 7);
    Handle(Select3D_SensitivePoint) pt  =
        new Select3D_SensitivePoint(owner, m_origin3D);
    sel->Add(pt);
}

} // namespace aicad::cad
