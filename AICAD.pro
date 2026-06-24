# AICAD.pro - cross-platform Qt CAD project with ECL Lisp integration
# Q1-25 Ok.

CONFIG   += moc
CONFIG   += debug_and_release
QT       += qml

# ---------- macOS (Qt6 with Clang) ----------
macx {
    QT += widgets openglwidgets printsupport
    CONFIG += c++17

    QMAKE_CXXFLAGS += -Wall -Wextra

    # --- OCCT 7.9.3 (Homebrew Cellar) ---
    OCCT_DIR = /usr/local/Cellar/opencascade/7.9.3

    INCLUDEPATH += $$OCCT_DIR/include/opencascade
    LIBS += -L$$OCCT_DIR/lib \
            -Wl,-rpath,$$OCCT_DIR/lib

    LIBS += -lTKernel \
            -lTKMath \
            -lTKG2d \
            -lTKG3d \
            -lTKGeomBase \
            -lTKGeomAlgo \
            -lTKBRep \
            -lTKTopAlgo \
            -lTKPrim \
            -lTKV3d \
            -lTKOpenGl \
            -lTKService \
            -lTKLCAF \
            -lTKCAF \
            -lTKCDF \
            -lTKVCAF \
            -lTKMesh \
            -lTKHLR \
            -lTKBO \
            -lTKBool \
            -lTKOffset \
            -lTKFillet \
            -lTKXSBase \
            -lTKXCAF \
            -lTKBin \
            -lTKBinL \
            -lTKBinXCAF \
            -lTKShHealing

    # macOS 系統 framework（Cocoa 視窗 + OpenGL）
    LIBS += -framework Cocoa \
            -framework OpenGL \
            -framework IOKit \
            -framework CoreFoundation

    # --- ECL ---
    ECL_PREFIX = $$system(brew --prefix ecl 2>/dev/null)
    isEmpty(ECL_PREFIX): ECL_PREFIX = /usr/local/opt/ecl

    GMP_PREFIX = $$system(brew --prefix gmp 2>/dev/null)
    isEmpty(GMP_PREFIX): GMP_PREFIX = /usr/local/opt/gmp

    BDWGC_PREFIX = $$system(brew --prefix bdw-gc 2>/dev/null)
    isEmpty(BDWGC_PREFIX): BDWGC_PREFIX = /usr/local/opt/bdw-gc

    INCLUDEPATH += $$ECL_PREFIX/include
    INCLUDEPATH += $$GMP_PREFIX/include
    INCLUDEPATH += $$BDWGC_PREFIX/include
    LIBS += -L$$ECL_PREFIX/lib -lecl
    QMAKE_LFLAGS += -Wl,-rpath,$$ECL_PREFIX/lib

    INCLUDEPATH += /usr/local/Cellar/eigen/5.0.1/include/eigen3

    # Copy menu.txt to build directory
    copydata.commands = $(COPY_FILE) $$PWD/menu.txt $$OUT_PWD
    first.depends = $(first) copydata
    export(first.depends)
    export(copydata.commands)
    QMAKE_EXTRA_TARGETS += first copydata
}

# ---------- Linux (Qt5 with GCC) ----------
unix:!macx {
    QT += widgets opengl printsupport
    CONFIG += c++17

    QMAKE_CXXFLAGS += -Wall -Wextra
    QMAKE_CXXFLAGS += -std=c++11

    # --- OCCT 8.0.0 paths ---
    OCCT_DIR = /usr/local/occt

    INCLUDEPATH += $$OCCT_DIR/include/opencascade
    LIBS += -L$$OCCT_DIR/lib -Wl,-rpath,$$OCCT_DIR/lib

    LIBS += -lTKernel \
            -lTKMath \
            -lTKG2d \
            -lTKG3d \
            -lTKGeomBase \
            -lTKGeomAlgo \
            -lTKBRep \
            -lTKTopAlgo \
            -lTKPrim \
            -lTKV3d \
            -lTKOpenGl \
            -lTKService \
            -lTKLCAF \
            -lTKCAF \
            -lTKCDF \
            -lTKVCAF \
            -lTKMesh \
            -lTKHLR \
            -lTKBO \
            -lTKBool \
            -lTKOffset \
            -lTKFillet \
            -lTKXSBase \
            -lTKXCAF \
            -lTKBin \
            -lTKBinL \
            -lTKBinXCAF \
            -lTKShHealing

    # Link X11 (required for OpenGL context on Linux)
    LIBS += -lX11 -lXext

    DEFINES += __linux__

    # ECL headers
    INCLUDEPATH += /usr/include/ecl
    LIBS += -lecl -lgmp -lmpfr

    INCLUDEPATH += /usr/include/eigen3

    # Copy menu.txt to build directory
    copydata.commands = $(COPY_FILE) $$PWD/menu.txt $$OUT_PWD
    first.depends = $(first) copydata
    export(first.depends)
    export(copydata.commands)
    QMAKE_EXTRA_TARGETS += first copydata
}

