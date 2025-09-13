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
    m_actDrawArc  = new QAction(tr("Draw Arc"), this);

    // file ops
    m_actSave = new QAction(tr("Save"), this);
    m_actLoad = new QAction(tr("Load"), this);

    // TODO: connect m_actDrawLine, m_actDrawArc, m_actSave, m_actLoad
    // to CadView2D slots or appropriate handlers if implemented
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

    // default: 2D checked
    m_act2D->setChecked(true);
}

void MainWindow::createCentral() {
    m_stack = new QStackedWidget(this);
    m_view2d = new CadView2D(this);
    m_view3d = new CadView3D(this);
    m_stack->addWidget(m_view2d);
    m_stack->addWidget(m_view3d);
    setCentralWidget(m_stack);

    m_stack->setCurrentWidget(m_view2d);
}

// --- slots ---
void MainWindow::toggle2D() {
    m_stack->setCurrentWidget(m_view2d);
    m_act2D->setChecked(true);
    m_act3D->setChecked(false);
}

void MainWindow::toggle3D() {
    m_stack->setCurrentWidget(m_view3d);
    m_act2D->setChecked(false);
    m_act3D->setChecked(true);
}
