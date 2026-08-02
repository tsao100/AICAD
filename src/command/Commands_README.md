# AICAD 命令系統使用說明

## 概述

命令系統由 Kaufen 實作，提供統一的命令管理框架。所有 CAD 操作都應該透過命令系統執行。

## 核心組件

### 1. CommandManager
負責管理所有命令的註冊、執行和歷史記錄。

**主要功能：**
- 註冊/取消註冊命令
- 執行命令並處理結果
- 維護命令歷史
- 支援命令別名
- 提供自動完成功能

### 2. Command
所有命令的基礎類別，定義了命令的生命週期。

**生命週期：**
1. 建立命令物件
2. `initialize()` - 初始化
3. `validateParameters()` - 驗證參數
4. `execute()` - 執行命令
5. `cleanup()` - 清理資源
6. 刪除命令物件

### 3. CommandContext
命令執行的上下文資訊。

**包含：**
- `args` - 命令參數
- `sender` - 命令發送者
- `data` - 額外數據
- `interactive` - 是否為互動模式

### 4. CommandResult
命令執行結果。

**包含：**
- `success` - 是否成功
- `message` - 結果訊息
- `data` - 結果資料

## 使用範例

### 註冊命令

```cpp
#include "core/Application.h"
#include "core/CommandManager.h"
#include "commands/RectangleCommand.h"

// 在應用程式初始化時
CommandManager* cmdMgr = Application::instance()->commandManager();

// 註冊矩形命令
cmdMgr->registerCommand("rectangle", {"rect"}, 
    []() { return new RectangleCommand(); });
```

### 執行命令

```cpp
// 方式 1: 簡單執行
CommandResult result = cmdMgr->executeCommand("rectangle", 
    QStringList() << "0" << "0" << "100" << "50");

if (result.success) {
    qDebug() << "Success:" << result.message;
} else {
    qDebug() << "Failed:" << result.message;
}

// 方式 2: 使用上下文
CommandContext context;
context.args = QStringList() << "0" << "0" << "100" << "50";
context.interactive = false;

CommandResult result = cmdMgr->executeCommand("rectangle", context);
```

### 建立自訂命令

```cpp
// 1. 定義命令類別
class LineCommand : public Command {
public:
    LineCommand() : Command("line", "Draw a line") {}
    
    CommandResult execute(const CommandContext& context) override {
        setState(CommandState::Running);
        
        // 驗證參數
        if (!validateParameters(context)) {
            setState(CommandState::Failed);
            return CommandResult::Failure("Invalid parameters");
        }
        
        // 執行命令邏輯
        outputMessage("Drawing line...");
        
        // ... 實作繪製直線的邏輯 ...
        
        setState(CommandState::Completed);
        return CommandResult::Success("Line created");
    }
    
    bool validateParameters(const CommandContext& context) const override {
        // 需要 4 個參數：x1 y1 x2 y2
        return context.args.size() == 4;
    }
    
    QString getUsage() const override {
        return "Usage: line x1 y1 x2 y2\n"
               "Example: line 0 0 100 100";
    }
};

// 2. 註冊命令
cmdMgr->registerCommand("line", {"l"}, 
    []() { return new LineCommand(); });
```

### 監聽命令事件

```cpp
// 透過 CommandManager 的信號
connect(cmdMgr, &CommandManager::commandStarted,
    [](const QString& name) {
        qDebug() << "Command started:" << name;
    });

connect(cmdMgr, &CommandManager::commandFinished,
    [](const QString& name, const CommandResult& result) {
        qDebug() << "Command finished:" << name 
                 << "Success:" << result.success;
    });

// 或透過 EventBus
EventBus* bus = Application::instance()->eventBus();
bus->subscribe(Events::COMMAND_STARTED, this,
    [](const QVariant& data) {
        qDebug() << "Command started via EventBus:" << data.toString();
    });
```

## 與其他模組整合

### 與 DocumentManager 整合

