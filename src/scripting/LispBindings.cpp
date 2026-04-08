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

} // namespace scripting
} // namespace aicad
