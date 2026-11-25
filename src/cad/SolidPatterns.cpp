/**
 * @file SolidPatterns.cpp
 */
#include "SolidPatterns.h"
#include "ShapingFeature.h"
#include "Document.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>

#include <BRepBuilderAPI_Transform.hxx>
#include <Standard_Failure.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>

#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace aicad {
namespace cad {

namespace {

QJsonArray vecToJson(const gp_Vec& v) { return QJsonArray{ v.X(), v.Y(), v.Z() }; }
QJsonArray ptToJson(const gp_Pnt& p)  { return QJsonArray{ p.X(), p.Y(), p.Z() }; }

gp_Vec vecFromJson(const QJsonValue& v, const gp_Vec& def)
{
    const QJsonArray a = v.toArray();
    if (a.size() != 3) return def;
    return gp_Vec(a.at(0).toDouble(), a.at(1).toDouble(), a.at(2).toDouble());
}
gp_Pnt ptFromJson(const QJsonValue& v, const gp_Pnt& def)
{
    const QJsonArray a = v.toArray();
    if (a.size() != 3) return def;
    return gp_Pnt(a.at(0).toDouble(), a.at(1).toDouble(), a.at(2).toDouble());
}

} // namespace

// ═════════════════════════ SolidTransformCopy ═════════════════════════

SolidTransformCopy::SolidTransformCopy(Document* parent)
    : SolidModifier(parent)
{
}

bool SolidTransformCopy::rebuild()
{
    if (!m_source) { setError(QStringLiteral("尚未指定來源")); return false; }

    const TopoDS_Shape base = m_source->shape();
    if (base.IsNull()) {
        setError(QStringLiteral("來源「%1」目前沒有有效的形狀").arg(m_source->name()));
        return false;
    }

    QVector<gp_Trsf> trs;
    QString err;
    if (!buildTransforms(trs, &err)) { setError(err); return false; }
    if (trs.isEmpty()) { setError(QStringLiteral("沒有產生任何副本（數量至少要 2）")); return false; }

    try {
        TopoDS_Shape result;

        if (m_mode == Mode::Body) {
            QList<TopoDS_Shape> copies;
            for (const gp_Trsf& t : trs) {
                BRepBuilderAPI_Transform mk(base, t, Standard_True);
                if (!mk.IsDone() || mk.Shape().IsNull()) {
                    setError(QStringLiteral("複製實體失敗"));
                    return false;
                }
                copies.append(mk.Shape());
            }
            if (keepOriginal()) result = solidops::applyOpMulti(base, copies, SolidOp::Fuse, &err);
            else                result = solidops::fuseAll(copies, &err);
        } else {
            auto* sf = qobject_cast<ShapingFeature*>(m_source.data());
            if (!sf || sf->operation() == SolidOp::NewBody) {
                setError(QStringLiteral("「特徵」模式的來源必須是挖除／聯集類的成形特徵（例如孔、擠出切除）"));
                return false;
            }
            const TopoDS_Shape tool = sf->toolShape();
            if (tool.IsNull()) {
                setError(QStringLiteral("來源特徵尚未產生有效的工具形狀"));
                return false;
            }
            QList<TopoDS_Shape> copies;
            for (const gp_Trsf& t : trs) {
                BRepBuilderAPI_Transform mk(tool, t, Standard_True);
                if (!mk.IsDone() || mk.Shape().IsNull()) {
                    setError(QStringLiteral("複製特徵失敗"));
                    return false;
                }
                copies.append(mk.Shape());
            }
            result = solidops::applyOpMulti(base, copies, sf->operation(), &err);
        }

        if (result.IsNull()) {
            setError(err.isEmpty() ? QStringLiteral("陣列／鏡射運算失敗") : err);
            return false;
        }
        result = solidops::finalizeSolid(result, &err);
        if (result.IsNull()) {
            setError(err.isEmpty() ? QStringLiteral("結果沒有任何實體") : err);
            return false;
        }
        setShape(result);
        return true;

    } catch (const Standard_Failure& ex) {
        setError(QStringLiteral("陣列／鏡射發生 OCC 例外：%1")
                     .arg(ex.GetMessageString() ? ex.GetMessageString() : "unknown"));
        return false;
    }
}

QJsonObject SolidTransformCopy::toJson() const
{
    QJsonObject obj = Feature::toJson();
    writeSourceJson(obj);
    obj["mode"] = static_cast<int>(m_mode);
    writeParams(obj);
    return obj;
}

bool SolidTransformCopy::fromJson(const QJsonObject& json)
{
    if (!Feature::fromJson(json)) return false;
    readSourceJson(json);
    m_mode = json["mode"].toInt(0) == 1 ? Mode::Feature : Mode::Body;
    readParams(json);
    return true;
}

// ═════════════════════════ SolidPattern ═════════════════════════

SolidPattern::SolidPattern(Document* parent)
    : SolidTransformCopy(parent)
{
    setName("Pattern");
}

bool SolidPattern::buildTransforms(QVector<gp_Trsf>& out, QString* err) const
{
    out.clear();

    if (m_p.kind == PatternParams::Kind::Linear) {
        if (m_p.count1 < 1 || m_p.count2 < 1 || m_p.count1 * m_p.count2 < 2) {
            if (err) *err = QStringLiteral("陣列的總數量至少要 2");
            return false;
        }
        if (m_p.count1 * m_p.count2 > 2000) {
            if (err) *err = QStringLiteral("陣列數量過多（上限 2000）");
            return false;
        }
        if (m_p.dir1.Magnitude() < 1e-9 || (m_p.useDir2 && m_p.dir2.Magnitude() < 1e-9)) {
            if (err) *err = QStringLiteral("陣列方向不可為零向量");
            return false;
        }
        const gp_Vec d1 = m_p.dir1.Normalized() * m_p.spacing1;
        const gp_Vec d2 = m_p.useDir2 ? m_p.dir2.Normalized() * m_p.spacing2 : gp_Vec(0, 0, 0);
        const int c2 = m_p.useDir2 ? m_p.count2 : 1;

        for (int j = 0; j < c2; ++j) {
            for (int i = 0; i < m_p.count1; ++i) {
                if (i == 0 && j == 0) continue;
                gp_Trsf t;
                t.SetTranslation(d1 * double(i) + d2 * double(j));
                out.append(t);
            }
        }
        return true;
    }

    // 環狀
    if (m_p.count < 2) {
        if (err) *err = QStringLiteral("環狀陣列的數量至少要 2");
        return false;
    }
    if (m_p.count > 2000) {
        if (err) *err = QStringLiteral("陣列數量過多（上限 2000）");
        return false;
    }
    if (m_p.axisDir.Magnitude() < 1e-9) {
        if (err) *err = QStringLiteral("旋轉軸方向不可為零向量");
        return false;
    }
    const bool   full = std::abs(m_p.totalAngle) >= 360.0 - 1e-9;
    const double step = (full ? m_p.totalAngle / m_p.count : m_p.totalAngle / (m_p.count - 1))
                        * M_PI / 180.0;
    if (std::abs(step) < 1e-12) {
        if (err) *err = QStringLiteral("角度不可為 0");
        return false;
    }
    const gp_Ax1 axis(m_p.axisOrigin, gp_Dir(m_p.axisDir));
    for (int i = 1; i < m_p.count; ++i) {
        gp_Trsf t;
        t.SetRotation(axis, step * i);
        out.append(t);
    }
    return true;
}

void SolidPattern::writeParams(QJsonObject& o) const
{
    o["kind"]       = static_cast<int>(m_p.kind);
    o["dir1"]       = vecToJson(m_p.dir1);
    o["count1"]     = m_p.count1;
    o["spacing1"]   = m_p.spacing1;
    o["useDir2"]    = m_p.useDir2;
    o["dir2"]       = vecToJson(m_p.dir2);
    o["count2"]     = m_p.count2;
    o["spacing2"]   = m_p.spacing2;
    o["axisOrigin"] = ptToJson(m_p.axisOrigin);
    o["axisDir"]    = vecToJson(m_p.axisDir);
    o["count"]      = m_p.count;
    o["totalAngle"] = m_p.totalAngle;
}

void SolidPattern::readParams(const QJsonObject& o)
{
    PatternParams p;
    p.kind       = o["kind"].toInt(0) == 1 ? PatternParams::Kind::Circular : PatternParams::Kind::Linear;
    p.dir1       = vecFromJson(o["dir1"], p.dir1);
    p.count1     = o["count1"].toInt(p.count1);
    p.spacing1   = o["spacing1"].toDouble(p.spacing1);
    p.useDir2    = o["useDir2"].toBool(false);
    p.dir2       = vecFromJson(o["dir2"], p.dir2);
    p.count2     = o["count2"].toInt(p.count2);
    p.spacing2   = o["spacing2"].toDouble(p.spacing2);
    p.axisOrigin = ptFromJson(o["axisOrigin"], p.axisOrigin);
    p.axisDir    = vecFromJson(o["axisDir"], p.axisDir);
    p.count      = o["count"].toInt(p.count);
    p.totalAngle = o["totalAngle"].toDouble(p.totalAngle);
    m_p = p;
}

// ═════════════════════════ SolidMirror ═════════════════════════

SolidMirror::SolidMirror(Document* parent)
    : SolidTransformCopy(parent)
{
    setName("Mirror");
}

bool SolidMirror::buildTransforms(QVector<gp_Trsf>& out, QString* err) const
{
    out.clear();
    if (m_normal.Magnitude() < 1e-9) {
        if (err) *err = QStringLiteral("鏡射平面的法向量不可為零向量");
        return false;
    }
    gp_Trsf t;
    t.SetMirror(gp_Ax2(m_point, gp_Dir(m_normal)));      // 以通過 m_point、垂直於 m_normal 的平面為鏡面
    out.append(t);
    return true;
}

void SolidMirror::writeParams(QJsonObject& o) const
{
    o["planePoint"]    = ptToJson(m_point);
    o["planeNormal"]   = vecToJson(m_normal);
    o["keepOriginal"]  = m_keepOriginal;
}

void SolidMirror::readParams(const QJsonObject& o)
{
    m_point        = ptFromJson(o["planePoint"], m_point);
    m_normal       = vecFromJson(o["planeNormal"], m_normal);
    m_keepOriginal = o["keepOriginal"].toBool(true);
}

} // namespace cad
} // namespace aicad
