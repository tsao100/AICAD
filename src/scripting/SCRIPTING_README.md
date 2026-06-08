# AICAD Scripting 系統文件

## 概述

AICAD Scripting 系統提供 ECL (Embeddable Common Lisp) 整合，讓使用者可以用 Lisp 語言自動化 CAD 操作。

## 架構

```
aicad/
├── scripting/
│   ├── LispEngine.h        # Lisp 引擎核心
│   ├── LispEngine.cpp
│   ├── LispBindings.h      # AICAD API 綁定
│   └── LispBindings.cpp
└── core/
    └── Application.h       # 整合 Scripting 到主程式
```

## 功能特點

### 1. LispEngine (Lisp 引擎)

- **ECL 環境管理**: 初始化、執行、關閉
- **型別轉換**: Lisp ↔ Qt/C++
- **函式註冊**: 將 C++ 函式暴露給 Lisp
- **錯誤處理**: 捕獲和報告 Lisp 錯誤

### 2. LispBindings (API 綁定)

將 AICAD 功能綁定到 Lisp，分為五大類：

#### A. 文件管理 API
```lisp
(sketch-create "XY" "MySketch")         ; 建立草圖
(extrude-create 1 50 "MyExtrude")       ; 建立拉伸
(feature-delete 1)                      ; 刪除特徵
```

#### B. 幾何繪圖 API
```lisp
(line 0 0 100 100)                      ; 繪製直線
(rectangle 0 0 100 50)                  ; 繪製矩形
(circle 50 50 25)                       ; 繪製圓形
(arc 50 50 30 0 90)                     ; 繪製圓弧
(polyline 0 0 50 0 50 50 0 50 0 0)      ; 繪製多段線
```

#### C. 視圖控制 API
```lisp
(view-set "TOP")                        ; 設定視圖
(view-fit)                              ; 自動縮放
(view-zoom 1.5)                         ; 縮放視圖
```

#### D. 查詢功能 API
```lisp
(feature-list)                          ; 取得所有特徵
(feature-info 1)                        ; 取得特徵資訊
(feature-count)                         ; 取得特徵數量
(sketch-polylines 1)                    ; 取得草圖折線
```

#### E. 工具函式 API
```lisp
(distance 0 0 100 100)                  ; 計算距離
(angle 0 0 100 100)                     ; 計算角度
(polar 50 50 100 45)                    ; 極座標計算
(print-message "Hello World")           ; 顯示訊息
```

## 使用方式

### 1. 初始化

```cpp
#include "core/Application.h"
#include "scripting/LispEngine.h"
#include "scripting/LispBindings.h"

// 初始化應用程式 (自動初始化 Scripting)
Application* app = Application::instance();
app->initialize();

// 取得 Lisp 引擎
LispEngine* lisp = app->lispEngine();
```

### 2. 執行 Lisp 代碼

```cpp
// 方式 1: 直接執行字串
QVariant result = lisp->eval("(+ 1 2 3)");
qDebug() << result.toInt();  // 6

// 方式 2: 載入檔案
lisp->loadFile("example.lisp");

// 方式 3: 透過 MainWindow 的命令輸入
// 使用者可以直接在命令列輸入 Lisp 代碼
```

### 3. 註冊自訂函式

```cpp
lisp->registerFunction("MY-FUNCTION", 
    [](const QVariantList& args) -> QVariant {
        // 你的實作
        return QVariant::fromValue(QString("Success!"));
    }, 
    2,   // 最小參數數
    3    // 最大參數數 (-1 表示不限)
);
```

### 4. Lisp 腳本範例

```lisp
;; 建立立方體
(defun create-box ()
  (sketch-create "XY" "Base")
  (rectangle 0 0 100 100)
  (extrude-create 1 50 "Body")
  (view-set "ISOMETRIC")
  (view-fit))

;; 執行
(create-box)
```

## 整合到 MainWindow

MainWindow 已經整合了 Lisp 引擎，提供：

1. **命令列輸入**: 按 F2 切換 Lisp 控制台
2. **交互式 getpoint**: 使用 `(getpoint)` 取得使用者點擊
3. **歷史記錄**: Up/Down 鍵瀏覽命令歷史

### getpoint 使用範例

```lisp
;; 交互式繪製直線
(defun draw-line-interactive ()
  (let ((p1 (getpoint "First point: "))
        (p2 (getpoint p1 "Second point: ")))
    (when (and p1 p2)
      (apply #'line (append p1 p2)))))

;; 執行
(draw-line-interactive)
```

## 事件系統整合

