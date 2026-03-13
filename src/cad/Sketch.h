/**
 * @file Sketch.h
 * @brief 與增強版 Plane 整合的 Sketch 類別範例
 */

#ifndef AICAD_CAD_SKETCH_ENHANCED_H
#define AICAD_CAD_SKETCH_ENHANCED_H

#include "Feature.h"
#include "Plane.h"  // 增強版 Plane
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
 * @brief 草圖幾何元素基礎類別
 */
struct SketchGeometry {
    SketchGeometryType type;
    QVector<QVector2D> points;

    SketchGeometry(SketchGeometryType t) : type(t) {}
    virtual ~SketchGeometry() = default;
};

/**
 * @brief 草圖線段
 */
struct SketchLine : public SketchGeometry {
    QVector2D start;
    QVector2D end;

    SketchLine(const QVector2D& p1, const QVector2D& p2)
        : start(p1), end(p2), SketchGeometry(SketchGeometryType::Line)
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

    SketchArc(const Handle(Geom_TrimmedCurve)& c)
        : SketchGeometry(SketchGeometryType::Arc)
        , curve(c)
    {
    }
};

/**
 * @brief 草圖多段線
 */
struct SketchPolyline : public SketchGeometry {
    bool closed;

    SketchPolyline(const QVector<QVector2D>& pts, bool isClosed)
        : SketchGeometry(SketchGeometryType::Polyline)
        , closed(isClosed) {
        points = pts;
    }
};

/**
 * @brief 草圖多段線
 */
struct SketchSpline : public SketchGeometry {

    SketchSpline(const QVector<QVector2D>& pts)
        : SketchGeometry(SketchGeometryType::Spline)
    {
        points = pts;
    }
};

/**
 * @brief 草圖正多邊形
 */
struct SketchPolygon : public SketchGeometry {
    bool closed;

    SketchPolygon(const QVector<QVector2D>& pts, bool isClosed = true)
        : SketchGeometry(SketchGeometryType::Polyline)
        , closed(isClosed) {
        points = pts;
    }
};

/**
 * @brief 草圖圓
 */
struct SketchCircle : public SketchGeometry {
    const QVector2D center;
    double radius;

    SketchCircle(const QVector2D& c, double r)
        : SketchGeometry(SketchGeometryType::Circle)
        , center(c)
        , radius(r) {
    }
};

/**
 * @brief 草圖橢圓
 */
struct SketchEllipse : public SketchGeometry {
    const QVector2D center;
    double majorRadius;
    double minorRadius;
    double angle;

    SketchEllipse(const QVector2D& c, double r1, double r2, double a)
        : SketchGeometry(SketchGeometryType::Ellipse)
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
    QList<Handle(AIS_Shape)> displayInContext(const Handle(AIS_InteractiveContext)& context);
    void eraseFromContext(const Handle(AIS_InteractiveContext)& context);
    TopoDS_Wire mainWire() const;
    bool hasClosedProfile() const;

    // ==================== 序列化 ====================

    /**
     * @brief 序列化（使用 planeId 參考）
     */
    QJsonObject toJson() const override;

    /**
     * @brief 反序列化（從 planeId 恢復參考）
     */
    bool fromJson(const QJsonObject& json) override;

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
};

} // namespace cad
} // namespace aicad

#endif // AICAD_CAD_SKETCH_ENHANCED_H
