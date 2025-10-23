#include "MainWindow.h"

#include <TDataStd_Name.hxx>

#ifdef __unix__
#include <fenv.h>
#include <signal.h>
#endif

#include <QToolBar>
#include <QAction>
#include <QFileDialog>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QPrinter>
#include <QPrintDialog>
#include <QPdfWriter>
#include <QPageLayout>

#ifdef HAVE_ECL
QString eclObjectToQString(cl_object obj) {
    if (obj == Cnil) return "NIL";

    cl_object strObj = cl_princ_to_string(obj);

    if (strObj != Cnil && ECL_STRINGP(strObj) && ECL_BASE_STRING_P(strObj)) {
        const char* cstr = (const char*)ecl_base_string_pointer_safe(strObj);
        if (cstr) {
            return QString::fromUtf8(cstr);
        }
    }

    if (strObj != Cnil && ECL_STRINGP(strObj)) {
        cl_object base_str = si_coerce_to_base_string(strObj);
        if (base_str != Cnil && ECL_BASE_STRING_P(base_str)) {
            const char* cstr = (const char*)ecl_base_string_pointer_safe(base_str);
            if (cstr) {
                return QString::fromUtf8(cstr);
            }
        }
    }

    return "<unconvertible>";
}
#endif

MainWindow::MainWindow()
    : m_waitingForSecondPoint(false)
#ifdef HAVE_ECL
    , historyIndex(-1), consoleVisible(false)
#endif
{
    m_document.newDocument();

    createMenusAndToolbars();
    createCentral();
    createFeatureBrowser();

#ifdef HAVE_ECL
    QTimer::singleShot(0, this, [this]() {
        initECL();
        setPrompt("Command: ");
    });
#endif

    setWindowTitle("AICAD - Open CASCADE CAD System");
    resize(1280, 800);
    setStatusBar(new QStatusBar(this));
}

MainWindow::~MainWindow() {
#ifdef HAVE_ECL
    cl_shutdown();
#endif
}

// void MainWindow::createMenusAndToolbars() {
//     QMenuBar* menuBar = new QMenuBar(this);
//     setMenuBar(menuBar);

//     QMenu* fileMenu = menuBar->addMenu("&File");
//     QMenu* viewMenu = menuBar->addMenu("&View");
//     QMenu* sketchMenu = menuBar->addMenu("&Sketch");
//     QMenu* featureMenu = menuBar->addMenu("F&eature");

//     QToolBar* mainToolBar = addToolBar("Main");
//     QToolBar* viewToolBar = addToolBar("View");

//     QAction* newAction = new QAction("&New", this);
//     newAction->setShortcut(QKeySequence::New);
//     connect(newAction, &QAction::triggered, [this]() {
//         m_document.newDocument();
//         m_view->displayAllFeatures();
//         updateFeatureTree();
//     });
//     fileMenu->addAction(newAction);

//     QAction* saveAction = new QAction("&Save", this);
//     saveAction->setShortcut(QKeySequence::Save);
//     connect(saveAction, &QAction::triggered, this, &MainWindow::onSave);
//     fileMenu->addAction(saveAction);
//     mainToolBar->addAction(saveAction);

//     QAction* loadAction = new QAction("&Open", this);
//     loadAction->setShortcut(QKeySequence::Open);
//     connect(loadAction, &QAction::triggered, this, &MainWindow::onLoad);
//     fileMenu->addAction(loadAction);
//     mainToolBar->addAction(loadAction);

//     fileMenu->addSeparator();

//     QAction* exportPdfAction = new QAction("Export &PDF", this);
//     connect(exportPdfAction, &QAction::triggered, this, &MainWindow::onExportPdf);
//     fileMenu->addAction(exportPdfAction);

//     fileMenu->addSeparator();

//     QAction* exitAction = new QAction("E&xit", this);
//     exitAction->setShortcut(QKeySequence::Quit);
//     connect(exitAction, &QAction::triggered, this, &MainWindow::onExit);
//     fileMenu->addAction(exitAction);

//     QAction* viewTopAction = new QAction("&Top", this);
//     connect(viewTopAction, &QAction::triggered, this, &MainWindow::onViewTop);
//     viewMenu->addAction(viewTopAction);
//     viewToolBar->addAction(viewTopAction);

//     QAction* viewFrontAction = new QAction("&Front", this);
//     connect(viewFrontAction, &QAction::triggered, this, &MainWindow::onViewFront);
//     viewMenu->addAction(viewFrontAction);
//     viewToolBar->addAction(viewFrontAction);

