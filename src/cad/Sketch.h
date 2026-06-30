/**
 * @file Sketch.h
 * @brief 與增強版 Plane 整合的 Sketch 類別
 *        Phase 0B：SketchPoint 一等公民架構
 */

#ifndef AICAD_CAD_SKETCH_ENHANCED_H
#define AICAD_CAD_SKETCH_ENHANCED_H

#include "Feature.h"
#include "Plane.h"
#include "sketch/ConstraintSolver.h"
#include "sketch/SketchConstraint.h"
#include "sketch/SketchRegion.h"
#include "../core/ParameterStore.h"

#include <optional>
#include <QVector>
#include <QVector2D>
#include <QJsonObject>
#include <QJsonArray>
#include <QUuid>
#include <TopoDS_Wire.hxx>
#include <AIS_Shape.hxx>
#include <AIS_InteractiveObject.hxx>
#include <Geom_TrimmedCurve.hxx>

namespace aicad {
namespace cad {

/**
 * @brief 草圖幾何類型
 */
enum class SketchGeometryType {
    Line,        ///< 直線
    Arc,         ///< 圓弧
    Circle,      ///< 圓
    Ellipse,     ///< 橢圓
    Polyline,    ///< 多段線
    Spline,      ///< 樣條曲線
    Point,       ///< 獨立點（Phase 0B 新增）
};

/**
* @brief 幾何元素的角色
*/
enum class GeomRole {
    Normal,
    Construction,
    Centerline,
};

/**
 * @brief 草圖幾何元素基礎類別
 */
struct SketchGeometry {
    QString    uuid;
    SketchGeometryType type;
    GeomRole   role = GeomRole::Normal;
    QVector<QVector2D> points;

    SketchGeometry(SketchGeometryType t, GeomRole r = GeomRole::Normal)
        : uuid(QUuid::createUuid().toString(QUuid::WithoutBraces))
        , type(t), role(r) {}

    bool isConstruction() const {
        return role == GeomRole::Construction || role == GeomRole::Centerline;
    }
    virtual ~SketchGeometry() = default;
};

// ─────────────────────────────────────────────────────────────────────────────
// Phase 0B：SketchPoint — 一等公民點
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief 草圖點（一等公民幾何元素）
 *
 * 可由以下來源建立：
 *  - 繪製曲線時自動建立並關聯（端點、圓心）
 *  - 使用者明確繪製「構建點」
 *  - 兩條曲線共享端點時共用同一個 SketchPoint
 */
struct SketchPoint : public SketchGeometry {
    QVector2D pos;

    enum class Origin {
        Explicit,       ///< 使用者明確繪製的獨立點
        Endpoint,       ///< 曲線端點（由繪圖命令建立）
        Center,         ///< 圓/弧/橢圓的圓心
        Intersection,   ///< 兩條曲線的交點（未來擴充）
    };
    Origin origin = Origin::Endpoint;

    explicit SketchPoint(const QVector2D& p,
                         Origin o = Origin::Endpoint,
                         GeomRole r = GeomRole::Normal)
        : SketchGeometry(SketchGeometryType::Point, r)
        , pos(p), origin(o)
    {
        points.clear();
        points.append(p);
    }
};

/**
 * @brief 草圖線段（Phase 0B：引用點 UUID）
 */
struct SketchLine : public SketchGeometry {
    // Phase 0B：新增 UUID 引用（向後相容保留 start/end）
    QString startUuid;   ///< → SketchPoint UUID
    QString endUuid;     ///< → SketchPoint UUID

    // 向後相容：由 Sketch 物件透過 UUID 取得實際座標
    QVector2D start;
    QVector2D end;

