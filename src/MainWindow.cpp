// CRITICAL: Include MainWindow.h first (which includes ECL before Qt)
#include "MainWindow.h"

// Unix-specific headers for FPU control
#ifdef __unix__
#include <fenv.h>
#include <signal.h>
#endif

// Now include remaining Qt headers
#include <QToolBar>
#include <QAction>
#include <QFileDialog>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <cmath>

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

// Helper function to evaluate ECL code
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

    // CRITICAL: Initialize ECL AFTER all OpenGL widgets are created
    // This prevents ECL from interfering with OpenGL context creation
    QTimer::singleShot(0, this, [this]() {
        initECL();
        // Set initial prompt
        setPrompt("Command: ");
    });

    setWindowTitle("Qt CAD Viewer with Lisp");
    resize(1024, 768);
}

MainWindow::~MainWindow() {
    cl_shutdown();
}

void MainWindow::initECL() {
#ifdef __unix__
    // Save the current FPU state before ECL initialization
    fenv_t fpu_state;
    fegetenv(&fpu_state);

    // Save signal handlers
    struct sigaction old_sigfpe;
    sigaction(SIGFPE, NULL, &old_sigfpe);
#endif

    char *argv[1] = {(char*)"app"};
    cl_boot(1, argv);
    atexit(cl_shutdown);

#ifdef __unix__
    // CRITICAL: Restore FPU state after ECL boot
    // ECL may change floating point exception masks
    fesetenv(&fpu_state);

    // Restore SIGFPE handler if ECL changed it
    sigaction(SIGFPE, &old_sigfpe, NULL);

    // Explicitly clear any pending FPU exceptions
    feclearexcept(FE_ALL_EXCEPT);
#endif

    defineCADCommands();

    // Setup command line interface (overlay on CAD view)
    QWidget *central = centralWidget();
    QVBoxLayout *overlay = qobject_cast<QVBoxLayout*>(central->layout());
    if (!overlay) {
        overlay = new QVBoxLayout(central);
        overlay->setContentsMargins(0, 0, 0, 0);
        overlay->setSpacing(0);
    }

    // Result label setup
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

    // Console output setup
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

    // Add to overlay
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

    commandInput->installEventFilter(this);

    showResultTemporarily("ECL Lisp initialized. Press F2 to toggle console.");
}

// Add ECL getpoint interface setup
void MainWindow::setupGetPointECLInterface() {
    // Register C++ function callable from Lisp
    // This requires ECL's defun interface

    const char* bridgeSetup = R"(
        ;; Global state for tracking active getpoint requests
        (defvar *active-getpoint* nil)
        (defvar *getpoint-callbacks* (make-hash-table))

        ;; Simulate getpoint result delivery from C++
        (defun deliver-point-result (request-id x y)
          "Called by C++ when point is acquired"
          (let ((callback (gethash request-id *getpoint-callbacks*)))
            (when callback
              (funcall callback (list x y))
              (remhash request-id *getpoint-callbacks*))))

        ;; Helper to create point list
        (defun make-point (x y)
          (list x y))

        (defun point-x (pt) (first pt))
        (defun point-y (pt) (second pt))
    )";

    cl_object result;
    evaluateECLForm(bridgeSetup, &result);
}