//     QAction* viewRightAction = new QAction("&Right", this);
//     connect(viewRightAction, &QAction::triggered, this, &MainWindow::onViewRight);
//     viewMenu->addAction(viewRightAction);
//     viewToolBar->addAction(viewRightAction);

//     QAction* viewIsoAction = new QAction("&Isometric", this);
//     connect(viewIsoAction, &QAction::triggered, this, &MainWindow::onViewIsometric);
//     viewMenu->addAction(viewIsoAction);
//     viewToolBar->addAction(viewIsoAction);

//     QAction* sketchAction = new QAction("New &Sketch", this);
//     connect(sketchAction, &QAction::triggered, this, &MainWindow::onCreateSketch);
//     sketchMenu->addAction(sketchAction);
//     mainToolBar->addAction(sketchAction);

//     QAction* rectAction = new QAction("&Rectangle", this);
//     connect(rectAction, &QAction::triggered, this, &MainWindow::onDrawRectangle);
//     sketchMenu->addAction(rectAction);

//     QAction* lineAction = new QAction("&Line", this);
//     connect(lineAction, &QAction::triggered, this, &MainWindow::onDrawLine);
//     sketchMenu->addAction(lineAction);

//     QAction* extrudeAction = new QAction("&Extrude", this);
//     connect(extrudeAction, &QAction::triggered, this, &MainWindow::onCreateExtrude);
//     featureMenu->addAction(extrudeAction);
//     mainToolBar->addAction(extrudeAction);

//     statusBar = new QStatusBar(this);
//     setStatusBar(statusBar);
//     statusBar()->showMessage("Ready");
// }

void MainWindow::registerCADCommand(
    const QString& name,
    const QStringList& aliases,
    int expectedArgs,
    const QString& description,
    bool interactive,
    const QString& qtSlot,
    std::function<void(const QStringList&)> handler)
{
    CADCommand cmd;
    cmd.name = name;
    cmd.aliases = aliases;
    cmd.handler = handler;
    cmd.expectedArgs = expectedArgs;
    cmd.description = description;
    cmd.interactive = interactive;
    cmd.qtSlot = qtSlot;

    cadCommands[name.toLower()] = cmd;

    // Register aliases
    for (const QString& alias : aliases) {
        cadCommands[alias.toLower()] = cmd;
    }
}

void MainWindow::loadMenuConfig(const QString& filename) {
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Cannot open menu config:" << filename;
        return;
    }

    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();

        if (line.isEmpty() || line.startsWith('#')) continue;

        QStringList parts = line.split('|');
        if (parts.size() < 3) continue;

        QString type = parts[0];

        if (type == "toolbar") {
            QString id = parts[1];
            QString label = parts[2];
            QString icon = parts.size() > 3 ? parts[3] : "";
            QString shortcut = parts.size() > 4 ? parts[4] : "";
            QString callback = parts.size() > 5 ? parts[5] : "";

            if (id == "separator") {
                if (QToolBar* tb = findChild<QToolBar*>("MainToolbar")) {
                    tb->addSeparator();
                }
            } else {
                QAction* action = new QAction(label, this);
                if (!icon.isEmpty()) action->setIcon(QIcon(icon));
                if (!shortcut.isEmpty()) action->setShortcut(QKeySequence(shortcut));

                if (!callback.isEmpty()) {
                    connect(action, &QAction::triggered, this, [this, callback]() {
                        QMetaObject::invokeMethod(this, callback.toUtf8().constData());
                    });
                }

                actions[id] = action;
                if (QToolBar* tb = findChild<QToolBar*>("MainToolbar")) {
                    tb->addAction(action);
                }
            }
        }
        else if (type == "menu") {
            QString menuName = parts[1];
            QString id = parts[2];
            QString label = parts[3];
            QString shortcut = parts.size() > 4 ? parts[4] : "";
            QString callback = parts.size() > 5 ? parts[5] : "";

            if (!menus.contains(menuName)) {
                QMenu* menu = menuBar()->addMenu(menuName);
                menus[menuName] = menu;
            }

            QMenu* menu = menus[menuName];

            if (id == "separator") {
                menu->addSeparator();
            } else {
                QAction* action = new QAction(label, this);
                if (!shortcut.isEmpty()) action->setShortcut(QKeySequence(shortcut));

                if (!callback.isEmpty()) {
                    connect(action, &QAction::triggered, this, [this, callback]() {
                        QMetaObject::invokeMethod(this, callback.toUtf8().constData());
                    });
                }

                menu->addAction(action);
                actions[menuName + "_" + id] = action;
            }
        }
        else if (type == "command") {
            QString name = parts[1];
            QString alias = parts.size() > 2 ? parts[2] : "";
            QString expectedArgsStr = parts.size() > 3 ? parts[3] : "-1";
            QString callback = parts.size() > 4 ? parts[4] : "";

            if (!callback.isEmpty()) {
                QStringList aliases;
                if (!alias.isEmpty()) {
                    aliases << alias;
                }

                int expectedArgs = expectedArgsStr.toInt();

                registerCADCommand(
                    name,
                    aliases,
                    expectedArgs,
                    "", // Description
                    expectedArgs > 0, // Interactive if expects args
                    callback,
                    [this, callback](const QStringList& args) {
                        if (args.isEmpty()) {
                            QMetaObject::invokeMethod(this, callback.toUtf8().constData());
                        } else {
                            // Arguments provided - let initializeCADCommands() override handle it
                            QMetaObject::invokeMethod(this, callback.toUtf8().constData());
                        }
                    }
                    );
            }
        }
    }
}

