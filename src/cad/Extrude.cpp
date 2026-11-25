/**
 * @file Extrude.cpp
 * @brief 擠出特徵實作
 * @author AICAD Team
 * @date 2025-01-08
 */

#include "core/Application.h"
#include "geometry/GeometryBuilder.h"
#include "Extrude.h"
#include <algorithm>
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
    : ShapingFeature(parent)
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

    // 反向附加長度（非對稱兩側）：總長 = 正向高度 + 反向附加長度，起點沿反方向後移
    const double backLen = m_symmetric ? 0.0 : std::max(0.0, m_backExpr.cachedValue);

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

    if (qAbs(m_draftAngle) > 1e-9) {
        // ✅ 拔模擠出（錐度）：目前限單向、無內孔輪廓
        if (m_symmetric || backLen > 1e-9) {
            setError("拔模擠出目前不支援「兩向平分」或「反向附加長度」");
            return false;
        }
        QString taperErr;
        resultShape = solidops::taperedPrism(profile, direction, m_draftAngle, &taperErr);
        if (resultShape.IsNull()) {
            setError(taperErr.isEmpty() ? QStringLiteral("拔模擠出失敗") : taperErr);
            return false;
        }
    } else if (m_symmetric) {
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
    } else if (backLen > 1e-9) {
        // 非對稱兩側：profile 沿「反向」後移 backLen，再擠出 (height + backLen)
        const double sign = (actualHeight >= 0.0) ? 1.0 : -1.0;
        const double total = qAbs(actualHeight) + backLen;
        gp_Trsf shift;
        shift.SetTranslation(gp_Vec(-n.x() * sign * backLen, -n.y() * sign * backLen, -n.z() * sign * backLen));
        BRepBuilderAPI_Transform mover(profile, shift, Standard_True);
        if (!mover.IsDone()) { setError("Back-extension offset failed"); return false; }
        TopoDS_Face movedProfile = TopoDS::Face(mover.Shape());
        const gp_Vec dirTotal(n.x() * sign * total, n.y() * sign * total, n.z() * sign * total);

        auto result = geometry::GeometryBuilder::extrude(movedProfile, dirTotal);
        if (!result.success) { setError(result.errorMessage); return false; }
        resultShape = result.shape;
    } else {
        auto result = geometry::GeometryBuilder::extrude(profile, direction);
        if (!result.success) { setError(result.errorMessage); return false; }
        resultShape = result.shape;
    }

    // ✅ 運算方式（聯集／挖除／交集）：NewBody 時完全維持原本行為
    if (operation() != SolidOp::NewBody) {
        QString ferr;
        const TopoDS_Shape tool = solidops::finalizeSolid(resultShape, &ferr);
        if (tool.IsNull()) {
            setError(ferr.isEmpty() ? QStringLiteral("擠出形狀無效") : ferr);
            return false;
        }
        const TopoDS_Shape combined = combineWithTarget(tool);   // 失敗時已 setError
        if (combined.IsNull()) return false;

        resultShape = solidops::finalizeSolid(combined, &ferr);
        if (resultShape.IsNull()) {
            setError(ferr.isEmpty() ? QStringLiteral("運算結果沒有任何實體") : ferr);
            return false;
        }
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

bool Extrude::setParameters(const QString& heightExpr, const QString& backExpr,
                            bool reversed, bool symmetric, QString* error)
{
    auto eval = [&](const QString& expr, double& out) -> bool {
        const QString e = expr.trimmed();
        if (e.isEmpty()) { out = 0.0; return true; }
        bool lit = false;
        const double v = e.toDouble(&lit);
        if (lit) { out = v; return true; }
        if (!document()) return false;
        const auto r = document()->parameterStore()->evaluate(e);
        if (!r.first) return false;
        out = r.second;
        return true;
    };

    double h = 0.0, b = 0.0;
    if (!eval(heightExpr, h)) { if (error) *error = QStringLiteral("高度運算式無法求值：%1").arg(heightExpr); return false; }
    if (!eval(backExpr, b))   { if (error) *error = QStringLiteral("反向附加長度運算式無法求值：%1").arg(backExpr); return false; }
    if (qAbs(h) < 1e-6)       { if (error) *error = QStringLiteral("擠出高度不可為 0"); return false; }
    if (b < 0.0)              { if (error) *error = QStringLiteral("反向附加長度不可為負"); return false; }

    m_heightExpr = aicad::core::ParameterExpr(heightExpr.trimmed(), h);
    m_backExpr   = aicad::core::ParameterExpr(backExpr.trimmed().isEmpty() ? QStringLiteral("0") : backExpr.trimmed(), b);
    m_reversed   = reversed;
    m_symmetric  = symmetric;

    markDirty();
    Q_EMIT heightChanged(h);
    Q_EMIT reversedChanged(reversed);
    Q_EMIT rebuildRequested();          // 只重建一次
    return true;
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
    QSet<QString> deps = targetDependencies();      // 挖除／聯集的目標實體
    if (m_sketch) deps.insert(m_sketch->id());
    return deps;
}

void Extrude::resolveReferences(Document* doc) {
    if (!doc) return;

    // 目標實體（挖除／聯集／交集）
    resolveTargetReference(doc);

    if (!m_pendingSketchId.isEmpty()) {
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

    // 依賴圖在建立（fromJson 之前）時 sketch／target 都還是空的，這裡補登記，
    // 讓讀檔後的拓撲重建順序正確（草圖／目標先於本特徵）。
    doc->refreshFeatureDependencies(this);
}

// toJson：儲存表達式而非計算值
QJsonObject Extrude::toJson() const {
    QJsonObject obj = Feature::toJson();
    obj["sketchId"]        = m_sketch ? m_sketch->id() : QString();
    obj["heightExpression"] = m_heightExpr.expression;   // ← 設計意圖
    obj["reversed"]        = m_reversed;
    obj["symmetric"]       = m_symmetric;
    obj["backExpression"]  = m_backExpr.expression;
    obj["draftAngle"]      = m_draftAngle;
    writeOpJson(obj);                                    // operation／targetId
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
    if (json.contains("backExpression")) {
        const QString be = json["backExpression"].toString();
        bool lit = false; double bv = be.toDouble(&lit);
        if (!lit) {
            bv = 0.0;
            if (document() && !be.trimmed().isEmpty()) {
                const auto r = document()->parameterStore()->evaluate(be);
                if (r.first) bv = r.second;
            }
        }
        m_backExpr = aicad::core::ParameterExpr(be, bv);
    }
    m_draftAngle = json["draftAngle"].toDouble(0.0);
    m_pendingSketchId = json["sketchId"].toString();
    readOpJson(json);

    return true;
}

} // namespace cad
} // namespace aicad