void MainWindow::defineCADCommands() {
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

    const char* sketchCommand = R"(
        (defun sketch (&optional plane)
          (cond
            (plane
             (format nil "Circle center at ~A. Specify radius." plane))
            (t
             "Sketch command: Specify plane.")))
         )";

    evaluateECLForm(sketchCommand, &result);

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

    // Define getpoint function
    const char* getpointCommand = R"(
        (defparameter *getpoint-result* nil)
        (defparameter *getpoint-waiting* nil)

        (defun getpoint (&optional (prompt "Specify point") previous-point)
          (setf *getpoint-waiting* t)
          (setf *getpoint-result* nil)

          (if previous-point
              (format nil "GETPOINT: ~A [from ~A]" prompt previous-point)
              (format nil "GETPOINT: ~A" prompt)))
    )";

    evaluateECLForm(getpointCommand, &result);

    // Interactive line command example
    const char* interactiveLineCmd = R"(
        (defun draw-line-interactive ()
          "Interactive line drawing"
          (let ((p1 (getpoint "First point: "))
                (p2 nil))
            (when p1
              (setf p2 (getpoint "Second point: " p1))
              (when p2
                (format nil "Line drawn from ~A to ~A" p1 p2)))))
    )";

    evaluateECLForm(interactiveLineCmd, &result);

    // Setup C++ <-> Lisp bridge (would need additional work)
    setupGetPointECLInterface();
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

        // Prevent deleting prompt text with Backspace or Delete
        if (keyEvent->key() == Qt::Key_Backspace) {
            if (commandInput->cursorPosition() <= promptLength) {
                return true; // Block backspace if cursor is at or before prompt
            }
            // Check if selection includes prompt
            if (commandInput->hasSelectedText() && commandInput->selectionStart() < promptLength) {
                return true; // Block backspace if selection includes prompt
            }
        }

        if (keyEvent->key() == Qt::Key_Delete) {
            if (commandInput->cursorPosition() < promptLength) {
                return true; // Block delete if cursor is before prompt end
            }
            // Check if selection includes prompt
            if (commandInput->hasSelectedText() && commandInput->selectionStart() < promptLength) {
                return true; // Block backspace if selection includes prompt
            }
        }

        // Prevent cursor from moving into prompt area with Left arrow
        if (keyEvent->key() == Qt::Key_Left) {
            if (commandInput->cursorPosition() <= promptLength) {
                return true; // Block left arrow at prompt boundary
            }
        }

        // Prevent selecting or moving to beginning if it would include prompt
        if (keyEvent->key() == Qt::Key_Home) {
            if (keyEvent->modifiers() & Qt::ShiftModifier) {
                // Shift+Home: select from cursor to after prompt
                int curPos = commandInput->cursorPosition();
                commandInput->setSelection(promptLength, curPos - promptLength);
            } else {
                // Home: move cursor to just after prompt
                commandInput->setCursorPosition(promptLength);
            }
            return true;
        }

        // Handle Ctrl+A (Select All) - only select user input, not prompt
        if (keyEvent->key() == Qt::Key_A && (keyEvent->modifiers() & Qt::ControlModifier)) {
            int textLen = commandInput->text().length();
            if (textLen > promptLength) {
                commandInput->setSelection(promptLength, textLen - promptLength);
            }
            return true;
        }

        // Handle Up Arrow - navigate backwards in history
        if (keyEvent->key() == Qt::Key_Up) {
            if (!commandHistory.isEmpty() && historyIndex < commandHistory.size() - 1) {
                historyIndex++;
                QString cmd = commandHistory[commandHistory.size() - 1 - historyIndex];
                commandInput->setText(promptText + cmd);
                commandInput->setCursorPosition(commandInput->text().length());
            }
            return true;
        }

        if (keyEvent->key() == Qt::Key_Down) {
            if (historyIndex > 0) {
                historyIndex--;
                QString cmd = commandHistory[commandHistory.size() - 1 - historyIndex];
                commandInput->setText(promptText + cmd);
                commandInput->setCursorPosition(commandInput->text().length());
            } else if (historyIndex == 0) {
                historyIndex = -1;
                commandInput->setText(promptText);
                commandInput->setCursorPosition(commandInput->text().length());
            }
            return true;
        }

        if (keyEvent->key() == Qt::Key_Space && commandInput->text().isEmpty()) {
            if (!commandHistory.isEmpty()) {
                commandInput->setText(promptText + commandHistory.last());
                commandInput->setCursorPosition(commandInput->text().length());
                executeCommand();
            }
            return true;
        }

        // Handle text input - if there's a selection that includes prompt, adjust it
        if (!keyEvent->text().isEmpty() && commandInput->hasSelectedText()) {
            int selStart = commandInput->selectionStart();
            int selLen = commandInput->selectedText().length();
            if (selStart < promptLength) {
                // Selection starts in prompt area - adjust to only replace user text
                int newStart = promptLength;
                int newLen = selLen - (promptLength - selStart);
                if (newLen > 0) {
                    commandInput->setSelection(newStart, newLen);
                } else {
                    commandInput->deselect();
                    commandInput->setCursorPosition(promptLength);
                }
            }
        }
    }

    // Handle mouse selection to prevent selecting prompt
    if (obj == commandInput && event->type() == QEvent::MouseButtonPress) {
        // Allow the click, but we'll fix the cursor position after
        QTimer::singleShot(0, this, [this]() {
            if (commandInput->cursorPosition() < promptLength) {
                commandInput->setCursorPosition(promptLength);
            }
            // If selection includes prompt, adjust it
            if (commandInput->hasSelectedText() && commandInput->selectionStart() < promptLength) {
                int selEnd = commandInput->selectionStart() + commandInput->selectedText().length();
                if (selEnd > promptLength) {
                    commandInput->setSelection(promptLength, selEnd - promptLength);
                } else {
                    commandInput->deselect();
                    commandInput->setCursorPosition(promptLength);
                }
            }
        });
    }

    // Handle text changes to ensure prompt stays
    if (obj == commandInput && event->type() == QEvent::FocusIn) {
        // Ensure cursor doesn't start in prompt area
        if (commandInput->cursorPosition() < promptLength) {
            commandInput->setCursorPosition(promptLength);
        }
    }

    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::toggleConsole() {
    consoleVisible = !consoleVisible;
    consoleOutput->setVisible(consoleVisible);

    if (consoleVisible) {
        // Stop any ongoing fade and restore opacity
        fadeTimer->stop();
        fadeAnimation->stop();
        opacityEffect->setOpacity(1.0);
    }

    commandInput->setFocus();
}

