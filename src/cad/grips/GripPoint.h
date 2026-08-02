// src/cad/grips/GripPoint.h
#pragma once

#include <gp_Pnt.hxx>
#include <QString>
#include <QVariant>
#include <functional>

namespace aicad::cad {

/// Grip 的視覺狀態
enum class GripState {
    Normal,    ///< 藍色方塊（預設）
    Hover,     ///< 綠色方塊（滑鼠懸停）
    Active,    ///< 紅色方塊（拖拉中）
    Disabled   ///< 灰色（不可互動）
};

/// Grip 的功能類型
enum class GripType {
    Vertex,      ///< 頂點拖拉 → Stretch
    Midpoint,    ///< 中點拖拉 → Move edge
    Center,      ///< 中心點拖拉 → Move whole shape
    Endpoint,    ///< 端點
    Quadrant,    ///< 圓弧四分點
    Rotation,    ///< 旋轉控制點
    Scale,       ///< 縮放控制點
    Mirror,      ///< 鏡像基準點
    DimLine      ///< ✅ Task F: 尺寸線拖曳菱形 Grip
};

/// 單一 Grip 控制點
struct GripPoint {
    QString     id;           ///< 唯一識別（"v0", "mid01", "center"...）
    gp_Pnt      position;     ///< 世界座標
    GripType    type    = GripType::Vertex;
    GripState   state   = GripState::Normal;
    bool        enabled = true;

    /// 拖拉回呼：(newWorldPos, isSnapped) → 呼叫端更新幾何
    std::function<void(const gp_Pnt&, bool)> onDrag;

    /// 拖拉結束回呼：用於 Undo 記錄
    std::function<void(const gp_Pnt& before, const gp_Pnt& after)> onCommit;
};

} // namespace aicad::cad
