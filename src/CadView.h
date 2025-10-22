#ifndef CADVIEW_H
#define CADVIEW_H

#include <QWidget>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>

#include <AIS_InteractiveContext.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <Aspect_Handle.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <AIS_Shape.hxx>
#include <AIS_ViewCube.hxx>

#include "OcafDocument.h"

#include <QVector2D>
#include <QVector3D>
#include <QPoint>

enum class SketchView {
    None,
    Top,
    Bottom,
    Front,
    Back,
    Right,
    Left,
    Isometric
};

enum class CadMode {
    Idle,
    Sketching,
    Extruding,
    SelectingFace
};

enum class RubberBandMode {
    None,
    Line,
    Rectangle,
    Polyline
};

class OcafDocument;

class CadView : public QWidget {
    Q_OBJECT

public:
    explicit CadView(QWidget* parent = nullptr);
    ~CadView();

    void setDocument(OcafDocument* doc);
    void setSketchView(SketchView view);
    void refreshView();

    Handle(AIS_InteractiveContext) getContext() const { return m_context; }
    Handle(V3d_View) getView() const { return m_view; }

    void displayAllFeatures();
    void displayFeature(TDF_Label label);
    void highlightFeature(int featureId);

    void setMode(CadMode mode) { m_mode = mode; }
    CadMode getMode() const { return m_mode; }

    void setRubberBandMode(RubberBandMode mode);
    void setPendingSketch(TDF_Label sketch);

    QVector2D screenToPlane(const QPoint& screenPos);

    SketchView getCurrentView() const { return m_currentView; }

Q_SIGNALS:
    void pointAcquired(QVector2D point);
    void getPointCancelled();
    void getPointKeyPressed(QString key);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

    QPaintEngine* paintEngine() const override { return nullptr; }

private:
    void initializeViewer();
    void fitAll();

    TopoDS_Shape createPolylineShape(const QVector<QVector2D>& points, const CustomPlane& plane);
    TopoDS_Shape createExtrudeShape(TDF_Label sketchLabel, double height);

    Handle(AIS_InteractiveContext) m_context;
    Handle(V3d_View) m_view;
    Handle(V3d_Viewer) m_viewer;

    OcafDocument* m_document;

    SketchView m_currentView;
    CadMode m_mode;
    RubberBandMode m_rubberBandMode;

    TDF_Label m_pendingSketch;

    QPoint m_lastMousePos;
    bool m_mousePressed;
    Qt::MouseButton m_pressedButton;

    QVector<QVector2D> m_sketchPoints;
    QVector2D m_currentPoint;
    bool m_hasCurrentPoint;

    Handle(AIS_ViewCube) m_viewCube;
};

#endif
