/**
 * @file Revolve.cpp
 */
#include "Revolve.h"
#include "Document.h"

#include <QDebug>
#include <QJsonObject>
#include <QVector2D>
#include <QVector3D>

#include <BRep_Builder.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <Standard_Failure.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace aicad {
namespace cad {

Revolve::Revolve(Document* parent)
    : ShapingFeature(parent)
{
    setName("Revolve");
}

void Revolve::setSketch(Sketch* sketch)
{
    if (m_sketch.data() == sketch) return;

    if (m_sketch) disconnect(m_sketch.data(), nullptr, this, nullptr);
    m_sketch = sketch;

    // 草圖改動 → 本特徵重建（比照 Extrude）
    if (m_sketch) {
        connect(m_sketch.data(), &Sketch::shapeChanged,    this, &Revolve::rebuildRequested);
        connect(m_sketch.data(), &Sketch::geometryChanged, this, &Revolve::rebuildRequested);
    }
}

bool Revolve::setParameters(const QString& angleExpr, AxisKind kind, const QString& axisLineUuid,
                            bool reversed, bool symmetric, int regionIndex, QString* error)
{
    double a = 0.0;
    if (!solidops::evalExpression(document(), angleExpr, &a)) {
        if (error) *error = QStringLiteral("角度運算式無法求值：%1").arg(angleExpr);
        return false;
    }
    if (std::abs(a) < 1e-6 || std::abs(a) > 360.0 + 1e-6) {
        if (error) *error = QStringLiteral("角度必須介於 −360° ~ 360°，且不可為 0");
        return false;
    }
    if (kind == AxisKind::SketchLine && axisLineUuid.isEmpty()) {
        if (error) *error = QStringLiteral("請選擇旋轉軸（草圖內的直線）");
        return false;
    }

    m_angleExpr    = core::ParameterExpr(angleExpr.trimmed(), a);
    m_axisKind     = kind;
    m_axisLineUuid = (kind == AxisKind::SketchLine) ? axisLineUuid : QString();
    m_reversed     = reversed;
    m_symmetric    = symmetric;
    m_regionIndex  = regionIndex;
    return true;
}

bool Revolve::resolveAxis(gp_Ax1& out, QString* err) const
{
    if (!m_sketch) {
        if (err) *err = QStringLiteral("尚未指定草圖");
        return false;
    }

    QVector3D p0, p1;
    switch (m_axisKind) {
    case AxisKind::SketchX:
        p0 = m_sketch->planeToWorld(QVector2D(0.0f, 0.0f));
        p1 = m_sketch->planeToWorld(QVector2D(1.0f, 0.0f));
        break;
    case AxisKind::SketchY:
        p0 = m_sketch->planeToWorld(QVector2D(0.0f, 0.0f));
        p1 = m_sketch->planeToWorld(QVector2D(0.0f, 1.0f));
        break;
    case AxisKind::SketchLine: {
        const auto* line = dynamic_cast<const SketchLine*>(m_sketch->findGeometry(m_axisLineUuid));
        if (!line) {
            if (err) *err = QStringLiteral("找不到旋轉軸（草圖內的直線可能已被刪除）");
            return false;
        }
        p0 = m_sketch->planeToWorld(line->start);
        p1 = m_sketch->planeToWorld(line->end);
        break;
    }
    }

    const gp_Vec v(p1.x() - p0.x(), p1.y() - p0.y(), p1.z() - p0.z());
    if (v.Magnitude() < 1e-9) {
        if (err) *err = QStringLiteral("旋轉軸長度為 0");
        return false;
    }
    out = gp_Ax1(gp_Pnt(p0.x(), p0.y(), p0.z()), gp_Dir(v));
    return true;
}

bool Revolve::rebuild()
{
    if (!m_sketch) { setError("No sketch specified"); return false; }

    QString err;
    const QList<TopoDS_Face> faces = solidops::profileFaces(m_sketch.data(), m_regionIndex, &err);
    if (faces.isEmpty()) {
        setError(err.isEmpty() ? QStringLiteral("草圖沒有封閉輪廓") : err);
        return false;
    }

    // 角度：運算式優先（參數可能剛被改過），求不出來就用快取值
    double angleDeg = m_angleExpr.cachedValue;
    double evaluated = 0.0;
    if (solidops::evalExpression(document(), m_angleExpr.expression, &evaluated)) {
        angleDeg = evaluated;
        m_angleExpr.cachedValue = evaluated;
    }
    if (std::abs(angleDeg) < 1e-6 || std::abs(angleDeg) > 360.0 + 1e-6) {
        setError(QStringLiteral("角度必須介於 −360° ~ 360°，且不可為 0"));
        return false;
    }

    gp_Ax1 axis;
    if (!resolveAxis(axis, &err)) { setError(err); return false; }
    if (m_reversed != (angleDeg < 0.0)) axis.Reverse();       // 反向與負角度各反一次

    const bool   full = std::abs(angleDeg) >= 360.0 - 1e-6;
    const double ang  = std::abs(angleDeg) * M_PI / 180.0;

    try {
        BRep_Builder bb;
        TopoDS_Compound tools;
        bb.MakeCompound(tools);

        for (const TopoDS_Face& face : faces) {
            TopoDS_Shape f = face;

            if (m_symmetric && !full) {
                gp_Trsf rot;
                rot.SetRotation(axis, -ang * 0.5);
                BRepBuilderAPI_Transform mover(face, rot, Standard_True);
                if (!mover.IsDone()) { setError("Symmetric rotation failed"); return false; }
                f = mover.Shape();
            }

            TopoDS_Shape revolved;
            if (full) {
                BRepPrimAPI_MakeRevol mk(f, axis, Standard_True);
                if (!mk.IsDone()) {
                    setError(QStringLiteral("旋轉失敗：輪廓不可橫跨旋轉軸，也不能與旋轉軸重疊"));
                    return false;
                }
                revolved = mk.Shape();
            } else {
                BRepPrimAPI_MakeRevol mk(f, axis, ang, Standard_True);
                if (!mk.IsDone()) {
                    setError(QStringLiteral("旋轉失敗：輪廓不可橫跨旋轉軸，也不能與旋轉軸重疊"));
                    return false;
                }
                revolved = mk.Shape();
            }
            bb.Add(tools, revolved);
        }

        TopoDS_Shape tool = solidops::finalizeSolid(tools, &err);
        if (tool.IsNull()) {
            setError(err.isEmpty() ? QStringLiteral("旋轉結果無效") : err);
            return false;
        }

        TopoDS_Shape result = combineWithTarget(tool);      // 失敗時已 setError
        if (result.IsNull()) return false;

        if (operation() != SolidOp::NewBody) {
            result = solidops::finalizeSolid(result, &err);
            if (result.IsNull()) {
                setError(err.isEmpty() ? QStringLiteral("運算結果沒有任何實體") : err);
                return false;
            }
        }

        setShape(result);
        return true;

    } catch (const Standard_Failure& ex) {
        setError(QStringLiteral("旋轉發生 OCC 例外：%1")
                     .arg(ex.GetMessageString() ? ex.GetMessageString() : "unknown"));
        return false;
    }
}

QSet<QString> Revolve::featureDependencies() const
{
    QSet<QString> deps = targetDependencies();
    if (m_sketch) deps.insert(m_sketch->id());
    return deps;
}

void Revolve::resolveReferences(Document* doc)
{
    if (!doc) return;
    resolveTargetReference(doc);

    if (!m_pendingSketchId.isEmpty()) {
        if (auto* sk = qobject_cast<Sketch*>(doc->findFeature(m_pendingSketchId))) {
            setSketch(sk);
            if (!sk->parent()) sk->setFeatureParent(this);
            m_pendingSketchId.clear();
        } else {
            qWarning() << "[Revolve]" << name() << "cannot resolve sketchId:" << m_pendingSketchId;
        }
    }
    doc->refreshFeatureDependencies(this);
}

QJsonObject Revolve::toJson() const
{
    QJsonObject obj = Feature::toJson();
    obj["sketchId"]        = m_sketch ? m_sketch->id() : m_pendingSketchId;
    obj["regionIndex"]     = m_regionIndex;
    obj["axisKind"]        = static_cast<int>(m_axisKind);
    obj["axisLineUuid"]    = m_axisLineUuid;
    obj["angleExpression"] = m_angleExpr.expression;
    obj["reversed"]        = m_reversed;
    obj["symmetric"]       = m_symmetric;
    writeOpJson(obj);
    return obj;
}

bool Revolve::fromJson(const QJsonObject& json)
{
    if (!Feature::fromJson(json)) return false;

    m_pendingSketchId = json["sketchId"].toString();
    m_regionIndex     = json["regionIndex"].toInt(-1);
    m_axisKind        = static_cast<AxisKind>(json["axisKind"].toInt(static_cast<int>(AxisKind::SketchY)));
    m_axisLineUuid    = json["axisLineUuid"].toString();
    m_reversed        = json["reversed"].toBool(false);
    m_symmetric       = json["symmetric"].toBool(false);

    const QString expr = json["angleExpression"].toString(QStringLiteral("360"));
    double v = 360.0;
    solidops::evalExpression(document(), expr, &v);       // 讀檔時全域參數已載入
    m_angleExpr = core::ParameterExpr(expr, v);

    readOpJson(json);
    return true;
}

} // namespace cad
} // namespace aicad
