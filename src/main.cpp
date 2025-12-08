// Include ECL headers FIRST, before any Qt headers
#include <ecl/ecl.h>

// Undefine slots macro to avoid conflict with ECL
#ifdef slots
#undef slots
#endif

// Now include application headers (which contain Qt headers)
#include <QApplication>
#include <QSurfaceFormat>
#include <QDebug>
#include <QMessageBox>

#include "MainWindow.h"
#include "core/Application.h"

int main(int argc, char **argv) {
#ifdef _WIN32
    QApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
    QSurfaceFormat fmt;
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    QSurfaceFormat::setDefaultFormat(fmt);
#else
    // Force Qt to use XCB before QApplication is created
    const char* session = std::getenv("XDG_SESSION_TYPE");

    if (session && std::strcmp(session, "wayland") == 0) {
        qDebug("Detected Wayland session → forcing xcb");
        qputenv("QT_QPA_PLATFORM", QByteArray("xcb"));
    }
#endif

    // 建立 Qt 應用程式
    QApplication qapp(argc, argv);
    
    // 設定應用程式資訊
    qapp.setOrganizationName("AICAD Team");
    qapp.setApplicationName("AICAD");
    qapp.setApplicationVersion("1.0.0-dev");
    
    qDebug() << "===========================================";
    qDebug() << "  AICAD - Advanced Interactive CAD System";
    qDebug() << "  Version:" << qapp.applicationVersion();
    qDebug() << "===========================================";
    
    // 初始化 AICAD 核心系統
    aicad::core::Application* app = aicad::core::Application::instance();
    
    if (!app->initialize()) {
        QMessageBox::critical(nullptr, "Initialization Failed",
                            "Failed to initialize AICAD core system.\n"
                            "Please check the console for error messages.");
        return 1;
    }
    
    qDebug() << "AICAD Core initialized successfully";
    qDebug() << "Application Name:" << app->applicationName();
    qDebug() << "Version:" << app->version();
    
    // 建立主視窗
    MainWindow mainWindow;
    
    // Pass command line args to window (if needed)
    // if (argc > 1) {
    //     mainWindow.loadFileFromCommandLine(QString::fromUtf8(argv[1]));
    // }
    
    mainWindow.show();
    // mainWindow.showMaximized();
    
    qDebug() << "Main window shown";
    qDebug() << "Entering event loop...";
    
    // 執行事件循環
    int result = qapp.exec();
    
    qDebug() << "Event loop exited with code:" << result;
    qDebug() << "Shutting down AICAD...";
    
    // 關閉應用程式
    app->shutdown();
    
    qDebug() << "AICAD shutdown completed";
    qDebug() << "===========================================";
    
    return result;
}