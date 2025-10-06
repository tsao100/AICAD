#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#define QT_NO_KEYWORDS

// CRITICAL ORDER:
// 1. Include ECL first (before any Qt headers)
// 2. Undefine 'slots' macro to avoid conflict with ECL's object.h
// 3. Include Qt headers
// 4. Include other project headers

#include <ecl/ecl.h>


// Now safely include Qt headers
#include <QMainWindow>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QDockWidget>
#include <QTreeWidget>
#include <QToolBar>
#include <QAction>
#include <QMenuBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>

#include "CadView.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

    // Command registry structure
    struct CommandEntry {
        QString name;           // Primary command name
        QString alias;          // Optional alias
        QString callback;       // Method name to call
        std::function<void()> func; // Actual function pointer
    };

public:
    MainWindow();
    ~MainWindow();

    bool isCommandInputEmpty() const;

protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private Q_SLOTS:
    void executeCommand();
    void fadeOutResult();
    void onPointAcquired(QVector2D point);
    void onGetPointCancelled();
    void onGetPointKeyPressed(QString key);
    void updateGetPointFocus();

    // CAD command handlers
    void onDrawRectangle();
    void onDrawLine();
    void onDrawArc();
    void onDrawCircle();
    void onCreateSketch();
    void onCreateExtrude();
    void onSave();
    void onLoad();
    void onPrint();
    void onExportPdf();
    void onViewTop();
    void onViewFront();
    void onViewRight();
    void onViewIsometric();
    void onExit();

private:
    // CAD UI
    void createMenusAndToolbars();
    void createCentral();
    void createFeatureBrowser();

    // Command system
    void loadMenuConfig(const QString& filename);
    void registerCommand(const QString& name, const QString& alias, std::function<void()> func);
    bool executeRegisteredCommand(const QString& cmdName);

    void updateFeatureTree();
    void onFeatureSelected(QTreeWidgetItem* item, int column);
    void createRectangleEntity(std::shared_ptr<SketchNode> sketch,
                                const QVector2D& corner1,
                               const QVector2D& corner2);

    // Helper methods for rectangle command
    QVector2D parsePoint(const QString& ptStr);
    void startRectangleWithFirstPoint(const QVector2D& pt1);
    void drawRectangleDirect(const QVector2D& pt1, const QVector2D& pt2);

    // Command registry
    QVector<CommandEntry> commands;
    QHash<QString, QAction*> actions;
    QHash<QString, QMenu*> menus;

    QTreeWidget* featureTree;
    CadView *m_view;

    // ECL/Lisp integration
    void initECL();
    void toggleConsole();
    void showResultTemporarily(const QString &result);
    void defineCADCommands();
    void setPrompt(const QString &prompt);

    QPlainTextEdit *consoleOutput;
    QLineEdit *commandInput;
    QPushButton *toggleButton;
    QLabel *resultLabel;
    QTimer *fadeTimer;
    QGraphicsOpacityEffect *opacityEffect;
    QGraphicsOpacityEffect *resultOpacityEffect;
    QPropertyAnimation *fadeAnimation;
    QPropertyAnimation *resultFadeAnimation;

    QStringList commandHistory;
    int historyIndex;
    bool consoleVisible;
    QString promptText;
    int promptLength;

    // GetPoint state management
    struct GetPointRequest {
        bool active = false;
        QString prompt;
        bool hasPreviousPoint = false;
        QVector2D previousPoint;
        std::function<void(QVector2D)> callback;
        std::function<void(QVector2D)> pendingCallback;
    };
    GetPointRequest currentGetPointRequest;

    void setupGetPointECLInterface();
};
#endif // MAINWINDOW_H
