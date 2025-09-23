#include "MainWindow.h"
#include <QToolBar>
#include <QAction>
#include <QFileDialog>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QInputDialog>

// ctor
MainWindow::MainWindow() {
    createActions();
    createToolbar();
    createCentral();
    createFeatureBrowser(); // Dockable feature tree
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
    //    m_view->setMode(CadView::DrawLine);
    });

    m_actDrawArc  = new QAction(tr("Draw Arc"), this);
    connect(m_actDrawArc, &QAction::triggered, [this]() {
        toggle2D();  // ensure 2D mode
    //    m_view->setMode(CadView::DrawArc);
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
    //        m_view->saveEntities(fileName);
        }
    });

    m_actLoad = new QAction(tr("Load"), this);
    connect(m_actLoad, &QAction::triggered, [this]() {
        QString fileName = QFileDialog::getOpenFileName(
            this, tr("Open CAD File"), QString(),
            tr("CAD Files (*.txt *.json);;All Files (*)"));
        if (!fileName.isEmpty()) {
    //        m_view->loadEntities(fileName);
        }
    });

    // --- View plane actions ---
    m_actTop   = new QAction(tr("Top (XY)"), this);
    m_actFront = new QAction(tr("Front (XZ)"), this);
    m_actRight = new QAction(tr("Right (YZ)"), this);

    connect(m_actTop, &QAction::triggered, [this]() {
    //    m_view->setViewXY();
        m_view->update();
        m_act2D->setChecked(true);
        m_act3D->setChecked(false);
    });

    connect(m_actFront, &QAction::triggered, [this]() {
    //    m_view->setViewXZ();
        m_view->update();
        m_act2D->setChecked(true);
        m_act3D->setChecked(false);
    });

    connect(m_actRight, &QAction::triggered, [this]() {
    //    m_view->setViewYZ();
        m_view->update();
        m_act2D->setChecked(true);
        m_act3D->setChecked(false);
    });

    actionCreateSketch = new QAction(QIcon(":/icons/sketch.png"), "Create Sketch", this);
    actionCreateExtrude = new QAction(QIcon(":/icons/extrude.png"), "Create Extrusion", this);

    connect(actionCreateSketch, &QAction::triggered, this, &MainWindow::onCreateSketch);
    connect(actionCreateExtrude, &QAction::triggered, this, &MainWindow::onCreateExtrude);

}

void MainWindow::createToolbar() {
    auto *tb = addToolBar(tr("Main"));
    tb->addAction(actionCreateSketch);
    tb->addAction(actionCreateExtrude);
    tb->addSeparator();
    tb->addAction(m_actDrawLine);
    tb->addAction(m_actDrawArc);
    tb->addSeparator();
    tb->addAction(m_actSave);
    tb->addAction(m_actLoad);
    tb->addSeparator();
    tb->addAction(m_actPrint);
    tb->addAction(m_actExportPdf);
    tb->addSeparator();
    tb->addAction(m_actTop);
    tb->addAction(m_actFront);
    tb->addAction(m_actRight);

    // default: 2D checked
    m_act2D->setChecked(true);
}

void MainWindow::createCentral() {
    m_view = new CadView(this);
    setCentralWidget(m_view);
    // connect signal → slot
    connect(m_view, &CadView::featureAdded, this, &MainWindow::updateFeatureTree);
}

void MainWindow::createFeatureBrowser() {
    QDockWidget* dock = new QDockWidget("Feature Tree", this);
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    featureTree = new QTreeWidget(dock);
    featureTree->setColumnCount(1);
    featureTree->setHeaderLabel("Features");

    dock->setWidget(featureTree);
    addDockWidget(Qt::LeftDockWidgetArea, dock);

    connect(featureTree, &QTreeWidget::itemClicked, this, &MainWindow::onFeatureSelected);
}

