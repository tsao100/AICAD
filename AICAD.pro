# ---------- Unix / Linux (Qt5 with GCC) ----------
unix {
    QT += widgets opengl   # in Qt5 we use 'opengl'
    CONFIG += c++11
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

CONFIG   += debug_and_release  # optional, for both build types

SOURCES += AICAD.cpp