# Define PROJECT_SOURCE_DIR as the .pro file's directory
DEFINES += PROJECT_SOURCE_DIR=\\\"$$PWD\\\"


# ---------- Windows (Qt6 with MSVC) ----------
win32 {
    QT += widgets openglwidgets printsupport
    CONFIG += c++17
    LIBS += -lopengl32
    DEFINES += _USE_MATH_DEFINES
    DEFINES += QT_NO_OPENGL_ES_2
    # --- OCCT 7.8.0 paths ---
    OCC_INC = D:/Git/OCCT/OCCT-install/inc
    OCC_LIB = D:/Git/OCCT/OCCT-install/win64/vc14/lib
    OCC_BIN = D:/Git/OCCT/OCCT-install/win64/vc14/bin

    INCLUDEPATH += $$OCC_INC
    LIBS += -L$$OCC_LIB

    # Add PATH for DLLs during execution
    QMAKE_POST_LINK += $$quote(cmd /C "set PATH=$$OCC_BIN;%PATH% && echo Added OCCT bin to PATH")

    # OCCT core libs (adjusted for 7.8.0)
    LIBS += -lTKernel \
            -lTKMath \
            -lTKG2d \
            -lTKG3d \
            -lTKGeomBase \
            -lTKGeomAlgo \
            -lTKBRep \
            -lTKTopAlgo \
            -lTKPrim \
            -lTKV3d \
            -lTKOpenGl \
            -lTKService \
            -lTKLCAF \
            -lTKCAF \
            -lTKCDF \
            -lTKVCAF \
            -lTKMesh \
            -lTKHLR \
            -lTKBO \
            -lTKBool \
            -lTKOffset \
            -lTKFillet \
            -lTKXSBase \
            -lTKXCAF \
            -lTKBin \
            -lTKBinL \
            -lTKBinXCAF \
            -lTKShHealing

    # ECL paths - ADJUST THESE TO YOUR ECL INSTALLATION
    INCLUDEPATH += D:/Git/ecl/v24.5.10/vc143-x64
    LIBS += -LD:/Git/ecl/v24.5.10/vc143-x64

    # Link ECL libraries
    msvc: {
        LIBS += ecl.lib
    }
    mingw: {
        LIBS += -lecl -lgmp -lmpfr
    }
    # Download from https://eigen.tuxfamily.org/
    INCLUDEPATH += D:/Git/eigen-5.0.0

    # 每次編譯完自動複製 menu.txt 到輸出資料夾
    QMAKE_POST_LINK += cmd /c copy /Y \
        "\"$$shell_path($$PWD/menu.txt)\"" \
        "\"$$shell_path($$OUT_PWD)\""

}

# ========================================
# 源文件和頭文件
# ========================================

