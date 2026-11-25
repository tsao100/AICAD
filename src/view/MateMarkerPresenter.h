/**
 * @file MateMarkerPresenter.h
 * @brief 在 3D 視圖畫出「配合」的標示：每個配合的兩個參照各畫一個記號，疊在實體前面（Topmost Z-layer），不可選取。
 *
 *   點    → 三軸十字
 *   軸線  → 沿軸向的線段（兩端各延伸）＋箭頭（指向軸的 +方向）
 *   平面  → 方框＋法向量箭頭（法向量朝實體外側）
 * 顏色：參照 1 藍、參照 2 橘；不滿足的配合（求解失敗）紅；被抑制的灰；在樹狀圖中選到的配合以黃色、較粗的線強調。
 */
#pragma once

#include "cad/AssemblyUtil.h"

#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <QString>
#include <QVector>
#include <vector>

namespace aicad {
namespace view {

class MateMarkerPresenter {
public:
    /// 重畫全部標示（先清掉舊的）。visible = false → 只清除
    void update(const Handle(AIS_InteractiveContext)& context,
                const QVector<cad::assemblyutil::MateMarker>& markers, double size,
                bool visible, const QString& highlightMateId);
    void clear(const Handle(AIS_InteractiveContext)& context);

private:
    std::vector<Handle(AIS_Shape)> m_objects;
};

} // namespace view
} // namespace aicad
