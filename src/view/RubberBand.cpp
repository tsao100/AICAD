/**
 * @file RubberBand.cpp
 * @brief RubberBand 類別實作
 * @author Felicia
 * @date 2024-12-04
 */

#include "RubberBand.h"
#include "cad/Plane.h"
#include "cad/PlaneManager.h"

#include <QDebug>
#include <cmath>
#include <functional>
#include <Graphic3d_ArrayOfPolylines.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Graphic3d_Group.hxx>
#include <Quantity_Color.hxx>
#include <Aspect_TypeOfLine.hxx>
#include <gp_Pnt.hxx>
#include <gp_Circ.hxx>
#include <gp_Elips.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <ElCLib.hxx>
#include <Graphic3d_DisplayPriority.hxx>
#include <Geom_BSplineCurve.hxx>
#include <GeomAPI_Interpolate.hxx>
#include <GeomAdaptor_Curve.hxx>
#include <GCPnts_UniformAbscissa.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <TColgp_HArray1OfPnt.hxx>
#include <gp_Pnt.hxx>
#include <Standard_Failure.hxx>

namespace aicad {
namespace view {

class RubberBand::Private {
public:
    Handle(AIS_InteractiveContext) context;
    Handle(Prs3d_Presentation) presentation;
    
    RubberBandMode mode;
    cad::Plane* plane;
    QVector<QVector2D> points;
    QVector2D currentPoint;
    bool hasCurrentPoint;

