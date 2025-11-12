#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#define QT_NO_KEYWORDS
#define HAVE_ECL

#ifdef HAVE_ECL
#include <ecl/ecl.h>
#endif

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
#include <QStatusBar>
#include <QDebug>

#include "CadView.h"
#include "OcafDocument.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow();
    ~MainWindow();
 //   void loadFileFromCommandLine(const QString& filename);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private Q_SLOTS:
    void executeCommand();
    void fadeOutResult();
    void onPointAcquired(QVector2D point);
    void onGetPointCancelled();

    void onDrawRectangle();
    void onDrawLine();
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

    void onFeatureSelected(QTreeWidgetItem* item, int column);
    void updateFeatureTree();

private:

    bool m_waitingForGetPoint;
    QVector2D m_getPointBase;
    bool m_hasGetPointBase;
    QString m_getPointMessage;
    QVector2D m_getPointResult;
    bool m_getPointCompleted;
    bool m_getPointCancelled;

    static cl_object lisp_getpoint(cl_narg narg, ...);
    void startGetPoint(const QVector2D* basePoint = nullptr, const QString& message = "");

// Unified command system
    struct CADCommand {
        QString name;
        QStringList aliases;
        std::function<void(const QStringList&)> handler;
        int expectedArgs; // -1 = variable, 0+ = fixed count
        QString description;
        bool interactive; // Whether command needs interactive point input
        QString qtSlot; // Qt slot/method name for menu/toolbar binding
    };

    QHash<QString, CADCommand> cadCommands;

    void registerCADCommand(const QString& name,
                            const QStringList& aliases,
                            int expectedArgs,
                            const QString& description,
                            bool interactive,
                            const QString& qtSlot,
                            std::function<void(const QStringList&)> handler);
    void loadMenuConfig(const QString& filename);
    void createMenusAndToolbars();
    void createCentral();
    void createFeatureBrowser();

#ifdef HAVE_ECL
    void initECL();
    void toggleConsole();
    void showResultTemporarily(const QString &result);
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
#endif

    void registerCommand(const QString& name, const QString& alias, std::function<void()> func);

    QTreeWidget* featureTree;
    CadView *m_view;
//    QStatusBar* statusBar;

    OcafDocument m_document;

    TDF_Label m_pendingSketch;
    TDF_Label m_activeSketch;

//    QVector<QVector2D> m_rectanglePoints;
//    bool m_waitingForSecondPoint;

    struct CommandEntry {
        QString name;
        QString alias;
        std::function<void()> func;
    };
    QVector<CommandEntry> commands;
    QHash<QString, QAction*> actions;
    QHash<QString, QMenu*> menus;
};

#endif
