/**
 * @file OSnapTypes.h
 * @brief Object Snap 類型定義 - Industrial 級別
 *
 * 設計原則：
 *  - 與 OCCT AIS_InteractiveContext 深度整合
 *  - 與現有 Grip 系統協同運作（Grip 優先，OSnap 次之）
 *  - EventBus 發布 snap 事件，讓 Command 系統接收座標
 *  - 支援 2D Sketch 平面投影與 3D 世界座標雙模式
 *
 * @author AICAD Team
 * @date 2025-01-08
 */

#pragma once

#include <gp_Pnt.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Face.hxx>
#include <Standard_Handle.hxx>
#include <AIS_InteractiveObject.hxx>

#include <QString>
#include <QVector2D>
#include <QVector3D>
#include <QFlags>
#include <QMetaType>

namespace aicad {
namespace osnap {

// ============================================================================
//  Snap 類型位元遮罩（可組合）
// ============================================================================
enum class SnapType : quint32 {
    None            = 0x00000000,

    // ── 頂點類 ────────────────────────────────────────────────────────────────
    Endpoint        = 0x00000001,  ///< 線段端點、邊端點
    Midpoint        = 0x00000002,  ///< 線段中點
    Center          = 0x00000004,  ///< 圓/橢圓/弧中心
    Quadrant        = 0x00000008,  ///< 圓四分點（0°/90°/180°/270°）

    // ── 幾何關係類 ─────────────────────────────────────────────────────────────
    Intersection    = 0x00000010,  ///< 兩條邊的交點
    Perpendicular   = 0x00000020,  ///< 對線段的垂足
    Tangent         = 0x00000040,  ///< 切線點
    Nearest         = 0x00000080,  ///< 邊上最近點

    // ── 延伸類 ────────────────────────────────────────────────────────────────
    Extension       = 0x00000100,  ///< 直線延伸線上的點
    Parallel        = 0x00000200,  ///< 與某邊平行方向上的點

    // ── 節點類 ────────────────────────────────────────────────────────────────
    Node            = 0x00000400,  ///< 草圖幾何節點/頂點
    Insert          = 0x00000800,  ///< 插入點（圖塊/文字）

    // ── 網格 ──────────────────────────────────────────────────────────────────
    Grid            = 0x00001000,  ///< 網格點吸附

    // ── 鐵路平面線形 (Alignment) ──────────────────────────────────────────────
    AlignmentPI     = 0x00002000,  ///< 線形交點 (PI, Point of Intersection)
    AlignmentTC     = 0x00004000,  ///< 切點／緩和曲線起終點 (TC/CS/ST/TS)
    AlignmentMid    = 0x00008000,  ///< 各元素中點（圓弧或切線段中央）
    AlignmentPerp   = 0x00010000,  ///< 游標至線形最近垂足

    // ── 預設組合 ──────────────────────────────────────────────────────────────
    Standard = Endpoint | Midpoint | Center | Quadrant,
    All      = 0x0001FFFF
};
Q_DECLARE_FLAGS(SnapTypes, SnapType)
Q_DECLARE_OPERATORS_FOR_FLAGS(SnapTypes)

// ============================================================================
//  Snap 優先順序（數字越小越優先）
// ============================================================================
inline int snapPriority(SnapType t) {
    switch (t) {
    case SnapType::Endpoint:      return 10;
    case SnapType::Intersection:  return 15;
    case SnapType::Center:        return 20;
    case SnapType::Midpoint:      return 25;
    case SnapType::Quadrant:      return 30;
    case SnapType::Node:          return 35;
    case SnapType::Perpendicular: return 40;
    case SnapType::Tangent:       return 45;
    case SnapType::Nearest:       return 50;
    case SnapType::Extension:     return 55;
    case SnapType::Parallel:      return 60;
    case SnapType::Grid:          return 70;
    case SnapType::AlignmentPI:   return 12;  ///< 線形 PI 點，與 Endpoint 同級略高
    case SnapType::AlignmentTC:   return 14;  ///< 切點（TC/CS），緊接 PI 之後
    case SnapType::AlignmentMid:  return 27;  ///< 線形中點，與 Midpoint 同級
    case SnapType::AlignmentPerp: return 42;  ///< 垂足，與 Perpendicular 同級
    default:                      return 99;
    }
}

// ============================================================================
//  Snap 候選點
// ============================================================================
struct SnapCandidate {
    SnapType          type         = SnapType::None;
    gp_Pnt            worldPoint;          ///< 3D 世界座標吸附點
    QVector2D         planePoint;          ///< 2D 草圖平面座標（若適用）
    double            screenDist   = 1e9;  ///< 到滑鼠的螢幕距離（像素）
    double            worldDist    = 1e9;  ///< 到滑鼠射線的 3D 距離
    TopoDS_Shape      sourceShape;         ///< 來源幾何體
    TopoDS_Edge       sourceEdge;          ///< 來源邊（若適用）
    TopoDS_Vertex     sourceVertex;        ///< 來源頂點（若適用）
    TopoDS_Face       sourceFace;          ///< 來源面（若適用）
    Handle(AIS_InteractiveObject) sourceAIS; ///< 來源 AIS 物件
    double            paramOnEdge = 0.0;   ///< 邊上的參數（0~1）
    bool              isValid     = false;