LispBindings 透過 EventBus 與其他模組通訊：

```cpp
// Lisp 函式發布事件
d->app->eventBus()->publish("scripting.geometry-add", data);

// 其他模組訂閱事件
bus->subscribe("scripting.geometry-add", this, [](const QVariant& data) {
    // 處理幾何建立
});
```

## 型別轉換

### Lisp → Qt

| Lisp 型別 | Qt 型別 |
|----------|--------|
| NIL | QVariant() |
| T | true |
| integer | int/long |
| float | double |
| string | QString |
| list | QVariantList |

### Qt → Lisp

| Qt 型別 | Lisp 型別 |
|--------|----------|
| bool | T/NIL |
| int | integer |
| double | float |
| QString | string |
| QVariantList | list |

## 錯誤處理

```cpp
// 檢查錯誤
QVariant result = lisp->eval("(invalid syntax");
if (!lisp->lastError().isEmpty()) {
    qWarning() << "Error:" << lisp->lastError();
}

// 監聽錯誤信號
connect(lisp, &LispEngine::errorOccurred, this, 
    [](const QString& error) {
        qWarning() << "Lisp Error:" << error;
    });
```

## 編譯需求

### Unix/Linux
```bash
# 安裝 ECL
sudo apt-get install ecl libecl-dev

# AICAD.pro 已設定
INCLUDEPATH += /usr/include/ecl
LIBS += -lecl -lgmp -lmpfr
```

### Windows
```bash
# 下載 ECL 預編譯版本
# 設定 AICAD.pro:
INCLUDEPATH += D:/Git/ecl/v24.5.10/vc143-x64
LIBS += -LD:/Git/ecl/v24.5.10/vc143-x64
LIBS += ecl.lib
```

## 測試

```cpp
// 測試基本運算
QCOMPARE(lisp->eval("(+ 1 2 3)").toInt(), 6);

// 測試函式註冊
lisp->registerFunction("TEST", [](const QVariantList&) {
    return 42;
});
QCOMPARE(lisp->eval("(test)").toInt(), 42);

// 測試型別轉換
QVariantList list = {1, 2, 3};
lisp->setVariable("MY-LIST", list);
QVariant result = lisp->eval("(length my-list)");
QCOMPARE(result.toInt(), 3);
```

## 最佳實務

1. **錯誤處理**: 所有 Lisp 呼叫都要檢查錯誤
2. **參數驗證**: 在 C++ 端驗證參數數量和型別
3. **記憶體管理**: ECL 會自動管理 Lisp 物件
4. **執行緒安全**: Lisp 引擎不是執行緒安全的
5. **FPU 狀態**: Unix 上需要保存/恢復 FPU 狀態

## 擴充功能

要新增自訂 API：

```cpp
// 在 LispBindings 中新增函式
void LispBindings::registerCustomAPI() {
    d->engine->registerFunction("MY-API", 
        [this](const QVariantList& args) -> QVariant {
            // 實作...
            return QVariant();
        }, 
        minArgs, maxArgs);
}

// 在 registerAll() 中呼叫
void LispBindings::registerAll() {
    registerDocumentAPI();
    registerGeometryAPI();
    // ...
    registerCustomAPI();  // 新增這行
}
```

## 範例腳本

完整範例請參考 `example.lisp`：

- 基本幾何繪圖
- 建立草圖和拉伸
- 交互式命令
- 幾何計算
- 批次處理

## 常見問題

### Q: Lisp 引擎初始化失敗？
A: 檢查 ECL 是否正確安裝，路徑是否設定正確。

### Q: 函式找不到？
A: 確認函式名稱正確 (區分大小寫)，並已註冊。

### Q: getpoint 不工作？
A: 確認在正交視圖模式，且有活動草圖。

### Q: 如何除錯 Lisp 腳本？
A: 使用 `(print-message ...)` 輸出除錯訊息。

## 未來改進

- [ ] 更多幾何 API (spline, fillet 等)
- [ ] 尺寸標註 API
- [ ] 圖層管理 API
- [ ] 材質和渲染 API
- [ ] 匯入/匯出 API
- [ ] 參數化設計支援

## 總結

Daney 已完成:
- ✅ LispEngine 核心
- ✅ LispBindings API
- ✅ 整合到 Application
- ✅ 型別轉換系統
- ✅ 錯誤處理
- ✅ 範例腳本

團隊成員可以:
1. 使用 Lisp 自動化 CAD 操作
2. 擴充 API 綁定
3. 撰寫測試腳本
4. 建立腳本庫