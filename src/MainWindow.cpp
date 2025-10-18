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
#ifdef _MSC_VER
    *result = cl_eval(form);
    success = (*result != OBJNULL);
#else
    CL_CATCH_ALL_BEGIN(ecl_process_env()) {
        *result = cl_eval(form);
    } CL_CATCH_ALL_IF_CAUGHT {
        success = false;
    } CL_CATCH_ALL_END;
#endif
    return success;
}

// Add near the top of MainWindow.cpp, after includes
static QString featureTypeToString(FeatureType type) {
    switch (type) {
    case FeatureType::Sketch:  return "Sketch";
    case FeatureType::Extrude: return "Extrude";
    default:                   return "Unknown";
    }
}

// ctor
MainWindow::MainWindow()
    : historyIndex(-1), consoleVisible(false)
{
    createMenusAndToolbars();
    createCentral();
    createFeatureBrowser();

    // CRITICAL: Initialize ECL AFTER all OpenGL widgets are created
    // This prevents ECL from interfering with OpenGL context creation
    QTimer::singleShot(0, this, [this]() {
        initECL();
        initializeCADCommands();
        // Set initial prompt
        setPrompt("Command: ");
        // Auto-load files after initialization
        autoLoadFiles();
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

    // Add rectangle command definition
    const char* rectangleCommand = R"(
        (defun rectangle (&optional pt1 pt2)
          (cond
            ((and pt1 pt2)
             (format nil "RECTANGLE: Drawing from ~A to ~A" pt1 pt2))
            (pt1
             (format nil "RECTANGLE: First corner at ~A. Specify opposite corner." pt1))
            (t
             "RECTANGLE: Specify first corner")))
    )";

    evaluateECLForm(rectangleCommand, &result);

    // Define ECL wrapper that calls C++ unified command system
    const char* commandWrapper = R"(
        (defun command (cmd &rest args)
          "Execute CAD commands - bridges to C++ handler"
          (let ((cmd-str (if (stringp cmd) cmd (string-downcase (symbol-name cmd))))
                (args-str (mapcar #'princ-to-string args)))
            (format nil "EXEC_CMD ~A~{ ~A~}" cmd-str args-str)))
    )";

    evaluateECLForm(commandWrapper, &result);


    // Setup C++ <-> Lisp bridge (would need additional work)
    setupGetPointECLInterface();
}

void MainWindow::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_F2) {
        toggleConsole();
        event->accept();
    } else if (event->key() == Qt::Key_F3) {
        onToggleObjectSnap();
        event->accept();
    } else if (event->key() == Qt::Key_Escape) {
        if (m_view) {
            m_view->clearSelection();
        }
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

    // Try registered commands first
    if (executeRegisteredCommand(cmd)) {
        commandHistory.append(cmd);
        historyIndex = -1;
        commandInput->clear();
        setPrompt("Command: ");
        consoleOutput->appendPlainText(promptText + cmd);
        return;
    }

    QString lowerCmd = cmd.toLower();
    if (lowerCmd == "rectangle" || lowerCmd == "rect") {
        commandHistory.append(cmd);
        historyIndex = -1;
        commandInput->clear();
        setPrompt("Command: ");
        consoleOutput->appendPlainText(promptText + cmd);
        onDrawRectangle();
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
    // Check if it's a direct CAD command
    QString wrapped;
    QStringList parts;
    if (cmd.startsWith("(command")) {
        QStringList parts = parseLispList(cmd);

        if (!parts.isEmpty()) {
            QString cmdName = parts[0].remove("\"").trimmed();
            QStringList args = parts.mid(1);

            consoleOutput->appendPlainText(promptText + cmd);
            executeCADCommand(cmdName, args);
            commandInput->clear();
            setPrompt("Command: ");
            return;
        }
    }
    // Check if it's a direct CAD command
    else if (!cmd.startsWith('(') && !cmd.startsWith('!')) {
        QStringList parts = cmd.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);

        QString cmdName = parts[0].toLower();
        if (cadCommands.contains(cmdName)) {
            QStringList args = parts.mid(1);

            consoleOutput->appendPlainText(promptText + cmd);
            executeCADCommand(cmdName, args);
            commandInput->clear();
            setPrompt("Command: ");
            return;
        }
    } else if (cmd.startsWith('!')) {
        // Extract variable name after !
        QString varName = cmd.mid(1).trimmed();
        if (!varName.isEmpty()) {
            wrapped = QString("(print %1)").arg(varName);
        }
    }
    else if (!cmd.startsWith('(')) {
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

    if (out.startsWith("EXEC_RECTANGLE")) {
        if (out == "EXEC_RECTANGLE") {
            // No args - start interactive rectangle
            onDrawRectangle();
            consoleOutput->appendPlainText(promptText + cmd);
            consoleOutput->appendPlainText("Starting rectangle command...\n");
            return;
        } else if (out.startsWith("EXEC_RECTANGLE_P1 ")) {
            // One point provided - start with first corner
            QString ptStr = out.mid(QString("EXEC_RECTANGLE_P1 ").length());
            QVector2D pt1 = parsePoint(ptStr);
            startRectangleWithFirstPoint(pt1);
            consoleOutput->appendPlainText(promptText + cmd);
            consoleOutput->appendPlainText(QString("First corner: (%1, %2)\n")
                                               .arg(pt1.x(), 0, 'f', 3).arg(pt1.y(), 0, 'f', 3));
            return;
        } else if (out.startsWith("EXEC_RECTANGLE ")) {
            // Both points provided - draw directly
            QString ptsStr = out.mid(QString("EXEC_RECTANGLE ").length());
            QStringList pts = ptsStr.split('(', Qt::SkipEmptyParts);
            if (pts.size() >= 2) {
                QVector2D pt1 = parsePoint(pts[0]);
                QVector2D pt2 = parsePoint(pts[1]);
                drawRectangleDirect(pt1, pt2);
                consoleOutput->appendPlainText(promptText + cmd);
                consoleOutput->appendPlainText("Rectangle created.\n");
                return;
            }
        }
    }

    // Log to console
    consoleOutput->appendPlainText(promptText + QString("%1").arg(cmd));
    consoleOutput->appendPlainText(out + "\n");

    // Show result if console is hidden
    if (!consoleVisible) {
        showResultTemporarily(out);
    }
}

// Implement command handlers
void MainWindow::onDrawLine() {
    consoleOutput->appendPlainText("Line command not yet implemented");
}

void MainWindow::onDrawArc() {
    consoleOutput->appendPlainText("Arc command not yet implemented");
}

void MainWindow::onDrawCircle() {
    consoleOutput->appendPlainText("Circle command started");
    // Implementation...
}

void MainWindow::onSave() {
    QString fileName = QFileDialog::getSaveFileName(
        this, tr("Save CAD File"), "../../Draw",
        tr("CAD Files (*.cad);;All Files (*)"));
    if (!fileName.isEmpty()) {
        m_view->doc.saveToFile(fileName);
    }
}

void MainWindow::onLoad() {
    QString fileName = QFileDialog::getOpenFileName(
        this, tr("Open CAD File"), "../../Draw",
        tr("CAD Files (*.cad);;All Files (*)"));
    if (!fileName.isEmpty()) {
        m_view->doc.loadFromFile(fileName);
        updateFeatureTree();
    }
}

// Add menu command to load Lisp files interactively
void MainWindow::onLoadLisp() {
    QString fileName = QFileDialog::getOpenFileName(
        this, tr("Load Lisp File"), QString(),
        tr("Lisp Files (*.lsp *.lisp);;All Files (*)"));

    if (!fileName.isEmpty()) {
        loadLispFile(fileName);
    }
}

void MainWindow::onPrint() {
    m_view->printView();
}

void MainWindow::onExportPdf() {
    QString file = QFileDialog::getSaveFileName(
        this, tr("Export PDF"), QString(), tr("PDF Files (*.pdf)"));
    if (!file.isEmpty())
        m_view->exportPdf(file);
}

void MainWindow::onViewTop() {
    m_view->setSketchView(SketchView::Top);
    m_view->update();
}

void MainWindow::onViewFront() {
    m_view->setSketchView(SketchView::Front);
    m_view->update();
}

void MainWindow::onViewRight() {
    m_view->setSketchView(SketchView::Right);
    m_view->update();
}

void MainWindow::onViewIsometric() {
    m_view->setSketchView(SketchView::None);
    m_view->update();
}

void MainWindow::onExit() {
    close();
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
            return;
        }

        // Cancelled
        consoleOutput->appendPlainText("*Cancelled*");
        currentGetPointRequest.active = false;
        commandInput->clear();
        setPrompt("Command: ");
        m_view->rubberBandState.active = false;
        m_view->update();
    }
}


void MainWindow::onDrawRectangle() {
    if (!m_view) return;

    if (m_view->doc.sketches.isEmpty()) {
        QMessageBox::information(this,
                                 tr("No Sketch"),
                                 tr("Please create a sketch first using 'Create Sketch' button."));
        return;
    }

    std::shared_ptr<SketchNode> targetSketch;

    if (m_view->doc.sketches.size() == 1) {
        targetSketch = m_view->doc.sketches.last();
    } else {
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

    // Orient view to sketch plane
    QString planeName = targetSketch->plane.getDisplayName();
    if (planeName == "XY") {
        m_view->setSketchView(SketchView::Top);
    } else if (planeName == "XZ") {
        m_view->setSketchView(SketchView::Front);
    } else if (planeName == "YZ") {
        m_view->setSketchView(SketchView::Right);
    } else {
        // Custom plane
        m_view->getCamera().lookAt(
            targetSketch->plane.origin + targetSketch->plane.normal * 10.0f,
            targetSketch->plane.origin,
            targetSketch->plane.vAxis
            );
        m_view->setSketchView(SketchView::None);
    }

    m_view->pendingSketch = targetSketch;

    consoleOutput->appendPlainText("=== Draw Rectangle ===");

    m_view->startGetPoint("Specify first corner:");

    std::shared_ptr<SketchNode> sketch = targetSketch;

    currentGetPointRequest.callback = [this, sketch](QVector2D corner1) {
        consoleOutput->appendPlainText(
            QString("First corner: (%1, %2)")
                .arg(corner1.x(), 0, 'f', 3)
                .arg(corner1.y(), 0, 'f', 3)
            );

        m_view->rubberBandState.mode = RubberBandMode::Rectangle;
        m_view->rubberBandState.startPoint = corner1;
        m_view->rubberBandState.currentPoint = corner1;
        m_view->rubberBandState.active = true;

        m_view->startGetPoint("Specify opposite corner:", &corner1);

        currentGetPointRequest.pendingCallback = [this, sketch, corner1](QVector2D corner2) {
            consoleOutput->appendPlainText(
                QString("Opposite corner: (%1, %2)")
                    .arg(corner2.x(), 0, 'f', 3)
                    .arg(corner2.y(), 0, 'f', 3)
                );

            createRectangleEntity(sketch, corner1, corner2);

            consoleOutput->appendPlainText("Rectangle created.");
            updateFeatureTree();
            m_view->update();
        };
    };

    m_view->rubberBandState.active = false;
    m_view->rubberBandState.mode = RubberBandMode::None;
    m_view->rubberBandState.intermediatePoints.clear();
}

void MainWindow::onGetPointKeyPressed(QString key) {
    if (!currentGetPointRequest.active) return;

    // Switch to keyboard mode
    m_view->getPointState.keyboardMode = true;

    // Activate commandInput and insert the key
    commandInput->setFocus();

    // Insert the key at cursor position (after prompt)
    int cursorPos = commandInput->cursorPosition();
    if (cursorPos < promptLength) {
        cursorPos = promptLength;
    }

    QString currentText = commandInput->text();
    QString newText = currentText.left(cursorPos) + key + currentText.mid(cursorPos);

    commandInput->setText(newText);
    commandInput->setCursorPosition(cursorPos + key.length());

    // Update focus management
    updateGetPointFocus();
}

bool MainWindow::isCommandInputEmpty() const {
    QString text = commandInput->text().trimmed();
    // Remove prompt to check actual user input
    if (text.startsWith(promptText)) {
        text = text.mid(promptText.length()).trimmed();
    }
    return text.isEmpty();
}

void MainWindow::updateGetPointFocus() {
    if (!currentGetPointRequest.active) return;

    if (isCommandInputEmpty() && !m_view->getPointState.keyboardMode) {
        // Empty input and not in keyboard mode - focus on CadView for mouse picking
        m_view->setFocus();
    } else {
        // User typed something - keep focus on CommandInput
        commandInput->setFocus();
    }
}

// Helper function to create rectangle entity
void MainWindow::createRectangleEntity(std::shared_ptr<SketchNode> sketch,
                                       const QVector2D& corner1,
                                       const QVector2D& corner2) {
    if (!sketch) return;

    QVector2D c1 = corner1;
    QVector2D c2 = QVector2D(corner2.x(), corner1.y());
    QVector2D c3 = corner2;
    QVector2D c4 = QVector2D(corner1.x(), corner2.y());

    auto poly = std::make_shared<PolylineEntity>();
    poly->points << c1 << c2 << c3 << c4 << c1;
    poly->plane = sketch->plane; // Store plane info

    sketch->entities.push_back(poly);
}

QVector2D MainWindow::parsePoint(const QString& ptStr) {
    // Parse "(X Y)" or "X,Y" or "X Y" format
    QString cleaned = ptStr.trimmed();

    // Remove parentheses and commas
    cleaned.remove('(').remove(')').remove(',');
    cleaned = cleaned.trimmed();

    // Split by whitespace
    QStringList coords = cleaned.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);

    if (coords.size() >= 2) {
        bool okX, okY;
        float x = coords[0].toFloat(&okX);
        float y = coords[1].toFloat(&okY);

        if (okX && okY) {
            return QVector2D(x, y);
        }
    }

    qWarning() << "parsePoint failed for:" << ptStr << "cleaned:" << cleaned << "coords:" << coords;
    return QVector2D(0, 0);
}

void MainWindow::startRectangleWithFirstPoint(const QVector2D& pt1) {
    if (!m_view || m_view->doc.sketches.isEmpty()) return;

    std::shared_ptr<SketchNode> sketch = m_view->doc.sketches.last();
    m_view->pendingSketch = sketch;

    // Setup rubber band
    m_view->rubberBandState.mode = RubberBandMode::Rectangle;
    m_view->rubberBandState.startPoint = pt1;
    m_view->rubberBandState.active = true;

    m_view->startGetPoint("Specify opposite corner:", &pt1);

    currentGetPointRequest.callback = [this, sketch, pt1](QVector2D pt2) {
        createRectangleEntity(sketch, pt1, pt2);
        updateFeatureTree();
        m_view->rubberBandState.active = false;
        m_view->update();
    };
}

void MainWindow::drawRectangleDirect(const QVector2D& pt1, const QVector2D& pt2) {
    if (!m_view || m_view->doc.sketches.isEmpty()) return;

    std::shared_ptr<SketchNode> sketch = m_view->doc.sketches.last();
    createRectangleEntity(sketch, pt1, pt2);
    updateFeatureTree();
    m_view->update();
}

// --- CAD UI implementation ---
void MainWindow::createCentral() {
    m_view = new CadView(this);
    setCentralWidget(m_view);

    statusBar = new QStatusBar(this);
    setStatusBar(statusBar);

    connect(m_view, &CadView::featureAdded, this, &MainWindow::updateFeatureTree);

    // Connect GetPoint signals
    connect(m_view, &CadView::pointAcquired, this, &MainWindow::onPointAcquired);
    connect(m_view, &CadView::getPointCancelled, this, &MainWindow::onGetPointCancelled);
    connect(m_view, &CadView::getPointKeyPressed, this, &MainWindow::onGetPointKeyPressed);
}

void MainWindow::createFeatureBrowser() {
    comboDock = new QDockWidget("Model Browser", this);
    comboDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    // Create tab widget for combo view
    comboView = new QTabWidget(comboDock);

    // Model tab with splitter
    createModelWidget();
    comboView->addTab(modelWidget, "Model");

    // Task widget tab
    createTaskWidget();
    comboView->addTab(taskWidget, "Task");

    int index = comboView->indexOf(taskWidget);
    if (index >= 0) {
        comboView->setTabVisible(index, false);  // Qt 5.15+
    }

    comboDock->setWidget(comboView);
    addDockWidget(Qt::LeftDockWidgetArea, comboDock);

    connect(featureTree, &QTreeWidget::itemClicked, this, &MainWindow::onFeatureSelected);
}

void MainWindow::createModelWidget() {
    modelWidget = new QWidget();
    QVBoxLayout* mainLayout = new QVBoxLayout(modelWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // Create splitter for tree and property sheets
    modelSplitter = new QSplitter(Qt::Vertical);

    // Feature tree (top half)
    featureTree = new QTreeWidget();
    featureTree->setColumnCount(1);
    featureTree->setHeaderLabel("Features");
    featureTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(featureTree, &QTreeWidget::customContextMenuRequested,
            this, &MainWindow::onTreeContextMenu);

    modelSplitter->addWidget(featureTree);

    // Property sheets (bottom half)
    propertyTabs = new QTabWidget();

    // View tab - text display
    viewText = new QTextEdit();
    viewText->setReadOnly(true);
    viewText->setStyleSheet("font-family: monospace; font-size: 10pt;");
    propertyTabs->addTab(viewText, "View");

    // Data tab - table display
    dataTable = new QTableWidget();
    dataTable->setColumnCount(2);
    dataTable->setHorizontalHeaderLabels(QStringList() << "Property" << "Value");
    dataTable->horizontalHeader()->setStretchLastSection(true);
    dataTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    propertyTabs->addTab(dataTable, "Data");

    modelSplitter->addWidget(propertyTabs);

    // Set initial splitter sizes (60% tree, 40% properties)
    modelSplitter->setStretchFactor(0, 6);
    modelSplitter->setStretchFactor(1, 4);

    mainLayout->addWidget(modelSplitter);
}

void MainWindow::createTaskWidget() {
    taskWidget = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(taskWidget);

    // Return button at top
    returnButton = new QPushButton(QIcon(":/icons/return.png"), "Return");
    returnButton->setStyleSheet("QPushButton { padding: 8px; font-size: 12pt; }");
    returnButton->setVisible(false);

    // Force the button to its natural size
    returnButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    returnButton->resize(returnButton->sizeHint());
    connect(returnButton, &QPushButton::clicked, this, &MainWindow::onReturnFromSketch);
    layout->addWidget(returnButton);
    layout->setAlignment(returnButton, Qt::AlignHCenter);

    // Title label
    QLabel* titleLabel = new QLabel("No Active Task");
    titleLabel->setObjectName("taskTitle");
    titleLabel->setStyleSheet("font-weight: bold; font-size: 14px; padding: 5px; background: #e0e0e0;");
    layout->addWidget(titleLabel);

    // Task content area
    QWidget* taskContent = new QWidget();
    taskContent->setObjectName("taskContent");
    QVBoxLayout* contentLayout = new QVBoxLayout(taskContent);
    contentLayout->addWidget(new QLabel("Select an item to edit"));
    layout->addWidget(taskContent);

    layout->addStretch();

}

void MainWindow::showTaskWidget(const QString& title) {
    int index = comboView->indexOf(taskWidget);
    if (index >= 0) {
        comboView->setTabVisible(index, true);  // Qt 5.15+
    }

    QLabel* titleLabel = taskWidget->findChild<QLabel*>("taskTitle");
    if (titleLabel) {
        titleLabel->setText(title);
    }

    returnButton->setVisible(true);
    comboView->setCurrentWidget(taskWidget);
}

void MainWindow::hideTaskWidget() {
    QLabel* titleLabel = taskWidget->findChild<QLabel*>("taskTitle");
    if (titleLabel) {
        titleLabel->setText("No Active Task");
    }

    // Clear task content
    QWidget* taskContent = taskWidget->findChild<QWidget*>("taskContent");
    if (taskContent) {
        QLayout* oldLayout = taskContent->layout();
        if (oldLayout) {
            QLayoutItem* item;
            while ((item = oldLayout->takeAt(0)) != nullptr) {
                if (item->widget()) {
                    item->widget()->deleteLater();
                }
                delete item;
            }
            delete oldLayout;
        }

        QVBoxLayout* newLayout = new QVBoxLayout(taskContent);
        newLayout->addWidget(new QLabel("Select an item to edit"));
    }

    returnButton->setVisible(false);
    comboView->setCurrentIndex(0); // Back to Model tab
    int index = comboView->indexOf(taskWidget);
    if (index >= 0) {
        comboView->setTabVisible(index, false);  // Qt 5.15+
    }
}

void MainWindow::updatePropertySheets(std::shared_ptr<FeatureNode> feature) {
    if (!feature) {
        clearPropertySheets();
        return;
    }

    QString viewInfo;
    viewInfo += QString("Type: %1\n").arg(featureTypeToString(feature->type));
    viewInfo += QString("ID: %1\n").arg(feature->id);
    viewInfo += QString("Name: %1\n").arg(feature->name);
    viewInfo += QString("\n");

    if (feature->type == FeatureType::Sketch) {
        auto sketch = std::static_pointer_cast<SketchNode>(feature);
        viewInfo += QString("Plane: %1\n").arg(sketch->plane.getDisplayName());
        viewInfo += QString("Origin: (%1, %2, %3)\n")
                        .arg(sketch->plane.origin.x(), 0, 'f', 2)
                        .arg(sketch->plane.origin.y(), 0, 'f', 2)
                        .arg(sketch->plane.origin.z(), 0, 'f', 2);
        viewInfo += QString("Normal: (%1, %2, %3)\n")
                        .arg(sketch->plane.normal.x(), 0, 'f', 2)
                        .arg(sketch->plane.normal.y(), 0, 'f', 2)
                        .arg(sketch->plane.normal.z(), 0, 'f', 2);
        viewInfo += QString("Entities: %1\n").arg(sketch->entities.size());

        for (int i = 0; i < sketch->entities.size(); ++i) {
            auto& entity = sketch->entities[i];
            if (entity->type == EntityType::Polyline) {
                auto poly = std::dynamic_pointer_cast<PolylineEntity>(entity);
                viewInfo += QString("  [%1] Polyline: %2 points\n").arg(i).arg(poly->points.size());
            }
        }
    }
    else if (feature->type == FeatureType::Extrude) {
        auto extrude = std::static_pointer_cast<ExtrudeNode>(feature);
        viewInfo += QString("Height: %1\n").arg(extrude->height, 0, 'f', 3);
        viewInfo += QString("Direction: (%1, %2, %3)\n")
                        .arg(extrude->direction.x(), 0, 'f', 3)
                        .arg(extrude->direction.y(), 0, 'f', 3)
                        .arg(extrude->direction.z(), 0, 'f', 3);

        if (auto sketch = extrude->sketch.lock()) {
            viewInfo += QString("Base Sketch: %1 (ID: %2)\n").arg(sketch->name).arg(sketch->id);
            viewInfo += QString("Base Plane: %1\n").arg(sketch->plane.getDisplayName());
        }
    }

    viewText->setText(viewInfo);

    // Update Data tab
    dataTable->setRowCount(0);

    int row = 0;
    auto addRow = [this, &row](const QString& prop, const QString& value) {
        dataTable->insertRow(row);
        dataTable->setItem(row, 0, new QTableWidgetItem(prop));
        dataTable->setItem(row, 1, new QTableWidgetItem(value));
        row++;
    };

    addRow("Type", featureTypeToString(feature->type));
    addRow("ID", QString::number(feature->id));
    addRow("Name", feature->name);

    if (feature->type == FeatureType::Sketch) {
        auto sketch = std::static_pointer_cast<SketchNode>(feature);
        addRow("Plane", sketch->plane.getDisplayName());
        addRow("Origin X", QString::number(sketch->plane.origin.x(), 'f', 3));
        addRow("Origin Y", QString::number(sketch->plane.origin.y(), 'f', 3));
        addRow("Origin Z", QString::number(sketch->plane.origin.z(), 'f', 3));
        addRow("Normal X", QString::number(sketch->plane.normal.x(), 'f', 3));
        addRow("Normal Y", QString::number(sketch->plane.normal.y(), 'f', 3));
        addRow("Normal Z", QString::number(sketch->plane.normal.z(), 'f', 3));
        addRow("Entity Count", QString::number(sketch->entities.size()));
    }
    else if (feature->type == FeatureType::Extrude) {
        auto extrude = std::static_pointer_cast<ExtrudeNode>(feature);
        addRow("Height", QString::number(extrude->height, 'f', 3));
        addRow("Direction X", QString::number(extrude->direction.x(), 'f', 3));
        addRow("Direction Y", QString::number(extrude->direction.y(), 'f', 3));
        addRow("Direction Z", QString::number(extrude->direction.z(), 'f', 3));

        if (auto sketch = extrude->sketch.lock()) {
            addRow("Base Sketch", QString("%1 (ID: %2)").arg(sketch->name).arg(sketch->id));
            addRow("Base Plane", sketch->plane.getDisplayName());
        }
    }

    dataTable->resizeColumnsToContents();
}

void MainWindow::clearPropertySheets() {
    viewText->clear();
    dataTable->setRowCount(0);
}

void MainWindow::onTreeContextMenu(const QPoint& pos) {
    QTreeWidgetItem* item = featureTree->itemAt(pos);
    if (!item) return;

    contextMenuItem = item;
    int featureId = item->data(0, Qt::UserRole).toInt();
    auto f = m_view->doc.findFeature(featureId);

    if (!f) return;

    QMenu contextMenu;

    if (f->type == FeatureType::Sketch) {
        QAction* editAction = contextMenu.addAction(QIcon(":/icons/sketch.png"), "Edit Sketch");
        connect(editAction, &QAction::triggered, this, &MainWindow::onEditSketch);

        contextMenu.addSeparator();

        QAction* visAction = contextMenu.addAction(
            m_view->isSketchVisible(f->id) ? "Hide" : "Show");
        connect(visAction, &QAction::triggered, this, [this, f]() {
            m_view->toggleSketchVisibility(f->id);
            updateFeatureTree();
        });
    }
    else if (f->type == FeatureType::Extrude) {
        QAction* editAction = contextMenu.addAction(QIcon(":/icons/extrude.png"), "Edit Extrude");
        connect(editAction, &QAction::triggered, this, &MainWindow::onEditExtrude);

        contextMenu.addSeparator();

        QAction* visAction = contextMenu.addAction(
            m_view->isFeatureVisible(f->id) ? "Hide" : "Show");
        connect(visAction, &QAction::triggered, this, [this, f]() {
            m_view->toggleFeatureVisibility(f->id);
            updateFeatureTree();
        });

        auto extrude = std::static_pointer_cast<ExtrudeNode>(f);
        if (auto sketch = extrude->sketch.lock()) {
            contextMenu.addSeparator();
            QAction* editSketchAction = contextMenu.addAction(
                QIcon(":/icons/sketch.png"), "Edit Parent Sketch");
            connect(editSketchAction, &QAction::triggered, this, [this, sketch]() {
                m_view->setActiveSketch(sketch);
                showTaskWidget(QString("Editing: %1").arg(sketch->name));
            });
        }
    }

    contextMenu.exec(featureTree->viewport()->mapToGlobal(pos));
}

void MainWindow::onEditSketch() {
    if (!contextMenuItem) return;

    int featureId = contextMenuItem->data(0, Qt::UserRole).toInt();
    auto f = m_view->doc.findFeature(featureId);

    if (f && f->type == FeatureType::Sketch) {
        auto sketch = std::static_pointer_cast<SketchNode>(f);
        m_view->setActiveSketch(sketch);
        showTaskWidget(QString("Editing: %1").arg(sketch->name));

        QWidget* taskContent = taskWidget->findChild<QWidget*>("taskContent");
        if (taskContent) {
            QLayout* oldLayout = taskContent->layout();
            if (oldLayout) {
                QLayoutItem* item;
                while ((item = oldLayout->takeAt(0)) != nullptr) {
                    if (item->widget()) item->widget()->deleteLater();
                    delete item;
                }
                delete oldLayout;
            }

            QVBoxLayout* layout = new QVBoxLayout(taskContent);

            QLabel* infoLabel = new QLabel(QString("Plane: %1\nOrigin: (%2, %3, %4)\nEntities: %5")
                                               .arg(sketch->plane.getDisplayName())
                                               .arg(sketch->plane.origin.x(), 0, 'f', 2)
                                               .arg(sketch->plane.origin.y(), 0, 'f', 2)
                                               .arg(sketch->plane.origin.z(), 0, 'f', 2)
                                               .arg(sketch->entities.size()));
            layout->addWidget(infoLabel);

            layout->addWidget(new QLabel("\nSketch Tools:"));

            QPushButton* rectBtn = new QPushButton("Draw Rectangle");
            connect(rectBtn, &QPushButton::clicked, this, &MainWindow::onDrawRectangle);
            layout->addWidget(rectBtn);

            layout->addStretch();
        }
    }
}

void MainWindow::onEditExtrude() {
    if (!contextMenuItem) return;

    int featureId = contextMenuItem->data(0, Qt::UserRole).toInt();
    auto f = m_view->doc.findFeature(featureId);

    if (f && f->type == FeatureType::Extrude) {
        auto extrude = std::static_pointer_cast<ExtrudeNode>(f);
        showTaskWidget(QString("Editing: %1").arg(extrude->name));

        // Update task content
        QWidget* taskContent = taskWidget->findChild<QWidget*>("taskContent");
        if (taskContent) {
            QLayout* oldLayout = taskContent->layout();
            if (oldLayout) {
                QLayoutItem* item;
                while ((item = oldLayout->takeAt(0)) != nullptr) {
                    if (item->widget()) item->widget()->deleteLater();
                    delete item;
                }
                delete oldLayout;
            }

            QVBoxLayout* layout = new QVBoxLayout(taskContent);

            QLabel* heightLabel = new QLabel("Height:");
            layout->addWidget(heightLabel);

            QDoubleSpinBox* heightSpin = new QDoubleSpinBox();
            heightSpin->setRange(0.1, 1000.0);
            heightSpin->setValue(extrude->height);
            heightSpin->setSingleStep(0.1);
            heightSpin->setDecimals(3);

            connect(heightSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                    this, [this, extrude](double value) {
                        extrude->height = value;
                        extrude->evaluate();
                        m_view->update();
                        updatePropertySheets(extrude);
                    });

            layout->addWidget(heightSpin);
            layout->addStretch();
        }

        m_view->highlightFeature(extrude->id);
        updatePropertySheets(extrude);
    }
}

void MainWindow::onReturnFromSketch() {
    if (m_view) {
        m_view->exitSketchEdit();
        hideTaskWidget();
        clearPropertySheets();
        updateFeatureTree();
    }
}

void MainWindow::registerCommand(const QString& name, const QString& alias, std::function<void()> func) {
    CommandEntry entry;
    entry.name = name;
    entry.alias = alias;
    entry.func = func;
    commands.append(entry);
}

bool MainWindow::executeRegisteredCommand(const QString& cmdName) {
    QString lower = cmdName.toLower();

    for (const auto& cmd : commands) {
        if (cmd.name == lower || (!cmd.alias.isEmpty() && cmd.alias == lower)) {
            cmd.func();
            return true;
        }
    }
    return false;
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

void MainWindow::onToggleObjectSnap() {
    if (!m_view) return;

    bool enabled = !m_view->isObjectSnapEnabled();
    m_view->setObjectSnapEnabled(enabled);

    QString msg = enabled ? "Object snap: ON" : "Object snap: OFF";
    showResultTemporarily(msg);
    consoleOutput->appendPlainText(msg);
}

void MainWindow::initializeCADCommands() {
    // Commands are now registered from menu.txt in loadMenuConfig()
    // Only add special handlers that need argument processing here

    // Override RECTANGLE with argument handling
    if (cadCommands.contains("rectangle")) {
        CADCommand& cmd = cadCommands["rectangle"];
        cmd.expectedArgs = 2;
        cmd.description = "Draw a rectangle by specifying two opposite corners";
        cmd.handler = [this](const QStringList& args) {
            if (args.isEmpty()) {
                onDrawRectangle();
                return;
            }

            if (args.size() >= 2) {
                QVector2D pt1 = parseLispPoint(args[0]);
                QVector2D pt2 = parseLispPoint(args[1]);

                consoleOutput->appendPlainText(
                    QString("Specify first corner: (%1, %2)")
                        .arg(pt1.x(), 0, 'f', 3).arg(pt1.y(), 0, 'f', 3)
                    );
                consoleOutput->appendPlainText(
                    QString("Specify opposite corner: (%1, %2)")
                        .arg(pt2.x(), 0, 'f', 3).arg(pt2.y(), 0, 'f', 3)
                    );

                drawRectangleDirect(pt1, pt2);
            } else if (args.size() == 1) {
                QVector2D pt1 = parseLispPoint(args[0]);
                consoleOutput->appendPlainText(
                    QString("Specify first corner: (%1, %2)")
                        .arg(pt1.x(), 0, 'f', 3).arg(pt1.y(), 0, 'f', 3)
                    );
                startRectangleWithFirstPoint(pt1);
            }
        };

        // Also register for alias
        if (cadCommands.contains("rect")) {
            cadCommands["rect"] = cmd;
        }
    }

    // Object snap toggle command
    registerCADCommand(
        "osnap",
        QStringList() << "os",
        0,
        "Toggle object snap",
        false,
        "onToggleObjectSnap",
        [this](const QStringList&) {
            onToggleObjectSnap();
        }
        );

    // Escape key handler for clearing selection
    registerCADCommand(
        "esc",
        QStringList() << "escape",
        0,
        "Clear selection",
        false,
        "",
        [this](const QStringList&) {
            if (m_view) {
                m_view->clearSelection();
                consoleOutput->appendPlainText("Selection cleared.");
            }
        }
        );
    // Add more specialized handlers as needed
    // For commands that need argument parsing (line, circle, etc.)
}

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

void MainWindow::executeCADCommand(const QString& name, const QStringList& args) {
    QString lowerName = name.toLower();

    if (!cadCommands.contains(lowerName)) {
        consoleOutput->appendPlainText(QString("Unknown command: %1").arg(name));
        showResultTemporarily(QString("Unknown command: %1").arg(name));
        return;
    }

    const CADCommand& cmd = cadCommands[lowerName];

    // Execute handler
    cmd.handler(args);
}

QStringList MainWindow::parseLispList(const QString& str) {
    QStringList result;

    // Use ECL to parse the Lisp expression
    QByteArray bytes = str.toUtf8();
    const char* cstr = bytes.constData();

    cl_object form = Cnil;
#ifdef _MSC_VER
    form = c_string_to_object(cstr);
#else
    CL_CATCH_ALL_BEGIN(ecl_process_env()) {
        form = c_string_to_object(cstr);
    } CL_CATCH_ALL_IF_CAUGHT {
        qWarning() << "Failed to parse Lisp expression:" << str;
        return result;
    } CL_CATCH_ALL_END;
#endif
    if (form == Cnil || form == NULL) {
        return result;
    }

    // Check if it's a list starting with 'command
    if (ECL_LISTP(form) && form != Cnil) {
        cl_object first = cl_car(form);

        // Check if first element is 'command or "command"
        QString firstStr = eclObjectToQString(first).toLower();
        if (firstStr == "command") {
            // Get rest of arguments
            cl_object rest = cl_cdr(form);

            while (rest != Cnil && ECL_LISTP(rest)) {
                cl_object item = cl_car(rest);
                QString itemStr = eclObjectToQString(item);
                result << itemStr;
                rest = cl_cdr(rest);
            }
        }
    }

    return result;
}

QVector2D MainWindow::parseLispPoint(const QString& str) {
    QString cleaned = str.trimmed();

    // Try parsing as Lisp list first using ECL
    if (cleaned.startsWith("(") || cleaned.startsWith("'(")) {
        QString expr = cleaned;
        if (expr.startsWith("'(")) {
            expr = expr.mid(1);  // Remove the leading quote
        }

        QByteArray bytes = expr.toUtf8();
        const char* cstr = bytes.constData();

        cl_object form = Cnil;

#ifdef _MSC_VER
        form = c_string_to_object(cstr);
#else
        CL_CATCH_ALL_BEGIN(ecl_process_env()) {
            form = c_string_to_object(cstr);
        } CL_CATCH_ALL_IF_CAUGHT {
            form = Cnil;
        } CL_CATCH_ALL_END;
#endif
        if (form != Cnil && form != NULL && ECL_LISTP(form)) {
            cl_object x_obj = cl_car(form);
            cl_object y_obj = cl_cadr(form);

            double x = 0.0f;
            double y = 0.0f;

            if (cl_realp(x_obj)) x = ecl_to_double(x_obj);
            if (cl_realp(y_obj)) y = ecl_to_double(y_obj);

            return QVector2D(x, y);
        }
    }

    // Fall back to string parsing for formats like "0,0" or "0 0"
    cleaned.remove("'(").remove("(").remove(")").remove("\"");
    cleaned.replace(",", " ");
    cleaned = cleaned.simplified();

    QStringList parts = cleaned.split(' ', Qt::SkipEmptyParts);

    if (parts.size() >= 2) {
        bool okX, okY;
        float x = parts[0].toFloat(&okX);
        float y = parts[1].toFloat(&okY);

        if (okX && okY) {
            return QVector2D(x, y);
        }
    }

    return QVector2D(0, 0);
}
void MainWindow::createMenusAndToolbars() {
    // Create toolbar first
    QToolBar* tb = addToolBar(tr("Main"));
    tb->setObjectName("MainToolbar");

    // Load menu/toolbar configuration
    loadMenuConfig("menu.txt");
}

void MainWindow::onToggleSketchVisibility() {
    QTreeWidgetItem* item = featureTree->currentItem();
    if (!item) return;

    int sketchId = item->data(0, Qt::UserRole).toInt();
    m_view->toggleSketchVisibility(sketchId);
    updateFeatureTree();
}

void MainWindow::updateFeatureTree() {
    featureTree->clear();

    QTreeWidgetItem* sketchesRoot = new QTreeWidgetItem(featureTree);
    sketchesRoot->setText(0, "Free Sketches");
    sketchesRoot->setExpanded(true);

    QTreeWidgetItem* featuresRoot = new QTreeWidgetItem(featureTree);
    featuresRoot->setText(0, "Features");
    featuresRoot->setExpanded(true);

    QSet<int> usedSketchIds;

    for (auto& f : m_view->doc.features) {
        if (f->type == FeatureType::Extrude) {
            auto extrude = std::static_pointer_cast<ExtrudeNode>(f);
            if (auto s = extrude->sketch.lock()) {
                usedSketchIds.insert(s->id);
            }
        }
    }

    // Add free sketches
    for (auto& s : m_view->doc.sketches) {
        if (!usedSketchIds.contains(s->id)) {
            QTreeWidgetItem* item = new QTreeWidgetItem(sketchesRoot);

            QWidget* widget = new QWidget();
            QHBoxLayout* layout = new QHBoxLayout(widget);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->setSpacing(2);

            QPushButton* eyeBtn = new QPushButton();
            eyeBtn->setFlat(true);
            eyeBtn->setMaximumSize(16, 16);
            bool visible = m_view->isSketchVisible(s->id);
            eyeBtn->setIcon(QIcon(visible ? ":/icons/eyeOpen.png" : ":/icons/eyeClose.png"));

            // Capture sketch ID by value
            int sketchId = s->id;
            connect(eyeBtn, &QPushButton::clicked, this, [this, sketchId]() {
                m_view->toggleSketchVisibility(sketchId);
                m_view->update();
                updateFeatureTree();
            });

            QLabel* icon = new QLabel();
            icon->setPixmap(QIcon(":/icons/sketch.png").pixmap(16, 16));

            QLabel* text = new QLabel(s->name.isEmpty() ?
                                          QString("Sketch %1").arg(s->id) : s->name);

            layout->addWidget(eyeBtn);
            layout->addWidget(icon);
            layout->addWidget(text);
            layout->addStretch();

            item->setData(0, Qt::UserRole, s->id);
            featureTree->setItemWidget(item, 0, widget);
        }
    }

    // Build feature tree
    for (auto& f : m_view->doc.features) {
        QTreeWidgetItem* featureItem = new QTreeWidgetItem(featuresRoot);
        featureItem->setData(0, Qt::UserRole, f->id);

        QWidget* featureWidget = new QWidget();
        QHBoxLayout* featureLayout = new QHBoxLayout(featureWidget);
        featureLayout->setContentsMargins(0, 0, 0, 0);
        featureLayout->setSpacing(2);

        QPushButton* featureEyeBtn = new QPushButton();
        featureEyeBtn->setFlat(true);
        featureEyeBtn->setMaximumSize(16, 16);
        bool featureVisible = m_view->isFeatureVisible(f->id);
        featureEyeBtn->setIcon(QIcon(featureVisible ?
                                         ":/icons/eyeOpen.png" : ":/icons/eyeClose.png"));

        // Capture feature ID by value
        int featureId = f->id;
        connect(featureEyeBtn, &QPushButton::clicked, this, [this, featureId]() {
            m_view->toggleFeatureVisibility(featureId);
            m_view->update();
            updateFeatureTree();
        });

        QLabel* featureIcon = new QLabel();
        QLabel* featureText = new QLabel(f->name.isEmpty() ?
                                             QString("%1 %2").arg(featureTypeToString(f->type)).arg(f->id) : f->name);

        if (f->type == FeatureType::Extrude) {
            featureIcon->setPixmap(QIcon(":/icons/extrude.png").pixmap(16, 16));

            auto extrude = std::static_pointer_cast<ExtrudeNode>(f);
            if (auto s = extrude->sketch.lock()) {
                QTreeWidgetItem* sketchChild = new QTreeWidgetItem(featureItem);

                QWidget* widget = new QWidget();
                QHBoxLayout* layout = new QHBoxLayout(widget);
                layout->setContentsMargins(20, 0, 0, 0);
                layout->setSpacing(2);

                QPushButton* eyeBtn = new QPushButton();
                eyeBtn->setFlat(true);
                eyeBtn->setMaximumSize(16, 16);
                bool sketchVisible = m_view->isSketchVisible(s->id);
                eyeBtn->setIcon(QIcon(sketchVisible ?
                                          ":/icons/eyeOpen.png" : ":/icons/eyeClose.png"));

                // Capture sketch ID by value
                int sketchId = s->id;
                connect(eyeBtn, &QPushButton::clicked, this, [this, sketchId]() {
                    m_view->toggleSketchVisibility(sketchId);
                    m_view->update();
                    updateFeatureTree();
                });

                QLabel* icon = new QLabel();
                icon->setPixmap(QIcon(":/icons/sketch.png").pixmap(16, 16));

                QLabel* text = new QLabel(QString("%1").arg(
                    s->name.isEmpty() ? QString("Sketch %1").arg(s->id) : s->name));
                text->setStyleSheet("color: rgb(100, 150, 200);");

                layout->addWidget(eyeBtn);
                layout->addWidget(icon);
                layout->addWidget(text);
                layout->addStretch();

                sketchChild->setData(0, Qt::UserRole, s->id);
                featureTree->setItemWidget(sketchChild, 0, widget);
            }
        }

        featureLayout->addWidget(featureEyeBtn);
        featureLayout->addWidget(featureIcon);
        featureLayout->addWidget(featureText);
        featureLayout->addStretch();

        featureTree->setItemWidget(featureItem, 0, featureWidget);
        featureItem->setExpanded(true);
    }

    featureTree->expandAll();
}

void MainWindow::onFeatureSelected(QTreeWidgetItem* item, int /*column*/) {
    int featureId = item->data(0, Qt::UserRole).toInt();
    auto f = m_view->doc.findFeature(featureId);

    if (f) {
        m_view->highlightFeature(featureId);
        updatePropertySheets(f);
    } else {
        clearPropertySheets();
    }
}

void MainWindow::onCreateSketch() {
    if (!m_view) return;

    QStringList planes = { "XY (Top)", "XZ (Front)", "YZ (Right)",
                          "XY (Bottom)", "XZ (Back)", "YZ (Left)" ,
                          "Custom (1,1,1) at origin",
                          "Select Face..." };

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

    if (choice == "Select Face...") {
        m_view->startFaceSelectionMode();
        showResultTemporarily("Click on a face to create sketch plane");

        connect(m_view, &CadView::faceSelected, this, [this](CustomPlane plane) {
            disconnect(m_view, &CadView::faceSelected, this, nullptr);

            auto sketch = m_view->doc.createSketch(plane);

            m_view->getCamera().lookAt(
                plane.origin + plane.normal * 10.0f,
                plane.origin,
                plane.vAxis
                );
            m_view->setSketchView(SketchView::None);

            m_view->startSketchMode(sketch);
            updateFeatureTree();

            consoleOutput->appendPlainText(
                QString("Sketch created on face at (%1, %2, %3)")
                    .arg(plane.origin.x(), 0, 'f', 2)
                    .arg(plane.origin.y(), 0, 'f', 2)
                    .arg(plane.origin.z(), 0, 'f', 2)
                );
        }, Qt::UniqueConnection);

        return;
    }

    CustomPlane plane;

    if (choice.startsWith("XY (Top)")) {
        plane = CustomPlane::XY();
        m_view->setSketchView(SketchView::Top);
    } else if (choice.startsWith("XZ (Front)")) {
        plane = CustomPlane::XZ();
        m_view->setSketchView(SketchView::Front);
    } else if (choice.startsWith("YZ (Right)")) {
        plane = CustomPlane::YZ();
        m_view->setSketchView(SketchView::Right);
    } else if (choice.startsWith("XY (Bottom)")) {
        plane = CustomPlane::XY();
        m_view->setSketchView(SketchView::Bottom);
    } else if (choice.startsWith("XZ (Back)")) {
        plane = CustomPlane::XZ();
        m_view->setSketchView(SketchView::Back);
    } else if (choice.startsWith("YZ (Left)")) {
        plane = CustomPlane::YZ();
        m_view->setSketchView(SketchView::Left);
    } else if (choice.startsWith("Custom")) {
        plane = createPlane(QVector3D(0, 0, 0), QVector3D(1, 1, 1));
        m_view->setSketchView(SketchView::None);
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

// Add new method to auto-load files
void MainWindow::autoLoadFiles() {
    // Load Lisp file
    QString lispFile = "../../../my.lsp";
    if (QFile::exists(lispFile)) {
        loadLispFile(lispFile);
        consoleOutput->appendPlainText(QString("Loaded: %1").arg(lispFile));
    } else {
        qWarning() << "Lisp file not found:" << lispFile;
        consoleOutput->appendPlainText(QString("Warning: %1 not found").arg(lispFile));
    }

    // Load CAD file
    QString cadFile = "../../Draw/sample1.cad";
    if (QFile::exists(cadFile)) {
        m_view->doc.loadFromFile(cadFile);
        updateFeatureTree();
        m_view->update();
        consoleOutput->appendPlainText(QString("Loaded: %1").arg(cadFile));
        showResultTemporarily(QString("Loaded: %1").arg(cadFile));
    } else {
        qWarning() << "CAD file not found:" << cadFile;
        consoleOutput->appendPlainText(QString("Warning: %1 not found").arg(cadFile));
    }
}

// Add method to load Lisp files
void MainWindow::loadLispFile(const QString& filename) {
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        consoleOutput->appendPlainText(QString("ERROR: Cannot open %1").arg(filename));
        return;
    }

    QTextStream in(&file);
    QString content = in.readAll();
    file.close();

    // Wrap content in progn to evaluate all forms
    QString wrappedContent = QString("(progn %1)").arg(content);

    // Evaluate the Lisp code
    QByteArray codeBytes = wrappedContent.toUtf8();
    const char* codeStr = codeBytes.constData();

    cl_object result = Cnil;
    bool success = evaluateECLForm(codeStr, &result);

    if (success) {
        consoleOutput->appendPlainText(QString("Lisp file loaded: %1").arg(filename));
    } else {
        consoleOutput->appendPlainText(QString("ERROR loading Lisp file: %1").arg(filename));
    }
}

void MainWindow::loadFileFromCommandLine(const QString& filename) {
    QTimer::singleShot(100, this, [this, filename]() {
        if (filename.endsWith(".cad")) {
            if (QFile::exists(filename)) {
                m_view->doc.loadFromFile(filename);
                updateFeatureTree();
                m_view->update();
                showResultTemporarily(QString("Loaded: %1").arg(filename));
            }
        } else if (filename.endsWith(".lsp") || filename.endsWith(".lisp")) {
            loadLispFile(filename);
        }
    });
}


