## 11. 給團隊成員的說明

### 11.1 Jack 已完成的工作

✅ **Application 單例系統**
- 統一管理應用程式生命週期
- 初始化所有子系統
- 提供全域存取點

✅ **EventBus 事件總線**
- 實作發布-訂閱模式
- 支援自動清理訂閱
- 提供標準事件名稱常數

✅ **基礎架構設計**
- 定義模組介面規範
- 建立依賴規則
- 設定 PIMPL 模式範例

### 11.2 各成員下一步工作

**Ben (CAD Engine)**:
```cpp
// 實作這些類別
class DocumentManager;  // 管理多個文件
class Document;         // 單一文件
class Feature;          // 特徵基礎類別
class Sketch;           // 草圖特徵
class Extrude;          // 擠出特徵

// 參考
Application* app = Application::instance();
EventBus* bus = app->eventBus();
bus->publish(Events::FEATURE_CREATED, QVariant::fromValue(feature));
```

**James (UI System)**:
```cpp
// 實作這些類別
class MainWindow;       // 簡化的主視窗
class FeatureBrowser;   // 特徵瀏覽器

// 在 MainWindow 建構子中
Application* app = Application::instance();
DocumentManager* docMgr = app->documentManager();
connect(docMgr, &DocumentManager::documentCreated, 
        this, &MainWindow::onDocumentCreated);
```

**Felicia (View System)**:
```cpp
// 實作這些類別
class ViewManager;      // 管理多個視圖
class CadView;          // 重構後的視圖
class RubberBand;       // 獨立的橡皮筋類別

// 使用事件總線
EventBus* bus = aicadApp->eventBus();
bus->subscribe(Events::FEATURE_UPDATED, this, [this](const QVariant& data) {
    updateDisplay();
});
```

**Kaufen (Commands)**:
```cpp
// 實作這些類別
class CommandManager;   // 命令管理器
class Command;          // 命令基礎類別
class RectangleCommand; // 矩形命令範例

// 註冊命令
CommandManager* cmdMgr = aicadApp->commandManager();
cmdMgr->registerCommand("rectangle", {"rect"}, []() {
    return new RectangleCommand();
});
```

**Daney (Scripting)**:
```cpp
// 實作這些類別
class LispEngine;       // Lisp 引擎
class LispBindings;     // API 綁定

// 註冊函式
LispEngine* lisp = aicadApp->lispEngine();
lisp->registerFunction("sketch-create", [](const QVariantList& args) {
    // 實作...
    return QVariant::fromValue(sketch);
});
```

### 11.3 使用 Application 的範例
```cpp
// 任何地方都可以存取
#include "core/Application.h"

void someFunction() {
    using namespace aicad::core;
    
    // 方式 1: 使用巨集
    Application* app = aicadApp;
    
    // 方式 2: 完整路徑
    Application* app = Application::instance();
    
    // 存取子系統
    DocumentManager* docMgr = app->documentManager();
    CommandManager* cmdMgr = app->commandManager();
    EventBus* bus = app->eventBus();
    
    // 發布事件
    bus->publish(Events::FEATURE_CREATED, someData);
    
    // 訂閱事件
    bus->subscribe(Events::DOCUMENT_OPENED, this, [](const QVariant& data) {
        // 處理事件
    });
}
```

### 11.4 編譯與測試
```bash
# 編譯
qmake AICAD.pro
make -j4

# 執行
./AICAD

# 執行測試
qmake AICAD.pro CONFIG+=test
make
./test_aicad
```

## 12. 注意事項

1. **所有新類別都應該使用 PIMPL 模式** (參考 Application 的實作)
2. **使用 EventBus 進行模組間通訊**，避免直接依賴
3. **遵循命名空間規範**: `aicad::core`, `aicad::cad` 等
4. **所有公開 API 都要有 Doxygen 註解**
5. **提交前確保編譯無警告**
6. **為新功能撰寫單元測試**

## 總結

Jack 已經完成了核心基礎架構，包括:
- ✅ Application 單例系統
- ✅ EventBus 事件總線
- ✅ PluginManager 外掛框架
- ✅ 設定管理系統
- ✅ 版本資訊管理
- ✅ 完整的測試範例

現在各成員可以開始實作各自的模組，並透過 Application 和 EventBus 進行整合！