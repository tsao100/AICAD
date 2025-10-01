// CRITICAL: Include MainWindow.h first (which includes ECL before Qt)
#include "MainWindow.h"

// Now include remaining Qt headers
#include <QToolBar>
#include <QAction>
#include <QFileDialog>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>

// Qt5/Qt6 compatibility for regular expressions
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    #include <QRegularExpression>
#else
    #include <QRegExp>
#endif

// Helper to convert ECL objects to QString
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

// Helper function to evaluate ECL code - separate to avoid MSVC SEH issues
// This function has no C++ objects with destructors
static bool evaluateECLForm(const char* code, cl_object* result) {
    cl_object form = c_string_to_object(code);

    if (form == Cnil || form == NULL) {
        return false;
    }

    bool success = true;
    *result = Cnil;

    CL_CATCH_ALL_BEGIN(ecl_process_env()) {
        *result = cl_eval(form);
    } CL_CATCH_ALL_IF_CAUGHT {
        success = false;
    } CL_CATCH_ALL_END;

    return success;
}

// ctor
MainWindow::MainWindow()
    : historyIndex(-1), consoleVisible(false)
{
    createActions();
    createToolbar();
    createCentral();
    createFeatureBrowser();

    // Initialize ECL Lisp command interface
    initECL();

    setWindowTitle("Qt CAD Viewer with Lisp");
    resize(1024, 768);
}

MainWindow::~MainWindow() {
    cl_shutdown();
}

void MainWindow::initECL() {
    char *argv[1] = {(char*)"app"};
    cl_boot(1, argv);
    atexit(cl_shutdown);

    defineCADCommands();

    // Setup command line interface (overlay on CAD view)
    QWidget *central = centralWidget();
    QVBoxLayout *overlay = qobject_cast<QVBoxLayout*>(central->layout());
    if (!overlay) {
        overlay = new QVBoxLayout(central);
        overlay->setContentsMargins(0, 0, 0, 0);
        overlay->setSpacing(0);
    }

    // Result label setup (positioned above command input)
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
    resultLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    resultLabel->setVisible(false);
    resultLabel->setMinimumHeight(100);
    resultLabel->setMaximumHeight(400);
    resultLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // Setup result fade animation
    resultOpacityEffect = new QGraphicsOpacityEffect(resultLabel);
    resultLabel->setGraphicsEffect(resultOpacityEffect);
    resultOpacityEffect->setOpacity(1.0);

    resultFadeAnimation = new QPropertyAnimation(resultOpacityEffect, "opacity", this);
    resultFadeAnimation->setDuration(1000);
    resultFadeAnimation->setStartValue(1.0);
    resultFadeAnimation->setEndValue(0.0);

    // Console output setup (hidden by default)
    consoleOutput = new QPlainTextEdit(central);
    consoleOutput->setReadOnly(true);
    consoleOutput->setStyleSheet("background:black; color:lightgreen; font-family:monospace;");
    consoleOutput->setVisible(false);
    consoleOutput->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // Setup console fade animation
    opacityEffect = new QGraphicsOpacityEffect(consoleOutput);
    consoleOutput->setGraphicsEffect(opacityEffect);
    opacityEffect->setOpacity(1.0);

    fadeAnimation = new QPropertyAnimation(opacityEffect, "opacity", this);
    fadeAnimation->setDuration(1000);
    fadeAnimation->setStartValue(1.0);
    fadeAnimation->setEndValue(0.0);

    // Command input setup
    commandInput = new QLineEdit(central);
    commandInput->setPlaceholderText("Enter Lisp command or CAD command (e.g., 'line')...");
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
    commandInput->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    // Toggle button
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
        "} "
        "QPushButton:hover { "
        "background: rgba(50, 50, 50, 200); "
        "border: 2px solid lightgray; "
        "} "
        "QPushButton:pressed { "
        "background: rgba(100, 100, 100, 200); "
        "}"
    );
    toggleButton->setToolTip("Toggle console (F2)");
    toggleButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    // Bottom layout
    QHBoxLayout *bottomLayout = new QHBoxLayout();
    bottomLayout->setSpacing(5);
    bottomLayout->setContentsMargins(5, 5, 5, 5);
    bottomLayout->addWidget(commandInput);
    bottomLayout->addWidget(toggleButton);

    QWidget *bottomWidget = new QWidget(central);
    bottomWidget->setLayout(bottomLayout);

    // Add to overlay (at bottom of CAD view)
    overlay->addStretch();
    overlay->addWidget(consoleOutput);
    overlay->addWidget(resultLabel);
    overlay->addWidget(bottomWidget, 0, Qt::AlignBottom);

    // Setup fade timer
    fadeTimer = new QTimer(this);
    fadeTimer->setSingleShot(true);
    fadeTimer->setInterval(3000);

    connect(commandInput, &QLineEdit::returnPressed, this, &MainWindow::executeCommand);
    connect(toggleButton, &QPushButton::clicked, this, &MainWindow::toggleConsole);
    connect(fadeTimer, &QTimer::timeout, this, &MainWindow::fadeOutResult);
    connect(resultFadeAnimation, &QPropertyAnimation::finished, this, [this]() {
        resultLabel->setVisible(false);
        resultOpacityEffect->setOpacity(1.0);
    });

    // Install event filter for command history
    commandInput->installEventFilter(this);

    showResultTemporarily("ECL Lisp initialized. Press F2 to toggle console.");
}