void MainWindow::createMenusAndToolbars() {
    // Create toolbar first
    QToolBar* tb = addToolBar(tr("Main"));
    tb->setObjectName("MainToolbar");

    // Load menu/toolbar configuration
    loadMenuConfig("menu.txt");
}

void MainWindow::createCentral() {
    m_view = new CadView(this);
    m_view->setDocument(&m_document);

    connect(m_view, &CadView::pointAcquired, this, &MainWindow::onPointAcquired);
    connect(m_view, &CadView::getPointCancelled, this, &MainWindow::onGetPointCancelled);

    setCentralWidget(m_view);
}

void MainWindow::createFeatureBrowser() {
    QDockWidget* dock = new QDockWidget("Feature Tree", this);
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    featureTree = new QTreeWidget(dock);
    featureTree->setHeaderLabel("Features");
    connect(featureTree, &QTreeWidget::itemClicked, this, &MainWindow::onFeatureSelected);

    dock->setWidget(featureTree);
    addDockWidget(Qt::LeftDockWidgetArea, dock);

    updateFeatureTree();
}

void MainWindow::updateFeatureTree() {
    featureTree->clear();

    QVector<TDF_Label> features = m_document.getFeatures();

    for (const TDF_Label& label : features) {
        QString name = m_document.getFeatureName(label);
        int id = m_document.getFeatureId(label);
        FeatureType type = m_document.getFeatureType(label);

        QString typeStr = (type == FeatureType::Sketch) ? "Sketch" :
                              (type == FeatureType::Extrude) ? "Extrude" : "Unknown";

        QTreeWidgetItem* item = new QTreeWidgetItem(featureTree);
        item->setText(0, QString("%1 [%2]").arg(name).arg(typeStr));
        item->setData(0, Qt::UserRole, id);

        featureTree->addTopLevelItem(item);
    }
}

void MainWindow::onFeatureSelected(QTreeWidgetItem* item, int column) {
    if (!item) return;

    int featureId = item->data(0, Qt::UserRole).toInt();
    m_view->highlightFeature(featureId);
}

void MainWindow::onCreateSketch() {
    QStringList items;
    items << "XY Plane" << "XZ Plane" << "YZ Plane";

    bool ok;
    QString item = QInputDialog::getItem(this, "New Sketch",
                                         "Select sketch plane:", items, 0, false, &ok);

    if (ok && !item.isEmpty()) {
        CustomPlane plane;
        if (item == "XY Plane") plane = CustomPlane::XY();
        else if (item == "XZ Plane") plane = CustomPlane::XZ();
        else if (item == "YZ Plane") plane = CustomPlane::YZ();

        QString tempName = QString("Sketch (%1)").arg(plane.getDisplayName());

        m_activeSketch = m_document.createSketch(plane, tempName);

        int sketchId = m_document.getFeatureId(m_activeSketch);
        QString name = QString("Sketch %1 (%2)").arg(sketchId).arg(plane.getDisplayName());
        TDataStd_Name::Set(m_activeSketch, TCollection_ExtendedString(name.toStdWString().c_str()));

        m_view->setPendingSketch(m_activeSketch);

        updateFeatureTree();
        statusBar()->showMessage("Sketch created. Use sketch tools to add geometry.");
    }
}

