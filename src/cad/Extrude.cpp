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
#include <BRepBuilderAPI_Transform.hxx>

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
        
        // ✅ 移除：setParent(m_sketch);
        //    原本這行呼叫的是 QObject::setParent，
        //    把 Extrude 的擁有權轉給 Sketch，導致 Sketch 析構時連帶刪除 Extrude。
        //    Feature 層的父子關係改由 Document::createExtrude 呼叫
        //    sketch->setFeatureParent(extrude) 建立。
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

    if (!m_sketch) { setError("No sketch specified"); return false; }
    if (!m_sketch->hasValidShape() || !m_sketch->hasExtrudableProfile()) {
        setError(m_sketch->hasValidShape()
                     ? "Sketch has no closed profile for extrude"
                     : "Sketch has no valid shape");
        return false;
    }

    // ✅ 只用一個變數 profile（修正：移除多餘的 face 變數）
    TopoDS_Face profile;
    if (auto reg = core::Application::instance()->selectedRegion()) {
        profile = geometry::faceFromSketchRegion(m_sketch, *reg);
    } else {
        auto regions = m_sketch->detectRegions();
        if (!regions.isEmpty())
            profile = geometry::faceFromSketchRegion(m_sketch, regions.first());
        else if (m_sketch->hasClosedProfile()) {
            BRepBuilderAPI_MakeFace fm(m_sketch->mainWire(), Standard_True);
            if (fm.IsDone()) profile = fm.Face();
        }
    }

    // Path 3: 直接從 sketch wire 建面（Polyline / Rectangle fallback）
    if (profile.IsNull()) {
        for (const TopoDS_Wire& wire : m_sketch->wires()) {
            if (wire.IsNull()) continue;
            BRepBuilderAPI_MakeFace fm(wire, Standard_True);
            if (fm.IsDone()) { profile = fm.Face(); break; }
        }
    }

    if (profile.IsNull()) { setError("No valid profile for extrude"); return false; }

    double actualHeight = m_heightExpr.cachedValue;
    if (m_reversed) actualHeight = -actualHeight;

    // ✅ 新增：防止零高度
    if (qAbs(actualHeight) < 1e-6) {
        setError("Extrude height is zero or too small");
        return false;
    }

    // ✅ 修正：從草圖平面法向量算擠出方向，不再寫死 Z 軸
    const cad::Plane* plane = m_sketch->plane();
    if (!plane) { setError("Sketch has no valid plane"); return false; }

    QVector3D n = plane->normal().normalized();
    gp_Vec direction(n.x() * actualHeight,
                     n.y() * actualHeight,
                     n.z() * actualHeight);

    TopoDS_Shape resultShape;

    if (m_symmetric) {
        // ✅ 對稱擠出：沿法向偏移 profile 半個高度，再擠出全高
        gp_Vec halfVec = direction * 0.5;
        // 將 profile 沿反方向移動 half
        gp_Trsf offset;
        offset.SetTranslation(-halfVec);
        BRepBuilderAPI_Transform mover(profile, offset, Standard_True);
        if (!mover.IsDone()) { setError("Symmetric offset failed"); return false; }
        TopoDS_Face movedProfile = TopoDS::Face(mover.Shape());

        auto result = geometry::GeometryBuilder::extrude(movedProfile, direction);
        if (!result.success) { setError(result.errorMessage); return false; }
        resultShape = result.shape;
    } else {
        auto result = geometry::GeometryBuilder::extrude(profile, direction);
        if (!result.success) { setError(result.errorMessage); return false; }
        resultShape = result.shape;
    }

    setShape(resultShape);
    return true;
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

void Extrude::resolveReferences(Document* doc) {
    if (m_pendingSketchId.isEmpty()) return;
    Feature* f = doc->findFeature(m_pendingSketchId);
    if (auto* sk = qobject_cast<Sketch*>(f)) {
        setSketch(sk);
        // ✅ 恢復 Feature 層的父子關係（setSketch 不再自動呼叫 setParent）
        sk->setFeatureParent(this);
        m_pendingSketchId.clear();
    } else {
        qWarning() << "[Extrude]" << name()
                   << "Cannot resolve sketchId:" << m_pendingSketchId;
    }
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
    m_pendingSketchId = json["sketchId"].toString();

    return true;
}

} // namespace cad
} // namespace aicad
