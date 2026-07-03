// 注意：以前這裡曾經手動 "#include <ecl/ecl.h>" 並在其後 "#undef slots"，
// 靠著「一定要在 Qt 標頭之前 include ECL」這個順序來避免 Qt 的 slots 巨集
// 把 ECL <ecl/object.h> 內的 "cl_object *slots;" 成員展開成 "cl_object *;"
// 而編譯失敗。這個順序約定很脆弱（任何人調整 include 順序就會壞掉），
// 已改在 scripting/LispEngine.h 內用 push_macro/undef/pop_macro 妥善處理，
// 因此這裡不再需要特別把 ECL 標頭排在 Qt 標頭之前。
#include <QApplication>
#include <QSurfaceFormat>
#include <QDebug>
#include <QMessageBox>

#include "core/Application.h"
#include "ui/UIManager.h"         // ✅ Add this include
#include "scripting/LispEngine.h"
#include "scripting/LispBindings.h"

int main(int argc, char **argv) {
#ifdef _WIN32
    QApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
    QSurfaceFormat fmt;
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    QSurfaceFormat::setDefaultFormat(fmt);
#elif defined(__APPLE__)
    // macOS：Qt 使用原生 Cocoa，無須設定 QPA platform
    // Retina 支援由 Qt 自動處理
#else
    // Linux：偵測 Wayland 並強制回退到 XCB
    const char* session = std::getenv("XDG_SESSION_TYPE");
    if (session && strcmp(session, "wayland") == 0) {
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
    qDebug() << "  AICAD - Artificial Intelligence CAD System";
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

    // 顯示主視窗
    aicad::ui::UIManager* uiMgr = app->uiManager();
    if (uiMgr) {
        //uiMgr->showMainWindow();
        uiMgr->showMainWindow();
    }
    
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