void MainWindow::onDrawRectangle() {
    if (m_activeSketch.IsNull()) {
        QMessageBox::warning(this, "No Active Sketch",
                             "Please create a sketch first.");
        return;
    }

    m_rectanglePoints.clear();
    m_waitingForSecondPoint = false;
    m_view->setMode(CadMode::Sketching);
    m_view->setRubberBandMode(RubberBandMode::Rectangle);
    statusBar()->showMessage("Click first corner of rectangle...");
}

void MainWindow::onDrawLine() {
    if (m_activeSketch.IsNull()) {
        QMessageBox::warning(this, "No Active Sketch",
                             "Please create a sketch first.");
        return;
    }

    m_view->setMode(CadMode::Sketching);
    m_view->setRubberBandMode(RubberBandMode::Polyline);
    statusBar()->showMessage("Click first point (Enter to finish)...");
}

void MainWindow::onPointAcquired(QVector2D point) {
    if (m_view->getMode() == CadMode::Sketching) {
        if (m_view->getCurrentView() == SketchView::None ||
            m_view->getCurrentView() == SketchView::Isometric) {
            QMessageBox::warning(this, "Invalid View",
                                 "Please switch to an orthographic view (Top/Front/Right) for sketching.");
            m_view->setMode(CadMode::Idle);
            return;
        }

        if (!m_waitingForSecondPoint) {
            m_rectanglePoints.clear();
            m_rectanglePoints.append(point);
            m_waitingForSecondPoint = true;
            statusBar()->showMessage(QString("First corner: (%1, %2). Click opposite corner...")
                                       .arg(point.x(), 0, 'f', 2)
                                       .arg(point.y(), 0, 'f', 2));
        } else {
            m_rectanglePoints.append(point);

            QVector<QVector2D> rectPoints;
            QVector2D p1 = m_rectanglePoints[0];
            QVector2D p2 = m_rectanglePoints[1];

            rectPoints.append(QVector2D(p1.x(), p1.y()));
            rectPoints.append(QVector2D(p2.x(), p1.y()));
            rectPoints.append(QVector2D(p2.x(), p2.y()));
            rectPoints.append(QVector2D(p1.x(), p2.y()));
            rectPoints.append(QVector2D(p1.x(), p1.y()));

            m_document.addPolylineToSketch(m_activeSketch, rectPoints);
            m_view->displayFeature(m_activeSketch);

            m_view->setMode(CadMode::Idle);
            m_waitingForSecondPoint = false;
            statusBar()->showMessage("Rectangle created.");
        }
    }
}

void MainWindow::onGetPointCancelled() {
    m_view->setMode(CadMode::Idle);
    m_rectanglePoints.clear();
    m_waitingForSecondPoint = false;
    statusBar()->showMessage("Operation cancelled.");
}

void MainWindow::onCreateExtrude() {
    if (m_activeSketch.IsNull()) {
        QMessageBox::warning(this, "No Active Sketch",
                             "Please select a sketch to extrude.");
        return;
    }

    bool ok;
    double height = QInputDialog::getDouble(this, "Extrude",
                                            "Enter extrude height:",
                                            1.0, -1000.0, 1000.0, 2, &ok);

    if (ok) {
        QString tempName = "Extrude";
        TDF_Label extrudeLabel = m_document.createExtrude(m_activeSketch, height, tempName);

        int extrudeId = m_document.getFeatureId(extrudeLabel);
        QString name = QString("Extrude %1").arg(extrudeId);
        TDataStd_Name::Set(extrudeLabel, TCollection_ExtendedString(name.toStdWString().c_str()));

        m_view->displayFeature(extrudeLabel);
        m_view->fitAll();

        updateFeatureTree();
        statusBar()->showMessage(QString("Extrude created with height %1").arg(height));
    }
}

void MainWindow::onSave() {
    QString filename = QFileDialog::getSaveFileName(this, "Save Document",
                                                    "", "OCAF Documents (*.ocaf)");

    if (!filename.isEmpty()) {
        if (!filename.endsWith(".ocaf")) {
            filename += ".ocaf";
        }

        if (m_document.saveDocument(filename)) {
            statusBar()->showMessage("Document saved: " + filename);
        } else {
            QMessageBox::critical(this, "Error", "Failed to save document.");
        }
    }
}

