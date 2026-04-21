/**
 * @file Extrude.cpp
 * @brief 擠出特徵實作
 * @author AICAD Team
 * @date 2025-01-08
 */

#include "core/Application.h"
#include "geometry/GeometryBuilder.h"
#include "Extrude.h"
#include "Sketch.h"
#include "Document.h"
#include "geometry/GeometryBuilder.h"

#include <QJsonObject>
#include <QDebug>

#include <BRepBuilderAPI_MakeFace.hxx>

namespace aicad {
namespace cad {

Extrude::Extrude(Document* parent)
    : Feature(parent)
    , m_sketch(nullptr)
    , m_reversed(false)
    , m_symmetric(false)
    , m_draftAngle(0.0)
{
    setName("Extrude");
    qDebug() << "[Extrude] Created";
}

Extrude::~Extrude() {
    qDebug() << "[Extrude]" << name() << "destroyed";
}

void Extrude::setSketch(Sketch* sketch) {
    if (m_sketch == sketch) {
        return;
    }
    
    // 斷開舊草圖的連接
    if (m_sketch) {
        disconnect(m_sketch, nullptr, this, nullptr);
    }
    
    m_sketch = sketch;
    
    // 連接新草圖的信號
    if (m_sketch) {
        connect(m_sketch, &Sketch::shapeChanged,
                this, &Extrude::rebuildRequested);
        connect(m_sketch, &Sketch::geometryChanged,
                this, &Extrude::rebuildRequested);
        
        // 設定父子關係
        setParent(m_sketch);
    }
    
    qDebug() << "[Extrude]" << name() << "sketch changed to"
             << (sketch ? sketch->name() : "null");
    
    Q_EMIT sketchChanged(sketch);
    Q_EMIT rebuildRequested();
}

void Extrude::setReversed(bool reversed) {
    if (m_reversed == reversed) {
        return;
    }
    
    m_reversed = reversed;
    qDebug() << "[Extrude]" << name() << "reversed:" << reversed;
    
    Q_EMIT reversedChanged(reversed);
    Q_EMIT rebuildRequested();
}

void Extrude::setSymmetric(bool symmetric) {
    if (m_symmetric == symmetric) {
        return;
    }
    
    m_symmetric = symmetric;
    qDebug() << "[Extrude]" << name() << "symmetric:" << symmetric;
    Q_EMIT rebuildRequested();
}

void Extrude::setDraftAngle(double angle) {
    if (qFuzzyCompare(m_draftAngle, angle)) {
        return;
    }
    
    m_draftAngle = angle;
    qDebug() << "[Extrude]" << name() << "draft angle:" << angle;
    Q_EMIT rebuildRequested();
}

bool Extrude::rebuild() {
    qDebug() << "[Extrude]" << name() << "rebuilding...";
    
    if (!m_sketch) {
        QString error = "No sketch specified";
        qWarning() << "[Extrude]" << name() << error;
        setError(error);
        return false;
    }
    
    if (!m_sketch->hasValidShape() || !m_sketch->hasExtrudableProfile()) {
        QString error = m_sketch->hasValidShape()
                            ? "Sketch has no closed profile for extrude"
                            : "Sketch has no valid shape";
        qWarning() << "[Extrude]" << name() << error;
        setError(error);
        return false;
    }
    // 在 Extrude::rebuild() 中
    TopoDS_Shape profile;

    if (auto reg = core::Application::instance()->selectedRegion()) {
        // 用選取的 region 建立 face（含洞）
        profile = geometry::faceFromSketchRegion(m_sketch, *reg);
    } else if (m_sketch->hasClosedProfile()) {
        // 原本的全輪廓 extrude 行為
        BRepBuilderAPI_MakeFace faceMaker(m_sketch->mainWire(),
                                          Standard_True);
        if (faceMaker.IsDone())
            profile = faceMaker.Face();
    }

    if (profile.IsNull()) {
        setError("No valid profile for extrude");
        return false;
    }

    try {
        // 計算實際高度
        double actualHeight = m_heightExpr.cachedValue;
        if (m_reversed) {
            actualHeight = -actualHeight;
        }
        
        // 使用 GeometryBuilder 執行擠出
        auto result = geometry::GeometryBuilder::extrudeSketch(m_sketch, actualHeight);
        
        if (result) {
            setShape(result.shape);
            clearError();
            qDebug() << "[Extrude]" << name() << "rebuilt successfully";
            return true;
        } else {
            qWarning() << "[Extrude]" << name() << "build failed:" << result.errorMessage;
            setError(result.errorMessage);
            return false;
        }
        
    } catch (const Standard_Failure& e) {
        QString error = QString("OCCT error: %1").arg(e.GetMessageString());
        qCritical() << "[Extrude]" << name() << error;
        setError(error);
        return false;
    } catch (...) {
        QString error = "Unknown error during rebuild";
        qCritical() << "[Extrude]" << name() << error;
        setError(error);
        return false;
    }
}

void Extrude::setHeight(double h) {
    m_heightExpr = aicad::core::ParameterExpr(h);
    markDirty();
    Q_EMIT heightChanged(h);
    Q_EMIT rebuildRequested();
}

void Extrude::setHeightExpression(const QString& expr) {
    // 從 Document 取得 ParameterStore 求值
    if (document()) {
        auto [ok, v] = document()->parameterStore()->evaluate(expr);
        m_heightExpr = aicad::core::ParameterExpr(expr, ok ? v : m_heightExpr.cachedValue);
        if (ok) {
            markDirty();
            Q_EMIT heightChanged(v);
            Q_EMIT rebuildRequested();
        }
    } else {
        m_heightExpr.expression = expr;
    }
}

QSet<QString> Extrude::featureDependencies() const {
    if (m_sketch) return { m_sketch->id() };
    return {};
}

// toJson：儲存表達式而非計算值
QJsonObject Extrude::toJson() const {
    QJsonObject obj = Feature::toJson();
    obj["sketchId"]        = m_sketch ? m_sketch->id() : QString();
    obj["heightExpression"] = m_heightExpr.expression;   // ← 設計意圖
    obj["reversed"]        = m_reversed;
    obj["symmetric"]       = m_symmetric;
    obj["draftAngle"]      = m_draftAngle;
    return obj;
}

bool Extrude::fromJson(const QJsonObject& json) {
    Feature::fromJson(json);
    // heightExpression 優先，相容舊格式 "height"
    if (json.contains("heightExpression"))
        setHeightExpression(json["heightExpression"].toString());
    else
        setHeight(json["height"].toDouble(10.0));
    m_reversed   = json["reversed"].toBool(false);
    m_symmetric  = json["symmetric"].toBool(false);
    m_draftAngle = json["draftAngle"].toDouble(0.0);
    return true;
}

} // namespace cad
} // namespace aicad
