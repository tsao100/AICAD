/**
 * @file GeometryBuilder.h
 * @brief 幾何建構輔助類別
 * @author AICAD Team
 * @date 2025-01-08
 */

#ifndef AICAD_GEOMETRY_GEOMETRYBUILDER_H
#define AICAD_GEOMETRY_GEOMETRYBUILDER_H

#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS.hxx>
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>
#include <gp_Vec.hxx>
#include <gp_Pln.hxx>

#include <QVector>
#include <QVector2D>
#include <QVector3D>
#include <QString>

namespace aicad {

// 前向宣告
namespace cad {
    class Plane;
    class Sketch;
}

namespace geometry {

/**
 * @brief 幾何建構結果
 */
struct BuildResult {
    bool success;
    TopoDS_Shape shape;
    QString errorMessage;
    
    BuildResult() : success(false) {}
    BuildResult(const TopoDS_Shape& s) : success(true), shape(s) {}
    
    static BuildResult error(const QString& msg) {
        BuildResult result;
        result.success = false;
        result.errorMessage = msg;
        return result;
    }
    
    operator bool() const { return success; }
};

/**
 * @brief 幾何建構輔助類別
 * 
 * 提供高階 OCCT 幾何建構功能：
 * - 從 2D 點建立 Wire
 * - 從 Wire 建立 Face
 * - 擠出操作
 * - 旋轉操作
 * - 布林運算
 * 
 * 使用範例:
 * @code
 * // 建立矩形 Wire
 * QVector<QVector2D> points;
 * points << QVector2D(0, 0) << QVector2D(100, 0)
 *        << QVector2D(100, 50) << QVector2D(0, 50)
 *        << QVector2D(0, 0);
 * 
 * auto wireResult = GeometryBuilder::makeWire(points, Plane::xy());
 * if (wireResult) {
 *     // 擠出成實體
 *     auto solidResult = GeometryBuilder::extrude(
 *         wireResult.shape, QVector3D(0, 0, 10));
 * }
 * @endcode
 */
class GeometryBuilder {
public:
    // === Wire 建構 ===
    
    /**
     * @brief 從 2D 點建立 Wire
     * @param points 2D 點列表（平面座標）
     * @param plane 草圖平面
     * @param closed 是否封閉
     * @return 建構結果
     */
    static BuildResult makeWire(const QVector<QVector2D>& points,
                               const cad::Plane& plane,
                               bool closed = false);
    
    /**
     * @brief 從 3D 點建立 Wire
     * @param points 3D 點列表
     * @param closed 是否封閉
     * @return 建構結果
     */
    static BuildResult makeWire(const QVector<QVector3D>& points,
                               bool closed = false);
    
    /**
     * @brief 從邊列表建立 Wire
     */
    static BuildResult makeWire(const QVector<TopoDS_Edge>& edges);
    
    // === Edge 建構 ===
    
    /**
     * @brief 建立直線段
     */
    static BuildResult makeLine(const gp_Pnt& p1, const gp_Pnt& p2);
    
    /**
     * @brief 建立圓弧
     * @param center 圓心
     * @param radius 半徑
     * @param normal 法向量
     * @param startAngle 起始角度（弧度）
     * @param endAngle 結束角度（弧度）
     */
    static BuildResult makeArc(const gp_Pnt& center,
                              double radius,
                              const gp_Dir& normal,
                              double startAngle,
                              double endAngle);
    
    /**
     * @brief 建立完整圓
     */
    static BuildResult makeCircle(const gp_Pnt& center,
                                 double radius,
                                 const gp_Dir& normal);
    
    // === Face 建構 ===
    
    /**
     * @brief 從 Wire 建立平面 Face
     * @param wire 邊界 Wire
     * @param plane 所在平面
     * @return 建構結果
     */
    static BuildResult makeFace(const TopoDS_Wire& wire,
                               const gp_Pln& plane);
    
    /**
     * @brief 從 Wire 建立平面 Face（自動偵測平面）
     */
    static BuildResult makeFace(const TopoDS_Wire& wire);
    
