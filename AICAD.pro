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

    # If ECL is in a custom location:
    # INCLUDEPATH += /path/to/ecl/include
    # LIBS += -L/path/to/ecl/lib -lecl
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

SOURCES += \
    src/CadView.cpp \
    src/OcafDocument.cpp \
    src/main.cpp \
    src/MainWindow.cpp

HEADERS += \
    src/CadView.h \
    src/MainWindow.h \
    src/OcafDocument.h

RESOURCES += \
    resources.qrc

