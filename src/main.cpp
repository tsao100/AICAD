// main.cpp 範例

#include <QApplication>
#include "core/Application.h"
#include "ui/MainWindow.h"
#include <QDebug>

int main(int argc, char** argv) {
    // 1. 建立 Qt 應用程式
    QApplication qtApp(argc, argv);
    // 設定應用程式資訊
    QApplication::setOrganizationName("AICAD");
    QApplication::setOrganizationDomain("aicad.org");
    QApplication::setApplicationName("AICAD");
    QApplication::setApplicationVersion(aicad::core::Application::version());

    // 2. 初始化 AICAD 應用程式
    aicad::core::Application* app = aicad::core::Application::instance();

    if (!app->initialize()) {
        qCritical() << "Failed to initialize AICAD";
        return 1;
    }

    // 3. 建立主視窗
    aicad::ui::MainWindow mainWindow;
    mainWindow.show();

    // 4. 執行應用程式
    int result = qtApp.exec();

    // 5. 清理
    app->shutdown();

    return result;
}