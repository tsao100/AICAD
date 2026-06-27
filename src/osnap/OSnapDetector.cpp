/**
 * @file OSnapDetector.cpp
 * @brief Object Snap 偵測核心引擎實作
 */

#include "OSnapDetector.h"
#include "../cad/Plane.h"   // aicad::cad::Plane
#include "../cad/Sketch.h"  // 草圖幾何 geomUuid 對應
#include "../cad/sketch/SketchPointAIS.h"  // ✅ Step 14: SketchPoint snap
#include "../railway/AlignmentDocument.h"  // aicad::railway::AlignmentDocument

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
        detectGridPoint(view, mouseWorldPt, mouseX, mouseY, candidates);
    }

    // Alignment snap（不依賴 AIS shape，直接查詢 AlignmentDocument）
    const bool anyAlignmentEnabled =
        m_settings.enabledTypes.testFlag(SnapType::AlignmentPI)   ||
        m_settings.enabledTypes.testFlag(SnapType::AlignmentTC)   ||
        m_settings.enabledTypes.testFlag(SnapType::AlignmentMid)  ||
        m_settings.enabledTypes.testFlag(SnapType::AlignmentPerp);
    if (m_alignmentDoc && anyAlignmentEnabled) {
        detectAlignmentSnap(view, mouseWorldPt, mouseX, mouseY, candidates);
    }

    // ── Step 4: 過濾掉超過 pickRadius 的候選 ──────────────────────────────────
    // ✅ 修正：鎖定用 magnetRadius，收集用 pickPixelRadius
    candidates.erase(
        std::remove_if(candidates.begin(), candidates.end(),
                       [this](const SnapCandidate& c) {
                           if (c.screenDist > m_settings.magnetRadius)
                               return true;
                           // ★ 排除 active grip 自身位置
                           if (m_hasExcludedPoint &&
                               c.worldPoint.Distance(m_excludedPoint) < m_excludedPointTol)
                               return true;
                           return false;
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

        // ✅ Step 14: 偵測 SketchPointAIS（點比曲線端點更精確）
        if (auto ptAis = Handle(aicad::cad::SketchPointAIS)::DownCast(obj)) {
            // 用螢幕距離快速篩選
            gp_Pnt pos = ptAis->position3D();
            double dist = screenDistance(view, pos, mouseX, mouseY);
            if (dist < pickRadius * 2.0) {
                aisObjects.append(ptAis);
                // SketchPointAIS 沒有 TopoDS_Shape，用空 shape 佔位
                shapes.append(TopoDS_Shape());
            }
            continue;
        }

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

    // ✅ Step 14: SketchPointAIS 優先偵測（點優先於曲線端點）
    if (auto ptAis = Handle(aicad::cad::SketchPointAIS)::DownCast(aisObj)) {
        if (enabled.testFlag(SnapType::Endpoint) ||
            enabled.testFlag(SnapType::Center)) {
            gp_Pnt pos = ptAis->position3D();
            double screenDist = screenDistance(view, pos, mouseX, mouseY);
            if (screenDist < m_settings.pickPixelRadius * 2.0) {
                SnapType stype = (ptAis->origin() == aicad::cad::SketchPoint::Origin::Center)
                                 ? SnapType::Center : SnapType::Endpoint;
                SnapCandidate c;
                c.type       = stype;
                c.worldPoint = pos;
                c.geomUuid   = ptAis->pointUuid();
                c.geomHandle = static_cast<int>(aicad::cad::GeomHandle::WholeGeom);
                // 轉回平面座標
                if (m_activePlane) {
                    c.planePoint = m_activePlane->toPlane(
                        QVector3D(pos.X(), pos.Y(), pos.Z()));
                }
                candidates.append(c);
            }
        }
        return;  // SketchPointAIS 不走後續曲線 detect 路徑
    }

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

    if (enabled.testFlag(SnapType::Parallel) && m_hasLastPoint)
        detectParallel(shape, aisObj, mouseWorldPt, view, mouseX, mouseY, candidates);
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

    // 建立 AIS shape → geomUuid 的快查表（只在有 activeSketch 時）
    // 匹配規則：比對 3D 點座標與草圖幾何端點，找出 geomUuid + GeomHandle
    auto resolveSketchRef = [this](const gp_Pnt& worldPt,
                                   QString& outUuid, int& outHandle) {
        if (!m_activeSketch) return;
        double tol = 1e-4;
        for (const cad::SketchGeometry* g : m_activeSketch->geometriesRef()) {
            if (!g || g->points.size() < 2) {
                // Circle/Arc 可能只有 center
                if (g && g->points.size() == 1) {
                    QVector3D w3 = m_activeSketch->plane()
                                   ? m_activeSketch->plane()->toWorld(
                                       g->points[0].x(), g->points[0].y())
                                   : QVector3D(g->points[0].x(), g->points[0].y(), 0);
                    gp_Pnt wp(w3.x(), w3.y(), w3.z());
                    if (wp.Distance(worldPt) < tol) {
                        outUuid   = g->uuid;
                        outHandle = static_cast<int>(cad::GeomHandle::Center);
                        return;
                    }
                }
                continue;
            }
            // Start point
            {
                QVector3D w3 = m_activeSketch->plane()
                               ? m_activeSketch->plane()->toWorld(
                                   g->points.front().x(), g->points.front().y())
                               : QVector3D(g->points.front().x(), g->points.front().y(), 0);
                gp_Pnt wp(w3.x(), w3.y(), w3.z());
                if (wp.Distance(worldPt) < tol) {
                    outUuid   = g->uuid;
                    outHandle = static_cast<int>(cad::GeomHandle::Start);
                    return;
                }
            }
            // End point
            {
                QVector3D w3 = m_activeSketch->plane()
                               ? m_activeSketch->plane()->toWorld(
                                   g->points.back().x(), g->points.back().y())
                               : QVector3D(g->points.back().x(), g->points.back().y(), 0);
                gp_Pnt wp(w3.x(), w3.y(), w3.z());
                if (wp.Distance(worldPt) < tol) {
                    outUuid   = g->uuid;
                    outHandle = static_cast<int>(cad::GeomHandle::End);
                    return;
                }
            }
        }
    };

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

        // Phase: 填入草圖點 ID
        resolveSketchRef(pt, c.geomUuid, c.geomHandle);

        double sx, sy;
        if (worldToScreen(view, pt, sx, sy)) {
            c.worldDist = pt.Distance(gp_Pnt(0,0,0));
            c.isValid   = true;
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
        c.screenDist  = screenDistance(view, midPt, mouseX, mouseY);
        c.isValid     = true;
        // Phase: Midpoint 對應整條幾何（Curve handle）— geomUuid 留空，
        // 由上層根據 aisObj 查表補填（ConstraintPickSession 負責）
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
    // ① 取得四分點基準方向：優先草圖平面軸，否則世界 X/Y 軸
    gp_Dir refX(1,0,0), refY(0,1,0);
    if (m_activePlane) {
        QVector3D qx = m_activePlane->xAxis();
        QVector3D qy = m_activePlane->yAxis();
        refX = gp_Dir(qx.x(), qx.y(), qx.z());
        refY = gp_Dir(qy.x(), qy.y(), qy.z());
    }

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

        const gp_Circ& circ = circle->Circ();
        const gp_Pnt   center = circle->Location();
        const double   r      = circle->Radius();

        // ✅ Bug 1 修正：完整圓時完全跳過範圍檢查
        bool isFullCircle = (last - first >= 2.0 * M_PI - 1e-7);

        // ② 將基準方向投影到圓所在平面（去除法線分量後正規化）
        // ✅ Bug 2 修正：以草圖座標軸而非參數角計算四分點
        gp_Dir circNormal = circ.Axis().Direction();

        auto projectOntoCircPlane = [&](const gp_Dir& d) -> gp_Vec {
            gp_Vec v(d);
            double dot = v.Dot(gp_Vec(circNormal));
            v = v - gp_Vec(circNormal) * dot;
            if (v.Magnitude() < 1e-10) return gp_Vec(0,0,0);
            v.Normalize();
            return v;
        };

        gp_Vec px = projectOntoCircPlane(refX);
        gp_Vec py = projectOntoCircPlane(refY);

        // 若投影後趨近零向量（基準軸與圓法線平行），退而使用圓自身軸
        if (px.Magnitude() < 0.5) px = gp_Vec(circ.XAxis().Direction());
        if (py.Magnitude() < 0.5) py = gp_Vec(circ.YAxis().Direction());

        // ③ 四個四分點（+X, -X, +Y, -Y）
        const gp_Vec quadDirs[4] = { px, -px, py, -py };

        for (const gp_Vec& d : quadDirs) {
            gp_Pnt qpt(center.X() + d.X() * r,
                       center.Y() + d.Y() * r,
                       center.Z() + d.Z() * r);

            if (!isFullCircle) {
                // ④ 取得此點在圓上的參數
                double param     = ElCLib::Parameter(circ, qpt);
                // 正規化到 [first, first+2π)
                double normParam = ElCLib::InPeriod(param, first, first + 2.0 * M_PI);

                // ⑤ 嚴格排除端點（端點由 Endpoint snap 負責，避免重疊顯示）
                double margin = std::max(1e-6, (last - first) * 1e-4);
                if (normParam < first + margin || normParam > last - margin)
                    continue;

                // ⑥ 世界座標距離雙重保護（對抗浮點誤差）
                gp_Pnt arcStart = ElCLib::Value(first, circ);
                gp_Pnt arcEnd   = ElCLib::Value(last,  circ);
                if (qpt.Distance(arcStart) < r * 1e-3 ||
                    qpt.Distance(arcEnd)   < r * 1e-3)
                    continue;
            }

            SnapCandidate c;
            c.type        = SnapType::Quadrant;
            c.worldPoint  = qpt;
            c.sourceShape = shape;
            c.sourceEdge  = edge;
            c.sourceAIS   = aisObj;
            c.paramOnEdge = ElCLib::Parameter(circ, qpt);
            c.screenDist  = screenDistance(view, qpt, mouseX, mouseY);
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
    const gp_Pnt& /*mousePt*/,
    int mouseX, int mouseY,
    QVector<SnapCandidate>& out)
{
    // ✅ 建立邊→AIS 的對應表（修正原本用邊索引去查 shape 陣列的越界問題）
    QVector<TopoDS_Edge>                   allEdges;
    QVector<Handle(AIS_InteractiveObject)> edgeAIS;

    for (int si = 0; si < shapes.size(); ++si) {
        TopExp_Explorer exp(shapes[si], TopAbs_EDGE);
        for (; exp.More(); exp.Next()) {
            allEdges.append(TopoDS::Edge(exp.Current()));
            edgeAIS.append(aisObjects[si]);   // ← 與邊同步，不會越界
        }
    }

    const double tol = 0.1;

    for (int i = 0; i < allEdges.size(); ++i) {
        for (int j = i + 1; j < allEdges.size(); ++j) {
            try {
                BRepExtrema_DistShapeShape dist(allEdges[i], allEdges[j]);
                if (!dist.IsDone()) continue;
                if (dist.Value() > tol) continue;

                gp_Pnt pt1 = dist.PointOnShape1(1);
                gp_Pnt pt2 = dist.PointOnShape2(1);
                gp_Pnt intersect((pt1.X()+pt2.X())*0.5,
                                 (pt1.Y()+pt2.Y())*0.5,
                                 (pt1.Z()+pt2.Z())*0.5);

                SnapCandidate c;
                c.type       = SnapType::Intersection;
                c.worldPoint = intersect;
                c.sourceEdge = allEdges[i];
                c.sourceAIS  = edgeAIS[i];   // ← 使用對應表，不越界
                c.screenDist = screenDistance(view, intersect, mouseX, mouseY);
                c.isValid    = true;
                out.append(c);
            } catch (const Standard_Failure&) {
                continue;  // 忽略不相容的邊組合
            }
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
            gp_Ax1 rotAxis(center, gp_Dir(zAxis));
            gp_Vec rotDir = fromDir;   // ✅ 修正：不 Reversed()
            rotDir.Rotate(rotAxis, sign * (M_PI * 0.5 - alpha));
            gp_Pnt tangPt(center.X() + rotDir.X() * r,
                          center.Y() + rotDir.Y() * r,
                          center.Z() + rotDir.Z() * r);

            gp_Vec v1(center, tangPt);
            gp_Vec v2(m_lastInputPoint, tangPt);
            if (std::abs(v1.Dot(v2)) > 0.01 * r) continue;

            SnapCandidate c;
            c.type        = SnapType::Tangent;
            c.worldPoint  = tangPt;
            c.sourceShape = shape;
            c.sourceEdge  = edge;
            c.sourceAIS   = aisObj;
            c.screenDist  = screenDistance(view, tangPt, mouseX, mouseY);
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
    TopoDS_Vertex mouseVertex = BRepBuilderAPI_MakeVertex(mousePt);

    TopExp_Explorer edgeExp(shape, TopAbs_EDGE);
    for (; edgeExp.More(); edgeExp.Next()) {
        TopoDS_Edge edge = TopoDS::Edge(edgeExp.Current());

        // ✅ 防止退化邊或不相容形狀拋出 BRepExtrema_UnCompatibleShape
        try {
            BRepExtrema_DistShapeShape dist(mouseVertex, edge);
            if (!dist.IsDone()) continue;

            gp_Pnt nearPt = dist.PointOnShape2(1);

            double first, last;
            Handle(Geom_Curve) curve = BRep_Tool::Curve(edge, first, last);
            if (!curve.IsNull()) {
                Standard_Real param = 0.0;
                dist.ParOnEdgeS2(1, param);
                double relParam = (param - first) / (last - first + 1e-12);
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
            c.screenDist  = screenDistance(view, nearPt, mouseX, mouseY);
            c.isValid     = true;
            out.append(c);
        } catch (const Standard_Failure&) {
            continue;  // ✅ 捕捉所有 OCCT 例外，包含 BRepExtrema_UnCompatibleShape
        }
    }
}

// ──────────────────────────────────────────────────────────────────────────────
//  Parallel：從上一個輸入點，沿與已有直線平行的方向吸附
// ──────────────────────────────────────────────────────────────────────────────
void OSnapDetector::detectParallel(
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

        // 只對直線段求平行
        Handle(Geom_Line) gline;
        Handle(Geom_TrimmedCurve) tc = Handle(Geom_TrimmedCurve)::DownCast(baseCurve);
        if (!tc.IsNull())
            gline = Handle(Geom_Line)::DownCast(tc->BasisCurve());
        else
            gline = Handle(Geom_Line)::DownCast(baseCurve);
        if (gline.IsNull()) continue;

        gp_Dir dir = gline->Lin().Direction();

        // 過 m_lastInputPoint，方向平行於 dir 的直線
        // 將 mousePt 投影到此平行線上
        gp_Vec toMouse(m_lastInputPoint, mousePt);
        double t = toMouse.Dot(gp_Vec(dir));
        gp_Pnt snapPt(m_lastInputPoint.X() + dir.X() * t,
                      m_lastInputPoint.Y() + dir.Y() * t,
                      m_lastInputPoint.Z() + dir.Z() * t);

        // snapPt 與 mousePt 的螢幕距離：距離越小表示滑鼠越靠近平行方向
        double sd = screenDistance(view, snapPt, mouseX, mouseY);

        SnapCandidate c;
        c.type        = SnapType::Parallel;
        c.worldPoint  = snapPt;
        c.sourceShape = shape;
        c.sourceEdge  = edge;
        c.sourceAIS   = aisObj;
        c.screenDist  = sd;
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
    int mouseX, int mouseY,    // ✅ 新增
    QVector<SnapCandidate>& out)
{
    const double g = m_settings.gridSpacing;
    if (g < 1e-10) return;

    gp_Pln plane;
    if (m_activePlane) {
        plane = m_activePlane->toGpPln();
    } else {
        plane = gp_Pln(gp_Ax3());
    }

    double u, v;
    ElSLib::Parameters(plane, mousePt, u, v);
    double ug = std::round(u / g) * g;
    double vg = std::round(v / g) * g;
    gp_Pnt gridPt = ElSLib::Value(ug, vg, plane);

    SnapCandidate c;
    c.type       = SnapType::Grid;
    c.worldPoint = gridPt;
    c.screenDist = screenDistance(view, gridPt, mouseX, mouseY);  // ✅ 必須設定，否則被 magnetRadius 過濾
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
    double dist = pln.Distance(pt);
    if (std::abs(dist) < 1e-6) return pt;

    gp_Vec normal = pln.Axis().Direction();
    return gp_Pnt(pt.X() - normal.X() * dist,
                  pt.Y() - normal.Y() * dist,
                  pt.Z() - normal.Z() * dist);
}

// ──────────────────────────────────────────────────────────────────────────────
//  Alignment Snap 偵測
//
//  資料來源：AlignmentDocument → HorizontalAlignmentEdit → HorizontalAlignment
//  AlignmentPI   — EditableElement 的 startPI / endPI（PI 交點）
//  AlignmentTC   — rawPoints() 中 tsc 為 TC/CS/ST/TS/CC 的切點
//  AlignmentMid  — 相鄰兩個 rawPoints 的中點
//  AlignmentPerp — 游標至相鄰 rawPoints 連線段的垂足
//
//  注意：rawPoints 座標以 (easting, northing) 存放，Z = 0（平面線形）
// ──────────────────────────────────────────────────────────────────────────────
void OSnapDetector::detectAlignmentSnap(
    const Handle(V3d_View)& view,
    const gp_Pnt& mousePt,
    int mouseX, int mouseY,
    QVector<SnapCandidate>& out)
{
    using namespace aicad::railway;

    if (!m_alignmentDoc) return;

    HorizontalAlignmentEdit* hEdit = m_alignmentDoc->horizontal();
    if (!hEdit) return;

    const HorizontalAlignment* halign = hEdit->result();

    // ──────────────────────────────────────────────────────────────────────────
    //  AlignmentPI：從 EditableElement 的 startPI / endPI 取交點
    // ──────────────────────────────────────────────────────────────────────────
    if (m_settings.enabledTypes.testFlag(SnapType::AlignmentPI)) {
        const auto& elems = hEdit->elements();
        for (const EditableElement& e : elems) {
            // Tangent 元素的兩個端點即為 PI 點
            if (e.type == EditableElementType::Tangent) {
                for (const QPointF& pi : { e.startPI, e.endPI }) {
                    gp_Pnt piPt(pi.x(), pi.y(), 0.0);
                    double dist = screenDistance(view, piPt, mouseX, mouseY);
                    if (dist <= m_settings.pickPixelRadius) {
                        SnapCandidate c = makeCandidate(
                            SnapType::AlignmentPI, piPt, nullptr, view, mouseX, mouseY);
                        out.append(c);
                    }
                }
            }
        }
    }

    // rawPoints 以下三個類型都需要
    const bool needTC    = m_settings.enabledTypes.testFlag(SnapType::AlignmentTC);
    const bool needMid   = m_settings.enabledTypes.testFlag(SnapType::AlignmentMid);
    const bool needPerp  = m_settings.enabledTypes.testFlag(SnapType::AlignmentPerp);

    if (!halign || (!needTC && !needMid && !needPerp))
        return;

    const QVector<AlignmentPoint>& pts = halign->rawPoints();
    if (pts.isEmpty()) return;

    // ──────────────────────────────────────────────────────────────────────────
    //  AlignmentTC：rawPoints 中 tsc 為 TC/CS/ST/TS/CC 的切點
    // ──────────────────────────────────────────────────────────────────────────
    if (needTC) {
        // 已知切點 tsc 代碼集合（英制/日制/台鐵混用）
        static const QStringList kTcCodes = {
            "TC", "CT",         // Tangent-Curve / Curve-Tangent
            "CS", "SC",         // Curve-Spiral / Spiral-Curve
            "ST", "TS",         // Spiral-Tangent / Tangent-Spiral
            "CC",               // Curve-Curve（複曲線）
            "SCS", "CSS"        // 複合螺旋切點（較少見）
        };
        for (const AlignmentPoint& ap : pts) {
            if (kTcCodes.contains(ap.tsc, Qt::CaseInsensitive)) {
                gp_Pnt tcPt(ap.easting, ap.northing, 0.0);
                double dist = screenDistance(view, tcPt, mouseX, mouseY);
                if (dist <= m_settings.pickPixelRadius) {
                    SnapCandidate c = makeCandidate(
                        SnapType::AlignmentTC, tcPt, nullptr, view, mouseX, mouseY);
                    out.append(c);
                }
            }
        }
    }

    // ──────────────────────────────────────────────────────────────────────────
    //  AlignmentMid & AlignmentPerp：逐段掃描相鄰 rawPoints
    // ──────────────────────────────────────────────────────────────────────────
    if (!needMid && !needPerp) return;

    for (int i = 0; i + 1 < pts.size(); ++i) {
        const AlignmentPoint& a = pts[i];
        const AlignmentPoint& b = pts[i + 1];

        gp_Pnt pA(a.easting, a.northing, 0.0);
        gp_Pnt pB(b.easting, b.northing, 0.0);

        gp_Vec seg(pA, pB);
        double segLen = seg.Magnitude();
        if (segLen < kMinEdgeLength) continue;

        // ── AlignmentMid：線段中點 ─────────────────────────────────────────
        if (needMid) {
            gp_Pnt midPt(
                (pA.X() + pB.X()) * 0.5,
                (pA.Y() + pB.Y()) * 0.5,
                0.0);
            double dist = screenDistance(view, midPt, mouseX, mouseY);
            if (dist <= m_settings.pickPixelRadius) {
                SnapCandidate c = makeCandidate(
                    SnapType::AlignmentMid, midPt, nullptr, view, mouseX, mouseY);
                out.append(c);
            }
        }

        // ── AlignmentPerp：游標在線段上的垂足 ────────────────────────────
        if (needPerp) {
            // 參數 t = (mousePt - pA) · seg / |seg|²，限制到 [0, 1]
            gp_Vec toMouse(pA, mousePt);
            double t = toMouse.Dot(seg) / (segLen * segLen);
            t = std::max(0.0, std::min(1.0, t));

            // 若垂足剛好是端點，跳過（避免與 AlignmentTC / PI 重複）
            if (t < 1e-4 || t > 1.0 - 1e-4) continue;

            gp_Pnt footPt(
                pA.X() + seg.X() * t,
                pA.Y() + seg.Y() * t,
                0.0);
            double dist = screenDistance(view, footPt, mouseX, mouseY);
            if (dist <= m_settings.pickPixelRadius) {
                SnapCandidate c = makeCandidate(
                    SnapType::AlignmentPerp, footPt, nullptr, view, mouseX, mouseY);
                out.append(c);
            }
        }
    }
}

} // namespace osnap
} // namespace aicad