SOURCES += \
    src/cad/ConstraintPickSession.cpp \
    src/cad/DependencyGraph.cpp \
    src/cad/PlaneManager.cpp \
    src/cad/SketchInstance.cpp \
    src/cad/grips/AIS_GripHandle.cpp \
    src/cad/grips/AlignmentGripProvider.cpp \
    src/cad/grips/GripManager.cpp \
    src/cad/grips/SketchGripProvider.cpp \
    src/cad/sketch/ConstraintOverlayManager.cpp \
    src/cad/sketch/ConstraintSolver.cpp \
    src/cad/sketch/ConstraintSymbolAIS.cpp \
    src/cad/sketch/DimensionLineAIS.cpp \
    src/cad/sketch/SketchConstraint.cpp \
    src/cad/sketch/SketchLoopFinder.cpp \
    src/cad/sketch/SketchPointAIS.cpp \
    src/cad/sketch/SketchRegion.cpp \
    src/command/ArcCommand.cpp \
    src/command/CircleCommand.cpp \
    src/command/CommandAlias.cpp \
    src/command/ConstraintCommands.cpp \
    src/command/GeneralDimCommand.cpp \
    src/cad/sketch/GeneralDimClassifier.cpp \
    src/command/ExtrudeCommand.cpp \
    src/command/alignment/SetOriginCommand.cpp \
    src/command/alignment/AlignmentAddSpiralCommand.cpp \
    src/command/alignment/AlignmentEditCommand.cpp \
    src/command/alignment/AlignmentFixCurveCommand.cpp \
    src/command/alignment/AlignmentFixTangentCommand.cpp \
    src/command/alignment/AlignmentFloatCurveCommand.cpp \
    src/command/alignment/AlignmentSCSCommand.cpp \
    src/command/alignment/TrackCenterLineCommands.cpp \
    src/command/alignment/VAlignFloatVCurveCommand.cpp \
    src/core/CommandHistory.cpp \
    src/command/EllipseCommand.cpp \
    src/command/InputParser.cpp \
    src/command/PolygonCommand.cpp \
    src/command/PolylineCommand.cpp \
    src/command/RectCommand.cpp \
    src/command/SplineCommand.cpp \
    src/core/ParameterStore.cpp \
    src/core/geometry/CoordinateTransform.cpp \
    src/core/geometry/ProjectOrigin.cpp \
    src/core/geometry/WorkPlane.cpp \
    src/drawing/DrawingSheet.cpp \
    src/drawing/DrawingSheetDialog.cpp \
    src/drawing/DrawingSheetManager.cpp \
    src/drawing/DrawingSheetWidget.cpp \
    src/drawing/DrawingViewRenderer.cpp \
    src/main.cpp \
    \
    # Core Module (核心系統)
    src/core/Application.cpp \
    src/core/EventBus.cpp \
    src/core/DocumentManager.cpp \
    src/core/PluginManager.cpp \
    src/core/Settings.cpp \
    src/core/MenuParser.cpp \
    \
    # CAD Module (Ben - CAD引擎)
    src/cad/Document.cpp \
    src/cad/Feature.cpp \
    src/cad/Sketch.cpp \
    src/cad/SketchEntity.cpp \
    src/cad/Extrude.cpp \
    src/cad/FeatureTree.cpp \
    src/cad/Plane.cpp \
    \
    # Geometry Module                # ✅ 添加整個 geometry 模組
    src/geometry/GeometryBuilder.cpp \
    \
    # UI Module (James - 用戶界面)
    src/manipulator/AIS_ExtrudeManipulator.cpp \
    src/manipulator/ExtrudeManipulator.cpp \
    src/osnap/OSnapDetector.cpp \
    src/osnap/OSnapIndicator.cpp \
    src/osnap/OSnapManager.cpp \
    src/osnap/OSnapToolbar.cpp \
    src/railway/AlignmentDocument.cpp \
    src/railway/AlignmentSolver.cpp \
    src/railway/RailwayAlignment.cpp \
    src/railway/RailwayAlignmentElement.cpp \
    src/ui/AutoCompleteModel.cpp \
    src/ui/CommandHistoryPopup.cpp \
    src/ui/CommandInputEdit.cpp \
    src/core/CommandLineManager.cpp \
    src/ui/CommandLineWidget.cpp \
    src/ui/GripEventFilter.cpp \
    src/ui/MainWindow.cpp \
    src/ui/FeatureBrowser.cpp \
    src/ui/ParameterPanel.cpp \
    src/ui/PropertyPanel.cpp \
    src/ui/SketchPanel.cpp \
    src/ui/ToolManager.cpp \
    src/ui/TransientCommandHistory.cpp \
    src/ui/UIManager.cpp \
    src/ui/ResultPopup.cpp \
    \
    # View Module (Felicia - 視圖系統)
    src/ui/VAlignCommandBar.cpp \
    src/ui/VAlignEditorDockWidget.cpp \
    src/ui/VAlignProfileView.cpp \
    src/ui/VAlignTheme.cpp \
    src/view/AlignmentRenderer.cpp \
    src/view/ViewManager.cpp \
    src/view/CadView.cpp \
    src/view/DimPreviewOverlay.cpp \
    src/view/RubberBand.cpp \
    #src/view/SelectionManager.cpp \
    src/view/ViewGrid.cpp \
    \
    # Command Module (Kaufen - 命令系統)
    src/command/CommandManager.cpp \
    src/command/Command.cpp \
    src/command/BasicCommands.cpp \
    src/command/LineCommand.cpp \
    \
    # Scripting Module (Daney - 腳本系統)
    src/scripting/LispEngine.cpp \
    src/scripting/LispBindings.cpp \
    src/scripting/ScriptManager.cpp