    /// 比較：用於排序（優先順序 + 螢幕距離）
    bool operator<(const SnapCandidate& o) const {
        int p1 = snapPriority(type);
        int p2 = snapPriority(o.type);
        if (p1 != p2) return p1 < p2;
        return screenDist < o.screenDist;
    }
};

// ============================================================================
//  Snap 設定
// ============================================================================
struct OSnapSettings {
    SnapTypes  enabledTypes     = SnapType::Standard;
    double     pickPixelRadius  = 12.0;   ///< 螢幕吸附半徑（像素）
    double     magnetRadius     = 8.0;    ///< 磁吸半徑（像素），進入此範圍才鎖定
    bool       showTooltip      = true;   ///< 顯示吸附類型提示
    bool       showTrackingLine = true;   ///< 顯示追蹤輔助線
    bool       snapToSketch     = true;   ///< 對草圖幾何吸附
    bool       snapToEdges      = true;   ///< 對實體邊吸附
    bool       snapToFaces      = false;  ///< 對面上最近點吸附（效能較重）
    bool       gridSnapEnabled  = false;  ///< 網格吸附
    double     gridSpacing      = 10.0;   ///< 網格間距（模型單位）
    int        maxCandidates    = 32;     ///< 每次偵測最多候選數
};

// ============================================================================
//  顏色對應
// ============================================================================
inline Quantity_NameOfColor snapColor(SnapType t) {
    switch (t) {
    case SnapType::Endpoint:      return Quantity_NOC_YELLOW;
    case SnapType::Midpoint:      return Quantity_NOC_CYAN1;
    case SnapType::Center:        return Quantity_NOC_GREEN;
    case SnapType::Quadrant:      return Quantity_NOC_MAGENTA1;
    case SnapType::Intersection:  return Quantity_NOC_RED;
    case SnapType::Perpendicular: return Quantity_NOC_ORANGE;
    case SnapType::Tangent:       return Quantity_NOC_LIGHTBLUE;
    case SnapType::Nearest:       return Quantity_NOC_WHITE;
    case SnapType::Extension:     return Quantity_NOC_GRAY60;
    case SnapType::Grid:          return Quantity_NOC_GRAY80;
    case SnapType::AlignmentPI:   return Quantity_NOC_ORANGE;
    case SnapType::AlignmentTC:   return Quantity_NOC_GREENYELLOW;
    case SnapType::AlignmentMid:  return Quantity_NOC_CYAN2;
    case SnapType::AlignmentPerp: return Quantity_NOC_LIGHTPINK;
    default:                      return Quantity_NOC_WHITE;
    }
}

// ============================================================================
//  顯示名稱
// ============================================================================
inline QString snapTypeName(SnapType t) {
    switch (t) {
    case SnapType::Endpoint:      return "Endpoint";
    case SnapType::Midpoint:      return "Midpoint";
    case SnapType::Center:        return "Center";
    case SnapType::Quadrant:      return "Quadrant";
    case SnapType::Intersection:  return "Intersection";
    case SnapType::Perpendicular: return "Perpendicular";
    case SnapType::Tangent:       return "Tangent";
    case SnapType::Nearest:       return "Nearest";
    case SnapType::Extension:     return "Extension";
    case SnapType::Parallel:      return "Parallel";
    case SnapType::Node:          return "Node";
    case SnapType::Grid:          return "Grid";
    case SnapType::AlignmentPI:   return "AlignPI";
    case SnapType::AlignmentTC:   return "AlignTC";
    case SnapType::AlignmentMid:  return "AlignMid";
    case SnapType::AlignmentPerp: return "AlignPerp";
    default:                      return "";
    }
}

} // namespace osnap
} // namespace aicad

Q_DECLARE_METATYPE(aicad::osnap::SnapCandidate)
Q_DECLARE_METATYPE(aicad::osnap::OSnapSettings)