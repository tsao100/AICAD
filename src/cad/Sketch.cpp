/**
 * @file Sketch.cpp
 * @brief 與增強版 Plane 整合的 Sketch 類別實作
 */

#include "Sketch.h"
#include <cmath>
#include "Document.h"
#include "PlaneManager.h"
#include "sketch/SketchLoopFinder.h"
#include "sketch/SketchPointAIS.h"
#include "sketch/SketchGeom2DMath.h"
#include <gp_Ax3.hxx>

#include <QDebug>
#include <QtMath>
#include <QUuid>
#include <QRegularExpression>
#include <TopoDS.hxx>
#include <TopoDS_Wire.hxx>
#include <TopoDS_Compound.hxx>
#include <Precision.hxx>
#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <BRep_Builder.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <Geom_BSplineCurve.hxx>
#include <GeomAPI_Interpolate.hxx>
#include <TColgp_HArray1OfPnt.hxx>
#include <Standard_Failure.hxx>
#include <AIS_Shape.hxx>
#include <gp_Circ.hxx>
#include <gp_Elips.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <Geom_Circle.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <Prs3d_LineAspect.hxx>
#include <TopTools_HSequenceOfShape.hxx>
#include <ShapeAnalysis_FreeBounds.hxx>
#include <TopExp_Explorer.hxx>

#include <QJsonArray>
#include <QSet>
#include <cstring>