    SketchLine(const QVector2D& p1, const QVector2D& p2,
               GeomRole role = GeomRole::Normal)
        : SketchGeometry(SketchGeometryType::Line, role)
        , start(p1), end(p2)
    {
        points.clear();
        points.append(p1);
        points.append(p2);
    }
};

/**
 * @brief 草圖三點弧（Phase 0B：引用點 UUID）
 */
struct SketchArc : public SketchGeometry
{
    Handle(Geom_TrimmedCurve) curve;
    QString startUuid;    ///< 弧起點 UUID
    QString endUuid;      ///< 弧終點 UUID
    QString centerUuid;   ///< 弧圓心 UUID

    SketchArc(const Handle(Geom_TrimmedCurve)& c,
              GeomRole role = GeomRole::Normal)
        : SketchGeometry(SketchGeometryType::Arc, role)
        , curve(c)
    {
    }
};

/**
 * @brief 草圖多段線
 */
struct SketchPolyline : public SketchGeometry {
    bool closed;
    QVector<QString> vertexUuids;  ///< 各頂點對應的 SketchPoint UUID（Phase 0B）

    SketchPolyline(const QVector<QVector2D>& pts, bool isClosed,
                   GeomRole role = GeomRole::Normal)
        : SketchGeometry(SketchGeometryType::Polyline, role)
        , closed(isClosed) {
        points = pts;
    }
};

/**
 * @brief 草圖樣條
 */
struct SketchSpline : public SketchGeometry {
    QVector<QString> controlPointUuids;  ///< 各控制點對應的 SketchPoint UUID（Phase 0B）

    SketchSpline(const QVector<QVector2D>& pts,
                 GeomRole role = GeomRole::Normal)
        : SketchGeometry(SketchGeometryType::Spline, role)
    {
        points = pts;
    }
};

/**
 * @brief 草圖正多邊形
 */
struct SketchPolygon : public SketchGeometry {
    bool closed;

    SketchPolygon(const QVector<QVector2D>& pts, bool isClosed = true,
                  GeomRole role = GeomRole::Normal)
        : SketchGeometry(SketchGeometryType::Polyline, role)
        , closed(isClosed) {
        points = pts;
    }
};

/**
 * @brief 草圖圓（Phase 0B：引用圓心 UUID）
 */
struct SketchCircle : public SketchGeometry {
    QString   centerUuid;  ///< 圓心 UUID（Phase 0B）
    QVector2D center;      ///< 向後相容
    double radius;

    SketchCircle(const QVector2D& c, double r,
                 GeomRole role = GeomRole::Normal)
        : SketchGeometry(SketchGeometryType::Circle, role)
        , center(c)
        , radius(r) {
    }
};

/**
 * @brief 草圖橢圓
 */
struct SketchEllipse : public SketchGeometry {
    QString   centerUuid;  ///< 橢圓圓心 UUID（Phase 0B）
    QVector2D center;
    double majorRadius;
    double minorRadius;
    double angle;

