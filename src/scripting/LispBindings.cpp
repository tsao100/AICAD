/**
 * @file LispBindings.cpp
 * @brief LispBindings 類別實作
 * @author Daney
 * @date 2024-12-04
 */

// CRITICAL: ECL headers MUST be included BEFORE Qt headers
// to avoid macro conflicts (especially 'slots')
#include <ecl/ecl.h>

// Undefine Qt-conflicting macros from ECL
#ifdef slots
#undef slots
#endif


#include "LispBindings.h"
#include "LispEngine.h"
#include "core/Application.h"
#include "core/DocumentManager.h"
#include "core/EventBus.h"
#include "cad/Sketch.h"
#include "cad/sketch/SketchConstraint.h"

#include <QDebug>
#include <QVector2D>
#include <QVector3D>
#include <cmath>

namespace aicad {
namespace scripting {

class LispBindings::Private {
public:
    LispEngine* engine;
    core::Application* app;
};

LispBindings::LispBindings(LispEngine* engine,
                          core::Application* app,
                          QObject* parent)
    : QObject(parent)
    , d(new Private())
{
    d->engine = engine;
    d->app = app;
    
    qDebug() << "[LispBindings] Created";
}

LispBindings::~LispBindings() {
    qDebug() << "[LispBindings] Destroyed";
    delete d;
}

void LispBindings::registerAll() {
    qDebug() << "[LispBindings] Registering all API functions...";
    
    registerDocumentAPI();
    registerGeometryAPI();
    registerViewAPI();
    registerQueryAPI();
    registerUtilityAPI();
    registerSnapAPI();
    registerConstraintAPI();  // Phase 8

    qDebug() << "[LispBindings] All API functions registered";
}

void LispBindings::registerDocumentAPI() {
    qDebug() << "[LispBindings] Registering Document API...";
    
    // (sketch-create plane-name sketch-name)
    d->engine->registerFunction("SKETCH-CREATE", 
        [this](const QVariantList& args) -> QVariant {
            if (args.size() < 1) {
                qWarning() << "sketch-create: requires plane name";
                return QVariant();
            }
            
            QString planeName = args[0].toString().toUpper();
            QString sketchName = args.size() > 1 ? args[1].toString() : "Sketch";
            
            qDebug() << "sketch-create:" << planeName << sketchName;
            
            // TODO: 實際實作需要存取 OcafDocument
            // 這裡示範如何透過 EventBus 發送命令
            
            QVariantMap data;
            data["plane"] = planeName;
            data["name"] = sketchName;
            
            d->app->eventBus()->publish("scripting.sketch-create", data);
            
            return QVariant::fromValue(QString("Sketch created: ") + sketchName);
        }, 1, 2);
    
    // (extrude-create sketch-id height extrude-name)
    d->engine->registerFunction("EXTRUDE-CREATE",
        [this](const QVariantList& args) -> QVariant {
            if (args.size() < 2) {
                qWarning() << "extrude-create: requires sketch-id and height";
                return QVariant();
            }
            
            int sketchId = args[0].toInt();
            double height = args[1].toDouble();
            QString extrudeName = args.size() > 2 ? args[2].toString() : "Extrude";
            
            qDebug() << "extrude-create: sketch" << sketchId 
                     << "height" << height << extrudeName;
            
            QVariantMap data;
            data["sketchId"] = sketchId;
            data["height"] = height;
            data["name"] = extrudeName;
            
            d->app->eventBus()->publish("scripting.extrude-create", data);
            
            return QVariant::fromValue(QString("Extrude created: ") + extrudeName);
        }, 2, 3);
    
    // (feature-delete feature-id)
    d->engine->registerFunction("FEATURE-DELETE",
        [this](const QVariantList& args) -> QVariant {
            if (args.size() < 1) {
                qWarning() << "feature-delete: requires feature-id";
                return QVariant();
            }
            
            int featureId = args[0].toInt();
            
            qDebug() << "feature-delete:" << featureId;
            
            d->app->eventBus()->publish("scripting.feature-delete", featureId);
            
            return true;
        }, 1, 1);
}

void LispBindings::registerGeometryAPI() {
    qDebug() << "[LispBindings] Registering Geometry API...";
    
    // (line x1 y1 x2 y2)
    d->engine->registerFunction("LINE",
        [this](const QVariantList& args) -> QVariant {
            if (args.size() < 4) {
                qWarning() << "line: requires x1 y1 x2 y2";
                return QVariant();
            }
            
            double x1 = args[0].toDouble();
            double y1 = args[1].toDouble();
            double x2 = args[2].toDouble();
            double y2 = args[3].toDouble();
            
            qDebug() << "line:" << x1 << y1 << x2 << y2;
            
            QVariantMap data;
            data["type"] = "line";
            data["x1"] = x1;
            data["y1"] = y1;
            data["x2"] = x2;
            data["y2"] = y2;
            
            d->app->eventBus()->publish("scripting.geometry-add", data);
            
            return true;
        }, 4, 4);
    
    // (rectangle x1 y1 x2 y2)
    d->engine->registerFunction("RECTANGLE",
        [this](const QVariantList& args) -> QVariant {
            if (args.size() < 4) {
                qWarning() << "rectangle: requires x1 y1 x2 y2";
                return QVariant();
            }
            
            double x1 = args[0].toDouble();
            double y1 = args[1].toDouble();
            double x2 = args[2].toDouble();
            double y2 = args[3].toDouble();
            
            qDebug() << "rectangle:" << x1 << y1 << x2 << y2;
            
            QVariantMap data;
            data["type"] = "rectangle";
            data["x1"] = x1;
            data["y1"] = y1;
            data["x2"] = x2;
            data["y2"] = y2;
            
            d->app->eventBus()->publish("scripting.geometry-add", data);
            
            return true;
        }, 4, 4);
    
    // (circle x y radius)
    d->engine->registerFunction("CIRCLE",
        [this](const QVariantList& args) -> QVariant {
            if (args.size() < 3) {
                qWarning() << "circle: requires x y radius";
                return QVariant();
            }
            
            double x = args[0].toDouble();
            double y = args[1].toDouble();
            double radius = args[2].toDouble();
            
            qDebug() << "circle:" << x << y << radius;
            
            QVariantMap data;
            data["type"] = "circle";
            data["x"] = x;
            data["y"] = y;
            data["radius"] = radius;
            
            d->app->eventBus()->publish("scripting.geometry-add", data);
            
            return true;
        }, 3, 3);
    
    // (arc x y radius start-angle end-angle)
    d->engine->registerFunction("ARC",
        [this](const QVariantList& args) -> QVariant {
            if (args.size() < 5) {
                qWarning() << "arc: requires x y radius start-angle end-angle";
                return QVariant();
            }
            
            double x = args[0].toDouble();
            double y = args[1].toDouble();
            double radius = args[2].toDouble();
            double startAngle = args[3].toDouble();
            double endAngle = args[4].toDouble();
            
            qDebug() << "arc:" << x << y << radius << startAngle << endAngle;
            
            QVariantMap data;
            data["type"] = "arc";
            data["x"] = x;
            data["y"] = y;
            data["radius"] = radius;
            data["startAngle"] = startAngle;
            data["endAngle"] = endAngle;
            
            d->app->eventBus()->publish("scripting.geometry-add", data);
            
            return true;
        }, 5, 5);
    
    // (polyline x1 y1 x2 y2 x3 y3 ...)
    d->engine->registerFunction("POLYLINE",
        [this](const QVariantList& args) -> QVariant {
            if (args.size() < 4 || args.size() % 2 != 0) {
                qWarning() << "polyline: requires even number of coordinates (x y pairs)";
                return QVariant();
            }
            
            QVariantList points;
            for (int i = 0; i < args.size(); i += 2) {
                QVariantMap point;
                point["x"] = args[i].toDouble();
                point["y"] = args[i + 1].toDouble();
                points.append(point);
            }
            
            qDebug() << "polyline:" << points.size() << "points";
            
            QVariantMap data;
            data["type"] = "polyline";
            data["points"] = points;
            
            d->app->eventBus()->publish("scripting.geometry-add", data);
            
            return true;
        }, 4, -1);
}

void LispBindings::registerViewAPI() {
    qDebug() << "[LispBindings] Registering View API...";
    
    // (view-set view-name)
    d->engine->registerFunction("VIEW-SET",
        [this](const QVariantList& args) -> QVariant {
            if (args.size() < 1) {
                qWarning() << "view-set: requires view name";
                return QVariant();
            }
            
            QString viewName = args[0].toString().toUpper();
            
            qDebug() << "view-set:" << viewName;
            
            d->app->eventBus()->publish("scripting.view-set", viewName);
            
            return true;
        }, 1, 1);
    
    // (view-fit)
    d->engine->registerFunction("VIEW-FIT",
        [this](const QVariantList& args) -> QVariant {
            Q_UNUSED(args);
            
            qDebug() << "view-fit";
            
            d->app->eventBus()->publish("scripting.view-fit", QVariant());
            
            return true;
        }, 0, 0);
    
    // (view-zoom factor)
    d->engine->registerFunction("VIEW-ZOOM",
        [this](const QVariantList& args) -> QVariant {
            if (args.size() < 1) {
                qWarning() << "view-zoom: requires zoom factor";
                return QVariant();
            }
            
            double factor = args[0].toDouble();
            
            qDebug() << "view-zoom:" << factor;
            
            d->app->eventBus()->publish("scripting.view-zoom", factor);
            
            return true;
        }, 1, 1);
}

void LispBindings::registerQueryAPI() {
    qDebug() << "[LispBindings] Registering Query API...";
    
    // (feature-list)
    d->engine->registerFunction("FEATURE-LIST",
        [this](const QVariantList& args) -> QVariant {
            Q_UNUSED(args);
            
            qDebug() << "feature-list";
            
            // TODO: 實際實作需要查詢 DocumentManager
            // 這裡返回示範資料
            
            QVariantList features;
            features << 1 << 2 << 3;
            
            return features;
        }, 0, 0);
    
    // (feature-info feature-id)
    d->engine->registerFunction("FEATURE-INFO",
        [this](const QVariantList& args) -> QVariant {
            if (args.size() < 1) {
                qWarning() << "feature-info: requires feature-id";
                return QVariant();
            }
            
            int featureId = args[0].toInt();
            
            qDebug() << "feature-info:" << featureId;
            
            // TODO: 實際查詢
            // 返回示範資料
            
            QVariantMap info;
            info["id"] = featureId;
            info["type"] = "Sketch";
            info["name"] = QString("Sketch %1").arg(featureId);
            
            return info;
        }, 1, 1);
    
    // (feature-count)
    d->engine->registerFunction("FEATURE-COUNT",
        [this](const QVariantList& args) -> QVariant {
            Q_UNUSED(args);
            
            qDebug() << "feature-count";
            
            // TODO: 實際查詢
            return 3;
        }, 0, 0);
    
    // (sketch-polylines sketch-id)
    d->engine->registerFunction("SKETCH-POLYLINES",
        [this](const QVariantList& args) -> QVariant {
            if (args.size() < 1) {
                qWarning() << "sketch-polylines: requires sketch-id";
                return QVariant();
            }
            
            int sketchId = args[0].toInt();
            
            qDebug() << "sketch-polylines:" << sketchId;
            
            // TODO: 實際查詢
            // 返回示範資料
            
            QVariantList polylines;
            QVariantList line1;
            line1 << QVariantList{0.0, 0.0} << QVariantList{100.0, 0.0};
            polylines << line1;
            
            return polylines;
        }, 1, 1);
}

void LispBindings::registerUtilityAPI() {
    qDebug() << "[LispBindings] Registering Utility API...";
    
    // (print-message message)
    d->engine->registerFunction("PRINT-MESSAGE",
        [this](const QVariantList& args) -> QVariant {
            if (args.size() < 1) {
                return QVariant();
            }
            
            QString message = args[0].toString();
            
            qDebug() << "print-message:" << message;
            
            d->app->eventBus()->publish("scripting.print-message", message);
            
            return true;
        }, 1, 1);
    
    // (distance x1 y1 x2 y2)
    d->engine->registerFunction("DISTANCE",
        [this](const QVariantList& args) -> QVariant {
            if (args.size() < 4) {
                qWarning() << "distance: requires x1 y1 x2 y2";
                return QVariant();
            }
            
            double x1 = args[0].toDouble();
            double y1 = args[1].toDouble();
            double x2 = args[2].toDouble();
            double y2 = args[3].toDouble();
            
            double dx = x2 - x1;
            double dy = y2 - y1;
            double dist = std::sqrt(dx * dx + dy * dy);
            
            qDebug() << "distance:" << dist;
            
            return dist;
        }, 4, 4);
    
    // (angle x1 y1 x2 y2)
    d->engine->registerFunction("ANGLE",
        [this](const QVariantList& args) -> QVariant {
            if (args.size() < 4) {
                qWarning() << "angle: requires x1 y1 x2 y2";
                return QVariant();
            }
            
            double x1 = args[0].toDouble();
            double y1 = args[1].toDouble();
            double x2 = args[2].toDouble();
            double y2 = args[3].toDouble();
            
            double dx = x2 - x1;
            double dy = y2 - y1;
            double angle = std::atan2(dy, dx) * 180.0 / M_PI;
            
            qDebug() << "angle:" << angle;
            
            return angle;
        }, 4, 4);
    
    // (polar x y distance angle)
    d->engine->registerFunction("POLAR",
        [this](const QVariantList& args) -> QVariant {
            if (args.size() < 4) {
                qWarning() << "polar: requires x y distance angle";
                return QVariant();
            }
            
            double x = args[0].toDouble();
            double y = args[1].toDouble();
            double distance = args[2].toDouble();
            double angle = args[3].toDouble() * M_PI / 180.0;
            
            double newX = x + distance * std::cos(angle);
            double newY = y + distance * std::sin(angle);
            
            qDebug() << "polar:" << newX << newY;
            
            QVariantList result;
            result << newX << newY;
            
            return result;
        }, 4, 4);
}

// 新增 registerSnapAPI() 實作：
void LispBindings::registerSnapAPI() {
    // (osnap-enable t/nil)
    d->engine->registerFunction("OSNAP-ENABLE",
                                [this](const QVariantList& args) -> QVariant {
                                    bool on = args.isEmpty() ? true : args[0].toBool();
                                    d->app->eventBus()->publish("scripting.osnap-enable", on);
                                    return on;
                                }, 0, 1);

    // (osnap-set type1 type2 ...)  e.g. (osnap-set "endpoint" "midpoint")
    d->engine->registerFunction("OSNAP-SET",
                                [this](const QVariantList& args) -> QVariant {
                                    QStringList types;
                                    for (const QVariant& a : args) types << a.toString();
                                    d->app->eventBus()->publish("scripting.osnap-set", types);
                                    return true;
                                }, 0, -1);
}


// ─────────────────────────────────────────────────────────────────────────────
// Phase 8：Lisp 約束 API
// ─────────────────────────────────────────────────────────────────────────────

void LispBindings::registerConstraintAPI()
{
    using namespace aicad::cad;

    auto getSketch = [this]() -> Sketch* {
        return d->app ? d->app->activeSketch() : nullptr;
    };

    // (sk-constrain-coincident geom-uuid-1 handle-1 geom-uuid-2 handle-2)
    // handle: 0=WholeGeom 1=Start 2=End 3=Center
    d->engine->registerFunction("SK-CONSTRAIN-COINCIDENT",
        [getSketch](const QVariantList& args) -> QVariant {
            if (args.size() < 4) return QVariant();
            auto* sk = getSketch();
            if (!sk) return QVariant();
            QString u1 = args[0].toString();
            GeomHandle h1 = static_cast<GeomHandle>(args[1].toInt());
            QString u2 = args[2].toString();
            GeomHandle h2 = static_cast<GeomHandle>(args[3].toInt());
            return sk->constrainCoincident(GeomRef(u1,h1), GeomRef(u2,h2));
        }, 4, 4);

    // (sk-constrain-horizontal line-uuid)
    d->engine->registerFunction("SK-CONSTRAIN-HORIZONTAL",
        [getSketch](const QVariantList& args) -> QVariant {
            if (args.isEmpty()) return QVariant();
            auto* sk = getSketch(); if (!sk) return QVariant();
            return sk->constrainHorizontal(args[0].toString());
        }, 1, 1);

    // (sk-constrain-vertical line-uuid)
    d->engine->registerFunction("SK-CONSTRAIN-VERTICAL",
        [getSketch](const QVariantList& args) -> QVariant {
            if (args.isEmpty()) return QVariant();
            auto* sk = getSketch(); if (!sk) return QVariant();
            return sk->constrainVertical(args[0].toString());
        }, 1, 1);

    // (sk-constrain-parallel line-uuid-1 line-uuid-2)
    d->engine->registerFunction("SK-CONSTRAIN-PARALLEL",
        [getSketch](const QVariantList& args) -> QVariant {
            if (args.size() < 2) return QVariant();
            auto* sk = getSketch(); if (!sk) return QVariant();
            return sk->constrainParallel(args[0].toString(), args[1].toString());
        }, 2, 2);

    // (sk-constrain-perpendicular line-uuid-1 line-uuid-2)
    d->engine->registerFunction("SK-CONSTRAIN-PERPENDICULAR",
        [getSketch](const QVariantList& args) -> QVariant {
            if (args.size() < 2) return QVariant();
            auto* sk = getSketch(); if (!sk) return QVariant();
            return sk->constrainPerpendicular(args[0].toString(), args[1].toString());
        }, 2, 2);

    // (sk-constrain-tangent geom-uuid-1 geom-uuid-2)
    d->engine->registerFunction("SK-CONSTRAIN-TANGENT",
        [getSketch](const QVariantList& args) -> QVariant {
            if (args.size() < 2) return QVariant();
            auto* sk = getSketch(); if (!sk) return QVariant();
            return sk->constrainTangent(args[0].toString(), args[1].toString());
        }, 2, 2);

    // (sk-constrain-concentric uuid-1 uuid-2)
    d->engine->registerFunction("SK-CONSTRAIN-CONCENTRIC",
        [getSketch](const QVariantList& args) -> QVariant {
            if (args.size() < 2) return QVariant();
            auto* sk = getSketch(); if (!sk) return QVariant();
            return sk->constrainConcentric(args[0].toString(), args[1].toString());
        }, 2, 2);

    // (sk-constrain-equal-length line-uuid-1 line-uuid-2)
    d->engine->registerFunction("SK-CONSTRAIN-EQUAL-LENGTH",
        [getSketch](const QVariantList& args) -> QVariant {
            if (args.size() < 2) return QVariant();
            auto* sk = getSketch(); if (!sk) return QVariant();
            return sk->constrainEqualLength(args[0].toString(), args[1].toString());
        }, 2, 2);

    // (sk-constrain-equal-radius uuid-1 uuid-2)
    d->engine->registerFunction("SK-CONSTRAIN-EQUAL-RADIUS",
        [getSketch](const QVariantList& args) -> QVariant {
            if (args.size() < 2) return QVariant();
            auto* sk = getSketch(); if (!sk) return QVariant();
            return sk->constrainEqualRadius(args[0].toString(), args[1].toString());
        }, 2, 2);

    // (sk-constrain-collinear line-uuid-1 line-uuid-2)
    d->engine->registerFunction("SK-CONSTRAIN-COLLINEAR",
        [getSketch](const QVariantList& args) -> QVariant {
            if (args.size() < 2) return QVariant();
            auto* sk = getSketch(); if (!sk) return QVariant();
            return sk->constrainCollinear(args[0].toString(), args[1].toString());
        }, 2, 2);

    // (sk-constrain-midpoint point-uuid line-uuid)
    d->engine->registerFunction("SK-CONSTRAIN-MIDPOINT",
        [getSketch](const QVariantList& args) -> QVariant {
            if (args.size() < 2) return QVariant();
            auto* sk = getSketch(); if (!sk) return QVariant();
            return sk->constrainMidpoint(
                GeomRef(args[0].toString(), GeomHandle::WholeGeom),
                args[1].toString());
        }, 2, 2);

    // (sk-constrain-symmetric uuid-a handle-a uuid-b handle-b axis-uuid)
    d->engine->registerFunction("SK-CONSTRAIN-SYMMETRIC",
        [getSketch](const QVariantList& args) -> QVariant {
            if (args.size() < 5) return QVariant();
            auto* sk = getSketch(); if (!sk) return QVariant();
            GeomHandle ha = static_cast<GeomHandle>(args[1].toInt());
            GeomHandle hb = static_cast<GeomHandle>(args[3].toInt());
            return sk->constrainSymmetric(
                GeomRef(args[0].toString(), ha),
                GeomRef(args[2].toString(), hb),
                args[4].toString());
        }, 5, 5);

    // (sk-constrain-fixed geom-uuid)
    d->engine->registerFunction("SK-CONSTRAIN-FIXED",
        [getSketch](const QVariantList& args) -> QVariant {
            if (args.isEmpty()) return QVariant();
            auto* sk = getSketch(); if (!sk) return QVariant();
            return sk->constrainFixed(args[0].toString());
        }, 1, 1);

    // ── 尺寸約束 ─────────────────────────────────────────────────────────────

    // (sk-constrain-dist uuid-1 handle-1 uuid-2 handle-2 dist)
    //   or (sk-constrain-dist uuid-1 handle-1 uuid-2 handle-2 "expr")
    d->engine->registerFunction("SK-CONSTRAIN-DIST",
        [getSketch](const QVariantList& args) -> QVariant {
            if (args.size() < 5) return QVariant();
            auto* sk = getSketch(); if (!sk) return QVariant();
            GeomHandle h1 = static_cast<GeomHandle>(args[1].toInt());
            GeomHandle h2 = static_cast<GeomHandle>(args[3].toInt());
            bool isNum;
            double v = args[4].toDouble(&isNum);
            QString expr = isNum ? QString() : args[4].toString();
            if (!isNum) {
                auto* store = sk->parameterStore();
                v = store ? store->evaluate(expr).second : 0.0;
            }
            return sk->constrainDistance(GeomRef(args[0].toString(), h1),
                                          GeomRef(args[2].toString(), h2), v);
        }, 5, 5);

    // (sk-constrain-radius geom-uuid radius-or-expr)
    d->engine->registerFunction("SK-CONSTRAIN-RADIUS",
        [getSketch](const QVariantList& args) -> QVariant {
            if (args.size() < 2) return QVariant();
            auto* sk = getSketch(); if (!sk) return QVariant();
            bool isNum;
            double v = args[1].toDouble(&isNum);
            if (!isNum) {
                auto* store = sk->parameterStore();
                v = store ? store->evaluate(args[1].toString()).second : 0.0;
            }
            return sk->constrainRadius(args[0].toString(), v);
        }, 2, 2);

    // (sk-constrain-angle uuid-1 h1 uuid-2 h2 angle-deg)
    d->engine->registerFunction("SK-CONSTRAIN-ANGLE",
        [getSketch](const QVariantList& args) -> QVariant {
            if (args.size() < 5) return QVariant();
            auto* sk = getSketch(); if (!sk) return QVariant();
            GeomHandle h1 = static_cast<GeomHandle>(args[1].toInt());
            GeomHandle h2 = static_cast<GeomHandle>(args[3].toInt());
            double angleDeg = args[4].toDouble();
            double angleRad = angleDeg * M_PI / 180.0;
            return sk->constrainAngle(GeomRef(args[0].toString(), h1),
                                       GeomRef(args[2].toString(), h2),
                                       angleRad);
        }, 5, 5);

    // (sk-constrain-fix-x geom-uuid x-val)
    d->engine->registerFunction("SK-CONSTRAIN-FIX-X",
        [getSketch](const QVariantList& args) -> QVariant {
            if (args.size() < 2) return QVariant();
            auto* sk = getSketch(); if (!sk) return QVariant();
            return sk->constrainFixedX(GeomRef(args[0].toString(), GeomHandle::WholeGeom),
                                        args[1].toDouble());
        }, 2, 2);

    // (sk-constrain-fix-y geom-uuid y-val)
    d->engine->registerFunction("SK-CONSTRAIN-FIX-Y",
        [getSketch](const QVariantList& args) -> QVariant {
            if (args.size() < 2) return QVariant();
            auto* sk = getSketch(); if (!sk) return QVariant();
            return sk->constrainFixedY(GeomRef(args[0].toString(), GeomHandle::WholeGeom),
                                        args[1].toDouble());
        }, 2, 2);

    // ── 管理 ─────────────────────────────────────────────────────────────────

    // (sk-remove-constraint uuid)
    d->engine->registerFunction("SK-REMOVE-CONSTRAINT",
        [getSketch](const QVariantList& args) -> QVariant {
            if (args.isEmpty()) return false;
            auto* sk = getSketch(); if (!sk) return false;
            return sk->removeConstraint(args[0].toString());
        }, 1, 1);

    // (sk-list-constraints)  → list of maps: {uuid, type, value, refs}
    d->engine->registerFunction("SK-LIST-CONSTRAINTS",
        [getSketch](const QVariantList&) -> QVariant {
            auto* sk = getSketch();
            if (!sk) return QVariantList();
            QVariantList result;
            for (const auto& c : sk->constraints()) {
                QVariantMap m;
                m["uuid"]    = c.uuid;
                m["type"]    = static_cast<int>(c.type);
                m["value"]   = c.value;
                m["expr"]    = c.paramExpr;
                m["driving"] = c.driving;
                result.append(m);
            }
            return result;
        }, 0, 0);

    // (sk-dof)  → int
    d->engine->registerFunction("SK-DOF",
        [getSketch](const QVariantList&) -> QVariant {
            auto* sk = getSketch();
            return sk ? sk->degreesOfFreedom() : -999;
        }, 0, 0);

    // (sk-solve)  → t/nil
    d->engine->registerFunction("SK-SOLVE",
        [getSketch](const QVariantList&) -> QVariant {
            auto* sk = getSketch();
            if (!sk) return false;
            SolveResult r = sk->solveConstraints();
            return r.status == SolveStatus::FullyConstrained ||
                   r.status == SolveStatus::UnderConstrained;
        }, 0, 0);

    // (sk-edit-constraint uuid new-value-or-expr)
    d->engine->registerFunction("SK-EDIT-CONSTRAINT",
        [getSketch](const QVariantList& args) -> QVariant {
            if (args.size() < 2) return false;
            auto* sk = getSketch(); if (!sk) return false;
            SketchConstraint* c = sk->findConstraint(args[0].toString());
            if (!c || !c->isDimensional()) return false;
            bool isNum;
            double v = args[1].toDouble(&isNum);
            QString expr = isNum ? QString() : args[1].toString();
            if (!isNum) {
                auto* store = sk->parameterStore();
                v = store ? store->evaluate(expr).second : 0.0;
            }
            c->value     = v;
            c->paramExpr = expr;
            sk->solveConstraints();
            return true;
        }, 2, 2);

    qDebug() << "[LispBindings] Phase 8: Constraint API registered (23 functions)";
}

} // namespace scripting
} // namespace aicad
