#include "MainWindow.h"
#include <QToolBar>
#include <QAction>
#include <QFileDialog>
#include <QMessageBox>
#include <QVBoxLayout>

// ctor
MainWindow::MainWindow() {
    createActions();
    createToolbar();
    createCentral();
    setWindowTitle("Qt CAD Viewer");
    resize(800, 600);
}

// --- private setup helpers ---
void MainWindow::createActions() {
    // view toggle
    m_act2D = new QAction(tr("2D View"), this);
    m_act2D->setCheckable(true);
    connect(m_act2D, &QAction::triggered, this, &MainWindow::toggle2D);

    m_act3D = new QAction(tr("3D View"), this);
    m_act3D->setCheckable(true);
    connect(m_act3D, &QAction::triggered, this, &MainWindow::toggle3D);

    // drawing
    m_actDrawLine = new QAction(tr("Draw Line"), this);
    connect(m_actDrawLine, &QAction::triggered, [this]() {
        toggle2D();  // ensure 2D mode
        m_view->setMode(CadView::DrawLine);
    });

    m_actDrawArc  = new QAction(tr("Draw Arc"), this);
    connect(m_actDrawArc, &QAction::triggered, [this]() {
        toggle2D();  // ensure 2D mode
        m_view->setMode(CadView::DrawArc);
    });

    // printing/export
    m_actPrint = new QAction(tr("Print"), this);
    connect(m_actPrint, &QAction::triggered, [this]() {
        m_view->printView();
    });

    m_actExportPdf = new QAction(tr("Export PDF"), this);
    connect(m_actExportPdf, &QAction::triggered, [this]() {
        QString file = QFileDialog::getSaveFileName(
            this, tr("Export PDF"), QString(), tr("PDF Files (*.pdf)"));
        if (!file.isEmpty())
            m_view->exportPdf(file);
    });

    // file ops
    m_actSave = new QAction(tr("Save"), this);
    connect(m_actSave, &QAction::triggered, [this]() {
        QString fileName = QFileDialog::getSaveFileName(
            this, tr("Save CAD File"), QString(),
            tr("CAD Files (*.txt *.json);;All Files (*)"));
        if (!fileName.isEmpty()) {
            m_view->saveEntities(fileName);
        }
    });

    m_actLoad = new QAction(tr("Load"), this);
    connect(m_actLoad, &QAction::triggered, [this]() {
        QString fileName = QFileDialog::getOpenFileName(
            this, tr("Open CAD File"), QString(),
            tr("CAD Files (*.txt *.json);;All Files (*)"));
        if (!fileName.isEmpty()) {
            m_view->loadEntities(fileName);
        }
    });
}

void MainWindow::createToolbar() {
    auto *tb = addToolBar(tr("Main"));
    tb->addAction(m_act2D);
    tb->addAction(m_act3D);
    tb->addSeparator();
    tb->addAction(m_actDrawLine);
    tb->addAction(m_actDrawArc);
    tb->addSeparator();
    tb->addAction(m_actSave);
    tb->addAction(m_actLoad);
    tb->addSeparator();
    tb->addAction(m_actPrint);
    tb->addAction(m_actExportPdf);

    // default: 2D checked
    m_act2D->setChecked(true);
}

void MainWindow::createCentral() {
    m_view = new CadView(this);
    setCentralWidget(m_view);
}

// --- slots ---
void MainWindow::toggle2D() {
    m_view->setViewMode(CadView::Mode2D);
    m_act2D->setChecked(true);
    m_act3D->setChecked(false);
}

void MainWindow::toggle3D() {
    m_view->setViewMode(CadView::Mode3D);
    m_act2D->setChecked(false);
    m_act3D->setChecked(true);
}
