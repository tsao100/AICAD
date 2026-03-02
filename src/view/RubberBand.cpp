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

} // namespace view
} // namespace aicad
