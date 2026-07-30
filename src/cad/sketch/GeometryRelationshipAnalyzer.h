#pragma once
#include "SketchConstraint.h"

namespace aicad::cad {

/**
 * @brief 幾何關係分析器 —— 對應 Auto_DIM.md 第九節架構圖中的
 *        「Relationship Analyzer」層，以及第六節「依 Constraint 推論」。
 *
 * 這一層夾在 Geometry Classifier（GeneralDimClassifier::classifySingle）
 * 與 Dimension Candidate Generator（GeneralDimClassifier::classifyPair）
 * 之間：先判斷兩個幾何之間有沒有特殊關係（平行/垂直/同心/相切/重合），
 * 再讓 classifyPair 依關係調整候選清單（例如同心圓只提供半徑/直徑，
 * 不提供中心距；重合點不提供距離）。
 *
 * 判斷優先序：
 *   1. 先查 Sketch 既有的 SketchConstraint（求解器已經確認過的關係，
 *      最權威，見 Auto_DIM.md 第六節）
 *   2. 查不到才退回幾何數值計算（見 .cpp 的容許誤差說明）
 */
enum class GeomRelationship {
    Independent,    ///< 無特殊關係，一般情況
    Parallel,       ///< 兩線平行
    Perpendicular,  ///< 兩線垂直
    Intersecting,   ///< 兩線相交但非垂直
    Concentric,     ///< 兩圓/弧圓心重合
    Tangent,        ///< 相切（線與圓/弧，或圓與圓）
    Coincident,     ///< 兩點（或兩幾何的參考點）完全重合
};

class GeometryRelationshipAnalyzer {
public:
    static GeomRelationship analyze(const GeomRef& a, const GeomRef& b, const Sketch* sketch);
};

} // namespace aicad::cad