void MainWindow::onLoad() {
    QString filename = QFileDialog::getOpenFileName(this, "Open Document",
                                                    "", "OCAF Documents (*.ocaf)");

    if (!filename.isEmpty()) {
        if (m_document.loadDocument(filename)) {
            m_view->displayAllFeatures();
            updateFeatureTree();
            statusBar()->showMessage("Document loaded: " + filename);
        } else {
            QMessageBox::critical(this, "Error", "Failed to load document.");
        }
    }
}

void MainWindow::onPrint() {
    statusBar()->showMessage("Print functionality not yet implemented.");
}

void MainWindow::onExportPdf() {
    QString filename = QFileDialog::getSaveFileName(this, "Export PDF",
                                                    "", "PDF Files (*.pdf)");

    if (!filename.isEmpty()) {
        if (!filename.endsWith(".pdf")) {
            filename += ".pdf";
        }

        statusBar()->showMessage("PDF export not yet fully implemented: " + filename);
    }
}

void MainWindow::onViewTop() {
    m_view->setSketchView(SketchView::Top);
    statusBar()->showMessage("View: Top");
}

void MainWindow::onViewFront() {
    m_view->setSketchView(SketchView::Front);
    statusBar()->showMessage("View: Front");
}

void MainWindow::onViewRight() {
    m_view->setSketchView(SketchView::Right);
    statusBar()->showMessage("View: Right");
}

void MainWindow::onViewIsometric() {
    m_view->setSketchView(SketchView::Isometric);
    statusBar()->showMessage("View: Isometric");
}

void MainWindow::onExit() {
    close();
}

void MainWindow::keyPressEvent(QKeyEvent *event) {
#ifdef HAVE_ECL
    if (event->key() == Qt::Key_F2) {
        toggleConsole();
        return;
    }
#endif

    QMainWindow::keyPressEvent(event);
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event) {
#ifdef HAVE_ECL
    if (obj == commandInput && event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);

        if (keyEvent->key() == Qt::Key_Up) {
            if (historyIndex > 0) {
                historyIndex--;
                commandInput->setText(commandHistory[historyIndex]);
            }
            return true;
        } else if (keyEvent->key() == Qt::Key_Down) {
            if (historyIndex < commandHistory.size() - 1) {
                historyIndex++;
                commandInput->setText(commandHistory[historyIndex]);
            } else {
                historyIndex = commandHistory.size();
                commandInput->clear();
            }
            return true;
        }
    }
#endif

    return QMainWindow::eventFilter(obj, event);
}