void MainWindow::defineCADCommands() {
    // Define CAD-style commands in Lisp
    const char* lineCommand = R"(
        (defun line (&optional p1 p2)
          (cond
            ((and p1 p2)
             (format nil "Drawing line from ~A to ~A" p1 p2))
            (p1
             (format nil "Line started at ~A. Specify next point." p1))
            (t
             "LINE command: Specify first point")))
    )";

    cl_object result;
    evaluateECLForm(lineCommand, &result);

    const char* circleCommand = R"(
        (defun circle (&optional center radius)
          (cond
            ((and center radius)
             (format nil "Drawing circle at ~A with radius ~A" center radius))
            (center
             (format nil "Circle center at ~A. Specify radius." center))
            (t
             "CIRCLE command: Specify center point")))
    )";

    evaluateECLForm(circleCommand, &result);

    // Add sketch command
    const char* sketchCommand = R"(
        (defun sketch (&optional plane)
          (cond
            (plane
             (format nil "Circle center at ~A. Specify radius." plane))
            (t
             "Sketch command: Specify plane.")))
         )";

    evaluateECLForm(sketchCommand, &result);

    // Add extrude command
    const char* extrudeCommand = R"(
        (defun extrude (&optional sketch-id height)
          (cond
            ((and sketch-id height)
             (format nil "Extruding sketch ~A by height ~A" sketch-id height))
            (sketch-id
             (format nil "Sketch ~A selected. Specify height." sketch-id))
            (t
             "EXTRUDE command: Select sketch and specify height")))
    )";

    evaluateECLForm(extrudeCommand, &result);
}

void MainWindow::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_F2) {
        toggleConsole();
        event->accept();
    } else {
        QMainWindow::keyPressEvent(event);
    }
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event) {
    if (obj == commandInput && event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);

        // Handle Up Arrow
        if (keyEvent->key() == Qt::Key_Up) {
            if (!commandHistory.isEmpty() && historyIndex < commandHistory.size() - 1) {
                historyIndex++;
                commandInput->setText(commandHistory[commandHistory.size() - 1 - historyIndex]);
            }
            return true;
        }

        // Handle Down Arrow
        if (keyEvent->key() == Qt::Key_Down) {
            if (historyIndex > 0) {
                historyIndex--;
                commandInput->setText(commandHistory[commandHistory.size() - 1 - historyIndex]);
            } else if (historyIndex == 0) {
                historyIndex = -1;
                commandInput->clear();
            }
            return true;
        }

        // Handle Spacebar when empty - repeat last command
        if (keyEvent->key() == Qt::Key_Space && commandInput->text().isEmpty()) {
            if (!commandHistory.isEmpty()) {
                commandInput->setText(commandHistory.last());
                executeCommand();
            }
            return true;
        }
    }

    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::toggleConsole() {
    consoleVisible = !consoleVisible;
    consoleOutput->setVisible(consoleVisible);

    if (consoleVisible) {
        fadeTimer->stop();
        fadeAnimation->stop();
        opacityEffect->setOpacity(1.0);
    }

    commandInput->setFocus();
}

