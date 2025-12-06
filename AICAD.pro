# AICAD.pro

CONFIG += c++17
QT += core gui widgets opengl printsupport

TARGET = AICAD
TEMPLATE = app

# 定義版本資訊
DEFINES += AICAD_VERSION=\\\"1.0.0\\\"

# 包含路徑
INCLUDEPATH += src

# 核心模組
HEADERS += \
    src/core/Application.h \
    src/core/EventBus.h \
    src/core/PluginManager.h

SOURCES += \
    src/core/Application.cpp \
    src/core/EventBus.cpp \
    src/core/PluginManager.cpp

# CAD 引擎 (Ben 的工作)
HEADERS += \
    src/cad/DocumentManager.h \
    src/cad/Document.h \
    src/cad/features/Feature.h \
    src/cad/features/Sketch.h \
    src/cad/features/Extrude.h \
    src/cad/geometry/CustomPlane.h

SOURCES += \
    src/cad/DocumentManager.cpp \
    src/cad/Document.cpp \
    src/cad/features/Feature.cpp \
    src/cad/features/Sketch.cpp \
    src/cad/features/Extrude.cpp \
    src/cad/geometry/CustomPlane.cpp

# 命令系統 (Kaufen 的工作)
HEADERS += \
    src/commands/CommandManager.h \
    src/commands/Command.h \
    src/commands/SketchCommands.h

SOURCES += \
    src/commands/CommandManager.cpp \
    src/commands/Command.cpp \
    src/commands/SketchCommands.cpp

# 視圖系統 (Felicia 的工作)
HEADERS += \
    src/view/ViewManager.h \
    src/view/CadView.h \
    src/view/RubberBand.h

SOURCES += \
    src/view/ViewManager.cpp \
    src/view/CadView.cpp \
    src/view/RubberBand.cpp

# UI 系統 (James 的工作)
HEADERS += \
    src/ui/MainWindow.h \
    src/ui/FeatureBrowser.h

SOURCES += \
    src/ui/MainWindow.cpp \
    src/ui/FeatureBrowser.cpp

# 腳本系統 (Daney 的工作)
HEADERS += \
    src/scripting/LispEngine.h \
    src/scripting/LispBindings.h

SOURCES += \
    src/scripting/LispEngine.cpp \
    src/scripting/LispBindings.cpp

# 主程式
SOURCES += src/main.cpp

# 平台特定設定
unix {
    # Linux 設定
    OCCT_DIR = /usr/local/occt
    INCLUDEPATH += $$OCCT_DIR/include/opencascade
    LIBS += -L$$OCCT_DIR/lib -Wl,-rpath,$$OCCT_DIR/lib
    
    # OCCT 函式庫
    LIBS += -lTKernel -lTKMath -lTKG2d -lTKG3d -lTKGeomBase \
            -lTKBRep -lTKTopAlgo -lTKPrim -lTKV3d -lTKOpenGl \
            -lTKService -lTKLCAF -lTKCAF -lTKCDF -lTKVCAF
    
    # ECL
    INCLUDEPATH += /usr/include/ecl
    LIBS += -lecl -lgmp -lmpfr
    
    # X11
    LIBS += -lX11 -lXext
}

win32 {
    # Windows 設定
    OCC_INC = D:/Git/OCCT/OCCT-install/inc
    OCC_LIB = D:/Git/OCCT/OCCT-install/win64/vc14/lib
    
    INCLUDEPATH += $$OCC_INC
    LIBS += -L$$OCC_LIB
    
    # OCCT 函式庫
    LIBS += -lTKernel -lTKMath -lTKG2d -lTKG3d -lTKGeomBase \
            -lTKBRep -lTKTopAlgo -lTKPrim -lTKV3d -lTKOpenGl \
            -lTKService -lTKLCAF -lTKCAF -lTKCDF -lTKVCAF
    
    # ECL
    INCLUDEPATH += D:/Git/ecl/v24.5.10/vc143-x64
    LIBS += -LD:/Git/ecl/v24.5.10/vc143-x64 -lecl
    
    LIBS += -lopengl32
    DEFINES += _USE_MATH_DEFINES
}

# 測試
test {
    QT += testlib
    TARGET = test_aicad
    
    SOURCES -= src/main.cpp
    SOURCES += tests/unit/core/test_application.cpp
}
