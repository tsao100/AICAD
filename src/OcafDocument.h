#ifndef OCAFDOCUMENT_H
#define OCAFDOCUMENT_H

#include <TDocStd_Document.hxx>
#include <TDocStd_Application.hxx>
#include <TDF_Label.hxx>
#include <TDataStd_Name.hxx>
#include <TDataStd_Integer.hxx>
#include <TDataStd_Real.hxx>
#include <TNaming_Builder.hxx>
#include <TNaming_NamedShape.hxx>
#include <TPrsStd_AISPresentation.hxx>
#include <XCAFApp_Application.hxx>

#include <TopoDS_Shape.hxx>
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>
#include <gp_Ax2.hxx>
#include <gp_Pln.hxx>

#include <AIS_InteractiveContext.hxx>

#include <QString>
#include <QVector>
#include <QVector2D>
#include <QVector3D>
#include <memory>

enum class FeatureType {
    Sketch,
    Extrude,
    Root
};

struct CustomPlane {
    QVector3D origin;
    QVector3D normal;
    QVector3D uAxis;
    QVector3D vAxis;

    static CustomPlane XY();
    static CustomPlane XZ();
    static CustomPlane YZ();
    QString getDisplayName() const;

    gp_Pln toGpPln() const;
    gp_Ax2 toGpAx2() const;
};

class OcafDocument {
public:
    OcafDocument();
    ~OcafDocument();

    bool newDocument();
    bool saveDocument(const QString& filename);
    bool loadDocument(const QString& filename);

    TDF_Label createSketch(const CustomPlane& plane, const QString& name);
    TDF_Label createExtrude(TDF_Label sketchLabel, double height, const QString& name);

    void addPolylineToSketch(TDF_Label sketchLabel, const QVector<QVector2D>& points);

    TDF_Label getRootLabel() const;
    QVector<TDF_Label> getFeatures() const;

    FeatureType getFeatureType(TDF_Label label) const;
    QString getFeatureName(TDF_Label label) const;
    int getFeatureId(TDF_Label label) const;

    CustomPlane getSketchPlane(TDF_Label sketchLabel) const;
    QVector<QVector<QVector2D>> getSketchPolylines(TDF_Label sketchLabel) const;

    double getExtrudeHeight(TDF_Label extrudeLabel) const;
    TDF_Label getExtrudeSketch(TDF_Label extrudeLabel) const;

    TopoDS_Shape getShape(TDF_Label label) const;
    void setShape(TDF_Label label, const TopoDS_Shape& shape);

    void updateVisualization(TDF_Label label, const Handle(AIS_InteractiveContext)& context);

    Handle(TDocStd_Document) getDocument() const { return m_doc; }

    int getNextFeatureId() { return m_nextFeatureId++; }

private:
    Handle(TDocStd_Application) m_app;
    Handle(TDocStd_Document) m_doc;

    int m_nextFeatureId;

    TDF_Label createFeatureLabel(const QString& name, FeatureType type);
    void savePlaneToLabel(TDF_Label label, const CustomPlane& plane);
    CustomPlane loadPlaneFromLabel(TDF_Label label) const;
};

#endif
