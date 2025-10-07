# AICAD.pro - cross-platform Qt CAD project with ECL Lisp integration

CONFIG   += moc
CONFIG   += debug_and_release

# ---------- Unix / Linux (Qt5 with GCC) ----------
unix {
    QT += widgets opengl printsupport
    CONFIG += c++17

    # GCC specific options
    QMAKE_CXXFLAGS += -Wall -Wextra

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
    debug {
    QMAKE_POST_LINK += $$QMAKE_COPY $$shell_path($$PWD/menu.txt) $$shell_path($$OUT_PWD/debug)}

    release {
    QMAKE_POST_LINK += $$QMAKE_COPY $$shell_path($$PWD/menu.txt) $$shell_path($$OUT_PWD/release)}

}

SOURCES += \
    src/CadView.cpp \
    src/main.cpp \
    src/MainWindow.cpp

HEADERS += \
    src/CadView.h \
    src/MainWindow.h

RESOURCES += \
    resources.qrc