HEADERS += \
    # Core Module
    src/cad/ConstraintPickSession.h \
    src/cad/DependencyGraph.h \
    src/cad/PlaneManager.h \
    src/cad/SketchInstance.h \
    src/cad/grips/AIS_GripHandle.h \
    src/cad/grips/AlignmentGripProvider.h \
    src/cad/grips/GripManager.h \
    src/cad/grips/GripPoint.h \
    src/cad/grips/GripProvider.h \
    src/cad/grips/SketchGripProvider.h \
    src/cad/sketch/ConstraintOverlayManager.h \
    src/cad/sketch/ConstraintSolver.h \
    src/cad/sketch/ConstraintSymbolAIS.h \
    src/cad/sketch/DimensionLineAIS.h \
    src/cad/sketch/SketchConstraint.h \
    src/cad/sketch/SketchLoopFinder.h \
    src/cad/sketch/SketchPointAIS.h \
    src/cad/sketch/SketchRegion.h \
    src/command/ArcCommand.h \
    src/command/CircleCommand.h \
    src/command/CommandAlias.h \
    src/command/ConstraintCommands.h \
    src/command/GeneralDimCommand.h \
    src/cad/sketch/GeneralDimClassifier.h \
    src/command/ExtrudeCommand.h \
    src/command/GripMoveCommand.h \
    src/command/alignment/SetOriginCommand.h \
    src/command/alignment/AlignmentAddSpiralCommand.h \
    src/command/alignment/AlignmentCommandBase.h \
    src/command/alignment/AlignmentEditCommand.h \
    src/command/alignment/AlignmentFixCurveCommand.h \
    src/command/alignment/AlignmentFixTangentCommand.h \
    src/command/alignment/AlignmentFloatCurveCommand.h \
    src/command/alignment/AlignmentSCSCommand.h \
    src/command/alignment/ProfileViewCommand.h \
    src/command/alignment/TrackCenterLineCommands.h \
    src/command/alignment/VAlignCheckGradeCommand.h \
    src/command/alignment/VAlignFloatVCurveCommand.h \
    src/command/alignment/VAlignMovePVICommand.h \
    src/command/alignment/VAlignSetKCommand.h \
    src/core/CommandHistory.h \
    src/command/EllipseCommand.h \
    src/command/InputParser.h \
    src/command/PolygonCommand.h \
    src/command/PolylineCommand.h \
    src/command/RectCommand.h \
    src/command/SplineCommand.h \
    src/core/Application.h \
    src/core/EventBus.h \
    src/core/DocumentManager.h \
    src/core/ParameterStore.h \
    src/core/PluginManager.h \
    src/core/Settings.h \
    src/core/MenuParser.h \
    \
    # CAD Module (Ben)
    src/cad/Document.h \
    src/cad/Feature.h \
    src/cad/Sketch.h \
    src/cad/SketchEntity.h \
    src/cad/Extrude.h \
    src/cad/FeatureTree.h \
    src/cad/Plane.h \                # ✅ 添加
    \
    # Geometry Module                # ✅ 添加
    src/core/geometry/CoordinateTransform.h \
    src/core/geometry/ProjectOrigin.h \
    src/core/geometry/WorkPlane.h \
    src/drawing/DrawingSheet.h \
    src/drawing/DrawingSheetManager.h \
    src/drawing/DrawingSheetWidget.h \
    src/drawing/DrawingViewRenderer.h \
    src/geometry/GeometryBuilder.h \
    \
    # UI Module (James)
    src/manipulator/AIS_ExtrudeManipulator.h \
    src/manipulator/ExtrudeManipulator.h \
    src/osnap/OSnapDetector.h \
    src/osnap/OSnapIndicator.h \
    src/osnap/OSnapManager.h \
    src/osnap/OSnapToolbar.h \
    src/osnap/OSnapTypes.h \
    src/railway/AlignmentDocument.h \
    src/railway/AlignmentSolver.h \
    src/railway/RailwayAlignment.h \
    src/railway/RailwayAlignmentElement.h \
    src/ui/AutoCompleteModel.h \
    src/ui/CommandHistoryPopup.h \
    src/ui/CommandInputEdit.h \
    src/core/CommandLineManager.h \
    src/ui/CommandLineWidget.h \
    src/ui/GripEventFilter.h \
    src/ui/MainWindow.h \
    src/ui/FeatureBrowser.h \
    src/ui/ParameterPanel.h \
    src/ui/PropertyPanel.h \
    src/ui/SketchPanel.h \
    src/ui/ToolManager.h \
    src/ui/TransientCommandHistory.h \
    src/ui/UIManager.h \
    src/ui/ResultPopup.h \
    \
    # View Module (Felicia)
    src/ui/VAlignCommandBar.h \
    src/ui/VAlignEditorDockWidget.h \
    src/ui/VAlignProfileView.h \
    src/ui/VAlignTheme.h \
    src/view/AlignmentRenderer.h \
    src/view/ViewManager.h \
    src/view/CadView.h \
    src/view/DimPreviewOverlay.h \
    src/view/RubberBand.h \
    #src/view/SelectionManager.h \
    src/view/ViewGrid.h \
    \
    # Command Module (Kaufen)
    src/command/CommandManager.h \
    src/command/Command.h \
    src/command/CommandTypes.h \
    src/command/CommandFactory.h \
    src/command/LineCommand.h \
    \
    # Scripting Module (Daney)
    src/scripting/LispEngine.h \
    src/scripting/LispBindings.h \
    src/scripting/ScriptManager.h

RESOURCES += \
    resources.qrc

# 設定 Include 路徑
INCLUDEPATH += $$PWD/src
