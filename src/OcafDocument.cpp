#include "OcafDocument.h"
#include <TDataStd_TreeNode.hxx>
#include <TDataStd_RealArray.hxx>
#include <TDataStd_IntegerArray.hxx>
#include <TDF_ChildIterator.hxx>
#include <BinDrivers.hxx>
#include <QFile>

static const Standard_GUID GUID_FEATURE_TYPE("12345678-1234-1234-1234-000000000001");
static const Standard_GUID GUID_FEATURE_ID("12345678-1234-1234-1234-000000000002");
static const Standard_GUID GUID_PLANE_ORIGIN("12345678-1234-1234-1234-000000000003");
static const Standard_GUID GUID_PLANE_NORMAL("12345678-1234-1234-1234-000000000004");
static const Standard_GUID GUID_PLANE_UAXIS("12345678-1234-1234-1234-000000000005");
static const Standard_GUID GUID_PLANE_VAXIS("12345678-1234-1234-1234-000000000006");
static const Standard_GUID GUID_EXTRUDE_HEIGHT("12345678-1234-1234-1234-000000000007");
static const Standard_GUID GUID_EXTRUDE_SKETCH("12345678-1234-1234-1234-000000000008");
static const Standard_GUID GUID_POLYLINES("12345678-1234-1234-1234-000000000009");

CustomPlane CustomPlane::XY() {
    CustomPlane p;
    p.origin = QVector3D(0, 0, 0);
    p.normal = QVector3D(0, 0, 1);
    p.uAxis = QVector3D(1, 0, 0);
    p.vAxis = QVector3D(0, 1, 0);
    return p;
}

CustomPlane CustomPlane::XZ() {
    CustomPlane p;
    p.origin = QVector3D(0, 0, 0);
    p.normal = QVector3D(0, 1, 0);
    p.uAxis = QVector3D(1, 0, 0);
    p.vAxis = QVector3D(0, 0, 1);
    return p;
}

CustomPlane CustomPlane::YZ() {
    CustomPlane p;
    p.origin = QVector3D(0, 0, 0);
    p.normal = QVector3D(1, 0, 0);
    p.uAxis = QVector3D(0, 1, 0);
    p.vAxis = QVector3D(0, 0, 1);
    return p;
}

QString CustomPlane::getDisplayName() const {
    if (normal == QVector3D(0, 0, 1) && origin == QVector3D(0, 0, 0))
        return "XY";
    if (normal == QVector3D(0, 1, 0) && origin == QVector3D(0, 0, 0))
        return "XZ";
    if (normal == QVector3D(1, 0, 0) && origin == QVector3D(0, 0, 0))
        return "YZ";
    return QString("Custom (%1, %2, %3)")
        .arg(normal.x(), 0, 'f', 2)
        .arg(normal.y(), 0, 'f', 2)
        .arg(normal.z(), 0, 'f', 2);
}

gp_Pln CustomPlane::toGpPln() const {
    gp_Pnt origin_pnt(origin.x(), origin.y(), origin.z());
    gp_Dir normal_dir(normal.x(), normal.y(), normal.z());
    return gp_Pln(origin_pnt, normal_dir);
}

gp_Ax2 CustomPlane::toGpAx2() const {
    gp_Pnt origin_pnt(origin.x(), origin.y(), origin.z());
    gp_Dir normal_dir(normal.x(), normal.y(), normal.z());
    gp_Dir uaxis_dir(uAxis.x(), uAxis.y(), uAxis.z());
    return gp_Ax2(origin_pnt, normal_dir, uaxis_dir);
}

OcafDocument::OcafDocument() : m_nextFeatureId(1) {
    m_app = XCAFApp_Application::GetApplication();
    BinDrivers::DefineFormat(m_app);
}

OcafDocument::~OcafDocument() {
    if (!m_doc.IsNull()) {
        m_app->Close(m_doc);
    }
}

bool OcafDocument::newDocument() {
    if (!m_doc.IsNull()) {
        m_app->Close(m_doc);
    }
    m_app->NewDocument("BinOcaf", m_doc);
    m_nextFeatureId = 1;
    return !m_doc.IsNull();
}

bool OcafDocument::saveDocument(const QString& filename) {
    if (m_doc.IsNull()) return false;

    TCollection_ExtendedString path(filename.toStdWString().c_str());
    return m_app->SaveAs(m_doc, path) == PCDM_SS_OK;
}

