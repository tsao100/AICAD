#include "ConstraintSolver.h"
#include "../Sketch.h"
#include <QtMath>
#include <QDebug>
#include <algorithm>

#include <Geom_Circle.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <Eigen/Dense>

namespace aicad::cad {

// ════════════════════════════════════════════════════════════════════════════
// GeomVarLayout
// ════════════════════════════════════════════════════════════════════════════

int GeomVarLayout::indexFor(GeomHandle h) const {
    // Line layout:    [x1, y1, x2, y2]
    // Circle layout:  [cx, cy, r]
    // Arc layout:     [cx, cy, r, startAngle, endAngle]
    // Ellipse layout: [cx, cy, majorR, minorR, angle]
    switch (h) {
    case GeomHandle::Start:        return offset + 0;  // x1 / cx
    case GeomHandle::End:          return offset + 2;  // x2（Line only）
    case GeomHandle::Center:       return offset + 0;  // cx
    case GeomHandle::RadiusValue:  return offset + 2;  // r
    case GeomHandle::ArcStartAngle:return offset + 3;
    case GeomHandle::ArcEndAngle:  return offset + 4;
    case GeomHandle::MajorRadius:  return offset + 2;
    case GeomHandle::MinorRadius:  return offset + 3;
    default:                       return offset;
    }
}

// ════════════════════════════════════════════════════════════════════════════
// ConstraintEquation 基礎
// ════════════════════════════════════════════════════════════════════════════

int ConstraintEquation::varIdx(int refIdx, GeomHandle handle) const {
    const auto& ref = m_constraint.refs[refIdx];
    auto it = m_layout->find(ref.geomUuid);
    if (it == m_layout->end()) return -1;
    return it->indexFor(handle != GeomHandle::WholeGeom ? handle : ref.handle);
}
int ConstraintEquation::varIdx(const GeomRef& ref) const {
    auto it = m_layout->find(ref.geomUuid);
    if (it == m_layout->end()) return -1;
    return it->indexFor(ref.handle);
}

// ════════════════════════════════════════════════════════════════════════════
// 各約束方程式實作
// ════════════════════════════════════════════════════════════════════════════

// ── Coincident：F = [x_a - x_b, y_a - y_b] ──────────────────────────────
void CoincidentEquation::evaluate(const QVector<double>& v, QVector<double>& out) const {
    auto& refs = constraint().refs;
    int ix_a = varIdx(refs[0]); int iy_a = ix_a + 1;
    int ix_b = varIdx(refs[1]); int iy_b = ix_b + 1;
    out[0] = v[ix_a] - v[ix_b];
    out[1] = v[iy_a] - v[iy_b];
}
void CoincidentEquation::jacobian(const QVector<double>&, int r0,
                                  QVector<QVector<double>>& J) const {
    auto& refs = constraint().refs;
    int ix_a = varIdx(refs[0]); int iy_a = ix_a + 1;
    int ix_b = varIdx(refs[1]); int iy_b = ix_b + 1;
    if (ix_a>=0) { J[r0][ix_a]=1; J[r0+1][iy_a]=1; }
    if (ix_b>=0) { J[r0][ix_b]=-1; J[r0+1][iy_b]=-1; }
}

// ── Horizontal：F = [y1 - y2] ─────────────────────────────────────────────
void HorizontalEquation::evaluate(const QVector<double>& v, QVector<double>& out) const {
    const auto& uuid = constraint().refs[0].geomUuid;
    auto it = layout().find(uuid);
    if (it == layout().end()) { out[0]=0; return; }
    // y1 at offset+1, y2 at offset+3
    out[0] = v[it->offset + 1] - v[it->offset + 3];
}
void HorizontalEquation::jacobian(const QVector<double>&, int r0,
                                  QVector<QVector<double>>& J) const {
    const auto& uuid = constraint().refs[0].geomUuid;
    auto it = layout().find(uuid);
    if (it == layout().end()) return;
    J[r0][it->offset + 1] =  1.0;
    J[r0][it->offset + 3] = -1.0;
}

// ── Vertical：F = [x1 - x2] ──────────────────────────────────────────────
void VerticalEquation::evaluate(const QVector<double>& v, QVector<double>& out) const {
    const auto& uuid = constraint().refs[0].geomUuid;
    auto it = layout().find(uuid);
    if (it == layout().end()) { out[0]=0; return; }
    out[0] = v[it->offset + 0] - v[it->offset + 2];
}
void VerticalEquation::jacobian(const QVector<double>&, int r0,
                                QVector<QVector<double>>& J) const {
    const auto& uuid = constraint().refs[0].geomUuid;
    auto it = layout().find(uuid);
    if (it == layout().end()) return;
    J[r0][it->offset + 0] =  1.0;
    J[r0][it->offset + 2] = -1.0;
}

// ── Parallel：F = [dx_a * dy_b - dy_a * dx_b] ───────────────────────────
void ParallelEquation::evaluate(const QVector<double>& v, QVector<double>& out) const {
    auto itA = layout().find(constraint().refs[0].geomUuid);
    auto itB = layout().find(constraint().refs[1].geomUuid);
    if (itA==layout().end()||itB==layout().end()){out[0]=0;return;}
    double dxA = v[itA->offset+2]-v[itA->offset+0];
    double dyA = v[itA->offset+3]-v[itA->offset+1];
    double dxB = v[itB->offset+2]-v[itB->offset+0];
    double dyB = v[itB->offset+3]-v[itB->offset+1];
    out[0] = dxA*dyB - dyA*dxB;   // 叉積 = 0
}
void ParallelEquation::jacobian(const QVector<double>& v, int r0,
                                QVector<QVector<double>>& J) const {
    auto itA = layout().find(constraint().refs[0].geomUuid);
    auto itB = layout().find(constraint().refs[1].geomUuid);
    if (itA==layout().end()||itB==layout().end()) return;
    double dxA=v[itA->offset+2]-v[itA->offset+0], dyA=v[itA->offset+3]-v[itA->offset+1];
    double dxB=v[itB->offset+2]-v[itB->offset+0], dyB=v[itB->offset+3]-v[itB->offset+1];
    // ∂/∂x1A = -dyB, ∂/∂y1A = dxB, ∂/∂x2A = dyB, ∂/∂y2A = -dxB
    J[r0][itA->offset+0] = -dyB;  J[r0][itA->offset+1] = dxB;
    J[r0][itA->offset+2] =  dyB;  J[r0][itA->offset+3] = -dxB;
    // ∂/∂x1B = dyA, ∂/∂y1B = -dxA, ...
    J[r0][itB->offset+0] =  dyA;  J[r0][itB->offset+1] = -dxA;
    J[r0][itB->offset+2] = -dyA;  J[r0][itB->offset+3] =  dxA;
}

// ── Perpendicular：F = [dxA*dxB + dyA*dyB] ──────────────────────────────
void PerpendicularEquation::evaluate(const QVector<double>& v, QVector<double>& out) const {
    auto itA = layout().find(constraint().refs[0].geomUuid);
    auto itB = layout().find(constraint().refs[1].geomUuid);
    if (itA==layout().end()||itB==layout().end()){out[0]=0;return;}
    double dxA=v[itA->offset+2]-v[itA->offset+0], dyA=v[itA->offset+3]-v[itA->offset+1];
    double dxB=v[itB->offset+2]-v[itB->offset+0], dyB=v[itB->offset+3]-v[itB->offset+1];
    out[0] = dxA*dxB + dyA*dyB;   // 點積 = 0
}
void PerpendicularEquation::jacobian(const QVector<double>& v, int r0,
                                     QVector<QVector<double>>& J) const {
    auto itA = layout().find(constraint().refs[0].geomUuid);
    auto itB = layout().find(constraint().refs[1].geomUuid);
    if (itA==layout().end()||itB==layout().end()) return;
    double dxA=v[itA->offset+2]-v[itA->offset+0], dyA=v[itA->offset+3]-v[itA->offset+1];
    double dxB=v[itB->offset+2]-v[itB->offset+0], dyB=v[itB->offset+3]-v[itB->offset+1];
    J[r0][itA->offset+0]=-dxB; J[r0][itA->offset+1]=-dyB;
    J[r0][itA->offset+2]= dxB; J[r0][itA->offset+3]= dyB;
    J[r0][itB->offset+0]=-dxA; J[r0][itB->offset+1]=-dyA;
    J[r0][itB->offset+2]= dxA; J[r0][itB->offset+3]= dyA;
}

// ── Tangent（線與圓）：dist(center, line) = r ────────────────────────────
// refs[0]=Curve(line), refs[1]=Center(circle)
void TangentEquation::evaluate(const QVector<double>& v, QVector<double>& out) const {
    auto itL = layout().find(constraint().refs[0].geomUuid);
    auto itC = layout().find(constraint().refs[1].geomUuid);
    if (itL==layout().end()||itC==layout().end()){out[0]=0;return;}
    double x1=v[itL->offset],y1=v[itL->offset+1],x2=v[itL->offset+2],y2=v[itL->offset+3];
    double cx=v[itC->offset],cy=v[itC->offset+1],r=v[itC->offset+2];
    // 點到直線距離公式
    double dx=x2-x1, dy=y2-y1, len=qSqrt(dx*dx+dy*dy);
    if (len < 1e-10){out[0]=0;return;}
    double dist = qAbs((cy-y1)*dx-(cx-x1)*dy) / len;
    out[0] = dist - r;
}
void TangentEquation::jacobian(const QVector<double>& v, int r0,
                               QVector<QVector<double>>& J) const {
    // 數值雅可比（避免複雜解析式；可後續替換）
    const double h = 1e-7;
    QVector<double> Fp(1), Fm(1), vp=v, vm=v;
    auto it = layout().begin();
    int N = v.size();
    for (int i = 0; i < N; ++i) {
        vp[i]=v[i]+h; vm[i]=v[i]-h;
        evaluate(vp, Fp); evaluate(vm, Fm);
        J[r0][i] = (Fp[0]-Fm[0])/(2*h);
        vp[i]=v[i]; vm[i]=v[i];
    }
}

// ── Concentric：F = [cx_a-cx_b, cy_a-cy_b] ──────────────────────────────
void ConcentricEquation::evaluate(const QVector<double>& v, QVector<double>& out) const {
    auto itA = layout().find(constraint().refs[0].geomUuid);
    auto itB = layout().find(constraint().refs[1].geomUuid);
    if (itA==layout().end()||itB==layout().end()){out[0]=out[1]=0;return;}
    out[0] = v[itA->offset] - v[itB->offset];
    out[1] = v[itA->offset+1] - v[itB->offset+1];
}
void ConcentricEquation::jacobian(const QVector<double>&, int r0,
                                  QVector<QVector<double>>& J) const {
    auto itA = layout().find(constraint().refs[0].geomUuid);
    auto itB = layout().find(constraint().refs[1].geomUuid);
    if (itA==layout().end()||itB==layout().end()) return;
    J[r0][itA->offset]=1;   J[r0+1][itA->offset+1]=1;
    J[r0][itB->offset]=-1;  J[r0+1][itB->offset+1]=-1;
}

// ── FixedDistance：F = [dist(a,b) - value] ──────────────────────────────
void FixedDistanceEquation::evaluate(const QVector<double>& v, QVector<double>& out) const {
    int ia = varIdx(constraint().refs[0]), ib = varIdx(constraint().refs[1]);
    if (ia<0||ib<0){out[0]=0;return;}
    double dx=v[ia]-v[ib], dy=v[ia+1]-v[ib+1];
    out[0] = qSqrt(dx*dx+dy*dy) - constraint().value;
}
void FixedDistanceEquation::jacobian(const QVector<double>& v, int r0,
                                     QVector<QVector<double>>& J) const {
    int ia = varIdx(constraint().refs[0]), ib = varIdx(constraint().refs[1]);
    if (ia<0||ib<0) return;
    double dx=v[ia]-v[ib], dy=v[ia+1]-v[ib+1];
    double dist=qSqrt(dx*dx+dy*dy);
    if (dist<1e-10) return;
    J[r0][ia]  =  dx/dist; J[r0][ia+1] =  dy/dist;
    J[r0][ib]  = -dx/dist; J[r0][ib+1] = -dy/dist;
}

// ── EqualLength：F = [len_a - len_b] ────────────────────────────────────
void EqualLengthEquation::evaluate(const QVector<double>& v, QVector<double>& out) const {
    auto itA = layout().find(constraint().refs[0].geomUuid);
    auto itB = layout().find(constraint().refs[1].geomUuid);
    if (itA==layout().end()||itB==layout().end()){out[0]=0;return;}
    auto len=[&](const GeomVarLayout& l){
        double dx=v[l.offset+2]-v[l.offset],dy=v[l.offset+3]-v[l.offset+1];
        return qSqrt(dx*dx+dy*dy);
    };
    out[0] = len(*itA) - len(*itB);
}
void EqualLengthEquation::jacobian(const QVector<double>& v, int r0,
                                   QVector<QVector<double>>& J) const {
    // 數值雅可比
    const double h=1e-7; QVector<double> Fp(1),Fm(1),vp=v,vm=v;
    for (int i=0;i<v.size();++i){
        vp[i]=v[i]+h;vm[i]=v[i]-h;
        evaluate(vp,Fp);evaluate(vm,Fm);
        J[r0][i]=(Fp[0]-Fm[0])/(2*h);
        vp[i]=v[i];vm[i]=v[i];
    }
}

// ── FixedRadius：F = [r - value] ─────────────────────────────────────────
void FixedRadiusEquation::evaluate(const QVector<double>& v, QVector<double>& out) const {
    auto it = layout().find(constraint().refs[0].geomUuid);
    if (it==layout().end()){out[0]=0;return;}
    out[0] = v[it->offset+2] - constraint().value;
}
void FixedRadiusEquation::jacobian(const QVector<double>&, int r0,
                                   QVector<QVector<double>>& J) const {
    auto it = layout().find(constraint().refs[0].geomUuid);
    if (it==layout().end()) return;
    J[r0][it->offset+2] = 1.0;
}

// ── PointOnCurve（點在線段上）：叉積 = 0 + 點在線段範圍內 ──────────────
void PointOnCurveEquation::evaluate(const QVector<double>& v, QVector<double>& out) const {
    int ip = varIdx(constraint().refs[0]);
    auto itL = layout().find(constraint().refs[1].geomUuid);
    if (ip<0||itL==layout().end()){out[0]=0;return;}
    double px=v[ip],py=v[ip+1];
    double x1=v[itL->offset],y1=v[itL->offset+1],x2=v[itL->offset+2],y2=v[itL->offset+3];
    // 叉積 (P-P1)×(P2-P1) = 0
    out[0] = (px-x1)*(y2-y1) - (py-y1)*(x2-x1);
}
void PointOnCurveEquation::jacobian(const QVector<double>& v, int r0,
                                    QVector<QVector<double>>& J) const {
    int ip = varIdx(constraint().refs[0]);
    auto itL = layout().find(constraint().refs[1].geomUuid);
    if (ip<0||itL==layout().end()) return;
    double x1=v[itL->offset],y1=v[itL->offset+1],x2=v[itL->offset+2],y2=v[itL->offset+3];
    double px=v[ip],py=v[ip+1];
    double dy=y2-y1, dx=x2-x1;
    J[r0][ip]   =  dy;  J[r0][ip+1] = -dx;
    J[r0][itL->offset]   = -(py-y1); J[r0][itL->offset+1] =  (px-x1);
    J[r0][itL->offset+2] =  (py-y1); J[r0][itL->offset+3] = -(px-x1);
}

// ── EqualRadius：F = [r_a - r_b] ─────────────────────────────────────────
void EqualRadiusEquation::evaluate(const QVector<double>& v, QVector<double>& out) const {
    auto itA = layout().find(constraint().refs[0].geomUuid);
    auto itB = layout().find(constraint().refs[1].geomUuid);
    if (itA==layout().end()||itB==layout().end()){out[0]=0;return;}
    out[0] = v[itA->offset+2] - v[itB->offset+2];
}
void EqualRadiusEquation::jacobian(const QVector<double>&, int r0,
                                   QVector<QVector<double>>& J) const {
    auto itA = layout().find(constraint().refs[0].geomUuid);
    auto itB = layout().find(constraint().refs[1].geomUuid);
    if (itA==layout().end()||itB==layout().end()) return;
    J[r0][itA->offset+2] =  1.0;
    J[r0][itB->offset+2] = -1.0;
}

// ── FixedX：F = [x - value] ──────────────────────────────────────────────
void FixedXEquation::evaluate(const QVector<double>& v, QVector<double>& out) const {
    int ix = varIdx(constraint().refs[0]);
    if (ix < 0){out[0]=0;return;}
    out[0] = v[ix] - constraint().value;
}
void FixedXEquation::jacobian(const QVector<double>&, int r0,
                              QVector<QVector<double>>& J) const {
    int ix = varIdx(constraint().refs[0]);
    if (ix >= 0) J[r0][ix] = 1.0;
}

// ── FixedY：F = [y - value] ──────────────────────────────────────────────
void FixedYEquation::evaluate(const QVector<double>& v, QVector<double>& out) const {
    int ix = varIdx(constraint().refs[0]);
    if (ix < 0){out[0]=0;return;}
    out[0] = v[ix+1] - constraint().value;   // +1 = y
}
void FixedYEquation::jacobian(const QVector<double>&, int r0,
                              QVector<QVector<double>>& J) const {
    int ix = varIdx(constraint().refs[0]);
    if (ix >= 0) J[r0][ix+1] = 1.0;
}

// ── Fixed：固定所有 DOF（將每個變數鎖在初始值）──────────────────────────
int FixedEquation::equationCount() const { return m_dof; }

void FixedEquation::evaluate(const QVector<double>& v, QVector<double>& out) const {
    for (int i = 0; i < m_dof; ++i)
        out[i] = v[m_offset + i] - m_snap[i];
}
void FixedEquation::jacobian(const QVector<double>&, int r0,
                             QVector<QVector<double>>& J) const {
    for (int i = 0; i < m_dof; ++i)
        J[r0+i][m_offset+i] = 1.0;
}

// ════════════════════════════════════════════════════════════════════════════
// ConstraintSolver
// ════════════════════════════════════════════════════════════════════════════
ConstraintSolver::ConstraintSolver()
    : m_config() {}

ConstraintSolver::ConstraintSolver(Config cfg) : m_config(cfg) {}

// ── 建立變數向量 ──────────────────────────────────────────────────────────
void ConstraintSolver::packVariables(const QList<SketchGeometry*>& geoms,
                                     QHash<QString, GeomVarLayout>& layout,
                                     QVector<double>& vars) const {
    vars.clear();
    layout.clear();
    for (const SketchGeometry* g : geoms) {
        GeomVarLayout vl;
        vl.offset = vars.size();
        switch (g->type) {
        case SketchGeometryType::Line: {
            const auto* l = static_cast<const SketchLine*>(g);
            vars << l->start.x() << l->start.y() << l->end.x() << l->end.y();
            vl.dof = 4;
            break;
        }
        case SketchGeometryType::Circle: {
            const auto* c = static_cast<const SketchCircle*>(g);
            vars << c->center.x() << c->center.y() << c->radius;
            vl.dof = 3;
            break;
        }
        case SketchGeometryType::Arc: {
            const auto* a = static_cast<const SketchArc*>(g);
            // 從 OCCT curve 取圓心、半徑、角度
            if (!a->curve.IsNull()) {
                auto baseCircle = Handle(Geom_Circle)::DownCast(a->curve->BasisCurve());
                if (!baseCircle.IsNull()) {
                    gp_Pnt c = baseCircle->Location();
                    double r = baseCircle->Radius();
                    double t0 = a->curve->FirstParameter();
                    double t1 = a->curve->LastParameter();
                    vars << c.X() << c.Y() << r << t0 << t1;
                    vl.dof = 5;
                    break;
                }
            }
            vars << 0.0 << 0.0 << 1.0 << 0.0 << M_PI;
            vl.dof = 5;
            break;
        }
        case SketchGeometryType::Ellipse: {
            const auto* e = static_cast<const SketchEllipse*>(g);
            vars << e->center.x() << e->center.y()
                 << e->majorRadius << e->minorRadius << e->angle;
            vl.dof = 5;
            break;
        }
        default:
            // Polyline / Spline：逐點打包
            for (const QVector2D& pt : g->points)
                vars << pt.x() << pt.y();
            vl.dof = g->points.size() * 2;
            break;
        }
        layout[g->uuid] = vl;
    }
}

// ── 回寫變數向量 ──────────────────────────────────────────────────────────
void ConstraintSolver::unpackVariables(const QVector<double>& vars,
                                       const QHash<QString, GeomVarLayout>& layout,
                                       QList<SketchGeometry*>& geoms) const {
    for (SketchGeometry* g : geoms) {
        auto it = layout.find(g->uuid);
        if (it == layout.end()) continue;
        int off = it->offset;
        switch (g->type) {
        case SketchGeometryType::Line: {
            auto* l = static_cast<SketchLine*>(g);
            l->start = QVector2D(vars[off+0], vars[off+1]);
            l->end   = QVector2D(vars[off+2], vars[off+3]);
            l->points[0] = l->start;
            l->points[1] = l->end;
            break;
        }
        case SketchGeometryType::Circle: {
            auto* c = static_cast<SketchCircle*>(g);
            c->center = QVector2D(vars[off+0], vars[off+1]);  // 直接賦值，不再 const_cast
            c->radius = vars[off+2];
            break;
        }
        case SketchGeometryType::Arc: {
            // Arc 由 OCCT curve 決定，這裡重建
            auto* a = static_cast<SketchArc*>(g);
            double cx=vars[off],cy=vars[off+1],r=vars[off+2];
            double t0=vars[off+3], t1=vars[off+4];
            gp_Ax2 ax2(gp_Pnt(cx,cy,0), gp_Dir(0,0,1));
            Handle(Geom_Circle) circ = new Geom_Circle(ax2, r);
            a->curve = new Geom_TrimmedCurve(circ, t0, t1);
            break;
        }
        case SketchGeometryType::Ellipse: {
            auto* e = static_cast<SketchEllipse*>(g);
            e->center = QVector2D(vars[off+0], vars[off+1]);
            e->majorRadius = vars[off+2];
            e->minorRadius = vars[off+3];
            e->angle       = vars[off+4];
            break;
        }
        default:
            for (int i=0; i<g->points.size(); ++i)
                g->points[i] = QVector2D(vars[off+i*2], vars[off+i*2+1]);
            break;
        }
    }
}

// ── 建立方程式物件 ─────────────────────────────────────────────────────────
QList<ConstraintEquation*> ConstraintSolver::buildEquations(
    const QList<SketchConstraint>& constraints,
    const QHash<QString, GeomVarLayout>& layout,
    const QVector<double>& vars) const
{
    QList<ConstraintEquation*> eqs;
    for (const SketchConstraint& c : constraints) {
        if (!c.driving) continue;
        ConstraintEquation* eq = nullptr;
        switch (c.type) {
        case ConstraintType::Coincident:    eq = new CoincidentEquation(c, &layout);    break;
        case ConstraintType::Horizontal:    eq = new HorizontalEquation(c, &layout);    break;
        case ConstraintType::Vertical:      eq = new VerticalEquation(c, &layout);      break;
        case ConstraintType::Parallel:      eq = new ParallelEquation(c, &layout);      break;
        case ConstraintType::Perpendicular: eq = new PerpendicularEquation(c, &layout); break;
        case ConstraintType::Tangent:       eq = new TangentEquation(c, &layout);       break;
        case ConstraintType::Concentric:    eq = new ConcentricEquation(c, &layout);    break;
        case ConstraintType::FixedDistance: eq = new FixedDistanceEquation(c, &layout); break;
        case ConstraintType::EqualLength:   eq = new EqualLengthEquation(c, &layout);   break;
        case ConstraintType::FixedRadius:   eq = new FixedRadiusEquation(c, &layout);   break;
        case ConstraintType::PointOnCurve:  eq = new PointOnCurveEquation(c, &layout);  break;
        case ConstraintType::EqualRadius:
            eq = new EqualRadiusEquation(c, &layout); break;
        case ConstraintType::FixedX:
            eq = new FixedXEquation(c, &layout); break;
        case ConstraintType::FixedY:
            eq = new FixedYEquation(c, &layout); break;
        case ConstraintType::Fixed: {
            // Fixed：從 layout 取出 offset+dof，記錄 snapshot
            auto it = layout.find(c.refs[0].geomUuid);
            if (it != layout.end()) {
                auto* feq = new FixedEquation(c, &layout);
                QVector<double> snap;
                for (int i = 0; i < it->dof; ++i) snap << vars[it->offset+i];
                feq->setSnapshot(snap, it->offset, it->dof);
                eqs.append(feq);
            }
            continue;   // 跳過下面的 if(eq) eqs.append(eq)
        }
        // FixedX / FixedY 轉換成 Coincident（固定虛擬點）
        // TODO: 其他類型
        default: break;
        }
        if (eq) eqs.append(eq);
    }
    return eqs;
}

// ── QR 最小二乘求解（Householder） ─────────────────────────────────────────
bool ConstraintSolver::solveLinearLS(const QVector<QVector<double>>& J,
                                     const QVector<double>& F,
                                     QVector<double>& dx)
{
    int m = J.size();
    if (m == 0) return false;
    int n = J[0].size();
    if (n == 0) return false;

    Eigen::MatrixXd A(m, n);
    Eigen::VectorXd b(m);
    for (int i = 0; i < m; ++i) {
        b(i) = -F[i];
        for (int j = 0; j < n; ++j)
            A(i, j) = J[i][j];
    }

    Eigen::VectorXd x =
        A.bdcSvd(Eigen::ComputeThinU | Eigen::ComputeThinV).solve(b);

    dx.resize(n);
    for (int j = 0; j < n; ++j) dx[j] = x(j);
    return true;
}

// ── Newton-Raphson 單步 ────────────────────────────────────────────────────
bool ConstraintSolver::newtonStep(QVector<double>& vars,
                                  const QList<ConstraintEquation*>& eqs,
                                  double& residualNorm) const
{
    int totalEq = 0;
    for (auto* e : eqs) totalEq += e->equationCount();
    int n = vars.size();

    // F 向量
    QVector<double> F(totalEq, 0.0);
    int row = 0;
    for (auto* e : eqs) {
        QVector<double> ef(e->equationCount(), 0.0);
        e->evaluate(vars, ef);
        for (int i=0; i<ef.size(); ++i) F[row+i] = ef[i];
        row += e->equationCount();
    }

    // 殘差
    residualNorm = 0;
    for (double f : F) residualNorm += f*f;
    residualNorm = qSqrt(residualNorm);

    if (residualNorm < m_config.tolerance) return true;   // 已收斂

    // 雅可比 J (totalEq × n)
    QVector<QVector<double>> J(totalEq, QVector<double>(n, 0.0));
    row = 0;
    for (auto* e : eqs) {
        e->jacobian(vars, row, J);
        row += e->equationCount();
    }

    // Δx = J⁺ · (-F)
    QVector<double> dx(n, 0.0);
    if (!solveLinearLS(J, F, dx)) return false;

    // vars ← vars + damping · Δx
    for (int i=0; i<n; ++i)
        vars[i] += m_config.damping * dx[i];

    return false;   // 繼續疊代
}

// ── 主求解入口 ─────────────────────────────────────────────────────────────
SolveResult ConstraintSolver::solve(QList<SketchGeometry*>& geoms,
                                    const QList<SketchConstraint>& constraints)
{
    SolveResult result;

    QHash<QString, GeomVarLayout> layout;
    QVector<double> vars;
    packVariables(geoms, layout, vars);

    auto eqs = buildEquations(constraints, layout, vars);

    int dof = computeDOF(geoms, constraints);
    result.dof = dof;

    if (eqs.isEmpty()) {
        result.status = (dof==0) ? SolveStatus::FullyConstrained
                                   : SolveStatus::UnderConstrained;
        qDeleteAll(eqs);
        return result;
    }

    double residual = 1e30;
    bool converged = false;
    for (int iter = 0; iter < m_config.maxIterations; ++iter) {
        converged = newtonStep(vars, eqs, residual);
        result.iterations = iter + 1;
        if (converged) break;
    }

    result.residual = residual;

    if (converged) {
        unpackVariables(vars, layout, geoms);
        result.status = (dof == 0) ? SolveStatus::FullyConstrained
                                   : SolveStatus::UnderConstrained;
    } else if (residual > 1.0) {
        result.status = SolveStatus::Conflict;
    } else {
        result.status = SolveStatus::UnderConstrained;
        unpackVariables(vars, layout, geoms);
    }

    qDeleteAll(eqs);
    return result;
}

int ConstraintSolver::computeDOF(const QList<SketchGeometry*>& geoms,
                                 const QList<SketchConstraint>& constraints)
{
    int totalDOF = 0;
    for (const SketchGeometry* g : geoms) {
        switch (g->type) {
        case SketchGeometryType::Line:    totalDOF += 4; break;
        case SketchGeometryType::Circle:  totalDOF += 3; break;
        case SketchGeometryType::Arc:     totalDOF += 5; break;
        case SketchGeometryType::Ellipse: totalDOF += 5; break;
        default: totalDOF += g->points.size() * 2; break;
        }
    }
    int consumed = 0;
    for (const SketchConstraint& c : constraints)
        if (c.driving) consumed += qMin(c.dofConsumed(), totalDOF - consumed);
    return qMax(totalDOF - consumed, 0);
}

} // namespace aicad::cad
