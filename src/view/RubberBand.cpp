/**
 * @file RubberBand.cpp
 * @brief RubberBand 類別實作
 * @author Felicia
 * @date 2024-12-04
 */

#include "RubberBand.h"
#include "cad/Plane.h"
#include "cad/PlaneManager.h"
#include "railway/RailwayAlignmentElement.h"
#include "railway/AlignmentDocument.h"   // SpiralType

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
    QVector<QPointF> points;
    QPointF currentPoint;
    bool hasCurrentPoint;

    // ── Spiral / SCS parameters ───────────────────────────────────────────────
    double radius        = 400.0;   ///< Circular arc radius [m]; +right, −left
    double spiralLength  = 100.0;   ///< Transition curve length [m] (L1 = L2 symmetric)
    double spiralLength1 = 100.0;   ///< Entry spiral length L1 [m]  (SCS asymmetric)
    double spiralLength2 = 100.0;   ///< Exit  spiral length L2 [m]  (SCS asymmetric)

    /// Spiral family for the entry spiral (Spiral mode) or L1 (SCS mode).
    railway::SpiralType spiralType1 = railway::SpiralType::Clothoid;
    /// Spiral family for the exit spiral L2 (SCS mode only).
    railway::SpiralType spiralType2 = railway::SpiralType::Clothoid;
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

void RubberBand::addPoint(const QPointF& point) {
    d->points.append(point);
    qDebug() << "[RubberBand] Point added:" << point.x() << "," << point.y()
             << "Total points:" << d->points.size();
}

void RubberBand::setCurrentPoint(const QPointF& point) {
    d->currentPoint = point;
    d->hasCurrentPoint = true;
}