bool OcafDocument::loadDocument(const QString& filename) {
    if (!QFile::exists(filename)) return false;

    if (!m_doc.IsNull()) {
        m_app->Close(m_doc);
    }

    TCollection_ExtendedString path(filename.toStdWString().c_str());
    PCDM_ReaderStatus status = m_app->Open(path, m_doc);

    if (status != PCDM_RS_OK) return false;

    int maxId = 0;
    for (TDF_ChildIterator it(getRootLabel()); it.More(); it.Next()) {
        int id = getFeatureId(it.Value());
        if (id > maxId) maxId = id;
    }
    m_nextFeatureId = maxId + 1;

    return true;
}

TDF_Label OcafDocument::getRootLabel() const {
    if (m_doc.IsNull()) return TDF_Label();
    return m_doc->Main();
}

TDF_Label OcafDocument::createFeatureLabel(const QString& name, FeatureType type) {
    TDF_Label root = getRootLabel();
    TDF_Label newLabel = TDF_TagSource::NewChild(root);

    TDataStd_Name::Set(newLabel, TCollection_ExtendedString(name.toStdWString().c_str()));
    TDataStd_Integer::Set(newLabel, GUID_FEATURE_TYPE, static_cast<int>(type));
    TDataStd_Integer::Set(newLabel, GUID_FEATURE_ID, getNextFeatureId());

    return newLabel;
}

TDF_Label OcafDocument::createSketch(const CustomPlane& plane, const QString& name) {
    TDF_Label sketchLabel = createFeatureLabel(name, FeatureType::Sketch);
    savePlaneToLabel(sketchLabel, plane);
    return sketchLabel;
}

void OcafDocument::savePlaneToLabel(TDF_Label label, const CustomPlane& plane) {
    Handle(TDataStd_RealArray) origin = TDataStd_RealArray::Set(label, GUID_PLANE_ORIGIN, 0, 2);
    origin->SetValue(0, plane.origin.x());
    origin->SetValue(1, plane.origin.y());
    origin->SetValue(2, plane.origin.z());

    Handle(TDataStd_RealArray) normal = TDataStd_RealArray::Set(label, GUID_PLANE_NORMAL, 0, 2);
    normal->SetValue(0, plane.normal.x());
    normal->SetValue(1, plane.normal.y());
    normal->SetValue(2, plane.normal.z());

    Handle(TDataStd_RealArray) uaxis = TDataStd_RealArray::Set(label, GUID_PLANE_UAXIS, 0, 2);
    uaxis->SetValue(0, plane.uAxis.x());
    uaxis->SetValue(1, plane.uAxis.y());
    uaxis->SetValue(2, plane.uAxis.z());

    Handle(TDataStd_RealArray) vaxis = TDataStd_RealArray::Set(label, GUID_PLANE_VAXIS, 0, 2);
    vaxis->SetValue(0, plane.vAxis.x());
    vaxis->SetValue(1, plane.vAxis.y());
    vaxis->SetValue(2, plane.vAxis.z());
}

CustomPlane OcafDocument::loadPlaneFromLabel(TDF_Label label) const {
    CustomPlane plane;

    Handle(TDataStd_RealArray) origin;
    if (label.FindAttribute(GUID_PLANE_ORIGIN, origin)) {
        plane.origin = QVector3D(origin->Value(0), origin->Value(1), origin->Value(2));
    }

    Handle(TDataStd_RealArray) normal;
    if (label.FindAttribute(GUID_PLANE_NORMAL, normal)) {
        plane.normal = QVector3D(normal->Value(0), normal->Value(1), normal->Value(2));
    }

    Handle(TDataStd_RealArray) uaxis;
    if (label.FindAttribute(GUID_PLANE_UAXIS, uaxis)) {
        plane.uAxis = QVector3D(uaxis->Value(0), uaxis->Value(1), uaxis->Value(2));
    }

    Handle(TDataStd_RealArray) vaxis;
    if (label.FindAttribute(GUID_PLANE_VAXIS, vaxis)) {
        plane.vAxis = QVector3D(vaxis->Value(0), vaxis->Value(1), vaxis->Value(2));
    }

    return plane;
}

TDF_Label OcafDocument::createExtrude(TDF_Label sketchLabel, double height, const QString& name) {
    TDF_Label extrudeLabel = createFeatureLabel(name, FeatureType::Extrude);

    TDataStd_Real::Set(extrudeLabel, GUID_EXTRUDE_HEIGHT, height);
    TDataStd_Integer::Set(extrudeLabel, GUID_EXTRUDE_SKETCH, getFeatureId(sketchLabel));

    return extrudeLabel;
}

