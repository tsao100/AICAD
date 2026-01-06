# Felicia 視圖系統建置說明

## Git 分支操作

### 建立功能分支

```bash
# 1. 確保在最新的 develop 分支
git checkout develop
git pull origin develop

# 2. 建立 Felicia 的功能分支
git checkout -b feature/view-system

# 3. 確認當前分支
git branch
# 應該顯示: * feature/view-system
```

### 開發流程

```bash
# 定期提交變更
git add src/view/
git commit -m "feat(view): 實作 CoordinateConverter"
git commit -m "feat(view): 實作 RubberBand 系統"
git commit -m "feat(view): 實作 GridOverlay"

# 推送到遠端
git push -u origin feature/view-system

# 定期同步 develop 的變更
git fetch origin
git rebase origin/develop

# 如果有衝突，解決後繼續
git add .
git rebase --continue
```

### 完成後建立 Pull Request

```bash
# 確保所有變更已提交
git status

# 最後推送
git push origin feature/view-system

# 在 GitHub 上建立 PR
# 標題: feat(view): 實作視圖系統核心功能
# 描述:
# - 實作 CoordinateConverter 座標轉換工具
# - 實作 RubberBand 橡皮筋視覺回饋
# - 實作 GridOverlay 網格覆蓋層
# - 所有類別包含完整文件註解
# - 包含樁檔案確保可獨立編譯
#
# 指派審查者: Jack
```

---

## 檔案清單

### Felicia 新增的檔案

```
src/
├── view/
│   ├── CoordinateConverter.h      - 座標轉換工具
│   ├── CoordinateConverter.cpp
│   ├── RubberBand.h               - 橡皮筋視覺回饋
│   ├── RubberBand.cpp
│   ├── GridOverlay.h              - 網格覆蓋層
│   └── GridOverlay.cpp
└── cad/
    └── geometry/
        └── CustomPlane.h          - 樁檔案 (等待 Ben)
```

### 更新的檔案

- `AICAD.pro` - 新增視圖模組的原始檔

---

## 建置步驟 (Qt Creator)

### 1. 開啟專案

```
File > Open File or Project...
選擇 AICAD.pro
```

### 2. 確認檔案結構

在 Qt Creator 左側 "Projects" 面板中，確認看到：

```
AICAD
├── Sources
│   ├── main.cpp
│   ├── MainWindow.cpp
│   ├── CadView.cpp
│   ├── core/
│   │   ├── Application.cpp
│   │   ├── EventBus.cpp
│   │   └── DocumentManager.cpp
│   └── view/
│       ├── CoordinateConverter.cpp
│       ├── RubberBand.cpp
│       └── GridOverlay.cpp
└── Headers
    ├── CadView.h
    ├── core/
    ├── view/
    └── cad/geometry/CustomPlane.h
```

如果看不到新檔案，右鍵點擊專案 > "Run qmake"

### 3. 清理並重新建置

```
建置 (Build) > 清理專案 "AICAD" (Clean Project)
建置 (Build) > 重新建置專案 "AICAD" (Rebuild Project)
```

或使用快捷鍵：
- **清理**: 無預設快捷鍵
- **建置**: `Ctrl+B` (Linux/Windows) / `Cmd+B` (macOS)
- **執行**: `Ctrl+R` (Linux/Windows) / `Cmd+R` (macOS)

### 4. 查看編譯輸出

在底部 "編譯輸出 (Compile Output)" 面板中，應該看到：

```
正在編譯 CoordinateConverter.cpp...
正在編譯 RubberBand.cpp...
正在編譯 GridOverlay.cpp...
正在編譯 CadView.cpp...
正在連結...
建置成功 (Build succeeded)
```

### 5. 執行程式

點擊左下角綠色三角形 ▶️ 按鈕，或按 `Ctrl+R`

### 常見 Qt Creator 操作