    /**
     * @brief 從多個 Wire 建立 Face（帶孔洞）
     * @param outerWire 外輪廓
     * @param innerWires 內部孔洞
     */
    static BuildResult makeFace(const TopoDS_Wire& outerWire,
                               const QVector<TopoDS_Wire>& innerWires);
    
    // === Solid 建構 ===
    
    /**
     * @brief 擠出 Face 成 Solid
     * @param face 要擠出的面
     * @param direction 擠出方向向量
     * @return 建構結果
     */
    static BuildResult extrude(const TopoDS_Face& face,
                              const gp_Vec& direction);
    
    /**
     * @brief 擠出 Wire 成 Solid
     * @param wire 要擠出的 Wire
     * @param direction 擠出方向向量
     * @param makeSolid 是否建立封閉實體
     */
    static BuildResult extrude(const TopoDS_Wire& wire,
                              const gp_Vec& direction,
                              bool makeSolid = true);
    
    /**
     * @brief 從 Sketch 擠出成 Solid
     * @param sketch 草圖
     * @param height 擠出高度
     */
    static BuildResult extrudeSketch(cad::Sketch* sketch, double height);
    
    /**
     * @brief 旋轉成 Solid
     * @param profile 輪廓（Wire 或 Face）
     * @param axis 旋轉軸（點和方向）
     * @param angle 旋轉角度（弧度）
     */
    static BuildResult revolve(const TopoDS_Shape& profile,
                              const gp_Pnt& axisPoint,
                              const gp_Dir& axisDir,
                              double angle);
    
    // === 布林運算 ===
    
    /**
     * @brief 聯集（Union）
     */
    static BuildResult unite(const TopoDS_Shape& shape1,
                            const TopoDS_Shape& shape2);
    
    /**
     * @brief 差集（Cut）
     */
    static BuildResult cut(const TopoDS_Shape& base,
                          const TopoDS_Shape& tool);
    
    /**
     * @brief 交集（Common）
     */
    static BuildResult common(const TopoDS_Shape& shape1,
                             const TopoDS_Shape& shape2);
    
    // === 修改操作 ===
    
    /**
     * @brief 圓角（Fillet）
     * @param shape 要倒圓角的形狀
     * @param edges 要倒圓角的邊
     * @param radius 圓角半徑
     */
    static BuildResult fillet(const TopoDS_Shape& shape,
                             const QVector<TopoDS_Edge>& edges,
                             double radius);
    
    /**
     * @brief 倒角（Chamfer）
     * @param shape 要倒角的形狀
     * @param edges 要倒角的邊
     * @param distance 倒角距離
     */
    static BuildResult chamfer(const TopoDS_Shape& shape,
                              const QVector<TopoDS_Edge>& edges,
                              double distance);
    
    // === 工具函式 ===
    
    /**
     * @brief 檢查 Wire 是否封閉
     */
    static bool isClosed(const TopoDS_Wire& wire);
    
    /**
     * @brief 檢查 Wire 是否平面
     */
    static bool isPlanar(const TopoDS_Wire& wire, gp_Pln* plane = nullptr);
    
    /**
     * @brief 計算 Wire 的包圍盒
     */
    static void getBoundingBox(const TopoDS_Shape& shape,
                              double& xmin, double& ymin, double& zmin,
                              double& xmax, double& ymax, double& zmax);
    
    /**
     * @brief QVector3D 轉 gp_Pnt
     */
    static gp_Pnt toGpPnt(const QVector3D& v);
    
    /**
     * @brief QVector3D 轉 gp_Vec
     */
    static gp_Vec toGpVec(const QVector3D& v);
    
    /**
     * @brief QVector3D 轉 gp_Dir
     */
    static gp_Dir toGpDir(const QVector3D& v);
    
    /**
     * @brief gp_Pnt 轉 QVector3D
     */
    static QVector3D fromGpPnt(const gp_Pnt& p);

private:
    GeometryBuilder() = delete;  // 靜態類別，不可實例化
};

} // namespace geometry
} // namespace aicad

#endif // AICAD_GEOMETRY_GEOMETRYBUILDER_H
