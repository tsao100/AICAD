# CAD Engine 模組

## 概述

Ben 已完成 CAD Engine 核心系統的實作,包括文件管理、特徵系統和基礎幾何操作。

## 已完成的類別

### 1. Document 類別 (`cad/Document.h/cpp`)

**功能:**
- 管理 CAD 文件的完整生命週期
- 整合 OCCT 文件系統
- 特徵的建立、刪除和查詢
- 文件儲存/載入

**使用範例:**
```cpp
#include "cad/Document.h"

cad::Document* doc = new cad::Document();
cad::Sketch* sketch = doc->createSketch("XY");
cad::Feature* extrude = doc->createExtrude(sketch, 10.0);
doc->save("mydesign.cad");
```

### 2. Feature 類別 (`cad/Feature.h/cpp`)

**功能:**
- 所有特徵的抽象基礎類別
- 提供通用特徵介面
- 處理特徵屬性和狀態
- 整合 EventBus 發布特徵事件

**繼承結構:**
```
Feature (抽象)
├── Sketch
├── Extrude
├── Revolve (未來擴充)
├── Fillet (未來擴充)
└── Chamfer (未來擴充)
```

### 3. Sketch 類別 (`cad/Sketch.h/cpp`)

**功能:**
- 管理 2D 草圖元素
- 支援線、矩形、圓、圓弧、多段線
- 定義草圖平面 (XY, XZ, YZ)
- 2D/3D 座標轉換

**使用範例:**
```cpp
cad::Sketch* sketch = doc->createSketch("XY");
sketch->addLine(QVector2D(0, 0), QVector2D(10, 0));
sketch->addRectangle(QVector2D(0, 0), QVector2D(20, 15));
sketch->addCircle(QVector2D(10, 10), 5.0);
sketch->rebuild();
```

### 4. Extrude 類別 (`cad/Extrude.h/cpp`)

**功能:**
- 從草圖建立 3D 實體
- 支援正向/反向/對稱擠出
- 計算體積和表面積
- 自動重建機制

**使用範例:**
```cpp
cad::Extrude* extrude = 
    dynamic_cast<cad::Extrude*>(doc->createExtrude(sketch, 5.0));
    
extrude->setDirection(ExtrudeDirection::Symmetric);
extrude->setDistance(10.0);

qDebug() << "Volume:" << extrude->volume();
qDebug() << "Surface Area:" << extrude->surfaceArea();
```

## 架構設計

### PIMPL 模式
所有類別都使用 PIMPL (Pointer to Implementation) 模式:
- 減少編譯依賴
- 隱藏實作細節
- 保持 ABI 穩定性

### EventBus 整合
特徵變更自動透過 EventBus 發布:
```cpp
// 在特徵建立時
bus->publish(Events::FEATURE_CREATED, QVariant::fromValue(feature));

// 在特徵更新時
bus->publish(Events::FEATURE_UPDATED, QVariant::fromValue(feature));

// 在特徵刪除時
bus->publish(Events::FEATURE_DELETED, featureId);
```

### OCCT 整合
- 使用 `TDF_Label` 管理特徵層級
- 使用 `TNaming_NamedShape` 儲存幾何
- 使用 `TDataStd_*` 屬性儲存參數

## 與現有系統整合

### 1. 修改 DocumentManager

在 `DocumentManager` 中新增對新 CAD Document 的支援:

```cpp
// DocumentManager.h
#include "cad/Document.h"

class DocumentManager : public QObject {
    Q_OBJECT
public:
    cad::Document* createCadDocument(const QString& name = QString());
    cad::Document* currentCadDocument() const;
    
private:
    QHash<QString, cad::Document*> m_cadDocuments;
};
```

### 2. 修改 MainWindow

更新命令處理函式以使用新的 CAD 系統:

```cpp
void MainWindow::onCreateSketch() {
    cad::Document* doc = getCurrentCadDocument();
    cad::Sketch* sketch = doc->createSketch("XY");
    m_currentSketch = sketch;
}

void MainWindow::onDrawRectangle() {
    if (m_currentSketch) {
        m_currentSketch->addRectangle(corner1, corner2);
        m_currentSketch->rebuild();
    }
}

void MainWindow::onCreateExtrude() {
    cad::Feature* extrude = doc->createExtrude(m_currentSketch, height);
    m_view->displayFeature(extrude);
}
```

### 3. 修改 CadView

新增顯示新特徵的方法:

