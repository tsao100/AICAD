/**
 * @file CustomPlane.h
 * @brief CustomPlane 樁檔案 (供 Felicia 編譯使用，等待 Ben 完成實際實作)
 * @author Felicia (臨時)
 * @date 2024-12-04
 */

#ifndef AICAD_CAD_CUSTOMPLANE_H
#define AICAD_CAD_CUSTOMPLANE_H

#include <QString>
#include <QVector3D>
#include <gp_Pln.hxx>
#include <gp_Ax2.hxx>
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>

namespace aicad {
namespace cad {

/**
 * @brief 自訂平面結構 (樁實作)
 * 
 * TODO(Ben): 這是臨時樁實作，等待 Ben 完成正式版本
 */
struct CustomPlane {
    QVector3D origin;   ///< 平面原點
    QVector3D normal;   ///< 平面法向量
    QVector3D uAxis;    ///< U 軸方向 (X軸)
    QVector3D vAxis;    ///< V 軸方向 (Y軸)
    
    /**
     * @brief 建立 XY 平面
     */
    static CustomPlane XY() {
        CustomPlane p;
        p.origin = QVector3D(0, 0, 0);
        p.normal = QVector3D(0, 0, 1);
        p.uAxis = QVector3D(1, 0, 0);
        p.vAxis = QVector3D(0, 1, 0);
        return p;
    }
    
    /**
     * @brief 建立 XZ 平面
     */
    static CustomPlane XZ() {
        CustomPlane p;
        p.origin = QVector3D(0, 0, 0);
        p.normal = QVector3D(0, 1, 0);
        p.uAxis = QVector3D(1, 0, 0);
        p.vAxis = QVector3D(0, 0, 1);
        return p;
    }
    
    /**
     * @brief 建立 YZ 平面
     */
    static CustomPlane YZ() {
        CustomPlane p;
        p.origin = QVector3D(0, 0, 0);
        p.normal = QVector3D(1, 0, 0);
        p.uAxis = QVector3D(0, 1, 0);
        p.vAxis = QVector3D(0, 0, 1);
        return p;
    }
    
    /**
     * @brief 取得顯示名稱
     */
    QString getDisplayName() const {
        if (normal == QVector3D(0, 0, 1) && origin == QVector3D(0, 0, 0))
            return "XY";
        if (normal == QVector3D(0, 1, 0) && origin == QVector3D(0, 0, 0))
            return "XZ";
        if (normal == QVector3D(1, 0, 0) && origin == QVector3D(0, 0, 0))
            return "YZ";
        return QString("Custom (%1, %2, %3)")
            .arg(normal.x(), 0, 'f', 2)
            .arg(normal.y(), 0, 'f', 2)
            .arg(normal.z(), 0, 'f', 2);
    }
    
    /**
     * @brief 轉換為 OCCT gp_Pln
     */
    gp_Pln toGpPln() const {
        gp_Pnt originPnt(origin.x(), origin.y(), origin.z());
        gp_Dir normalDir(normal.x(), normal.y(), normal.z());
        return gp_Pln(originPnt, normalDir);
    }
    
    /**
     * @brief 轉換為 OCCT gp_Ax2
     */
    gp_Ax2 toGpAx2() const {
        gp_Pnt originPnt(origin.x(), origin.y(), origin.z());
        gp_Dir normalDir(normal.x(), normal.y(), normal.z());
        gp_Dir uAxisDir(uAxis.x(), uAxis.y(), uAxis.z());
        return gp_Ax2(originPnt, normalDir, uAxisDir);
    }
};

} // namespace cad
} // namespace aicad

#endif // AICAD_CAD_CUSTOMPLANE_H