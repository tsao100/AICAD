/**
 * @file OSnapDetector.cpp
 * @brief Object Snap 偵測核心引擎實作
 */

#include "OSnapDetector.h"
#include "../cad/Plane.h"   // aicad::cad::Plane

// OCCT topology
#include <TopoDS.hxx>
#include <TopoDS_Iterator.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <BRep_Tool.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>

// OCCT geometry
#include <Geom_Curve.hxx>
#include <Geom_Circle.hxx>
#include <Geom_Ellipse.hxx>
#include <Geom_Line.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <GeomAdaptor_Curve.hxx>
#include <GeomAPI_ProjectPointOnCurve.hxx>

// OCCT math
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>
#include <gp_Lin.hxx>
#include <gp_Pln.hxx>
#include <gp_Circ.hxx>
#include <ElCLib.hxx>
#include <IntAna_IntConicQuad.hxx>
#include <IntAna_Quadric.hxx>
#include <ElCLib.hxx>
#include <ElSLib.hxx>

// AIS / V3d
#include <AIS_InteractiveContext.hxx>
#include <AIS_InteractiveObject.hxx>
#include <AIS_Shape.hxx>
#include <V3d_View.hxx>
#include <SelectMgr_SelectionManager.hxx>

#include <algorithm>
#include <cmath>
#include <QDebug>