#ifdef HAVE_ECL
void MainWindow::initECL() {
#ifdef __unix__
    fenv_t fpu_state;
    fegetenv(&fpu_state);
    struct sigaction old_sigfpe;
    sigaction(SIGFPE, NULL, &old_sigfpe);
#endif

    char *argv[1] = {(char*)"AICAD"};
    cl_boot(1, argv);
    atexit(cl_shutdown);

#ifdef __unix__
    fesetenv(&fpu_state);
    sigaction(SIGFPE, &old_sigfpe, NULL);
    feclearexcept(FE_ALL_EXCEPT);
#endif

    QWidget *central = centralWidget();
    QVBoxLayout *overlay = new QVBoxLayout();
    central->setLayout(overlay);

    resultLabel = new QLabel(central);
    resultLabel->setStyleSheet(
        "QLabel { "
        "background: rgba(0, 0, 0, 180); "
        "color: lightgreen; "
        "font-family: monospace; "
        "font-size: 14px; "
        "padding: 10px; "
        "border: 1px solid green; "
        "border-radius: 5px; "
        "}"
        );
    resultLabel->setWordWrap(true);
    resultLabel->setVisible(false);

    resultOpacityEffect = new QGraphicsOpacityEffect(resultLabel);
    resultLabel->setGraphicsEffect(resultOpacityEffect);

    resultFadeAnimation = new QPropertyAnimation(resultOpacityEffect, "opacity", this);
    resultFadeAnimation->setDuration(1000);

    consoleOutput = new QPlainTextEdit(central);
    consoleOutput->setReadOnly(true);
    consoleOutput->setStyleSheet("background:black; color:lightgreen; font-family:monospace;");
    consoleOutput->setVisible(false);

    opacityEffect = new QGraphicsOpacityEffect(consoleOutput);
    consoleOutput->setGraphicsEffect(opacityEffect);

    fadeAnimation = new QPropertyAnimation(opacityEffect, "opacity", this);
    fadeAnimation->setDuration(1000);

    commandInput = new QLineEdit(central);
    commandInput->setPlaceholderText("");
    commandInput->setStyleSheet(
        "QLineEdit { "
        "background: rgba(0, 0, 0, 200); "
        "color: white; "
        "font-family: monospace; "
        "font-size: 14px; "
        "padding: 8px; "
        "border: 2px solid gray; "
        "border-radius: 3px; "
        "}"
        );

    toggleButton = new QPushButton("F2", central);
    toggleButton->setStyleSheet(
        "QPushButton { "
        "background: rgba(0, 0, 0, 200); "
        "color: white; "
        "font-family: monospace; "
        "font-size: 14px; "
        "padding: 8px 15px; "
        "border: 2px solid gray; "
        "border-radius: 3px; "
        "min-width: 50px; "
        "}"
        );

    QHBoxLayout *bottomLayout = new QHBoxLayout();
    bottomLayout->addWidget(commandInput);
    bottomLayout->addWidget(toggleButton);

    QWidget *bottomWidget = new QWidget(central);
    bottomWidget->setLayout(bottomLayout);

    overlay->addStretch();
    overlay->addWidget(consoleOutput);
    overlay->addWidget(resultLabel);
    overlay->addWidget(bottomWidget, 0, Qt::AlignBottom);

    fadeTimer = new QTimer(this);
    fadeTimer->setSingleShot(true);
    fadeTimer->setInterval(3000);

    connect(commandInput, &QLineEdit::returnPressed, this, &MainWindow::executeCommand);
    connect(toggleButton, &QPushButton::clicked, this, &MainWindow::toggleConsole);
    connect(fadeTimer, &QTimer::timeout, this, &MainWindow::fadeOutResult);

    commandInput->installEventFilter(this);

    showResultTemporarily("ECL Lisp initialized. Press F2 to toggle console.");
}

void MainWindow::toggleConsole() {
    consoleVisible = !consoleVisible;
    consoleOutput->setVisible(consoleVisible);

    if (consoleVisible) {
        commandInput->setFocus();
    } else {
        m_view->setFocus();
    }
}

void MainWindow::showResultTemporarily(const QString &result) {
    resultLabel->setText(result);
    resultLabel->setVisible(true);
    resultOpacityEffect->setOpacity(1.0);
    fadeTimer->start();
}

void MainWindow::fadeOutResult() {
    resultFadeAnimation->setStartValue(1.0);
    resultFadeAnimation->setEndValue(0.0);
    resultFadeAnimation->start();

    QTimer::singleShot(1000, this, [this]() {
        resultLabel->setVisible(false);
    });
}

void MainWindow::setPrompt(const QString &prompt) {
    promptText = prompt;
    promptLength = prompt.length();
    commandInput->setPlaceholderText(prompt);
}

void MainWindow::executeCommand() {
    QString cmd = commandInput->text().trimmed();
    if (cmd.isEmpty()) return;

    commandHistory.append(cmd);
    historyIndex = commandHistory.size();

    consoleOutput->appendPlainText(promptText + cmd);

    cl_object form = c_string_to_object(cmd.toUtf8().constData());
    if (form != Cnil) {
        cl_object result = Cnil;

#ifdef _MSC_VER
        result = cl_eval(form);
#else
        CL_CATCH_ALL_BEGIN(ecl_process_env()) {
            result = cl_eval(form);
        } CL_CATCH_ALL_IF_CAUGHT {
            consoleOutput->appendPlainText("Error evaluating expression");
        } CL_CATCH_ALL_END;
#endif

        QString resultStr = eclObjectToQString(result);
        consoleOutput->appendPlainText(resultStr);
        showResultTemporarily(resultStr);
    }

    commandInput->clear();
}
#endif

// void MainWindow::loadFileFromCommandLine(const QString& filename) {
//     QTimer::singleShot(100, this, [this, filename]() {
//         if (filename.endsWith(".cad")) {
//             if (QFile::exists(filename)) {
//                 m_view->doc.loadFromFile(filename);
//                 updateFeatureTree();
//                 m_view->update();
//                 showResultTemporarily(QString("Loaded: %1").arg(filename));
//             }
//         } else if (filename.endsWith(".lsp") || filename.endsWith(".lisp")) {
//             loadLispFile(filename);
//         }
//     });
// }

