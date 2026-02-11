/**
 * @file Sketch.h
 * @brief 草圖特徵類別
 * @author AICAD Team
 * @date 2025-01-08
 */

#ifndef AICAD_CAD_SKETCH_H
#define AICAD_CAD_SKETCH_H

#include "Feature.h"
#include "Plane.h"
#include <QVector>
#include <QVector2D>
#include <QJsonObject>
#include <QJsonArray>
#include <TopoDS_Wire.hxx>
#include <AIS_Shape.hxx>

namespace aicad {
namespace cad {

/**
 * @brief 草圖幾何類型
 */
enum class SketchGeometryType {
    Line,        ///< 直線
    Arc,         ///< 圓弧
    Circle,      ///< 圓
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
struct SketchArc : public SketchGeometry {
    QVector2D center;
    double radius;
    double startAngle;
    double endAngle;

    SketchArc(const QVector2D& c, double r, double s, double e)
        : SketchGeometry(SketchGeometryType::Arc), center(c), radius(r)
        , startAngle(s), endAngle(e)
    {
    }
};

/**
 * @brief 草圖多段線
 */
struct SketchPolyline : public SketchGeometry {
    bool closed;

    SketchPolyline(const QVector<QVector2D>& pts, bool isClosed = false)
        : SketchGeometry(SketchGeometryType::Polyline)
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

    SketchCircle(const QVector2D& c, double r)
        : SketchGeometry(SketchGeometryType::Circle)
        , center(c)
        , radius(r) {
    }
};

/**
 * @brief 草圖特徵
 *
 * 包含 2D 幾何元素的平面草圖
 * - 定義在特定平面上
 * - 包含多個幾何元素（線、圓、多段線等）
 * - 可以轉換為 OCCT Wire 用於建立實體
 *
 * 使用範例:
 * @code
 * Sketch* sketch = new Sketch(document);
 * sketch->setPlane(Plane::xy());
 * sketch->setName("Front Profile");
 *
 * // 加入矩形
 * sketch->addLine(QVector2D(0, 0), QVector2D(100, 0));
 * sketch->addLine(QVector2D(100, 0), QVector2D(100, 50));
 * sketch->addLine(QVector2D(100, 50), QVector2D(0, 50));
 * sketch->addLine(QVector2D(0, 50), QVector2D(0, 0));
 *
 * sketch->rebuild();
 * @endcode
 */
class Sketch : public Feature {
    Q_OBJECT

public:
    /**
     * @brief 建構子
     * @param parent 父文件
     */
    explicit Sketch(Document* parent = nullptr);

    /**
     * @brief 解構子
     */
    ~Sketch() override;

    /**
     * @brief 取得特徵類型
     */
    FeatureType type() const override { return FeatureType::Sketch; }

    /**
     * @brief 重建草圖幾何
     */
    bool rebuild() override;

    // 平面管理
    Plane plane() const { return m_plane; }
    void setPlane(const Plane& plane);

    // 幾何元素管理
    void addGeometry(SketchGeometry* geom);
    void removeGeometry(int index);
    void clearGeometry();

    QList<SketchGeometry*> geometries() const { return m_geometries; }
    int geometryCount() const { return m_geometries.size(); }

    // 便捷方法：加入基本幾何
    void addLine(const QVector2D& p1, const QVector2D& p2);
    void addPolyline(const QVector<QVector2D>& points, bool closed = false);
    void addCircle(const QVector2D& center, double radius);
    void addRectangle(const QVector2D& corner1, const QVector2D& corner2);
    void addArc(const QVector2D& startPoint, const QVector2D& midPoint, const QVector2D& endPoint);

    /**
     * @brief 取得所有的 Wire（用於擠出等操作）
     */
    QList<TopoDS_Wire> wires() const;

    // ✅ 新增：取得對應的 AIS 物件
    QList<Handle(AIS_Shape)> aisShapes() const;

    // ✅ 新增：顯示到 AIS Context
    void displayInContext(const Handle(AIS_InteractiveContext)& context);
    void eraseFromContext(const Handle(AIS_InteractiveContext)& context);

    /**
     * @brief 取得主要輪廓 Wire
     * @return 第一個封閉的 Wire，如果沒有則返回第一個 Wire
     */
    TopoDS_Wire mainWire() const;

    /**
     * @brief 檢查草圖是否有封閉輪廓
     */
    bool hasClosedProfile() const;

    /**
     * @brief 序列化
     */
    QJsonObject toJson() const;

    /**
     * @brief 反序列化
     */
    bool fromJson(const QJsonObject& json);

Q_SIGNALS:
    /**
     * @brief 平面改變時發出
     */
    void planeChanged(const Plane& plane);

    /**
     * @brief 幾何元素改變時發出
     */
    void geometryChanged();

private:
    /**
     * @brief 從幾何元素建立 OCCT Wire
     */
    TopoDS_Wire buildWire(const QList<SketchGeometry*>& geoms);

    /**
     * @brief 將 2D 點轉換為 3D
     */
    gp_Pnt toWorld(const QVector2D& point) const;

private:
    Plane m_plane;                        ///< 草圖平面
    QList<SketchGeometry*> m_geometries;  ///< 幾何元素列表
    QList<TopoDS_Wire> m_wires;           ///< 快取的 Wire 列表
    QList<Handle(AIS_Shape)> m_aisShapes;
};

} // namespace cad
} // namespace aicad

#endif // AICAD_CAD_SKETCH_H