```cpp
CommandResult execute(const CommandContext& context) override {
    // 取得文件管理器
    Application* app = Application::instance();
    DocumentManager* docMgr = app->documentManager();
    
    // 取得當前文件
    Document* doc = docMgr->currentDocument();
    if (!doc) {
        return CommandResult::Failure("No active document");
    }
    
    // 執行操作
    // ...
    
    return CommandResult::Success("Operation completed");
}
```

### 與 EventBus 整合

```cpp
CommandResult execute(const CommandContext& context) override {
    // 執行操作
    bool success = doSomething();
    
    if (success) {
        // 發布事件通知其他模組
        EventBus* bus = Application::instance()->eventBus();
        bus->publish("custom.event", QVariant::fromValue(result));
    }
    
    return CommandResult::Success("Done");
}
```

### 與 UI 整合（MainWindow）

```cpp
// 在 MainWindow 中連接命令系統
void MainWindow::initializeCommands() {
    CommandManager* cmdMgr = Application::instance()->commandManager();
    
    // 監聽命令訊息並顯示在狀態列
    connect(cmdMgr, &CommandManager::commandMessage,
        this, [this](const QString& msg) {
            statusBar()->showMessage(msg);
        });
    
    // 註冊命令
    cmdMgr->registerCommand("rectangle", {"rect"}, 
        []() { return new RectangleCommand(); });
}

// 從 UI 執行命令
void MainWindow::onRectangleButtonClicked() {
    CommandManager* cmdMgr = Application::instance()->commandManager();
    
    CommandContext context;
    context.interactive = true;  // 互動模式
    context.sender = this;
    
    cmdMgr->executeCommand("rectangle", context);
}
```

## 命令列表

以下是已實作的命令：

| 命令名稱 | 別名 | 參數 | 說明 |
|---------|------|------|------|
| rectangle | rect | [x1 y1 x2 y2] | 繪製矩形 |

## 注意事項

1. **命令生命週期**：每次執行命令都會建立新的命令實例，執行完畢後自動刪除
2. **參數驗證**：在 `validateParameters()` 中進行參數檢查
3. **錯誤處理**：使用 `CommandResult::Failure()` 回報錯誤
4. **狀態管理**：適時更新命令狀態（Ready → Running → Completed/Failed）
5. **訊息輸出**：使用 `outputMessage()` 輸出使用者可見的訊息
6. **事件發布**：重要操作完成後透過 EventBus 發布事件

## 待實作功能

- [ ] 命令的 Undo/Redo 支援
- [ ] 命令巨集錄製與回放
- [ ] 命令參數的即時提示
- [ ] 命令的圖形化參數輸入對話框
- [ ] 批次執行多個命令
- [ ] 命令執行的進度顯示

## 擴充建議

其他團隊成員可以實作以下命令：

**Ben (CAD Engine)**:
- LineCommand - 繪製直線
- ArcCommand - 繪製圓弧
- CircleCommand - 繪製圓
- ExtrudeCommand - 擠出特徵
- RevolveCommand - 旋轉特徵

**James (UI System)**:
- ZoomCommand - 視圖縮放
- PanCommand - 視圖平移
- RotateViewCommand - 視圖旋轉
- FitAllCommand - 全部適配

**Felicia (View System)**:
- ViewTopCommand - 上視圖
- ViewFrontCommand - 前視圖
- ViewRightCommand - 右視圖
- ViewIsometricCommand - 等角視圖

**Daney (Scripting)**:
- LoadScriptCommand - 載入腳本
- ExecuteScriptCommand - 執行腳本
- ExportScriptCommand - 匯出當前操作為腳本

## 總結

命令系統提供了統一的框架來管理所有 CAD 操作。透過這個系統，我們可以：

✅ 統一的命令介面
✅ 自動的歷史記錄
✅ 支援別名和自動完成
✅ 與 EventBus 無縫整合
✅ 易於擴充和維護

請參考 `RectangleCommand` 範例來實作新的命令！