void MainWindow::executeCommand() {
    QString cmd = commandInput->text().trimmed();

    // Remove prompt from command if present
    if (cmd.startsWith(promptText)) {
        cmd = cmd.mid(promptText.length()).trimmed();
    }

    if (cmd.isEmpty()) return;

    // Handle GetPoint keyboard input
    if (currentGetPointRequest.active) {
        // Parse coordinates from command
        // trimmed() returns a new QString, must capture it
        QString coordStr = cmd;
        coordStr = coordStr.remove(currentGetPointRequest.prompt).trimmed();

        // Support multiple formats:
        // 1. "X,Y" - Absolute
        // 2. "X Y" - Absolute
        // 3. "@X,Y" - Relative (if previous point exists)
        // 4. "distance<angle" - Polar

        bool relative = coordStr.startsWith('@');
        bool polar = coordStr.contains('<');

        if (relative) coordStr = coordStr.mid(1);

        QVector2D point;

        if (polar && coordStr.contains('<')) {
            // Polar coordinates: distance<angle
            QStringList parts = coordStr.split('<');
            if (parts.size() == 2) {
                bool okDist, okAngle;
                float distance = parts[0].toFloat(&okDist);
                float angle = parts[1].toFloat(&okAngle);

                if (okDist && okAngle) {
                    float radians = angle * M_PI / 180.0f;
                    float dx = distance * cos(radians);
                    float dy = distance * sin(radians);

                    if (relative && currentGetPointRequest.hasPreviousPoint) {
                        point = currentGetPointRequest.previousPoint + QVector2D(dx, dy);
                    } else {
                        point = QVector2D(dx, dy);
                    }
                } else {
                    showResultTemporarily("Invalid polar format. Use: distance<angle");
                    return;
                }
            }
        } else {
            // Cartesian coordinates - Qt5/Qt6 compatible split
            QStringList parts;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            parts = coordStr.split(QRegularExpression("[,\\s]+"), Qt::SkipEmptyParts);
#elif QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
            parts = coordStr.split(QRegularExpression("[,\\s]+"), Qt::SkipEmptyParts);
#else \
    // For Qt5 < 5.14, use simple split on comma or space
            coordStr = coordStr.replace(',', ' ');
            parts = coordStr.split(' ', QString::SkipEmptyParts);
#endif

            if (parts.size() >= 2) {
                bool okX, okY;
                float x = parts[0].toFloat(&okX);
                float y = parts[1].toFloat(&okY);

                if (okX && okY) {
                    if (relative && currentGetPointRequest.hasPreviousPoint) {
                        point = currentGetPointRequest.previousPoint + QVector2D(x, y);
                    } else {
                        point = QVector2D(x, y);
                    }
                } else {
                    showResultTemporarily("Invalid coordinate format. Use: X,Y or X Y");
                    return;
                }
            } else {
                showResultTemporarily("Invalid format. Use: X,Y or X Y or @X,Y or distance<angle");
                return;
            }
        }

        // Cancel getpoint mode in view
        m_view->getPointState.active = false;

        // CRITICAL: Don't execute callback directly!
        // Use QTimer::singleShot to defer execution, allowing executeCommand() to return first
        QTimer::singleShot(0, this, [this, point]() {
            // Now safely call onPointAcquired which handles callback execution properly
            onPointAcquired(point);
        });

        // Clear command input immediately
        commandInput->clear();
        setPrompt("Command: ");
        return;
    }

    // Add to history
    if (commandHistory.isEmpty() || commandHistory.last() != cmd) {
        commandHistory.append(cmd);
    }
    historyIndex = -1; // Reset history navigation

    // Reset to prompt
    setPrompt("Command: ");

    // Check if it's a CAD-style command
    QString wrapped;
    if (!cmd.startsWith('(')) {
        // Qt5/Qt6 compatible string splitting
        QStringList parts;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        parts = cmd.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
#elif QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
        parts = cmd.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
#else \
    // For Qt5 < 5.14, use simple split
        parts = cmd.split(' ', QString::SkipEmptyParts);
#endif

        QString funcName = parts[0].toLower();
        QStringList args = parts.mid(1);

        if (args.isEmpty()) {
            wrapped = QString("(%1)").arg(funcName);
        } else {
            QString argStr = args.join(" ");
            wrapped = QString("(%1 %2)").arg(funcName, argStr);
        }
    } else {
        wrapped = cmd;
    }

    // Wrap in error handler
    QString safewrapped = QString(
                              "(handler-case %1 "
                              "(error (e) (format nil \"ERROR: ~A\" e)))").arg(wrapped);

    QString out;

    // Convert to C string
    QByteArray codeBytes = safewrapped.toUtf8();
    const char* codeStr = codeBytes.constData();

    // Evaluate
    cl_object res = Cnil;
    bool evalSuccess = evaluateECLForm(codeStr, &res);

    if (!evalSuccess) {
        out = "ERROR: Exception during evaluation";
    } else if (res == NULL) {
        out = "ERROR: Evaluation returned NULL";
    } else {
        out = eclObjectToQString(res);
    }

    // Log to console
    consoleOutput->appendPlainText(promptText + QString("%1").arg(cmd));
    consoleOutput->appendPlainText(out + "\n");

    // Show result if console is hidden
    if (!consoleVisible) {
        showResultTemporarily(out);
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

void MainWindow::setPrompt(const QString &prompt) {
    promptText = prompt;
    promptLength = prompt.length();
    commandInput->setText(promptText);
    commandInput->setCursorPosition(promptLength);
}

void MainWindow::onPointAcquired(QVector2D point) {
    // First signal from startGetPoint() - just setup the prompt, don't execute callback
    if (!currentGetPointRequest.active) {
        if (m_view->getPointState.active) {
            currentGetPointRequest.active = true;
            currentGetPointRequest.prompt = m_view->getPointState.prompt;
            currentGetPointRequest.hasPreviousPoint = m_view->getPointState.hasPreviousPoint;
            if (m_view->getPointState.hasPreviousPoint) {
                currentGetPointRequest.previousPoint = m_view->getPointState.previousPoint;
            }

            // Show prompt in commandInput
            setPrompt(currentGetPointRequest.prompt);
            commandInput->setReadOnly(false);
            commandInput->setFocus();
            commandInput->selectAll();
        }
        return;
    }

    // Actual point acquired - execute callback
    if (currentGetPointRequest.callback) {
        // Step 1: Move callback to temp variable (prevents self-assignment during execution)
        auto callback = std::move(currentGetPointRequest.callback);

        // Step 2: Log to console
        consoleOutput->appendPlainText(
            QString("%1 (%2, %3)")
                .arg(currentGetPointRequest.prompt)
                .arg(point.x(), 0, 'f', 3)
                .arg(point.y(), 0, 'f', 3)
            );

        // Step 3: Clear state BEFORE executing callback
        currentGetPointRequest.active = false;
        currentGetPointRequest.callback = nullptr;

        // Reset prompt
        commandInput->clear();
        setPrompt("Command: ");

        // Step 4: Execute the callback
        // (Callback may call startGetPoint() and set pendingCallback)
        callback(point);

        // Step 5: After callback completes, check if pendingCallback was set
        // Move it to callback for the next point acquisition
        if (currentGetPointRequest.pendingCallback) {
            currentGetPointRequest.callback = std::move(currentGetPointRequest.pendingCallback);
            currentGetPointRequest.pendingCallback = nullptr;
        }
    } else {
        // No callback set - just reset
        currentGetPointRequest.active = false;
        commandInput->clear();
        setPrompt("Command: ");
    }

}

void MainWindow::onGetPointCancelled() {
    if (currentGetPointRequest.active) {
        // Check if switching to keyboard mode
        if (m_view->getPointState.keyboardMode) {
            setPrompt(currentGetPointRequest.prompt + " ");
            commandInput->setFocus();
            commandInput->setCursorPosition(commandInput->text().length());
            return;
        }

        // Cancelled
        consoleOutput->appendPlainText("*Cancelled*");
        currentGetPointRequest.active = false;
        commandInput->clear();
        setPrompt("Command: ");
    }
}


void MainWindow::onDrawRectangle() {
    if (!m_view) return;

    // Check if we have an active sketch or create one
    if (m_view->doc.sketches.isEmpty()) {
        // No sketch exists - need to create one first
        QMessageBox::information(this,
                                 tr("No Sketch"),
                                 tr("Please create a sketch first using 'Create Sketch' button."));
        return;
    }

    // Use the most recent sketch or let user select
    std::shared_ptr<SketchNode> targetSketch;

    if (m_view->doc.sketches.size() == 1) {
        targetSketch = m_view->doc.sketches.last();
    } else {
        // Multiple sketches - ask user to select from tree
        QTreeWidgetItem* item = featureTree->currentItem();
        if (item) {
            int featureId = item->data(0, Qt::UserRole).toInt();
            auto f = m_view->doc.findFeature(featureId);
            if (f && f->type == FeatureType::Sketch) {
                targetSketch = std::static_pointer_cast<SketchNode>(f);
            }
        }

        if (!targetSketch) {
            QMessageBox::information(this,
                                     tr("Select Sketch"),
                                     tr("Please select a sketch in the feature tree first."));
            return;
        }
    }

    // Set appropriate view for the sketch plane
    switch (targetSketch->plane) {
    case SketchPlane::XY:
        m_view->setSketchView(SketchView::Top);
        break;
    case SketchPlane::XZ:
        m_view->setSketchView(SketchView::Front);
        break;
    case SketchPlane::YZ:
        m_view->setSketchView(SketchView::Right);
        break;
    default:
        break;
    }

    // Store the target sketch for use in callbacks
    m_view->pendingSketch = targetSketch;

    consoleOutput->appendPlainText("=== Draw Rectangle ===");

    // Step 1: Get first corner
    m_view->startGetPoint("Specify first corner:");

    // Capture by value (shared_ptr is ref-counted, safe to copy)
    std::shared_ptr<SketchNode> sketch = targetSketch;

    // Setup callback for first corner
    currentGetPointRequest.callback = [this, sketch](QVector2D corner1) {
        consoleOutput->appendPlainText(
            QString("First corner: (%1, %2)")
                .arg(corner1.x(), 0, 'f', 3)
                .arg(corner1.y(), 0, 'f', 3)
            );

        // Step 2: Get opposite corner with rubber band preview
        m_view->rubberBandState.mode = RubberBandMode::Rectangle;
        m_view->rubberBandState.startPoint = corner1;
        m_view->rubberBandState.currentPoint = corner1;
        m_view->rubberBandState.active = true;

        m_view->startGetPoint("Specify opposite corner:", &corner1);

        // CRITICAL: Use pendingCallback to avoid self-assignment during callback execution
        // Setup callback for second corner - capture corner1 and sketch
        currentGetPointRequest.pendingCallback = [this, sketch, corner1](QVector2D corner2) {
            consoleOutput->appendPlainText(
                QString("Opposite corner: (%1, %2)")
                    .arg(corner2.x(), 0, 'f', 3)
                    .arg(corner2.y(), 0, 'f', 3)
                );

            // Create the rectangle
            createRectangleEntity(sketch, corner1, corner2);

            consoleOutput->appendPlainText("Rectangle created.");
            updateFeatureTree();
            m_view->update();
        };
    };

    // Clear rubber band state after point acquisition
    m_view->rubberBandState.active = false;
    m_view->rubberBandState.mode = RubberBandMode::None;
    m_view->rubberBandState.intermediatePoints.clear();
}

// Helper function to create rectangle entity
void MainWindow::createRectangleEntity(std::shared_ptr<SketchNode> sketch,
                                       const QVector2D& corner1,
                                       const QVector2D& corner2) {
    if (!sketch) return;

    // Create 4 corners in 2D plane coordinates
    QVector2D c1 = corner1;
    QVector2D c2 = QVector2D(corner2.x(), corner1.y());
    QVector2D c3 = corner2;
    QVector2D c4 = QVector2D(corner1.x(), corner2.y());

    // Convert to 3D world coordinates
    QVector<QVector3D> rectPoints;
    rectPoints << m_view->planeToWorld(c1)
               << m_view->planeToWorld(c2)
               << m_view->planeToWorld(c3)
               << m_view->planeToWorld(c4)
               << m_view->planeToWorld(c1); // Close the loop

    // Create polyline entity
    auto poly = std::make_shared<PolylineEntity>();
    poly->points = rectPoints;
    poly->plane = sketch->plane;

    // Add to sketch
    sketch->entities.push_back(poly);
}

// --- CAD UI implementation ---
void MainWindow::createActions() {
    m_act2D = new QAction(tr("2D View"), this);
    m_act2D->setCheckable(true);
    connect(m_act2D, &QAction::triggered, this, &MainWindow::toggle2D);

    m_act3D = new QAction(tr("3D View"), this);
    m_act3D->setCheckable(true);
    connect(m_act3D, &QAction::triggered, this, &MainWindow::toggle3D);

    m_actDrawLine = new QAction(QIcon(":/icons/line.png"), tr("Draw Line"), this);
    connect(m_actDrawLine, &QAction::triggered, [this]() {
        toggle2D();
    });

    m_actDrawArc = new QAction(QIcon(":/icons/arc.png"), tr("Draw Arc"), this);
    connect(m_actDrawArc, &QAction::triggered, [this]() {
        toggle2D();
    });

    m_actDrawRect = new QAction(QIcon(":/icons/rect.png"), tr("Draw Rect"), this);
    connect(m_actDrawRect, &QAction::triggered, this, &MainWindow::onDrawRectangle);

    m_actPrint = new QAction(QIcon(":/icons/print.png"), tr("Print"), this);
    connect(m_actPrint, &QAction::triggered, [this]() {
        m_view->printView();
    });

    m_actExportPdf = new QAction(QIcon(":/icons/export_pdf.png"), tr("Export PDF"), this);
    connect(m_actExportPdf, &QAction::triggered, [this]() {
        QString file = QFileDialog::getSaveFileName(
            this, tr("Export PDF"), QString(), tr("PDF Files (*.pdf)"));
        if (!file.isEmpty())
            m_view->exportPdf(file);
    });

    m_actSave = new QAction(QIcon(":/icons/save.png"), tr("Save"), this);
    connect(m_actSave, &QAction::triggered, [this]() {
        QString fileName = QFileDialog::getSaveFileName(
            this, tr("Save CAD File"), QString(),
            tr("CAD Files (*.txt *.json);;All Files (*)"));
        if (!fileName.isEmpty()) {
            // TODO: implement save
        }
    });

    m_actLoad = new QAction(QIcon(":/icons/load.png"), tr("Load"), this);
    connect(m_actLoad, &QAction::triggered, [this]() {
        QString fileName = QFileDialog::getOpenFileName(
            this, tr("Open CAD File"), QString(),
            tr("CAD Files (*.txt *.json);;All Files (*)"));
        if (!fileName.isEmpty()) {
            // TODO: implement load
        }
    });

    m_actTop = new QAction(QIcon(":/icons/top.png"), tr("Top (XY)"), this);
    m_actFront = new QAction(QIcon(":/icons/front.png"), tr("Front (XZ)"), this);
    m_actRight = new QAction(QIcon(":/icons/right.png"), tr("Right (YZ)"), this);

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
    tb->addAction(m_actDrawRect);
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

    // Connect GetPoint signals
    connect(m_view, &CadView::pointAcquired, this, &MainWindow::onPointAcquired);
    connect(m_view, &CadView::getPointCancelled, this, &MainWindow::onGetPointCancelled);
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
                          "XY (Bottom)", "XZ (Back)", "YZ (Left)" ,
                          "Custom (1,1,1) at origin" };

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

    // Create sketch once for all cases
    auto sketch = m_view->doc.createSketch(plane);

    // Configure custom plane if needed
    if (choice.startsWith("Custom")) {
        sketch->customPlane.origin = QVector3D(0, 0, 0);
        sketch->customPlane.normal = QVector3D(1, 1, 1).normalized();
        CadView::planeBasis(sketch->customPlane.normal,
                            sketch->customPlane.uAxis,
                            sketch->customPlane.vAxis);
        m_view->setSketchView(SketchView::None);
    }
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
