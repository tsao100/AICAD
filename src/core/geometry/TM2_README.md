# AICAD 雙座標系統（TM2 Global / Local CAD）架構說明

> 對應實作：`src/core/geometry/ProjectOrigin.h/.cpp`
> 日期：2026-06-22

---

## 一、核心原則

```
TM2 / TWD97 座標（Global, Survey）
        ↓  減 Project Origin（ProjectOrigin::toLocal）
Local CAD 座標（Model / OCCT / OpenGL 真正使用）
        ↓
顯示時換算回 Global（ProjectOrigin::toGlobal）
→ 狀態列、座標輸入、Alignment 報表、輸出檔案
```

> **OCCT / OpenGL / V3d_View 永遠只看到 Local 座標（量級 10³～10⁵）。**
> **TM2 大數值只存在於「輸入框」「顯示文字」「檔案 I/O」三個邊界。**

---

## 二、類別職責對照

| 類別 | 檔案 | 職責 |
|---|---|---|
| `ProjectOrigin` | `src/core/geometry/ProjectOrigin.{h,cpp}` | TM2 Global ↔ CAD Local 平移轉換；單例；持久化進 JSON |
| `CoordinateTransform` | `src/core/geometry/CoordinateTransform.{h,cpp}` | Sketch Plane 2D ↔ OCCT 3D World；**與 TM2 無關** |

---

## 三、轉換公式

```
local  = global − origin      （ProjectOrigin::toLocal）
global = local  + origin      （ProjectOrigin::toGlobal）
```

`origin`（= `ProjectOrigin::originE/N/Z`）是「Local 原點在 TM2 系中的位置」。

---

## 四、邊界一覽（何時呼叫 toLocal / toGlobal）

| 邊界 | 方向 | 呼叫位置 |
|---|---|---|
| 鍵盤輸入 TM2 座標 | Global → Local | `InputParser::parseAbsoluteCoord`（量級 > 10000 自動偵測） |
| `COORDINATE_INPUT` 橋接 | Global → Local | `UIManager` COORDINATE_INPUT 訂閱 |
| OCCT `gp_Pnt` 建構 | Global → Local | `AlignmentRenderer::toOCCT()` |
| 狀態列游標座標顯示 | Local → Global | `UIManager` POINT_ACQUIRED 訂閱 |
| 存檔（JSON） | — | `AlignmentDocument::toJson()` 嵌入 `"projectOrigin"` |
| 開檔（JSON） | — | `AlignmentDocument::fromJson()` 還原並做衝突偵測 |

---

## 五、SETORIGIN 指令流程

```
使用者輸入 E,N（TM2 目標座標）
    ↓
點選模型點 modelPt（Local 座標）
    ↓
SetOriginCommand::applyOffset():
    originE = E − modelPt.x
    originN = N − modelPt.y
    ProjectOrigin::instance().setOrigin(originE, originN)
    ↓
ProjectOrigin::publishChanged() 發布 Events::PROJECT_ORIGIN_CHANGED
    ↓
UIManager 訂閱 → CadView::setCoordinateOffset（deprecated 相容層）
AlignmentDocument::fromJson → 自動還原
```

---

## 六、多檔案疊圖衝突

`AlignmentDocument` 在 `fromJson` 時比對：
- 檔案內嵌 origin（`docOriginE/N`）vs 目前 `ProjectOrigin::instance()`
- 差異 > 1 m → 彈出 `QMessageBox::question` 讓使用者選擇

---

## 七、向下相容（舊 .aicad 檔案）

- 舊檔案無 `"projectOrigin"` 鍵 → `isSet()==false` → 恆等轉換（local = global）
- 行為與未實作前完全一致，舊圖紙直接開啟不需任何遷移

---

## 八、未來延伸（Phase 6 選項）

- 整合 PROJ library，支援完整 EPSG ↔ EPSG 投影轉換
  （目前 `ProjectOrigin` 只做平移，適合 TM2 同一分帶內的工程場景）
- 支援 Alignment-based Local Coordinate（里程 STA / 橫距 Offset）
