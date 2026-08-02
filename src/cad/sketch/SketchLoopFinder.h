// SketchLoopFinder.h — planar graph loop / face builder
#pragma once
#include "SketchRegion.h"
#include <cad/Sketch.h>

namespace aicad { namespace cad {

/**
 * @brief Planar graph loop finder
 *
 *  輸入  : Sketch 所有 Normal 幾何
 *  輸出  : 所有偵測到的閉合迴路，組合成 SketchRegion 列表
 *
 *  演算法概要
 *  ----------
 *  1. 將所有幾何端點建成鄰接表（adjacency list），
 *     端點以 QVector2D（容差 snap）作為 key。
 *  2. 以 half-edge 方式遍歷，對每條有向半邊執行
 *     "next clockwise edge" 規則，找出所有最小面（minimal face）。
 *  3. 以帶符號面積（shoelace）判斷 CCW / CW，
 *     再以 containment test 指派 outer / hole 關係。
 */
class SketchLoopFinder {
public:
    explicit SketchLoopFinder(double snapTol = 1e-4);

    /**
     * @brief 對 sketch 執行 loop detection + region construction
     * @return 所有偵測到的 SketchRegion（已區分 outer / hole）
     */
    QVector<SketchRegion> findRegions(const Sketch* sketch) const;

private:
    double m_tol;

    // --- 內部資料結構 ---
    struct Vertex {
        QVector2D pos;
        QVector<int> outEdges;   // indices into m_halfEdges
    };

    struct HalfEdge {
        int from, to;            // vertex indices
        int twin  = -1;
        int next  = -1;         // next in face loop (after linking)
        QString geomUuid;
        bool visited = false;
    };

    int   snapVertex(QVector<Vertex>& verts,
                   const QVector2D& p) const;
    void  linkTwins(QVector<HalfEdge>& he) const;
    void  buildNextPointers(QVector<Vertex>& verts,
                           QVector<HalfEdge>& he) const;
    float signedArea(const QVector<int>& loop,
                     const QVector<Vertex>& verts) const;
    bool  pointInPolygon(const QVector2D& pt,
                        const QVector<int>& loop,
                        const QVector<Vertex>& verts) const;
};

}} // namespace aicad::cad