namespace aicad {
namespace cad {

namespace {
/// 一般草圖幾何（線／圓／弧／橢圓／折線／雲形線等 AIS_Shape 邊線）的選取
/// 容差（螢幕像素），供 AIS_InteractiveContext::SetSelectionSensitivity()
/// 使用。原本這些幾何從未明確設定過，落回 OCCT 內建預設值（實測大約只有
/// 1～2 像素等級），在一般螢幕/高 DPI 下線幾乎要「精準點在線上」才選得到，
/// 這正是「線的 threshold 太小、選取不易」的根因。已知的參考幾何（X/Y 軸、
/// 原點，見 CadView.cpp）之前就已經個別設成 4.0/8.0，這裡把一般可編輯的
/// 草圖幾何也一併明確設定，數值刻意略高於參考軸線（4.0）——因為使用者
/// 實際要頻繁點選、編輯的是這些幾何，理應比背景參考線更好點。
constexpr double kGeomSelectionSensitivityPx = 6.0;

/// SketchPoint（端點/頂點）的選取容差（像素）。點本身沒有面積，比線更難
/// 精準點中，容差維持比線更寬鬆，比照既有原點的 8.0。
constexpr double kPointSelectionSensitivityPx = 8.0;

/// 依 AIS 物件實際型別（一般幾何 AIS_Shape vs. SketchPointAIS）套用對應的
/// 選取容差；selectionMode 固定為 0（AIS_Shape 的「整體形狀」/SketchPointAIS
/// 唯一啟用的模式，見呼叫端既有的 Activate(obj, 0, ...) 慣例）。
void applyGeomSelectionSensitivity(const Handle(AIS_InteractiveContext)& ctx,
                                    const Handle(AIS_InteractiveObject)& obj)
{
    if (ctx.IsNull() || obj.IsNull()) return;
    const double px = Handle(SketchPointAIS)::DownCast(obj).IsNull()
                           ? kGeomSelectionSensitivityPx
                           : kPointSelectionSensitivityPx;
    ctx->SetSelectionSensitivity(obj, 0, px);
}
} // namespace

Sketch::Sketch(Document* parent)
    : Feature(parent)
    , m_plane(nullptr)
{
    setName("Sketch");

    // 建立預設 XY 平面
    createDefaultPlane();

    qDebug() << "[Sketch]" << name() << "created with plane:"
             << (m_plane ? m_plane->displayName() : "None");
}

Sketch::~Sketch() {
    disconnectPlaneSignals();
    clearGeometry();
    qDebug() << "[Sketch]" << name() << "destroyed";
}

void Sketch::setPlane(Plane* plane) {
    if (!plane) {
        qWarning() << "[Sketch]" << name() << "Cannot set null plane";
        return;
    }

    if (m_plane == plane) {
        return;
    }

    disconnectPlaneSignals();

    Plane* oldPlane = m_plane;
    m_plane = plane;

    connectPlaneSignals();

    qDebug() << "[Sketch]" << name() << "plane changed from"
             << (oldPlane ? oldPlane->displayName() : "None")
             << "to" << m_plane->displayName();

    Q_EMIT planeChanged(m_plane);
    Q_EMIT rebuildRequested();
}

void Sketch::addGeometry(SketchGeometry* geom) {
    if (!geom) return;
    m_geometries.append(geom);
    Q_EMIT geometryChanged();
    Q_EMIT rebuildRequested();
}

void Sketch::removeGeometryAt(int index) {
    if (index >= 0 && index < m_geometries.size()) {
        QString uuid = m_geometries[index]->uuid;
        removeConstraintsOf(uuid);          // ← 新增
        removeAnnotationsOf(uuid);          // ← GDIM v2 Phase 1：連同標註一併清除
        delete m_geometries.takeAt(index);
    }
}

void Sketch::removeGeometry(int index) {
    if (index >= 0 && index < m_geometries.size()) {
        removeGeometryAt(index);
        Q_EMIT geometryChanged();
        Q_EMIT rebuildRequested();
    }
}

bool Sketch::removeGeometry(const QString& uuid) {
    for (int i = 0; i < m_geometries.size(); ++i) {
        if (m_geometries[i]->uuid == uuid) {
            removeGeometry(i);
            return true;
        }
    }
    return false;
}


void Sketch::clearGeometry() {
    qDeleteAll(m_geometries);
    m_geometries.clear();
    m_uuidToGeomIndex.clear();
    Q_EMIT geometryChanged();
}

void Sketch::addLine(const QVector2D& p1, const QVector2D& p2) {
    // ✅ 改為呼叫 addLineGeom，確保 SketchPoint 被建立
    addLineGeom(p1, p2);
}

void Sketch::addPolyline(const QVector<QVector2D>& points, bool closed) {
    // ✅ 退化為多條獨立 SketchLine + 相鄰段落自動 Coincident 束制，
    //    不再建立單一 SketchPolyline 幾何。理由：
    //      - 每一段可個別被選取、標註尺寸、設定束制（水平/垂直/相切…）、
    //        轉為建構線、單獨刪除或倒圓角，與 Line 指令產生的結果完全一致；
    //      - 與 ConstraintSolver / grip 編輯 / 尺寸標註等既有管線共用同一套
    //        SketchLine 處理邏輯，不需要為 SketchPolyline 另外維護特例。
    //    （舊格式 SketchPolyline/addPolylineGeom 仍保留，供讀取舊檔案使用。）
    addLineChainGeom(points, closed);
}

void Sketch::addSpline(const QVector<QVector2D>& points) {
    if (points.size() < 3) {
        qWarning() << "[Sketch]" << name() << "spline needs at least 3 points";
        return;
    }
    addSplineGeom(points);
}

void Sketch::addCircle(const QVector2D& center, double radius) {
    // ✅ 改為呼叫 addCircleGeom，確保 SketchPoint 被建立
    addCircleGeom(center, radius);
}

void Sketch::addEllipse(const QVector2D& center, double majorRadius, double minorRadius, double angle) {
    if (majorRadius <= 0 || minorRadius <= 0) {
        qWarning() << "[Sketch]" << name() << "ellipse radii must be positive";
        return;
    }
    if (majorRadius < minorRadius) {
        qWarning() << "[Sketch]" << name() << "major radius must be >= minor radius";
        return;
    }
    addEllipseGeom(center, majorRadius, minorRadius, angle);
}

void Sketch::addRectangle(const QVector2D& corner1, const QVector2D& corner2) {
    QVector<QVector2D> points;
    points << corner1
           << QVector2D(corner2.x(), corner1.y())
           << corner2
           << QVector2D(corner1.x(), corner2.y());

    // 退化為 4 條獨立 SketchLine + 4 個角落的 Coincident 束制
    QStringList lineUuids = addLineChainGeom(points, true);

    // 補上 Horizontal/Vertical 束制，讓矩形在之後被拖曳/求解時仍維持「矩形」
    // （否則退化後只是一個沒有形狀限制的封閉四邊形）。
    // 邊的順序對應 points：corner1→(x2,y1) 水平、(x2,y1)→corner2 垂直、
    //                     corner2→(x1,y2) 水平、(x1,y2)→corner1 垂直。
    if (lineUuids.size() == 4) {
        constrainHorizontal(lineUuids[0]);
        constrainVertical(lineUuids[1]);
        constrainHorizontal(lineUuids[2]);
        constrainVertical(lineUuids[3]);
    } else {
        qWarning() << "[Sketch]" << name()
                   << "addRectangle: unexpected line count" << lineUuids.size();
    }
}

QVector3D Sketch::planeToWorld(const QVector2D& planePt) const {
    return m_plane->origin() + m_plane->xAxis() * planePt.x() + m_plane->yAxis() * planePt.y();
}

void Sketch::addArc(const QVector2D& startPoint,
                    const QVector2D& midPoint,
                    const QVector2D& endPoint)
{
    // ✅ 改為呼叫 addArcGeom，確保 SketchPoint 被建立
    addArcGeom(startPoint, midPoint, endPoint);
}

QString Sketch::addArcGeom(const QVector2D& startPoint,
                            const QVector2D& midPoint,
                            const QVector2D& endPoint,
                            const QString& reuseStartUuid,
                            const QString& reuseEndUuid,
                            const QString& reuseCenterUuid)
{
    try {
        QVector3D w1 = planeToWorld(startPoint);
        QVector3D w2 = planeToWorld(midPoint);
        QVector3D w3 = planeToWorld(endPoint);

        gp_Pnt gp1(w1.x(), w1.y(), w1.z());
        gp_Pnt gp2(w2.x(), w2.y(), w2.z());
        gp_Pnt gp3(w3.x(), w3.y(), w3.z());

        GC_MakeArcOfCircle arcMaker(gp1, gp2, gp3);
        if (!arcMaker.IsDone()) {
            qWarning() << "[Sketch]" << name() << "addArcGeom: failed (points may be collinear)";
            return QString();
        }

        Handle(Geom_TrimmedCurve) arc = arcMaker.Value();
        auto* arcGeom = new SketchArc(arc);

        // ✅ 修正：points[] 現在會在建構時就被初始化為 3 個佔位元素（見
        // SketchArc 建構子註解），這裡立即填入實際的平面座標，讓
        // ConstraintPickSession 等依賴 points[] 判斷「是否可點選」的邏輯，
        // 以及 syncGeometryFromPoints() 之後的同步都能拿到正確資料。
        arcGeom->points = { startPoint, midPoint, endPoint };

        // ✅ Phase 0B：建立或重用起點/終點/圓心 SketchPoint
        arcGeom->startUuid  = reuseStartUuid.isEmpty()
            ? addPoint(startPoint, SketchPoint::Origin::Endpoint)
            : reuseStartUuid;
        arcGeom->endUuid    = reuseEndUuid.isEmpty()
            ? addPoint(endPoint,   SketchPoint::Origin::Endpoint)
            : reuseEndUuid;

        if (reuseCenterUuid.isEmpty()) {
            // 計算圓心 2D 座標並建立 Center 點
            gp_Pnt gcCenter = Handle(Geom_Circle)::DownCast(arc->BasisCurve())->Location();
            QVector2D centerPos = m_plane->toPlane(
                QVector3D(gcCenter.X(), gcCenter.Y(), gcCenter.Z()));
            arcGeom->centerUuid = addPoint(centerPos, SketchPoint::Origin::Center);
        } else {
            arcGeom->centerUuid = reuseCenterUuid;
        }

        m_geometries.append(arcGeom);
        Q_EMIT geometryChanged();
        Q_EMIT rebuildRequested();
        qDebug() << "[Sketch]" << name() << "Arc added with SketchPoints";
        return arcGeom->uuid;
    } catch (Standard_Failure const& e) {
        qWarning() << "[Sketch] OCCT error:" << e.GetMessageString();
        return QString();
    }
}

// ── 便捷方法實作 ──────────────────────────────────────────────────────────

void Sketch::addConstructionLine(const QVector2D& p1, const QVector2D& p2) {
    addGeometry(new SketchLine(p1, p2, GeomRole::Construction));
}

void Sketch::addCenterline(const QVector2D& p1, const QVector2D& p2) {
    addGeometry(new SketchLine(p1, p2, GeomRole::Centerline));
}

void Sketch::addConstructionCircle(const QVector2D& center, double radius) {
    addGeometry(new SketchCircle(center, radius, GeomRole::Construction));
}

void Sketch::addConstructionArc(const QVector2D& start, const QVector2D& mid,
                                const QVector2D& end, GeomRole role) {
    // 複用現有 addArc 邏輯，但在建立後設定 role
    int countBefore = m_geometries.size();
    addArc(start, mid, end);
    if (m_geometries.size() > countBefore)
        m_geometries.last()->role = role;
}

QList<SketchGeometry*> Sketch::normalGeometries() const {
    QList<SketchGeometry*> result;
    for (auto* g : m_geometries)
        if (!g->isConstruction()) result.append(g);
    return result;
}

QList<SketchGeometry*> Sketch::constructionGeometries() const {
    QList<SketchGeometry*> result;
    for (auto* g : m_geometries)
        if (g->isConstruction()) result.append(g);
    return result;
}

SketchGeometry* Sketch::findGeometry(const QString& uuid) const {
    // O(1) cache lookup
    auto it = m_uuidToGeomIndex.find(uuid);
    if (it != m_uuidToGeomIndex.end()) {
        int idx = it.value();
        if (idx >= 0 && idx < m_geometries.size() && m_geometries[idx]->uuid == uuid)
            return m_geometries[idx];
        // cache stale — fall through to linear scan
    }
    // linear fallback（rebuild 後 index 可能改變）
    for (int i = 0; i < m_geometries.size(); ++i) {
        if (m_geometries[i]->uuid == uuid) {
            m_uuidToGeomIndex[uuid] = i;  // refresh cache
            return m_geometries[i];
        }
    }
    return nullptr;
}

// ============================================================================
//  geometryFingerprint() — cheap dirty-check for incremental AIS rebuild
// ============================================================================

namespace {
/// Combine a double into a running hash (bit-reinterpret to avoid NaN/round-off
/// surprises from qHash(double) on some Qt versions).
inline void hashCombine(quint64& seed, double v) {
    quint64 bits;
    static_assert(sizeof(bits) == sizeof(v), "double must be 64-bit");
    std::memcpy(&bits, &v, sizeof(v));
    // boost::hash_combine-style mix
    seed ^= bits + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
}
inline void hashCombine(quint64& seed, int v) {
    hashCombine(seed, static_cast<double>(v));
}
} // anonymous namespace

quint64 Sketch::geometryFingerprint(const SketchGeometry* geom)
{
    if (!geom) return 0;

    quint64 h = 1469598103934665603ULL;  // FNV offset basis, arbitrary non-zero seed
    hashCombine(h, static_cast<int>(geom->type));
    hashCombine(h, static_cast<int>(geom->role));

    if (const auto* arc = dynamic_cast<const SketchArc*>(geom)) {
        // SketchArc keeps its geometry in an OCCT curve, not in `points`.
        if (!arc->curve.IsNull()) {
            const double t0 = arc->curve->FirstParameter();
            const double t1 = arc->curve->LastParameter();
            gp_Pnt pS = arc->curve->Value(t0);
            gp_Pnt pM = arc->curve->Value((t0 + t1) * 0.5);
            gp_Pnt pE = arc->curve->Value(t1);
            hashCombine(h, pS.X()); hashCombine(h, pS.Y()); hashCombine(h, pS.Z());
            hashCombine(h, pM.X()); hashCombine(h, pM.Y()); hashCombine(h, pM.Z());
            hashCombine(h, pE.X()); hashCombine(h, pE.Y()); hashCombine(h, pE.Z());
        }
        return h;
    }

    for (const QVector2D& p : geom->points) {
        hashCombine(h, static_cast<double>(p.x()));
        hashCombine(h, static_cast<double>(p.y()));
    }

    if (const auto* pt = dynamic_cast<const SketchPoint*>(geom)) {
        hashCombine(h, static_cast<double>(pt->pos.x()));
        hashCombine(h, static_cast<double>(pt->pos.y()));
    } else if (const auto* circle = dynamic_cast<const SketchCircle*>(geom)) {
        hashCombine(h, static_cast<double>(circle->center.x()));
        hashCombine(h, static_cast<double>(circle->center.y()));
        hashCombine(h, circle->radius);
    } else if (const auto* ellipse = dynamic_cast<const SketchEllipse*>(geom)) {
        hashCombine(h, static_cast<double>(ellipse->center.x()));
        hashCombine(h, static_cast<double>(ellipse->center.y()));
        hashCombine(h, ellipse->majorRadius);
        hashCombine(h, ellipse->minorRadius);
        hashCombine(h, ellipse->angle);
    } else if (const auto* pline = dynamic_cast<const SketchPolyline*>(geom)) {
        hashCombine(h, pline->closed ? 1 : 0);
    }

    return h;
}

bool Sketch::rebuild() {
    qDebug() << "[Sketch]" << name() << "rebuilding with"
             << m_geometries.size() << "geometries on plane"
             << (m_plane ? m_plane->displayName() : "NULL");

    // ✅ Guard: plane 必須有效
    if (!hasValidPlane()) {
        qWarning() << "[Sketch]" << name() << "rebuild skipped: no valid plane";
        return false;
    }

    try {
        m_wires.clear();
        m_aisShapes.clear();
        m_aisShapeUuids.clear();
        m_constructionShapes.clear();
        m_constructionShapeUuids.clear();

        if (m_geometries.isEmpty()) {
            setShape(TopoDS_Shape());
            return true;
        }

        TopoDS_Compound compound;
        BRep_Builder builder;
        builder.MakeCompound(compound);

        for (const SketchGeometry* geom : m_geometries) {
            if (!geom) continue;
            // ── Point：建立 SketchPointAIS 並納入 m_aisShapes ────────────────
            if (geom->type == SketchGeometryType::Point) {
                const auto* pt = static_cast<const SketchPoint*>(geom);
                gp_Pnt org(m_plane->origin().x(), m_plane->origin().y(), m_plane->origin().z());
                gp_Dir xd (m_plane->xAxis().x(),  m_plane->xAxis().y(),  m_plane->xAxis().z());
                gp_Dir nd (m_plane->normal().x(), m_plane->normal().y(), m_plane->normal().z());
                gp_Ax3 ax3(org, nd, xd);
                Handle(SketchPointAIS) ptAis = new SketchPointAIS(pt, ax3);
                if (!geom->isConstruction()) {
                    m_aisShapes.append(ptAis);
                    m_aisShapeUuids.append(pt->uuid);
                }
                continue;
            }

            TopoDS_Wire wire;
            bool wireCreated = false;

            // ── Line ───────────────────────────────────────────────────────
            if (geom->type == SketchGeometryType::Line && geom->points.size() >= 2) {
                QVector3D p1 = m_plane->toWorld(geom->points[0].x(), geom->points[0].y());
                QVector3D p2 = m_plane->toWorld(geom->points[1].x(), geom->points[1].y());

                BRepBuilderAPI_MakeEdge edgeBuilder(
                    gp_Pnt(p1.x(), p1.y(), p1.z()),
                    gp_Pnt(p2.x(), p2.y(), p2.z()));

                if (edgeBuilder.IsDone()) {
                    BRepBuilderAPI_MakeWire wireBuilder(edgeBuilder.Edge());
                    if (wireBuilder.IsDone()) {
                        wire = wireBuilder.Wire();
                        wireCreated = true;
                    }
                }
            }

            // ── Polyline ───────────────────────────────────────────────────
            else if (geom->type == SketchGeometryType::Polyline) {
                const SketchPolyline* pline = static_cast<const SketchPolyline*>(geom);
                BRepBuilderAPI_MakeWire wireBuilder;

                int numSegments = pline->closed ? geom->points.size()
                                                : geom->points.size() - 1;

                for (int i = 0; i < numSegments; ++i) {
                    QVector3D p1 = m_plane->toWorld(geom->points[i].x(), geom->points[i].y());
                    int nextIdx  = (i + 1) % geom->points.size();
                    QVector3D p2 = m_plane->toWorld(geom->points[nextIdx].x(), geom->points[nextIdx].y());

                    BRepBuilderAPI_MakeEdge edgeBuilder(
                        gp_Pnt(p1.x(), p1.y(), p1.z()),
                        gp_Pnt(p2.x(), p2.y(), p2.z()));

                    if (edgeBuilder.IsDone()) {
                        wireBuilder.Add(edgeBuilder.Edge());
                    }
                }

                if (wireBuilder.IsDone()) {
                    wire = wireBuilder.Wire();
                    wireCreated = true;
                }
            }

            // ── Circle ────────────────────────────────────────────────────
            else if (geom->type == SketchGeometryType::Circle) {
                const SketchCircle* circle = static_cast<const SketchCircle*>(geom);
                QVector3D center3d = m_plane->toWorld(circle->center.x(), circle->center.y());

                gp_Ax2 ax2(
                    gp_Pnt(center3d.x(), center3d.y(), center3d.z()),
                    gp_Dir(m_plane->normal().x(), m_plane->normal().y(), m_plane->normal().z()));

                BRepBuilderAPI_MakeEdge edgeBuilder(gp_Circ(ax2, circle->radius));
                if (edgeBuilder.IsDone()) {
                    BRepBuilderAPI_MakeWire wireBuilder(edgeBuilder.Edge());
                    if (wireBuilder.IsDone()) {
                        wire = wireBuilder.Wire();
                        wireCreated = true;
                    }
                }
            }

            // ── Spline ────────────────────────────────────────────────────
            else if (geom->type == SketchGeometryType::Spline) {
                if (geom->points.size() < 3) {
                    qWarning() << "[Sketch] Spline requires at least 3 points, got:"
                               << geom->points.size();
                    continue;
                }

                try {
                    int numPoints = geom->points.size();
                    Handle(TColgp_HArray1OfPnt) controlPoints =
                        new TColgp_HArray1OfPnt(1, numPoints);

                    for (int i = 0; i < numPoints; ++i) {
                        QVector3D p = m_plane->toWorld(geom->points[i].x(), geom->points[i].y());
                        controlPoints->SetValue(i + 1, gp_Pnt(p.x(), p.y(), p.z()));
                    }

                    GeomAPI_Interpolate interpolator(controlPoints, Standard_False, 1.0e-6);
                    interpolator.Perform();

                    if (!interpolator.IsDone()) {
                        qWarning() << "[Sketch] Failed to interpolate spline";
                        continue;
                    }

                    BRepBuilderAPI_MakeEdge edgeBuilder(interpolator.Curve());
                    if (!edgeBuilder.IsDone()) {
                        qWarning() << "[Sketch] Failed to create edge from spline curve";
                        continue;
                    }

                    BRepBuilderAPI_MakeWire wireBuilder(edgeBuilder.Edge());
                    if (wireBuilder.IsDone()) {
                        wire = wireBuilder.Wire();
                        wireCreated = true;
                    }
                } catch (Standard_Failure& e) {
                    qWarning() << "[Sketch] Exception in spline creation:" << e.GetMessageString();
                }
            }

            // ── Arc ───────────────────────────────────────────────────────
            else if (geom->type == SketchGeometryType::Arc) {
                const SketchArc* arc = static_cast<const SketchArc*>(geom);
                if (arc->curve.IsNull()) {
                    qWarning() << "[Sketch] Arc curve is null";
                    continue;
                }

                BRepBuilderAPI_MakeEdge edgeBuilder(arc->curve);
                if (!edgeBuilder.IsDone()) {
                    qWarning() << "[Sketch] Arc edge build failed:" << edgeBuilder.Error();
                    continue;
                }

                BRepBuilderAPI_MakeWire wireBuilder(edgeBuilder.Edge());
                if (wireBuilder.IsDone()) {
                    wire = wireBuilder.Wire();
                    wireCreated = true;
                } else {
                    qWarning() << "[Sketch] Arc wire build failed";
                }
            }

            // ── Ellipse ───────────────────────────────────────────────────
            else if (geom->type == SketchGeometryType::Ellipse) {
                const SketchEllipse* ellipse = static_cast<const SketchEllipse*>(geom);

                QVector3D center3d = m_plane->toWorld(ellipse->center.x(), ellipse->center.y());
                gp_Pnt centerPnt(center3d.x(), center3d.y(), center3d.z());
                gp_Dir normal(m_plane->normal().x(), m_plane->normal().y(), m_plane->normal().z());

                double cosAngle = qCos(ellipse->angle);
                double sinAngle = qSin(ellipse->angle);

                QVector3D majorAxisEnd3D = m_plane->toWorld(
                    ellipse->center.x() + cosAngle,
                    ellipse->center.y() + sinAngle);
                QVector3D majorAxisDir3D = (majorAxisEnd3D - center3d).normalized();
                gp_Dir xDir(majorAxisDir3D.x(), majorAxisDir3D.y(), majorAxisDir3D.z());

                gp_Ax2 ax2(centerPnt, normal, xDir);
                gp_Elips gpEllipse(ax2, ellipse->majorRadius, ellipse->minorRadius);

                BRepBuilderAPI_MakeEdge edgeBuilder(gpEllipse);
                if (edgeBuilder.IsDone()) {
                    BRepBuilderAPI_MakeWire wireBuilder(edgeBuilder.Edge());
                    if (wireBuilder.IsDone()) {
                        wire = wireBuilder.Wire();
                        wireCreated = true;
                    }
                }
            }

            // ── Wire → AIS_Shape ──────────────────────────────────────────
            if (wireCreated) {
                Handle(AIS_Shape) aisShape = new AIS_Shape(wire);

                if (geom->isConstruction()) {
                    // ── 建構線樣式 ────────────────────────────────
                    applyConstructionStyle(aisShape, geom->role);  // ← 新增
                    m_constructionShapes.append(aisShape);
                    m_constructionShapeUuids.append(geom->uuid);  // ← 修正：需要 UUID 才能被
                                                                    //   CadView 反查表登記、進而可選取
                    // 不加入 m_wires（不參與輪廓）
                } else {
                    // ── 正常幾何樣式 ──────────────────────────────
                    aisShape->SetColor(Quantity_NOC_WHITE);
                    aisShape->SetWidth(2.0);
                    aisShape->SetDisplayMode(AIS_WireFrame);
                    m_wires.append(wire);
                    m_aisShapes.append(aisShape);
                    m_aisShapeUuids.append(geom->uuid);
                }
            }
        }

        // compound 只由 normal wires 組成（供 Extrude 使用）
        builder.MakeCompound(compound);
        for (const TopoDS_Wire& w : m_wires)
            builder.Add(compound, w);

        setShape(compound);

        // SketchPointAIS 已在上方迴圈中建立並加入 m_aisShapes

        qDebug() << "[Sketch]" << name() << "rebuilt with"
                 << m_wires.size() << "wires and"
                 << m_aisShapes.size() << "AIS shapes";
        if (!m_suppressRebuiltSignal)
            Q_EMIT rebuilt();
        return true;

    } catch (const Standard_Failure& e) {
        QString error = QString("OCCT error: %1").arg(e.GetMessageString());
        qCritical() << "[Sketch]" << name() << error;
        setError(error);
        return false;
    }
}

bool Sketch::rebuildShapesOnly()
{
    if (!hasValidPlane()) return false;
    if (m_aisContext.IsNull()) return false;

    // ── Snapshot the previous AIS state before rebuild() overwrites it ───────
    // rebuild() is CPU-only (TopoDS + fresh AIS handle allocation, no OCCT
    // context calls), so it's cheap to call unconditionally. What's
    // expensive is Erase/Display/Activate/UpdateCurrentViewer on the AIS
    // context — so we diff old vs new by uuid+fingerprint and only touch
    // the context for geometries whose solved shape actually changed,
    // instead of erasing and redisplaying everything every solve.
    const QList<Handle(AIS_InteractiveObject)> oldAisShapes   = m_aisShapes;
    const QList<QString>                       oldAisUuids    = m_aisShapeUuids;
    const QHash<QString, quint64>              oldFingerprints = m_geomFingerprints;
    const QList<Handle(AIS_Shape)>             oldConstructionShapes = m_constructionShapes;

    QHash<QString, Handle(AIS_InteractiveObject)> oldByUuid;
    for (int i = 0; i < oldAisUuids.size(); ++i)
        oldByUuid.insert(oldAisUuids[i], oldAisShapes[i]);

    // ② 重建 TopoDS + 取得全新 AIS handle 候選列表（純 CPU，未碰 context）。
    //    rebuild() 會 emit shapeChanged()（Extrude 等下游 feature 需要這個信號
    //    才能即時更新，不可封鎖）以及 rebuilt()（此時 m_aisShapes 只是
    //    「候選」handle，未變動的幾何稍後會被下面的 diff 換回舊 handle；
    //    若讓 CadView::onSketchRebuilt() 在這個時間點處理，會把反查表
    //    登錄成 candidate（即將被丟棄）的指標，導致之後 pick 沿用舊
    //    handle 的幾何時反查失敗，例如 GDIM 第二個選取點不到）。
    //    因此只暫時阻斷 rebuilt()，shapeChanged() 仍正常發出。
    m_suppressRebuiltSignal = true;
    const bool ok = rebuild();
    m_suppressRebuiltSignal = false;
    if (!ok) return false;

    // ── ③ Diff: for each new geometry, decide reuse-old vs display-new ──────
    QHash<QString, quint64> newFingerprints;
    bool anyChanged = false;

    for (int i = 0; i < m_aisShapes.size(); ++i) {
        const QString& uuid = m_aisShapeUuids[i];
        SketchGeometry* geom = findGeometry(uuid);
        const quint64 fp = geometryFingerprint(geom);
        newFingerprints.insert(uuid, fp);

        auto oldIt = oldByUuid.find(uuid);
        const bool existedBefore = (oldIt != oldByUuid.end());
        const bool unchanged = existedBefore
                                && oldFingerprints.value(uuid, ~0ULL) == fp;

        if (unchanged) {
            // Geometry's solved shape is identical to last solve — keep the
            // AIS object already displayed in the context untouched
            // (preserves selection/highlight state too), discard the
            // freshly-built duplicate from rebuild().
            m_aisShapes[i] = oldIt.value();
            continue;
        }

        anyChanged = true;

        if (existedBefore) {
            // Try in-place update for SketchPointAIS (no Erase/Display
            // churn, preserves selection state); fall back to swap for
            // wire-based shapes where in-place geometry mutation isn't
            // exposed.
            Handle(SketchPointAIS) oldPtAis = Handle(SketchPointAIS)::DownCast(oldIt.value());
            const auto* pt = (geom && geom->type == SketchGeometryType::Point)
                                ? static_cast<const SketchPoint*>(geom) : nullptr;

            if (!oldPtAis.IsNull() && pt) {
                oldPtAis->updatePosition(pt->pos);
                m_aisContext->Redisplay(oldPtAis, Standard_False);
                m_aisShapes[i] = oldPtAis;   // keep the old, now-updated handle
            } else {
                m_aisContext->Erase(oldIt.value(), Standard_False);
                m_aisContext->Display(m_aisShapes[i], Standard_False);
                if (Handle(SketchPointAIS)::DownCast(m_aisShapes[i]))
                    m_aisContext->Activate(m_aisShapes[i], 0, Standard_False);
                applyGeomSelectionSensitivity(m_aisContext, m_aisShapes[i]);
            }
        } else {
            // Brand-new geometry — just display it.
            m_aisContext->Display(m_aisShapes[i], Standard_False);
            if (Handle(SketchPointAIS)::DownCast(m_aisShapes[i]))
                m_aisContext->Activate(m_aisShapes[i], 0, Standard_False);
            applyGeomSelectionSensitivity(m_aisContext, m_aisShapes[i]);
        }
    }

    // Geometries that existed before but are gone now (deleted) — erase them.
    // ⚠️ 例外：SketchPointAIS 一律不透過這個「uuid 已不在最新列表」的
    // 捷徑判斷來 Erase。這個判斷式只是拿「這次 rebuild() 產生的
    // m_aisShapeUuids 有沒有這個 uuid」來猜測「幾何是否被刪除」，但
    // rebuild() 對 Point 的收錄邏輯（見上方 addPoint 分支：
    // `if (!geom->isConstruction()) { append... }`）在某些求解路徑下
    // 可能與呼叫當下的暫時狀態不同步，導致「明明沒被使用者刪除的點」
    // 也被這裡誤判成「已刪除」而 Erase 掉——這正是「雙擊編輯尺寸約束數值
    // 完成後 SketchPoint 被隱藏」的成因之一。SketchPoint 依需求必須永遠
    // 顯示，真正刪除幾何（例如刪除一條線連帶其端點）另有專屬的刪除指令
    // 路徑（見 EraseCommand），不依賴這裡的捷徑判斷，因此排除 SketchPointAIS
    // 不會影響「真的刪除幾何」時的清理。
    QSet<QString> currentUuids;
    currentUuids.reserve(m_aisShapeUuids.size());
    for (const QString& u : m_aisShapeUuids)
        currentUuids.insert(u);
    for (auto it = oldByUuid.begin(); it != oldByUuid.end(); ++it) {
        if (!currentUuids.contains(it.key())) {
            if (!Handle(SketchPointAIS)::DownCast(it.value()).IsNull())
                continue;   // SketchPoint 永遠顯示，不因這裡的捷徑判斷被隱藏
            anyChanged = true;
            m_aisContext->Erase(it.value(), Standard_False);
        }
    }

    m_geomFingerprints = newFingerprints;

    // ── Construction shapes: no stable per-shape identity is tracked today
    //    (rebuild() always allocates fresh handles for these), so erase the
    //    previous batch and redisplay the new one. These are typically a
    //    small, mostly-static subset (reference geometry), so this remains
    //    cheap relative to the main geometry diff above. ───────────────────
    for (const Handle(AIS_Shape)& s : oldConstructionShapes) {
        if (!s.IsNull()) {
            anyChanged = true;
            m_aisContext->Erase(s, Standard_False);
        }
    }
    for (const Handle(AIS_Shape)& s : m_constructionShapes) {
        if (!s.IsNull()) {
            anyChanged = true;
            m_aisContext->Display(s, Standard_False);
            // ⚠️ 修正：建構線／弧／圓除了不參與輪廓外，其他功能都必須與一般
            // 幾何相同——包含可被滑鼠選取，才能參與 TRIM/EXTEND/FILLET/
            // CHAMFER/MIRROR/ROTATE/MOVE/COPY/STRETCH/ERASE/GDIM 等命令。
            // 這裡原本會呼叫 context->Deactivate(s) 讓建構線「顯示但不可
            // 選」，是本次要修正的錯誤行為，故移除該呼叫——比照下面一般
            // 幾何（m_aisShapes 內的 AIS_Shape）的做法，Display() 之後不
            // 額外呼叫 Deactivate，即維持 AIS_Shape 預設可選取狀態。
        }
    }

    // ⚠️ 修正：兩條線 COLLINEAR（以及其他幾何約束：Parallel/Perpendicular/
    // Tangent/Concentric…）套用後 SketchPoint 消失。
    //
    // 根因：SketchPanel::onConstraintReadyFromSession() 對這類「雙幾何」
    // 約束的共用後處理，會在 constrainXxx()（內部已呼叫一次
    // addConstraint() → solveConstraints()）回傳後，*再*呼叫一次
    // m_sketch->solveConstraints()——形同同一次使用者操作觸發兩次求解。
    // 第一次 solveConstraints() 內的 markDirty() 會經由
    // rebuildRequested → Document::rebuildFeature() 完整重建並顯示一次
    // （該函式尾端已有「SketchPoint 永遠顯示」的保險迴圈），但第二次
    // solveConstraints() 走的是 rebuildShapesOnly() 這條「直接對 AIS
    // context 做 diff 後 Erase/Display」的路徑，並不經過
    // Document::rebuildFeature()，因此前面那道保險完全套用不到這裡。
    // 若 diff 過程中任何邊界情況（例如 m_geomFingerprints 尚未同步、
    // 或某點在兩次重建之間被判定成「已存在但需 swap」而非「in-place
    // update」）讓某個 SketchPointAIS 被 Erase 之後沒有接著被
    // Display/Activate，畫面上該點就會消失。
    //
    // 與其在上面的 diff 邏輯裡窮舉所有邊界情況，這裡直接在
    // rebuildShapesOnly() 結束前補上與 Document::rebuildFeature() 相同的
    // 最終保險：只要 context 有效，就無條件確保 m_aisShapes 裡所有
    // SketchPointAIS 都是「顯示＋可選取」狀態，滿足「SketchPoint 在
    // sketch edit 時永遠顯示」的需求。對已經正確顯示的點是無副作用的
    // no-op（Display/Activate 對已顯示/已啟用物件重複呼叫是安全的）。
    for (const auto& obj : m_aisShapes) {
        if (obj.IsNull()) continue;
        if (!Handle(SketchPointAIS)::DownCast(obj).IsNull()) {
            m_aisContext->Display(obj, Standard_False);
            m_aisContext->Activate(obj, 0, Standard_False);
            anyChanged = true;
        }
    }

    if (anyChanged)
        m_aisContext->UpdateCurrentViewer();

    // 此時 m_aisShapes 已是「最終」狀態（未變動的沿用舊 handle，
    // 變動的是新 handle）。現在才發出 rebuilt()，讓
    // CadView::onSketchRebuilt() 用正確的 pointer 集合重建
    // aisToFeatureId / aisToGeomUuid / aisToGeomIndex 反查表，
    // 避免 pick（例如 GDIM 第二個選取）打到未登錄的 handle。
    Q_EMIT rebuilt();

    return true;
}

// 加在 Sketch.cpp 匿名 namespace 或 private 方法中
void Sketch::applyConstructionStyle(Handle(AIS_Shape)& shape, GeomRole role) {
    shape->SetDisplayMode(AIS_WireFrame);

    switch (role) {
    case GeomRole::Construction:
        // 青色虛線，細線
        shape->SetColor(Quantity_NOC_CYAN1);
        shape->SetWidth(1.0);
        shape->Attributes()->WireAspect()->SetTypeOfLine(Aspect_TOL_DASH);
        break;

    case GeomRole::Centerline:
        // 橙色點鏈線，區別於一般建構線
        shape->SetColor(Quantity_NOC_ORANGE);
        shape->SetWidth(1.0);
        shape->Attributes()->WireAspect()->SetTypeOfLine(Aspect_TOL_DOTDASH);
        break;

    default:
        break;
    }
}

// ============================================================================
// Serialization
// ============================================================================

QJsonObject Sketch::toJson() const {
    QJsonObject json = Feature::toJson();

    if (m_plane) {
        json["planeId"]   = m_plane->id();
        json["planeName"] = m_plane->displayName();

        // ✅ 儲存完整平面幾何，確保 load 後能正確重建
        json["planeData"] = m_plane->toJson();
    } else {
        qWarning() << "[Sketch]" << name() << "has no plane when serializing";
    }

    QJsonArray geomsArray;
    for (const SketchGeometry* geom : m_geometries) {
        QJsonObject geomJson;
        geomJson["type"] = static_cast<int>(geom->type);        
        geomJson["role"] = static_cast<int>(geom->role);
        geomJson["uuid"] = geom->uuid;

        QJsonArray pointsArray;
        for (const QVector2D& pt : geom->points) {
            QJsonObject ptJson;
            ptJson["x"] = pt.x();
            ptJson["y"] = pt.y();
            pointsArray.append(ptJson);
        }
        geomJson["points"] = pointsArray;

        // ✅ Phase 0B：儲存各幾何的點 UUID 引用
        if (const auto* line = dynamic_cast<const SketchLine*>(geom)) {
            geomJson["startUuid"]  = line->startUuid;
            geomJson["endUuid"]    = line->endUuid;
        } else if (const auto* circle = dynamic_cast<const SketchCircle*>(geom)) {
            geomJson["centerUuid"] = circle->centerUuid;
        } else if (const auto* arc = dynamic_cast<const SketchArc*>(geom)) {
            geomJson["startUuid"]  = arc->startUuid;
            geomJson["endUuid"]    = arc->endUuid;
            geomJson["centerUuid"] = arc->centerUuid;
        } else if (const auto* pline = dynamic_cast<const SketchPolyline*>(geom)) {
            // Phase 0B：序列化各頂點 UUID
            QJsonArray vertexUuidsArray;
            for (const QString& uuid : pline->vertexUuids)
                vertexUuidsArray.append(uuid);
            geomJson["vertexUuids"] = vertexUuidsArray;
        } else if (const auto* splineG = dynamic_cast<const SketchSpline*>(geom)) {
            // Phase 0B：序列化各控制點 UUID
            QJsonArray cpUuidsArray;
            for (const QString& uuid : splineG->controlPointUuids)
                cpUuidsArray.append(uuid);
            geomJson["controlPointUuids"] = cpUuidsArray;
        } else if (const auto* ellipseG = dynamic_cast<const SketchEllipse*>(geom)) {
            // Phase 0B：序列化橢圓圓心 UUID
            geomJson["centerUuid"] = ellipseG->centerUuid;
        }

        // 各幾何類型的額外屬性
        switch (geom->type) {
        case SketchGeometryType::Point: {
            // ✅ Step 15: Point 序列化（origin + pos 已在 points[] 中，額外存 origin 欄位）
            const auto* pt = static_cast<const SketchPoint*>(geom);
            geomJson["px"]     = pt->pos.x();
            geomJson["py"]     = pt->pos.y();
            geomJson["origin"] = static_cast<int>(pt->origin);
            break;
        }
        case SketchGeometryType::Circle: {
            const SketchCircle* c = static_cast<const SketchCircle*>(geom);
            geomJson["radius"]  = c->radius;
            geomJson["centerX"] = c->center.x();
            geomJson["centerY"] = c->center.y();
            break;
        }
        case SketchGeometryType::Arc: {
            const SketchArc* a = static_cast<const SketchArc*>(geom);
            if (!a->curve.IsNull()) {
                // Extract start / mid / end from the OCCT trimmed curve
                double t0  = a->curve->FirstParameter();
                double t1  = a->curve->LastParameter();
                gp_Pnt gpS = a->curve->Value(t0);
                gp_Pnt gpM = a->curve->Value((t0 + t1) * 0.5);
                gp_Pnt gpE = a->curve->Value(t1);

                // Convert 3-D world → 2-D plane coords
                auto worldToPlane2D = [&](const gp_Pnt& p) -> QJsonObject {
                    QVector2D uv = m_plane->toPlane(
                        QVector3D(p.X(), p.Y(), p.Z()));
                    QJsonObject o;
                    o["x"] = uv.x();
                    o["y"] = uv.y();
                    return o;
                };

                geomJson["startPt"] = worldToPlane2D(gpS);
                geomJson["midPt"]   = worldToPlane2D(gpM);
                geomJson["endPt"]   = worldToPlane2D(gpE);
            }
            break;
        }
        case SketchGeometryType::Polyline: {
            const SketchPolyline* p = static_cast<const SketchPolyline*>(geom);
            geomJson["closed"] = p->closed;
            break;
        }
        case SketchGeometryType::Ellipse: {
            const SketchEllipse* e = static_cast<const SketchEllipse*>(geom);
            geomJson["centerX"]     = e->center.x();
            geomJson["centerY"]     = e->center.y();
            geomJson["majorRadius"] = e->majorRadius;
            geomJson["minorRadius"] = e->minorRadius;
            geomJson["angle"]       = e->angle;
            break;
        }
        case SketchGeometryType::Spline:
            // points already saved above
            break;
        default:
            break;
        }

        geomsArray.append(geomJson);
    }
    json["geometries"] = geomsArray;

    // 🔍 診斷用 log（追查「POINT 指令畫的獨立點存檔重開後消失」用）：報告
    // 這次 toJson() 實際寫出了幾個 Explicit 來源的點。若這個數字跟建立當下
    // PointCommand 印出的數字一致，代表寫檔這一步沒有遺漏，問題在載入端
    // （見 fromJson() 對應的 log）；若在這裡數字就已經對不上，代表點在
    // 「建立」與「存檔」這兩個時間點之間，於呼叫 toJson() 之前就已經從
    // m_geometries 消失了。
    {
        int explicitCount = 0;
        for (const SketchGeometry* g : m_geometries) {
            if (g->type == SketchGeometryType::Point &&
                static_cast<const SketchPoint*>(g)->origin == SketchPoint::Origin::Explicit) {
                ++explicitCount;
            }
        }
        qDebug() << "[Sketch]" << name() << "toJson(): wrote"
                  << geomsArray.size() << "geometries total,"
                  << explicitCount << "Explicit-origin point(s).";
    }

    // Phase 1 重構後：Point 已統一在 "geometries" 陣列中，不需要獨立儲存

    // GDIM v2 Phase 1：schemaVersion=2 起，"constraints" 只存「純約束」，
    // implicitOf 非空的隱含約束（由 SketchAnnotation 產生）排除在外，
    // 改由 "annotations" 陣列儲存、載入後由 addAnnotation() 重新生成，
    // 避免資料重複、也避免舊隱含約束值與標註值不同步。
    QJsonArray conArr;
    for (const auto& c : m_constraints) {
        if (!c.implicitOf.isEmpty()) continue;
        conArr.append(c.toJson());
    }
    json["constraints"] = conArr;

    QJsonArray annArr;
    for (const auto& a : m_annotations)
        annArr.append(a.toJson());
    json["annotations"]    = annArr;
    json["schemaVersion"]  = 2;

    // ✅ 儲存 Sketch 自身的參數（cant、H 等），否則重新載入後參數列表會消失
    if (m_parameterStore)
        json["parameters"] = m_parameterStore->toJson();

    // ⚠️ 第 17 項回報修正：X 軸/Y 軸/原點參考幾何本身（連同鎖住它們的
    // Fixed 約束）已經在上面的 geometries/constraints 陣列裡正常存檔，
    // 但 xAxisGeomUuid()/yAxisGeomUuid()/originPointUuid() 用來記住
    // 「已經建立過，不要重複建立」的快取欄位（m_xAxisGeomUuid 等）只存在
    // Sketch 記憶體內，本身沒有被存檔。如果不額外記錄，重新載入後這幾個
    // 快取欄位是空的，只要 GDIM 再次參考 X 軸/Y 軸/原點，就會誤判成
    // 「還沒建立過」，又建立一組全新的軸線/原點幾何——不但留下沒人使用
    // 的孤兒幾何，舊約束原本指向的那個軸線也不會被新建立的取代，兩者
    // 各自獨立，容易讓人誤以為「軸線會變動」。這裡把三個快取 UUID 一併
    // 存檔，重新載入後就能正確接續使用同一份既有的軸線/原點幾何。
    if (!m_xAxisGeomUuid.isEmpty())   json["xAxisGeomUuid"]   = m_xAxisGeomUuid;
    if (!m_yAxisGeomUuid.isEmpty())   json["yAxisGeomUuid"]   = m_yAxisGeomUuid;
    if (!m_originPointUuid.isEmpty()) json["originPointUuid"] = m_originPointUuid;

    return json;
}

bool Sketch::fromJson(const QJsonObject& json) {
    if (!Feature::fromJson(json)) {
        return false;
    }

    // ✅ blockSignals 涵蓋平面解析到幾何載入全程
    blockSignals(true);

    // ================================================================
    // ✅ 平面恢復：四階段 fallback
    //   1. 用 UUID 直接查 PlaneManager（同 session 重用時有效）
    //   2. 用 planeName 比對標準平面（XY / XZ / YZ / ZX）
    //   3. 用儲存的 planeData 幾何比對現有平面
    //   4. 用 planeData 重建新平面並向 PlaneManager 登記
    // ================================================================
    Plane* resolvedPlane = nullptr;

    const QString planeId   = json["planeId"].toString();
    const QString planeName = json["planeName"].toString();

    // ── 階段 1：UUID 查找 ────────────────────────────────────────────
    if (!planeId.isEmpty()) {
        resolvedPlane = PlaneManager::instance()->getPlane(planeId);
        if (resolvedPlane) {
            qDebug() << "[Sketch]" << name()
                     << "Plane resolved by UUID:" << resolvedPlane->displayName();
        }
    }

    // ── 階段 2：標準平面名稱比對 ────────────────────────────────────
    if (!resolvedPlane && !planeName.isEmpty()) {
        resolvedPlane = resolveStandardPlane(planeName);
        if (resolvedPlane) {
            qDebug() << "[Sketch]" << name()
                     << "Plane resolved by name '" << planeName
                     << "':" << resolvedPlane->displayName();
        }
    }

    // ── 階段 3 & 4：從儲存的幾何資料恢復 ───────────────────────────
    if (!resolvedPlane && json.contains("planeData")) {
        resolvedPlane = reconstructPlaneFromJson(json["planeData"].toObject(), planeName);
        if (resolvedPlane) {
            qDebug() << "[Sketch]" << name()
                     << "Plane reconstructed from planeData:" << resolvedPlane->displayName();
        }
    }


    // ── 最終 fallback ────────────────────────────────────────────────
    if (resolvedPlane) {
        setPlane(resolvedPlane);
    } else {
        qWarning() << "[Sketch]" << name()
                   << "All plane resolution strategies failed, using default XY plane."
                   << "planeId=" << planeId << "planeName=" << planeName;
        createDefaultPlane();
    }

    // ================================================================
    // 載入幾何元素（完整版：含 Line / Polyline / Circle /
    //                         Spline / Arc(跳過) / Ellipse）
    // ================================================================
    clearGeometry();

    // ✅ Phase 0B：先還原 SketchPoint（幾何需要 UUID 引用）
    if (json.contains("sketchPoints")) {
        // 舊格式：SketchPoints 在獨立陣列中
        for (const QJsonValue& v : json["sketchPoints"].toArray()) {
            QJsonObject ptJson = v.toObject();
            QString uuid = ptJson["uuid"].toString();
            QVector2D pos(ptJson["x"].toDouble(), ptJson["y"].toDouble());
            SketchPoint::Origin origin =
                static_cast<SketchPoint::Origin>(ptJson["origin"].toInt(
                    static_cast<int>(SketchPoint::Origin::Endpoint)));
            auto* pt = new SketchPoint(pos, origin);
            pt->uuid = uuid;
            m_geometries.append(pt);
            m_uuidToGeomIndex[uuid] = m_geometries.size() - 1;
        }
        qDebug() << "[Sketch]" << name() << "Loaded"
                 << points().size() << "SketchPoints (legacy format)";
    }

    if (json.contains("geometries")) {
        QJsonArray geomsArray = json["geometries"].toArray();

        // ── 第一遍：只載入 Point（曲線需要先有 UUID 才能引用）────────
        for (const QJsonValue& val : geomsArray) {
            QJsonObject geomJson = val.toObject();
            SketchGeometryType type =
                static_cast<SketchGeometryType>(geomJson["type"].toInt());
            if (type != SketchGeometryType::Point) continue;

            QString uuid = geomJson["uuid"].toString();
            if (uuid.isEmpty() || findGeometry(uuid)) continue;  // 已由 legacy 載入

            QVector2D pos(geomJson["px"].toDouble(), geomJson["py"].toDouble());
            SketchPoint::Origin origin =
                static_cast<SketchPoint::Origin>(geomJson["origin"].toInt(
                    static_cast<int>(SketchPoint::Origin::Endpoint)));
            auto* pt = new SketchPoint(pos, origin);
            pt->uuid = uuid;
            pt->role = static_cast<GeomRole>(geomJson["role"].toInt());
            m_geometries.append(pt);
            m_uuidToGeomIndex[uuid] = m_geometries.size() - 1;
        }

        // 🔍 診斷用 log（追查「POINT 指令畫的獨立點存檔重開後消失」用）：
        // 報告第一遍實際載入了幾個 Explicit 來源的點。若這個數字比存檔當下
        // Sketch::toJson() 印出的數字少，代表問題出在載入這一步（例如
        // geomsArray 裡的資料沒被正確讀到、或被上面的 findGeometry(uuid)
        // 判斷誤判成「已載入」而跳過）；若數字一致，代表點有正確載入到
        // m_geometries，問題出在載入「之後」的某個顯示/清理步驟。
        {
            int explicitCount = 0;
            for (const SketchGeometry* g : m_geometries) {
                if (g->type == SketchGeometryType::Point &&
                    static_cast<const SketchPoint*>(g)->origin == SketchPoint::Origin::Explicit) {
                    ++explicitCount;
                }
            }
            qDebug() << "[Sketch]" << name() << "fromJson(): first pass loaded"
                      << explicitCount << "Explicit-origin point(s),"
                      << m_geometries.size() << "geometries total so far.";
        }

        // ── 第二遍：載入曲線（可安全引用已存在的 Point UUID）──────────
        for (const QJsonValue& val : geomsArray) {
            QJsonObject geomJson = val.toObject();
            SketchGeometryType type =
                static_cast<SketchGeometryType>(geomJson["type"].toInt());
            if (type == SketchGeometryType::Point) continue;  // 已在第一遍處理

            // 通用：讀取點陣列
            QVector<QVector2D> points;
            QJsonArray pointsArray = geomJson["points"].toArray();
            for (const QJsonValue& ptVal : pointsArray) {
                QJsonObject ptJson = ptVal.toObject();
                points.append(QVector2D(ptJson["x"].toDouble(),
                                        ptJson["y"].toDouble()));
            }

            switch (type) {
            case SketchGeometryType::Line:
                if (points.size() >= 2) {
                    // ✅ Phase 0B：傳遞儲存的 UUID，避免重建新 SketchPoint
                    QString startUuid = geomJson["startUuid"].toString();
                    QString endUuid   = geomJson["endUuid"].toString();
                    // 若舊格式無 uuid（legacy），addLineGeom 會自動建立點
                    addLineGeom(points[0], points[1], startUuid, endUuid);
                }
                // 通用：在每個 case 的 addXxx() 之後加：
                if (!m_geometries.isEmpty() && geomJson.contains("uuid"))
                    m_geometries.last()->uuid = geomJson["uuid"].toString();
                if (geomJson.contains("role") && !m_geometries.isEmpty())
                    m_geometries.last()->role = static_cast<GeomRole>(geomJson["role"].toInt());
                break;
            case SketchGeometryType::Polyline: {
                bool closed = geomJson["closed"].toBool(false);
                if (points.size() >= 2) {
                    // ✅ Phase 0B：傳入已儲存的 vertexUuids 重用已載入的 SketchPoint
                    QVector<QString> savedVertexUuids;
                    if (geomJson.contains("vertexUuids")) {
                        for (const auto& v : geomJson["vertexUuids"].toArray())
                            savedVertexUuids.append(v.toString());
                    }
                    addPolylineGeom(points, closed, savedVertexUuids);
                }
                if (!m_geometries.isEmpty() && geomJson.contains("uuid"))
                    m_geometries.last()->uuid = geomJson["uuid"].toString();
                if (geomJson.contains("role") && !m_geometries.isEmpty())
                    m_geometries.last()->role = static_cast<GeomRole>(geomJson["role"].toInt());
                break;
            }

            case SketchGeometryType::Circle: {
                QVector2D center(geomJson["centerX"].toDouble(),
                                 geomJson["centerY"].toDouble());
                double radius = geomJson["radius"].toDouble();
                if (radius > 0.0) {
                    // ✅ Phase 0B：傳入已儲存的 centerUuid 重用已載入的 SketchPoint
                    QString savedCenterUuid = geomJson["centerUuid"].toString();
                    addCircleGeom(center, radius, savedCenterUuid);
                }
                if (!m_geometries.isEmpty() && geomJson.contains("uuid"))
                    m_geometries.last()->uuid = geomJson["uuid"].toString();
                if (geomJson.contains("role") && !m_geometries.isEmpty())
                    m_geometries.last()->role = static_cast<GeomRole>(geomJson["role"].toInt());
                break;
            }

            case SketchGeometryType::Spline:
                if (points.size() >= 3) {
                    // ✅ Phase 0B：傳入已儲存的 controlPointUuids 重用已載入的 SketchPoint
                    QVector<QString> savedCpUuids;
                    if (geomJson.contains("controlPointUuids")) {
                        for (const auto& v : geomJson["controlPointUuids"].toArray())
                            savedCpUuids.append(v.toString());
                    }
                    addSplineGeom(points, savedCpUuids);
                } else {
                    qWarning() << "[Sketch]" << name()
                               << "Spline skipped: need >= 3 points, got" << points.size();
                }
                if (!m_geometries.isEmpty() && geomJson.contains("uuid"))
                    m_geometries.last()->uuid = geomJson["uuid"].toString();
                if (geomJson.contains("role") && !m_geometries.isEmpty())
                    m_geometries.last()->role = static_cast<GeomRole>(geomJson["role"].toInt());
                break;

            case SketchGeometryType::Arc: {
                auto readPt = [&](const QString& key) -> QVector2D {
                    QJsonObject o = geomJson[key].toObject();
                    return QVector2D(o["x"].toDouble(), o["y"].toDouble());
                };

                // ✅ Phase 0B：傳入已儲存的 UUID 重用已載入的 SketchPoint
                QString savedStartUuid  = geomJson["startUuid"].toString();
                QString savedEndUuid    = geomJson["endUuid"].toString();
                QString savedCenterUuid = geomJson["centerUuid"].toString();

                if (geomJson.contains("startPt") &&
                    geomJson.contains("midPt")   &&
                    geomJson.contains("endPt")) {
                    // ── 修正：Arc 起終點與 SketchPoint 座標不一致（load 後即
                    // 存在，而非求解才造成） ────────────────────────────────
                    // Arc 存檔時（見 toJson）是直接從 a->curve 取端點座標寫入
                    // startPt/midPt/endPt；但同一個 Arc 的 startUuid/endUuid
                    // 對應的 SketchPoint，是各自以自己的 px/py 獨立存檔、獨立
                    // 載入的（"geometries" 陣列裡的 Point 條目一定排在 Arc 之
                    // 前，此時已載入完成）。這兩者理論上應該完全一致，但只要
                    // 存檔當下 a->curve 與該 SketchPoint 曾經有過任何不同步
                    // （例如舊版本 bug、或某次編輯只 movePoint() 更新了點卻還
                    // 沒來得及跑 solveConstraints() 重建 curve 就存檔），這個
                    // 不一致就會被原封不動寫進檔案、下次載入時原封不動重現，
                    // 且此時 startPt/midPt/endPt 對應的位置可能相差甚遠（不只
                    // 是浮點誤差，實測發現過整個弧被「繞著圓心轉了幾十度」的
                    // 落差），要等到之後任何一次 solveConstraints() 執行，才
                    // 會把 curve「拉回」去跟 SketchPoint 對齊，使用者就會看到
                    // 圓弧突然跳動、跟被圓角的線斷開。
                    //
                    // 修法：載入時，若 startUuid/endUuid/centerUuid 都已經對應
                    // 到「現在」載入好的 SketchPoint（這才是連接關係的唯一真相
                    // 來源，跟 Line 的 start/end 完全比照），就直接用這些
                    // SketchPoint 目前的座標取代 startPt/endPt 來重建 curve，
                    // 而不是用可能過時的、Arc 自己另外快取的 startPt/endPt。
                    //
                    // 但 GC_MakeArcOfCircle 需要「三個彼此一致」的點才能正確
                    // 決定圓弧要走哪一段弧（優弧/劣弧、順時針/逆時針），若只
                    // 換掉 startPt/endPt、留著過時的 midPt，三點可能不再落在
                    // 同一段弧上，輕則重建出方向錯誤的弧，重則 GC_MakeArcOf-
                    // Circle 直接建構失敗。這裡改用「訊號化總掃角」重建 midPt：
                    // 從存檔當下的 startPt→midPt→endPt 算出一個逆時針為正的訊
                    // 號化總掃角（此角度只跟三點的相對關係有關，即使三點被整
                    // 體繞圓心轉了任意角度也不會變，正好符合這種「同一個圓、
                    // 整組同步跑掉」的錯誤模式），再套用到修正後的起點角度
                    // 上，算出跟修正後終點吻合、方向與優劣弧都正確的新中點。
                    QVector2D startPt = readPt("startPt");
                    QVector2D endPt   = readPt("endPt");
                    QVector2D midPt   = readPt("midPt");

                    SketchPoint* spStart  = savedStartUuid.isEmpty()  ? nullptr : point(savedStartUuid);
                    SketchPoint* spEnd    = savedEndUuid.isEmpty()    ? nullptr : point(savedEndUuid);
                    SketchPoint* spCenter = savedCenterUuid.isEmpty() ? nullptr : point(savedCenterUuid);

                    if (spStart && spEnd && spCenter &&
                        geomJson.contains("startPt") && geomJson.contains("midPt") &&
                        geomJson.contains("endPt")) {
                        const QVector2D center      = spCenter->pos;
                        const QVector2D staleStart  = startPt;   // 存檔當下的 curve 端點（可能過時）
                        const QVector2D staleMid    = midPt;
                        const QVector2D staleEnd    = endPt;
                        const QVector2D newStart    = spStart->pos;   // SketchPoint 目前（權威）座標
                        const QVector2D newEnd      = spEnd->pos;

                        auto angleDeg = [](const QVector2D& c, const QVector2D& p) -> double {
                            return qRadiansToDegrees(
                                std::atan2(double(p.y() - c.y()), double(p.x() - c.x())));
                        };
                        auto norm360 = [](double a) -> double {
                            while (a < 0.0)    a += 360.0;
                            while (a >= 360.0) a -= 360.0;
                            return a;
                        };

                        const double staleStartDeg = angleDeg(center, staleStart);
                        const double dMidCCW = norm360(angleDeg(center, staleMid) - staleStartDeg);
                        const double dEndCCW = norm360(angleDeg(center, staleEnd) - staleStartDeg);
                        // 逆時針為正的訊號化總掃角：mid 若落在 start→end 的逆
                        // 時針短邊之間，掃角就是 dEndCCW（劣弧或優弧皆可能，
                        // 由 dEndCCW 本身大小決定）；否則代表實際是走另一個方
                        // 向（順時針），掃角以負值表示。
                        const double sweepDeg = (dMidCCW > 0.0 && dMidCCW < dEndCCW)
                                                ? dEndCCW
                                                : -(360.0 - dEndCCW);

                        const double r = double((newStart - center).length());
                        if (r > 1e-9) {
                            const double newStartDeg = angleDeg(center, newStart);
                            const double newMidRad = qDegreesToRadians(newStartDeg + sweepDeg * 0.5);
                            startPt = newStart;
                            endPt   = newEnd;
                            midPt   = QVector2D(float(center.x() + r * std::cos(newMidRad)),
                                                 float(center.y() + r * std::sin(newMidRad)));
                        }
                        // r 過小（起點與圓心幾乎重合）代表資料本身已退化，這種
                        // 情況維持使用原始存檔值，避免除以近似 0 的半徑放大誤差。
                    }

                    addArcGeom(startPt, midPt, endPt,
                               savedStartUuid, savedEndUuid, savedCenterUuid);
                } else if (points.size() >= 3) {
                    // legacy fallback
                    addArcGeom(points[0], points[1], points[2],
                               savedStartUuid, savedEndUuid, savedCenterUuid);
                } else {
                    qWarning() << "[Sketch]" << name()
                               << "Arc skipped: no key points in JSON";
                }
                if (!m_geometries.isEmpty() && geomJson.contains("uuid"))
                    m_geometries.last()->uuid = geomJson["uuid"].toString();
                if (geomJson.contains("role") && !m_geometries.isEmpty())
                    m_geometries.last()->role = static_cast<GeomRole>(geomJson["role"].toInt());
                break;
            }
            case SketchGeometryType::Ellipse: {
                QVector2D center(geomJson["centerX"].toDouble(),
                                 geomJson["centerY"].toDouble());
                double major = geomJson["majorRadius"].toDouble();
                double minor = geomJson["minorRadius"].toDouble();
                double angle = geomJson["angle"].toDouble(0.0);
                if (major > 0.0 && minor > 0.0) {
                    // ✅ Phase 0B：傳入已儲存的 centerUuid 重用已載入的 SketchPoint
                    QString savedCenterUuid = geomJson["centerUuid"].toString();
                    addEllipseGeom(center, major, minor, angle, savedCenterUuid);
                }
                if (!m_geometries.isEmpty() && geomJson.contains("uuid"))
                    m_geometries.last()->uuid = geomJson["uuid"].toString();
                if (geomJson.contains("role") && !m_geometries.isEmpty())
                    m_geometries.last()->role = static_cast<GeomRole>(geomJson["role"].toInt());
                break;
            }

            default:
                qWarning() << "[Sketch]" << name()
                           << "Unknown geometry type:" << static_cast<int>(type);
                break;
            }
        }  // 第二遍 for loop
    }  // json.contains("geometries")


    qDebug() << "[Sketch]" << name() << "fromJson complete:"
             << m_geometries.size() << "geometries on plane"
             << (m_plane ? m_plane->displayName() : "NULL");

    m_constraints.clear();
    if (json.contains("constraints")) {
        for (const QJsonValue& v : json["constraints"].toArray())
            m_constraints.append(SketchConstraint::fromJson(v.toObject()));
    }

    // GDIM v2 Phase 1：讀取標註（schemaVersion >= 2）。
    // 舊檔（無 "annotations" 欄位、schemaVersion 缺省視為 1）目前尚無自動
    // 遷移路徑內嵌於此處——請改用隨附的一次性腳本
    // tools/migrate_gdim_schema_v2.py 離線轉檔（見該腳本說明），
    // 避免在 runtime 為每個舊欄位寫 fallback 判斷式。
    m_annotations.clear();
    if (json.contains("annotations")) {
        for (const QJsonValue& v : json["annotations"].toArray())
            m_annotations.append(SketchAnnotation::fromJson(v.toObject()));
        // 依標註內容重新生成隱含約束（存檔時已被排除，避免與標註值不同步）
        for (const auto& a : m_annotations)
            addImplicitConstraint(a);
    }

    // ✅ 還原 Sketch 自身的參數（cant、H 等），確保重新載入後參數列表正常顯示
    if (json.contains("parameters")) {
        parameterStore()->fromJson(json["parameters"].toObject());
    }

    // ⚠️ 第 17 項回報修正：還原 X 軸/Y 軸/原點參考幾何的快取 UUID（見
    // toJson() 對應位置的說明）。只有在該 UUID 對應的幾何確實還存在於
    // m_geometries 時才採信，避免存檔格式被手動修改或損毀時，讓
    // xAxisGeomUuid() 之後對著一個不存在的 UUID 誤判為「已建立」。
    // 找不到就保持空字串，之後第一次真的需要時會照舊自動建立一份。
    auto restoreCachedAxisUuid = [this, &json](const char* key) -> QString {
        const QString uuid = json.value(key).toString();
        return (!uuid.isEmpty() && findGeometry(uuid)) ? uuid : QString();
    };
    m_xAxisGeomUuid   = restoreCachedAxisUuid("xAxisGeomUuid");
    m_yAxisGeomUuid   = restoreCachedAxisUuid("yAxisGeomUuid");
    m_originPointUuid = restoreCachedAxisUuid("originPointUuid");

    blockSignals(false);
    // ✅ 載入完畢後只 emit 一次
//    Q_EMIT geometryChanged();

    // ✅ 新增：載入完後求解一次，使幾何符合約束
    if (!m_constraints.isEmpty())
        solveConstraints();

    // ✅ 載入幾何後必須 rebuild 一次，讓 m_wires / shape() 有效，
    //    否則後續 Extrude::rebuild() 的 hasValidShape() 檢查會失敗
    rebuild();

    return true;
}

// ── 加入約束 ─────────────────────────────────────────────────────────────
QString Sketch::addConstraint(const SketchConstraint& c, bool solve) {
    // 驗證參考的幾何是否存在
    for (const GeomRef& ref : c.refs) {
        bool found = std::any_of(m_geometries.begin(), m_geometries.end(),
                                 [&](const SketchGeometry* g){ return g->uuid == ref.geomUuid; });
        if (!found) {
            qWarning() << "[Sketch] addConstraint: geom not found:" << ref.geomUuid;
            return {};
        }
    }
    m_constraints.append(c);
    Q_EMIT constraintAdded(c.uuid);
    // 加入約束後立即嘗試求解——solve=false 時跳過，留給呼叫端在整批約束
    // 都加完後自己統一呼叫一次 solveConstraints()（見 Sketch.h 的參數
    // 說明）。
    if (solve) solveConstraints();
    return c.uuid;
}

bool Sketch::removeConstraintInternal(const QString& uuid) {
    for (int i=0; i<m_constraints.size(); ++i) {
        if (m_constraints[i].uuid == uuid) {
            m_constraints.removeAt(i);
            return true;
        }
    }
    return false;
}

bool Sketch::removeConstraint(const QString& uuid) {
    if (removeConstraintInternal(uuid)) {
        Q_EMIT constraintRemoved(uuid);
        solveConstraints();
        return true;
    }
    return false;
}

int Sketch::removeMany(const QStringList& uuids) {
    int removedCount = 0;
    bool anyGeometryRemoved   = false;
    bool anyConstraintRemoved = false;

    for (const QString& uuid : uuids) {
        if (uuid.isEmpty()) continue;

        // 先當作一般幾何處理（比照 EraseCommand.cpp 內原 eraseOne() 的既有
        // 邏輯：先試幾何、找不到再試約束——例如點擊尺寸線文字選到的是約束
        // UUID）。
        bool found = false;
        for (int i = 0; i < m_geometries.size(); ++i) {
            if (m_geometries[i]->uuid == uuid) {
                removeGeometryAt(i);
                found = true;
                anyGeometryRemoved = true;
                break;
            }
        }
        if (!found) {
            // ⚠️ 修正（第 7 項回報：刪除尺寸約束，相關部分／存檔沒有同步刪除）：
            // 使用者在畫面上點掉的 GDIM 尺寸線，其 AIS 掛的 uuid 其實是「隱含
            // 約束」的 uuid（ConstraintOverlayManager::createSymbolFor() 用
            // `m_dimLines[c.uuid] = dim;` 建立，這個 c 是 addImplicitConstraint()
            // 依 SketchAnnotation 自動產生、存在 m_constraints 裡的隱含約束，
            // 並非標註本身）。若這裡只把它當一般約束移除，只會刪掉這個隱含
            // 約束，產生它的 SketchAnnotation（連同 Prefix/Suffix/公差/
            // Basic/Inspection 等額外資料）仍然留在 m_annotations 裡沒有被
            // 清掉：畫面上尺寸線因為隱含約束沒了而跟著消失，看起來像是刪除
            // 成功，但存檔時那筆標註還是會被寫回 JSON 的 annotations 陣列，
            // 重新載入後 fromJson() 又會依標註內容重新產生一次隱含約束，讓
            // 已經刪除的尺寸「復活」。批次刪除（removeMany）繞過了原本單一
            // 刪除路徑 eraseOne() 的判斷，故修正需下沉到這裡，與單一刪除
            // 路徑共用同一邏輯。
            //
            // 正確作法（也是 Sketch.h 對 SketchAnnotation API 的既有設計
            // 原則：「SketchAnnotation 是標註的唯一對外資料來源」）：先判斷
            // 這個 uuid 是不是某個標註的隱含約束，是的話改呼叫
            // removeAnnotation()——它會同時清掉標註本身與其隱含約束（並自行
            // Q_EMIT annotationRemoved()／solveConstraints()，非幾何/一般
            // 約束批次延後通知的最佳化在這條路徑上不適用，維持與原
            // eraseOne() 相同的即時行為）；否則才當成一般（非隱含）約束
            // 處理，沿用批次延後 solveConstraints() 的最佳化。
            if (SketchConstraint* c = findConstraint(uuid)) {
                if (!c->implicitOf.isEmpty()) {
                    if (removeAnnotation(c->implicitOf)) {
                        found = true;
                        // removeAnnotation() 已自行處理訊號與求解，不計入
                        // anyGeometryRemoved/anyConstraintRemoved 的延後通知。
                    }
                } else if (removeConstraintInternal(uuid)) {
                    found = true;
                    anyConstraintRemoved = true;
                    // 單一移除時 removeConstraint() 會逐一 Q_EMIT
                    // constraintRemoved()，讓 ConstraintOverlayManager 即時
                    // 移除對應圖示；這裡沿用同樣的 per-uuid 訊號（訊號本身
                    // 很輕量，不是造成「刪一批重繪 N 次」的元兇——真正昂貴的
                    // rebuildRequested()／solveConstraints() 才延後到整批
                    // 結束後只做一次，見下方）。
                    Q_EMIT constraintRemoved(uuid);
                }
            }
            // 保險：uuid 也可能直接就是標註自己的 uuid（例如 driving 恆為
            // false、不會產生隱含約束的 LeaderNote，只能靠標註自己的 uuid
            // 被選取/刪除）。
            if (!found && removeAnnotation(uuid)) {
                found = true;
            }
        }

        if (found) ++removedCount;
    }

    // ★ 整批刪除完畢後才統一通知一次，取代逐一刪除時各自 Q_EMIT
    //   geometryChanged()／rebuildRequested()／呼叫 solveConstraints()
    //   （rebuildRequested 掛在 Document 的完整 Feature 重建上，見
    //   Document.cpp connect(feature, &Feature::rebuildRequested, ...)）——
    //   把「刪一個重繪一次」改成「刪一批只重繪一次」。
    if (anyGeometryRemoved) {
        Q_EMIT geometryChanged();
        Q_EMIT rebuildRequested();
    } else if (anyConstraintRemoved) {
        // 只刪了約束、沒有動到幾何：不需要整個 Feature 重建，跑一次
        // solveConstraints()（其內部 rebuildShapesOnly() 只會對真的有變的
        // shape 做 Erase/Display）即可，比照原本 removeConstraint() 單獨
        // 呼叫時的行為。
        solveConstraints();
    }

    return removedCount;
}

SketchConstraint* Sketch::findConstraint(const QString& uuid)
{
    for (SketchConstraint& c : m_constraints) {
        if (c.uuid == uuid)
            return &c;
    }
    return nullptr;
}

std::optional<double> Sketch::previewAngleValueForOffset(const SketchConstraint* c,
                                                          double offsetX, double offsetY) const
{
    if (!c) return std::nullopt;
    if (c->type != ConstraintType::FixedAngle && c->type != ConstraintType::FixedAngleDim)
        return std::nullopt;
    if (c->refs.size() < 2) return std::nullopt;
    auto* lineA = dynamic_cast<SketchLine*>(findGeometry(c->refs[0].geomUuid));
    auto* lineB = dynamic_cast<SketchLine*>(findGeometry(c->refs[1].geomUuid));
    if (!lineA || !lineB) return std::nullopt;
    QVector2D dirA = (lineA->end - lineA->start).normalized();
    QVector2D dirB = (lineB->end - lineB->start).normalized();
    QVector2D offset(static_cast<float>(offsetX), static_cast<float>(offsetY));
    return geom2d::angleSectorValue(dirA, dirB, offset);
}

bool Sketch::previewConstraintDimValue(const QString& uuid, double offsetX, double offsetY)
{
    // ★ 新增（第 11 項回報：拖曳「既有」角度標註時，數值要比照 hover 預覽
    //   一樣即時更新，不要等放開滑鼠才變——CadView::dimLineDragging 這個
    //   訊號在拖曳過程中每次滑鼠移動都會發，之前只接去單純的
    //   ConstraintOverlayManager::updateDimLine()（只搬位置，不碰數值），
    //   數值只有在 dimLineDragFinished（放開滑鼠）才透過
    //   updateConstraintDimOffset() 重算一次——這中間拖曳的過程，畫面上
    //   弧的位置雖然跟著滑鼠即時移動，但標籤文字的數字其實是舊的，直到
    //   放開才跳一次，跟建立新標註時「滑鼠移到哪，數字就即時跟著換」的
    //   體驗不一致。
    //
    //   這個函式只做「輕量」預覽：只更新 SketchConstraint::value（讓
    //   labelText() 讀到新數字），不呼叫 solveConstraints()（拖曳中每次
    //   滑鼠移動都真的重新解一次幾何太重，而且線的實際位置本來就不該在
    //   放開滑鼠前被移動——跟建立時的 hover 預覽只是「預覽數值」、不會
    //   真的移動任何幾何是同一個道理）、也不同步 SketchAnnotation／
    //   paramExpr（那些是「確定套用」才需要處理的持久化細節，見
    //   updateConstraintDimOffset() 的完整說明）。真正落地生根、觸發求解
    //   跟存檔同步，還是要靠放開滑鼠時呼叫的 updateConstraintDimOffset()。
    SketchConstraint* c = findConstraint(uuid);
    if (!c) return false;
    if (c->type != ConstraintType::FixedAngle && c->type != ConstraintType::FixedAngleDim)
        return false;   // 非角度型別：拖曳不改變「量測的是什麼」，不需要預覽數值
    if (auto v = previewAngleValueForOffset(c, offsetX, offsetY))
        c->value = *v;
    return true;
}

bool Sketch::updateConstraintDimOffset(const QString& uuid, double offsetX, double offsetY)
{
    SketchConstraint* c = findConstraint(uuid);
    if (!c) return false;
    c->dimLineOffsetX = offsetX;
    c->dimLineOffsetY = offsetY;

    // ★ 修正（第 7 項回報：角度約束拖到不同位置時，值應該跟著調整才
    //   對——原本這裡只更新了 dimLineOffsetX/Y，位置變了、畫面上的弧也
    //   跟著移動，但約束實際要求解算的角度值完全沒變，變成「畫的位置」
    //   跟「真正在約束的角度」不一致）。角度是特例：對一般距離/座標尺寸
    //   而言，拖曳只是重新擺放標籤位置，不影響「量測的是什麼」；但兩條
    //   線交叉會分出 4 個扇區，量測的可能是夾角本身、也可能是補角，拖到
    //   不同扇區代表使用者想量測的目標本來就不一樣，不能只搬標籤不改值。
    //   用 previewAngleValueForOffset()（跟上面 previewConstraintDimValue()
    //   共用同一份，也是跟 GeneralDimCommand 建立時同一套
    //   geom2d::angleSectorValue() 邏輯，見該函式說明）依新的偏移方向重新
    //   判斷該用夾角還是補角。
    if (c->type == ConstraintType::FixedAngle || c->type == ConstraintType::FixedAngleDim) {
        if (auto v = previewAngleValueForOffset(c, offsetX, offsetY)) {
            c->value = *v;
            c->paramExpr.clear();
            // 同步 SketchAnnotation，理由跟下面「第 9 項」那段一致：
            // 隱含約束在任何重新生成的時機（存檔重載、標註重新 add）
            // 都會被 SketchAnnotation::value/paramExpr 覆蓋回去，這裡
            // 剛算好、剛拖出來的新角度不同步寫回的話會悄悄被還原。
            // makeAngleDim() 的 angleRad 參數本來就是弧度，跟 c->value
            // 單位一致，不需要再轉換。
            if (SketchAnnotation* ann = findAnnotation(uuid)) {
                ann->value = *v;
                ann->paramExpr.clear();
            }
        }
    }

    // ⚠️ 修正（第 9 項回報：尺寸約束移動調整後，位置沒有存檔）：
    // GDIM 尺寸線對應的是 SketchAnnotation 產生的「隱含約束」，兩者刻意
    // 共用同一個 uuid（見 SketchAnnotation::toImplicitConstraint()：
    // 「c.uuid = uuid; // 與標註同 uuid，方便 Sketch 端一對一同步」）。
    // 但隱含約束本身在 Sketch::toJson() 存檔時並不會被序列化進
    // "constraints" 陣列（避免與標註值不同步，見 addImplicitConstraint()
    // 說明），尺寸線偏移實際存檔／重新載入靠的是
    // SketchAnnotation::dimLineOffset（JSON 的 "dimOffX"/"dimOffY"）。
    // 上面只更新了隱含約束自己的 dimLineOffsetX/Y，若沒有同步寫回對應的
    // SketchAnnotation，使用者拖曳調整的位置只在「這次執行期間」有效，
    // 存檔／重新載入後又會被標註裡的舊值蓋掉（fromJson() → addAnnotation()
    // → addImplicitConstraint() 會用 SketchAnnotation::dimLineOffset 重新
    // 產生隱含約束，覆蓋掉這裡剛改好的值）——正是回報的現象。
    if (SketchAnnotation* ann = findAnnotation(uuid))
        ann->dimLineOffset = QVector2D(offsetX, offsetY);
    // ★ 修正：搬動尺寸約束只是調整它的位置／重新判斷該標哪個角度值，不用
    // 再觸發 solveConstraints()——跟其他型別的尺寸線拖曳（純粹重新擺放
    // 標籤位置）待遇一致，不應該因為拖了角度標註就連帶讓兩條線的實際幾何
    // 跟著轉動。上面已經把新的目標角度寫進 c->value（供下一次真正求解時
    // 使用）並同步進 SketchAnnotation（供存檔／重載使用），這裡不需要、
    // 也不應該立刻叫求解器把幾何轉到新角度。
    return true;
}

bool Sketch::computeDistanceSide(const SketchConstraint& c,
                                 double& sign, double& dirX, double& dirY) const
{
    // 與 ConstraintSolver.cpp 的 FixedDistanceEquation::lockReference()
    // 算法一致，只是這裡直接讀 SketchGeometry 物件（給求解後的同步、以及
    // flipDistanceSide() 共用），不是求解器內部扁平的 vars 向量。
    auto perpDist = [](const QVector2D& p, const QVector2D& l1,
                       const QVector2D& l2, double& outLen) -> double {
        QVector2D d = l2 - l1;
        outLen = d.length();
        if (outLen < 1e-10) return 0.0;
        return (double(p.x()-l1.x())*d.y() - double(p.y()-l1.y())*d.x()) / outLen;
    };

    switch (c.distMode) {
    case DistanceMode::PointToLine: {
        SketchPoint* p  = point(c.refs[0].geomUuid);
        auto* ln = dynamic_cast<SketchLine*>(findGeometry(c.refs[1].geomUuid));
        if (!p || !ln) return false;
        double len;
        double signedDist = perpDist(p->pos, ln->start, ln->end, len);
        // ⚠️ 修正（cant 經過 0 後，記住的翻轉方向被雜訊蓋掉）：這裡原本
        // 檢查的是 len（參考線本身的長度，perpDist() 內部已經用它防止
        // 除以 0），跟「這個點目前離線有多近」是兩件事。應該檢查的是
        // signedDist（點到線的實際距離）——當尺寸目標值（例如
        // d3=if(cant>0,0,abs(cant))）剛好是 0 或非常接近 0 時，點會被解算
        // 到幾乎正好落在線上，這時候 signedDist 的正負號只是浮點數雜訊，
        // 不能拿來覆寫已經記住的方向（否則使用者辛苦按過的「翻轉方向」
        // 選擇，會在下一次尺寸值經過 0 附近時被雜訊悄悄蓋掉，變成「有時
        // 候會自己跑掉」）。夠靠近線（<1e-6）時直接視為退化、不更新記憶，
        // 沿用上一次記住的方向。
        if (std::abs(signedDist) < 1e-6) return false;
        sign = (signedDist >= 0.0) ? 1.0 : -1.0;
        return true;
    }
    case DistanceMode::LineToLine: {
        auto* lnA = dynamic_cast<SketchLine*>(findGeometry(c.refs[0].geomUuid));
        auto* lnB = dynamic_cast<SketchLine*>(findGeometry(c.refs[1].geomUuid));
        if (!lnA || !lnB) return false;
        double len;
        double signedDist = perpDist(lnB->start, lnA->start, lnA->end, len);
        // 同上：檢查 signedDist，不是 len。
        if (std::abs(signedDist) < 1e-6) return false;
        sign = (signedDist >= 0.0) ? 1.0 : -1.0;
        return true;
    }
    case DistanceMode::PointToPoint:
    default: {
        SketchPoint* a = point(c.refs[0].geomUuid);
        SketchPoint* b = point(c.refs[1].geomUuid);
        if (!a || !b) return false;
        QVector2D d = b->pos - a->pos;
        double len = d.length();
        if (len < 1e-9) return false;
        dirX = d.x()/len; dirY = d.y()/len;
        return true;
    }
    }
}

bool Sketch::flipDistanceSide(const QString& uuid)
{
    SketchConstraint* c = findConstraint(uuid);
    if (!c || c->type != ConstraintType::FixedDistance) return false;

    // ⚠️ 先計算並明確覆寫「記住的哪一側」，再搬動幾何——因為
    // FixedDistanceEquation::lockReference() 現在優先採用這幾個持久化
    // 欄位、而不是每次從當下幾何位置現算（見 SketchConstraint.h 同名
    // 欄位說明）。如果這裡只搬動幾何卻不同步更新這幾個欄位，下一次
    // solveConstraints() 會直接沿用「翻轉前」記住的舊值，使用者剛按下的
    // 翻轉操作會立刻被蓋掉、形同無效。
    double curSign = 0.0, curDirX = 0.0, curDirY = 0.0;
    bool hasSide;
    const bool isSignMode = (c->distMode == DistanceMode::PointToLine ||
                             c->distMode == DistanceMode::LineToLine);
    if (isSignMode && c->distSideSign != 0.0) {
        curSign = c->distSideSign; hasSide = true;
    } else if (!isSignMode && (c->distSideDirX != 0.0 || c->distSideDirY != 0.0)) {
        curDirX = c->distSideDirX; curDirY = c->distSideDirY; hasSide = true;
    } else {
        hasSide = computeDistanceSide(*c, curSign, curDirX, curDirY);
    }
    if (!hasSide) return false;   // 兩個參考幾何目前重合/退化，沒有方向可翻轉

    if (isSignMode) c->distSideSign = -curSign;
    else            { c->distSideDirX = -curDirX; c->distSideDirY = -curDirY; }

    // 同步寫回對應的 SketchAnnotation——隱含約束本身不會被序列化，
    // GDIM 標註才是真正的存檔來源（見 SketchAnnotation.h 同名欄位說明）。
    if (!c->implicitOf.isEmpty()) {
        if (SketchAnnotation* ann = findAnnotation(c->implicitOf)) {
            ann->distSideSign = c->distSideSign;
            ann->distSideDirX = c->distSideDirX;
            ann->distSideDirY = c->distSideDirY;
        }
    }

    // 把點 p 反射到直線 (l1,l2) 的另一側，距離線的垂距不變、只是換邊。
    auto reflectAcrossLine = [](const QVector2D& p, const QVector2D& l1,
                                const QVector2D& l2) -> QVector2D {
        QVector2D dir = l2 - l1;
        const float len2 = QVector2D::dotProduct(dir, dir);
        if (len2 < 1e-12f) return p;   // 線退化成一點，無法定義鏡射軸，原樣返回
        const float t = QVector2D::dotProduct(p - l1, dir) / len2;
        const QVector2D proj = l1 + dir * t;
        return proj * 2.0f - p;
    };

    // 順便把幾何也搬到新的一側，讓使用者立刻在畫面上看到翻轉結果，不用
    // 等到下一次某個操作觸發 solveConstraints() 才看到變化。
    switch (c->distMode) {
    case DistanceMode::PointToPoint: {
        // 把 refs[1] 的點對 refs[0] 的點做點對稱（距離不變，換到正對面）。
        SketchPoint* a = point(c->refs[0].geomUuid);
        SketchPoint* b = point(c->refs[1].geomUuid);
        if (!a || !b) return false;
        movePoint(b->uuid, a->pos * 2.0f - b->pos);
        break;
    }
    case DistanceMode::PointToLine: {
        SketchPoint* p = point(c->refs[0].geomUuid);
        auto* ln = dynamic_cast<SketchLine*>(findGeometry(c->refs[1].geomUuid));
        if (!p || !ln) return false;
        movePoint(p->uuid, reflectAcrossLine(p->pos, ln->start, ln->end));
        break;
    }
    case DistanceMode::LineToLine: {
        // 把整條線 B（refs[1]）的兩端點都反射到線 A（refs[0]）的另一側，
        // 相當於把線 B 整條搬到線 A 的對面，垂直間距（value）不變。
        auto* lnA = dynamic_cast<SketchLine*>(findGeometry(c->refs[0].geomUuid));
        auto* lnB = dynamic_cast<SketchLine*>(findGeometry(c->refs[1].geomUuid));
        if (!lnA || !lnB) return false;
        SketchPoint* bs = point(lnB->startUuid);
        SketchPoint* be = point(lnB->endUuid);
        if (bs) movePoint(bs->uuid, reflectAcrossLine(bs->pos, lnA->start, lnA->end));
        if (be) movePoint(be->uuid, reflectAcrossLine(be->pos, lnA->start, lnA->end));
        break;
    }
    default:
        return false;
    }

    // movePoint() 把幾何搬到新的一側當作這次求解的起始猜測值；上面已經
    // 明確把「記住的哪一側」也翻轉過了，所以這裡的 solveConstraints()
    // 會穩定收斂在使用者剛剛選的這一側，且結果會在求解完成後的同步步驟
    // （見本函式下方 solveConstraints() 尾端的說明）繼續被記住、存檔。
    solveConstraints();
    return true;
}

void Sketch::removeConstraintsOf(const QString& geomUuid) {
    m_constraints.erase(
        std::remove_if(m_constraints.begin(), m_constraints.end(),
                       [&](const SketchConstraint& c){
                           return std::any_of(c.refs.begin(), c.refs.end(),
                                              [&](const GeomRef& r){ return r.geomUuid == geomUuid; });
                       }),
        m_constraints.end());
}

// ── 標註管理（GDIM v2 Phase 1）─────────────────────────────────────────────

void Sketch::addImplicitConstraint(const SketchAnnotation& a) {
    // 先移除舊的（若存在），再視需要重新加入，簡化「同步」邏輯
    removeImplicitConstraint(a.uuid);

    auto implicit = a.toImplicitConstraint();
    if (!implicit) return;   // driving==false 或 LeaderNote 等非尺寸型別

    // 驗證參考幾何存在，行為與 addConstraint 一致
    for (const GeomRef& ref : implicit->refs) {
        bool found = std::any_of(m_geometries.begin(), m_geometries.end(),
                                 [&](const SketchGeometry* g){ return g->uuid == ref.geomUuid; });
        if (!found) {
            qWarning() << "[Sketch] addImplicitConstraint: geom not found:" << ref.geomUuid;
            return;
        }
    }
    m_constraints.append(*implicit);
}

void Sketch::removeImplicitConstraint(const QString& annotationUuid) {
    m_constraints.erase(
        std::remove_if(m_constraints.begin(), m_constraints.end(),
                       [&](const SketchConstraint& c){ return c.implicitOf == annotationUuid; }),
        m_constraints.end());
}

QString Sketch::addAnnotation(const SketchAnnotation& a) {
    SketchAnnotation* existing = findAnnotation(a.uuid);
    if (existing) {
        *existing = a;
    } else {
        m_annotations.append(a);
    }
    addImplicitConstraint(a);
    if (a.driving)
        solveConstraints();   // 觸發 constraintSolved → ConstraintOverlayManager::rebuildAll()，
                               // 確保下面 annotationAdded 觸發輕量刷新時 AIS 已存在
    Q_EMIT annotationAdded(a.uuid);
    return a.uuid;
}

bool Sketch::removeAnnotation(const QString& uuid) {
    for (int i = 0; i < m_annotations.size(); ++i) {
        if (m_annotations[i].uuid == uuid) {
            const QString paramExpr = m_annotations[i].paramExpr;
            m_annotations.removeAt(i);
            removeImplicitConstraint(uuid);
            // ⚠️ 修正（第 12 項回報：刪除尺寸約束後，GDIM 自動命名參數
            // 〔d11、d12…〕沒有一併刪除）：GDIM 建立尺寸時若使用者選擇
            // 自動命名，會在 ParameterStore 裡註冊一個新參數（d1, d2,
            // a1, r1…），並讓這個標註的 paramExpr 指向它。刪除標註時
            // 原本只清掉標註與隱含約束，從沒有人再引用的自動命名參數卻
            // 一直留在 ParameterStore 裡——多次「建立尺寸→改用別的方式
            // 標註→刪除」下來就會累積出一堆孤兒參數。這裡刪除標註後，
            // 順便檢查它原本引用的參數名稱是否符合自動命名格式、且已經
            // 沒有任何標註/約束/其他參數的表達式在引用它，是的話才一併
            // 移除；使用者自訂名稱（不符合自動命名格式）一律不動，避免
            // 誤刪還想留著重複使用的全域參數。
            maybeRemoveOrphanedAutoParam(paramExpr);
            Q_EMIT annotationRemoved(uuid);
            solveConstraints();
            return true;
        }
    }
    return false;
}

void Sketch::maybeRemoveOrphanedAutoParam(const QString& name) {
    if (name.isEmpty() || !m_parameterStore) return;
    // 與 command::autoParamPrefix()/nextAutoParamName()（ConstraintCommands.cpp）
    // 使用的命名規則一致：<字母前綴><數字>，例如 d1、a12、r3、dia2、s5。
    static const QRegularExpression autoNameRe(QStringLiteral("^(d|a|r|dia|s)\\d+$"));
    if (!autoNameRe.match(name).hasMatch())
        return;   // 不符合自動命名格式，視為使用者自訂參數，不動它

    // 還有其他標註在引用這個名稱嗎？
    for (const auto& a : m_annotations)
        if (a.paramExpr == name) return;
    // 還有其他（非隱含）尺寸約束在引用嗎？
    for (const auto& c : m_constraints)
        if (c.implicitOf.isEmpty() && c.paramExpr == name) return;
    // 還有其他參數的表達式依賴它嗎？（例如 d3 = "d1 + d2"）
    if (!m_parameterStore->dependentsOf(name).isEmpty()) return;

    m_parameterStore->removeLocal(name);
}

SketchAnnotation* Sketch::findAnnotation(const QString& uuid) {
    for (SketchAnnotation& a : m_annotations) {
        if (a.uuid == uuid) return &a;
    }
    return nullptr;
}

QList<SketchAnnotation*> Sketch::annotationsOf(const QString& geomUuid) {
    QList<SketchAnnotation*> result;
    for (SketchAnnotation& a : m_annotations) {
        bool refs_it = std::any_of(a.refs.begin(), a.refs.end(),
                                   [&](const GeomRef& r){ return r.geomUuid == geomUuid; });
        if (refs_it) result.append(&a);
    }
    return result;
}

void Sketch::removeAnnotationsOf(const QString& geomUuid) {
    QList<QString> toRemove;
    for (const SketchAnnotation& a : m_annotations) {
        bool hit = std::any_of(a.refs.begin(), a.refs.end(),
                               [&](const GeomRef& r){ return r.geomUuid == geomUuid; });
        if (hit) toRemove.append(a.uuid);
    }
    for (const QString& uuid : toRemove) {
        m_annotations.erase(
            std::remove_if(m_annotations.begin(), m_annotations.end(),
                           [&](const SketchAnnotation& a){ return a.uuid == uuid; }),
            m_annotations.end());
        removeImplicitConstraint(uuid);
    }
}

// GDIM v2 Phase 6：重複尺寸偵測
const SketchAnnotation* Sketch::findDuplicateAnnotation(
    const QList<GeomRef>& refs, AnnotationKind kind) const
{
    auto sameRefs = [](const QList<GeomRef>& a, const QList<GeomRef>& b) {
        if (a.size() != b.size()) return false;
        QList<GeomRef> remaining = b;   // 不分順序比對（例如 A→B 距離與 B→A 距離視為相同）
        for (const auto& ra : a) {
            int idx = -1;
            for (int i = 0; i < remaining.size(); ++i) {
                if (remaining[i].geomUuid == ra.geomUuid && remaining[i].handle == ra.handle) {
                    idx = i; break;
                }
            }
            if (idx < 0) return false;
            remaining.removeAt(idx);
        }
        return true;
    };

    for (const auto& a : m_annotations) {
        if (a.kind != kind) continue;
        if (sameRefs(a.refs, refs)) return &a;
    }
    return nullptr;
}

SolveResult Sketch::solveConstraints() {
    gp_Dir normal(0, 0, 1);
    if (m_plane) {
        QVector3D n = m_plane->normal();
        normal = gp_Dir(n.x(), n.y(), n.z());
    }
    // ⚠️ 修正：這是「物件間的約束（例如平行）套用到建構線之後完全沒有反應」
    // 的根本原因。原本這裡用 normalGeometries()（會把 role != Normal 的
    // 建構線/中心線整批排除）取得要丟給 solver 的幾何列表，導致：
    //   - packVariables() 建立的 layout（uuid → 變數位置）裡完全沒有建構
    //     幾何的條目
    //   - 任何參考到建構幾何 UUID 的約束方程式（ParallelEquation／
    //     PerpendicularEquation／TangentEquation…，見
    //     ConstraintSolver::buildEquations()）在 layout 裡查無資料，
    //     等於是對著不存在的變數求解——約束雖然成功加進 m_constraints
    //     （addConstraint() 的存在性檢查是查 m_geometries，本來就找得到），
    //     但 solve() 實際上完全沒有東西可以動，看起來就是「套用了但毫無
    //     反應」。
    //   - 這同時也是本次「建構線／弧／圓除了不參與輪廓外，其他功能都要
    //     與一般幾何相同」需求裡，唯一還沒補上的一塊：normalGeometries()
    //     只應該用在「輪廓/迴圈偵測」（SketchLoopFinder／buildWires()）
    //     以及需要跟 aisShapes()／aisToGeomIndex 保持索引對齊的 Grip
    //     系統（SketchGripProvider，其 gi 索引本來就是配合 m_aisShapes
    //     排除建構幾何後的順序），不應該用在「幾何約束求解」這種所有
    //     幾何都該一視同仁參與的場合——syncGeometryFromPoints() 等其他
    //     地方本來就是統一操作 m_geometries（見該函式），這裡改成一致。
    //
    // 改為 m_geometries（含建構/中心線），與 solveWithStore()（原本就是
    // 用 m_geometries，兩個 solve 入口原本行為不一致也是一種隱藏的 bug）
    // 保持一致。
    auto geoms = m_geometries;
    auto result = m_solver.solve(geoms, m_constraints, normal);
    Q_EMIT constraintSolved(result);
    if (result.status != SolveStatus::SolverError) {
        // unpackVariables 已同時更新：
        //   - SketchLine.start/end（由 line 自身的 DOF 決定）
        //   - SketchPoint.pos（由 SketchPoint 自身的 DOF 決定）
        //
        // 若某條約束直接操作 SketchLine DOF（refs = lineUuid+Start/End），
        // Solver 更新了 SketchLine 但沒有更新對應的 SketchPoint。
        // 反之，若約束操作 SketchPoint UUID，Solver 更新了 SketchPoint
        // 但 SketchLine 需要跟著同步。
        //
        // 策略：先讓 line.start/end → SketchPoint（確保 Solver 對 line DOF
        // 的修改能傳播到 SketchPoint），再由 syncGeometryFromPoints 統一
        // 以 SketchPoint 為單一來源同步回所有曲線。
        for (auto* g : geoms) {
            if (auto* line = dynamic_cast<SketchLine*>(g)) {
                if (auto* ps = point(line->startUuid)) ps->pos = line->start;
                if (auto* pe = point(line->endUuid))   pe->pos = line->end;
            }
            // Arc/Circle 的 SketchPoint 由 unpackVariables 直接更新，不需覆蓋
        }
        // 以 SketchPoint 為單一來源，同步所有曲線座標
        syncGeometryFromPoints();

        // ⚠️ 第 10 項回報後續需求：把每個 FixedDistance 約束「現在」收斂
        // 到的哪一側/方向，寫回 distSideSign/DirX/DirY，讓下一次求解（見
        // FixedDistanceEquation::lockReference()）優先沿用這個記住的值，
        // 而不是每次都從當下幾何現算——這樣「翻轉方向」操作的結果、以及
        // 剛好在距離值接近 0 的瞬間求解出來的方向，才能持續穩定下去，不
        // 會下一次求解又被重新現算成別的結果。implicitOf 非空（GDIM 標註
        // 產生）的隱含約束另外同步回對應的 SketchAnnotation——那裡才是
        // 真正的存檔來源（隱含約束本身不會被序列化，見
        // SketchConstraint.h／SketchAnnotation.h 同名欄位的說明）。
        // 只在非退化（computeDistanceSide() 回傳 true）時才覆寫，避免用
        // 兩個參考幾何暫時重合時算出的雜訊值，把先前記住的好值蓋掉。
        for (auto& c : m_constraints) {
            if (c.type != ConstraintType::FixedDistance) continue;
            double sign = 0.0, dirX = 0.0, dirY = 0.0;
            if (!computeDistanceSide(c, sign, dirX, dirY)) continue;
            if (c.distMode == DistanceMode::PointToLine ||
                c.distMode == DistanceMode::LineToLine) {
                c.distSideSign = sign;
            } else {
                c.distSideDirX = dirX;
                c.distSideDirY = dirY;
            }
            if (!c.implicitOf.isEmpty()) {
                if (SketchAnnotation* ann = findAnnotation(c.implicitOf)) {
                    ann->distSideSign = c.distSideSign;
                    ann->distSideDirX = c.distSideDirX;
                    ann->distSideDirY = c.distSideDirY;
                }
            }
        }

        markDirty();
        Q_EMIT geometryChanged();

        // ★ Solver 可能改變了幾何尺寸（如圓的 radius），需重建 AIS shapes。
        //   rebuildShapesOnly() 現在會逐一比對每個幾何的 fingerprint，
        //   只對「真的改變」的 shape 做 Erase/Display，未變動的沿用舊 handle。
        if (!m_aisContext.IsNull())
            rebuildShapesOnly();
    }

    // ✅ Step 13: 將全域 SolveStatus 傳遞給所有 SketchPointAIS，即時更新顏色
    //   只有狀態真的改變的點才呼叫 Redisplay／UpdateCurrentViewer，
    //   避免每次 solve 都對所有點觸發一次 context 更新。
    if (!m_aisContext.IsNull()) {
        bool anyStatusChanged = false;
        for (const auto& obj : m_aisShapes) {
            if (auto ptAis = Handle(SketchPointAIS)::DownCast(obj)) {
                if (ptAis->solveStatus() == result.status) continue;
                ptAis->updateSolveStatus(result.status);
                m_aisContext->Redisplay(ptAis, Standard_False);
                anyStatusChanged = true;
            }
        }
        if (anyStatusChanged)
            m_aisContext->UpdateCurrentViewer();
    }

    // ⚠️ 效能優化：快取本次求解結果，供呼叫端（例如 SketchPanel 幾何約束
    // 共用後處理）直接讀取，避免像過去那樣「拿到 constrainXxx() 的結果
    // 後，只為了取得 SolveResult/DOF 訊息又整個再呼叫一次 solveConstraints()」
    // ——那等於讓每次下約束（水平/垂直/平行/共線…）都完整跑兩遍 Newton-
    // Raphson 疊代（含每次疊代都要做的 BDCSVD 分解）與兩遍 AIS 重建/重繪，
    // 使用者在複雜草圖（大量幾何/約束，如本次回報的 CurbPlinth.aicad）
    // 上操作時感受到的延遲有一半以上其實是這個重複求解白白浪費的。
    m_lastSolveResult = result;
    return result;
}

SolveResult Sketch::solveWithStore(const aicad::core::ParameterStore* store) {
    // 用指定 store 求值所有尺寸約束（instance store 內含父子 fallback）
    if (store) {
        for (auto& c : m_constraints) {
            if (c.isDimensional() && !c.paramExpr.isEmpty())
                c.evaluateValue(store);
        }
    }
    gp_Dir normal(0, 0, 1);
    if (m_plane) {
        QVector3D n = m_plane->normal();
        normal = gp_Dir(n.x(), n.y(), n.z());
    }
    return m_solver.solve(m_geometries, m_constraints, normal);
}

int Sketch::degreesOfFreedom() const {
    // ⚠️ 修正：同 solveConstraints() 的說明——DOF 計算也必須含建構/中心線，
    // 否則使用者看到的「DOF: X → Y」報告會低估實際自由度（建構幾何的
    // DOF 完全沒算進去），且與 solve() 內部（現在已改用 m_geometries）
    // 算出來的 dof 對不上。
    return ConstraintSolver::computeDOF(m_geometries, m_constraints);
}

// ── 便捷 API ──────────────────────────────────────────────────────────────
QString Sketch::constrainCoincident(const GeomRef& a, const GeomRef& b) {
    return addConstraint(SketchConstraint::makeCoincident(a, b));
}
QString Sketch::constrainHorizontal(const QString& uuid) {
    return addConstraint(SketchConstraint::makeHorizontal(uuid));
}

QString Sketch::constrainVertical(const QString& lineUuid) {
    return addConstraint(SketchConstraint::makeVertical(lineUuid));
}
QString Sketch::constrainParallel(const QString& lineA, const QString& lineB) {
    return addConstraint(SketchConstraint::makeParallel(lineA, lineB));
}
QString Sketch::constrainPerpendicular(const QString& lineA, const QString& lineB) {
    return addConstraint(SketchConstraint::makePerpendicular(lineA, lineB));
}
QString Sketch::constrainTangent(const QString& geomA, const QString& geomB) {
    return addConstraint(SketchConstraint::makeTangent(geomA, geomB));
}
QString Sketch::constrainEqualLength(const QString& lineA, const QString& lineB) {
    return addConstraint(SketchConstraint::makeEqualLength(lineA, lineB));
}
QString Sketch::constrainEqualRadius(const QString& circA, const QString& circB) {
    return addConstraint(SketchConstraint::makeEqualRadius(circA, circB));
}
QString Sketch::constrainConcentric(const QString& geomA, const QString& geomB) {
    return addConstraint(SketchConstraint::makeConcentric(geomA, geomB));
}
QString Sketch::constrainFixed(const QString& geomUuid) {
    return addConstraint(SketchConstraint::makeFixed(geomUuid));
}
QString Sketch::constrainDistance(const GeomRef& a, const GeomRef& b, double dist) {
    return addConstraint(SketchConstraint::makeFixedDistance(a, b, dist));
}
QString Sketch::constrainRadius(const QString& geomUuid, double radius) {
    return addConstraint(SketchConstraint::makeFixedRadius(geomUuid, radius));
}

// GeomRef overload：用於 ConstraintPickSession 選到的圓/弧 ref
QString Sketch::constrainRadius(const GeomRef& ref, double radius) {
    // FixedRadius 只需要幾何 UUID，handle 用 WholeGeom 即可
    SketchConstraint c;
    c.uuid    = QUuid::createUuid().toString(QUuid::WithoutBraces);
    c.type    = ConstraintType::FixedRadius;
    c.value   = radius;
    c.refs    = { GeomRef(ref.geomUuid, GeomHandle::WholeGeom) };
    c.driving = true;
    return addConstraint(c);
}

// ── 草圖平面參考幾何（X 軸 / Y 軸 / 原點）lazy-init ──────────────────────
// 這三個幾何在第一次被請求時建立，以「Fixed」約束固定位置。
// GeomRole::Construction 使它們不出現在普通幾何清單（未來可過濾）。

QString Sketch::xAxisGeomUuid()
{
    if (!m_xAxisGeomUuid.isEmpty()) return m_xAxisGeomUuid;

    const QVector2D p0(-200.0f, 0.0f);
    const QVector2D p1( 200.0f, 0.0f);

    auto* sp0  = new SketchPoint(p0, SketchPoint::Origin::Explicit, GeomRole::Construction);
    auto* sp1  = new SketchPoint(p1, SketchPoint::Origin::Explicit, GeomRole::Construction);
    auto* line = new SketchLine(p0, p1, GeomRole::Construction);
    line->startUuid = sp0->uuid;
    line->endUuid   = sp1->uuid;

    m_geometries.append(sp0);
    m_geometries.append(sp1);
    m_geometries.append(line);

    // Fixed 約束鎖住兩端點，使軸線不可移動
    // ⚠️ 第 17 項回報修正：fixedAbsolute=true + value/value2 存絕對座標，
    // 避免 ConstraintSolver 每次求解都用「當下座標」重新 snapshot，導致
    // 累積數值誤差讓軸線慢慢飄移（見 SketchConstraint.h 說明）。
    {
        SketchConstraint c0;
        c0.uuid  = QUuid::createUuid().toString(QUuid::WithoutBraces);
        c0.type  = ConstraintType::Fixed;
        c0.refs  = { GeomRef(sp0->uuid, GeomHandle::WholeGeom) };
        c0.fixedAbsolute = true;
        c0.value  = p0.x();
        c0.value2 = p0.y();
        m_constraints.append(c0);

        SketchConstraint c1;
        c1.uuid  = QUuid::createUuid().toString(QUuid::WithoutBraces);
        c1.type  = ConstraintType::Fixed;
        c1.refs  = { GeomRef(sp1->uuid, GeomHandle::WholeGeom) };
        c1.fixedAbsolute = true;
        c1.value  = p1.x();
        c1.value2 = p1.y();
        m_constraints.append(c1);
    }

    m_xAxisGeomUuid = line->uuid;
    return m_xAxisGeomUuid;
}

QString Sketch::yAxisGeomUuid()
{
    if (!m_yAxisGeomUuid.isEmpty()) return m_yAxisGeomUuid;

    const QVector2D p0(0.0f, -200.0f);
    const QVector2D p1(0.0f,  200.0f);

    auto* sp0  = new SketchPoint(p0, SketchPoint::Origin::Explicit, GeomRole::Construction);
    auto* sp1  = new SketchPoint(p1, SketchPoint::Origin::Explicit, GeomRole::Construction);
    auto* line = new SketchLine(p0, p1, GeomRole::Construction);
    line->startUuid = sp0->uuid;
    line->endUuid   = sp1->uuid;

    m_geometries.append(sp0);
    m_geometries.append(sp1);
    m_geometries.append(line);

    for (auto* sp : { sp0, sp1 }) {
        SketchConstraint c;
        c.uuid  = QUuid::createUuid().toString(QUuid::WithoutBraces);
        c.type  = ConstraintType::Fixed;
        c.refs  = { GeomRef(sp->uuid, GeomHandle::WholeGeom) };
        c.fixedAbsolute = true;
        c.value  = sp->pos.x();
        c.value2 = sp->pos.y();
        m_constraints.append(c);
    }

    m_yAxisGeomUuid = line->uuid;
    return m_yAxisGeomUuid;
}

QString Sketch::originPointUuid()
{
    if (!m_originPointUuid.isEmpty()) return m_originPointUuid;

    auto* sp = new SketchPoint(QVector2D(0.0f, 0.0f),
                               SketchPoint::Origin::Explicit,
                               GeomRole::Construction);
    m_geometries.append(sp);

    SketchConstraint c;
    c.uuid  = QUuid::createUuid().toString(QUuid::WithoutBraces);
    c.type  = ConstraintType::Fixed;
    c.refs  = { GeomRef(sp->uuid, GeomHandle::WholeGeom) };
    c.fixedAbsolute = true;
    c.value  = 0.0;
    c.value2 = 0.0;
    m_constraints.append(c);

    m_originPointUuid = sp->uuid;
    return m_originPointUuid;
}

// 固定某個端點的 X 座標
QString Sketch::constrainFixedX(const GeomRef& point, double x) {
    SketchConstraint c;
    c.uuid    = QUuid::createUuid().toString(QUuid::WithoutBraces);
    c.type    = ConstraintType::FixedX;
    c.value   = x;
    c.refs    = { point };
    c.driving = true;
    return addConstraint(c);
}

// 固定某個端點的 Y 座標
QString Sketch::constrainFixedY(const GeomRef& point, double y) {
    SketchConstraint c;
    c.uuid    = QUuid::createUuid().toString(QUuid::WithoutBraces);
    c.type    = ConstraintType::FixedY;
    c.value   = y;
    c.refs    = { point };
    c.driving = true;
    return addConstraint(c);
}

// 兩條線（各取一個點）的夾角（弧度）
QString Sketch::constrainAngle(const GeomRef& a, const GeomRef& b, double angleRad) {
    SketchConstraint c;
    c.uuid    = QUuid::createUuid().toString(QUuid::WithoutBraces);
    c.type    = ConstraintType::FixedAngleDim;
    c.value   = angleRad;
    c.refs    = { a, b };
    c.driving = true;
    return addConstraint(c);
}

QString Sketch::constrainPointOnCurve(const GeomRef& point, const QString& curveUuid) {
    return addConstraint(SketchConstraint::makePointOnCurve(point, curveUuid));
}

// ============================================================================
// ✅ 新增：標準平面名稱解析
//    按 planeName 比對 PlaneManager 中已登記的標準平面
// ============================================================================
Plane* Sketch::resolveStandardPlane(const QString& planeName) {
    PlaneManager* manager = PlaneManager::instance();
    if (!manager) return nullptr;

    QList<Plane*> planes = manager->planes();

    // ── 優先：完全比對 displayName / name ──────────────────────────
    for (Plane* p : planes) {
        if (p->displayName() == planeName || p->name() == planeName) {
            return p;
        }
    }

    // ── 次要：依幾何類型比對 ────────────────────────────────────────
    //   planeName 可能是 "XY", "YZ", "XZ", "ZX" 等
    const QString upper = planeName.trimmed().toUpper();

    for (Plane* p : planes) {
        if ((upper == "XY" || upper == "XY PLANE") && p->isXY()) return p;
        if ((upper == "YZ" || upper == "YZ PLANE") && p->isYZ()) return p;
        if ((upper == "XZ" || upper == "XZ PLANE") && p->isXZ()) return p;
        if ((upper == "ZX" || upper == "ZX PLANE") && p->isZX()) return p;
    }

    // ── 次要：依 Plane::Type enum 比對 ──────────────────────────────
    for (Plane* p : planes) {
        if (upper == "XY" && p->type() == Plane::Type::XY) return p;
        if (upper == "YZ" && p->type() == Plane::Type::YZ) return p;
        if ((upper == "XZ" || upper == "ZX") &&
            (p->type() == Plane::Type::XZ || p->type() == Plane::Type::ZX)) {
            return p;
        }
    }

    return nullptr;
}

// ============================================================================
// ✅ 新增：從 planeData JSON 重建平面
//    先嘗試幾何比對現有平面，若無則建立新平面
// ============================================================================
Plane* Sketch::reconstructPlaneFromJson(const QJsonObject& planeJson,
                                        const QString& hint) {
    if (planeJson.isEmpty()) return nullptr;

    PlaneManager* manager = PlaneManager::instance();
    if (!manager) return nullptr;

    // ── 讀取幾何 ────────────────────────────────────────────────────
    QJsonObject geometry = planeJson["geometry"].toObject();
    if (geometry.isEmpty()) {
        // 舊格式：geometry 直接在頂層
        // 嘗試以 hint 名稱解析
        return resolveStandardPlane(hint);
    }

    auto readVec3 = [&](const QString& key) -> QVector3D {
        QJsonArray arr = geometry[key].toArray();
        if (arr.size() < 3) return QVector3D();
        return QVector3D(arr[0].toDouble(), arr[1].toDouble(), arr[2].toDouble());
    };

    QVector3D origin = readVec3("origin");
    QVector3D normal = readVec3("normal");
    QVector3D xAxis  = readVec3("xAxis");

    if (normal.length() < 1e-6) {
        qWarning() << "[Sketch] reconstructPlaneFromJson: invalid normal";
        return nullptr;
    }

    const double tol = 1e-4;

    // ── 嘗試與現有平面幾何比對（避免重複建立） ─────────────────────
    for (Plane* p : manager->planes()) {
        if ((p->origin() - origin).length() < tol &&
            (p->normal() - normal).length() < tol  &&
            (p->xAxis()  - xAxis ).length() < tol) {
            qDebug() << "[Sketch] reconstructPlaneFromJson: matched existing plane"
                     << p->displayName();
            return p;
        }
    }

    // ── 建立新平面並向 PlaneManager 登記 ────────────────────────────
    QString newName = hint.isEmpty()
                          ? planeJson["name"].toString("RestoredPlane")
                          : hint;

    Plane* newPlane = manager->createPlane(Plane::Type::Custom, newName);
    if (!newPlane) {
        qWarning() << "[Sketch] reconstructPlaneFromJson: createPlane failed";
        return nullptr;
    }

    newPlane->setCoordinateSystem(origin, normal, xAxis);

    qDebug() << "[Sketch] reconstructPlaneFromJson: created new plane"
             << newName
             << "normal(" << normal.x() << normal.y() << normal.z() << ")";

    return newPlane;
}

// ============================================================================
// Plane signals
// ============================================================================

void Sketch::onPlaneAboutToBeDeleted() {
    qWarning() << "[Sketch]" << name() << "associated plane is being deleted!";
    disconnectPlaneSignals();
    createDefaultPlane();
    Q_EMIT planeChanged(m_plane);
    Q_EMIT rebuildRequested();
}

void Sketch::onPlaneGeometryChanged() {
    qDebug() << "[Sketch]" << name() << "plane geometry changed";
    Q_EMIT rebuildRequested();
}

void Sketch::scheduleRebuild() {
    // 參數變更後重新求值尺寸約束並重建幾何
    if (!m_parameterStore) return;
    for (auto& c : m_constraints) {
        if (c.isDimensional() && !c.paramExpr.isEmpty()) {
            c.evaluateValue(m_parameterStore);
            // ⚠️ 修正（第 12 項回報：cant 已改成 0，d1 卻還殘留舊值 65）：
            // 上面只更新了隱含約束自己的 SketchConstraint::value，用來讓
            // 求解器把幾何移動到正確位置；但 GDIM 尺寸顯示/存檔用的是
            // 對應 SketchAnnotation::value（Sketch::toJson() 存檔時，隱含
            // 約束本身是被排除的，見 removeAnnotation()/toJson() 的說明），
            // 這裡若不同步寫回，annotation 的 value 就會停在「上一次真正
            // 被存過」的舊值，即使幾何實際上已經正確跟著新參數移動了，
            // 存檔/面板顯示的數字仍然是舊的。
            if (!c.implicitOf.isEmpty()) {
                if (SketchAnnotation* ann = findAnnotation(c.implicitOf))
                    ann->value = c.value;
            }
        }
    }
    // ⚠️ 修正：ParameterPanel／SketchPanel 改動參數或表達式後，草圖畫面
    // 沒有同步更新的根因就在這裡。上面的迴圈只更新了每條尺寸約束的
    // 「目標值」（SketchConstraint::value = evaluateValue() 求值結果），
    // 但真正把幾何點移動到滿足新約束值的是 ConstraintSolver::solve()
    // （經 unpackVariables() 寫回 SketchPoint/SketchLine）。原本這裡呼叫
    // 的 rebuild() 純粹是依「目前」的點位置重新產生 AIS/OCCT shape，
    // 完全不會呼叫求解器——所以參數改了、c.value 也确实更新了，但畫面
    // 上的幾何仍停在改參數前的舊形狀，直到使用者剛好又做了其他會觸發
    // solveConstraints() 的操作（例如拖曳某個 grip）才會被「順便」補算
    // 出來，造成「Sketch Panel 改參數/表達式，Sketch 沒有同步更新」的
    // 錯覺。改成呼叫 solveConstraints()：它會用上面已經更新好的 c.value
    // 重新求解一次，並完成 AIS 重建、SketchPoint 求解狀態著色、
    // markDirty()/rebuildRequested() 等後續步驟，行為與其他所有下約束
    // 路徑一致（solveConstraints() 內部已包含相當於 rebuild() 的
    // rebuildShapesOnly()，不需要再另外呼叫 rebuild()）。
    solveConstraints();
}

gp_Pnt Sketch::toWorld(const QVector2D& point) const {
    if (!hasValidPlane()) {
        qWarning() << "[Sketch]" << name() << "No valid plane for conversion";
        return gp_Pnt(point.x(), point.y(), 0);
    }

    QVector3D worldPt = m_plane->toWorld(point.x(), point.y());
    return gp_Pnt(worldPt.x(), worldPt.y(), worldPt.z());
}

void Sketch::createDefaultPlane() {
    PlaneManager* manager = PlaneManager::instance();

    for (Plane* p : manager->planes()) {
        if (p->type() == Plane::Type::XY && p->isXY()) {
            m_plane = p;
            connectPlaneSignals();
            qDebug() << "[Sketch]" << name() << "using existing XY plane";
            return;
        }
    }

    m_plane = manager->createPlane(
        Plane::Type::XY,
        QString("%1_DefaultPlane").arg(name()));

    connectPlaneSignals();
    qDebug() << "[Sketch]" << name() << "created new default XY plane";
}

void Sketch::connectPlaneSignals() {
    if (!m_plane) return;
    connect(m_plane, &Plane::aboutToBeDeleted, this, &Sketch::onPlaneAboutToBeDeleted);
    connect(m_plane, &Plane::geometryChanged,  this, &Sketch::onPlaneGeometryChanged);
}

void Sketch::disconnectPlaneSignals() {
    if (!m_plane) return;
    disconnect(m_plane, nullptr, this, nullptr);
}

// ============================================================================
// AIS display
// ============================================================================

QList<TopoDS_Wire> Sketch::wires() const {
    return m_wires;
}

QList<Handle(AIS_InteractiveObject)> Sketch::aisShapes() const {
    return m_aisShapes;
}

const QList<QString>& Sketch::aisShapeUuids() const {
    return m_aisShapeUuids;
}

QList<Handle(AIS_InteractiveObject)> Sketch::displayInContext(
    const Handle(AIS_InteractiveContext)& context) {
    m_aisContext = context;
    if (context.IsNull()) return {};

    // 顯示所有幾何 AIS（含 SketchPointAIS）
    for (const auto& obj : m_aisShapes) {
        if (obj.IsNull()) continue;
        context->Display(obj, Standard_False);
        // SketchPointAIS 需要明確啟用 selection mode 0
        if (Handle(SketchPointAIS)::DownCast(obj)) {
            context->Activate(obj, 0, Standard_False);
        }
        applyGeomSelectionSensitivity(context, obj);
    }

    // 建構線：顯示且可選（與一般幾何一致；不參與輪廓的差異只在 buildWires()
    // 與 SketchLoopFinder，與是否可被滑鼠選取無關——不再呼叫 Deactivate()）。
    for (const Handle(AIS_Shape)& s : m_constructionShapes) {
        if (!s.IsNull()) {
            context->Display(s, Standard_False);
        }
    }

    context->UpdateCurrentViewer();
    return m_aisShapes;
}

void Sketch::eraseFromContext(const Handle(AIS_InteractiveContext)& context) {
    if (context.IsNull()) return;
    for (const auto& obj : m_aisShapes)
        if (!obj.IsNull()) context->Erase(obj, Standard_False);
    for (const Handle(AIS_Shape)& s : m_constructionShapes)
        if (!s.IsNull()) context->Erase(s, Standard_False);
    context->UpdateCurrentViewer();
}

TopoDS_Wire Sketch::mainWire() const {
    return m_wires.isEmpty() ? TopoDS_Wire() : m_wires.first();
}

bool Sketch::hasClosedProfile() const {
    for (const TopoDS_Wire& wire : m_wires) {
        if (!wire.IsNull() && wire.Closed()) return true;
    }
    return false;
}

QVector<SketchRegion> Sketch::detectRegions() const
{
    SketchLoopFinder finder;
    QVector<SketchRegion> regions = finder.findRegions(this);

    // 按 outer loop 頂點數量（近似面積）降序排列
    std::sort(regions.begin(), regions.end(),
              [](const SketchRegion& a,
                 const SketchRegion& b) {
                  return a.outerLoop.edgeUuids.size() >
                         b.outerLoop.edgeUuids.size();
              });
    return regions;
}

std::optional<SketchRegion>
Sketch::pickRegion(const QVector2D& sketchPt) const
{
    for (const auto& region : detectRegions()) {
        if (region.contains(sketchPt))
            return region;
    }
    return std::nullopt;
}

// Sketch.cpp 新增實作
bool Sketch::hasExtrudableProfile() const {
    if (m_wires.isEmpty()) return false;
    if (hasClosedProfile()) return true;

    // 嘗試連接所有 edge，看能否形成封閉輪廓
    Handle(TopTools_HSequenceOfShape) edges = new TopTools_HSequenceOfShape;
    for (const TopoDS_Wire& w : m_wires) {
        TopExp_Explorer exp(w, TopAbs_EDGE);
        for (; exp.More(); exp.Next())
            edges->Append(exp.Current());
    }
    if (edges->IsEmpty()) return false;

    Handle(TopTools_HSequenceOfShape) closed = new TopTools_HSequenceOfShape;
    ShapeAnalysis_FreeBounds::ConnectEdgesToWires(
        edges, Precision::Confusion(), Standard_False, closed);
    return !closed->IsEmpty();
}
// ═════════════════════════════════════════════════════════════════════════════
// Phase 0B：SketchPoint 一等公民 — 點管理 API 實作
// ═════════════════════════════════════════════════════════════════════════════

QString Sketch::addPoint(const QVector2D& pos, SketchPoint::Origin origin)
{
    auto* pt = new SketchPoint(pos, origin);
    m_geometries.append(pt);
    m_uuidToGeomIndex[pt->uuid] = m_geometries.size() - 1;
    return pt->uuid;
}

QString Sketch::addExplicitPoint(const QVector2D& pos)
{
    QString uuid = addPoint(pos, SketchPoint::Origin::Explicit);
    Q_EMIT geometryChanged();
    Q_EMIT rebuildRequested();
    return uuid;
}

SketchPoint* Sketch::point(const QString& uuid) const
{
    auto* g = findGeometry(uuid);
    return (g && g->type == SketchGeometryType::Point)
           ? static_cast<SketchPoint*>(g) : nullptr;
}

QList<SketchPoint*> Sketch::points() const
{
    QList<SketchPoint*> result;
    for (auto* g : m_geometries)
        if (g->type == SketchGeometryType::Point)
            result.append(static_cast<SketchPoint*>(g));
    return result;
}

void Sketch::movePoint(const QString& uuid, const QVector2D& newPos)
{
    auto* pt = point(uuid);
    if (!pt) return;

    pt->pos = newPos;
    pt->points[0] = newPos;

    // 同步所有引用此點的曲線的 start/end 座標
    syncGeometryFromPoints();
    Q_EMIT geometryChanged();
}

void Sketch::mergePoints(const QString& fromUuid, const QString& toUuid)
{
    if (fromUuid == toUuid) return;
    auto* fromPt = point(fromUuid);
    auto* toPt   = point(toUuid);
    if (!fromPt || !toPt) return;

    // 更新所有引用 fromUuid 的曲線
    for (auto* g : m_geometries) {
        if (auto* line = dynamic_cast<SketchLine*>(g)) {
            if (line->startUuid == fromUuid) line->startUuid = toUuid;
            if (line->endUuid   == fromUuid) line->endUuid   = toUuid;
        }
        if (auto* arc = dynamic_cast<SketchArc*>(g)) {
            if (arc->startUuid  == fromUuid) arc->startUuid  = toUuid;
            if (arc->endUuid    == fromUuid) arc->endUuid    = toUuid;
            if (arc->centerUuid == fromUuid) arc->centerUuid = toUuid;
        }
        if (auto* circ = dynamic_cast<SketchCircle*>(g)) {
            if (circ->centerUuid == fromUuid) circ->centerUuid = toUuid;
        }
        if (auto* pline = dynamic_cast<SketchPolyline*>(g)) {
            for (auto& uuid : pline->vertexUuids)
                if (uuid == fromUuid) uuid = toUuid;
        }
        if (auto* spline = dynamic_cast<SketchSpline*>(g)) {
            for (auto& uuid : spline->controlPointUuids)
                if (uuid == fromUuid) uuid = toUuid;
        }
        if (auto* ellipse = dynamic_cast<SketchEllipse*>(g)) {
            if (ellipse->centerUuid == fromUuid) ellipse->centerUuid = toUuid;
        }
    }

    // 刪除 from 點（從 m_geometries 中移除並釋放）
    m_geometries.removeAll(fromPt);
    m_uuidToGeomIndex.remove(fromUuid);
    delete fromPt;

    syncGeometryFromPoints();
    Q_EMIT geometryChanged();
}

QList<SketchGeometry*> Sketch::curvesReferencingPoint(const QString& ptUuid) const
{
    QList<SketchGeometry*> result;
    for (auto* g : m_geometries) {
        if (auto* line = dynamic_cast<SketchLine*>(g)) {
            if (line->startUuid == ptUuid || line->endUuid == ptUuid)
                result.append(g);
        } else if (auto* arc = dynamic_cast<SketchArc*>(g)) {
            if (arc->startUuid == ptUuid || arc->endUuid == ptUuid || arc->centerUuid == ptUuid)
                result.append(g);
        } else if (auto* circ = dynamic_cast<SketchCircle*>(g)) {
            if (circ->centerUuid == ptUuid)
                result.append(g);
        } else if (auto* pline = dynamic_cast<SketchPolyline*>(g)) {
            if (pline->vertexUuids.contains(ptUuid))
                result.append(g);
        } else if (auto* spline = dynamic_cast<SketchSpline*>(g)) {
            if (spline->controlPointUuids.contains(ptUuid))
                result.append(g);
        } else if (auto* ellipse = dynamic_cast<SketchEllipse*>(g)) {
            if (ellipse->centerUuid == ptUuid)
                result.append(g);
        }
    }
    return result;
}

QString Sketch::addLineGeom(const QVector2D& p1, const QVector2D& p2,
                             const QString& reuseStart, const QString& reuseEnd,
                             GeomRole role, bool emitSignals)
{
    QString startUuid = reuseStart.isEmpty()
        ? addPoint(p1, SketchPoint::Origin::Endpoint)
        : reuseStart;
    QString endUuid = reuseEnd.isEmpty()
        ? addPoint(p2, SketchPoint::Origin::Endpoint)
        : reuseEnd;

    auto* line = new SketchLine(p1, p2);
    line->startUuid = startUuid;
    line->endUuid   = endUuid;
    line->role      = role;
    m_geometries.append(line);

    if (emitSignals) {
        Q_EMIT geometryChanged();
        Q_EMIT rebuildRequested();
    }
    return line->uuid;
}

QString Sketch::addCircleGeom(const QVector2D& center, double radius,
                               const QString& reuseCenterUuid)
{
    QString centerUuid = reuseCenterUuid.isEmpty()
        ? addPoint(center, SketchPoint::Origin::Center)
        : reuseCenterUuid;

    auto* circ = new SketchCircle(center, radius);
    circ->centerUuid = centerUuid;
    m_geometries.append(circ);

    Q_EMIT geometryChanged();
    Q_EMIT rebuildRequested();
    return circ->uuid;
}

QStringList Sketch::addLineChainGeom(const QVector<QVector2D>& pts, bool closed)
{
    QStringList lineUuids;
    if (pts.size() < 2) {
        qWarning() << "[Sketch]" << name() << "addLineChainGeom needs at least 2 points";
        return lineUuids;
    }

    // Step 1：逐段建立獨立的 SketchLine（每段各自擁有獨立端點，不共用同一個 SketchPoint）
    for (int i = 0; i < pts.size() - 1; ++i) {
        lineUuids.append(addLineGeom(pts[i], pts[i + 1]));
    }
    if (closed) {
        lineUuids.append(addLineGeom(pts.last(), pts.first()));
    }

    // Step 2：相鄰線段的接點自動加上 Coincident 束制（前一段終點 ≈ 下一段起點）
    for (int i = 0; i + 1 < lineUuids.size(); ++i) {
        constrainCoincident(GeomRef(lineUuids[i],     GeomHandle::End),
                             GeomRef(lineUuids[i + 1], GeomHandle::Start));
    }
    // Step 3：封閉迴路：最後一段終點 與 第一段起點 重合
    if (closed && lineUuids.size() >= 2) {
        constrainCoincident(GeomRef(lineUuids.last(),  GeomHandle::End),
                             GeomRef(lineUuids.first(), GeomHandle::Start));
    }

    return lineUuids;
}

QString Sketch::addPolylineGeom(const QVector<QVector2D>& pts, bool closed,
                                 const QVector<QString>& reuseVertexUuids)
{
    auto* pline = new SketchPolyline(pts, closed);

    for (int i = 0; i < pts.size(); ++i) {
        QString uuid;
        if (i < reuseVertexUuids.size() && !reuseVertexUuids[i].isEmpty()) {
            uuid = reuseVertexUuids[i];
        } else {
            // 首尾共點（closed）：最後一點複用第一點
            if (closed && i == pts.size() - 1 && !pline->vertexUuids.isEmpty()) {
                uuid = pline->vertexUuids.first();
            } else {
                uuid = addPoint(pts[i], SketchPoint::Origin::Endpoint);
            }
        }
        pline->vertexUuids.append(uuid);
    }

    m_geometries.append(pline);
    Q_EMIT geometryChanged();
    Q_EMIT rebuildRequested();
    return pline->uuid;
}

QString Sketch::addSplineGeom(const QVector<QVector2D>& pts,
                               const QVector<QString>& reuseControlPointUuids)
{
    if (pts.size() < 3) {
        qWarning() << "[Sketch]" << name() << "spline needs at least 3 points";
        return {};
    }

    auto* spline = new SketchSpline(pts);

    for (int i = 0; i < pts.size(); ++i) {
        QString uuid;
        if (i < reuseControlPointUuids.size() && !reuseControlPointUuids[i].isEmpty()) {
            uuid = reuseControlPointUuids[i];
        } else {
            uuid = addPoint(pts[i], SketchPoint::Origin::Endpoint);
        }
        spline->controlPointUuids.append(uuid);
    }

    m_geometries.append(spline);
    Q_EMIT geometryChanged();
    Q_EMIT rebuildRequested();
    return spline->uuid;
}

QString Sketch::addEllipseGeom(const QVector2D& center,
                                double majorRadius, double minorRadius, double angle,
                                const QString& reuseCenterUuid)
{
    if (majorRadius <= 0 || minorRadius <= 0) {
        qWarning() << "[Sketch]" << name() << "ellipse radii must be positive";
        return {};
    }
    if (majorRadius < minorRadius) {
        qWarning() << "[Sketch]" << name() << "major radius must be >= minor radius";
        return {};
    }

    QString centerUuid = reuseCenterUuid.isEmpty()
        ? addPoint(center, SketchPoint::Origin::Center)
        : reuseCenterUuid;

    auto* ellipse = new SketchEllipse(center, majorRadius, minorRadius, angle);
    ellipse->centerUuid = centerUuid;
    m_geometries.append(ellipse);

    Q_EMIT geometryChanged();
    Q_EMIT rebuildRequested();
    return ellipse->uuid;
}

void Sketch::syncGeometryFromPoints()
{
    for (auto* g : m_geometries) {
        if (auto* line = dynamic_cast<SketchLine*>(g)) {
            if (auto* ps = point(line->startUuid)) {
                line->start = ps->pos;
                if (!line->points.isEmpty()) line->points[0] = ps->pos;
            }
            if (auto* pe = point(line->endUuid)) {
                line->end = pe->pos;
                if (line->points.size() > 1) line->points[1] = pe->pos;
            }
        } else if (auto* circ = dynamic_cast<SketchCircle*>(g)) {
            if (auto* pc = point(circ->centerUuid)) {
                circ->center = pc->pos;
            }
        } else if (auto* arc = dynamic_cast<SketchArc*>(g)) {
            // ✅ GAP 1 Fix: Arc 的 SketchPoint 同步回 arc->points[]
            // Arc 以 OCCT curve 為主，points[] 只作顯示/點選輔助用途
            // [0]=起點 [1]=中點 [2]=終點
            // （points[] 現在保證由 SketchArc 建構子初始化為 3 個元素，
            // 不再需要 isEmpty()/size()>2 這種只在「曾經被填過」時才成立
            // 的舊保護判斷——那兩個判斷過去因為 points 永遠是空的而永遠
            // 不成立，等於這段同步從未真的執行過。）
            if (arc->points.size() >= 3) {
                if (auto* ps = point(arc->startUuid))
                    arc->points[0] = ps->pos;
                if (auto* pe = point(arc->endUuid))
                    arc->points[2] = pe->pos;
                // 中點沒有獨立追蹤的 SketchPoint/UUID，改從目前的 curve
                // 幾何即時重新取樣，維持與 curve 一致（curve 才是 Arc 的
                // 權威幾何來源，points[] 只是給不方便直接處理 OCCT curve
                // 的下游模組用的快取）。
                if (!arc->curve.IsNull()) {
                    const double t0 = arc->curve->FirstParameter();
                    const double t1 = arc->curve->LastParameter();
                    const gp_Pnt midWorld = arc->curve->Value(0.5 * (t0 + t1));
                    arc->points[1] = m_plane->toPlane(
                        QVector3D(midWorld.X(), midWorld.Y(), midWorld.Z()));
                }
            }
        } else if (auto* pline = dynamic_cast<SketchPolyline*>(g)) {
            // Phase 0B：同步 Polyline 各頂點座標
            for (int i = 0; i < pline->vertexUuids.size(); ++i) {
                if (auto* pt = point(pline->vertexUuids[i])) {
                    if (i < pline->points.size())
                        pline->points[i] = pt->pos;
                }
            }
        } else if (auto* spline = dynamic_cast<SketchSpline*>(g)) {
            // Phase 0B：同步 Spline 各控制點座標
            for (int i = 0; i < spline->controlPointUuids.size(); ++i) {
                if (auto* pt = point(spline->controlPointUuids[i])) {
                    if (i < spline->points.size())
                        spline->points[i] = pt->pos;
                }
            }
        } else if (auto* ellipse = dynamic_cast<SketchEllipse*>(g)) {
            // Phase 0B：同步 Ellipse 圓心座標
            if (auto* pc = point(ellipse->centerUuid)) {
                ellipse->center = pc->pos;
            }
        }
    }
}

void Sketch::migrateFromLegacyFormat(const QJsonObject& json)
{
    // 舊格式沒有 "points" 陣列，從幾何座標重建
    // 相同座標的點視為同一點（模擬舊版 Coincident 語意）
    QHash<QString, QString> coordToPointUuid;  // "x,y" → pointUuid

    auto getOrCreate = [&](double x, double y) -> QString {
        QString key = QString("%1,%2").arg(x, 0, 'f', 6).arg(y, 0, 'f', 6);
        if (!coordToPointUuid.contains(key)) {
            coordToPointUuid[key] = addPoint(QVector2D(x, y), SketchPoint::Origin::Endpoint);
        }
        return coordToPointUuid[key];
    };

    for (const auto& gv : json["geometries"].toArray()) {
        QJsonObject g = gv.toObject();
        if (g["type"].toString() == "Line") {
            QString lineUuid; // will be set when line is added later by fromJson
            Q_UNUSED(getOrCreate(g["x1"].toDouble(), g["y1"].toDouble()));
            Q_UNUSED(getOrCreate(g["x2"].toDouble(), g["y2"].toDouble()));
        }
    }
    qDebug() << "[Sketch] migrateFromLegacyFormat: created" << points().size() << "points";
}

QList<Sketch::PointInfo> Sketch::listPoints() const
{
    QList<PointInfo> result;
    for (auto* g : m_geometries) {
        if (g->type != SketchGeometryType::Point) continue;
        auto* pt = static_cast<SketchPoint*>(g);
        PointInfo info;
        info.uuid   = pt->uuid;
        info.pos    = pt->pos;
        info.origin = pt->origin;

        for (auto* g : m_geometries) {
            if (auto* line = dynamic_cast<SketchLine*>(g)) {
                if (line->startUuid == pt->uuid)
                    info.referencedBy.append(line->uuid.left(8) + ".Start");
                if (line->endUuid == pt->uuid)
                    info.referencedBy.append(line->uuid.left(8) + ".End");
            } else if (auto* circ = dynamic_cast<SketchCircle*>(g)) {
                if (circ->centerUuid == pt->uuid)
                    info.referencedBy.append(circ->uuid.left(8) + ".Ctr");
            } else if (auto* pline = dynamic_cast<SketchPolyline*>(g)) {
                for (int i = 0; i < pline->vertexUuids.size(); ++i) {
                    if (pline->vertexUuids[i] == pt->uuid)
                        info.referencedBy.append(pline->uuid.left(8) + ".V" + QString::number(i));
                }
            } else if (auto* spline = dynamic_cast<SketchSpline*>(g)) {
                for (int i = 0; i < spline->controlPointUuids.size(); ++i) {
                    if (spline->controlPointUuids[i] == pt->uuid)
                        info.referencedBy.append(spline->uuid.left(8) + ".CP" + QString::number(i));
                }
            } else if (auto* ellipse = dynamic_cast<SketchEllipse*>(g)) {
                if (ellipse->centerUuid == pt->uuid)
                    info.referencedBy.append(ellipse->uuid.left(8) + ".Ctr");
            }
        }
        result.append(info);
    }
    return result;
}

// ── Phase 0B：新增 Constraint 便捷方法 ─────────────────────────────────────

QString Sketch::constrainMidpoint(const GeomRef& point, const QString& lineUuid) {
    return addConstraint(SketchConstraint::makeMidpoint(point, lineUuid));
}

QString Sketch::constrainSymmetric(const GeomRef& a, const GeomRef& b, const QString& axisUuid) {
    SketchConstraint c;
    c.uuid  = QUuid::createUuid().toString(QUuid::WithoutBraces);
    c.type  = ConstraintType::Symmetric;
    c.refs  = { a, b, GeomRef(axisUuid, GeomHandle::Curve) };
    return addConstraint(c);
}

QString Sketch::constrainCollinear(const QString& lineA, const QString& lineB) {
    // ⚠️ 修正：COLLINEAR 選「一點 + 一線」會 crash。
    // 舊版無條件把 lineA/lineB 都當「線」，用 GeomHandle::Curve 建立 refs，
    // 送進 CollinearEquation 後對獨立 SketchPoint 呼叫
    // indexFor(GeomHandle::End) 會讀到超出該點自身 DOF 範圍的
    // offset+2——若該點剛好是變數向量中最後一個元素，甚至會整個超出向量
    // 長度，觸發 QVector::operator[] 的 index-out-of-range assert 讓
    // 程式當掉。
    //
    // 現在依需求：COLLINEAR 應能接受「兩線」或「一點+一線」。這裡先判斷
    // 兩個參考各自的幾何型別，點用 GeomHandle::WholeGeom、線/曲線用
    // GeomHandle::Curve，讓 CollinearEquation 能透過
    // GeomVarLayout::isPoint 動態分辨並套用正確的方程式（1 條或 2 條）。
    // 「兩個都是點」不構成共線關係（缺少方向），直接擋掉。
    SketchGeometry* gA = findGeometry(lineA);
    SketchGeometry* gB = findGeometry(lineB);
    if (!gA || !gB) {
        qWarning() << "[Sketch] constrainCollinear: geom not found"
                   << lineA << lineB;
        return {};
    }
    const bool aIsPoint = gA->type == SketchGeometryType::Point;
    const bool bIsPoint = gB->type == SketchGeometryType::Point;
    if (aIsPoint && bIsPoint) {
        qWarning() << "[Sketch] constrainCollinear: 不支援兩個獨立點的共線約束"
                      "（COLLINEAR 需要至少一條線段/曲線）";
        return {};
    }

    SketchConstraint c;
    c.uuid  = QUuid::createUuid().toString(QUuid::WithoutBraces);
    c.type  = ConstraintType::Collinear;
    c.refs  = { GeomRef(lineA, aIsPoint ? GeomHandle::WholeGeom : GeomHandle::Curve),
                GeomRef(lineB, bIsPoint ? GeomHandle::WholeGeom : GeomHandle::Curve) };
    return addConstraint(c);
}

} // namespace cad
} // namespace aicad