| 操作 | 快捷鍵 (Linux/Win) | 快捷鍵 (macOS) |
|------|-------------------|----------------|
| 建置 | `Ctrl+B` | `Cmd+B` |
| 執行 | `Ctrl+R` | `Cmd+R` |
| 除錯 | `F5` | `F5` |
| 停止 | `Shift+F5` | `Shift+F5` |
| 切換標頭/源 | `F4` | `F4` |
| 查找 | `Ctrl+F` | `Cmd+F` |
| 轉到定義 | `F2` | `F2` |

---

## Qt Creator 設定技巧

### 啟用所有警告

幫助發現潛在問題：

```
Projects (左側欄) > Build Settings > Build Steps
在 Make arguments 中加入: -Wall -Wextra
```

### 自動格式化

統一程式碼風格：

```
Tools > Options > C++ > Code Style
選擇 "Qt" 或 "LLVM"
勾選 "Enable automatic formatting"
```

### 快速切換檔案

使用 Locator (定位器)：

```
按 Ctrl+K (Cmd+K on macOS)
輸入檔案名稱即可快速開啟
例如: 輸入 "rubber" 快速開啟 RubberBand.cpp
```

### 查看編譯錯誤

雙擊 "Issues" 面板中的錯誤，會自動跳到對應程式碼位置。

---

## 除錯技巧

### 設定中斷點

在程式碼左側行號處點擊，出現紅點 🔴

### 啟動除錯

```
按 F5 或點擊左下角除錯按鈕 🐛
```

### 除錯視窗

除錯時會顯示：
- **區域變數 (Locals)**: 查看當前變數值
- **呼叫堆疊 (Call Stack)**: 查看函式呼叫鏈
- **中斷點 (Breakpoints)**: 管理所有中斷點

### 常用除錯快捷鍵

| 操作 | 快捷鍵 |
|------|--------|
| 繼續執行 | `F5` |
| 單步執行 (進入) | `F11` |
| 單步執行 (跳過) | `F10` |
| 跳出函式 | `Shift+F11` |
| 停止除錯 | `Shift+F5` |

### 除錯 OCCT 物件

OCCT 的 Handle 物件可以在除錯器中展開查看：

```cpp
// 在此行設中斷點
Handle(V3d_View) view = m_view;

// 除錯時可以展開 view 查看：
// - IsNull()
// - 內部指標
// - 參考計數
```

### 測試 1: CoordinateConverter

在 CadView.cpp 中加入測試:

```cpp
#include "view/CoordinateConverter.h"

void CadView::mousePressEvent(QMouseEvent* event) {
    // 測試座標轉換
    using namespace aicad::view;
    
    QVector2D planePos = CoordinateConverter::screenToPlane(
        m_view, event->pos(), currentPlane, this);
    
    qDebug() << "Screen:" << event->pos() 
             << "Plane:" << planePos;
}
```

### 測試 2: RubberBand

```cpp
#include "view/RubberBand.h"

void CadView::initializeViewer() {
    // ... 現有初始化 ...
    
    // 建立橡皮筋
    m_rubberBand = new RubberBand(m_view, m_context, 
                                  CustomPlane::XY(), this);
    m_rubberBand->setMode(RubberBandMode::Rectangle);
    m_rubberBand->setBasePoint(QVector2D(0, 0));
}

void CadView::mouseMoveEvent(QMouseEvent* event) {
    if (m_rubberBand) {
        QVector2D pos = screenToPlane(event->pos());
        m_rubberBand->updateCurrentPoint(pos);
    }
}
```

### 測試 3: GridOverlay

```cpp
#include "view/GridOverlay.h"

void CadView::setPendingSketch(TDF_Label sketch) {
    // ... 現有程式碼 ...
    
    // 顯示網格
    if (!m_grid) {
        m_grid = new GridOverlay(m_view, m_context, this);
    }
    
    m_grid->setPlane(plane);
    m_grid->setGridSize(20);
    m_grid->setGridSpacing(10.0f);
    m_grid->show();
}
```

---

## 預期輸出

### 編譯成功訊息

```
Compiling src/view/CoordinateConverter.cpp...
Compiling src/view/RubberBand.cpp...
Compiling src/view/GridOverlay.cpp...
Linking AICAD...
Build succeeded.
```

### 執行時訊息

