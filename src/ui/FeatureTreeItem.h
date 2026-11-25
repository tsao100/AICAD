/**
 * @file FeatureTreeItem.h
 * @brief Feature Tree 的項目資料結構
 * @author James (UI Module)
 */

#ifndef AICAD_UI_FEATURETREEITEM_H
#define AICAD_UI_FEATURETREEITEM_H

#include <QString>
#include <QVariant>
#include <QIcon>

namespace aicad {
namespace ui {

/**
 * @brief Feature Tree 項目類型
 */
enum class ItemType {
    Base,
    Folder,           ///< 資料夾
    Origin,           ///< 原點
    Plane,            ///< 平面
    Axis,             ///< 軸
    Point,            ///< 點
    Sketch,           ///< 草圖
    Extrude,          ///< 擠出
    Feature,          ///< 一般特徵
    Railway,          ///< Railway 資料夾節點
    TrackCenterLine,  ///< 單條線路實例
    VAlignment,       ///< 縱斷面（TrackCenterLine 的子節點）
    Pattern,          ///< 沿線斷面陣列（AlignedProfileArray）
    Loft,             ///< 斷面放樣實體（ProfileLoftSolid）
    Chamfer,          ///< 倒角特徵（ChamferSolid）
    Drawings,         ///< 「Drawing Sheets」資料夾
    DrawingBook,      ///< 圖冊（Book）
    DrawingSheetItem, ///< 圖冊內的一張圖（封面／圖目錄／平縱面圖）
    DepotViewFolder,  ///< 圖冊內的「機廠矩形視圖」資料夾
    DepotViewItem,    ///< 一個機廠矩形視圖
    SlabPlan,         ///< 道版 XY 平面圖（TrackSlabPlan）
    Component,        ///< 3D 組件安裝（ComponentInstall）
    ComponentLayout,  ///< 「組件安裝位置」虛擬資料夾（底下是各組件）
    DefinedPlane,     ///< 定義平面（可選用為草圖平面）
    PartFolder,       ///< 固定根節點「Part」：底下的造型特徵組成零件本體
    VoidFolder,       ///< 固定根節點「Void」：底下的特徵組成開孔形體
    AssemblyFolder,   ///< 固定根節點「Assembly」：底下是造型特徵、插入的零件與「配合」資料夾
    AssemblyPart,     ///< 組合中的零件（AssemblyPart）
    AssemblyMatesFolder, ///< 「配合」虛擬資料夾
    AssemblyMate,      ///< 配合（AssemblyMate）
    Revolve,          ///< 旋轉
    Sweep,            ///< 掃掠
    SketchLoft,       ///< 草圖斷面放樣
    Fillet,           ///< 3D 圓角
    Shell,            ///< 抽殼
    Boolean,          ///< 布林運算
    SolidPattern,     ///< 3D 陣列（線性／環狀）
    Mirror,           ///< 3D 鏡射
    Hole              ///< 孔精靈
};

/**
 * @brief Feature Tree 項目資料
 */
struct FeatureTreeItem {
    ItemType type;
    QString id;              ///< 唯一識別碼
    QString name;            ///< 顯示名稱
    QString parentId;        ///< 父項目 ID
    bool visible;            ///< 是否可見
    bool selectable;         ///< 是否可選取
    QIcon icon;              ///< 圖示
    QVariant data;           ///< 額外資料
    
    FeatureTreeItem()
        : type(ItemType::Feature)
        , visible(true)
        , selectable(true)
    {}
    
    /**
     * @brief 取得類型字串
     */
    QString typeString() const {
        switch (type) {
            case ItemType::Folder:          return "Folder";
            case ItemType::Origin:          return "Origin";
            case ItemType::Plane:           return "Plane";
            case ItemType::Axis:            return "Axis";
            case ItemType::Point:           return "Point";
            case ItemType::Sketch:          return "Sketch";
            case ItemType::Extrude:         return "Extrude";
            case ItemType::Railway:         return "Railway";
            case ItemType::TrackCenterLine: return "TrackCenterLine";
            case ItemType::VAlignment:      return "VAlignment";
            case ItemType::Pattern:         return "Pattern";
            case ItemType::Loft:            return "Loft";
            case ItemType::Chamfer:         return "Chamfer";
            case ItemType::Drawings:        return "Drawings";
            case ItemType::DrawingBook:     return "DrawingBook";
            case ItemType::DrawingSheetItem:return "DrawingSheet";
            case ItemType::DepotViewFolder: return "DepotViewFolder";
            case ItemType::DepotViewItem:   return "DepotView";
            case ItemType::AssemblyPart:    return "AssemblyPart";
            case ItemType::AssemblyMate:    return "AssemblyMate";
            default:                        return "Feature";
        }
    }
};

} // namespace ui
} // namespace aicad

#endif
