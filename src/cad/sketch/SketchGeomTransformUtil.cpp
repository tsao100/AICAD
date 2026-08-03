/**
 * @file SketchGeomTransformUtil.cpp
 * @brief 見 SketchGeomTransformUtil.h 檔頭說明。
 */
#include "SketchGeomTransformUtil.h"
#include "../Sketch.h"

#include <QHash>
#include <QtMath>
#include <QDebug>
#include <cmath>

#include <GC_MakeArcOfCircle.hxx>
#include <Geom_Circle.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <gp_Pnt.hxx>

namespace aicad {
namespace cad {
namespace transform {

// ─────────────────────────────────────────────────────────────────────────
// Transform2D
// ─────────────────────────────────────────────────────────────────────────

Transform2D Transform2D::translation(const QVector2D& delta)
{
    Transform2D t;
    t.m_kind  = Kind::Translate;
    t.m_delta = delta;
    return t;
}

Transform2D Transform2D::rotation(const QVector2D& center, double angleRad)
{
    Transform2D t;
    t.m_kind      = Kind::Rotate;
    t.m_center    = center;
    t.m_angleRad  = angleRad;
    return t;
}

Transform2D Transform2D::mirror(const QVector2D& axisP0, const QVector2D& axisP1)
{
    Transform2D t;
    t.m_kind   = Kind::Mirror;
    t.m_axisP0 = axisP0;
    t.m_axisP1 = axisP1;
    const QVector2D dir = axisP1 - axisP0;
    t.m_axisAngle = std::atan2(double(dir.y()), double(dir.x()));
    return t;
}

QVector2D Transform2D::apply(const QVector2D& p) const
{
    switch (m_kind) {
    case Kind::Translate:
        return p + m_delta;

    case Kind::Rotate: {
        const QVector2D rel = p - m_center;
        const double c = std::cos(m_angleRad);
        const double s = std::sin(m_angleRad);
        const QVector2D rotated(float(double(rel.x()) * c - double(rel.y()) * s),
                                 float(double(rel.x()) * s + double(rel.y()) * c));
        return m_center + rotated;
    }

    case Kind::Mirror: {
        const QVector2D dir = m_axisP1 - m_axisP0;
        const double len2 = double(dir.x()) * dir.x() + double(dir.y()) * dir.y();
        if (len2 < 1e-12) {
            // 鏡射軸退化（兩點重合），安全防呆：不變換，避免除以零。
            return p;
        }
        const QVector2D rel = p - m_axisP0;
        const double t = (double(rel.x()) * dir.x() + double(rel.y()) * dir.y()) / len2;
        const QVector2D foot = m_axisP0 + dir * float(t);
        return foot * 2.0f - p;
    }
    }
    return p;
}

double Transform2D::applyAngle(double angleRad) const
{
    switch (m_kind) {
    case Kind::Translate:
        return angleRad;
    case Kind::Rotate:
        return angleRad + m_angleRad;
    case Kind::Mirror:
        return 2.0 * m_axisAngle - angleRad;
    }
    return angleRad;
}

// ─────────────────────────────────────────────────────────────────────────
// 內部輔助函式
// ─────────────────────────────────────────────────────────────────────────

namespace {

/// 將 OCCT 世界座標點轉換為 sketch 平面座標。
///
/// ⚠️ 這是本檔案最容易出錯的地方：SketchArc::curve 內部儲存的是「世界」
/// 座標（見 Sketch::addArcGeom() 內部呼叫 planeToWorld() 建構
/// GC_MakeArcOfCircle 的三個點），不是平面座標——只有當草圖恰好在 XY
/// 平面且原點與世界原點重合時，兩者數值才會剛好相同，這也是先前一版誤把
/// arc->curve->Value() 的 .X()/.Y() 直接當平面座標使用、在非 XY 平面草圖
/// 上會算錯的根本原因（比照 SketchGripProvider::computeGrips() 對 Arc
/// grip 拖曳的正確作法：全程用世界座標操作 gp_Pnt，只在最後呼叫
/// movePoint() 前才轉換成平面座標）。
inline QVector2D worldToPlaneVec(Sketch* sketch, const gp_Pnt& worldPt)
{
    if (!sketch || !sketch->plane()) return QVector2D(float(worldPt.X()), float(worldPt.Y()));
    return sketch->plane()->toPlane(QVector3D(float(worldPt.X()), float(worldPt.Y()), float(worldPt.Z())));
}

/// 將 sketch 平面座標點轉換為 OCCT 世界座標點（用於直接組裝
/// GC_MakeArcOfCircle 等 OCCT 曲線；若是呼叫 Sketch::addArcGeom() 之類的
/// 高階 API，那些 API 內部自己會做 planeToWorld()，不需要在外部先轉換）。
inline gp_Pnt planeVecToWorld(Sketch* sketch, const QVector2D& planePt)
{
    if (!sketch) return gp_Pnt(planePt.x(), planePt.y(), 0.0);
    const QVector3D w = sketch->planeToWorld(planePt);
    return gp_Pnt(w.x(), w.y(), w.z());
}

/// 搬動一個 SketchPoint（若尚未在本次操作中被搬過），並記錄到 touched 集合。
void moveOnce(Sketch* sketch, const QString& ptUuid, const Transform2D& xf,
              QSet<QString>& touched)
{
    if (ptUuid.isEmpty() || touched.contains(ptUuid)) return;
    SketchPoint* pt = sketch->point(ptUuid);
    if (!pt) return;
    sketch->movePoint(ptUuid, xf.apply(pt->pos));
    touched.insert(ptUuid);
}

void transformLineInPlace(Sketch* sketch, SketchLine* line, const Transform2D& xf,
                          QSet<QString>& touched)
{
    if (!line) return;
    moveOnce(sketch, line->startUuid, xf, touched);
    moveOnce(sketch, line->endUuid,   xf, touched);
}

void transformCircleInPlace(Sketch* sketch, SketchCircle* circ, const Transform2D& xf,
                            QSet<QString>& touched)
{
    if (!circ) return;
    moveOnce(sketch, circ->centerUuid, xf, touched);
    // 半徑在剛體變換（平移/旋轉）與鏡射下皆不變。
}

/// 依照既有 SketchGripProvider::computeGrips() 對 Arc grip 拖曳的作法：
/// 直接以變換後的起點/中點/終點三點重建 curve，而不是嘗試讓
/// ConstraintSolver 從 SketchPoint 反推（Arc 的 curve 在 solver 中是
/// 獨立變數，不由 startUuid/endUuid/centerUuid 驅動，詳見標頭檔說明）。
void transformArcInPlace(Sketch* sketch, SketchArc* arc, const Transform2D& xf,
                         QSet<QString>& touched)
{
    if (!arc || arc->curve.IsNull()) return;

    const double t0 = arc->curve->FirstParameter();
    const double t1 = arc->curve->LastParameter();
    const double tM = 0.5 * (t0 + t1);

    // arc->curve 是世界座標曲線，先轉成平面座標才能套用 Transform2D
    // （Transform2D 的 translate/rotate/mirror 都是定義在 sketch 平面座標
    // 系裡的 2D 變換）。
    const QVector2D start = worldToPlaneVec(sketch, arc->curve->Value(t0));
    const QVector2D mid   = worldToPlaneVec(sketch, arc->curve->Value(tM));
    const QVector2D end   = worldToPlaneVec(sketch, arc->curve->Value(t1));

    const QVector2D newStart = xf.apply(start);
    const QVector2D newMid   = xf.apply(mid);
    const QVector2D newEnd   = xf.apply(end);

    // 重建 curve 前，要把變換後的平面座標轉回世界座標（GC_MakeArcOfCircle
    // 需要世界座標的三點，比照 SketchGripProvider 既有的正確作法）。
    GC_MakeArcOfCircle maker(planeVecToWorld(sketch, newStart),
                             planeVecToWorld(sketch, newMid),
                             planeVecToWorld(sketch, newEnd));
    if (!maker.IsDone()) {
        qWarning() << "[SketchGeomTransformUtil] Arc 變換失敗（三點共線或重合，"
                      "無法決定唯一圓弧），略過 uuid=" << arc->uuid;
        return;
    }
    arc->curve = maker.Value();

    if (!arc->startUuid.isEmpty()) {
        sketch->movePoint(arc->startUuid, newStart);
        touched.insert(arc->startUuid);
    }
    if (!arc->endUuid.isEmpty()) {
        sketch->movePoint(arc->endUuid, newEnd);
        touched.insert(arc->endUuid);
    }
    if (!arc->centerUuid.isEmpty()) {
        // 既有 SketchGripProvider 的 Arc 拖曳並未同步 centerUuid（原始碼註解
        // 「圓心 UUID 無 points[] 對應」），這裡額外補上，讓以圓心為
        // OSnap／約束參考的其他幾何在整體變換後仍然正確。屬於本次順帶
        // 修正的既有小缺口，非本功能引入的新問題。
        Handle(Geom_Circle) baseCircle =
            Handle(Geom_Circle)::DownCast(arc->curve->BasisCurve());
        if (!baseCircle.IsNull()) {
            const QVector2D newCenter = worldToPlaneVec(sketch, baseCircle->Location());
            sketch->movePoint(arc->centerUuid, newCenter);
            touched.insert(arc->centerUuid);
        }
    }
}

void transformEllipseInPlace(Sketch* sketch, SketchEllipse* ell, const Transform2D& xf,
                             QSet<QString>& touched)
{
    if (!ell) return;
    ell->center = xf.apply(ell->center);
    ell->angle  = xf.applyAngle(ell->angle);

    if (!ell->centerUuid.isEmpty()) {
        sketch->movePoint(ell->centerUuid, ell->center);
        touched.insert(ell->centerUuid);
    }
}

void transformPolylineInPlace(Sketch* sketch, SketchPolyline* pl, const Transform2D& xf,
                              QSet<QString>& touched)
{
    if (!pl) return;
    for (int i = 0; i < pl->points.size(); ++i)
        pl->points[i] = xf.apply(pl->points[i]);

    for (const QString& vu : pl->vertexUuids)
        moveOnce(sketch, vu, xf, touched);
}

void transformPolygonInPlace(SketchPolygon* poly, const Transform2D& xf)
{
    if (!poly) return;
    for (int i = 0; i < poly->points.size(); ++i)
        poly->points[i] = xf.apply(poly->points[i]);
    // SketchPolygon 沒有 vertexUuids（舊格式相容用途，目前實際繪圖指令皆已
    // 改走 addLineChainGeom 產生獨立 SketchLine，見 Sketch::addPolyline 註解），
    // 因此沒有對應的 SketchPoint 需要同步。
}

void transformSplineInPlace(Sketch* sketch, SketchSpline* sp, const Transform2D& xf,
                            QSet<QString>& touched)
{
    if (!sp) return;
    for (int i = 0; i < sp->points.size(); ++i)
        sp->points[i] = xf.apply(sp->points[i]);

    for (const QString& cu : sp->controlPointUuids)
        moveOnce(sketch, cu, xf, touched);
}

} // 匿名 namespace

// ─────────────────────────────────────────────────────────────────────────
// 對外 API
// ─────────────────────────────────────────────────────────────────────────

QSet<QString> collectReferencedPointUuids(Sketch* sketch, const QStringList& geomUuids)
{
    QSet<QString> result;
    if (!sketch) return result;

    for (const QString& uuid : geomUuids) {
        SketchGeometry* g = sketch->findGeometry(uuid);
        if (!g) continue;

        switch (g->type) {
        case SketchGeometryType::Point:
            result.insert(g->uuid);
            break;
        case SketchGeometryType::Line: {
            auto* l = static_cast<SketchLine*>(g);
            if (!l->startUuid.isEmpty()) result.insert(l->startUuid);
            if (!l->endUuid.isEmpty())   result.insert(l->endUuid);
            break;
        }
        case SketchGeometryType::Circle: {
            auto* c = static_cast<SketchCircle*>(g);
            if (!c->centerUuid.isEmpty()) result.insert(c->centerUuid);
            break;
        }
        case SketchGeometryType::Arc: {
            auto* a = static_cast<SketchArc*>(g);
            if (!a->startUuid.isEmpty())  result.insert(a->startUuid);
            if (!a->endUuid.isEmpty())    result.insert(a->endUuid);
            if (!a->centerUuid.isEmpty()) result.insert(a->centerUuid);
            break;
        }
        case SketchGeometryType::Ellipse: {
            auto* e = static_cast<SketchEllipse*>(g);
            if (!e->centerUuid.isEmpty()) result.insert(e->centerUuid);
            break;
        }
        case SketchGeometryType::Polyline: {
            // 可能是 SketchPolyline 或（舊格式）SketchPolygon，兩者的 type
            // tag 皆為 Polyline，但彼此並非同一條繼承鏈，需用 dynamic_cast
            // 分辨，不能直接 static_cast。
            if (auto* pl = dynamic_cast<SketchPolyline*>(g)) {
                for (const QString& vu : pl->vertexUuids)
                    if (!vu.isEmpty()) result.insert(vu);
            }
            break;
        }
        case SketchGeometryType::Spline: {
            auto* s = static_cast<SketchSpline*>(g);
            for (const QString& cu : s->controlPointUuids)
                if (!cu.isEmpty()) result.insert(cu);
            break;
        }
        }
    }
    return result;
}

void applyToSelection(Sketch* sketch, const QStringList& geomUuids, const Transform2D& xf)
{
    if (!sketch) return;

    QSet<QString> touched;
    bool anyTouched = false;

    for (const QString& uuid : geomUuids) {
        SketchGeometry* g = sketch->findGeometry(uuid);
        if (!g) continue;

        switch (g->type) {
        case SketchGeometryType::Point: {
            auto* pt = static_cast<SketchPoint*>(g);
            moveOnce(sketch, pt->uuid, xf, touched);
            anyTouched = true;
            break;
        }
        case SketchGeometryType::Line:
            transformLineInPlace(sketch, static_cast<SketchLine*>(g), xf, touched);
            anyTouched = true;
            break;
        case SketchGeometryType::Circle:
            transformCircleInPlace(sketch, static_cast<SketchCircle*>(g), xf, touched);
            anyTouched = true;
            break;
        case SketchGeometryType::Arc:
            transformArcInPlace(sketch, static_cast<SketchArc*>(g), xf, touched);
            anyTouched = true;
            break;
        case SketchGeometryType::Ellipse:
            transformEllipseInPlace(sketch, static_cast<SketchEllipse*>(g), xf, touched);
            anyTouched = true;
            break;
        case SketchGeometryType::Polyline:
            if (auto* pl = dynamic_cast<SketchPolyline*>(g)) {
                transformPolylineInPlace(sketch, pl, xf, touched);
            } else if (auto* poly = dynamic_cast<SketchPolygon*>(g)) {
                transformPolygonInPlace(poly, xf);
            }
            anyTouched = true;
            break;
        case SketchGeometryType::Spline:
            transformSplineInPlace(sketch, static_cast<SketchSpline*>(g), xf, touched);
            anyTouched = true;
            break;
        }
    }

    if (anyTouched) {
        // 與 SketchGripProvider::onGripDragEnd() 相同的收尾方式：
        // 先讓約束求解器有機會依新基準位置重新收斂，再請求重建。
        sketch->solveConstraints();
        Q_EMIT sketch->rebuildRequested();
    }
}

QStringList cloneAndTransform(Sketch* sketch, const QStringList& geomUuids, const Transform2D& xf)
{
    QStringList result;
    if (!sketch) return result;

    // old SketchPoint uuid → new SketchPoint uuid，確保選取範圍內部共用的
    // 端點複製後仍然共用同一個新端點（不會在角落裂開）。
    QHash<QString, QString> ptMap;

    auto mapPoint = [&](const QString& oldUuid) -> QString {
        if (oldUuid.isEmpty()) return QString();
        auto it = ptMap.constFind(oldUuid);
        if (it != ptMap.constEnd()) return it.value();
        SketchPoint* oldPt = sketch->point(oldUuid);
        if (!oldPt) return QString();
        const QString newUuid = sketch->addPoint(xf.apply(oldPt->pos), oldPt->origin);
        ptMap.insert(oldUuid, newUuid);
        return newUuid;
    };

    for (const QString& uuid : geomUuids) {
        SketchGeometry* g = sketch->findGeometry(uuid);
        if (!g) continue;

        switch (g->type) {
        case SketchGeometryType::Point: {
            auto* pt = static_cast<SketchPoint*>(g);
            const QString nu = mapPoint(pt->uuid);
            if (!nu.isEmpty()) result.append(nu);
            break;
        }
        case SketchGeometryType::Line: {
            auto* line = static_cast<SketchLine*>(g);
            const QString ns = mapPoint(line->startUuid);
            const QString ne = mapPoint(line->endUuid);
            const QString newUuid = sketch->addLineGeom(
                xf.apply(line->start), xf.apply(line->end), ns, ne, line->role);
            result.append(newUuid);
            break;
        }
        case SketchGeometryType::Circle: {
            auto* circ = static_cast<SketchCircle*>(g);
            const QString nc = mapPoint(circ->centerUuid);
            const QString newUuid =
                sketch->addCircleGeom(xf.apply(circ->center), circ->radius, nc);
            if (auto* ng = sketch->findGeometry(newUuid)) ng->role = circ->role;
            result.append(newUuid);
            break;
        }
        case SketchGeometryType::Arc: {
            auto* arc = static_cast<SketchArc*>(g);
            if (arc->curve.IsNull()) break;
            const double t0 = arc->curve->FirstParameter();
            const double t1 = arc->curve->LastParameter();
            const double tM = 0.5 * (t0 + t1);
            const QVector2D start = worldToPlaneVec(sketch, arc->curve->Value(t0));
            const QVector2D mid   = worldToPlaneVec(sketch, arc->curve->Value(tM));
            const QVector2D end   = worldToPlaneVec(sketch, arc->curve->Value(t1));

            const QString ns = mapPoint(arc->startUuid);
            const QString ne = mapPoint(arc->endUuid);
            const QString nc = mapPoint(arc->centerUuid);

            const QString newUuid = sketch->addArcGeom(
                xf.apply(start), xf.apply(mid), xf.apply(end), ns, ne, nc);
            if (auto* ng = sketch->findGeometry(newUuid)) ng->role = arc->role;
            result.append(newUuid);
            break;
        }
        case SketchGeometryType::Ellipse: {
            auto* ell = static_cast<SketchEllipse*>(g);
            const QString nc = mapPoint(ell->centerUuid);
            const QString newUuid = sketch->addEllipseGeom(
                xf.apply(ell->center), ell->majorRadius, ell->minorRadius,
                xf.applyAngle(ell->angle), nc);
            if (auto* ng = sketch->findGeometry(newUuid)) ng->role = ell->role;
            result.append(newUuid);
            break;
        }
        case SketchGeometryType::Polyline: {
            if (auto* pl = dynamic_cast<SketchPolyline*>(g)) {
                QVector<QVector2D> newPts;
                newPts.reserve(pl->points.size());
                for (const QVector2D& p : pl->points) newPts.append(xf.apply(p));

                QVector<QString> newVerts;
                newVerts.reserve(pl->vertexUuids.size());
                for (const QString& vu : pl->vertexUuids) newVerts.append(mapPoint(vu));

                const QString newUuid = sketch->addPolylineGeom(newPts, pl->closed, newVerts);
                if (auto* ng = sketch->findGeometry(newUuid)) ng->role = pl->role;
                result.append(newUuid);
            } else if (auto* poly = dynamic_cast<SketchPolygon*>(g)) {
                QVector<QVector2D> newPts;
                newPts.reserve(poly->points.size());
                for (const QVector2D& p : poly->points) newPts.append(xf.apply(p));

                const QString newUuid = sketch->addPolylineGeom(newPts, poly->closed);
                if (auto* ng = sketch->findGeometry(newUuid)) ng->role = poly->role;
                result.append(newUuid);
            }
            break;
        }
        case SketchGeometryType::Spline: {
            auto* sp = static_cast<SketchSpline*>(g);
            QVector<QVector2D> newPts;
            newPts.reserve(sp->points.size());
            for (const QVector2D& p : sp->points) newPts.append(xf.apply(p));

            QVector<QString> newCtrl;
            newCtrl.reserve(sp->controlPointUuids.size());
            for (const QString& cu : sp->controlPointUuids) newCtrl.append(mapPoint(cu));

            const QString newUuid = sketch->addSplineGeom(newPts, newCtrl);
            if (auto* ng = sketch->findGeometry(newUuid)) ng->role = sp->role;
            result.append(newUuid);
            break;
        }
        }
    }

    if (!result.isEmpty()) {
        sketch->solveConstraints();
        Q_EMIT sketch->rebuildRequested();
    }

    return result;
}

QStringList stretchWithinRect(Sketch* sketch, const QVector2D& rectMin, const QVector2D& rectMax,
                              const QVector2D& delta)
{
    QStringList affected;
    if (!sketch) return affected;

    auto inRect = [&](const QVector2D& p) {
        return p.x() >= rectMin.x() && p.x() <= rectMax.x() &&
               p.y() >= rectMin.y() && p.y() <= rectMax.y();
    };

    QSet<QString> touched;          // 已透過直接 movePoint 搬動的點（局部拉伸）
    QStringList   fullyEnclosed;    // 交給 applyToSelection 整體搬動的幾何 uuid

    const QList<SketchGeometry*>& geoms = sketch->geometriesRef();
    for (SketchGeometry* g : geoms) {
        if (!g) continue;

        switch (g->type) {
        case SketchGeometryType::Point: {
            auto* pt = static_cast<SketchPoint*>(g);
            if (inRect(pt->pos)) fullyEnclosed.append(g->uuid);
            break;
        }
        case SketchGeometryType::Line: {
            auto* l = static_cast<SketchLine*>(g);
            SketchPoint* sp = sketch->point(l->startUuid);
            SketchPoint* ep = sketch->point(l->endUuid);
            const bool sIn = sp && inRect(sp->pos);
            const bool eIn = ep && inRect(ep->pos);
            if (sIn && eIn) {
                fullyEnclosed.append(g->uuid);
            } else {
                // 局部拉伸：只搬動落在窗內的那一端。
                if (sIn && !touched.contains(l->startUuid)) {
                    sketch->movePoint(l->startUuid, sp->pos + delta);
                    touched.insert(l->startUuid);
                    affected.append(g->uuid);
                }
                if (eIn && !touched.contains(l->endUuid)) {
                    sketch->movePoint(l->endUuid, ep->pos + delta);
                    touched.insert(l->endUuid);
                    if (!affected.contains(g->uuid)) affected.append(g->uuid);
                }
            }
            break;
        }
        case SketchGeometryType::Circle: {
            auto* c = static_cast<SketchCircle*>(g);
            if (inRect(c->center)) fullyEnclosed.append(g->uuid);
            break;
        }
        case SketchGeometryType::Arc: {
            // 不支援局部拉伸（見標頭檔說明）：起點/終點/圓心三點須全部落在
            // 窗內才整體搬動，否則完全不受影響。
            auto* a = static_cast<SketchArc*>(g);
            SketchPoint* sp = sketch->point(a->startUuid);
            SketchPoint* ep = sketch->point(a->endUuid);
            SketchPoint* cp = sketch->point(a->centerUuid);
            if (sp && ep && cp && inRect(sp->pos) && inRect(ep->pos) && inRect(cp->pos))
                fullyEnclosed.append(g->uuid);
            break;
        }
        case SketchGeometryType::Ellipse: {
            auto* e = static_cast<SketchEllipse*>(g);
            if (inRect(e->center)) fullyEnclosed.append(g->uuid);
            break;
        }
        case SketchGeometryType::Polyline: {
            if (auto* pl = dynamic_cast<SketchPolyline*>(g)) {
                bool allIn = true, anyIn = false;
                QVector<SketchPoint*> verts;
                verts.reserve(pl->vertexUuids.size());
                for (const QString& vu : pl->vertexUuids) {
                    SketchPoint* vp = sketch->point(vu);
                    verts.append(vp);
                    const bool in = vp && inRect(vp->pos);
                    allIn = allIn && in;
                    anyIn = anyIn || in;
                }
                if (!anyIn) break;
                if (allIn) {
                    fullyEnclosed.append(g->uuid);
                } else {
                    for (int i = 0; i < pl->vertexUuids.size(); ++i) {
                        SketchPoint* vp = verts[i];
                        if (!vp || touched.contains(pl->vertexUuids[i])) continue;
                        if (inRect(vp->pos)) {
                            sketch->movePoint(pl->vertexUuids[i], vp->pos + delta);
                            touched.insert(pl->vertexUuids[i]);
                        }
                    }
                    affected.append(g->uuid);
                }
            }
            // SketchPolygon（舊格式相容，無 vertexUuids）：沒有可搬動的 SketchPoint
            // 可供局部拉伸判斷，本函式不處理，維持原狀。
            break;
        }
        case SketchGeometryType::Spline: {
            auto* sp = static_cast<SketchSpline*>(g);
            bool allIn = true, anyIn = false;
            QVector<SketchPoint*> ctrls;
            ctrls.reserve(sp->controlPointUuids.size());
            for (const QString& cu : sp->controlPointUuids) {
                SketchPoint* cp = sketch->point(cu);
                ctrls.append(cp);
                const bool in = cp && inRect(cp->pos);
                allIn = allIn && in;
                anyIn = anyIn || in;
            }
            if (!anyIn) break;
            if (allIn) {
                fullyEnclosed.append(g->uuid);
            } else {
                for (int i = 0; i < sp->controlPointUuids.size(); ++i) {
                    SketchPoint* cp = ctrls[i];
                    if (!cp || touched.contains(sp->controlPointUuids[i])) continue;
                    if (inRect(cp->pos)) {
                        sketch->movePoint(sp->controlPointUuids[i], cp->pos + delta);
                        touched.insert(sp->controlPointUuids[i]);
                    }
                }
                affected.append(g->uuid);
            }
            break;
        }
        }
    }

    if (!fullyEnclosed.isEmpty()) {
        // applyToSelection() 內部會統一呼叫一次 solveConstraints()／
        // rebuildRequested()，一併涵蓋上面已經先做的局部 movePoint（因為
        // 那些都發生在這一步之前，同一次 solve 就能一起收斂）。
        applyToSelection(sketch, fullyEnclosed, Transform2D::translation(delta));
        for (const QString& u : fullyEnclosed)
            if (!affected.contains(u)) affected.append(u);
    } else if (!touched.isEmpty()) {
        sketch->solveConstraints();
        Q_EMIT sketch->rebuildRequested();
    }

    return affected;
}

} // namespace transform
} // namespace cad
} // namespace aicad
