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
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>

#include "CadView.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    ~MainWindow();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private Q_SLOTS:
    void toggle2D();
    void toggle3D();
    void executeCommand();
    void fadeOutResult();

private:
    // CAD UI
    void createActions();
    void createToolbar();
    void createCentral();
    void createFeatureBrowser();
    void updateFeatureTree();
    void onFeatureSelected(QTreeWidgetItem* item, int column);
    void onCreateSketch();
    void onCreateExtrude();

    QAction *m_act2D, *m_act3D;
    QAction *m_actDrawLine, *m_actDrawArc;
    QAction *m_actSave, *m_actLoad;
    QAction *m_actPrint, *m_actExportPdf;
    QAction *m_actTop, *m_actFront, *m_actRight;
    QAction* actionCreateSketch;
    QAction* actionCreateExtrude;

    QTreeWidget* featureTree;
    CadView *m_view;

    // ECL/Lisp integration
    void initECL();
    void toggleConsole();
    void showResultTemporarily(const QString &result);
    void defineCADCommands();

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
};
#endif // MAINWINDOW_H