```cpp
void CadView::displayFeature(cad::Feature* feature) {
    TopoDS_Shape shape = feature->shape();
    Handle(AIS_Shape) aisShape = new AIS_Shape(shape);
    
    if (feature->type() == cad::FeatureType::Sketch) {
        aisShape->SetColor(Quantity_NOC_WHITE);
    } else {
        aisShape->SetColor(Quantity_NOC_LIGHTSTEELBLUE);
    }
    
    m_context->Display(aisShape, Standard_True);
}
```

## 專案檔案更新

在 `AICAD.pro` 中新增:

```qmake
HEADERS += \
    src/cad/Document.h \
    src/cad/Feature.h \
    src/cad/Sketch.h \
    src/cad/Extrude.h

SOURCES += \
    src/cad/Document.cpp \
    src/cad/Feature.cpp \
    src/cad/Sketch.cpp \
    src/cad/Extrude.cpp

INCLUDEPATH += $$PWD/src/cad
```

## 檔案結構

```
src/
├── cad/
│   ├── Document.h       # CAD 文件管理
│   ├── Document.cpp
│   ├── Feature.h        # 特徵基礎類別
│   ├── Feature.cpp
│   ├── Sketch.h         # 草圖特徵
│   ├── Sketch.cpp
│   ├── Extrude.h        # 擠出特徵
│   └── Extrude.cpp
├── core/
│   ├── Application.h    # Jack 已完成
│   ├── EventBus.h       # Jack 已完成
│   └── DocumentManager.h
└── ...
```

## 測試方式

### 基本測試流程

1. **建立文件和草圖:**
```cpp
cad::Document* doc = new cad::Document();
cad::Sketch* sketch = doc->createSketch("XY");
```

2. **繪製幾何:**
```cpp
sketch->addRectangle(QVector2D(0, 0), QVector2D(10, 10));
sketch->rebuild();
```

3. **建立擠出:**
```cpp
cad::Feature* extrude = doc->createExtrude(sketch, 5.0);
```

4. **驗證結果:**
```cpp
qDebug() << "Feature count:" << doc->featureCount();
qDebug() << "Sketch valid:" << sketch->isValid();
qDebug() << "Extrude valid:" << extrude->isValid();
```

### 事件測試

```cpp
// 訂閱事件
EventBus* bus = Application::instance()->eventBus();
bus->subscribe(Events::FEATURE_CREATED, this, [](const QVariant& data) {
    qDebug() << "Feature created event received";
});

// 建立特徵 (應該觸發事件)
cad::Sketch* sketch = doc->createSketch("XY");
```

## 未來擴充

### 短期 (1-2 週)
- [ ] 實作 Revolve 特徵
- [ ] 實作 Fillet/Chamfer 特徵
- [ ] 完善草圖約束系統
- [ ] 改善錯誤處理

### 中期 (1-2 月)
- [ ] 布林運算 (Union, Subtract, Intersect)
- [ ] Pattern 特徵 (Linear, Circular)
- [ ] 參數化設計系統
- [ ] Undo/Redo 機制

### 長期 (3-6 月)
- [ ] Assembly 組裝系統
- [ ] 曲面建模
- [ ] 進階約束求解器
- [ ] 效能優化

## 已知問題

1. **序列化未完成:** `Document::load()` 需要實作特徵的反序列化
2. **圓弧支援:** `Sketch::rebuild()` 中圓和圓弧尚未實作
3. **對稱擠出:** `ExtrudeDirection::Symmetric` 尚未實作
4. **錯誤恢復:** 需要更好的錯誤處理和恢復機制

## 給其他成員的建議

### James (UI System)
- 可以使用 `Document::featureCreated` 信號更新特徵樹
- 使用 `Feature::nameChanged` 即時更新 UI
- 監聽 `Document::modifiedChanged` 顯示未儲存提示

### Felicia (View System)
- `Feature::shape()` 回傳的 `TopoDS_Shape` 可直接顯示
- 使用 `Feature::isVisible()` 控制顯示/隱藏
- `Sketch::plane()` 提供座標轉換功能

### Kaufen (Commands)
- 透過 `Document` 建立特徵
- 使用 EventBus 通知命令執行狀態
- 參考 `Extrude::setDistance()` 的參數更新模式

### Daney (Scripting)
- 所有 API 都可以透過 Lisp 綁定
- 使用 `QVariant::fromValue` 傳遞物件
- 特徵的 property 系統支援動態屬性

## 聯絡方式

有任何問題請找 Ben 討論!

---

**版本:** 1.0.0  
**日期:** 2025-01-06  
**作者:** Ben