/**
 * @file ui/MainWindow.cpp
 * @brief MainWindow 類別實作
 */

#include "MainWindow.h"
#include "ParameterPanel.h"
#include "core/Application.h"
#include "core/DocumentManager.h"
#include "core/EventBus.h"

#include <QWidget>
#include <QVBoxLayout>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QStatusBar>
#include <QCloseEvent>
#include <QMessageBox>
#include <QLabel>
#include <QShortcut>
#include <QKeySequence>
#include <QDebug>

namespace aicad {
namespace ui {

// ─────────────────────────────────────────────────────────────────────────────
// Private data
// ─────────────────────────────────────────────────────────────────────────────

class MainWindow::Private {
public:
    QWidget*        centralWidget  = nullptr;
    ParameterPanel* parameterPanel = nullptr;  // Phase 7
    QAction*        paramPanelAct  = nullptr;  // View 選單裡的切換 Action
};

// ─────────────────────────────────────────────────────────────────────────────
// 建構 / 析構
// ─────────────────────────────────────────────────────────────────────────────

MainWindow::MainWindow()
    : QMainWindow(nullptr)
    , d(new Private())
{
    qDebug() << "[MainWindow] Created";

    setWindowTitle("AICAD - Artificial Intelligence CAD System");
    resize(1280, 800);

    // EventBus 狀態列訂閱
    core::Application* app = core::Application::instance();
    core::EventBus*    bus = app->eventBus();

    if (bus) {
        bus->subscribe(core::Events::DOCUMENT_CREATED, this,
            [this](const QVariant& data) {
                statusBar()->showMessage(
                    QString("Document created: %1").arg(data.toString()), 3000);
            });
        bus->subscribe(core::Events::DOCUMENT_CLOSED, this,
            [this](const QVariant& data) {
                statusBar()->showMessage(
                    QString("Document closed: %1").arg(data.toString()), 3000);
            });
    }
}

MainWindow::~MainWindow() {
    qDebug() << "[MainWindow] Destroyed";
    delete d;
}

// ─────────────────────────────────────────────────────────────────────────────
// Phase 7 — ParameterPanel dock
// ─────────────────────────────────────────────────────────────────────────────

ParameterPanel* MainWindow::parameterPanel() {
    if (!d->parameterPanel)
        setupParameterPanelDock();
    return d->parameterPanel;
}

void MainWindow::setupParameterPanelDock() {
    if (d->parameterPanel) return;   // 只建一次

    d->parameterPanel = new ParameterPanel(this);
    d->parameterPanel->setObjectName("ParameterPanelDock");
    addDockWidget(Qt::RightDockWidgetArea, d->parameterPanel);
    d->parameterPanel->hide();   // 初始隱藏

    // ── Ctrl+P：切換顯示/隱藏 ─────────────────────────────────────────
    auto* sc = new QShortcut(QKeySequence("Ctrl+P"), this);
    connect(sc, &QShortcut::activated, this, [this] {
        if (!d->parameterPanel) return;
        if (d->parameterPanel->isVisible()) {
            d->parameterPanel->hide();
        } else {
            d->parameterPanel->show();
            d->parameterPanel->raise();
        }
    });

    // ── View 選單「參數面板」切換 Action ──────────────────────────────
    if (QMenuBar* mb = menuBar()) {
        QMenu* viewMenu = nullptr;
        for (QAction* a : mb->actions()) {
            if (a->menu() && a->text().contains("View", Qt::CaseInsensitive)) {
                viewMenu = a->menu();
                break;
            }
        }
        if (viewMenu) {
            viewMenu->addSeparator();
            d->paramPanelAct = viewMenu->addAction(tr("&Parameter Panel"));
            d->paramPanelAct->setShortcut(QKeySequence("Ctrl+P"));
            d->paramPanelAct->setCheckable(true);
            d->paramPanelAct->setChecked(false);

            connect(d->paramPanelAct, &QAction::triggered, this,
                    [this](bool checked) {
                if (checked) {
                    d->parameterPanel->show();
                    d->parameterPanel->raise();
                } else {
                    d->parameterPanel->hide();
                }
            });

            // 面板關閉時同步 Action check 狀態
            connect(d->parameterPanel, &QDockWidget::visibilityChanged,
                    d->paramPanelAct, &QAction::setChecked);
        }
    }

    qDebug() << "[MainWindow] ParameterPanel dock created";
}

// ─────────────────────────────────────────────────────────────────────────────
// setupUI
// ─────────────────────────────────────────────────────────────────────────────

void MainWindow::setupUI() {
    QMenuBar* mb = new QMenuBar(this);
    setMenuBar(mb);

    // File
    QMenu* fileMenu = mb->addMenu("&File");
    fileMenu->addAction("&New")->setShortcut(QKeySequence::New);
    fileMenu->addAction("&Open")->setShortcut(QKeySequence::Open);
    fileMenu->addAction("&Save")->setShortcut(QKeySequence::Save);
    fileMenu->addSeparator();
    QAction* exitAct = fileMenu->addAction("E&xit");
    exitAct->setShortcut(QKeySequence::Quit);
    connect(exitAct, &QAction::triggered, this, &MainWindow::close);

    // View（setupParameterPanelDock 會在裡面加 Parameter Panel 項目）
    QMenu* viewMenu = mb->addMenu("&View");
    viewMenu->addAction("&Feature Browser");
    viewMenu->addAction("&Properties");

    // Help
    QMenu* helpMenu = mb->addMenu("&Help");
    helpMenu->addAction("&About");

    QStatusBar* sb = new QStatusBar(this);
    setStatusBar(sb);
    sb->showMessage("Ready");

    setupCentralWidget();

    // Phase 7 dock（在 setupUI 末尾建立，確保 menuBar 已存在）
    setupParameterPanelDock();
}

void MainWindow::setupCentralWidget() {
    // 由 UIManager 負責填入 CadView 等中央元件
}

// ─────────────────────────────────────────────────────────────────────────────
// closeEvent
// ─────────────────────────────────────────────────────────────────────────────

void MainWindow::closeEvent(QCloseEvent* event) {
    qDebug() << "[MainWindow] Close event";
    Q_EMIT aboutToClose();

    core::Application*    app    = core::Application::instance();
    core::DocumentManager* docMgr = app->documentManager();

    if (docMgr && docMgr->hasUnsavedChanges()) {
        auto reply = QMessageBox::question(
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