// Escape helper: makes a safe Lisp string literal
static QString escapeForLisp(const QString &s) {
    QString out = s;
    out.replace("\\", "\\\\");   // backslashes
    out.replace("\"", "\\\"");   // double quotes
    out.replace("\n", "\\n");    // newlines
    out.replace("\r", "\\r");
    return out;
}

void MainWindow::executeCommand() {
    QString cmd = commandInput->text().trimmed();
    if (cmd.isEmpty()) return;

    consoleOutput->appendPlainText(QString("指令: %1").arg(cmd));
    commandInput->clear();

    // Wrap user text in a Lisp string literal
    QString lit = "\"" + escapeForLisp(cmd) + "\"";

    // Safe Lisp form: read the string, eval it, catch errors
    QString wrapped = QString(
                          "(handler-case "
                          "  (with-output-to-string (s) "
                          "    (let ((obj (read-from-string %1))) "
                          "      (princ (eval obj) s))) "
                          "  (arithmetic-error (e) (format nil \"ARITHMETIC ERROR: ~A\" e)) "
                          "  (error (e) (format nil \"ERROR: ~A\" e)))"
                          ).arg(lit);

    cl_object form = c_string_to_object(wrapped.toUtf8().constData());

    cl_object res = Cnil;
    try {
        // Use ecl_safe_eval if available, else cl_eval
#ifdef HAVE_ECL_SAFE_EVAL
        res = ecl_safe_eval(form, Cnil, Cnil);
#else
        res = cl_eval(form);
#endif
        QString out = eclObjectToQString(res);
        consoleOutput->appendPlainText(out + "\n");
    } catch (...) {
        consoleOutput->appendPlainText("Error evaluating expression (C++ exception).\n");
    }
}

void MainWindow::showResultTemporarily(const QString &result) {
    resultLabel->setText(result);
    resultLabel->setVisible(true);
    resultOpacityEffect->setOpacity(1.0);

    QFontMetrics fm(resultLabel->font());
    int textHeight = fm.boundingRect(
        resultLabel->contentsRect(),
        Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop,
        result
    ).height();

    int desiredHeight = qBound(100, textHeight + 30, 400);
    resultLabel->setFixedHeight(desiredHeight);

    QTimer::singleShot(3000, this, [this]() {
        resultFadeAnimation->start();
    });
}

void MainWindow::fadeOutResult() {
    fadeAnimation->start();
}