QVector<QPointF> RubberBand::points() const {
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

void RubberBand::setSpiralType1(railway::SpiralType type)
{
    d->spiralType1 = type;
}

railway::SpiralType RubberBand::spiralType1() const
{
    return d->spiralType1;
}

void RubberBand::setSpiralType2(railway::SpiralType type)
{
    d->spiralType2 = type;
}

railway::SpiralType RubberBand::spiralType2() const
{
    return d->spiralType2;
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
    QPointF p1 = d->points[0];
    QPointF p2 = d->currentPoint;
    
    QVector3D gp1 = planeToWorld(QPointF(p1.x(), p1.y()));
    QVector3D gp2 = planeToWorld(QPointF(p2.x(), p1.y()));
    QVector3D gp3 = planeToWorld(QPointF(p2.x(), p2.y()));
    QVector3D gp4 = planeToWorld(QPointF(p1.x(), p2.y()));
    
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
    
    for (const QPointF& pt : d->points) {
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
        for (const QPointF& pt : d->points) {
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
    QPointF center = d->points[0];

    // 從當前點計算半徑
    double dx = d->currentPoint.x() - center.x();
    double dy = d->currentPoint.y() - center.y();
    double radius = std::sqrt(dx*dx + dy*dy);

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

    QVector<QPointF> vertices;
    for (int i = 0; i < sides; ++i) {
        double angle = startAngle + i * angleStep;
        double x = center.x() + radius * std::cos(angle);
        double y = center.y() + radius * std::sin(angle);
        vertices.append(QPointF(x, y));
    }

    // 添加所有頂點到 polyline
    for (const QPointF& pt : vertices) {
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
    QPointF center = d->points[0];
    double dx = d->currentPoint.x() - center.x();
    double dy = d->currentPoint.y() - center.y();
    float radius = static_cast<float>(std::sqrt(dx*dx + dy*dy));
    
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

    QPointF center = d->points[0];

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
    // center 已是 QPointF（d->points[0]），轉為 QVector2D 以使用向量運算 API
    QVector2D centerV(static_cast<float>(center.x()), static_cast<float>(center.y()));
    QVector2D majorAxisEnd(static_cast<float>(d->points[1].x()),
                           static_cast<float>(d->points[1].y()));
    QVector2D majorVector = majorAxisEnd - centerV;
    float majorRadius = majorVector.length();

    if (majorRadius < 0.001) {
        return;
    }

    // Calculate minor radius from current point
    QVector2D toPoint(static_cast<float>(d->currentPoint.x()) - centerV.x(),
                      static_cast<float>(d->currentPoint.y()) - centerV.y());
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
    QVector3D majorAxisEndWorld = planeToWorld(QPointF(majorAxisEnd.x(), majorAxisEnd.y()));
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

QVector3D RubberBand::planeToWorld(const QPointF& planePt) const {
    return d->plane->origin() + d->plane->xAxis() * planePt.x() + d->plane->yAxis() * planePt.y();
}

// ============================================================================
//  Spiral / SCS helpers
// ============================================================================

namespace {

using railway::SpiralType;
using railway::LocalFrame;
using railway::TransitionElement;
using railway::ClothoidElement;
using railway::HalfSineElement;
using railway::ParabolaElement;
using railway::CubicJPNElement;
using railway::CubicECIElement;

/**
 * @brief Instantiate the TransitionElement subclass for @p type.
 *
 * The element is configured with @p Ls and @p signedR and used only to call
 * localFrame(L) — placement fields are left at defaults.
 */
std::unique_ptr<TransitionElement>
makeTransitionElement(SpiralType type, double Ls, double signedR)
{
    std::unique_ptr<TransitionElement> elem;
    switch (type) {
    case SpiralType::HalfSine: elem = std::make_unique<HalfSineElement>(); break;
    case SpiralType::Parabola: elem = std::make_unique<ParabolaElement>(); break;
    case SpiralType::CubicJPN: elem = std::make_unique<CubicJPNElement>(); break;
    case SpiralType::CubicECI: elem = std::make_unique<CubicECIElement>(); break;
    case SpiralType::Clothoid:
    default:                   elem = std::make_unique<ClothoidElement>();  break;
    }
    elem->setLength(Ls);
    elem->setRadius(signedR);
    return elem;
}

/**
 * @brief Rotate and translate a local-frame point into plane 2-D coordinates.
 *
 * The local frame has its x-axis along the initial tangent and its y-axis
 * 90° clockwise (rightward), i.e. y = right_perp(tangent).
 * In plane coordinates the tangent has angle @p angle (CCW from +U), so:
 *   U' = lx·cos(angle) − ly·sin(angle)   [lx along tangent, ly rightward]
 *   V' = lx·sin(angle) + ly·cos(angle)
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
 * @brief Append a spiral polyline to @p poly using the correct element type.
 *
 * Samples @p nSamples+1 points from L=0 to L=Ls by calling
 * element::localFrame(L) for each sample, then transforms the local-frame
 * coordinates to world space via @p toWorld.
 *
 * @param poly      Target polyline (caller ensures capacity ≥ nSamples+1)
 * @param ox, oy    Spiral start point in plane coords [m]
 * @param angle     Entry tangent angle in plane coords [rad, CCW from +U]
 * @param signedR   Signed circular radius [m]  (+ = right, − = left)
 * @param Ls        Total spiral length [m]
 * @param type      Spiral family (Clothoid / HalfSine / Parabola / …)
 * @param nSamples  Number of polyline segments
 * @param toWorld   Functor: plane QVector2D → world QVector3D
 */
void appendSpiralByType(Handle(Graphic3d_ArrayOfPolylines)& poly,
                        double ox, double oy, double angle,
                        double signedR, double Ls,
                        SpiralType type, int nSamples,
                        std::function<QVector3D(QVector2D)> toWorld)
{
    const auto elem = makeTransitionElement(type, Ls, signedR);
    for (int i = 0; i <= nSamples; ++i) {
        const double   L  = Ls * static_cast<double>(i) / nSamples;
        const LocalFrame lf = elem->localFrame(L);
        const QVector2D p2  = localToPlane(lf.x, lf.y, ox, oy, angle);
        const QVector3D w   = toWorld(p2);
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
    // d->points 現為 QPointF，轉 QVector2D 以使用向量運算（length, atan2 等）
    const QVector2D origin(static_cast<float>(d->points[0].x()),
                           static_cast<float>(d->points[0].y()));
    const QVector2D dirPt  = (d->points.size() >= 2)
        ? QVector2D(static_cast<float>(d->points[1].x()), static_cast<float>(d->points[1].y()))
        : QVector2D(static_cast<float>(d->currentPoint.x()), static_cast<float>(d->currentPoint.y()));
    const QVector2D delta  = dirPt - origin;
    if (delta.length() < 1e-6f) return;

    const double angle = std::atan2(static_cast<double>(delta.y()),
                                    static_cast<double>(delta.x()));

    constexpr int kSamples = 60;
    Handle(Graphic3d_ArrayOfPolylines) poly =
        new Graphic3d_ArrayOfPolylines(kSamples + 1);

    auto toWorld = [this](QVector2D p) { return planeToWorld(QPointF(p.x(), p.y())); };

    appendSpiralByType(poly,
                       static_cast<double>(origin.x()),
                       static_cast<double>(origin.y()),
                       angle, R, Ls,
                       d->spiralType1,   // honour selected spiral family
                       kSamples, toWorld);

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
    // d->points 現為 QPointF，轉 QVector2D 以使用向量運算 API（normalized 等）
    const QVector2D p0(static_cast<float>(d->points[0].x()),
                       static_cast<float>(d->points[0].y()));    // entry tangent start
    const QVector2D pi(static_cast<float>(d->points[1].x()),
                       static_cast<float>(d->points[1].y()));    // PI
    const QVector2D pe(static_cast<float>(d->currentPoint.x()),
                       static_cast<float>(d->currentPoint.y())); // exit tangent end

    const QVector2D entryVec = (pi - p0).normalized();
    const QVector2D exitVec  = (pe - pi).normalized();

    if (entryVec.length() < 1e-6f || exitVec.length() < 1e-6f) return;

    const double entryAngle = std::atan2(static_cast<double>(entryVec.y()),
                                         static_cast<double>(entryVec.x()));
    const double exitAngle  = std::atan2(static_cast<double>(exitVec.y()),
                                        static_cast<double>(exitVec.x()));

    // ── SCS geometry (asymmetric L1≠L2, type-aware) ─────────────────────────
    double delta = exitAngle - entryAngle;
    while (delta >  M_PI) delta -= 2.0 * M_PI;
    while (delta < -M_PI) delta += 2.0 * M_PI;

    const double signedR = (delta >= 0.0) ? std::abs(R) : -std::abs(R);
    const double absR    = std::abs(signedR);

    // Entry spiral tangent length (Xm1, Ym1) via correct element type
    double Xm1 = 0.0, Ym1 = 0.0, thetaS1 = 0.0;
    std::unique_ptr<TransitionElement> elem1;
    if (Ls1 > 1e-9) {
        elem1 = makeTransitionElement(d->spiralType1, Ls1, signedR);
        const LocalFrame lf1 = elem1->localFrame(Ls1);
        Xm1     = lf1.x;
        Ym1     = lf1.y;      // signed: positive = right
        thetaS1 = lf1.theta;  // signed deflection
    }

    // Exit spiral tangent length (Xm2, Ym2) via correct element type
    double Xm2 = 0.0, Ym2 = 0.0, thetaS2 = 0.0;
    std::unique_ptr<TransitionElement> elem2;
    if (Ls2 > 1e-9) {
        elem2 = makeTransitionElement(d->spiralType2, Ls2, signedR);
        const LocalFrame lf2 = elem2->localFrame(Ls2);
        Xm2     = lf2.x;
        Ym2     = lf2.y;
        thetaS2 = lf2.theta;
    }

    // Tangent lengths T1, T2 — derived from Xm, Ym for asymmetric SCS
    // (using shifted-centre formula identical to AlignmentSolver)
    const double halfD = std::abs(delta) / 2.0;
    const double tanHalfD = std::tan(halfD);
    const double sinDelta  = std::sin(std::abs(delta));
    const double correction = (sinDelta > 1e-9)
                              ? (std::abs(Ym2) - std::abs(Ym1)) / sinDelta * (delta >= 0 ? 1.0 : -1.0)
                              : 0.0;
    const double Ts1 = (absR + std::abs(Ym1)) * tanHalfD + Xm1 + correction;
    const double Ts2 = (absR + std::abs(Ym2)) * tanHalfD + Xm2 - correction;

    // SCS start / end in plane coords
    const QVector2D scsStart(
        static_cast<float>(static_cast<double>(pi.x()) - Ts1 * static_cast<double>(entryVec.x())),
        static_cast<float>(static_cast<double>(pi.y()) - Ts1 * static_cast<double>(entryVec.y())));
    const QVector2D scsEnd(
        static_cast<float>(static_cast<double>(pi.x()) + Ts2 * static_cast<double>(exitVec.x())),
        static_cast<float>(static_cast<double>(pi.y()) + Ts2 * static_cast<double>(exitVec.y())));

    // Entry spiral end point (SC) — from localFrame of the entry element
    QVector2D scPoint  = scsStart;  // fallback: no entry spiral → arc starts at TS
    QVector2D csPoint  = scsEnd;    // fallback: no exit  spiral → arc ends  at ST

    if (Ls1 > 1e-9 && elem1) {
        const LocalFrame scFrame = elem1->localFrame(Ls1);
        scPoint = localToPlane(scFrame.x, scFrame.y,
                               static_cast<double>(scsStart.x()),
                               static_cast<double>(scsStart.y()),
                               entryAngle);
    }

    // Exit spiral end point (CS) — from localFrame of the exit element,
    // evaluated from the ST end in the reversed (CT) direction.
    const double revExitAngle = exitAngle + M_PI;
    if (Ls2 > 1e-9 && elem2) {
        // The exit spiral is traversed CT: sign of R is inverted relative
        // to the entry spiral when looking from the ST end.
        auto elem2rev = makeTransitionElement(d->spiralType2, Ls2, -signedR);
        const LocalFrame csFrame = elem2rev->localFrame(Ls2);
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

    auto toWorld = [this](QVector2D p) { return planeToWorld(QPointF(p.x(), p.y())); };

    // ── Segment 1: Entry spiral (dashed green) ────────────────────────────────
    if (Ls1 > 1e-9) {
        Handle(Graphic3d_ArrayOfPolylines) poly =
            new Graphic3d_ArrayOfPolylines(kSpiral + 1);
        appendSpiralByType(poly,
                           static_cast<double>(scsStart.x()),
                           static_cast<double>(scsStart.y()),
                           entryAngle, signedR, Ls1,
                           d->spiralType1, kSpiral, toWorld);

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
        appendSpiralByType(poly,
                           static_cast<double>(scsEnd.x()),
                           static_cast<double>(scsEnd.y()),
                           revExitAngle, -signedR, Ls2,
                           d->spiralType2, kSpiral, toWorld);

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