    SketchEllipse(const QVector2D& c, double r1, double r2, double a,
                  GeomRole role = GeomRole::Normal)
        : SketchGeometry(SketchGeometryType::Ellipse, role)
        , center(c)
        , majorRadius(r1)
        , minorRadius(r2)
        , angle(a)
    {
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Sketch 類別
// ─────────────────────────────────────────────────────────────────────────────

class Sketch : public Feature {
    Q_OBJECT

public:
    explicit Sketch(Document* parent = nullptr);
    ~Sketch() override;

    FeatureType type() const override { return FeatureType::Sketch; }
    bool rebuild() override;
    bool rebuildShapesOnly();

    // ==================== 平面管理 ====================

    Plane* plane() const { return m_plane; }
    QVector3D planeToWorld(const QVector2D& planePt) const;
    void setPlane(Plane* plane);
    bool hasValidPlane() const { return m_plane != nullptr; }

    // ==================== 幾何元素管理 ====================

    void addGeometry(SketchGeometry* geom);
    void removeGeometry(int index);
    /// 依 UUID 移除幾何元素（ERASE 命令用）。找不到回傳 false。
    bool removeGeometry(const QString& uuid);
    void clearGeometry();

    QList<SketchGeometry*> geometries() const { return m_geometries; }
    const QList<SketchGeometry*>& geometriesRef() const { return m_geometries; }
    int geometryCount() const { return m_geometries.size(); }

    aicad::core::ParameterStore* parameterStore() {
        if (!m_parameterStore)
            m_parameterStore = new aicad::core::ParameterStore(this);
        return m_parameterStore;
    }

    // ── 基本幾何添加（Phase 0B：傳回 UUID，自動建立點）─────────────────────
    QString addLineGeom(const QVector2D& p1, const QVector2D& p2,
                        const QString& reuseStart = QString(),
                        const QString& reuseEnd   = QString());
    QString addCircleGeom(const QVector2D& center, double radius,
                          const QString& reuseCenterUuid = QString());
    QString addArcGeom(const QVector2D& startPoint,
                       const QVector2D& midPoint,
                       const QVector2D& endPoint,
                       const QString& reuseStartUuid  = QString(),
                       const QString& reuseEndUuid    = QString(),
                       const QString& reuseCenterUuid = QString());
    QString addPolylineGeom(const QVector<QVector2D>& pts, bool closed,
                            const QVector<QString>& reuseVertexUuids = {});
    QString addSplineGeom(const QVector<QVector2D>& pts,
                          const QVector<QString>& reuseControlPointUuids = {});
    QString addEllipseGeom(const QVector2D& center,
                           double majorRadius, double minorRadius, double angle,
                           const QString& reuseCenterUuid = QString());

    // 向後相容的舊版方法（void，包裝新版）
    void addLine(const QVector2D& p1, const QVector2D& p2);
    void addPolyline(const QVector<QVector2D>& points, bool closed = false);
    void addSpline(const QVector<QVector2D>& points);
    void addCircle(const QVector2D& center, double radius);
    void addEllipse(const QVector2D& center, double majorRadius, double minorRadius, double angle);
    void addRectangle(const QVector2D& corner1, const QVector2D& corner2);
    void addArc(const QVector2D& startPoint, const QVector2D& midPoint, const QVector2D& endPoint);

    // ==================== Phase 0B：點管理 API ====================

    /**
     * 建立一個新的獨立 SketchPoint，回傳其 UUID
     */
    QString addPoint(const QVector2D& pos,
                     SketchPoint::Origin origin = SketchPoint::Origin::Explicit);

    /**
     * 取得指定 UUID 的點（若不存在回傳 nullptr）
     */
    SketchPoint* point(const QString& uuid) const;

    /**
     * 所有點（含曲線端點）
     */
    QList<SketchPoint*> points() const;

    /**
     * 移動一個點（會觸發 Solver 重算，所有引用此點的曲線自動跟隨）
     */
    void movePoint(const QString& uuid, const QVector2D& newPos);

    /**
     * 合併兩個點（讓所有引用 fromUuid 的曲線改引用 toUuid）
     * 等同於施加 Coincident
     */
    void mergePoints(const QString& fromUuid, const QString& toUuid);

    /**
     * 查詢哪些曲線引用了此點
     */
    QList<SketchGeometry*> curvesReferencingPoint(const QString& pointUuid) const;

    // ==================== OCCT ====================

    QList<TopoDS_Wire> wires() const;
    QList<Handle(AIS_InteractiveObject)> aisShapes() const;
    const QList<QString>& aisShapeUuids() const;
    QList<Handle(AIS_InteractiveObject)> displayInContext(const Handle(AIS_InteractiveContext)& context);
    void eraseFromContext(const Handle(AIS_InteractiveContext)& context);
    TopoDS_Wire mainWire() const;
    bool hasClosedProfile() const;

    // ==================== 2D Region ====================

    QVector<SketchRegion> detectRegions() const;
    std::optional<SketchRegion> pickRegion(const QVector2D& sketchPt) const;
    bool hasExtrudableProfile() const;

    // ==================== 序列化 ====================

    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;

    // ==================== 約束管理 ====================

    QString addConstraint(const SketchConstraint& c);
    bool removeConstraint(const QString& uuid);
    const QList<SketchConstraint>& constraints() const { return m_constraints; }
    QList<SketchConstraint>& constraintsMutable() { return m_constraints; }
    SketchConstraint* findConstraint(const QString& uuid);
    QList<SketchConstraint*> constraintsOf(const QString& geomUuid);
    void removeConstraintsOf(const QString& geomUuid);
    /// 僅更新尺寸線偏移，不重新求解（拖曳尺寸線時輕量更新）
    bool updateConstraintDimOffset(const QString& uuid, double offsetX, double offsetY);
    SolveResult solveConstraints();
    SolveResult solveWithStore(const aicad::core::ParameterStore* store);
    int degreesOfFreedom() const;

    // 便捷 API
    QString constrainCoincident(const GeomRef& a, const GeomRef& b);
    QString constrainHorizontal(const QString& lineUuid);
    QString constrainVertical(const QString& lineUuid);
    QString constrainParallel(const QString& lineA, const QString& lineB);
    QString constrainPerpendicular(const QString& lineA, const QString& lineB);
    QString constrainTangent(const QString& geomA, const QString& geomB);
    QString constrainEqualLength(const QString& lineA, const QString& lineB);
    QString constrainEqualRadius(const QString& lineA, const QString& lineB);
    QString constrainConcentric(const QString& geomA, const QString& geomB);
    QString constrainFixed(const QString& geomUuid);

    // ── 草圖平面參考幾何（X 軸 / Y 軸 / 原點）─────────────────────────────
    // 這些幾何在第一次呼叫時 lazy-init，屬於 Fixed 參考幾何（不可移動）。
    // AIS 層用 "sketch_xaxis:<id>" 等 UUID 顯示；約束解析時用這裡的真實 UUID。
    QString xAxisGeomUuid();    ///< Sketch 內 X 軸 SketchLine 的 UUID
    QString yAxisGeomUuid();    ///< Sketch 內 Y 軸 SketchLine 的 UUID
    QString originPointUuid();  ///< Sketch 內原點 SketchPoint 的 UUID
    QString constrainDistance(const GeomRef& a, const GeomRef& b, double dist);
    QString constrainRadius(const QString& geomUuid, double radius);
    QString constrainRadius(const GeomRef& ref, double radius);
    QString constrainFixedX(const GeomRef& point, double x);
    QString constrainFixedY(const GeomRef& point, double y);
    QString constrainAngle(const GeomRef& a, const GeomRef& b, double angleRad);
    QString constrainPointOnCurve(const GeomRef& point, const QString& curveUuid);
    QString constrainMidpoint(const GeomRef& point, const QString& lineUuid);
    QString constrainSymmetric(const GeomRef& a, const GeomRef& b, const QString& axisUuid);
    QString constrainCollinear(const QString& lineA, const QString& lineB);

    // ── 建構線便捷方法 ────────────────────────────────────────────
    void addConstructionLine(const QVector2D& p1, const QVector2D& p2);
    void addCenterline(const QVector2D& p1, const QVector2D& p2);
    void addConstructionCircle(const QVector2D& center, double radius);
    void addConstructionArc(const QVector2D& start,
                            const QVector2D& mid,
                            const QVector2D& end,
                            GeomRole role = GeomRole::Construction);

    // ── 查詢 ─────────────────────────────────────────────────────
    QList<SketchGeometry*> normalGeometries() const;
    QList<SketchGeometry*> constructionGeometries() const;
    SketchGeometry* findGeometry(const QString& uuid) const;

    /**
     * LISTPOINTS 命令用：取得所有點的摘要資訊
     */
    struct PointInfo {
        QString uuid;
        QVector2D pos;
        SketchPoint::Origin origin;
        QStringList referencedBy;  ///< 格式："lineUuid.Start"、"circUuid.Center"
    };
    QList<PointInfo> listPoints() const;

Q_SIGNALS:
    void planeChanged(Plane* plane);
    void geometryChanged();
    void rebuilt();
    void constraintAdded(const QString& uuid);
    void constraintRemoved(const QString& uuid);
    void constraintSolved(SolveResult result);

public Q_SLOTS:
    void scheduleRebuild();

private Q_SLOTS:
    void onPlaneAboutToBeDeleted();
    void onPlaneGeometryChanged();

private:
    gp_Pnt toWorld(const QVector2D& point) const;
    void createDefaultPlane();
    void connectPlaneSignals();
    void disconnectPlaneSignals();
    Plane* resolveStandardPlane(const QString& planeName);
    Plane* reconstructPlaneFromJson(const QJsonObject& planeJson,
                                    const QString& hint = QString());

    // Phase 0B：從舊格式 JSON 遷移
    void migrateFromLegacyFormat(const QJsonObject& json);

    // Phase 0B：同步曲線的 start/end 座標（透過 UUID 查詢點座標）
    void syncGeometryFromPoints();

    /** Cheap content hash of a geometry's solved shape (type/role/points/
     *  type-specific fields). Used to detect whether a geometry's AIS
     *  representation actually needs rebuilding after solve(). */
    static quint64 geometryFingerprint(const SketchGeometry* geom);

private:
    Plane* m_plane;
    QList<SketchGeometry*> m_geometries;
    mutable QHash<QString, int> m_uuidToGeomIndex;  ///< UUID → m_geometries 索引（O(1) 查找 cache）
    QList<TopoDS_Wire> m_wires;
    QList<Handle(AIS_InteractiveObject)> m_aisShapes;
    QList<QString>            m_aisShapeUuids;
    /**
     * @brief Content fingerprint of each geometry, index-aligned with
     *        m_aisShapes/m_aisShapeUuids, used by rebuildShapesOnly() to
     *        skip AIS Erase/Display churn for geometries whose solved
     *        shape didn't actually change since the last solve.
     */
    QHash<QString, quint64> m_geomFingerprints;
    QList<SketchConstraint> m_constraints;
    QList<Handle(AIS_Shape)>  m_constructionShapes;
    QHash<QString, quint64>   m_constructionFingerprints;  ///< parallel cache for m_constructionShapes
    ConstraintSolver        m_solver;
    Handle(AIS_InteractiveContext) m_aisContext;
    aicad::core::ParameterStore*  m_parameterStore = nullptr;

    /**
     * @brief When true, rebuild() skips its Q_EMIT rebuilt() call.
     *
     * Set by rebuildShapesOnly() around its internal rebuild() call: at
     * that point m_aisShapes only holds *candidate* AIS handles (some of
     * which rebuildShapesOnly()'s diff will immediately discard in favour
     * of reused old handles for unchanged geometry). Letting rebuilt() fire
     * there would make CadView::onSketchRebuilt() register the
     * soon-to-be-discarded candidate pointers instead of the final ones,
     * breaking pick lookups (e.g. GDIM's second selection) for any
     * geometry whose AIS handle didn't actually change.
     * rebuildShapesOnly() re-emits rebuilt() itself once m_aisShapes
     * reflects the final, diffed state.
     */
    bool m_suppressRebuiltSignal = false;

    // 草圖平面參考幾何 UUID（lazy-init，初次存取時建立）
    QString m_xAxisGeomUuid;
    QString m_yAxisGeomUuid;
    QString m_originPointUuid;

    void applyConstructionStyle(Handle(AIS_Shape)& shape, GeomRole role);
};

} // namespace cad
} // namespace aicad

Q_DECLARE_METATYPE(aicad::cad::Sketch*)

#endif // AICAD_CAD_SKETCH_ENHANCED_H