void OcafDocument::addPolylineToSketch(TDF_Label sketchLabel, const QVector<QVector2D>& points) {
    TDF_Label polylineLabel = TDF_TagSource::NewChild(sketchLabel);

    int numPoints = points.size();
    Handle(TDataStd_RealArray) coords = TDataStd_RealArray::Set(
        polylineLabel, GUID_POLYLINES, 0, numPoints * 2 - 1);

    for (int i = 0; i < numPoints; ++i) {
        coords->SetValue(i * 2, points[i].x());
        coords->SetValue(i * 2 + 1, points[i].y());
    }
}

QVector<TDF_Label> OcafDocument::getFeatures() const {
    QVector<TDF_Label> features;
    TDF_Label root = getRootLabel();

    for (TDF_ChildIterator it(root); it.More(); it.Next()) {
        features.append(it.Value());
    }

    return features;
}

FeatureType OcafDocument::getFeatureType(TDF_Label label) const {
    Handle(TDataStd_Integer) typeAttr;
    if (label.FindAttribute(GUID_FEATURE_TYPE, typeAttr)) {
        return static_cast<FeatureType>(typeAttr->Get());
    }
    return FeatureType::Root;
}

QString OcafDocument::getFeatureName(TDF_Label label) const {
    Handle(TDataStd_Name) nameAttr;
    if (label.FindAttribute(TDataStd_Name::GetID(), nameAttr)) {
        TCollection_ExtendedString extStr = nameAttr->Get();
        return QString::fromUtf16(reinterpret_cast<const char16_t*>(extStr.ToExtString()));
    }
    return "Unnamed";
}

int OcafDocument::getFeatureId(TDF_Label label) const {
    Handle(TDataStd_Integer) idAttr;
    if (label.FindAttribute(GUID_FEATURE_ID, idAttr)) {
        return idAttr->Get();
    }
    return -1;
}

CustomPlane OcafDocument::getSketchPlane(TDF_Label sketchLabel) const {
    return loadPlaneFromLabel(sketchLabel);
}

QVector<QVector<QVector2D>> OcafDocument::getSketchPolylines(TDF_Label sketchLabel) const {
    QVector<QVector<QVector2D>> polylines;

    for (TDF_ChildIterator it(sketchLabel); it.More(); it.Next()) {
        TDF_Label polylineLabel = it.Value();
        Handle(TDataStd_RealArray) coords;

        if (polylineLabel.FindAttribute(GUID_POLYLINES, coords)) {
            QVector<QVector2D> points;
            int numCoords = coords->Upper() - coords->Lower() + 1;
            int numPoints = numCoords / 2;

            for (int i = 0; i < numPoints; ++i) {
                double x = coords->Value(i * 2);
                double y = coords->Value(i * 2 + 1);
                points.append(QVector2D(x, y));
            }

            polylines.append(points);
        }
    }

    return polylines;
}

double OcafDocument::getExtrudeHeight(TDF_Label extrudeLabel) const {
    Handle(TDataStd_Real) heightAttr;
    if (extrudeLabel.FindAttribute(GUID_EXTRUDE_HEIGHT, heightAttr)) {
        return heightAttr->Get();
    }
    return 0.0;
}

TDF_Label OcafDocument::getExtrudeSketch(TDF_Label extrudeLabel) const {
    Handle(TDataStd_Integer) sketchIdAttr;
    if (extrudeLabel.FindAttribute(GUID_EXTRUDE_SKETCH, sketchIdAttr)) {
        int sketchId = sketchIdAttr->Get();

        for (TDF_ChildIterator it(getRootLabel()); it.More(); it.Next()) {
            if (getFeatureId(it.Value()) == sketchId) {
                return it.Value();
            }
        }
    }
    return TDF_Label();
}

TopoDS_Shape OcafDocument::getShape(TDF_Label label) const {
    Handle(TNaming_NamedShape) namedShape;
    if (label.FindAttribute(TNaming_NamedShape::GetID(), namedShape)) {
        return namedShape->Get();
    }
    return TopoDS_Shape();
}

void OcafDocument::setShape(TDF_Label label, const TopoDS_Shape& shape) {
    TNaming_Builder builder(label);
    builder.Generated(shape);
}

void OcafDocument::updateVisualization(TDF_Label label, const Handle(AIS_InteractiveContext)& context) {
    if (context.IsNull()) return;

    Handle(TPrsStd_AISPresentation) prs;
    if (!label.FindAttribute(TPrsStd_AISPresentation::GetID(), prs)) {
        prs = TPrsStd_AISPresentation::Set(label, TNaming_NamedShape::GetID());
    }

    prs->Display(Standard_True);
    context->UpdateCurrentViewer();
}
