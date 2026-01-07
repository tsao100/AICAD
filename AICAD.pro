# AICAD.pro - cross-platform Qt CAD project with ECL Lisp integration

CONFIG   += moc
CONFIG   += debug_and_release

# ---------- Unix / Linux (Qt5 with GCC) ----------
unix {
    QT += widgets opengl printsupport
    CONFIG += c++17

    # GCC specific options
    QMAKE_CXXFLAGS += -Wall -Wextra
    # --- OCCT 8.0.0 paths ---
    OCCT_DIR = /usr/local/occt

    INCLUDEPATH += $$OCCT_DIR/include/opencascade
    LIBS += -L$$OCCT_DIR/lib -Wl,-rpath,$$OCCT_DIR/lib

    # Essential OCCT libraries
    LIBS += -lTKernel \
            -lTKMath \
            -lTKG2d \
            -lTKG3d \
            -lTKGeomBase \
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
            -lTKBinXCAF

    # Link X11 (required for OpenGL context)
    LIBS += -lX11 -lXext

    DEFINES += __linux__
    QMAKE_CXXFLAGS += -std=c++11

    # ECL headers (adjust path if needed)
    INCLUDEPATH += /usr/include/ecl

    # ECL libraries
    LIBS += -lecl -lgmp -lmpfr

    # Copy menu.txt to build directory
    copydata.commands = $(COPY_FILE) $$PWD/menu.txt $$OUT_PWD
    first.depends = $(first) copydata
    export(first.depends)
    export(copydata.commands)
    QMAKE_EXTRA_TARGETS += first copydata
}

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
            -lTKBinXCAF

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

    # 每次編譯完自動複製 menu.txt 到輸出資料夾
    QMAKE_POST_LINK += $$QMAKE_COPY $$shell_path($$PWD/menu.txt) $$shell_path($$OUT_PWD)
}

# ========================================
# 源文件和頭文件
# ========================================

SOURCES += \
    src/main.cpp \
    \
    # Core Module (核心系統)
    src/core/Application.cpp \
    src/core/EventBus.cpp \
    src/core/DocumentManager.cpp \
    src/core/PluginManager.cpp \
    src/core/Settings.cpp \
    \
    # CAD Module (Ben - CAD引擎)
    src/cad/Document.cpp \
    src/cad/Feature.cpp \
    src/cad/Sketch.cpp \
    #src/cad/SketchEntity.cpp \
    src/cad/Extrude.cpp \
    #src/cad/FeatureTree.cpp \
    \
    # UI Module (James - 用戶界面)
    src/ui/MainWindow.cpp \
    src/ui/FeatureBrowser.cpp \
    src/ui/PropertyPanel.cpp \
    src/ui/ToolManager.cpp \
    src/ui/UIManager.cpp \
    \
    # View Module (Felicia - 視圖系統)
    src/view/ViewManager.cpp \
    src/view/CadView.cpp \
    src/view/RubberBand.cpp \
    #src/view/SelectionManager.cpp \
    src/view/ViewGrid.cpp \
    \
    # Command Module (Kaufen - 命令系統)
    src/command/CommandManager.cpp \
    src/command/Command.cpp \
    src/command/RectangleCommand.cpp \
    #src/command/LineCommand.cpp \
    #src/command/CircleCommand.cpp \
    \
    # Scripting Module (Daney - 腳本系統)
    src/scripting/LispEngine.cpp \
    src/scripting/LispBindings.cpp \
    #src/scripting/ScriptManager.cpp \
    \
    # Legacy files (逐步遷移中)
    src/OcafDocument.cpp

HEADERS += \
    # Core Module
    src/core/Application.h \
    src/core/EventBus.h \
    src/core/DocumentManager.h \
    src/core/PluginManager.h \
    src/core/Settings.h \
    \
    # CAD Module (Ben)
    src/cad/Document.h \
    src/cad/Feature.h \
    src/cad/Sketch.h \
    #src/cad/SketchEntity.h \
    src/cad/Extrude.h \
    #src/cad/FeatureTree.h \
    \
    # UI Module (James)
    src/ui/MainWindow.h \
    src/ui/FeatureBrowser.h \
    src/ui/PropertyPanel.h \
    src/ui/ToolManager.h \
    src/ui/UIManager.h \
    \
    # View Module (Felicia)
    src/view/ViewManager.h \
    src/view/CadView.h \
    src/view/RubberBand.h \
    #src/view/SelectionManager.h \
    src/view/ViewGrid.h \
    \
    # Command Module (Kaufen)
    src/command/CommandManager.h \
    src/command/Command.h \
    src/command/RectangleCommand.h \
    #src/command/LineCommand.h \
    #src/command/CircleCommand.h \
    \
    # Scripting Module (Daney)
    src/scripting/LispEngine.h \
    src/scripting/LispBindings.h \
    #src/scripting/ScriptManager.h \
    \
    # Legacy files
    src/OcafDocument.h

RESOURCES += \
    resources.qrc

# 設定 Include 路徑
INCLUDEPATH += $$PWD/src
