# AICAD UI 系統整合指南

## 概述

UI 系統（James 負責）已完成實作，包含以下元件：

### 核心元件

1. **UIManager** - UI 系統管理器
   - 統一管理所有 UI 元件
   - 整合事件總線
   - 提供全域 UI 介面

2. **MainWindow** - 簡化的主視窗
   - 基本視窗框架
   - 選單列和狀態列
   - 視窗事件處理

3. **FeatureBrowser** - 特徵瀏覽器
   - 樹狀顯示特徵
   - 支援選擇和高亮
   - 右鍵選單支援

4. **PropertyPanel** - 屬性面板
   - 顯示物件屬性
   - 支援屬性編輯
   - 屬性變更通知

5. **ToolManager** - 工具管理器
   - 工具列管理
   - 動作註冊和管理
   - 配置檔載入

## 檔案結構

```
src/ui/
├── UIManager.h           # UI 管理器介面
├── UIManager.cpp         # UI 管理器實作
├── MainWindow.h          # 主視窗介面
├── MainWindow.cpp        # 主視窗實作
├── FeatureBrowser.h      # 特徵瀏覽器介面
├── FeatureBrowser.cpp    # 特徵瀏覽器實作
├── PropertyPanel.h       # 屬性面板介面
├── PropertyPanel.cpp     # 屬性面板實作
├── ToolManager.h         # 工具管理器介面
└── ToolManager.cpp       # 工具管理器實作
```

## 整合到 Application

### 1. 修改 Application.h

```cpp
#include "ui/UIManager.h"

namespace aicad {
namespace core {

class Application : public QObject {
    // ... 現有程式碼 ...
    
public:
    /**
     * @brief 取得 UI 管理器
     * @return UIManager 指標，未初始化則為 nullptr
     */
    ui::UIManager* uiManager() const;
    
private:
    class Private;
    Private* d;
};

} // namespace core
} // namespace aicad
```

### 2. 修改 Application.cpp

```cpp
#include "ui/UIManager.h"

namespace aicad {
namespace core {

class Application::Private {
public:
    // ... 現有成員 ...
    ui::UIManager* uiManager;
};

bool Application::initialize() {
    // ... 現有初始化程式碼 ...
    
    // 5. 建立 UI 管理器
    qDebug() << "[Application] Creating UIManager...";
    d->uiManager = new ui::UIManager(this);
    if (!d->uiManager) {
        Q_EMIT errorOccurred("Failed to create UIManager");
        return false;
    }
    
    // 6. 初始化 UI 系統
    qDebug() << "[Application] Initializing UI system...";
    if (!d->uiManager->initialize()) {
        Q_EMIT errorOccurred("Failed to initialize UI system");
        return false;
    }
    
    // ... 其餘初始化程式碼 ...
}

ui::UIManager* Application::uiManager() const {
    return d->uiManager;
}

} // namespace core
} // namespace aicad
```

### 3. 修改 main.cpp

```cpp
#include "core/Application.h"

int main(int argc, char** argv) {
    QApplication qapp(argc, argv);
    
    // 初始化 AICAD 核心
    aicad::core::Application* app = aicad::core::Application::instance();
    
    if (!app->initialize()) {
        QMessageBox::critical(nullptr, "Error", "Failed to initialize AICAD");
        return 1;
    }
    
    // 顯示主視窗
    aicad::ui::UIManager* uiMgr = app->uiManager();
    if (uiMgr) {
        uiMgr->showMainWindow();
    }
    
    // 執行事件循環
    int result = qapp.exec();
    
    // 關閉應用程式
    app->shutdown();
    
    return result;
}
```

## 使用範例

### 1. 更新特徵樹

```cpp
// 在任何地方都可以呼叫
using namespace aicad;

core::Application* app = core::Application::instance();
ui::UIManager* uiMgr = app->uiManager();

// 更新特徵樹
uiMgr->updateFeatureTree();

// 設定狀態列訊息
uiMgr->setStatusMessage("Feature created successfully", 3000);
```

### 2. 使用 FeatureBrowser

```cpp
// 取得特徵瀏覽器
ui::FeatureBrowser* browser = uiMgr->featureBrowser();

// 連接信號
connect(browser, &ui::FeatureBrowser::featureSelected,
        this, [](int featureId) {
    qDebug() << "Feature selected:" << featureId;
    // 處理特徵選擇
});

// 刷新顯示
browser->refresh();

// 選擇特定特徵
browser->selectFeature(42);
```

### 3. 使用 PropertyPanel

```cpp
// 取得屬性面板
ui::PropertyPanel* panel = uiMgr->propertyPanel();

// 清空並添加屬性
panel->clear();
panel->addProperty("Name", "Sketch 1", false);
panel->addProperty("Type", "Sketch", false);
panel->addProperty("Plane", "XY", false);
panel->addProperty("Visible", true, true);  // 可編輯

// 監聽屬性變更
connect(panel, &ui::PropertyPanel::propertyChanged,
        this, [](const QString& name, const QVariant& value) {
    qDebug() << "Property changed:" << name << "=" << value;
    // 處理屬性變更
});
```

### 4. 使用 ToolManager

