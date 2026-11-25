# 零件組合（Assembly）獨立測試

不屬於 `AICAD.pro`；只需要 Eigen、OCCT、Qt5Core。在專案根目錄執行（路徑依環境調整）：

```bash
QT=/usr/include/x86_64-linux-gnu/qt5
OCC="-lTKernel -lTKMath -lTKG2d -lTKG3d -lTKGeomBase -lTKBRep -lTKGeomAlgo -lTKTopAlgo -lTKPrim -lTKShHealing"
INC="-Isrc -Isrc/cad -Isrc/ui -I/usr/include/eigen3 -I/usr/include/opencascade -I$QT -I$QT/QtCore -I$QT/QtGui -I$QT/QtWidgets"

# 1. 求解器（純 Eigen）
g++ -std=c++17 -O2 $INC src/cad/AssemblySolver.cpp src/test/assembly/solver_test.cpp -o /tmp/solver_test && /tmp/solver_test

# 2. 幾何參照擷取（OCCT）
g++ -std=c++17 -fPIC -w $INC src/test/assembly/geom_test.cpp src/cad/AssemblyGeom.cpp src/cad/AssemblySolver.cpp -o /tmp/geom_test -lQt5Core $OCC && /tmp/geom_test

# 3. 端到端（真實方塊 + 位姿轉換 + 參照凍結 + 求解 + 重新放置後量 BRep）
#    AssemblyPart.cpp 會引用 Document，這裡只用到它的靜態位姿函式 → 允許未解析符號
g++ -std=c++17 -fPIC -w $INC src/test/assembly/e2e_test.cpp src/cad/AssemblyPart.cpp src/cad/AssemblyGeom.cpp src/cad/AssemblySolver.cpp \
    -o /tmp/e2e_test -Wl,--unresolved-symbols=ignore-all -lQt5Core -lQt5Gui $OCC && /tmp/e2e_test
```

# 4. Feature 的 Part／Assembly／Void 歸屬（JSON 來回、兩種舊檔格式的遷移）
moc $INC src/cad/Feature.h -o /tmp/moc_Feature.cpp
g++ -std=c++17 -fPIC -w $INC src/test/assembly/feature_group_test.cpp /tmp/moc_Feature.cpp src/cad/Feature.cpp \
    -o /tmp/feature_group_test -lQt5Core -lQt5Gui -lTKernel -lTKMath -lTKBRep -lTKG3d -lTKG2d -lTKGeomBase && /tmp/feature_group_test

# 5. 配合 3D 標示的形狀（直接引入 .cpp 以測試匿名命名空間的 buildRef）
g++ -std=c++17 -fPIC -w $INC src/test/assembly/marker_shape_test.cpp src/cad/AssemblyGeom.cpp src/cad/AssemblySolver.cpp \
    -o /tmp/marker_test -lQt5Core -lTKV3d -lTKService $OCC -lTKMesh -lTKHLR && /tmp/marker_test
