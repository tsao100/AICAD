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

# 啟用 Lisp 引擎（ECL）：
# macx / unix:!macx / win32 三個平台區塊都已經連結 ECL 的 include path 與函式庫，
# 但少了 HAVE_ECL 這個巨集，導致 LispEngine.h / LispEngine.cpp 內所有 #ifdef HAVE_ECL
# 保護的實際 ECL 呼叫都被跳過，只會編譯出永遠回傳失敗的 stub（initialize()/eval()
# 都直接回傳 "ECL support not compiled in"）。加上這行讓三個平台都吃到真正的實作。
DEFINES += HAVE_ECL


# ---------- Windows (Qt6 with MSVC) ----------
win32 {
    QT += widgets openglwidgets printsupport
    CONFIG += c++17
    LIBS += -lopengl32
    # PASTECLIP 直接呼叫 Win32 剪貼簿／GDI API（src/core/WinClipboardEmf.cpp）
    LIBS += -luser32 -lgdi32
    DEFINES += _USE_MATH_DEFINES

    # Eigen 模板（ConstraintSolver / AlignmentNLSolver）在 Debug 下會產生極多 section，
    # 超過 COFF 預設上限（MinGW: "too many sections / file too big"；MSVC: C1128）。
    msvc:   QMAKE_CXXFLAGS += /bigobj
    mingw:  QMAKE_CXXFLAGS += -Wa,-mbig-obj

    # ── 加速編譯／連結（MSVC）────────────────────────────────────────────
    msvc {
        # 預編譯標頭：Qt／std 常用標頭只解析一次
        CONFIG += precompile_header
        PRECOMPILED_HEADER = src/pch.h

        # /Zc:inline：丟掉未被引用的 inline 函式，縮小 .obj、加快連結（Eigen／Qt 模板很多）
        QMAKE_CXXFLAGS += /Zc:inline

        # Debug 連結：只放索引、不複製完整除錯資訊，連結時間大幅縮短
        QMAKE_LFLAGS_DEBUG += /DEBUG:FASTLINK
    }
    DEFINES += QT_NO_OPENGL_ES_2
    # --- OCCT 7.8.0 paths ---
    OCC_INC = D:/Git/OCCT/OCCT-install/inc
    OCC_LIB = D:/Git/OCCT/OCCT-install/win64/vc14/lib
    OCC_BIN = D:/Git/OCCT/OCCT-install/win64/vc14/bin

    INCLUDEPATH += $$OCC_INC
    LIBS += -L$$OCC_LIB

    # （原本這裡有一行 QMAKE_POST_LINK += cmd /C "set PATH=... && echo ..."：
    #  ① set PATH 只影響建置當下的 cmd，之後執行 AICAD.exe 並不會生效；
    #  ② 它結尾沒有命令分隔，後面所有 QMAKE_POST_LINK += 的內容都被接在 echo 後面當成它的參數，
    #     所以 menu.txt 的 copy 根本沒被執行。已移除。執行時的 DLL 路徑請見下方 OCC_3RD_BIN／Run 環境設定。）

    # ── OCCT 第三方 DLL（freetype.dll 等；TKService.dll 依賴它）────────────────
    # 上面那行 set PATH 只影響「建置當下」的 cmd，不會影響之後執行 AICAD.exe，所以這裡
    # 改成：把第三方 DLL 資料夾填進 OCC_3RD_BIN，建置後自動複製（只複製較新的）到 exe 旁邊。
    # 例：OCC_3RD_BIN = D:/Git/OCCT/3rdparty-vc14-64/freetype-2.13.2-vc14-64/bin
    # 多個資料夾可用空格分隔。留空 = 不複製（改在 Qt Creator 的 Run 環境設 PATH 亦可）。
    OCC_3RD_BIN =
    !isEmpty(OCC_3RD_BIN) {
        CONFIG(debug, debug|release): DLL_OUT = $$OUT_PWD/debug
        else:                         DLL_OUT = $$OUT_PWD/release
        for(dir, OCC_3RD_BIN) {
            # /I：目的地視為資料夾；目的路徑不可以反斜線結尾（會跳脫結尾的引號）
            QMAKE_POST_LINK += cmd /c xcopy /D /Y /Q /I \
                "\"$$shell_path($$dir)\\*.dll\"" \
                "\"$$shell_path($$DLL_OUT)\"" \
                $$escape_expand(\n\t)
        }
    }

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
    # ⚠️ 每條 POST_LINK 命令結尾一定要加 $$escape_expand(\n\t)，否則多個 += 會被接成同一行命令。
    QMAKE_POST_LINK += cmd /c copy /Y \
        "\"$$shell_path($$PWD/menu.txt)\"" \
        "\"$$shell_path($$OUT_PWD)\"" \
        $$escape_expand(\n\t)

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
    src/cad/sketch/SketchAxisAIS.cpp \
    src/cad/sketch/SketchConstraint.cpp \
    src/cad/sketch/SketchAnnotation.cpp \
    src/cad/sketch/AnnotationTextFormatter.cpp \
    src/cad/sketch/LeaderNoteAIS.cpp \
    src/cad/sketch/AnnotationStandardsChecker.cpp \
    src/cad/sketch/SketchLoopFinder.cpp \
    src/cad/sketch/SketchPointAIS.cpp \
    src/cad/sketch/SketchRegion.cpp \
    src/cad/sketch/SketchGeomTransformUtil.cpp \
    src/cad/sketch/SketchGeom2DMath.cpp \
    src/command/ArcCommand.cpp \
    src/command/CircleCommand.cpp \
    src/command/CommandAlias.cpp \
    src/command/ConstraintCommands.cpp \
    src/command/SketchEditCommand.cpp \
    src/command/SketchSelectionPicker.cpp \
    src/command/CommandRubberBandHelper.cpp \
    src/command/SketchTransformCommandBase.cpp \
    src/command/MoveCommand.cpp \
    src/command/CopyCommand.cpp \
    src/command/RotateCommand.cpp \
    src/command/MirrorCommand.cpp \
    src/command/StretchCommand.cpp \
    src/command/TrimExtendHelper.cpp \
    src/command/TrimCommand.cpp \
    src/command/ExtendCommand.cpp \
    src/command/FilletCommand.cpp \
    src/command/OffsetCommand.cpp \
    src/command/GeneralDimCommand.cpp \
    src/command/LeaderNoteCommand.cpp \
    src/cad/sketch/GeneralDimClassifier.cpp \
    src/cad/sketch/GeometryRelationshipAnalyzer.cpp \
    src/command/ExtrudeCommand.cpp \
    src/command/ChamferCommand.cpp \
    src/command/ConstructionToggleCommand.cpp \
    src/command/PasteClipCommand.cpp \
    src/core/geometry/EmfVectorParser.cpp \
    src/core/WinClipboardEmf.cpp \
    src/command/alignment/SetOriginCommand.cpp \
    src/command/alignment/AlignmentAddSpiralCommand.cpp \
    src/command/alignment/Alignment3DAddVProfileCommand.cpp \
    src/command/alignment/AlignmentEditCommand.cpp \
    src/command/alignment/AlignmentFixCurveCommand.cpp \
    src/command/alignment/AlignmentFixTangentCommand.cpp \
    src/command/alignment/AlignmentFloatCurveCommand.cpp \
    src/command/alignment/AlignmentSCSCommand.cpp \
    src/command/alignment/AlignmentSCSChainCommand.cpp \
    src/command/alignment/TrackCenterLineCommands.cpp \
    src/command/alignment/ImportAlignmentCommand.cpp \
    src/command/alignment/ImportAldCommand.cpp \
    src/command/alignment/TrackExtractCommand.cpp \
    src/command/alignment/AlignmentReverseSCSCommand.cpp \
    src/command/alignment/AlignmentQuickTableCommand.cpp \
    src/command/DrawingSheetCommand.cpp \
    src/command/PlanProfileCommand.cpp \
    src/command/DrawingBookActions.cpp \
    src/command/alignment/AlignmentDrawChainCommand.cpp \
    src/command/ScaleCommand.cpp \
    src/command/alignment/ExportAlignmentCommand.cpp \
    src/command/alignment/ProfileArrayCommand.cpp \
    src/command/alignment/VAlignFloatVCurveCommand.cpp \
    src/core/CommandHistory.cpp \
    src/command/EllipseCommand.cpp \
    src/command/InputParser.cpp \
    src/command/PointCommand.cpp \
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
    src/drawing/PlanProfileSettings.cpp \
    src/drawing/PlanProfilePainter.cpp \
    src/drawing/PlanProfileGenerator.cpp \
    src/drawing/PlanProfileDialog.cpp \
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
    src/cad/AlignedProfileArray.cpp \
    src/cad/ProfileLoftSolid.cpp \
    src/cad/TrackSlabPlan.cpp \
    src/cad/ComponentInstall.cpp \
    src/cad/DefinedPlane.cpp \
    src/cad/CantLineUtil.cpp \
    src/ui/ComponentDialogs.cpp \
    src/ui/ExtrudeEditDialog.cpp \
    src/ui/DocumentTabBar.cpp \
    src/ui/DefinePlaneDialog.cpp \
    src/command/DefinePlaneCommand.cpp \
    src/command/ComponentInsertCommand.cpp \
    src/cad/ChamferSolid.cpp \
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
    src/railway/AlignmentNLSolver.cpp \
    src/railway/RailwayAlignment.cpp \
    src/railway/RailwayAlignmentElement.cpp \
    src/railway/AlignmentQuickCalc.cpp \
    src/railway/AldFileIO.cpp \
    src/ui/AutoCompleteModel.cpp \
    src/ui/CommandHistoryPopup.cpp \
    src/ui/CommandInputEdit.cpp \
    src/core/CommandLineManager.cpp \
    src/ui/CommandLineWidget.cpp \
    src/ui/GripEventFilter.cpp \
    src/ui/MainWindow.cpp \
    src/ui/FeatureBrowser.cpp \
    src/ui/AlignmentDataTableDialog.cpp \
    src/ui/AlignmentQuickTableDialog.cpp \
    src/ui/HAlignTableWidget.cpp \
    src/ui/AlignmentDrawElementDialog.cpp \
    src/ui/CompoundChainCalcDialog.cpp \
    src/ui/ProfileArrayStationTableDialog.cpp \
    src/ui/ProfileArraySegmentTableDialog.cpp \
    src/ui/ParameterPanel.cpp \
    src/ui/PropertyPanel.cpp \
    src/ui/KeymapView.cpp \
    src/ui/SketchPanel.cpp \
    src/ui/ToolManager.cpp \
    src/ui/TransientCommandHistory.cpp \
    src/ui/UIManager.cpp \
    src/ui/ResultPopup.cpp \
    src/ui/DimExpressionDialog.cpp \
    src/ui/AddSpiralCalcDialog.cpp \
    src/ui/TransitionCurveHelp.cpp \
    src/ui/TrackNameSequenceDialog.cpp \
    src/ui/SCSCalcDialog.cpp \
    src/ui/AlignmentReverseSCSCalcDialog.cpp \
    \
    # View Module (Felicia - 視圖系統)
    src/ui/VAlignCommandBar.cpp \
    src/ui/VAlignEditorDockWidget.cpp \
    src/ui/VAlignProfileView.cpp \
    src/ui/VAlignTheme.cpp \
    src/view/AlignmentRenderer.cpp \
    src/view/Railway3DAlignmentRenderer.cpp \
    src/view/ViewManager.cpp \
    src/view/CadView.cpp \
    src/view/DimPreviewOverlay.cpp \
    src/view/InputJig.cpp \
    src/view/InputJigOverlay.cpp \
    src/view/RubberBand.cpp \
    #src/view/SelectionManager.cpp \
    src/view/ViewGrid.cpp \
    \
    # Command Module (Kaufen - 命令系統)
    src/command/CommandManager.cpp \
    src/command/Command.cpp \
    src/command/BasicCommands.cpp \
    src/command/LineCommand.cpp \
    src/command/EraseCommand.cpp \
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
    src/cad/sketch/SketchAxisAIS.h \
    src/cad/sketch/SketchConstraint.h \
    src/cad/sketch/SketchAnnotation.h \
    src/cad/sketch/AnnotationTextFormatter.h \
    src/cad/sketch/LeaderNoteAIS.h \
    src/cad/sketch/AnnotationStandardsChecker.h \
    src/cad/sketch/SketchLoopFinder.h \
    src/cad/sketch/SketchPointAIS.h \
    src/cad/sketch/SketchRegion.h \
    src/cad/sketch/SketchGeomTransformUtil.h \
    src/cad/sketch/SketchGeom2DMath.h \
    src/command/ArcCommand.h \
    src/command/CircleCommand.h \
    src/command/CommandAlias.h \
    src/command/ConstraintCommands.h \
    src/command/GeneralDimCommand.h \
    src/command/LeaderNoteCommand.h \
    src/cad/sketch/GeneralDimClassifier.h \
    src/cad/sketch/GeometryRelationshipAnalyzer.h \
    src/command/ExtrudeCommand.h \
    src/command/ChamferCommand.h \
    src/command/ConstructionToggleCommand.h \
    src/command/PasteClipCommand.h \
    src/core/geometry/EmfVectorParser.h \
    src/core/WinClipboardEmf.h \
    src/command/GripMoveCommand.h \
    src/command/SketchEditCommand.h \
    src/command/SketchSelectionPicker.h \
    src/command/CommandRubberBandHelper.h \
    src/command/SketchTransformCommandBase.h \
    src/command/MoveCommand.h \
    src/command/CopyCommand.h \
    src/command/RotateCommand.h \
    src/command/MirrorCommand.h \
    src/command/StretchCommand.h \
    src/command/TrimExtendHelper.h \
    src/command/TrimCommand.h \
    src/command/ExtendCommand.h \
    src/command/FilletCommand.h \
    src/command/OffsetCommand.h \
    src/command/alignment/SetOriginCommand.h \
    src/command/alignment/AlignmentAddSpiralCommand.h \
    src/command/alignment/Alignment3DAddVProfileCommand.h \
    src/command/alignment/AlignmentCommandBase.h \
    src/command/alignment/AlignmentEditCommand.h \
    src/command/alignment/AlignmentFixCurveCommand.h \
    src/command/alignment/AlignmentFixTangentCommand.h \
    src/command/alignment/AlignmentFloatCurveCommand.h \
    src/command/alignment/AlignmentSCSCommand.h \
    src/command/alignment/AlignmentSCSChainCommand.h \
    src/command/alignment/ProfileViewCommand.h \
    src/command/alignment/TrackCenterLineCommands.h \
    src/command/alignment/ImportAlignmentCommand.h \
    src/command/alignment/ImportAldCommand.h \
    src/command/alignment/TrackExtractCommand.h \
    src/command/alignment/AlignmentReverseSCSCommand.h \
    src/command/alignment/AlignmentQuickTableCommand.h \
    src/command/DrawingSheetCommand.h \
    src/command/PlanProfileCommand.h \
    src/command/DrawingBookActions.h \
    src/command/alignment/AlignmentDrawChainCommand.h \
    src/command/ScaleCommand.h \
    src/command/alignment/ExportAlignmentCommand.h \
    src/command/alignment/ProfileArrayCommand.h \
    src/command/alignment/VAlignCheckGradeCommand.h \
    src/command/alignment/VAlignFloatVCurveCommand.h \
    src/command/alignment/VAlignMovePVICommand.h \
    src/command/alignment/VAlignSetKCommand.h \
    src/core/CommandHistory.h \
    src/command/EllipseCommand.h \
    src/command/InputParser.h \
    src/command/PointCommand.h \
    src/command/PolygonCommand.h \
    src/command/PolylineCommand.h \
    src/command/RectCommand.h \
    src/command/SplineCommand.h \
    src/core/Application.h \
    src/core/EventBus.h \
    src/core/FpuGuard.h \
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
    src/cad/AlignedProfileArray.h \
    src/cad/ProfileLoftSolid.h \
    src/cad/TrackSlabPlan.h \
    src/cad/ComponentInstall.h \
    src/cad/DefinedPlane.h \
    src/cad/CantLineUtil.h \
    src/ui/ComponentDialogs.h \
    src/ui/ExtrudeEditDialog.h \
    src/ui/DocumentTabBar.h \
    src/ui/DefinePlaneDialog.h \
    src/command/DefinePlaneCommand.h \
    src/command/ComponentInsertCommand.h \
    src/cad/ChamferSolid.h \
    src/cad/FeatureTree.h \
    src/cad/Plane.h \                # ✅ 添加
    \
    # Geometry Module                # ✅ 添加
    src/core/geometry/CoordinateTransform.h \
    src/core/geometry/ProjectOrigin.h \
    src/core/geometry/WorkPlane.h \
    src/drawing/DrawingSheet.h \
    src/drawing/DrawingSheetManager.h \
    src/drawing/PlanProfileSettings.h \
    src/drawing/PlanProfilePainter.h \
    src/drawing/PlanProfileGenerator.h \
    src/drawing/PlanProfileDialog.h \
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
    src/railway/AlignmentNLSolver.h \
    src/railway/RailwayAlignment.h \
    src/railway/RailwayAlignmentElement.h \
    src/railway/AlignmentQuickCalc.h \
    src/railway/AldFileIO.h \
    src/ui/AutoCompleteModel.h \
    src/ui/CommandHistoryPopup.h \
    src/ui/CommandInputEdit.h \
    src/core/CommandLineManager.h \
    src/ui/CommandLineWidget.h \
    src/ui/GripEventFilter.h \
    src/ui/MainWindow.h \
    src/ui/FeatureBrowser.h \
    src/ui/FeatureTreeItem.h \
    src/ui/AlignmentDataTableDialog.h \
    src/ui/AlignmentQuickTableDialog.h \
    src/ui/HAlignTableWidget.h \
    src/ui/AlignmentDrawElementDialog.h \
    src/ui/CompoundChainCalcDialog.h \
    src/ui/ProfileArrayStationTableDialog.h \
    src/ui/ProfileArraySegmentTableDialog.h \
    src/ui/ParameterPanel.h \
    src/ui/PropertyPanel.h \
    src/ui/KeymapView.h \
    src/ui/SketchPanel.h \
    src/ui/ToolManager.h \
    src/ui/TransientCommandHistory.h \
    src/ui/UIManager.h \
    src/ui/ResultPopup.h \
    src/ui/DimExpressionDialog.h \
    src/ui/AddSpiralCalcDialog.h \
    src/ui/TransitionCurveHelp.h \
    src/ui/TrackNameSequenceDialog.h \
    src/ui/SCSCalcDialog.h \
    src/ui/AlignmentReverseSCSCalcDialog.h \
    \
    # View Module (Felicia)
    src/ui/VAlignCommandBar.h \
    src/ui/VAlignEditorDockWidget.h \
    src/ui/VAlignProfileView.h \
    src/ui/VAlignTheme.h \
    src/view/AlignmentRenderer.h \
    src/view/Railway3DAlignmentRenderer.h \
    src/view/ViewManager.h \
    src/view/CadView.h \
    src/view/DimPreviewOverlay.h \
    src/view/InputJig.h \
    src/view/InputJigOverlay.h \
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
    src/command/EraseCommand.h \
    \
    # Scripting Module (Daney)
    src/scripting/LispEngine.h \
    src/scripting/LispBindings.h \
    src/scripting/ScriptManager.h

RESOURCES += \
    resources.qrc

# 設定 Include 路徑
INCLUDEPATH += $$PWD/src

# ── 選用模組：QtSvg（DrawingSheet 的 SVG 匯出）──────────────────────────────
# 沒安裝時（Debian/Ubuntu：sudo apt install libqt5svg5-dev）仍可編譯，
# 只是 SVG 匯出會回報「未啟用」；PDF 匯出不受影響。
qtHaveModule(svg) {
    QT += svg
    DEFINES += AICAD_HAVE_QTSVG
}
