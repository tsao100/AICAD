#pragma once
// src/cad/sketch/SketchAxisAIS.h
//
// 草圖平面參考幾何 AIS 物件
//   SketchAxisAIS  — X 軸或 Y 軸（可選取的直線段）
//   SketchOriginAIS — 原點（可選取的點）
//
// 改用 AIS_Shape 為基底（包裝 TopoDS_Edge / TopoDS_Vertex），
// 這是 OCCT 久經驗證、保證選取行為正確的標準路徑，
// 避開純 AIS_InteractiveObject 自訂子類別可能遇到的選取啟用陷阱。
//
// UUID 格式（登記於 CadView::aisToGeomUuid）：
//   X 軸  : "sketch_xaxis:<sketchUuid>"
//   Y 軸  : "sketch_yaxis:<sketchUuid>"
//   原點  : "sketch_origin:<sketchUuid>"

#include <AIS_Shape.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pnt.hxx>
#include <QString>

namespace aicad::cad {

// ── X / Y 軸 ─────────────────────────────────────────────────────────────

class SketchAxisAIS : public AIS_Shape {
public:
    enum class AxisType { X, Y };

    /// @param plane     草圖座標系
    /// @param halfLen   顯示半長（mm，通常與草圖大小相符）
    /// @param uuid      此物件的 UUID（"sketch_xaxis:…" 或 "sketch_yaxis:…"）
    SketchAxisAIS(const gp_Ax3& plane, double halfLen, AxisType axis, const QString& uuid);

    const QString& uuid() const { return m_uuid; }

private:
    AxisType m_axis;
    QString  m_uuid;
};

// ── 原點 ─────────────────────────────────────────────────────────────────

class SketchOriginAIS : public AIS_Shape {
public:
    /// @param plane   草圖座標系（Location = 原點）
    /// @param size    顯示用標記大小（mm，未使用於選取容差，僅供未來擴充）
    /// @param uuid    "sketch_origin:…"
    SketchOriginAIS(const gp_Ax3& plane, double size, const QString& uuid);

    const QString& uuid() const { return m_uuid; }

private:
    QString m_uuid;
};

} // namespace aicad::cad
