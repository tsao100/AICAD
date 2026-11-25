/**
 * @file AssemblyUtil.cpp
 */
#include "AssemblyUtil.h"
#include "AssemblyPart.h"
#include "Document.h"

#include <QObject>
#include <algorithm>
#include <cmath>

#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

namespace aicad {
namespace cad {
namespace assemblyutil {

using namespace asmsolve;

void buildBodies(const Document* doc, Problem& pr, QHash<QString, int>& idx)
{
    pr.bodies.clear();
    idx.clear();
    double L = 0.0;
    const QVector<AssemblyPart*> parts = doc ? doc->assemblyParts() : QVector<AssemblyPart*>();
    for (int i = 0; i < parts.size(); ++i) {
        const AssemblyPart* p = parts[i];
        idx.insert(p->id(), i);
        Body b;
        double R[9], T[3];
        AssemblyPart::poseToRT(p->pose(), R, T);
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) b.R(r, c) = R[r * 3 + c];
            b.t[r] = T[r];
        }
        b.scale = p->unitScale();
        b.fixed = p->isFixed();
        double ctr[3] = {0, 0, 0}, diag = 0.0;
        if (assemblygeom::boundsOf(p->localShape(), ctr, &diag)) {
            b.center = Vec3(ctr[0], ctr[1], ctr[2]);
            L = std::max(L, diag * b.scale);
        }
        pr.bodies.push_back(b);
    }
    pr.lengthScale = L > 1e-9 ? L : 1.0;
}

Ref toSolverRef(const MateRefData& r, const QHash<QString, int>& idx)
{
    Ref x;
    x.body = r.isWorld() ? -1 : idx.value(r.ownerId, -2);
    x.kind = r.kind;
    x.p = Vec3(r.p[0], r.p[1], r.p[2]);
    x.d = Vec3(r.d[0], r.d[1], r.d[2]);
    return x;
}

bool measure(const Document* doc, const AssemblyMate::Data& d, double& value, Orient& suggested)
{
    Problem pr;
    QHash<QString, int> idx;
    buildBodies(doc, pr, idx);
    const Ref a = toSolverRef(d.a, idx), b = toSolverRef(d.b, idx);
    if (a.body == -2 || b.body == -2) return false;
    if (!isValidPair(d.type, a.kind, b.kind)) return false;

    suggested = closestOrient(pr, d.type, a, b);
    if (!mateUsesValue(d.type, a.kind, b.kind)) return false;
    double v = 0.0;
    const Orient o = mateUsesOrient(d.type, a.kind, b.kind) ? d.orient : Orient::Any;
    if (!measureValue(pr, d.type, a, b, o, v)) return false;
    value = (d.type == MateType::Angle) ? v * 180.0 / 3.14159265358979323846 : v;
    return true;
}

QVector<MateMarker> buildMarkers(const Document* doc, double* sizeOut)
{
    QVector<MateMarker> out;
    Problem pr;
    QHash<QString, int> idx;
    buildBodies(doc, pr, idx);
    if (sizeOut) *sizeOut = pr.lengthScale * 0.03;
    if (!doc) return out;

    for (const AssemblyMate* m : doc->assemblyMates()) {
        MateMarker mk;
        mk.mateId = m->id();
        mk.state = m->isSuppressed() ? MateMarker::Suppressed : (m->hasError() ? MateMarker::Error : MateMarker::Ok);
        bool valid = true;
        for (int i = 0; i < 2; ++i) {
            const Ref r = toSolverRef(i == 0 ? m->refA() : m->refB(), idx);
            if (r.body == -2) { valid = false; break; }          // 參照的零件已不存在
            const WorldGeom w = worldGeom(pr, r);
            mk.kind[i] = r.kind;
            for (int k = 0; k < 3; ++k) { mk.p[i][k] = w.p[k]; mk.d[i][k] = w.d[k]; }
        }
        if (valid) out.append(mk);
    }
    return out;
}

bool refFromPickedShape(const Document* doc, const QString& partId, const TopoDS_Shape& placedSub,
                        MateRefData& out, QString* error)
{
    auto fail = [&](const QString& m) { if (error) *error = m; return false; };
    const auto* part = doc ? qobject_cast<AssemblyPart*>(doc->findFeature(partId)) : nullptr;
    if (!part) return fail(QObject::tr("選到的不是組合中的零件"));

    MateRefData r;
    if (!assemblygeom::fromSubShape(placedSub, r, error)) return false;     // 世界座標

    // 世界 → 零件本地（原生）座標：套用 完整變換（pose ∘ scale）的反運算
    const gp_Trsf inv = part->fullTrsf().Inverted();
    gp_Pnt p(r.p[0], r.p[1], r.p[2]);
    p.Transform(inv);
    gp_Vec d(r.d[0], r.d[1], r.d[2]);
    d.Transform(inv);
    const double n = d.Magnitude();
    if (n > 1e-300) d.Divide(n);

    r.ownerId = partId;
    r.p[0] = p.X(); r.p[1] = p.Y(); r.p[2] = p.Z();
    r.d[0] = d.X(); r.d[1] = d.Y(); r.d[2] = d.Z();

    // 序號：先在放置後的形狀中找，再用本地形狀驗證幾何一致；不一致就不記序號
    const int idx = assemblygeom::indexOf(part->shape(), placedSub);
    r.topoIndex = -1;
    if (idx > 0) {
        MateRefData chk = r;
        chk.topoIndex = idx;
        if (assemblygeom::refresh(part->localShape(), chk)) {
            double dp = 0.0;
            for (int i = 0; i < 3; ++i) dp = std::max(dp, std::abs(chk.p[i] - r.p[i]));
            double dd = 0.0;
            for (int i = 0; i < 3; ++i) dd = std::max(dd, std::abs(chk.d[i] - r.d[i]));
            const double tol = 1e-6 * (1.0 + std::abs(r.p[0]) + std::abs(r.p[1]) + std::abs(r.p[2]));
            // 軸線方向允許相反（邊的參數方向）；平面法向量必須一致
            const double ddAbs = r.kind == RefKind::Axis
                                     ? std::min(dd, std::max({std::abs(chk.d[0] + r.d[0]), std::abs(chk.d[1] + r.d[1]), std::abs(chk.d[2] + r.d[2])}))
                                     : dd;
            // 位置只需落在同一條線／同一平面上；這裡保守地要求點的差異在容許內或幾何類型為 Plane／Axis（點可沿自身滑動）
            if (r.kind == RefKind::Point ? dp < tol : ddAbs < 1e-6) r.topoIndex = idx;
        }
    }

    const QString what = r.source == RefSource::Face ? QObject::tr("面")
                       : r.source == RefSource::Edge ? QObject::tr("邊") : QObject::tr("頂點");
    r.label = r.topoIndex > 0 ? QStringLiteral("%1 #%2 · %3").arg(what).arg(r.topoIndex).arg(r.label)
                              : QStringLiteral("%1 · %2").arg(what, r.label);
    out = r;
    return true;
}

} // namespace assemblyutil
} // namespace cad
} // namespace aicad
