/**
 * @file Sketch.h
 * @brief 與增強版 Plane 整合的 Sketch 類別範例
 */

#ifndef AICAD_CAD_SKETCH_ENHANCED_H
#define AICAD_CAD_SKETCH_ENHANCED_H

#include "Feature.h"
#include "Plane.h"
#include "sketch/ConstraintSolver.h"
#include "sketch/SketchConstraint.h"
#include "sketch/SketchRegion.h"

#include <optional>
#include <QVector>
#include <QVector2D>
#include <QJsonObject>
#include <QJsonArray>
#include <TopoDS_Wire.hxx>
#include <AIS_Shape.hxx>
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
    Spline       ///< 樣條曲線
};

/**
* @brief 幾何元素的角色
*
* Normal      : 正常幾何，參與 Wire 建立與 Extrude
* Construction: 建構線，僅供參考與約束，不進入輪廓
* Centerline  : 中心線（建構線的特殊樣式，語意為旋轉軸/對稱軸）
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

/**
 * @brief 草圖線段
 */
struct SketchLine : public SketchGeometry {
    QVector2D start;
    QVector2D end;

    SketchLine(const QVector2D& p1, const QVector2D& p2,
               GeomRole role = GeomRole::Normal)
        :SketchGeometry(SketchGeometryType::Line, role),  start(p1), end(p2)
    {
        points.clear();
        points.append(p1);  // ✅ points[0] = 起點
        points.append(p2);  // ✅ points[1] = 終點
    }
};

/**
 * @brief 草圖三點弧
 */
struct SketchArc : public SketchGeometry
{
    Handle(Geom_TrimmedCurve) curve;

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

    SketchPolyline(const QVector<QVector2D>& pts, bool isClosed,
                   GeomRole role = GeomRole::Normal)
        : SketchGeometry(SketchGeometryType::Polyline, role)
        , closed(isClosed) {
        points = pts;
    }
};

/**
 * @brief 草圖多段線
 */
struct SketchSpline : public SketchGeometry {

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
 * @brief 草圖圓
 */
struct SketchCircle : public SketchGeometry {
    QVector2D center;
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

/**
 * @brief 與增強版 Plane 整合的 Sketch 類別
 *
 * 主要變更：
 * - m_plane 從值成員改為指標成員
 * - 監聽 Plane 的事件（刪除、幾何改變）
 * - JSON 序列化使用 planeId 參考
 * - 空指標檢查和錯誤處理
 */
class Sketch : public Feature {
    Q_OBJECT

public:
    explicit Sketch(Document* parent = nullptr);
    ~Sketch() override;

    FeatureType type() const override { return FeatureType::Sketch; }
    bool rebuild() override;
    bool rebuildShapesOnly();  // ✅ 拖動用：只更新 AIS shape 不走 Document 路徑

    // ==================== 平面管理 ====================

    /**
     * @brief 取得關聯的平面（指標）
     */
    Plane* plane() const { return m_plane; }

    QVector3D planeToWorld(const QVector2D& planePt) const;

    /**
     * @brief 設定關聯的平面
     * @param plane 平面指標（不可為 null）
     */
    void setPlane(Plane* plane);

    /**
     * @brief 平面是否有效（非 null）
     */
    bool hasValidPlane() const { return m_plane != nullptr; }

    // ==================== 幾何元素管理 ====================

    void addGeometry(SketchGeometry* geom);
    void removeGeometry(int index);
    void clearGeometry();

    QList<SketchGeometry*> geometries() const { return m_geometries; }
    int geometryCount() const { return m_geometries.size(); }

    // 便捷方法：加入基本幾何
    void addLine(const QVector2D& p1, const QVector2D& p2);
    void addPolyline(const QVector<QVector2D>& points, bool closed = false);
    void addSpline(const QVector<QVector2D>& points);
    void addCircle(const QVector2D& center, double radius);
    void addEllipse(const QVector2D& center, double majorRadius, double minorRadius, double angle);
    void addRectangle(const QVector2D& corner1, const QVector2D& corner2);
    void addArc(const QVector2D& startPoint, const QVector2D& midPoint, const QVector2D& endPoint);

    // ==================== OCCT ====================

    QList<TopoDS_Wire> wires() const;
    QList<Handle(AIS_Shape)> aisShapes() const;
    const QList<QString>& aisShapeUuids() const;
    QList<Handle(AIS_Shape)> displayInContext(const Handle(AIS_InteractiveContext)& context);
    void eraseFromContext(const Handle(AIS_InteractiveContext)& context);
    TopoDS_Wire mainWire() const;
    bool hasClosedProfile() const;

    // ==================== 2D Region（剖面/面域）====================

    /**
     * @brief 偵測草圖中的所有閉合迴路，建立 2D region 列表
     *
     * 每次呼叫時重新計算（非快取），
     * 結果依 region 大小降序排列（最大外輪廓在前）。
     */
    QVector<SketchRegion> detectRegions() const;

    /**
     * @brief 根據 2D 點（草圖座標），選取包含該點的 region
     * @return 包含該點的 region，若無則回傳 std::nullopt
     */
    std::optional<SketchRegion>
    pickRegion(const QVector2D& sketchPt) const;

