#include "SketchConstraint.h"
#include "../../core/ParameterStore.h"
#include "../Sketch.h"
#include <QJsonArray>
#include <cmath>

namespace aicad::cad {

// ──────────────────────────────────────────────────────────────────────────────
// GeomRef
// ──────────────────────────────────────────────────────────────────────────────
QJsonObject GeomRef::toJson() const {
    QJsonObject o;
    o["geomUuid"] = geomUuid;
    o["handle"]   = static_cast<int>(handle);
    return o;
}
GeomRef GeomRef::fromJson(const QJsonObject& j) {
    return GeomRef(j["geomUuid"].toString(),
                   static_cast<GeomHandle>(j["handle"].toInt()));
}

// ── Task C: GeomRef point resolution ─────────────────────────────────────────
QString GeomRef::resolvedPointUuid(const Sketch* sketch) const
{
    if (!sketch || geomUuid.isEmpty()) return {};
    auto* geom = sketch->findGeometry(geomUuid);
    if (!geom) return {};

    // 直接引用 SketchPoint
    if (geom->type == SketchGeometryType::Point)
        return geomUuid;

    // Line：Start / End
    if (auto* line = dynamic_cast<const SketchLine*>(geom)) {
        if (handle == GeomHandle::Start) return line->startUuid;
        if (handle == GeomHandle::End)   return line->endUuid;
    }

    // Arc：Start / End / Center
    if (auto* arc = dynamic_cast<const SketchArc*>(geom)) {
        if (handle == GeomHandle::Start)  return arc->startUuid;
        if (handle == GeomHandle::End)    return arc->endUuid;
        if (handle == GeomHandle::Center) return arc->centerUuid;
    }

    // Circle：Center or WholeGeom
    if (auto* circ = dynamic_cast<const SketchCircle*>(geom)) {
        if (handle == GeomHandle::Center ||
            handle == GeomHandle::WholeGeom) return circ->centerUuid;
    }

    return {};
}

bool GeomRef::isDirectPoint(const Sketch* sketch) const
{
    if (!sketch) return false;
    auto* g = sketch->findGeometry(geomUuid);
    return g && g->type == SketchGeometryType::Point;
}

QVector2D GeomRef::resolvePosition(const Sketch* sketch) const
{
    if (!sketch) return {};
    QString ptUuid = resolvedPointUuid(sketch);
    if (!ptUuid.isEmpty()) {
        if (auto* pt = sketch->point(ptUuid))
            return pt->pos;
    }
    // Fallback：直接從 geom->points 取得
    auto* geom = sketch->findGeometry(geomUuid);
    if (!geom) return {};
    if (!geom->points.isEmpty()) return geom->points[0];
    // SketchCircle / SketchEllipse 不填 points，改從具體欄位取中心
    if (auto* c = dynamic_cast<const SketchCircle*>(geom))
        return c->center;
    if (auto* e = dynamic_cast<const SketchEllipse*>(geom))
        return e->center;
    return {};
}

// ──────────────────────────────────────────────────────────────────────────────
// SketchConstraint 工廠
// ──────────────────────────────────────────────────────────────────────────────
SketchConstraint SketchConstraint::makeCoincident(const GeomRef& a, const GeomRef& b) {
    SketchConstraint c; c.type = ConstraintType::Coincident;
    c.refs = {a, b}; return c;
}
SketchConstraint SketchConstraint::makeHorizontal(const QString& lineUuid) {
    SketchConstraint c; c.type = ConstraintType::Horizontal;
    c.refs = { GeomRef(lineUuid, GeomHandle::Curve) }; return c;
}
SketchConstraint SketchConstraint::makeVertical(const QString& lineUuid) {
    SketchConstraint c; c.type = ConstraintType::Vertical;
    c.refs = { GeomRef(lineUuid, GeomHandle::Curve) }; return c;
}
SketchConstraint SketchConstraint::makeParallel(const QString& a, const QString& b) {
    SketchConstraint c; c.type = ConstraintType::Parallel;
    c.refs = { GeomRef(a, GeomHandle::Curve), GeomRef(b, GeomHandle::Curve) }; return c;
}
SketchConstraint SketchConstraint::makePerpendicular(const QString& a, const QString& b) {
    SketchConstraint c; c.type = ConstraintType::Perpendicular;
    c.refs = { GeomRef(a, GeomHandle::Curve), GeomRef(b, GeomHandle::Curve) }; return c;
}
SketchConstraint SketchConstraint::makeTangent(const QString& a, const QString& b) {
    SketchConstraint c; c.type = ConstraintType::Tangent;
    c.refs = { GeomRef(a, GeomHandle::Curve), GeomRef(b, GeomHandle::Curve) }; return c;
}
SketchConstraint SketchConstraint::makeEqualLength(const QString& a, const QString& b) {
    SketchConstraint c; c.type = ConstraintType::EqualLength;
    c.refs = { GeomRef(a, GeomHandle::Curve), GeomRef(b, GeomHandle::Curve) }; return c;
}
SketchConstraint SketchConstraint::makeEqualRadius(const QString& a, const QString& b) {
    SketchConstraint c; c.type = ConstraintType::EqualRadius;
    c.refs = { GeomRef(a, GeomHandle::Center), GeomRef(b, GeomHandle::Center) }; return c;
}
SketchConstraint SketchConstraint::makeConcentric(const QString& a, const QString& b) {
    SketchConstraint c; c.type = ConstraintType::Concentric;
    c.refs = { GeomRef(a, GeomHandle::Center), GeomRef(b, GeomHandle::Center) }; return c;
}
SketchConstraint SketchConstraint::makeFixed(const QString& uuid) {
    SketchConstraint c; c.type = ConstraintType::Fixed;
    c.refs = { GeomRef(uuid, GeomHandle::WholeGeom) }; return c;
}
SketchConstraint SketchConstraint::makeFixedDistance(const GeomRef& a, const GeomRef& b, double dist) {
    SketchConstraint c; c.type = ConstraintType::FixedDistance;
    c.refs = {a, b}; c.value = dist; return c;
}
SketchConstraint SketchConstraint::makeFixedRadius(const QString& uuid, double r) {
    SketchConstraint c; c.type = ConstraintType::FixedRadius;
    c.refs = { GeomRef(uuid, GeomHandle::RadiusValue) }; c.value = r; return c;
}
SketchConstraint SketchConstraint::makeFixedX(const GeomRef& pt, double x) {
    SketchConstraint c; c.type = ConstraintType::FixedX;
    c.refs = {pt}; c.value = x; return c;
}
SketchConstraint SketchConstraint::makeFixedY(const GeomRef& pt, double y) {
    SketchConstraint c; c.type = ConstraintType::FixedY;
    c.refs = {pt}; c.value = y; return c;
}
SketchConstraint SketchConstraint::makePointOnCurve(const GeomRef& pt, const QString& curveUuid) {
    SketchConstraint c; c.type = ConstraintType::PointOnCurve;
    c.refs = {pt, GeomRef(curveUuid, GeomHandle::Curve)}; return c;
}
SketchConstraint SketchConstraint::makeMidpoint(const GeomRef& pt, const QString& lineUuid) {
    SketchConstraint c; c.type = ConstraintType::Midpoint;
    c.refs = {pt, GeomRef(lineUuid, GeomHandle::Curve)}; return c;
}

bool SketchConstraint::isDimensional() const {
    switch (type) {
    case ConstraintType::FixedDistance:
    case ConstraintType::FixedRadius:
    case ConstraintType::FixedX:
    case ConstraintType::FixedY:
    case ConstraintType::FixedAngleDim:
    case ConstraintType::FixedAngle:
    case ConstraintType::FixedLength:
    case ConstraintType::FixedDiameter:
    case ConstraintType::FixedHorizDist:
    case ConstraintType::FixedVertDist:
    case ConstraintType::FixedArcLength:
    case ConstraintType::CoordinateDim:
    case ConstraintType::Slope:
    case ConstraintType::Chamfer:
        return true;
    default:
        return false;
    }
}

bool SketchConstraint::evaluateValue(const aicad::core::ParameterStore* store) {
    if (paramExpr.isEmpty()) return true;
    if (!store) return false;
    auto [ok, v] = store->evaluate(paramExpr);
    if (ok) {
        // ★ 修正（實測回報：角度約束的值在再次 Solve 後會改變，例如 136
        //   變成 7792.23——7792.23÷136 恰好等於 180/π，不是巧合）：
        //   GeneralDimCommand::commitDimension() 幫角度型別（FixedAngle／
        //   FixedAngleDim）自動命名參數時，刻意用「使用者看到的單位」
        //   （度）去註冊 ParameterStore 裡的定義（跟手動輸入 paramExpr 時
        //   的慣例一致，見該處說明）；但 value 這個欄位系統其他每個地方
        //   都假設是弧度（ConstraintCommands.cpp 的 applyDimensionEdit()／
        //   EditConCommand::execute() 兩處編輯路徑都正確做了
        //   *M_PI/180.0 轉換）。這個函式是 solveWithStore() 對「所有
        //   paramExpr 非空的尺寸約束」重新求值時唯一會呼叫到的地方——
        //   driving 的角度約束幾乎必定有自動命名的 paramExpr，於是每次
        //   Solve 都會把 store 算出來的「度」原封不動塞進「應該是弧度」
        //   的 value，把正確的弧度值直接覆寫成一個以度為單位、但被當成
        //   弧度使用的錯誤數字——下次顯示成度數時再乘一次 180/π，就是
        //   使用者看到的暴增值。
        const bool isAngleType = (type == ConstraintType::FixedAngleDim ||
                                   type == ConstraintType::FixedAngle);
        value = isAngleType ? (v * M_PI / 180.0) : v;
    }
    return ok;
}

int SketchConstraint::dofConsumed() const {
    switch (type) {
    case ConstraintType::Coincident:      return 2;
    case ConstraintType::Midpoint:        return 2;
    case ConstraintType::Symmetric:       return 2;
    case ConstraintType::PointOnCurve:    return 1;
    case ConstraintType::PointOnMidpoint: return 2;
    case ConstraintType::Horizontal:      return 1;
    case ConstraintType::Vertical:        return 1;
    case ConstraintType::Parallel:        return 1;
    case ConstraintType::Perpendicular:   return 1;
    case ConstraintType::Collinear:
        // 兩線共線＝2 個方程式（平行＋點在延伸線上）；一點＋一線共線只有
        // 「點在延伸線上」1 個方程式（見 Sketch::constrainCollinear／
        // CollinearEquation 的點+線分支）。constrainCollinear() 建立 refs
        // 時，點一律用 GeomHandle::WholeGeom、線用 Curve，故可藉此分辨，
        // 讓 DOF 統計（degreesOfFreedom()／解算結果訊息）保持正確。
        return (refs.size() >= 2 &&
                (refs[0].handle == GeomHandle::WholeGeom ||
                 refs[1].handle == GeomHandle::WholeGeom)) ? 1 : 2;
    case ConstraintType::EqualLength:     return 1;
    case ConstraintType::FixedAngle:      return 1;
    case ConstraintType::Concentric:      return 2;
    case ConstraintType::EqualRadius:     return 1;
    case ConstraintType::Tangent:         return 1;
    case ConstraintType::FixedDistance:   return 1;
    case ConstraintType::FixedRadius:     return 1;
    case ConstraintType::FixedX:          return 1;
    case ConstraintType::FixedY:          return 1;
    case ConstraintType::FixedAngleDim:   return 1;
    case ConstraintType::Fixed:           return 999; // all DOF
    case ConstraintType::FixedLength:     return 1;
    case ConstraintType::FixedDiameter:   return 1;
    case ConstraintType::FixedHorizDist:  return 1;
    case ConstraintType::FixedVertDist:   return 1;
    case ConstraintType::FixedArcLength:  return 1;
    case ConstraintType::CoordinateDim:   return 2;
    case ConstraintType::Slope:           return 1;  // 消耗線的方向 DOF（同 Horizontal/Vertical/FixedAngleDim）
    case ConstraintType::Chamfer:         return 2;  // 2 條方程式，見 ConstraintType::Chamfer 註解
    default: return 0;
    }
}

QJsonObject SketchConstraint::toJson() const {
    QJsonObject o;
    o["uuid"]      = uuid;
    o["type"]      = static_cast<int>(type);
    o["value"]     = value;
    o["value2"]    = value2;
    o["paramExpr"] = paramExpr;
    o["driving"]   = driving;
    o["distMode"]  = static_cast<int>(distMode);
    o["dimOffX"]   = dimLineOffsetX;
    o["dimOffY"]   = dimLineOffsetY;
    if (fixedAbsolute) o["fixedAbsolute"] = true;
    // 第 10 項回報後續需求：只有非隱含（implicitOf 為空，即非 GDIM 標註
    // 產生）的 FixedDistance 約束才需要在這裡存這三個欄位——隱含約束
    // 本來就不會被序列化進來（見上方 implicitOf 判斷），它的「哪一側」
    // 記憶存在對應的 SketchAnnotation 裡（見 SketchAnnotation::toJson()）。
    if (implicitOf.isEmpty() && type == ConstraintType::FixedDistance) {
        o["distSideSign"] = distSideSign;
        o["distSideDirX"] = distSideDirX;
        o["distSideDirY"] = distSideDirY;
    }
    if (!implicitOf.isEmpty()) o["implicitOf"] = implicitOf;
    QJsonArray arr;
    for (const auto& r : refs) arr.append(r.toJson());
    o["refs"] = arr;
    return o;
}

SketchConstraint SketchConstraint::fromJson(const QJsonObject& j) {
    SketchConstraint c;
    c.uuid      = j["uuid"].toString(QUuid::createUuid().toString(QUuid::WithoutBraces));
    c.type      = static_cast<ConstraintType>(j["type"].toInt());
    c.value     = j["value"].toDouble(0.0);
    c.value2    = j["value2"].toDouble(0.0);
    c.paramExpr = j["paramExpr"].toString();
    c.driving   = j["driving"].toBool(true);
    c.distMode  = static_cast<DistanceMode>(j["distMode"].toInt(0));
    c.dimLineOffsetX = j["dimOffX"].toDouble(0.0);
    c.dimLineOffsetY = j["dimOffY"].toDouble(0.0);
    c.fixedAbsolute  = j["fixedAbsolute"].toBool(false);
    c.distSideSign   = j["distSideSign"].toDouble(0.0);
    c.distSideDirX   = j["distSideDirX"].toDouble(0.0);
    c.distSideDirY   = j["distSideDirY"].toDouble(0.0);
    c.implicitOf     = j["implicitOf"].toString();
    for (const auto& rv : j["refs"].toArray())
        c.refs.append(GeomRef::fromJson(rv.toObject()));
    return c;
}


SketchConstraint SketchConstraint::makeSymmetric(const GeomRef& a, const GeomRef& b, const QString& axisUuid) {
    SketchConstraint c;
    c.type = ConstraintType::Symmetric;
    c.refs = { a, b, GeomRef(axisUuid, GeomHandle::Curve) };
    return c;
}

SketchConstraint SketchConstraint::makeCollinear(const QString& lineA, const QString& lineB) {
    SketchConstraint c;
    c.type = ConstraintType::Collinear;
    c.refs = { GeomRef(lineA, GeomHandle::Curve), GeomRef(lineB, GeomHandle::Curve) };
    return c;
}

// ── General Dimension 工廠方法 ───────────────────────────────────────────────

SketchConstraint SketchConstraint::makeFixedLength(const QString& lineUuid, double len) {
    SketchConstraint c; c.type = ConstraintType::FixedLength; c.value = len;
    c.refs = { GeomRef(lineUuid, GeomHandle::Curve) }; return c;
}

SketchConstraint SketchConstraint::makeFixedDiameter(const QString& geomUuid, double dia) {
    SketchConstraint c; c.type = ConstraintType::FixedDiameter; c.value = dia;
    c.refs = { GeomRef(geomUuid, GeomHandle::WholeGeom) }; return c;
}

SketchConstraint SketchConstraint::makeFixedHorizDist(const GeomRef& a, const GeomRef& b, double d) {
    SketchConstraint c; c.type = ConstraintType::FixedHorizDist; c.value = d;
    c.refs = {a, b}; return c;
}

SketchConstraint SketchConstraint::makeFixedVertDist(const GeomRef& a, const GeomRef& b, double d) {
    SketchConstraint c; c.type = ConstraintType::FixedVertDist; c.value = d;
    c.refs = {a, b}; return c;
}

SketchConstraint SketchConstraint::makeFixedArcLength(const QString& arcUuid, double len) {
    SketchConstraint c; c.type = ConstraintType::FixedArcLength; c.value = len;
    c.refs = { GeomRef(arcUuid, GeomHandle::WholeGeom) }; return c;
}

SketchConstraint SketchConstraint::makeCoordinateDim(const GeomRef& point, double x, double y) {
    SketchConstraint c; c.type = ConstraintType::CoordinateDim;
    c.value = x; c.value2 = y;
    c.refs = {point}; return c;
}

SketchConstraint SketchConstraint::makeSlope(const QString& lineUuid, double slope) {
    SketchConstraint c; c.type = ConstraintType::Slope; c.value = slope;
    c.refs = { GeomRef(lineUuid, GeomHandle::Curve) }; return c;
}

SketchConstraint SketchConstraint::makeChamfer(const GeomRef& line1Ref, const GeomRef& line2Ref,
                                               double d1, double d2) {
    // line1Ref/line2Ref 的 geomUuid 必須是「線」的 UUID、handle 必須是
    // GeomHandle::Start 或 End（選裁切端點是哪一端）——不能是裁切端點本身
    // 那個 SketchPoint 的 UUID，也不能是 WholeGeom／Curve。ChamferEquation
    // 要靠這個 handle 反查「同一條線上的另一個端點」，見 SketchConstraint.h
    // 這個工廠方法上方的完整說明（含一個真實踩過的 bug案例）。呼叫端見
    // TrimExtendHelper.cpp::chamferAt()。
    SketchConstraint c; c.type = ConstraintType::Chamfer;
    c.value = d1; c.value2 = d2;
    c.refs = { line1Ref, line2Ref };
    return c;
}

} // namespace aicad::cad