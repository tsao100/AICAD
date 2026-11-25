/**
 * @file FeatureGroup.h
 * @brief Feature Browser 固定根節點 Part／Assembly／Void 的歸屬列舉（輕量標頭，Document.h 與 Feature.h 共用）。
 *
 *   Part     ：造型 —— 單一零件的本體，由擠出等造型特徵組成
 *   Assembly ：組合 —— 插入的零件檔（*.aipart／*.aiasm）與它們之間的配合
 *   Void     ：開孔 —— 底下的特徵組成開孔形體（沿線形安裝時從道版扣除）
 * 組件（沿線形安裝的實體）= Part ∪ Assembly 的終端結果；Void 另外當作開孔空間。
 */
#pragma once

namespace aicad {
namespace cad {

enum class FeatureGroup { Part = 0, Assembly = 1, Void = 2 };

} // namespace cad
} // namespace aicad
