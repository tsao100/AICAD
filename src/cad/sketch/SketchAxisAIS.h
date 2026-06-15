#pragma once
// src/cad/sketch/SketchAxisAIS.h
//
// 草圖平面參考幾何 AIS 物件
//   SketchAxisAIS  — X 軸或 Y 軸（可選取的直線段）
//   SketchOriginAIS — 原點（可選取的十字符號）
//
// UUID 格式（登記於 CadView::aisToGeomUuid）：
//   X 軸  : "sketch_xaxis:<sketchUuid>"
//   Y 軸  : "sketch_yaxis:<sketchUuid>"
//   原點  : "sketch_origin:<sketchUuid>"
//
// 這些 UUID 由 ConstraintCommands / SketchPanel 識別，
// 透過 constrainCoincident(pt, originRef) / constrainHorizontal(line) 等套用。

#include <AIS_InteractiveObject.hxx>
#include <PrsMgr_PresentationManager.hxx>
#include <Prs3d_Presentation.hxx>
#include <SelectMgr_Selection.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pnt.hxx>
#include <QString>

namespace aicad::cad {

// ── X / Y 軸 ─────────────────────────────────────────────────────────────

DEFINE_STANDARD_HANDLE(SketchAxisAIS, AIS_InteractiveObject)

class SketchAxisAIS : public AIS_InteractiveObject {
    DEFINE_STANDARD_RTTIEXT(SketchAxisAIS, AIS_InteractiveObject)
public:
    enum class AxisType { X, Y };

    /// @param plane     草圖座標系
    /// @param halfLen   顯示半長（mm，通常與草圖大小相符）
    /// @param uuid      此物件的 UUID（"sketch_xaxis:…" 或 "sketch_yaxis:…"）
    SketchAxisAIS(const gp_Ax3& plane, double halfLen, AxisType axis, const QString& uuid);

    const QString& uuid() const { return m_uuid; }

private:
    void Compute(const Handle(PrsMgr_PresentationManager)&,
                 const Handle(Prs3d_Presentation)& prs,
                 const Standard_Integer mode) override;

    void ComputeSelection(const Handle(SelectMgr_Selection)& sel,
                          const Standard_Integer mode) override;

    gp_Ax3   m_plane;
    double   m_halfLen;
    AxisType m_axis;
    QString  m_uuid;
    gp_Pnt   m_p0;   ///< 軸線起點（-halfLen）
    gp_Pnt   m_p1;   ///< 軸線終點（+halfLen）
};

// ── 原點 ─────────────────────────────────────────────────────────────────

DEFINE_STANDARD_HANDLE(SketchOriginAIS, AIS_InteractiveObject)

class SketchOriginAIS : public AIS_InteractiveObject {
    DEFINE_STANDARD_RTTIEXT(SketchOriginAIS, AIS_InteractiveObject)
public:
    /// @param plane   草圖座標系（Location = 原點）
    /// @param size    十字半長（mm）
    /// @param uuid    "sketch_origin:…"
    SketchOriginAIS(const gp_Ax3& plane, double size, const QString& uuid);

    const QString& uuid() const { return m_uuid; }

private:
    void Compute(const Handle(PrsMgr_PresentationManager)&,
                 const Handle(Prs3d_Presentation)& prs,
                 const Standard_Integer mode) override;

    void ComputeSelection(const Handle(SelectMgr_Selection)& sel,
                          const Standard_Integer mode) override;

    gp_Ax3  m_plane;
    double  m_size;
    QString m_uuid;
    gp_Pnt  m_origin3D;
};

} // namespace aicad::cad
