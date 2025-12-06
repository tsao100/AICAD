// tests/manual/test_view_visual.cpp

#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QPushButton>
#include "core/Application.h"
#include "view/ViewManager.h"
#include "view/CadView.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    
    // 初始化 AICAD
    aicad::core::Application* aicadApp = aicad::core::Application::instance();
    aicadApp->initialize();
    
    // 建立測試視窗
    QWidget window;
    window.setWindowTitle("View System Test");
    window.resize(1024, 768);
    
    QVBoxLayout* layout = new QVBoxLayout(&window);
    
    // 建立視圖
    aicad::view::ViewManager* viewMgr = aicadApp->viewManager();
    aicad::view::CadView* view = viewMgr->createView(&window);
    layout->addWidget(view);
    
    // 建立控制按鈕
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    
    QPushButton* btnTop = new QPushButton("Top");
    QPushButton* btnFront = new QPushButton("Front");
    QPushButton* btnIso = new QPushButton("Isometric");
    QPushButton* btnFit = new QPushButton("Fit All");
    
    buttonLayout->addWidget(btnTop);
    buttonLayout->addWidget(btnFront);
    buttonLayout->addWidget(btnIso);
    buttonLayout->addWidget(btnFit);
    
    layout->addLayout(buttonLayout);
    
    // 連接按鈕
    QObject::connect(btnTop, &QPushButton::clicked, [view]() {
        view->setOrientation(aicad::view::ViewOrientation::Top);
    });
    
    QObject::connect(btnFront, &QPushButton::clicked, [view]() {
        view->setOrientation(aicad::view::ViewOrientation::Front);
    });
    
    QObject::connect(btnIso, &QPushButton::clicked, [view]() {
        view->setOrientation(aicad::view::ViewOrientation::Isometric);
    });
    
    QObject::connect(btnFit, &QPushButton::clicked, [view]() {
        view->fitAll();
    });
    
    window.show();
    
    int result = app.exec();
    
    aicadApp->shutdown();
    
    return result;
}