namespace aicad {
namespace osnap {

// ──────────────────────────────────────────────────────────────────────────────
OSnapDetector::OSnapDetector() = default;

// ──────────────────────────────────────────────────────────────────────────────
//  主偵測入口
// ──────────────────────────────────────────────────────────────────────────────
std::optional<SnapCandidate>
OSnapDetector::detect(const Handle(AIS_InteractiveContext)& context,
                      const Handle(V3d_View)& view,
                      int mouseX, int mouseY)
{
    m_lastCandidates.clear();

    if (context.IsNull() || view.IsNull())
        return std::nullopt;

    // ── Step 1: 滑鼠射線投影至草圖平面（或預設 XY 平面）─────────────────────
    gp_Pnt mouseWorldPt;
    if (m_activePlane) {
        // ✅ 射線與草圖平面求交（與 GripEventFilter 一致）
        double px, py, pz, dx, dy, dz;
        view->ProjReferenceAxe(mouseX, mouseY, px, py, pz, dx, dy, dz);
        gp_Pnt  rayOrigin(px, py, pz);
        gp_Dir  rayDir(dx, dy, dz);

        QVector3D qo = m_activePlane->origin();
        QVector3D qn = m_activePlane->normal();
        gp_Pnt  planeOrigin(qo.x(), qo.y(), qo.z());
        gp_Dir  planeNormal(qn.x(), qn.y(), qn.z());

        gp_Vec toPlane(rayOrigin, planeOrigin);
        double denom = rayDir.XYZ().Dot(planeNormal.XYZ());
        if (std::abs(denom) > 1e-10) {
            double t = toPlane.XYZ().Dot(planeNormal.XYZ()) / denom;
            mouseWorldPt = gp_Pnt(rayOrigin.X() + rayDir.X() * t,
                                  rayOrigin.Y() + rayDir.Y() * t,
                                  rayOrigin.Z() + rayDir.Z() * t);
        } else {
            double wx, wy, wz;
            view->Convert(mouseX, mouseY, wx, wy, wz);
            mouseWorldPt = gp_Pnt(wx, wy, wz);
        }
    } else {
        double wx, wy, wz;
        view->Convert(mouseX, mouseY, wx, wy, wz);
        mouseWorldPt = gp_Pnt(wx, wy, wz);
    }

    // ── Step 2: 收集附近的候選 Shape ──────────────────────────────────────────
    QVector<TopoDS_Shape>                   shapes;
    QVector<Handle(AIS_InteractiveObject)>  aisObjects;
    collectCandidateShapes(context, view, mouseX, mouseY, shapes, aisObjects);

    if (shapes.isEmpty() && !m_settings.gridSnapEnabled)
        return std::nullopt;

    // ── Step 3: 對每個 Shape 執行各類型偵測 ───────────────────────────────────
    QVector<SnapCandidate> candidates;
    candidates.reserve(m_settings.maxCandidates);

    for (int i = 0; i < shapes.size(); ++i) {
        detectOnShape(view, shapes[i], aisObjects[i],
                      mouseWorldPt, mouseX, mouseY, candidates);
    }

    // Intersection 需要跨 shape 檢測
    if (m_settings.enabledTypes.testFlag(SnapType::Intersection) && shapes.size() >= 2)
        detectIntersections(shapes, aisObjects, view, mouseWorldPt, mouseX, mouseY, candidates);

    // Grid snap（不依賴 shape）
    if (m_settings.gridSnapEnabled &&
        m_settings.enabledTypes.testFlag(SnapType::Grid)) {
        detectGridPoint(view, mouseWorldPt, candidates);
    }

    // ── Step 4: 過濾掉超過 pickRadius 的候選 ──────────────────────────────────
    // ✅ 修正：鎖定用 magnetRadius，收集用 pickPixelRadius
    candidates.erase(
        std::remove_if(candidates.begin(), candidates.end(),
                       [this](const SnapCandidate& c) {
                           return c.screenDist > m_settings.magnetRadius;  // 磁吸半徑
                       }),
        candidates.end());

    if (candidates.isEmpty())
        return std::nullopt;

    // ── Step 5: 排序（優先順序 + 螢幕距離） ────────────────────────────────────
    std::sort(candidates.begin(), candidates.end());

    // ✅ 截斷到 maxCandidates（排序後保留最佳候選）
    if (candidates.size() > m_settings.maxCandidates)
        candidates.resize(m_settings.maxCandidates);

    m_lastCandidates = candidates;
    return candidates.first();
}

// ──────────────────────────────────────────────────────────────────────────────
//  排除清單管理
// ──────────────────────────────────────────────────────────────────────────────
void OSnapDetector::addExcludedObject(const Handle(AIS_InteractiveObject)& obj)
{
    if (obj.IsNull()) return;

    // 避免重複加入：用指標值比對（OCCT Handle 的 get() 回傳底層原始指標）
    for (const Handle(AIS_InteractiveObject)& existing : m_excludedObjects) {
        if (existing.get() == obj.get()) return;
    }

    m_excludedObjects.append(obj);
    qDebug() << "[OSnapDetector] Excluded object added, total excluded:"
             << m_excludedObjects.size();
}

void OSnapDetector::removeExcludedObject(const Handle(AIS_InteractiveObject)& obj)
{
    if (obj.IsNull()) return;

    const int sizeBefore = m_excludedObjects.size();
    m_excludedObjects.erase(
        std::remove_if(m_excludedObjects.begin(), m_excludedObjects.end(),
                       [&obj](const Handle(AIS_InteractiveObject)& existing) {
                           return existing.get() == obj.get();
                       }),
        m_excludedObjects.end());

    if (m_excludedObjects.size() < sizeBefore) {
        qDebug() << "[OSnapDetector] Excluded object removed, total excluded:"
                 << m_excludedObjects.size();
    }
}

// ──────────────────────────────────────────────────────────────────────────────
//  收集附近的 AIS Shape
//  策略：
//   1. 先用 AIS_InteractiveContext::MoveTo 讓 OCCT 做初步 HLR 篩選
//   2. 遍歷所有顯示中的 AIS_Shape，取其 TopoDS_Shape
//   3. 快速篩選：bounding box 中心距螢幕 < pickRadius * 5
// ──────────────────────────────────────────────────────────────────────────────
void OSnapDetector::collectCandidateShapes(
    const Handle(AIS_InteractiveContext)& context,
    const Handle(V3d_View)& view,
    int mouseX, int mouseY,
    QVector<TopoDS_Shape>& shapes,
    QVector<Handle(AIS_InteractiveObject)>& aisObjects)
{
    const double pickRadius = m_settings.pickPixelRadius * 5.0;  // 擴大收集範圍

    AIS_ListOfInteractive displayed;
    context->DisplayedObjects(displayed);

    for (AIS_ListIteratorOfListOfInteractive it(displayed); it.More(); it.Next()) {
        Handle(AIS_InteractiveObject) obj = it.Value();
        if (obj.IsNull()) continue;

        // ✅ 排除黑名單物件（參考平面、ViewCube 等）
        bool excluded = false;
        for (const auto& excl : m_excludedObjects) {
            if (obj == excl) { excluded = true; break; }
        }
        if (excluded) continue;

        // 只處理 AIS_Shape
        Handle(AIS_Shape) aisShape = Handle(AIS_Shape)::DownCast(obj);
        if (aisShape.IsNull()) continue;

        const TopoDS_Shape& shape = aisShape->Shape();
        if (shape.IsNull()) continue;

        // 快速篩選：取 shape 中任意頂點檢查螢幕距離
        bool inRange = false;
        // ✅ 修正：對每條邊取多點採樣，任一點在範圍內即納入
        TopExp_Explorer edgeExp(shape, TopAbs_EDGE);
        for (; edgeExp.More() && !inRange; edgeExp.Next()) {
            TopoDS_Edge edge = TopoDS::Edge(edgeExp.Current());
            double first, last;
            Handle(Geom_Curve) curve = BRep_Tool::Curve(edge, first, last);
            if (curve.IsNull()) continue;

            // 採樣 5 個點（兩端點 + 3 個內部點）
            for (int k = 0; k <= 4 && !inRange; ++k) {
                double param = first + (last - first) * k / 4.0;
                gp_Pnt pt = curve->Value(param);
                double dist = screenDistance(view, pt, mouseX, mouseY);
                if (dist < pickRadius * 3.0) inRange = true;
            }
        }

        // 若無邊，退而使用頂點
        if (!inRange) {
            TopExp_Explorer vertExp(shape, TopAbs_VERTEX);
            for (; vertExp.More() && !inRange; vertExp.Next()) {
                gp_Pnt pt = BRep_Tool::Pnt(TopoDS::Vertex(vertExp.Current()));
                if (screenDistance(view, pt, mouseX, mouseY) < pickRadius * 3.0)
                    inRange = true;
            }
        }

        if (inRange) {
            shapes.append(shape);
            aisObjects.append(obj);
        }
    }
}

// ──────────────────────────────────────────────────────────────────────────────
//  對單一 Shape 執行各類型偵測
// ──────────────────────────────────────────────────────────────────────────────
void OSnapDetector::detectOnShape(
    const Handle(V3d_View)& view,
    const TopoDS_Shape& shape,
    const Handle(AIS_InteractiveObject)& aisObj,
    const gp_Pnt& mouseWorldPt,
    int mouseX, int mouseY,
    QVector<SnapCandidate>& candidates)
{
    const SnapTypes& enabled = m_settings.enabledTypes;

    if (enabled.testFlag(SnapType::Endpoint) ||
        enabled.testFlag(SnapType::Node)) {
        detectEndpoints(shape, aisObj, mouseWorldPt, view, mouseX, mouseY, candidates);
    }
    if (enabled.testFlag(SnapType::Midpoint)) {
        detectMidpoints(shape, aisObj, mouseWorldPt, view, mouseX, mouseY, candidates);
    }
    if (enabled.testFlag(SnapType::Center)) {
        detectCenters(shape, aisObj, mouseWorldPt, view, mouseX, mouseY, candidates);
    }
    if (enabled.testFlag(SnapType::Quadrant)) {
        detectQuadrants(shape, aisObj, mouseWorldPt, view, mouseX, mouseY, candidates);
    }
    if (enabled.testFlag(SnapType::Perpendicular) && m_hasLastPoint) {
        detectPerpendicular(shape, aisObj, mouseWorldPt, view, mouseX, mouseY, candidates);
    }
    if (enabled.testFlag(SnapType::Tangent) && m_hasLastPoint) {
        detectTangent(shape, aisObj, mouseWorldPt, view, mouseX, mouseY, candidates);
    }
    if (enabled.testFlag(SnapType::Nearest)) {
        detectNearest(shape, aisObj, mouseWorldPt, view, mouseX, mouseY, candidates);
    }
    if (enabled.testFlag(SnapType::Extension))
        detectExtension(shape, aisObj, mouseWorldPt, view, mouseX, mouseY, candidates);
}

// ──────────────────────────────────────────────────────────────────────────────
//  Endpoint：遍歷所有 TopoDS_Vertex
// ──────────────────────────────────────────────────────────────────────────────
void OSnapDetector::detectEndpoints(
    const TopoDS_Shape& shape,
    const Handle(AIS_InteractiveObject)& aisObj,
    const gp_Pnt& /*mousePt*/,
    const Handle(V3d_View)& view,
    int mouseX, int mouseY,
    QVector<SnapCandidate>& out)
{
    // 使用 TopExp::MapShapes 避免重複頂點
    TopTools_IndexedMapOfShape vertMap;
    TopExp::MapShapes(shape, TopAbs_VERTEX, vertMap);

    for (Standard_Integer i = 1; i <= vertMap.Extent(); ++i) {
        TopoDS_Vertex v = TopoDS::Vertex(vertMap(i));
        gp_Pnt pt = BRep_Tool::Pnt(v);

        SnapCandidate c;
        c.type         = SnapType::Endpoint;
        c.worldPoint   = pt;
        c.sourceShape  = shape;
        c.sourceVertex = v;
        c.sourceAIS    = aisObj;
        c.screenDist   = screenDistance(view, pt, mouseX, mouseY);
        c.isValid      = true;

        // 使用 OCCT convert 取得精確螢幕距離
        double sx, sy;
        if (worldToScreen(view, pt, sx, sy)) {
            // 實際滑鼠座標從外部傳入，這裡存世界距離
            c.worldDist  = pt.Distance(gp_Pnt(0,0,0));  // 暫存
            c.isValid    = true;
        }

        out.append(c);
    }
}

// ──────────────────────────────────────────────────────────────────────────────
//  Midpoint：每條邊取參數中點
// ──────────────────────────────────────────────────────────────────────────────
void OSnapDetector::detectMidpoints(
    const TopoDS_Shape& shape,
    const Handle(AIS_InteractiveObject)& aisObj,
    const gp_Pnt& /*mousePt*/,
    const Handle(V3d_View)& view,
    int mouseX, int mouseY,
    QVector<SnapCandidate>& out)
{
    TopExp_Explorer edgeExp(shape, TopAbs_EDGE);
    for (; edgeExp.More(); edgeExp.Next()) {
        TopoDS_Edge edge = TopoDS::Edge(edgeExp.Current());

        double first, last;
        Handle(Geom_Curve) curve = BRep_Tool::Curve(edge, first, last);
        if (curve.IsNull()) continue;

        double midParam = (first + last) * 0.5;
        gp_Pnt midPt   = curve->Value(midParam);

        SnapCandidate c;
        c.type        = SnapType::Midpoint;
        c.worldPoint  = midPt;
        c.sourceShape = shape;
        c.sourceEdge  = edge;
        c.sourceAIS   = aisObj;
        c.paramOnEdge = 0.5;
        c.screenDist   = screenDistance(view, midPt, mouseX, mouseY);
        c.isValid     = true;
        out.append(c);
    }
}

// ──────────────────────────────────────────────────────────────────────────────
//  Center：辨識 Geom_Circle / Geom_Ellipse → 取圓心
// ──────────────────────────────────────────────────────────────────────────────
void OSnapDetector::detectCenters(
    const TopoDS_Shape& shape,
    const Handle(AIS_InteractiveObject)& aisObj,
    const gp_Pnt& /*mousePt*/,
    const Handle(V3d_View)& view,
    int mouseX, int mouseY,
    QVector<SnapCandidate>& out)
{
    TopExp_Explorer edgeExp(shape, TopAbs_EDGE);
    for (; edgeExp.More(); edgeExp.Next()) {
        TopoDS_Edge edge = TopoDS::Edge(edgeExp.Current());

        double first, last;
        Handle(Geom_Curve) baseCurve = BRep_Tool::Curve(edge, first, last);
        if (baseCurve.IsNull()) continue;

        // 解包 TrimmedCurve
        Handle(Geom_Curve) curve = baseCurve;
        Handle(Geom_TrimmedCurve) trimmed =
            Handle(Geom_TrimmedCurve)::DownCast(baseCurve);
        if (!trimmed.IsNull()) curve = trimmed->BasisCurve();

        gp_Pnt center;
        bool found = false;

        Handle(Geom_Circle) circle = Handle(Geom_Circle)::DownCast(curve);
        if (!circle.IsNull()) {
            center = circle->Location();
            found  = true;
        }

        Handle(Geom_Ellipse) ellipse = Handle(Geom_Ellipse)::DownCast(curve);
        if (!ellipse.IsNull()) {
            center = ellipse->Location();
            found  = true;
        }

        if (found) {
            SnapCandidate c;
            c.type        = SnapType::Center;
            c.worldPoint  = center;
            c.sourceShape = shape;
            c.sourceEdge  = edge;
            c.sourceAIS   = aisObj;
            c.screenDist   = screenDistance(view, center, mouseX, mouseY);
            c.isValid     = true;
            out.append(c);
        }
    }
}

// ──────────────────────────────────────────────────────────────────────────────
//  Quadrant：圓的 0°/90°/180°/270° 點
// ──────────────────────────────────────────────────────────────────────────────
void OSnapDetector::detectQuadrants(
    const TopoDS_Shape& shape,
    const Handle(AIS_InteractiveObject)& aisObj,
    const gp_Pnt& /*mousePt*/,
    const Handle(V3d_View)& view,
    int mouseX, int mouseY,
    QVector<SnapCandidate>& out)
{
    TopExp_Explorer edgeExp(shape, TopAbs_EDGE);
    for (; edgeExp.More(); edgeExp.Next()) {
        TopoDS_Edge edge = TopoDS::Edge(edgeExp.Current());

        double first, last;
        Handle(Geom_Curve) baseCurve = BRep_Tool::Curve(edge, first, last);
        if (baseCurve.IsNull()) continue;

        Handle(Geom_Curve) curve = baseCurve;
        Handle(Geom_TrimmedCurve) trimmed =
            Handle(Geom_TrimmedCurve)::DownCast(baseCurve);
        if (!trimmed.IsNull()) curve = trimmed->BasisCurve();

        Handle(Geom_Circle) circle = Handle(Geom_Circle)::DownCast(curve);
        if (circle.IsNull()) continue;

        const gp_Ax2& ax2 = circle->Position();
        double r = circle->Radius();

        // 四個四分點（用 ElCLib 確保在圓的座標系中）
        static const double angles[4] = { 0.0, M_PI*0.5, M_PI, M_PI*1.5 };
        for (double angle : angles) {
            // 檢查此角度是否在弧段範圍內
            double normAngle = ElCLib::InPeriod(angle, first, last);

            // ✅ 修正：使用寬鬆容差比較，避免浮點邊界誤判
            const double tol = 1e-7;
            if (normAngle < first - tol || normAngle > last + tol) continue;

            gp_Pnt qpt = ElCLib::Value(normAngle, circle->Circ());

            SnapCandidate c;
            c.type        = SnapType::Quadrant;
            c.worldPoint  = qpt;
            c.sourceShape = shape;
            c.sourceEdge  = edge;
            c.sourceAIS   = aisObj;
            c.paramOnEdge = normAngle;
            c.screenDist   = screenDistance(view, qpt, mouseX, mouseY);
            c.isValid     = true;
            out.append(c);
        }
    }
}

// ──────────────────────────────────────────────────────────────────────────────
//  Intersection：兩條邊的交點
//  策略：對 shape 陣列中所有 Edge 對進行 BRepExtrema_DistShapeShape，
//        若最小距離 < tolerance 則視為交點。
// ──────────────────────────────────────────────────────────────────────────────
void OSnapDetector::detectIntersections(
    const QVector<TopoDS_Shape>& shapes,
     const QVector<Handle(AIS_InteractiveObject)>& aisObjects,
     const Handle(V3d_View)& view,
     const gp_Pnt& mousePt,
     int mouseX, int mouseY,
     QVector<SnapCandidate>& out)
{
    // 收集所有邊
    QVector<TopoDS_Edge> allEdges;
    for (const TopoDS_Shape& shape : shapes) {
        TopExp_Explorer exp(shape, TopAbs_EDGE);
        for (; exp.More(); exp.Next()) {
            allEdges.append(TopoDS::Edge(exp.Current()));
        }
    }

    const double tol = 0.1;  // 模型單位交點容差

    // 兩兩比對（O(n²)，但因為 collectCandidateShapes 已限制 shape 數量，不會太慢）
    for (int i = 0; i < allEdges.size(); ++i) {
        for (int j = i + 1; j < allEdges.size(); ++j) {
            BRepExtrema_DistShapeShape dist(allEdges[i], allEdges[j]);
            if (!dist.IsDone()) continue;
            if (dist.Value() > tol) continue;

            // 取最近點的中點作為交點
            gp_Pnt pt1 = dist.PointOnShape1(1);
            gp_Pnt pt2 = dist.PointOnShape2(1);
            gp_Pnt intersect((pt1.X()+pt2.X())*0.5,
                             (pt1.Y()+pt2.Y())*0.5,
                             (pt1.Z()+pt2.Z())*0.5);

            SnapCandidate c;
            c.type       = SnapType::Intersection;
            c.worldPoint = intersect;
            c.sourceEdge = allEdges[i];
            c.screenDist = screenDistance(view, intersect, mouseX, mouseY);
            c.sourceAIS  = aisObjects[i];
            c.isValid    = true;
            out.append(c);
        }
    }
}

// ──────────────────────────────────────────────────────────────────────────────
//  Perpendicular：從上一個輸入點，對邊求垂足
// ──────────────────────────────────────────────────────────────────────────────
void OSnapDetector::detectPerpendicular(
    const TopoDS_Shape& shape,
    const Handle(AIS_InteractiveObject)& aisObj,
    const gp_Pnt& /*mousePt*/,
    const Handle(V3d_View)& view,
    int mouseX, int mouseY,
    QVector<SnapCandidate>& out)
{
    if (!m_hasLastPoint) return;

    TopExp_Explorer edgeExp(shape, TopAbs_EDGE);
    for (; edgeExp.More(); edgeExp.Next()) {
        TopoDS_Edge edge = TopoDS::Edge(edgeExp.Current());

        double first, last;
        Handle(Geom_Curve) curve = BRep_Tool::Curve(edge, first, last);
        if (curve.IsNull()) continue;

        GeomAPI_ProjectPointOnCurve proj(m_lastInputPoint, curve, first, last);
        if (proj.NbPoints() == 0) continue;

        gp_Pnt foot = proj.NearestPoint();

        SnapCandidate c;
        c.type        = SnapType::Perpendicular;
        c.worldPoint  = foot;
        c.sourceShape = shape;
        c.sourceEdge  = edge;
        c.sourceAIS   = aisObj;
        c.paramOnEdge = proj.LowerDistanceParameter();
        c.screenDist   = screenDistance(view, foot, mouseX, mouseY);
        c.isValid     = true;
        out.append(c);
    }
}

// ──────────────────────────────────────────────────────────────────────────────
//  Tangent：從上一個輸入點，對圓求切點
//  切點公式：d² = r²（d 為圓心到起點距離，切點角度由三角關係求得）
// ──────────────────────────────────────────────────────────────────────────────
void OSnapDetector::detectTangent(
    const TopoDS_Shape& shape,
    const Handle(AIS_InteractiveObject)& aisObj,
    const gp_Pnt& mousePt,
    const Handle(V3d_View)& view,
    int mouseX, int mouseY,
    QVector<SnapCandidate>& out)
{
    if (!m_hasLastPoint) return;

    TopExp_Explorer edgeExp(shape, TopAbs_EDGE);
    for (; edgeExp.More(); edgeExp.Next()) {
        TopoDS_Edge edge = TopoDS::Edge(edgeExp.Current());

        double first, last;
        Handle(Geom_Curve) baseCurve = BRep_Tool::Curve(edge, first, last);
        if (baseCurve.IsNull()) continue;

        Handle(Geom_Curve) curve = baseCurve;
        Handle(Geom_TrimmedCurve) trimmed =
            Handle(Geom_TrimmedCurve)::DownCast(baseCurve);
        if (!trimmed.IsNull()) curve = trimmed->BasisCurve();

        Handle(Geom_Circle) circle = Handle(Geom_Circle)::DownCast(curve);
        if (circle.IsNull()) continue;

        const gp_Pnt& center = circle->Location();
        double r = circle->Radius();
        gp_Vec toFrom(center, m_lastInputPoint);
        double d = toFrom.Magnitude();

        if (d < r + Precision::Confusion()) continue;  // 起點在圓內

        // 切點角度偏移
        double alpha = std::asin(r / d);
        // 從圓心到起點的方向角
        gp_Vec zAxis = circle->Axis().Direction();
        gp_Vec fromDir = toFrom.Normalized();

        // 建立圓上兩個切點
        for (double sign : {+1.0, -1.0}) {
            // 旋轉 fromDir 90° ± alpha（在圓平面內）
            // 使用 gp_Vec 旋轉（Rodrigues formula in circle plane）
            gp_Ax1 rotAxis(center, gp_Dir(zAxis));
            gp_Vec rotDir = fromDir.Reversed();
            rotDir.Rotate(rotAxis, sign * (M_PI * 0.5 - alpha));
            gp_Pnt tangPt(center.X() + rotDir.X() * r,
                          center.Y() + rotDir.Y() * r,
                          center.Z() + rotDir.Z() * r);

            // 驗證切線：(切點-圓心)·(切點-起點) ≈ 0
            gp_Vec v1(center, tangPt);
            gp_Vec v2(m_lastInputPoint, tangPt);
            if (std::abs(v1.Dot(v2)) > 0.01 * r) continue;

            SnapCandidate c;
            c.type        = SnapType::Tangent;
            c.worldPoint  = tangPt;
            c.sourceShape = shape;
            c.sourceEdge  = edge;
            c.sourceAIS   = aisObj;
            c.screenDist   = screenDistance(view, tangPt, mouseX, mouseY);
            c.isValid     = true;
            out.append(c);
        }
    }
}

// ──────────────────────────────────────────────────────────────────────────────
//  Nearest：邊上距滑鼠射線最近的點
//  使用 BRepExtrema_DistShapeShape(vertex, edge)
// ──────────────────────────────────────────────────────────────────────────────
void OSnapDetector::detectNearest(
    const TopoDS_Shape& shape,
    const Handle(AIS_InteractiveObject)& aisObj,
    const gp_Pnt& mousePt,
    const Handle(V3d_View)& view,
    int mouseX, int mouseY,
    QVector<SnapCandidate>& out)
{
    // 建立滑鼠位置的頂點 shape
    TopoDS_Vertex mouseVertex = BRepBuilderAPI_MakeVertex(mousePt);

    TopExp_Explorer edgeExp(shape, TopAbs_EDGE);
    for (; edgeExp.More(); edgeExp.Next()) {
        TopoDS_Edge edge = TopoDS::Edge(edgeExp.Current());

        BRepExtrema_DistShapeShape dist(mouseVertex, edge);
        if (!dist.IsDone()) continue;

        gp_Pnt nearPt = dist.PointOnShape2(1);

        // 避免與 Endpoint/Midpoint 重複（若接近端點，讓 Endpoint 優先）
        double first, last;
        Handle(Geom_Curve) curve = BRep_Tool::Curve(edge, first, last);
        if (!curve.IsNull()) {
            Standard_Real param = 0.0;
            dist.ParOnEdgeS2(1, param);
            double relParam = (param - first) / (last - first + 1e-12);
            // 若接近端點或中點，跳過（讓更高優先的 snap 覆蓋）
            if (relParam < 0.02 || relParam > 0.98 ||
                std::abs(relParam - 0.5) < 0.02) {
                continue;
            }
        }

        SnapCandidate c;
        c.type        = SnapType::Nearest;
        c.worldPoint  = nearPt;
        c.sourceShape = shape;
        c.sourceEdge  = edge;
        c.sourceAIS   = aisObj;
        c.worldDist   = dist.Value();
        c.screenDist = screenDistance(view, nearPt, mouseX, mouseY);
        c.isValid     = true;
        out.append(c);
    }
}

// ──────────────────────────────────────────────────────────────────────────────
//  Grid Snap：對網格點吸附
// ──────────────────────────────────────────────────────────────────────────────
void OSnapDetector::detectGridPoint(
    const Handle(V3d_View)& view,
    const gp_Pnt& mousePt,
    QVector<SnapCandidate>& out)
{
    const double g = m_settings.gridSpacing;
    if (g < 1e-10) return;

    gp_Pln plane;
    if (m_activePlane) {
        plane = m_activePlane->toGpPln();
    } else {
        // 預設 XY 平面
        plane = gp_Pln(gp_Ax3());
    }

    // 投影滑鼠點到平面
    gp_Pnt projPt = mousePt;
    // 取得平面上最近的網格點
    double u, v;
    ElSLib::Parameters(plane, mousePt, u, v);
    double ug = std::round(u / g) * g;
    double vg = std::round(v / g) * g;
    gp_Pnt gridPt = ElSLib::Value(ug, vg, plane);

    SnapCandidate c;
    c.type       = SnapType::Grid;
    c.worldPoint = gridPt;
    c.isValid    = true;
    out.append(c);
}

void OSnapDetector::detectExtension(
    const TopoDS_Shape& shape, const Handle(AIS_InteractiveObject)& aisObj,
    const gp_Pnt& mousePt, const Handle(V3d_View)& view,
    int mouseX, int mouseY, QVector<SnapCandidate>& out)
{
    TopExp_Explorer edgeExp(shape, TopAbs_EDGE);
    for (; edgeExp.More(); edgeExp.Next()) {
        TopoDS_Edge edge = TopoDS::Edge(edgeExp.Current());
        double first, last;
        Handle(Geom_Curve) curve = BRep_Tool::Curve(edge, first, last);
        if (curve.IsNull()) continue;

        // 只處理直線延伸
        Handle(Geom_Line) line = Handle(Geom_Line)::DownCast(curve);
        if (line.IsNull()) {
            Handle(Geom_TrimmedCurve) tc = Handle(Geom_TrimmedCurve)::DownCast(curve);
            if (!tc.IsNull()) line = Handle(Geom_Line)::DownCast(tc->BasisCurve());
        }
        if (line.IsNull()) continue;

        // 投影滑鼠點到無限直線
        GeomAPI_ProjectPointOnCurve proj(mousePt,
                                         new Geom_Line(line->Lin()), -1e9, 1e9);
        if (proj.NbPoints() == 0) continue;

        double param = proj.LowerDistanceParameter();
        // 只取邊的端點以外（延伸段）
        if (param >= first && param <= last) continue;

        gp_Pnt extPt = line->Value(param);

        SnapCandidate c;
        c.type        = SnapType::Extension;
        c.worldPoint  = extPt;
        c.sourceShape = shape;
        c.sourceEdge  = edge;
        c.sourceAIS   = aisObj;
        c.screenDist  = screenDistance(view, extPt, mouseX, mouseY);
        c.isValid     = true;
        out.append(c);
    }
}

// ──────────────────────────────────────────────────────────────────────────────
//  工具函數
// ──────────────────────────────────────────────────────────────────────────────
bool OSnapDetector::worldToScreen(
    const Handle(V3d_View)& view,
    const gp_Pnt& worldPt,
    double& screenX, double& screenY)
{
    Standard_Integer px, py;
    view->Convert(worldPt.X(), worldPt.Y(), worldPt.Z(), px, py);
    screenX = static_cast<double>(px);
    screenY = static_cast<double>(py);
    return true;
}

double OSnapDetector::screenDistance(
    const Handle(V3d_View)& view,
    const gp_Pnt& pt,
    int mouseX, int mouseY)
{
    double sx, sy;
    worldToScreen(view, pt, sx, sy);
    double dx = sx - mouseX;
    double dy = sy - mouseY;
    return std::sqrt(dx*dx + dy*dy);
}

SnapCandidate OSnapDetector::makeCandidate(
    SnapType type,
    const gp_Pnt& worldPt,
    const Handle(AIS_InteractiveObject)& aisObj,
    const Handle(V3d_View)& view,
    int mouseX, int mouseY)
{
    SnapCandidate c;
    c.type       = type;
    c.worldPoint = worldPt;
    c.sourceAIS  = aisObj;
    c.screenDist = screenDistance(view, worldPt, mouseX, mouseY);
    c.isValid    = true;
    return c;
}

gp_Pnt OSnapDetector::projectToActivePlane(const gp_Pnt& pt) {
    if (!m_activePlane) return pt;

    gp_Pln pln = m_activePlane->toGpPln();
    // 投影到平面
    gp_Pnt projPt = pt;
    double dist = pln.Distance(pt);
    if (std::abs(dist) < 1e-6) return pt;

    gp_Vec normal = pln.Axis().Direction();
    return gp_Pnt(pt.X() - normal.X() * dist,
                  pt.Y() - normal.Y() * dist,
                  pt.Z() - normal.Z() * dist);
}

} // namespace osnap
} // namespace aicad