void MainWindow::updateFeatureTree() {
    featureTree->clear();

    // --- Sketches section ---
    QTreeWidgetItem* sketchesRoot = new QTreeWidgetItem(featureTree);
    sketchesRoot->setText(0, "Sketches");

    for (auto& s : m_view->doc.sketches) {
        QTreeWidgetItem* item = new QTreeWidgetItem(sketchesRoot);
        item->setText(0, s->name.isEmpty() ? QString("Sketch %1").arg(s->id) : s->name);
        item->setIcon(0, QIcon(":/icons/sketch.png"));
        item->setData(0, Qt::UserRole, s->id);
    }

    // --- Features section ---
    QTreeWidgetItem* featuresRoot = new QTreeWidgetItem(featureTree);
    featuresRoot->setText(0, "Features");

    for (auto& f : m_view->doc.features) {
        QTreeWidgetItem* item = new QTreeWidgetItem(featuresRoot);
        item->setText(0, f->name.isEmpty() ? QString("Feature %1").arg(f->id) : f->name);

        switch (f->type) {
        case FeatureType::Extrude: item->setIcon(0, QIcon(":/icons/extrude.png")); break;
        default: break;
        }

        item->setData(0, Qt::UserRole, f->id);
    }

    featureTree->expandAll(); // expand by default
}

void MainWindow::onFeatureSelected(QTreeWidgetItem* item, int column) {
    int featureId = item->data(0, Qt::UserRole).toInt();
    auto f = m_view->doc.findFeature(featureId);
    if (f) {
        m_view->highlightFeature(featureId);
    }
}

void MainWindow::onCreateSketch() {
    if (!m_view) return;

    // 1. Ask user for plane selection
    QStringList planes = { "XY (Top)", "XZ (Front)", "YZ (Right)",
                          "XY (Bottom)", "XZ (Back)", "YZ (Left)" };

    bool ok;
    QString choice = QInputDialog::getItem(this,
                                           tr("Select Sketch Plane"),
                                           tr("Plane:"),
                                           planes,
                                           0,
                                           false,
                                           &ok);
    if (!ok || choice.isEmpty())
        return;

    // 2. Map choice → SketchPlane enum + camera orientation
    SketchPlane plane = SketchPlane::Custom;
    if (choice.startsWith("XY (Top)")) {
        plane = SketchPlane::XY;
        m_view->setSketchView(SketchView::Top);
    } else if (choice.startsWith("XZ (Front)")) {
        plane = SketchPlane::XZ;
        m_view->setSketchView(SketchView::Front);
    } else if (choice.startsWith("YZ (Right)")) {
        plane = SketchPlane::YZ;
        m_view->setSketchView(SketchView::Right);
    } else if (choice.startsWith("XY (Bottom)")) {
        plane = SketchPlane::XY;
        m_view->setSketchView(SketchView::Bottom);
    } else if (choice.startsWith("XZ (Back)")) {
        plane = SketchPlane::XZ;
        m_view->setSketchView(SketchView::Back);
    } else if (choice.startsWith("YZ (Left)")) {
        plane = SketchPlane::YZ;
        m_view->setSketchView(SketchView::Left);
    }

    // 3. Create new sketch feature in document
    auto sketch = m_view->doc.createSketch(plane);

    // 4. Hand over to view for interactive drawing
    m_view->startSketchMode(sketch);
    updateFeatureTree();
}

void MainWindow::onCreateExtrude() {
    if (!m_view) return;

    // Get selected feature from tree
    QTreeWidgetItem* item = featureTree->currentItem();
    if (!item) return;

    int featureId = item->data(0, Qt::UserRole).toInt();
    auto f = m_view->doc.findFeature(featureId);

    if (f && f->type == FeatureType::Sketch) {
        m_view->startExtrudeMode(std::static_pointer_cast<SketchNode>(f));
    }
}

// --- slots ---
void MainWindow::toggle2D() {
//    m_view->setViewMode(CadView::Mode2D);
    m_act2D->setChecked(true);
    m_act3D->setChecked(false);
}

void MainWindow::toggle3D() {
//    m_view->setViewMode(CadView::Mode3D);
    m_act2D->setChecked(false);
    m_act3D->setChecked(true);
}