    // ── Spiral / SCS parameters ───────────────────────────────────────────────
    double radius        = 400.0;   ///< Circular arc radius [m]; +right, −left
    double spiralLength  = 100.0;   ///< Transition curve length [m] (L1 = L2 symmetric)
    double spiralLength1 = 100.0;   ///< Entry spiral length L1 [m]  (SCS asymmetric)
    double spiralLength2 = 100.0;   ///< Exit  spiral length L2 [m]  (SCS asymmetric)
};

RubberBand::RubberBand(const Handle(AIS_InteractiveContext)& context, QObject* parent)
    : QObject(parent)
    , d(new Private())
{
    d->context = context;
    d->mode = RubberBandMode::None;
    d->hasCurrentPoint = false;
    
    // ✅ 使用 PlaneManager 取得預設平面
    cad::PlaneManager* manager = cad::PlaneManager::instance();
    d->plane = manager->activePlane();

    // 如果沒有活動平面，嘗試取得或建立 XY 平面
    if (!d->plane) {
        QList<cad::Plane*> planes = manager->planes();
        for (cad::Plane* p : planes) {
            if (p->isXY()) {
                d->plane = p;
                break;
            }
        }

        // 還是沒有，建立新的 XY 平面
        if (!d->plane) {
            d->plane = manager->createPlane(cad::Plane::Type::XY, "RubberBandPlane");
        }
    }

    qDebug() << "[RubberBand] Created with plane:"
             << (d->plane ? d->plane->displayName() : "None");
}

RubberBand::~RubberBand() {
    clear();
    delete d;
    qDebug() << "[RubberBand] Destroyed";
}

void RubberBand::setMode(RubberBandMode mode) {
    if (d->mode == mode) {
        return;
    }
    
    d->mode = mode;
    d->points.clear();
    d->hasCurrentPoint = false;
    clear();
    
    qDebug() << "[RubberBand] Mode changed to:" << static_cast<int>(mode);
}

RubberBandMode RubberBand::mode() const {
    return d->mode;
}

void RubberBand::setPlane(cad::Plane* plane) {
    if (!plane) {
        qWarning() << "[RubberBand] Cannot set null plane";
        return;
    }

    d->plane = plane;
    qDebug() << "[RubberBand] Plane set to:" << plane->displayName();
}

cad::Plane* RubberBand::plane() const {
    return d->plane;
}

void RubberBand::addPoint(const QVector2D& point) {
    d->points.append(point);
    qDebug() << "[RubberBand] Point added:" << point.x() << "," << point.y()
             << "Total points:" << d->points.size();
}

void RubberBand::setCurrentPoint(const QVector2D& point) {
    d->currentPoint = point;
    d->hasCurrentPoint = true;
}

QVector<QVector2D> RubberBand::points() const {
    return d->points;
}

void RubberBand::clearPoints() {
    d->points.clear();
    d->hasCurrentPoint = false;
    clear();
}

void RubberBand::setRadius(double r)
{
    d->radius = r;
}

double RubberBand::radius() const
{
    return d->radius;
}

void RubberBand::setSpiralLength(double ls)
{
    d->spiralLength  = ls;
    d->spiralLength1 = ls;   // symmetric: both L1 and L2 follow
    d->spiralLength2 = ls;
}

double RubberBand::spiralLength() const
{
    return d->spiralLength1;
}

void RubberBand::setSpiralLength1(double ls)
{
    d->spiralLength1 = ls;
    d->spiralLength  = ls;   // keep legacy field in sync with L1
}

double RubberBand::spiralLength1() const
{
    return d->spiralLength1;
}

void RubberBand::setSpiralLength2(double ls)
{
    d->spiralLength2 = ls;
}

double RubberBand::spiralLength2() const
{
    return d->spiralLength2;
}

void RubberBand::update() {
    if (d->context.IsNull()) {
        return;
    }
    
    // 清除舊的顯示
    clear();
    
    // 根據模式更新
    switch (d->mode) {
    case RubberBandMode::Line:
        updateLine();
        break;
    case RubberBandMode::Rectangle:
        updateRectangle();
        break;
    case RubberBandMode::Polyline:
        updatePolyline();
        break;
    case RubberBandMode::Polygon:
        updatePolygon();
        break;
    case RubberBandMode::Circle:
        updateCircle();
        break;
    case RubberBandMode::Spline:
        updateSpline();
        break;
    case RubberBandMode::Ellipse:
        updateEllipse();
        break;
    case RubberBandMode::Arc:
        updateArc();
        break;
    case RubberBandMode::Spiral:
        updateSpiral();
        break;
    case RubberBandMode::SCS:
        updateSCS();
        break;
    case RubberBandMode::None:
    default:
        break;
    }
    
    Q_EMIT updated();
}

void RubberBand::clear() {
    if (!d->presentation.IsNull()) {
        d->presentation->Clear();
        d->presentation->Erase();
        d->presentation.Nullify();
    }

    Q_EMIT cleared();
}

bool RubberBand::hasCurrentPoint() const {
    return d->hasCurrentPoint;
}

void RubberBand::updateLine() {
    if (d->points.isEmpty() || !d->hasCurrentPoint) {
        return;
    }
    
    // 創建線段
    Handle(Graphic3d_ArrayOfPolylines) polyline = new Graphic3d_ArrayOfPolylines(2);
    
    QVector3D p1 = planeToWorld(d->points[0]);
    QVector3D p2 = planeToWorld(d->currentPoint);
    
    polyline->AddVertex(gp_Pnt(p1.x(), p1.y(), p1.z()));
    polyline->AddVertex(gp_Pnt(p2.x(), p2.y(), p2.z()));
    
    // 創建呈現
    d->presentation = new Prs3d_Presentation(d->context->MainPrsMgr()->StructureManager());
    
    Handle(Prs3d_LineAspect) aspect = new Prs3d_LineAspect(
        Quantity_NOC_WHITE,
        Aspect_TOL_DASH,
        2.0
    );
    
    Handle(Graphic3d_Group) group = d->presentation->NewGroup();
    group->SetGroupPrimitivesAspect(aspect->Aspect());
    group->AddPrimitiveArray(polyline);
    
    d->presentation->SetZLayer(Graphic3d_ZLayerId_Top);
    d->presentation->SetDisplayPriority(Graphic3d_DisplayPriority_Topmost);
    d->presentation->Display();
    d->context->UpdateCurrentViewer();
}

void RubberBand::updateRectangle() {
    if (d->points.isEmpty() || !d->hasCurrentPoint) {
        return;
    }
    
    // 創建矩形
    QVector2D p1 = d->points[0];
    QVector2D p2 = d->currentPoint;
    
    QVector3D gp1 = planeToWorld(QVector2D(p1.x(), p1.y()));
    QVector3D gp2 = planeToWorld(QVector2D(p2.x(), p1.y()));
    QVector3D gp3 = planeToWorld(QVector2D(p2.x(), p2.y()));
    QVector3D gp4 = planeToWorld(QVector2D(p1.x(), p2.y()));
    
    Handle(Graphic3d_ArrayOfPolylines) polyline = new Graphic3d_ArrayOfPolylines(5);
    polyline->AddVertex(gp_Pnt(gp1.x(), gp1.y(), gp1.z()));
    polyline->AddVertex(gp_Pnt(gp2.x(), gp2.y(), gp2.z()));
    polyline->AddVertex(gp_Pnt(gp3.x(), gp3.y(), gp3.z()));
    polyline->AddVertex(gp_Pnt(gp4.x(), gp4.y(), gp4.z()));
    polyline->AddVertex(gp_Pnt(gp1.x(), gp1.y(), gp1.z()));
    
    // 創建呈現
    d->presentation = new Prs3d_Presentation(d->context->MainPrsMgr()->StructureManager());
    
    Handle(Prs3d_LineAspect) aspect = new Prs3d_LineAspect(
        Quantity_NOC_WHITE,
        Aspect_TOL_DASH,
        2.0
    );
    
    Handle(Graphic3d_Group) group = d->presentation->NewGroup();
    group->SetGroupPrimitivesAspect(aspect->Aspect());
    group->AddPrimitiveArray(polyline);
    
    d->presentation->SetZLayer(Graphic3d_ZLayerId_Top);
    d->presentation->SetDisplayPriority(Graphic3d_DisplayPriority_Topmost);
    d->presentation->Display();
    d->context->UpdateCurrentViewer();
}

void RubberBand::updatePolyline() {
    if (d->points.size() < 1 || !d->hasCurrentPoint) {
        return;
    }
    
    // 創建多段線
    int numPoints = d->points.size() + 1;
    Handle(Graphic3d_ArrayOfPolylines) polyline = new Graphic3d_ArrayOfPolylines(numPoints);
    
    for (const QVector2D& pt : d->points) {
        QVector3D p = planeToWorld(pt);
        polyline->AddVertex(gp_Pnt(p.x(), p.y(), p.z()));
    }
    
    QVector3D currentP = planeToWorld(d->currentPoint);
    polyline->AddVertex(gp_Pnt(currentP.x(), currentP.y(), currentP.z()));
    
    // 創建呈現
    d->presentation = new Prs3d_Presentation(d->context->MainPrsMgr()->StructureManager());
    
    Handle(Prs3d_LineAspect) aspect = new Prs3d_LineAspect(
        Quantity_NOC_WHITE,
        Aspect_TOL_DASH,
        2.0
    );
    
    Handle(Graphic3d_Group) group = d->presentation->NewGroup();
    group->SetGroupPrimitivesAspect(aspect->Aspect());
    group->AddPrimitiveArray(polyline);
    
    d->presentation->SetZLayer(Graphic3d_ZLayerId_Top);
    d->presentation->SetDisplayPriority(Graphic3d_DisplayPriority_Topmost);
    d->presentation->Display();
    d->context->UpdateCurrentViewer();
}

void RubberBand::updateSpline() {
    if (d->points.size() < 2 || !d->hasCurrentPoint) {
        return;
    }

    try {
        // 準備控制點（包括當前鼠標位置）
        int numControlPoints = d->points.size() + 1;
        Handle(TColgp_HArray1OfPnt) hControlPoints =
            new TColgp_HArray1OfPnt(1, numControlPoints);

        int index = 1;
        for (const QVector2D& pt : d->points) {
            QVector3D p = planeToWorld(pt);
            hControlPoints->SetValue(index++, gp_Pnt(p.x(), p.y(), p.z()));
        }

        QVector3D currentP = planeToWorld(d->currentPoint);
        hControlPoints->SetValue(index, gp_Pnt(currentP.x(), currentP.y(), currentP.z()));

        // 特殊處理：2點時顯示直線
        if (numControlPoints == 2) {
            Handle(Graphic3d_ArrayOfPolylines) polyline =
                new Graphic3d_ArrayOfPolylines(2);
            polyline->AddVertex(hControlPoints->Value(1));
            polyline->AddVertex(hControlPoints->Value(2));
            createAndDisplayPresentation(polyline);
            return;
        }

        // 3點或以上：創建插值樣條
        GeomAPI_Interpolate interpolator(hControlPoints, Standard_False, 1.0e-6);
        interpolator.Perform();

        if (!interpolator.IsDone()) {
            qWarning() << "[RubberBand] Failed to interpolate spline";
            return;
        }

        Handle(Geom_BSplineCurve) splineCurve = interpolator.Curve();

        // 自適應採樣並顯示
        GeomAdaptor_Curve adaptor(splineCurve);
        Standard_Real length = GCPnts_AbscissaPoint::Length(adaptor);

        // 根據曲線長度自適應調整採樣密度
        // 短曲線使用較少點，長曲線使用較多點
        int numSamples = qMax(20, qMin(100, static_cast<int>(length / 2.0)));

        GCPnts_UniformAbscissa uniformPoints(adaptor, numSamples);

        if (!uniformPoints.IsDone()) {
            qWarning() << "[RubberBand] Failed to sample spline curve";
            return;
        }

        int numPoints = uniformPoints.NbPoints();
        Handle(Graphic3d_ArrayOfPolylines) polyline =
            new Graphic3d_ArrayOfPolylines(numPoints);

        for (int i = 1; i <= numPoints; i++) {
            gp_Pnt pt;
            adaptor.D0(uniformPoints.Parameter(i), pt);
            polyline->AddVertex(pt);
        }

        createAndDisplayPresentation(polyline);

    } catch (Standard_Failure& e) {
        qWarning() << "[RubberBand] Exception in updateSpline:" << e.GetMessageString();
    }
}

// ============================================================
// 公共輔助方法實作
// ============================================================

void RubberBand::createAndDisplayPresentation(
    const Handle(Graphic3d_ArrayOfPolylines)& polyline)
{
    // 清除舊的 presentation
    if (!d->presentation.IsNull()) {
        d->presentation->Clear();
        d->presentation->Erase();
    }

    // 創建新的 presentation
    d->presentation = new Prs3d_Presentation(
        d->context->MainPrsMgr()->StructureManager()
        );

    // 設置線條樣式
    Handle(Prs3d_LineAspect) aspect = new Prs3d_LineAspect(
        Quantity_NOC_WHITE,      // 白色
        Aspect_TOL_DASH,         // 虛線
        2.0                      // 寬度
        );

    // 創建圖形組並添加幾何
    Handle(Graphic3d_Group) group = d->presentation->NewGroup();
    group->SetGroupPrimitivesAspect(aspect->Aspect());
    group->AddPrimitiveArray(polyline);

    // 設置顯示層級（確保在最上層）
    d->presentation->SetZLayer(Graphic3d_ZLayerId_Top);
    d->presentation->SetDisplayPriority(Graphic3d_DisplayPriority_Topmost);

    // 顯示並更新視圖
    d->presentation->Display();
    d->context->UpdateCurrentViewer();
}


void RubberBand::updatePolygon() {
    // 需要中心點和當前點來計算半徑
    if (!d->hasCurrentPoint || d->points.size() < 1) {
        return;
    }

    // 第一個點是中心點
    QVector2D center = d->points[0];

    // 從當前點計算半徑
    QVector2D delta = d->currentPoint - center;
    double radius = delta.length();

    if (radius < 0.001) {
        return;  // 半徑太小，不顯示
    }

    // 邊數（可以從成員變數讀取，預設為 6）
    //int sides = d->polygonSides > 0 ? d->polygonSides : 6;
    int sides = 6;

    // 計算多邊形頂點（閉合，所以需要 sides + 1 個點）
    int numPoints = sides + 1;
    Handle(Graphic3d_ArrayOfPolylines) polyline = new Graphic3d_ArrayOfPolylines(numPoints);

    double angleStep = 2.0 * M_PI / sides;
    double startAngle = -M_PI / 2.0;  // 從頂部（12點鐘方向）開始

    QVector<QVector2D> vertices;
    for (int i = 0; i < sides; ++i) {
        double angle = startAngle + i * angleStep;
        double x = center.x() + radius * std::cos(angle);
        double y = center.y() + radius * std::sin(angle);
        vertices.append(QVector2D(x, y));
    }

    // 添加所有頂點到 polyline
    for (const QVector2D& pt : vertices) {
        QVector3D p = planeToWorld(pt);
        polyline->AddVertex(gp_Pnt(p.x(), p.y(), p.z()));
    }

    // 閉合多邊形：添加第一個頂點以形成閉合形狀
    QVector3D firstP = planeToWorld(vertices.first());
    polyline->AddVertex(gp_Pnt(firstP.x(), firstP.y(), firstP.z()));

    // 創建呈現
    d->presentation = new Prs3d_Presentation(d->context->MainPrsMgr()->StructureManager());

    Handle(Prs3d_LineAspect) aspect = new Prs3d_LineAspect(
        Quantity_NOC_WHITE,
        Aspect_TOL_DASH,
        2.0
        );

    Handle(Graphic3d_Group) group = d->presentation->NewGroup();
    group->SetGroupPrimitivesAspect(aspect->Aspect());
    group->AddPrimitiveArray(polyline);

    d->presentation->SetZLayer(Graphic3d_ZLayerId_Top);
    d->presentation->SetDisplayPriority(Graphic3d_DisplayPriority_Topmost);
    d->presentation->Display();
    d->context->UpdateCurrentViewer();
}

void RubberBand::updateCircle() {
    if (d->points.isEmpty() || !d->hasCurrentPoint) {
        return;
    }
    
    // 計算半徑
    QVector2D center = d->points[0];
    float radius = (d->currentPoint - center).length();
    
    if (radius < 0.001) {
        return;
    }
    
    // 創建圓
    QVector3D centerWorld = planeToWorld(center);
    gp_Pnt centerPnt(centerWorld.x(), centerWorld.y(), centerWorld.z());
    gp_Dir normalDir(d->plane->normal().x(), d->plane->normal().y(), d->plane->normal().z());
    gp_Circ circle(gp_Ax2(centerPnt, normalDir), radius);
    
    // 創建圓的點陣列
    const int numSegments = 64;
    Handle(Graphic3d_ArrayOfPolylines) polyline = new Graphic3d_ArrayOfPolylines(numSegments + 1);
    
    for (int i = 0; i <= numSegments; ++i) {
        double angle = 2.0 * M_PI * i / numSegments;
        gp_Pnt pt = ElCLib::Value(angle, circle);
        polyline->AddVertex(pt);
    }
    
    // 創建呈現
    d->presentation = new Prs3d_Presentation(d->context->MainPrsMgr()->StructureManager());
    
    Handle(Prs3d_LineAspect) aspect = new Prs3d_LineAspect(
        Quantity_NOC_WHITE,
        Aspect_TOL_DASH,
        2.0
    );
    
    Handle(Graphic3d_Group) group = d->presentation->NewGroup();
    group->SetGroupPrimitivesAspect(aspect->Aspect());
    group->AddPrimitiveArray(polyline);
    
    d->presentation->SetZLayer(Graphic3d_ZLayerId_Top);
    d->presentation->SetDisplayPriority(Graphic3d_DisplayPriority_Topmost);
    d->presentation->Display();
    d->context->UpdateCurrentViewer();
}

void RubberBand::updateEllipse() {
    if (d->points.isEmpty() || !d->hasCurrentPoint) {
        return;
    }

    QVector2D center = d->points[0];

    // === Case 1: Only have center, show line to current point (major axis direction) ===
    if (d->points.size() == 1) {
        QVector3D centerWorld = planeToWorld(center);
        QVector3D currentWorld = planeToWorld(d->currentPoint);

        Handle(Graphic3d_ArrayOfPolylines) polyline = new Graphic3d_ArrayOfPolylines(2);
        polyline->AddVertex(gp_Pnt(centerWorld.x(), centerWorld.y(), centerWorld.z()));
        polyline->AddVertex(gp_Pnt(currentWorld.x(), currentWorld.y(), currentWorld.z()));

        // Create presentation
        d->presentation = new Prs3d_Presentation(d->context->MainPrsMgr()->StructureManager());

        Handle(Prs3d_LineAspect) aspect = new Prs3d_LineAspect(
            Quantity_NOC_WHITE,
            Aspect_TOL_DASH,
            2.0
            );

        Handle(Graphic3d_Group) group = d->presentation->NewGroup();
        group->SetGroupPrimitivesAspect(aspect->Aspect());
        group->AddPrimitiveArray(polyline);

        d->presentation->SetZLayer(Graphic3d_ZLayerId_Top);
        d->presentation->SetDisplayPriority(Graphic3d_DisplayPriority_Topmost);
        d->presentation->Display();
        d->context->UpdateCurrentViewer();
        return;
    }

    // === Case 2: Have center and major axis endpoint, show preview ellipse ===
    QVector2D majorAxisEnd = d->points[1];
    QVector2D majorVector = majorAxisEnd - center;
    float majorRadius = majorVector.length();

    if (majorRadius < 0.001) {
        return;
    }

    // Calculate minor radius from current point
    QVector2D toPoint = d->currentPoint - center;
    QVector2D majorNormalized = majorVector.normalized();
    QVector2D perpendicular(-majorNormalized.y(), majorNormalized.x());
    float minorRadius = qAbs(QVector2D::dotProduct(toPoint, perpendicular));

    // If point is too close to center or major axis, use distance to point
    if (minorRadius < 0.001) {
        minorRadius = toPoint.length();
        if (minorRadius < 0.001) {
            return; // Too close to center
        }
    }

    // Ensure minor radius doesn't exceed major radius for valid ellipse
    if (minorRadius > majorRadius) {
        minorRadius = majorRadius;
    }

    // Create ellipse in 3D
    QVector3D centerWorld = planeToWorld(center);
    gp_Pnt centerPnt(centerWorld.x(), centerWorld.y(), centerWorld.z());

    // Calculate major axis direction in 3D
    QVector3D majorAxisEndWorld = planeToWorld(majorAxisEnd);
    QVector3D majorAxisDir3D = (majorAxisEndWorld - centerWorld).normalized();
    gp_Dir xDir(majorAxisDir3D.x(), majorAxisDir3D.y(), majorAxisDir3D.z());

    gp_Dir normalDir(d->plane->normal().x(), d->plane->normal().y(), d->plane->normal().z());
    gp_Ax2 ax2(centerPnt, normalDir, xDir);
    gp_Elips ellipse(ax2, majorRadius, minorRadius);

    // Create ellipse point array
    const int numSegments = 64;
    Handle(Graphic3d_ArrayOfPolylines) polyline = new Graphic3d_ArrayOfPolylines(numSegments + 1);

    for (int i = 0; i <= numSegments; ++i) {
        double angle = 2.0 * M_PI * i / numSegments;
        gp_Pnt pt = ElCLib::Value(angle, ellipse);
        polyline->AddVertex(pt);
    }

    // Create presentation
    d->presentation = new Prs3d_Presentation(d->context->MainPrsMgr()->StructureManager());

    Handle(Prs3d_LineAspect) aspect = new Prs3d_LineAspect(
        Quantity_NOC_WHITE,
        Aspect_TOL_DASH,
        2.0
        );

    Handle(Graphic3d_Group) group = d->presentation->NewGroup();
    group->SetGroupPrimitivesAspect(aspect->Aspect());
    group->AddPrimitiveArray(polyline);

    d->presentation->SetZLayer(Graphic3d_ZLayerId_Top);
    d->presentation->SetDisplayPriority(Graphic3d_DisplayPriority_Topmost);
    d->presentation->Display();
    d->context->UpdateCurrentViewer();
}

void RubberBand::updateArc() {
    if (d->points.size() < 2 || !d->hasCurrentPoint) {
        return;
    }
    
    // 創建三點弧
    QVector3D p1 = planeToWorld(d->points[0]);
    QVector3D p2 = planeToWorld(d->points[1]);
    QVector3D p3 = planeToWorld(d->currentPoint);
    
    gp_Pnt gp1(p1.x(), p1.y(), p1.z());
    gp_Pnt gp2(p2.x(), p2.y(), p2.z());
    gp_Pnt gp3(p3.x(), p3.y(), p3.z());
    
    try {
        GC_MakeArcOfCircle arcMaker(gp1, gp2, gp3);
        if (arcMaker.IsDone()) {
            Handle(Geom_TrimmedCurve) arc = arcMaker.Value();
            
            // 創建弧的點陣列
            const int numSegments = 32;
            Handle(Graphic3d_ArrayOfPolylines) polyline = new Graphic3d_ArrayOfPolylines(numSegments + 1);
            
            double u1 = arc->FirstParameter();
            double u2 = arc->LastParameter();
            
            for (int i = 0; i <= numSegments; ++i) {
                double u = u1 + (u2 - u1) * i / numSegments;
                gp_Pnt pt = arc->Value(u);
                polyline->AddVertex(pt);
            }
            
            // 創建呈現
            d->presentation = new Prs3d_Presentation(d->context->MainPrsMgr()->StructureManager());
            
            Handle(Prs3d_LineAspect) aspect = new Prs3d_LineAspect(
                Quantity_NOC_WHITE,
                Aspect_TOL_DASH,
                2.0
            );
            
            Handle(Graphic3d_Group) group = d->presentation->NewGroup();
            group->SetGroupPrimitivesAspect(aspect->Aspect());
            group->AddPrimitiveArray(polyline);
            
            d->presentation->SetZLayer(Graphic3d_ZLayerId_Top);
            d->presentation->SetDisplayPriority(Graphic3d_DisplayPriority_Topmost);
            d->presentation->Display();
            d->context->UpdateCurrentViewer();
        }
    } catch (...) {
        qWarning() << "[RubberBand] Failed to create arc";
    }
}

QVector3D RubberBand::planeToWorld(const QVector2D& planePt) const {
    return d->plane->origin() + d->plane->xAxis() * planePt.x() + d->plane->yAxis() * planePt.y();
}

// ============================================================================
//  Spiral / SCS helpers
// ============================================================================

namespace {

/**
 * @brief Clothoid local-frame at arc-length L.
 *
 * Mirrors ClothoidElement::localFrame() — inlined here to avoid pulling the
 * entire railway header chain into RubberBand.cpp.
 *
 * @param L   Arc-length from TC [m]
 * @param R   Signed circular radius [m]  (+right, −left)
 * @param Ls  Total spiral length [m]
 * @return    {x, y, theta} in local tangent frame (x along entry tangent,
 *             y rightward, theta = cumulative deflection [rad])
 */
struct SpiralFrame { double x, y, theta; };

SpiralFrame clothoidFrame(double L, double R, double Ls)
{
    if (std::abs(R) < 1e-9 || Ls < 1e-9)
        return { L, 0.0, 0.0 };

    const double A2    = std::abs(R) * Ls;
    const double sign  = (R > 0.0) ? 1.0 : -1.0;
    const double theta = (L * L / (2.0 * A2)) * sign;
    const double th2   = theta * theta;
    const double th4   = th2 * th2;
    const double th6   = th4 * th2;
    const double x     = L * (1.0 - th2 / 10.0 + th4 / 216.0 - th6 / 9360.0);
    const double y     = L * theta * (1.0 / 3.0 - th2 / 42.0 + th4 / 1320.0);
    return { x, y, theta };
}

/**
 * @brief Rotate and translate a local-frame point to plane 2-D coords.
 *
 * @param lx, ly  Local x (along tangent) and y (rightward) [m]
 * @param ox, oy  Origin in plane coords [m]
 * @param angle   Tangent bearing in plane space [rad, CCW from +U axis]
 */
QVector2D localToPlane(double lx, double ly,
                       double ox, double oy,
                       double angle)
{
    const double ca = std::cos(angle);
    const double sa = std::sin(angle);
    return QVector2D(static_cast<float>(ox + lx * ca - ly * sa),
                     static_cast<float>(oy + lx * sa + ly * ca));
}

/**
 * @brief Append a dashed Clothoid spiral to a polyline array.
 *
 * @param poly      Target polyline (must be large enough)
 * @param ox, oy    Start point in plane coords
 * @param angle     Entry tangent angle in plane coords [rad, CCW from +U]
 * @param R         Signed radius [m]
 * @param Ls        Total spiral length [m]
 * @param nSamples  Number of segments
 * @param rb        RubberBand (for planeToWorld)
 */
void appendSpiral(Handle(Graphic3d_ArrayOfPolylines)& poly,
                  double ox, double oy, double angle,
                  double R, double Ls, int nSamples,
                  const RubberBand* rb,
                  std::function<QVector3D(QVector2D)> toWorld)
{
    for (int i = 0; i <= nSamples; ++i) {
        const double L   = Ls * static_cast<double>(i) / nSamples;
        const SpiralFrame lf = clothoidFrame(L, R, Ls);
        const QVector2D  p2  = localToPlane(lf.x, lf.y, ox, oy, angle);
        const QVector3D  w   = toWorld(p2);
        poly->AddVertex(gp_Pnt(w.x(), w.y(), w.z()));
    }
}

} // anonymous namespace

// ============================================================================
//  updateSpiral()
// ============================================================================

void RubberBand::updateSpiral()
{
    // Need at least a start point and either a second point or currentPoint
    if (d->points.isEmpty()) return;
    if (d->points.size() < 2 && !d->hasCurrentPoint) return;

    const double R  = d->radius;
    const double Ls = d->spiralLength;
    if (std::abs(R) < 1e-6 || Ls < 1e-6) return;

    // Origin: points[0]; direction: toward points[1] if present, else currentPoint
    const QVector2D origin = d->points[0];
    const QVector2D dirPt  = (d->points.size() >= 2) ? d->points[1]
                                                    : d->currentPoint;
    const QVector2D delta  = dirPt - origin;
    if (delta.length() < 1e-6f) return;

    const double angle = std::atan2(static_cast<double>(delta.y()),
                                    static_cast<double>(delta.x()));

    constexpr int kSamples = 60;
    Handle(Graphic3d_ArrayOfPolylines) poly =
        new Graphic3d_ArrayOfPolylines(kSamples + 1);

    auto toWorld = [this](QVector2D p) { return planeToWorld(p); };

    appendSpiral(poly,
                 static_cast<double>(origin.x()),
                 static_cast<double>(origin.y()),
                 angle, R, Ls, kSamples, this, toWorld);

    // ── Build presentation ────────────────────────────────────────────────────
    if (!d->presentation.IsNull()) {
        d->presentation->Clear();
        d->presentation->Erase();
    }
    d->presentation = new Prs3d_Presentation(
        d->context->MainPrsMgr()->StructureManager());

    Handle(Prs3d_LineAspect) aspect = new Prs3d_LineAspect(
        Quantity_NOC_GREEN,   // 緩和曲線 → 綠色虛線
        Aspect_TOL_DASH,
        2.0);

    Handle(Graphic3d_Group) group = d->presentation->NewGroup();
    group->SetGroupPrimitivesAspect(aspect->Aspect());
    group->AddPrimitiveArray(poly);

    d->presentation->SetZLayer(Graphic3d_ZLayerId_Top);
    d->presentation->SetDisplayPriority(Graphic3d_DisplayPriority_Topmost);
    d->presentation->Display();
    d->context->UpdateCurrentViewer();
}

// ============================================================================
//  updateSCS()
// ============================================================================

void RubberBand::updateSCS()
{
    // Need: points[0] = entry tangent start,
    //       points[1] = PI (intersection of entry & exit tangents),
    //       currentPoint = exit tangent end
    if (d->points.size() < 2 || !d->hasCurrentPoint) return;

    const double R   = d->radius;
    const double Ls1 = d->spiralLength1;   // entry spiral length
    const double Ls2 = d->spiralLength2;   // exit  spiral length
    if (std::abs(R) < 1e-6) return;

    // ── Tangent directions ────────────────────────────────────────────────────
    const QVector2D p0  = d->points[0];    // entry tangent start
    const QVector2D pi  = d->points[1];    // PI
    const QVector2D pe  = d->currentPoint; // exit tangent end

    const QVector2D entryVec = (pi - p0).normalized();
    const QVector2D exitVec  = (pe - pi).normalized();

    if (entryVec.length() < 1e-6f || exitVec.length() < 1e-6f) return;

    const double entryAngle = std::atan2(static_cast<double>(entryVec.y()),
                                         static_cast<double>(entryVec.x()));
    const double exitAngle  = std::atan2(static_cast<double>(exitVec.y()),
                                        static_cast<double>(exitVec.x()));

    // ── SCS geometry (Appendix B formula, asymmetric L1≠L2) ─────────────────
    double delta = exitAngle - entryAngle;
    while (delta >  M_PI) delta -= 2.0 * M_PI;
    while (delta < -M_PI) delta += 2.0 * M_PI;

    const double signedR = (delta >= 0.0) ? std::abs(R) : -std::abs(R);

    // Entry spiral tangent length
    const auto spiralTs = [](double Ls, double absR) -> std::pair<double,double> {
        if (Ls < 1e-9) return {0.0, 0.0};     // no spiral
        const double th = Ls / (2.0 * absR);
        const double Xm = Ls * (1.0 - th * th / 10.0);
        const double Ym = Ls * th / 3.0;
        return {Xm, Ym};
    };

    const double absR  = std::abs(signedR);
    const double halfD = std::abs(delta) / 2.0;

    auto [Xm1, Ym1] = spiralTs(Ls1, absR);
    auto [Xm2, Ym2] = spiralTs(Ls2, absR);

    // Asymmetric tangent lengths
    const double Ts1 = (absR + Ym1) * std::tan(halfD) + Xm1;
    const double Ts2 = (absR + Ym2) * std::tan(halfD) + Xm2;

    // SCS start / end in plane coords
    const QVector2D scsStart(
        static_cast<float>(static_cast<double>(pi.x()) - Ts1 * static_cast<double>(entryVec.x())),
        static_cast<float>(static_cast<double>(pi.y()) - Ts1 * static_cast<double>(entryVec.y())));
    const QVector2D scsEnd(
        static_cast<float>(static_cast<double>(pi.x()) + Ts2 * static_cast<double>(exitVec.x())),
        static_cast<float>(static_cast<double>(pi.y()) + Ts2 * static_cast<double>(exitVec.y())));

    // Entry spiral end point (SC)
    QVector2D scPoint  = scsStart;  // default: no entry spiral → arc starts here
    QVector2D csPoint  = scsEnd;    // default: no exit  spiral → arc ends here

    if (Ls1 > 1e-9) {
        SpiralFrame scFrame = clothoidFrame(Ls1, signedR, Ls1);
        scPoint = localToPlane(scFrame.x, scFrame.y,
                               static_cast<double>(scsStart.x()),
                               static_cast<double>(scsStart.y()),
                               entryAngle);
    }
    const double revExitAngle = exitAngle + M_PI;
    if (Ls2 > 1e-9) {
        SpiralFrame csFrame = clothoidFrame(Ls2, -signedR, Ls2);
        csPoint = localToPlane(csFrame.x, csFrame.y,
                               static_cast<double>(scsEnd.x()),
                               static_cast<double>(scsEnd.y()),
                               revExitAngle);
    }

    // ── Build presentation ────────────────────────────────────────────────────
    if (!d->presentation.IsNull()) {
        d->presentation->Clear();
        d->presentation->Erase();
    }
    d->presentation = new Prs3d_Presentation(
        d->context->MainPrsMgr()->StructureManager());

    constexpr int kSpiral = 60;
    constexpr int kArc    = 48;

    auto toWorld = [this](QVector2D p) { return planeToWorld(p); };

    // ── Segment 1: Entry spiral (dashed green) ────────────────────────────────
    if (Ls1 > 1e-9) {
        Handle(Graphic3d_ArrayOfPolylines) poly =
            new Graphic3d_ArrayOfPolylines(kSpiral + 1);
        appendSpiral(poly,
                     static_cast<double>(scsStart.x()),
                     static_cast<double>(scsStart.y()),
                     entryAngle, signedR, Ls1, kSpiral, this, toWorld);

        Handle(Prs3d_LineAspect) asp = new Prs3d_LineAspect(
            Quantity_NOC_GREEN, Aspect_TOL_DASH, 2.0);
        Handle(Graphic3d_Group) grp = d->presentation->NewGroup();
        grp->SetGroupPrimitivesAspect(asp->Aspect());
        grp->AddPrimitiveArray(poly);
    }

    // ── Segment 2: Circular arc (solid orange) ────────────────────────────────
    {
        const double perpAngle = entryAngle + M_PI / 2.0 * std::copysign(1.0, signedR);
        const double cx = static_cast<double>(scPoint.x()) + std::abs(signedR) * std::cos(perpAngle);
        const double cy = static_cast<double>(scPoint.y()) + std::abs(signedR) * std::sin(perpAngle);

        const double aStart = std::atan2(static_cast<double>(scPoint.y()) - cy,
                                         static_cast<double>(scPoint.x()) - cx);
        const double aEnd   = std::atan2(static_cast<double>(csPoint.y()) - cy,
                                       static_cast<double>(csPoint.x()) - cx);

        double arcSpan = aEnd - aStart;
        if (signedR > 0.0) {
            while (arcSpan < 0.0) arcSpan += 2.0 * M_PI;
        } else {
            while (arcSpan > 0.0) arcSpan -= 2.0 * M_PI;
        }

        Handle(Graphic3d_ArrayOfPolylines) poly =
            new Graphic3d_ArrayOfPolylines(kArc + 1);
        for (int i = 0; i <= kArc; ++i) {
            const double a = aStart + arcSpan * i / kArc;
            const QVector2D pt(static_cast<float>(cx + std::abs(signedR) * std::cos(a)),
                               static_cast<float>(cy + std::abs(signedR) * std::sin(a)));
            QVector3D w = toWorld(pt);
            poly->AddVertex(gp_Pnt(w.x(), w.y(), w.z()));
        }

        Handle(Prs3d_LineAspect) asp = new Prs3d_LineAspect(
            Quantity_NOC_ORANGE, Aspect_TOL_SOLID, 2.5);
        Handle(Graphic3d_Group) grp = d->presentation->NewGroup();
        grp->SetGroupPrimitivesAspect(asp->Aspect());
        grp->AddPrimitiveArray(poly);
    }

    // ── Segment 3: Exit spiral (dashed green) reversed from scsEnd ────────────
    if (Ls2 > 1e-9) {
        Handle(Graphic3d_ArrayOfPolylines) poly =
            new Graphic3d_ArrayOfPolylines(kSpiral + 1);
        appendSpiral(poly,
                     static_cast<double>(scsEnd.x()),
                     static_cast<double>(scsEnd.y()),
                     revExitAngle, -signedR, Ls2, kSpiral, this, toWorld);

        Handle(Prs3d_LineAspect) asp = new Prs3d_LineAspect(
            Quantity_NOC_GREEN, Aspect_TOL_DASH, 2.0);
        Handle(Graphic3d_Group) grp = d->presentation->NewGroup();
        grp->SetGroupPrimitivesAspect(asp->Aspect());
        grp->AddPrimitiveArray(poly);
    }

    d->presentation->SetZLayer(Graphic3d_ZLayerId_Top);
    d->presentation->SetDisplayPriority(Graphic3d_DisplayPriority_Topmost);
    d->presentation->Display();
    d->context->UpdateCurrentViewer();
}

} // namespace view
} // namespace aicad