/**
 * @file ui/MainWindow.cpp
 * @brief MainWindow 類別實作
 * @author James
 * @date 2025-01-07
 */

#include "MainWindow.h"
#include "core/Application.h"
#include "core/EventBus.h"

#include <QWidget>
#include <QVBoxLayout>
#include <QMenuBar>
#include <QMenu>
#include <QStatusBar>
#include <QCloseEvent>
#include <QMessageBox>
#include <QDebug>

namespace aicad {
namespace ui {

class MainWindow::Private {
public:
    Private()
        : centralWidget(nullptr)
    {
    }
    
    QWidget* centralWidget;
};

MainWindow::MainWindow()
    : QMainWindow(nullptr)
    , d(new Private())
{
    qDebug() << "[MainWindow] Created";
    
    setupUI();
    
    // 設定視窗屬性
    setWindowTitle("AICAD - Advanced Interactive CAD System");
    resize(1280, 800);
    
    // 訂閱事件
    core::Application* app = core::Application::instance();
    core::EventBus* bus = app->eventBus();
    
    if (bus) {
        bus->subscribe(core::Events::DOCUMENT_CREATED, this,
            [this](const QVariant& data) {
                QString name = data.toString();
                statusBar()->showMessage(
                    QString("Document created: %1").arg(name), 3000);
            });
        
        bus->subscribe(core::Events::DOCUMENT_CLOSED, this,
            [this](const QVariant& data) {
                QString name = data.toString();
                statusBar()->showMessage(
                    QString("Document closed: %1").arg(name), 3000);
            });
    }
}

MainWindow::~MainWindow() {
    qDebug() << "[MainWindow] Destroyed";
    delete d;
}

void MainWindow::setupUI() {
    // 建立選單列
    QMenuBar* menuBar = new QMenuBar(this);
    setMenuBar(menuBar);
    
    // 檔案選單
    QMenu* fileMenu = menuBar->addMenu("&File");
    
    QAction* newAction = fileMenu->addAction("&New");
    newAction->setShortcut(QKeySequence::New);
    
    QAction* openAction = fileMenu->addAction("&Open");
    openAction->setShortcut(QKeySequence::Open);
    
    QAction* saveAction = fileMenu->addAction("&Save");
    saveAction->setShortcut(QKeySequence::Save);
    
    fileMenu->addSeparator();
    
    QAction* exitAction = fileMenu->addAction("E&xit");
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &MainWindow::close);
    
    // 檢視選單
    QMenu* viewMenu = menuBar->addMenu("&View");
    viewMenu->addAction("&Feature Browser");
    viewMenu->addAction("&Properties");
    
    // 說明選單
    QMenu* helpMenu = menuBar->addMenu("&Help");
    helpMenu->addAction("&About");
    
    // 建立狀態列
    QStatusBar* statusBar = new QStatusBar(this);
    setStatusBar(statusBar);
    statusBar->showMessage("Ready");
    
    // 建立中央視窗區域
    setupCentralWidget();
}

void MainWindow::setupCentralWidget() {
    d->centralWidget = new QWidget(this);
    
    QVBoxLayout* layout = new QVBoxLayout(d->centralWidget);
    
    // 暫時顯示歡迎訊息
    QLabel* welcomeLabel = new QLabel(
        "<h1>Welcome to AICAD</h1>"
        "<p>Advanced Interactive CAD System</p>"
        "<p>Version 1.0.0-dev</p>",
        d->centralWidget);
    welcomeLabel->setAlignment(Qt::AlignCenter);
    
    layout->addWidget(welcomeLabel);
    
    setCentralWidget(d->centralWidget);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    qDebug() << "[MainWindow] Close event";
    
    // 發出即將關閉信號
    Q_EMIT aboutToClose();
    
    // 檢查是否有未儲存的變更
    core::Application* app = core::Application::instance();
    core::DocumentManager* docMgr = app->documentManager();
    
    if (docMgr && docMgr->hasUnsavedChanges()) {
        QMessageBox::StandardButton reply = QMessageBox::question(
            this,
            "Unsaved Changes",
            "There are unsaved changes. Do you want to quit anyway?",
            QMessageBox::Yes | QMessageBox::No);
        
        if (reply == QMessageBox::No) {
            event->ignore();
            return;
        }
    }
    
    event->accept();
}

} // namespace ui
} // namespace aicad