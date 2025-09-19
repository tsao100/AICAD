#pragma once
#include <QMainWindow>
#include "CadView.h"

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
    QAction *m_actTop, *m_actFront, *m_actRight;

    CadView *m_view;
};
