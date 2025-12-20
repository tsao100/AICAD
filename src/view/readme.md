```

### 5.4 手動測試指南

**Felicia 的測試步驟:**

#### 步驟 1: 編譯測試
```bash
cd AICAD
qmake AICAD.pro
make -j4

# 編譯測試
qmake AICAD.pro CONFIG+=test
make
```

#### 步驟 2: 執行單元測試
```bash
# 執行 ViewManager 測試
./test_viewmanager

# 執行 RubberBand 測試
./test_rubberband
```

#### 步驟 3: 視覺測試

```

編譯並執行:
```bash
g++ -o test_view_visual test_view_visual.cpp \
    -I../src \
    -L../build \
    -lAICAD \
    $(pkg-config --cflags --libs Qt5Widgets) \
    $(pkg-config --cflags --libs opencascade)

./test_view_visual
```

#### 步驟 4: 測試檢查清單

- [ ] ViewManager 可以建立視圖
- [ ] ViewManager 可以管理多個視圖
- [ ] CadView 正確顯示 3D 內容
- [ ] 視圖方向切換正常 (Top, Front, Isometric)
- [ ] 滑鼠平移/旋轉/縮放正常
- [ ] RubberBand 線條模式正常
- [ ] RubberBand 矩形模式正常
- [ ] RubberBand 折線模式正常
- [ ] GridOverlay 顯示正常
- [ ] GridOverlay 軸線顏色正確
- [ ] 座標轉換功能正確
- [ ] ViewCube 點擊響應正常

#### 步驟 5: 效能測試
```bash
# 使用 valgrind 檢查記憶體洩漏
valgrind --leak-check=full ./test_view_visual

# 使用 perf 分析效能
perf record ./test_view_visual
perf report
```

### 5.5 整合測試

測試視圖與文件的整合:
```cpp

// 更多整合測試...
```

---

## 總結

Felicia 現在有完整的視圖系統實作:

✅ **ViewManager** - 管理多個視圖
✅ **CadView** - 重構後的 3D 視圖
✅ **RubberBand** - 獨立的橡皮筋類別
✅ **GridOverlay** - 網格覆蓋層
✅ **完整的測試套件** - 單元測試 + 整合測試 + 手動測試

**下一步:**
1. 執行所有測試確保功能正常
2. 修復任何發現的問題
3. 與其他模組整合 (Ben 的 CAD 引擎, Kaufen 的命令系統)
4. 進行效能優化
5. 撰寫使用文件

祝 Felicia 測試順利! 🚀