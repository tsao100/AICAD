/**
 * @file Extrude.cpp
 * @brief 擠出特徵實作
 * @author AICAD Team
 * @date 2025-01-08
 */

#include "Extrude.h"
#include "Sketch.h"
#include "Document.h"
#include "geometry/GeometryBuilder.h"

#include <QJsonObject>
#include <QDebug>

namespace aicad {
namespace cad {

Extrude::Extrude(Document* parent)
    : Feature(parent)
    , m_sketch(nullptr)
    , m_height(10.0)
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

void Extrude::setHeight(double height) {
    if (qFuzzyCompare(m_height, height)) {
        return;
    }
    
    m_height = height;
    qDebug() << "[Extrude]" << name() << "height changed to" << height;
    
    Q_EMIT heightChanged(height);
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
    
    if (!m_sketch->hasValidShape()) {
        QString error = "Sketch has no valid shape";
        qWarning() << "[Extrude]" << name() << error;
        setError(error);
        return false;
    }
    
    try {
        // 計算實際高度
        double actualHeight = m_height;
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

QJsonObject Extrude::toJson() const {
    QJsonObject json = Feature::toJson();
    
    json["height"] = m_height;
    json["reversed"] = m_reversed;
    json["symmetric"] = m_symmetric;
    json["draftAngle"] = m_draftAngle;
    
    if (m_sketch) {
        json["sketchId"] = m_sketch->id();
    }
    
    return json;
}

bool Extrude::fromJson(const QJsonObject& json) {
    if (!Feature::fromJson(json)) {
        return false;
    }
    
    if (json.contains("height")) {
        m_height = json["height"].toDouble();
    }
    
    if (json.contains("reversed")) {
        m_reversed = json["reversed"].toBool();
    }
    
    if (json.contains("symmetric")) {
        m_symmetric = json["symmetric"].toBool();
    }
    
    if (json.contains("draftAngle")) {
        m_draftAngle = json["draftAngle"].toDouble();
    }
    
    // 注意：草圖參考需要在所有特徵載入後才能建立
    // 這通常由 Document 處理
    
    return true;
}

} // namespace cad
} // namespace aicad