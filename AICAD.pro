# AICAD.pro - cross-platform Qt CAD project

# Build both Debug and Release

CONFIG   += moc   # ensures Meta-Object Compiler runs automatically
CONFIG   += debug_and_release  # optional, for both build types

# ---------- Unix / Linux (Qt5 with GCC) ----------
unix {
    QT += widgets opengl   # in Qt5 we use 'opengl'
    CONFIG += c++17
    # GCC specific options
    QMAKE_CXXFLAGS += -Wall -Wextra
}

# ---------- Windows (Qt6 with MSVC) ----------
win32 {
    QT += widgets openglwidgets   # in Qt6 QOpenGLWidget needs this
    CONFIG += c++17
    LIBS += -lopengl32            # link system OpenGL
    DEFINES += _USE_MATH_DEFINES  # for <cmath> constants with MSVC
}


SOURCES += \
    src/main.cpp \
    src/MainWindow.cpp \
    src/CadView2D.cpp \
    src/CadView3D.cpp \
    src/TrackballCamera.cpp

HEADERS += \
    src/MainWindow.h \
    src/CadView2D.h \
    src/CadView3D.h \
    src/Entities.h \
    src/TrackballCamera.h