// --- CAD UI implementation ---
void MainWindow::createActions() {
    m_act2D = new QAction(tr("2D View"), this);
    m_act2D->setCheckable(true);
    connect(m_act2D, &QAction::triggered, this, &MainWindow::toggle2D);

    m_act3D = new QAction(tr("3D View"), this);
    m_act3D->setCheckable(true);
    connect(m_act3D, &QAction::triggered, this, &MainWindow::toggle3D);

    m_actDrawLine = new QAction(tr("Draw Line"), this);
    connect(m_actDrawLine, &QAction::triggered, [this]() {
        toggle2D();
    });

    m_actDrawArc = new QAction(tr("Draw Arc"), this);
    connect(m_actDrawArc, &QAction::triggered, [this]() {
        toggle2D();
    });

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

    m_actSave = new QAction(tr("Save"), this);
    connect(m_actSave, &QAction::triggered, [this]() {
        QString fileName = QFileDialog::getSaveFileName(
            this, tr("Save CAD File"), QString(),
            tr("CAD Files (*.txt *.json);;All Files (*)"));
        if (!fileName.isEmpty()) {
            // TODO: implement save
        }
    });

    m_actLoad = new QAction(tr("Load"), this);
    connect(m_actLoad, &QAction::triggered, [this]() {
        QString fileName = QFileDialog::getOpenFileName(
            this, tr("Open CAD File"), QString(),
            tr("CAD Files (*.txt *.json);;All Files (*)"));
        if (!fileName.isEmpty()) {
            // TODO: implement load
        }
    });

    m_actTop = new QAction(tr("Top (XY)"), this);
    m_actFront = new QAction(tr("Front (XZ)"), this);
    m_actRight = new QAction(tr("Right (YZ)"), this);

    connect(m_actTop, &QAction::triggered, [this]() {
        m_view->setSketchView(SketchView::Top);
        m_view->update();
        m_act2D->setChecked(true);
        m_act3D->setChecked(false);
    });

    connect(m_actFront, &QAction::triggered, [this]() {
        m_view->setSketchView(SketchView::Front);
        m_view->update();
        m_act2D->setChecked(true);
        m_act3D->setChecked(false);
    });

    connect(m_actRight, &QAction::triggered, [this]() {
        m_view->setSketchView(SketchView::Right);
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

    m_act2D->setChecked(true);
}

void MainWindow::createCentral() {
    m_view = new CadView(this);
    setCentralWidget(m_view);
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

    QTreeWidgetItem* sketchesRoot = new QTreeWidgetItem(featureTree);
    sketchesRoot->setText(0, "Sketches");

    for (auto& s : m_view->doc.sketches) {
        QTreeWidgetItem* item = new QTreeWidgetItem(sketchesRoot);
        item->setText(0, s->name.isEmpty() ? QString("Sketch %1").arg(s->id) : s->name);
        item->setIcon(0, QIcon(":/icons/sketch.png"));
        item->setData(0, Qt::UserRole, s->id);
    }

    QTreeWidgetItem* featuresRoot = new QTreeWidgetItem(featureTree);
    featuresRoot->setText(0, "Features");

    for (auto& f : m_view->doc.features) {
        QTreeWidgetItem* item = new QTreeWidgetItem(featuresRoot);
        item->setText(0, f->name.isEmpty() ? QString("Feature %1").arg(f->id) : f->name);

        switch (f->type) {
        case FeatureType::Extrude:
            item->setIcon(0, QIcon(":/icons/extrude.png"));
            break;
        default:
            break;
        }

        item->setData(0, Qt::UserRole, f->id);
    }

    featureTree->expandAll();
}

void MainWindow::onFeatureSelected(QTreeWidgetItem* item, int /*column*/) {
    int featureId = item->data(0, Qt::UserRole).toInt();
    auto f = m_view->doc.findFeature(featureId);
    if (f) {
        m_view->highlightFeature(featureId);
    }
}

void MainWindow::onCreateSketch() {
    if (!m_view) return;

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

    auto sketch = m_view->doc.createSketch(plane);
    m_view->startSketchMode(sketch);
    updateFeatureTree();
}

void MainWindow::onCreateExtrude() {
    if (!m_view) return;

    QTreeWidgetItem* item = featureTree->currentItem();
    if (!item) return;

    int featureId = item->data(0, Qt::UserRole).toInt();
    auto f = m_view->doc.findFeature(featureId);

    if (f && f->type == FeatureType::Sketch) {
        m_view->startExtrudeMode(std::static_pointer_cast<SketchNode>(f));
    }
}

void MainWindow::toggle2D() {
    m_act2D->setChecked(true);
    m_act3D->setChecked(false);
}

void MainWindow::toggle3D() {
    m_act2D->setChecked(false);
    m_act3D->setChecked(true);
}