```
[RubberBand] Created
[GridOverlay] Created
[CoordinateConverter] Screen to plane conversion: (150, 200) -> (15.0, 20.0)
[RubberBand] Mode changed to: 2
[GridOverlay] Plane set to: XY
[GridOverlay] Grid created. Size: 20 Spacing: 10
```

---

## 常見問題

### Q1: 編譯錯誤 "CustomPlane.h: No such file"

**解決方式**:
```bash
# 確認檔案存在
ls src/cad/geometry/CustomPlane.h

# 確認 .pro 檔案中有
INCLUDEPATH += $$PWD/src
```

### Q2: 連結錯誤 "undefined reference to RubberBand"

**解決方式**:
```bash
# 確認 .pro 檔案包含
SOURCES += src/view/RubberBand.cpp

# 完全重新編譯
make clean
qmake
make
```

### Q3: OCCT 相關錯誤

**解決方式**:
```bash
# Linux: 檢查 OCCT 安裝
pkg-config --modversion opencascade

# 確認 OCCT 函式庫路徑
ls /usr/local/occt/lib/libTKernel.so

# Windows: 確認環境變數
echo %PATH% | findstr OCCT
```

### Q4: 執行時沒有看到網格或橡皮筋

**可能原因**:
1. 平面未正確設定
2. 視圖未初始化
3. Context 為空

**除錯方式**:
```cpp
// 加入除錯輸出
qDebug() << "View is null:" << m_view.IsNull();
qDebug() << "Context is null:" << m_context.IsNull();
qDebug() << "Plane:" << plane.getDisplayName();
```

---

## 與其他模組的整合點

### 與 Jack (Core) 的整合

```cpp
// 訂閱視圖事件
auto* bus = Application::instance()->eventBus();
bus->subscribe(Events::VIEW_CHANGED, this, [this](const QVariant&) {
    qDebug() << "View changed event received";
});
```

### 與 Ben (CAD Engine) 的整合

```cpp
// 等待 Ben 完成 CustomPlane 正式版本後
// 1. 移除 src/cad/geometry/CustomPlane.h 樁檔案
// 2. 使用 Ben 的 #include "cad/geometry/CustomPlane.h"
// 3. 確認介面相容性
```

### 與 Kaufen (Commands) 的整合

```cpp
// Commands 可以使用 RubberBand
class RectangleCommand : public Command {
    void execute() override {
        m_rubberBand->setMode(RubberBandMode::Rectangle);
        m_rubberBand->setBasePoint(m_firstPoint);
    }
};
```

---

## 驗證檢查清單

### ✓ 編譯檢查
- [ ] 無編譯錯誤
- [ ] 無編譯警告
- [ ] 無連結錯誤
- [ ] 所有標頭檔可正確包含

### ✓ 功能檢查
- [ ] CoordinateConverter 座標轉換正確
- [ ] RubberBand 顯示正常
- [ ] GridOverlay 網格正確顯示
- [ ] 顏色和線型設定有效

### ✓ 程式碼品質
- [ ] 所有類別有完整 Doxygen 註解
- [ ] 遵循命名規範
- [ ] 使用 PIMPL 模式隱藏實作
- [ ] 正確的記憶體管理

### ✓ Git 檢查
- [ ] 在 feature/view-system 分支
- [ ] 提交訊息符合規範
- [ ] 無合併衝突
- [ ] 可以推送到遠端

---

## 下一步

Felicia 完成後:

1. **建立 Pull Request**
   - 標題清楚描述功能
   - 包含詳細的變更說明
   - 指派 Jack 審查

2. **等待審查**
   - 回應審查意見
   - 根據回饋修改
   - 推送更新

3. **合併後**
   - 通知其他成員
   - 更新文件
   - 協助其他成員整合

4. **持續改進**
   - 收集使用回饋
   - 優化效能
   - 新增功能

---

## 聯絡

如有任何問題，請聯絡:
- **Felicia**: felicia@aicad.dev, Slack @felicia
- **Jack** (審查者): jack@aicad.dev, Slack @jack

---

**文件版本**: 1.0  
**更新日期**: 2024-12-04  
**維護者**: Felicia