```cpp
// 取得工具管理器
ui::ToolManager* toolMgr = uiMgr->toolManager();

// 註冊動作
toolMgr->registerAction("new_sketch", "New Sketch", "Ctrl+K",
    []() {
        qDebug() << "Creating new sketch...";
        // 處理新建草圖
    },
    ":/icons/sketch.png");

// 建立工具列
QStringList actions = {"new_sketch", "separator", "save", "load"};
toolMgr->createToolBar("Sketch", actions);

// 啟用/停用動作
toolMgr->setActionEnabled("new_sketch", true);

// 監聽動作觸發
connect(toolMgr, &ui::ToolManager::actionTriggered,
        this, [](const QString& actionId) {
    qDebug() << "Action triggered:" << actionId;
});
```

## 事件整合

UI 系統已自動整合到 EventBus：

```cpp
// UI 會自動監聽這些事件並更新介面：
bus->publish(core::Events::DOCUMENT_CREATED, documentName);
bus->publish(core::Events::FEATURE_CREATED, featureName);
bus->publish(core::Events::SELECTION_CHANGED, featureId);
```

## 與其他模組的整合

### 與 CAD Engine (Ben) 整合

```cpp
// CAD Engine 建立特徵時
void FeatureManager::createFeature(...) {
    // 建立特徵
    Feature* feature = new Feature(...);
    
    // 發布事件（UI 會自動更新）
    EventBus* bus = aicadApp->eventBus();
    bus->publish(Events::FEATURE_CREATED, feature->name());
    
    // 或直接更新 UI
    UIManager* uiMgr = aicadApp->uiManager();
    uiMgr->updateFeatureTree();
}
```

### 與 View System (Felicia) 整合

```cpp
// View 中的選擇事件
void CadView::onFeatureSelected(int featureId) {
    // 更新屬性面板
    UIManager* uiMgr = aicadApp->uiManager();
    PropertyPanel* panel = uiMgr->propertyPanel();
    
    panel->clear();
    panel->addProperty("Feature ID", featureId);
    // ... 添加其他屬性
    
    // 同步特徵瀏覽器
    uiMgr->featureBrowser()->selectFeature(featureId);
}
```

### 與 Commands (Kaufen) 整合

```cpp
// 命令執行前後更新 UI
class SketchCommand : public Command {
public:
    void execute() override {
        UIManager* uiMgr = aicadApp->uiManager();
        
        // 執行前
        uiMgr->setStatusMessage("Creating sketch...");
        
        // 執行命令
        createSketch();
        
        // 執行後
        uiMgr->updateFeatureTree();
        uiMgr->setStatusMessage("Sketch created", 3000);
    }
};
```

## 編譯設定

### 修改 AICAD.pro

```qmake
HEADERS += \
    src/core/Application.h \
    src/core/EventBus.h \
    src/core/DocumentManager.h \
    src/ui/UIManager.h \
    src/ui/MainWindow.h \
    src/ui/FeatureBrowser.h \
    src/ui/PropertyPanel.h \
    src/ui/ToolManager.h

SOURCES += \
    src/main.cpp \
    src/core/Application.cpp \
    src/core/EventBus.cpp \
    src/core/DocumentManager.cpp \
    src/ui/UIManager.cpp \
    src/ui/MainWindow.cpp \
    src/ui/FeatureBrowser.cpp \
    src/ui/PropertyPanel.cpp \
    src/ui/ToolManager.cpp

INCLUDEPATH += $$PWD/src
```

## 注意事項

1. **執行緒安全**：所有 UI 操作必須在主執行緒中進行
2. **事件循環**：確保 QApplication::exec() 正在執行
3. **記憶體管理**：UI 元件由 UIManager 管理，不要手動刪除
4. **信號連接**：使用 Qt::QueuedConnection 進行跨執行緒通訊
5. **錯誤處理**：檢查指標是否為 nullptr

## 測試

### 基本測試程式

```cpp
#include "core/Application.h"
#include <QApplication>

int main(int argc, char** argv) {
    QApplication qapp(argc, argv);
    
    // 初始化
    auto app = aicad::core::Application::instance();
    if (!app->initialize()) {
        return 1;
    }
    
    // 取得 UI 管理器
    auto uiMgr = app->uiManager();
    
    // 測試功能
    uiMgr->showMainWindow();
    uiMgr->setStatusMessage("Testing UI system...");
    
    // 測試特徵瀏覽器
    auto browser = uiMgr->featureBrowser();
    browser->refresh();
    
    // 測試屬性面板
    auto panel = uiMgr->propertyPanel();
    panel->clear();
    panel->addProperty("Test", "Value");
    
    return qapp.exec();
}
```

## 後續開發

1. **圖示系統**：添加工具列和選單的圖示
2. **快捷鍵配置**：支援使用者自訂快捷鍵
3. **佈局儲存**：儲存和恢復視窗佈局
4. **主題支援**：支援明暗主題切換
5. **國際化**：添加多語言支援

## 總結

UI 系統提供了：
- ✅ 統一的 UI 管理介面
- ✅ 模組化的元件設計
- ✅ 事件總線整合
- ✅ 易於擴展的架構
- ✅ 完整的錯誤處理

所有 UI 元件都已實作 PIMPL 模式，遵循 AICAD 的設計規範，並與核心系統完美整合。