#pragma once
#include <QMainWindow>
#include <QStackedWidget>
#include "CadView2D.h"
#include "CadView3D.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();

private slots:
    void toggle2D();
    void toggle3D();

private:
    void createActions();
    void createToolbar();
    void createCentral();

    QAction *m_act2D, *m_act3D;
    QAction *m_actDrawLine, *m_actDrawArc;
    QAction *m_actSave, *m_actLoad;
    QAction *m_actPrint, *m_actExportPdf;

    QStackedWidget *m_stack;
    CadView2D *m_view2d;
    CadView3D *m_view3d;
};