    bool hasExtrudableProfile() const;

    // ==================== 序列化 ====================

    /**
     * @brief 序列化（使用 planeId 參考）
     */
    QJsonObject toJson() const override;

    /**
     * @brief 反序列化（從 planeId 恢復參考）
     */
    bool fromJson(const QJsonObject& json) override;

    // ══════════════════════════════════════════════
    // 約束管理
    // ══════════════════════════════════════════════

    // 加入約束，回傳約束 UUID（失敗回傳空字串）
    QString addConstraint(const SketchConstraint& c);

    // 移除約束
    bool removeConstraint(const QString& uuid);

    // 查詢
    const QList<SketchConstraint>& constraints() const { return m_constraints; }
    SketchConstraint* findConstraint(const QString& uuid);

    // 根據幾何 UUID 找出所有相關約束
    QList<SketchConstraint*> constraintsOf(const QString& geomUuid);

    // 移除幾何時，同步清除相關約束
    void removeConstraintsOf(const QString& geomUuid);

    // 求解：回傳求解結果，成功後觸發 rebuild
    SolveResult solveConstraints();

    // 只計算 DOF，不求解
    int degreesOfFreedom() const;

    // 便捷 API（語意清晰）
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
    QString constrainDistance(const GeomRef& a, const GeomRef& b, double dist);
    QString constrainRadius(const QString& geomUuid, double radius);
    QString constrainPointOnCurve(const GeomRef& point, const QString& curveUuid);

    // ── 建構線便捷方法 ────────────────────────────────────────────
    void addConstructionLine(const QVector2D& p1, const QVector2D& p2);
    void addCenterline(const QVector2D& p1, const QVector2D& p2);
    void addConstructionCircle(const QVector2D& center, double radius);
    void addConstructionArc(const QVector2D& start,
                            const QVector2D& mid,
                            const QVector2D& end,
                            GeomRole role = GeomRole::Construction);

    // ── 查詢 ─────────────────────────────────────────────────────
    QList<SketchGeometry*> normalGeometries() const;        ///< 只回傳 Normal
    QList<SketchGeometry*> constructionGeometries() const;  ///< 只回傳建構線

    // geometries 查詢輔助
    SketchGeometry* findGeometry(const QString& uuid) const;

Q_SIGNALS:
    /**
     * @brief 平面改變時發出
     */
    void planeChanged(Plane* plane);

    /**
     * @brief 幾何元素改變時發出
     */
    void geometryChanged();

    // ✅ emit after every successful rebuild
    void rebuilt();

    void constraintAdded(const QString& uuid);
    void constraintRemoved(const QString& uuid);
    void constraintSolved(SolveResult result);

private Q_SLOTS:
    /**
     * @brief 當關聯的平面即將被刪除時調用
     */
    void onPlaneAboutToBeDeleted();

    /**
     * @brief 當關聯的平面幾何改變時調用
     */
    void onPlaneGeometryChanged();

private:
    /**
     * @brief 將 2D 點轉換為 3D（帶空指標檢查）
     */
    gp_Pnt toWorld(const QVector2D& point) const;

    /**
     * @brief 建立預設平面
     */
    void createDefaultPlane();

    /**
     * @brief 連接平面信號
     */
    void connectPlaneSignals();

    /**
     * @brief 斷開平面信號
     */
    void disconnectPlaneSignals();

    // ✅ 平面解析輔助方法（供 fromJson 使用）

    /**
     * @brief 依名稱比對 PlaneManager 中已登記的標準平面
     *        支援 "XY" / "YZ" / "XZ" / "ZX" 及其完整名稱變體
     * @param planeName  存檔中的 planeName 字串
     * @return 找到的 Plane*，或 nullptr
     */
    Plane* resolveStandardPlane(const QString& planeName);

    /**
     * @brief 從儲存的 planeData JSON 幾何資料重建平面
     *        先嘗試與現有平面幾何比對；若無吻合則建立新平面並向 PlaneManager 登記
     * @param planeJson  Plane::toJson() 所產生的 JSON 物件
     * @param hint       planeName（作為新平面的名稱提示）
     * @return 重建或吻合的 Plane*，或 nullptr
     */
    Plane* reconstructPlaneFromJson(const QJsonObject& planeJson,
                                    const QString& hint = QString());

private:
    Plane* m_plane;                       ///< 關聯的平面（指標）
    QList<SketchGeometry*> m_geometries;  ///< 幾何元素列表
    QList<TopoDS_Wire> m_wires;           ///< 快取的 Wire 列表
    QList<Handle(AIS_Shape)> m_aisShapes;
    QList<QString>            m_aisShapeUuids;
    QList<SketchConstraint> m_constraints;
    QList<Handle(AIS_Shape)>  m_constructionShapes;
    ConstraintSolver        m_solver;
    Handle(AIS_InteractiveContext) m_aisContext;

    void applyConstructionStyle(Handle(AIS_Shape)& shape, GeomRole role);
};

} // namespace cad
} // namespace aicad

Q_DECLARE_METATYPE(aicad::cad::Sketch*)

#endif // AICAD_CAD_SKETCH_ENHANCED_H
