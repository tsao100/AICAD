// src/cad/sketch/SketchRegion.h（修訂版）
#pragma once
#include <QVector>
#include <QString>
#include <QVector2D>
#include <QUuid>

namespace aicad { namespace cad {

struct SketchLoop {
    QVector<QString>   edgeUuids;   ///< 依序排列的幾何 UUID
    QVector<QVector2D> vertices;    ///< 對應頂點座標（與 edgeUuids 同序）
    bool isOuter = true;            ///< CCW=true（外輪廓）；CW=false（洞）
};

struct SketchRegion {
    QString    uuid;
    SketchLoop outerLoop;
    QVector<SketchLoop> holes;

    /**
     * @brief 判斷草圖座標點是否在此 region 內（含洞的扣除）
     */
    bool contains(const QVector2D& pt) const;

    static bool raycast(const QVector2D& pt,
                        const QVector<QVector2D>& poly);
};

}} // namespace aicad::cad
