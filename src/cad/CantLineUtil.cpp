/**
 * @file CantLineUtil.cpp
 */
#include "CantLineUtil.h"
#include "SketchInstance.h"
#include "Sketch.h"
#include "sketch/SketchAnnotation.h"
#include "../core/ParameterStore.h"

#include <QRegularExpression>
#include <QStringList>
#include <memory>

namespace aicad {
namespace cad {

namespace {
bool exprDependsOn(const QString& expr, const QSet<QString>& names)
{
    if (expr.trimmed().isEmpty()) return false;
    static const QRegularExpression re(QStringLiteral("[A-Za-z_][A-Za-z0-9_]*"));
    auto it = re.globalMatch(expr);
    while (it.hasNext())
        if (names.contains(it.next().captured())) return true;
    return false;
}
} // namespace

QSet<QString> cantDependentNames(const core::ParameterStore* store)
{
    QSet<QString> names;
    const QString root = QStringLiteral("cant");
    names.insert(root);
    if (!store) return names;
    QStringList queue{ root };
    while (!queue.isEmpty()) {
        const QString n = queue.takeFirst();
        for (const QString& dep : store->dependentsOf(n)) {
            if (names.contains(dep)) continue;
            names.insert(dep);
            queue.append(dep);
        }
    }
    return names;
}

SketchLine* locateCantLine(Sketch* view, const QSet<QString>& cantNames, QString* err)
{
    auto fail = [&](const QString& m) -> SketchLine* { if (err) *err = m; return nullptr; };
    if (!view) return fail(QStringLiteral("草圖不存在"));

    QSet<QString> ownGeom;                       // 排除基準軸 datum 等內部物件
    for (SketchGeometry* g : view->geometriesRef()) if (g) ownGeom.insert(g->uuid);

    QSet<QString> cantPoints;
    for (const SketchAnnotation& an : view->annotations()) {
        if (!exprDependsOn(an.paramExpr, cantNames)) continue;
        for (const GeomRef& r : an.refs) {
            if (!ownGeom.contains(r.geomUuid)) continue;
            SketchGeometry* g = view->findGeometry(r.geomUuid);
            if (!g) continue;
            if (g->type == SketchGeometryType::Point) {
                cantPoints.insert(g->uuid);
            } else if (g->type == SketchGeometryType::Line) {
                auto* l = static_cast<SketchLine*>(g);
                cantPoints.insert(l->startUuid);
                cantPoints.insert(l->endUuid);
            }
        }
    }
    if (cantPoints.isEmpty())
        return fail(QStringLiteral("斷面草圖中沒有任何尺寸依賴參數「cant」"));

    SketchLine* best = nullptr;
    for (SketchGeometry* g : view->geometriesRef()) {
        if (!g || g->type != SketchGeometryType::Line) continue;
        if (g->role == GeomRole::Centerline) continue;          // 組件座標線不是軌道面斷線
        auto* l = static_cast<SketchLine*>(g);
        if (!cantPoints.contains(l->startUuid) || !cantPoints.contains(l->endUuid)) continue;
        if (!best || (l->isConstruction() && !best->isConstruction())) best = l;
    }
    if (!best) return fail(QStringLiteral("找不到兩端都受 cant 約束的直線"));
    return best;
}

CantLineUV findCantLineUV(SketchInstance* inst, const QSet<QString>& cantNames, QString* err)
{
    CantLineUV out;
    auto fail = [&](const QString& m) { if (err) *err = m; return out; };

    if (!inst) return fail(QStringLiteral("測站不存在"));

    // 本站「實際套用值」的唯讀檢視草圖：幾何是本站求解後的結果，標註運算式保留
    std::unique_ptr<Sketch> view(inst->createViewSketch());
    if (!view) return fail(QStringLiteral("測站 %1 尚未求解，無法取得斷面幾何").arg(inst->name()));

    QString e;
    SketchLine* best = locateCantLine(view.get(), cantNames, &e);
    if (!best) return fail(e);

    auto pointPos = [&](const QString& uuid, QVector2D& pos) {
        SketchGeometry* g = view->findGeometry(uuid);
        if (!g || g->type != SketchGeometryType::Point) return false;
        pos = static_cast<SketchPoint*>(g)->pos;
        return true;
    };
    QVector2D p0, p1;
    if (!pointPos(best->startUuid, p0) || !pointPos(best->endUuid, p1))
        return fail(QStringLiteral("軌道面直線的端點座標無法取得"));

    const float sc = static_cast<float>(inst->scale());
    out.a  = p0 * sc;
    out.b  = p1 * sc;
    out.ok = true;
    return out;
}

} // namespace cad
} // namespace aicad
