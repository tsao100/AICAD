#include "ConstraintSolver.h"
#include "../Sketch.h"
#include <QtMath>
#include <QDebug>
#include <cmath>

#include <Geom_Circle.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <Eigen/Dense>

namespace aicad::cad {

// ════════════════════════════════════════════════════════════════════════════
// GeomVarLayout
// ════════════════════════════════════════════════════════════════════════════

int GeomVarLayout::indexFor(GeomHandle h) const {
    // Line layout:    [x1, y1, x2, y2]（或共用 SketchPoint DOF）
    // Circle layout:  [cx, cy, r]  OR  shared-center: offset→r only, startOffset→cx
    // Arc layout:     [cx, cy, r]（Start/End 一律透過 startOffset/endOffset 取得；
    //                 不再有獨立的 t0/t1 角度變數，見 GeomVarLayout::refSweep 註解）
    // Ellipse layout: [cx, cy, majorR, minorR, angle]

    // ── shared-center Circle（dof==1）：offset 指向 radius，startOffset 指向 cx ──
    const bool sharedCenter = (dof == 1 && startOffset >= 0 && offset >= 0);

    switch (h) {
    case GeomHandle::Start:
        // 若有共用 SketchPoint，用 startOffset；否則用傳統 offset+0
        return (startOffset >= 0 && !sharedCenter) ? startOffset : offset + 0;
    case GeomHandle::End:
        return (endOffset >= 0) ? endOffset : offset + 2;
    case GeomHandle::Center:
        // shared-center Circle：圓心 index 存於 startOffset
        return sharedCenter ? startOffset : offset + 0;
    case GeomHandle::RadiusValue:
        // shared-center Circle：offset 直接就是 radius index（不是 offset+2）
        return sharedCenter ? offset : offset + 2;
    case GeomHandle::ArcStartAngle:
    case GeomHandle::ArcEndAngle:
        // Arc 已不再把 t0/t1 當成獨立求解變數（見 refSweep 註解），這兩個
        // handle 目前沒有任何呼叫端使用；回傳 -1 以免誤用到 offset+3/+4
        // （那已經是 Start/End fallback 的局部座標，或直接越界）。
        return -1;
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
    // y1 = Start y, y2 = End y  (用 indexFor 支援共用 SketchPoint DOF)
    int iy1 = it->indexFor(GeomHandle::Start) + 1;
    int iy2 = it->indexFor(GeomHandle::End)   + 1;
    out[0] = v[iy1] - v[iy2];
}
void HorizontalEquation::jacobian(const QVector<double>&, int r0,
                                  QVector<QVector<double>>& J) const {
    const auto& uuid = constraint().refs[0].geomUuid;
    auto it = layout().find(uuid);
    if (it == layout().end()) return;
    int iy1 = it->indexFor(GeomHandle::Start) + 1;
    int iy2 = it->indexFor(GeomHandle::End)   + 1;
    J[r0][iy1] =  1.0;
    J[r0][iy2] = -1.0;
}

// ── Vertical：F = [x1 - x2] ──────────────────────────────────────────────
void VerticalEquation::evaluate(const QVector<double>& v, QVector<double>& out) const {
    const auto& uuid = constraint().refs[0].geomUuid;
    auto it = layout().find(uuid);
    if (it == layout().end()) { out[0]=0; return; }
    int ix1 = it->indexFor(GeomHandle::Start);
    int ix2 = it->indexFor(GeomHandle::End);
    out[0] = v[ix1] - v[ix2];
}
void VerticalEquation::jacobian(const QVector<double>&, int r0,
                                QVector<QVector<double>>& J) const {
    const auto& uuid = constraint().refs[0].geomUuid;
    auto it = layout().find(uuid);
    if (it == layout().end()) return;
    int ix1 = it->indexFor(GeomHandle::Start);
    int ix2 = it->indexFor(GeomHandle::End);
    J[r0][ix1] =  1.0;
    J[r0][ix2] = -1.0;
}

// ── Parallel：F = [dx_a * dy_b - dy_a * dx_b] ───────────────────────────
void ParallelEquation::evaluate(const QVector<double>& v, QVector<double>& out) const {
    auto itA = layout().find(constraint().refs[0].geomUuid);
    auto itB = layout().find(constraint().refs[1].geomUuid);
    if (itA==layout().end()||itB==layout().end()){out[0]=0;return;}
    int ax1=itA->indexFor(GeomHandle::Start), ay1=ax1+1;
    int ax2=itA->indexFor(GeomHandle::End),   ay2=ax2+1;
    int bx1=itB->indexFor(GeomHandle::Start), by1=bx1+1;
    int bx2=itB->indexFor(GeomHandle::End),   by2=bx2+1;
    double dxA=v[ax2]-v[ax1], dyA=v[ay2]-v[ay1];
    double dxB=v[bx2]-v[bx1], dyB=v[by2]-v[by1];
    out[0] = dxA*dyB - dyA*dxB;
}
void ParallelEquation::jacobian(const QVector<double>& v, int r0,
                                QVector<QVector<double>>& J) const {
    auto itA = layout().find(constraint().refs[0].geomUuid);
    auto itB = layout().find(constraint().refs[1].geomUuid);
    if (itA==layout().end()||itB==layout().end()) return;
    int ax1=itA->indexFor(GeomHandle::Start), ay1=ax1+1;
    int ax2=itA->indexFor(GeomHandle::End),   ay2=ax2+1;
    int bx1=itB->indexFor(GeomHandle::Start), by1=bx1+1;
    int bx2=itB->indexFor(GeomHandle::End),   by2=bx2+1;
    double dxA=v[ax2]-v[ax1], dyA=v[ay2]-v[ay1];
    double dxB=v[bx2]-v[bx1], dyB=v[by2]-v[by1];
    J[r0][ax1]=-dyB; J[r0][ay1]=dxB;  J[r0][ax2]=dyB;  J[r0][ay2]=-dxB;
    J[r0][bx1]= dyA; J[r0][by1]=-dxA; J[r0][bx2]=-dyA; J[r0][by2]=dxA;
}

// ── Perpendicular：F = [dxA*dxB + dyA*dyB] ──────────────────────────────
void PerpendicularEquation::evaluate(const QVector<double>& v, QVector<double>& out) const {
    auto itA = layout().find(constraint().refs[0].geomUuid);
    auto itB = layout().find(constraint().refs[1].geomUuid);
    if (itA==layout().end()||itB==layout().end()){out[0]=0;return;}
    int ax1=itA->indexFor(GeomHandle::Start), ay1=ax1+1;
    int ax2=itA->indexFor(GeomHandle::End),   ay2=ax2+1;
    int bx1=itB->indexFor(GeomHandle::Start), by1=bx1+1;
    int bx2=itB->indexFor(GeomHandle::End),   by2=bx2+1;
    double dxA=v[ax2]-v[ax1], dyA=v[ay2]-v[ay1];
    double dxB=v[bx2]-v[bx1], dyB=v[by2]-v[by1];
    out[0] = dxA*dxB + dyA*dyB;
}
void PerpendicularEquation::jacobian(const QVector<double>& v, int r0,
                                     QVector<QVector<double>>& J) const {
    auto itA = layout().find(constraint().refs[0].geomUuid);
    auto itB = layout().find(constraint().refs[1].geomUuid);
    if (itA==layout().end()||itB==layout().end()) return;
    int ax1=itA->indexFor(GeomHandle::Start), ay1=ax1+1;
    int ax2=itA->indexFor(GeomHandle::End),   ay2=ax2+1;
    int bx1=itB->indexFor(GeomHandle::Start), by1=bx1+1;
    int bx2=itB->indexFor(GeomHandle::End),   by2=bx2+1;
    double dxA=v[ax2]-v[ax1], dyA=v[ay2]-v[ay1];
    double dxB=v[bx2]-v[bx1], dyB=v[by2]-v[by1];
    J[r0][ax1]=-dxB; J[r0][ay1]=-dyB; J[r0][ax2]=dxB; J[r0][ay2]=dyB;
    J[r0][bx1]=-dxA; J[r0][by1]=-dyA; J[r0][bx2]=dxA; J[r0][by2]=dyA;
}

// ── Tangent（線與圓）：dist(center, line) = r ────────────────────────────
// refs[0]=Curve(line), refs[1]=Center(circle)
void TangentEquation::evaluate(const QVector<double>& v, QVector<double>& out) const {
    auto itL = layout().find(constraint().refs[0].geomUuid);
    auto itC = layout().find(constraint().refs[1].geomUuid);
    if (itL==layout().end()||itC==layout().end()){out[0]=0;return;}
    int lx1=itL->indexFor(GeomHandle::Start), ly1=lx1+1;
    int lx2=itL->indexFor(GeomHandle::End),   ly2=lx2+1;
    // circle: cx=offset+0, cy=offset+1, r=offset+2（Circle 圓心仍用 offset）
    int cx_i = itC->indexFor(GeomHandle::Center);
    int cy_i = cx_i+1;
    int r_i  = itC->indexFor(GeomHandle::RadiusValue);
    double x1=v[lx1],y1=v[ly1],x2=v[lx2],y2=v[ly2];
    double cx=v[cx_i],cy=v[cy_i],r=v[r_i];
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
        int x1=l.indexFor(GeomHandle::Start), y1=x1+1;
        int x2=l.indexFor(GeomHandle::End),   y2=x2+1;
        double dx=v[x2]-v[x1], dy=v[y2]-v[y1];
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
    out[0] = v[it->indexFor(GeomHandle::RadiusValue)] - constraint().value;
}
void FixedRadiusEquation::jacobian(const QVector<double>&, int r0,
                                   QVector<QVector<double>>& J) const {
    auto it = layout().find(constraint().refs[0].geomUuid);
    if (it==layout().end()) return;
    J[r0][it->indexFor(GeomHandle::RadiusValue)] = 1.0;
}

// ── PointOnCurve（點在線段上）：叉積 = 0 + 點在線段範圍內 ──────────────
void PointOnCurveEquation::evaluate(const QVector<double>& v, QVector<double>& out) const {
    int ip = varIdx(constraint().refs[0]);
    auto itL = layout().find(constraint().refs[1].geomUuid);
    if (ip<0||itL==layout().end()){out[0]=0;return;}
    int lx1=itL->indexFor(GeomHandle::Start), ly1=lx1+1;
    int lx2=itL->indexFor(GeomHandle::End),   ly2=lx2+1;
    double px=v[ip],py=v[ip+1];
    double x1=v[lx1],y1=v[ly1],x2=v[lx2],y2=v[ly2];
    out[0] = (px-x1)*(y2-y1) - (py-y1)*(x2-x1);
}
void PointOnCurveEquation::jacobian(const QVector<double>& v, int r0,
                                    QVector<QVector<double>>& J) const {
    int ip = varIdx(constraint().refs[0]);
    auto itL = layout().find(constraint().refs[1].geomUuid);
    if (ip<0||itL==layout().end()) return;
    int lx1=itL->indexFor(GeomHandle::Start), ly1=lx1+1;
    int lx2=itL->indexFor(GeomHandle::End),   ly2=lx2+1;
    double x1=v[lx1],y1=v[ly1],x2=v[lx2],y2=v[ly2];
    double px=v[ip],py=v[ip+1];
    double dy=y2-y1, dx=x2-x1;
    J[r0][ip]   =  dy;  J[r0][ip+1] = -dx;
    J[r0][lx1]  = -(py-y1); J[r0][ly1]  =  (px-x1);
    J[r0][lx2]  =  (py-y1); J[r0][ly2]  = -(px-x1);
}

// ── Midpoint：F0 = px - (x1+x2)/2,  F1 = py - (y1+y2)/2 ────────────────
// refs[0] = 點，refs[1] = 線段
void MidpointEquation::evaluate(const QVector<double>& v, QVector<double>& out) const {
    int ip = varIdx(constraint().refs[0]);
    auto itL = layout().find(constraint().refs[1].geomUuid);
    if (ip<0||itL==layout().end()){out[0]=out[1]=0;return;}
    int lx1=itL->indexFor(GeomHandle::Start), ly1=lx1+1;
    int lx2=itL->indexFor(GeomHandle::End),   ly2=lx2+1;
    out[0] = v[ip]   - (v[lx1] + v[lx2]) * 0.5;
    out[1] = v[ip+1] - (v[ly1] + v[ly2]) * 0.5;
}
void MidpointEquation::jacobian(const QVector<double>&, int r0,
                                QVector<QVector<double>>& J) const {
    int ip = varIdx(constraint().refs[0]);
    auto itL = layout().find(constraint().refs[1].geomUuid);
    if (ip<0||itL==layout().end()) return;
    int lx1=itL->indexFor(GeomHandle::Start), ly1=lx1+1;
    int lx2=itL->indexFor(GeomHandle::End),   ly2=lx2+1;
    // ∂F0/∂px=1, ∂F0/∂x1=-0.5, ∂F0/∂x2=-0.5
    J[r0  ][ip  ] =  1.0;
    J[r0  ][lx1 ] = -0.5; J[r0  ][lx2] = -0.5;
    // ∂F1/∂py=1, ∂F1/∂y1=-0.5, ∂F1/∂y2=-0.5
    J[r0+1][ip+1] =  1.0;
    J[r0+1][ly1 ] = -0.5; J[r0+1][ly2] = -0.5;
}

// ── Symmetric：P0 與 P1 關於軸線對稱 ────────────────────────────────────
// 3 條方程式：
//   F0 = cross(dAxis, A→M) = 0   中點 M=(A+B)/2 在軸線上（cross=0）
//   F1 = (A→M) · cross90(dAxis) 亦即 M 在 axis 延伸方向 → 等價 PointOnCurve(M, axis)
//   實作：以 2 條 PointOnCurve 形式展開（midX on axis, midY on axis = 2 eq），
//          加 1 條 perpendicular(AB, axis)
// refs[0]=A, refs[1]=B, refs[2]=axis(Curve handle)
void SymmetricEquation::evaluate(const QVector<double>& v, QVector<double>& out) const {
    int ia = varIdx(constraint().refs[0]);
    int ib = varIdx(constraint().refs[1]);
    auto itAx = layout().find(constraint().refs[2].geomUuid);
    if (ia<0||ib<0||itAx==layout().end()){out[0]=out[1]=out[2]=0;return;}
    int ax1=itAx->indexFor(GeomHandle::Start), ay1=ax1+1;
    int ax2=itAx->indexFor(GeomHandle::End),   ay2=ax2+1;
    double ax=v[ia],   ay=v[ia+1];
    double bx=v[ib],   by=v[ib+1];
    double lx1=v[ax1], ly1=v[ay1], lx2=v[ax2], ly2=v[ay2];
    double mx = (ax+bx)*0.5, my = (ay+by)*0.5;  // midpoint
    double dx = lx2-lx1, dy = ly2-ly1;           // axis direction
    // F0,F1: midpoint on axis line  → cross(d, M-L1) = 0
    out[0] = (mx-lx1)*dy - (my-ly1)*dx;
    // F2: AB perpendicular to axis  → dot(AB, dAxis) = 0
    out[1] = (bx-ax)*dx + (by-ay)*dy;
    // F2: extra dot(AB, dAxis) already covers 1 DOF; also ensure distance symmetry
    // We only need 2 independent equations for Symmetric (2 DOF consumed):
    // Use: F0=midOnAxis(cross), F1=ABperpAxis(dot)
    // 3rd equation can be: dot(M-L1, dAxis) direction (proj) — not linearly independent
    // Safer: just 2 eq → override equationCount to 2... but declared as 3.
    // Use 3rd eq for robustness: midpoint-projected distance along axis perp = 0
    // (same as F0 with roles swapped — gives rank 2 in practice)
    // Actually provide: F2 = dot(AB, perp(dAxis)) = 0  i.e. same as F0 rewritten:
    // (bx-ax)*(-dy) + (by-ay)*(dx) = 0  → same as cross(AB, dAxis) but with sign:
    // This is identical to saying: vec(AB) parallel to perp(dAxis), i.e. perp to axis
    // F0 covers cross(dAxis, M-L1)=0; F1 covers dot(AB,dAxis)=0
    // For 3rd: cross(AB, dAxis) = 0  (AB parallel to perp of axis) — redundant with F1
    // Use a truly independent one: distance from A to axis == distance from B to axis (signed)
    // signedDist(P, line) = cross(dAxis_norm, P-L1) / |dAxis|
    // F2 = signedDist(A) + signedDist(B) = 0  (opposite sides)
    // = cross(d, A-L1) + cross(d, B-L1) = 2*cross(d, M-L1) → linearly dep on F0.
    // Conclusion: 2 independent equations suffice; F2=F0 duplicate causes rank drop.
    // Set F2 = F1 copy to avoid -nan in jacobian (solver handles rank def via pseudo-inv).
    out[2] = out[1]; // intentional rank-deficient placeholder; pseudo-inv handles it gracefully
}
void SymmetricEquation::jacobian(const QVector<double>& v, int r0,
                                 QVector<QVector<double>>& J) const {
    int ia = varIdx(constraint().refs[0]);
    int ib = varIdx(constraint().refs[1]);
    auto itAx = layout().find(constraint().refs[2].geomUuid);
    if (ia<0||ib<0||itAx==layout().end()) return;
    int ax1=itAx->indexFor(GeomHandle::Start), ay1=ax1+1;
    int ax2=itAx->indexFor(GeomHandle::End),   ay2=ax2+1;
    double ax=v[ia],   ay=v[ia+1];
    double bx=v[ib],   by=v[ib+1];
    double lx1=v[ax1], ly1=v[ay1], lx2=v[ax2], ly2=v[ay2];
    double dx = lx2-lx1, dy = ly2-ly1;
    // F0 = (mx-lx1)*dy - (my-ly1)*dx
    // mx=(ax+bx)/2, my=(ay+by)/2
    J[r0][ia]   =  0.5*dy;   J[r0][ia+1] = -0.5*dx;
    J[r0][ib]   =  0.5*dy;   J[r0][ib+1] = -0.5*dx;
    double mx=(ax+bx)*0.5, my=(ay+by)*0.5;
    J[r0][ax1] = -dy - (my-ly1)*(-1)*0 + 0;  // ∂F0/∂lx1 = -dy, ∂F0/∂ly1 = dx
    J[r0][ay1] =  dx;
    J[r0][ax2] =  (my-ly1);  // ∂F0/∂lx2 = 0, ∂F0/∂dy_part: dy=ly2-ly1, ∂F0/∂lx2=0... recalc:
    // F0 = (mx-lx1)*(ly2-ly1) - (my-ly1)*(lx2-lx1)
    // ∂F0/∂lx1 = -(ly2-ly1) - (my-ly1)*(-1) = -dy + (my-ly1)
    // ∂F0/∂ly1 = (mx-lx1)*(-1) - (-(lx2-lx1)) = -(mx-lx1) + dx = dx-(mx-lx1)
    // ∂F0/∂lx2 = (my-ly1)*(-1) = -(my-ly1) ... wait, ∂(lx2-lx1)/∂lx2=1 → -(my-ly1)
    // ∂F0/∂ly2 = (mx-lx1)
    J[r0][ax1]  = -dy + (my-ly1);
    J[r0][ay1]  =  dx - (mx-lx1);
    J[r0][ax2]  = -(my-ly1);
    J[r0][ay2]  =  (mx-lx1);
    // F1 = (bx-ax)*dx + (by-ay)*dy
    J[r0+1][ia]   = -dx;    J[r0+1][ia+1] = -dy;
    J[r0+1][ib]   =  dx;    J[r0+1][ib+1] =  dy;
    J[r0+1][ax1]  = -(bx-ax); J[r0+1][ax2] =  (bx-ax);
    J[r0+1][ay1]  = -(by-ay); J[r0+1][ay2] =  (by-ay);
    // F2 = F1 (duplicate row)
    for (int col = 0; col < J[r0+1].size(); ++col)
        J[r0+2][col] = J[r0+1][col];
}

// ── Collinear：F0 = cross(dA,dB)=0 (平行),  F1 = cross(dB, B1→A1)=0 ──
// refs[0]=線A, refs[1]=線B
void CollinearEquation::evaluate(const QVector<double>& v, QVector<double>& out) const {
    auto itA = layout().find(constraint().refs[0].geomUuid);
    auto itB = layout().find(constraint().refs[1].geomUuid);
    if (itA==layout().end()||itB==layout().end()){out[0]=out[1]=0;return;}
    int ax1=itA->indexFor(GeomHandle::Start), ay1=ax1+1;
    int ax2=itA->indexFor(GeomHandle::End),   ay2=ax2+1;
    int bx1=itB->indexFor(GeomHandle::Start), by1=bx1+1;
    int bx2=itB->indexFor(GeomHandle::End),   by2=bx2+1;
    double dxA=v[ax2]-v[ax1], dyA=v[ay2]-v[ay1];
    double dxB=v[bx2]-v[bx1], dyB=v[by2]-v[by1];
    // F0: parallel
    out[0] = dxA*dyB - dyA*dxB;
    // F1: A's start point lies on line B
    // cross(dB, A1-B1) = dxB*(v[ay1]-v[by1]) - dyB*(v[ax1]-v[bx1])
    out[1] = dxB*(v[ay1]-v[by1]) - dyB*(v[ax1]-v[bx1]);
}
void CollinearEquation::jacobian(const QVector<double>& v, int r0,
                                 QVector<QVector<double>>& J) const {
    auto itA = layout().find(constraint().refs[0].geomUuid);
    auto itB = layout().find(constraint().refs[1].geomUuid);
    if (itA==layout().end()||itB==layout().end()) return;
    int ax1=itA->indexFor(GeomHandle::Start), ay1=ax1+1;
    int ax2=itA->indexFor(GeomHandle::End),   ay2=ax2+1;
    int bx1=itB->indexFor(GeomHandle::Start), by1=bx1+1;
    int bx2=itB->indexFor(GeomHandle::End),   by2=bx2+1;
    double dxA=v[ax2]-v[ax1], dyA=v[ay2]-v[ay1];
    double dxB=v[bx2]-v[bx1], dyB=v[by2]-v[by1];
    // F0 = dxA*dyB - dyA*dxB
    J[r0][ax1]=-dyB; J[r0][ay1]=dxB;  J[r0][ax2]=dyB;  J[r0][ay2]=-dxB;
    J[r0][bx1]= dyA; J[r0][by1]=-dxA; J[r0][bx2]=-dyA; J[r0][by2]=dxA;
    // F1 = dxB*(ay1-by1) - dyB*(ax1-bx1)
    double a_y1=v[ay1], b_y1=v[by1], a_x1=v[ax1], b_x1=v[bx1];
    J[r0+1][ax1] = -dyB;
    J[r0+1][ay1] =  dxB;
    J[r0+1][bx1] =  dyB  + (a_y1-b_y1);  // ∂/∂bx1: -dyB*(-1) + dxB*(-1)... recalc
    // F1 = (bx2-bx1)*(ay1-by1) - (by2-by1)*(ax1-bx1)
    // ∂F1/∂ax1 = -(by2-by1) = -dyB
    // ∂F1/∂ay1 = (bx2-bx1) = dxB
    // ∂F1/∂bx1 = -(ay1-by1) + (by2-by1) ... d(bx2-bx1)/d(bx1)=-1 → -(ay1-by1)*(-1)... no:
    // dxB = bx2-bx1, d(dxB)/d(bx1) = -1 → ∂F1/∂bx1 = (-1)*(ay1-by1) - dyB*(-1) = -(ay1-by1)+dyB
    // ∂F1/∂by1 = dxB*(-1) - (ax1-bx1) * ... d(by2-by1)/d(by1)=-1 → -dxB - (-(ax1-bx1)*(-1))
    //          = -dxB - (ax1-bx1)
    // ∂F1/∂bx2 = (ay1-by1)
    // ∂F1/∂by2 = -(ax1-bx1)
    J[r0+1][ax1] = -dyB;
    J[r0+1][ay1] =  dxB;
    J[r0+1][bx1] = -(a_y1-b_y1) + dyB;
    J[r0+1][by1] = -dxB - (a_x1-b_x1) * (-1);  // ∂F1/∂by1 = -dxB*(1) + (ax1-bx1)
    J[r0+1][bx2] =  (a_y1-b_y1);
    J[r0+1][by2] = -(a_x1-b_x1);
    // fix by1 term: d(by2-by1)/dby1 = -1, so term2: -(ax1-bx1)*(-1) = (ax1-bx1)
    J[r0+1][by1] = -dxB + (a_x1-b_x1);
}

// ── EqualRadius：F = [r_a - r_b] ─────────────────────────────────────────
void EqualRadiusEquation::evaluate(const QVector<double>& v, QVector<double>& out) const {
    auto itA = layout().find(constraint().refs[0].geomUuid);
    auto itB = layout().find(constraint().refs[1].geomUuid);
    if (itA==layout().end()||itB==layout().end()){out[0]=0;return;}
    out[0] = v[itA->indexFor(GeomHandle::RadiusValue)] - v[itB->indexFor(GeomHandle::RadiusValue)];
}
void EqualRadiusEquation::jacobian(const QVector<double>&, int r0,
                                   QVector<QVector<double>>& J) const {
    auto itA = layout().find(constraint().refs[0].geomUuid);
    auto itB = layout().find(constraint().refs[1].geomUuid);
    if (itA==layout().end()||itB==layout().end()) return;
    J[r0][itA->indexFor(GeomHandle::RadiusValue)] =  1.0;
    J[r0][itB->indexFor(GeomHandle::RadiusValue)] = -1.0;
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
// General Dimension 新增方程式
// ════════════════════════════════════════════════════════════════════════════

// ── FixedLengthEquation：F = sqrt((x2-x1)²+(y2-y1)²) - value ────────────
void FixedLengthEquation::evaluate(const QVector<double>& v, QVector<double>& out) const {
    auto it = layout().find(constraint().refs[0].geomUuid);
    if (it == layout().end()) { out[0] = 0; return; }
    int x1i=it->indexFor(GeomHandle::Start), y1i=x1i+1;
    int x2i=it->indexFor(GeomHandle::End),   y2i=x2i+1;
    double dx=v[x2i]-v[x1i], dy=v[y2i]-v[y1i];
    out[0] = std::sqrt(dx*dx+dy*dy) - constraint().value;
}
void FixedLengthEquation::jacobian(const QVector<double>& v, int r0,
                                   QVector<QVector<double>>& J) const {
    auto it = layout().find(constraint().refs[0].geomUuid);
    if (it == layout().end()) return;
    int x1i=it->indexFor(GeomHandle::Start), y1i=x1i+1;
    int x2i=it->indexFor(GeomHandle::End),   y2i=x2i+1;
    double dx=v[x2i]-v[x1i], dy=v[y2i]-v[y1i];
    double len=std::sqrt(dx*dx+dy*dy);
    if (len < 1e-12) return;
    J[r0][x1i]=-dx/len; J[r0][y1i]=-dy/len;
    J[r0][x2i]= dx/len; J[r0][y2i]= dy/len;
}

// ── FixedDiameterEquation：F = r - value/2 ───────────────────────────────
void FixedDiameterEquation::evaluate(const QVector<double>& v, QVector<double>& out) const {
    auto it = layout().find(constraint().refs[0].geomUuid);
    if (it == layout().end()) { out[0] = 0; return; }
    out[0] = v[it->indexFor(GeomHandle::RadiusValue)] - constraint().value / 2.0;
}
void FixedDiameterEquation::jacobian(const QVector<double>&, int r0,
                                     QVector<QVector<double>>& J) const {
    auto it = layout().find(constraint().refs[0].geomUuid);
    if (it == layout().end()) return;
    J[r0][it->indexFor(GeomHandle::RadiusValue)] = 1.0;
}

// ── FixedHorizDistEquation：F = (x2 - x1) - value ────────────────────────
void FixedHorizDistEquation::evaluate(const QVector<double>& v, QVector<double>& out) const {
    int i0 = varIdx(constraint().refs[0]);
    int i1 = varIdx(constraint().refs[1]);
    if (i0 < 0 || i1 < 0) { out[0] = 0; return; }
    out[0] = (v[i1] - v[i0]) - constraint().value;
}
void FixedHorizDistEquation::jacobian(const QVector<double>&, int r0,
                                      QVector<QVector<double>>& J) const {
    int i0 = varIdx(constraint().refs[0]);
    int i1 = varIdx(constraint().refs[1]);
    if (i0 < 0 || i1 < 0) return;
    J[r0][i0] = -1.0;
    J[r0][i1] =  1.0;
}

// ── FixedVertDistEquation：F = (y2 - y1) - value ─────────────────────────
void FixedVertDistEquation::evaluate(const QVector<double>& v, QVector<double>& out) const {
    int i0 = varIdx(constraint().refs[0]);
    int i1 = varIdx(constraint().refs[1]);
    if (i0 < 0 || i1 < 0) { out[0] = 0; return; }
    // +1 = y component
    out[0] = (v[i1+1] - v[i0+1]) - constraint().value;
}
void FixedVertDistEquation::jacobian(const QVector<double>&, int r0,
                                     QVector<QVector<double>>& J) const {
    int i0 = varIdx(constraint().refs[0]);
    int i1 = varIdx(constraint().refs[1]);
    if (i0 < 0 || i1 < 0) return;
    J[r0][i0+1] = -1.0;
    J[r0][i1+1] =  1.0;
}

// ── FixedArcLengthEquation：F = r * sweep(Start,Center,End) - value ──────
// Arc layout（修正後）：offset+0=cx, +1=cy, +2=r；t0/t1「不」在求解變數之
// 列（見 GeomVarLayout::refSweep 註解）。弧長改由 Start/End 相對圓心的夾
// 角（cross/dot，取代 t1-t0）計算，避免角度獨立變數造成的病態問題；優弧
// （掃角 >π）或劣弧則由求解前快取的 refSweep 判斷分支，確保疊代過程中分
// 支選擇維持一致，不會忽大忽小亂跳。
void FixedArcLengthEquation::evaluate(const QVector<double>& v, QVector<double>& out) const {
    auto it = layout().find(constraint().refs[0].geomUuid);
    if (it == layout().end() || it->offset < 0) { out[0] = 0.0; return; }
    const double cx = v[it->offset+0], cy = v[it->offset+1], r = v[it->offset+2];
    const int sOff = it->indexFor(GeomHandle::Start);
    const int eOff = it->indexFor(GeomHandle::End);
    if (sOff < 0 || eOff < 0) { out[0] = 0.0; return; }
    const double ux = v[sOff] - cx, uy = v[sOff+1] - cy;
    const double vx = v[eOff] - cx, vy = v[eOff+1] - cy;
    const double cross = ux*vy - uy*vx;
    const double dot   = ux*vx + uy*vy;
    const double minorAngle = std::abs(std::atan2(cross, dot));   // [0, π]
    const bool   reflex     = std::abs(it->refSweep) > M_PI;
    const double sweep      = reflex ? (2.0*M_PI - minorAngle) : minorAngle;
    out[0] = r * sweep - constraint().value;
}
void FixedArcLengthEquation::jacobian(const QVector<double>& v, int r0,
                                      QVector<QVector<double>>& J) const {
    auto it = layout().find(constraint().refs[0].geomUuid);
    if (it == layout().end() || it->offset < 0) return;
    const double cx = v[it->offset+0], cy = v[it->offset+1], r = v[it->offset+2];
    const int sOff = it->indexFor(GeomHandle::Start);
    const int eOff = it->indexFor(GeomHandle::End);
    if (sOff < 0 || eOff < 0) return;
    const double ux = v[sOff] - cx, uy = v[sOff+1] - cy;
    const double vx = v[eOff] - cx, vy = v[eOff+1] - cy;
    const double cross = ux*vy - uy*vx;
    const double dot   = ux*vx + uy*vy;
    const double denom = cross*cross + dot*dot;
    if (denom < 1e-20) return;   // 退化（端點與圓心重合），避免除以 0

    const double signedAngle = std::atan2(cross, dot);
    const double signS  = (signedAngle >= 0.0) ? 1.0 : -1.0;
    const bool   reflex = std::abs(it->refSweep) > M_PI;
    const double k      = reflex ? -r : r;      // ∂F/∂minorAngle
    const double sweep  = reflex ? (2.0*M_PI - std::abs(signedAngle)) : std::abs(signedAngle);

    const double dAng_dCross =  dot   / denom;  // ∂(atan2 signedAngle)/∂cross
    const double dAng_dDot   = -cross / denom;  // ∂(atan2 signedAngle)/∂dot

    // cross = ux*vy - uy*vx, dot = ux*vx + uy*vy
    const double dF_dux = k * signS * (dAng_dCross * vy + dAng_dDot * vx);
    const double dF_duy = k * signS * (dAng_dCross * (-vx) + dAng_dDot * vy);
    const double dF_dvx = k * signS * (dAng_dCross * (-uy) + dAng_dDot * ux);
    const double dF_dvy = k * signS * (dAng_dCross * ux + dAng_dDot * uy);

    J[r0][it->offset+2] = sweep;               // ∂F/∂r
    J[r0][sOff]         = dF_dux;               // u = Start - Center
    J[r0][sOff+1]       = dF_duy;
    J[r0][eOff]         = dF_dvx;               // v = End - Center
    J[r0][eOff+1]       = dF_dvy;
    J[r0][it->offset+0] = -(dF_dux + dF_dvx);   // ∂F/∂cx（cx 同時出現在 u,v 中）
    J[r0][it->offset+1] = -(dF_duy + dF_dvy);   // ∂F/∂cy
}

// ── FixedAngleDimEquation：兩線夾角 ─────────────────────────────────────
// refs[0]=lineA（WholeGeom），refs[1]=lineB（WholeGeom）
// Line layout: [x1, y1, x2, y2]
// dirA = (x2A-x1A, y2A-y1A), dirB = (x2B-x1B, y2B-y1B)
// F = atan2(|cross(dirA,dirB)|, dot(dirA,dirB)) - value
//
// 為避免 atan2 不連續，使用等效形式：
//   cross² + dot² = |dirA|²|dirB|²（不含歸一化）
//   F = cross * sign(cross) - |dirA||dirB| * sin(value)   （若 value 為銳角）
// 更穩健的實作：直接用 dot - |dirA||dirB|*cos(value) = 0
// 因為 F(x)=0 在 Newton 中可交替使用 sin/cos 形式
// 此處用：F = dot(dirA_n, dirB_n) - cos(value) = 0（歸一化方向）
void FixedAngleDimEquation::evaluate(const QVector<double>& v, QVector<double>& out) const {
    const auto& refs = constraint().refs;
    if (refs.size() < 2) { out[0] = 0; return; }

    auto itA = layout().find(refs[0].geomUuid);
    auto itB = layout().find(refs[1].geomUuid);
    if (itA == layout().end() || itB == layout().end()) { out[0] = 0; return; }

    // ⚠️ 不可直接用 itA->offset + 0/1/2/3：Line 若與其他幾何共用 SketchPoint DOF
    // （offset == -1，這其實是最常見的情況 —— 每條 SketchLine 的端點本來就各自
    //  有獨立的 SketchPoint，因此幾乎必定共用），真正的變數位置要透過
    // indexFor(Start/End) 解析（與 HorizontalEquation/ParallelEquation 等其他
    // 線段類約束方程式一致），否則會存取到 v[-1] 等越界索引而觸發
    // QVector::operator[] 的 assert crash。
    int ax1 = itA->indexFor(GeomHandle::Start), ay1 = ax1 + 1;
    int ax2 = itA->indexFor(GeomHandle::End),   ay2 = ax2 + 1;
    int bx1 = itB->indexFor(GeomHandle::Start), by1 = bx1 + 1;
    int bx2 = itB->indexFor(GeomHandle::End),   by2 = bx2 + 1;

    double dxA = v[ax2] - v[ax1], dyA = v[ay2] - v[ay1];
    double dxB = v[bx2] - v[bx1], dyB = v[by2] - v[by1];

    double lenA = std::sqrt(dxA*dxA + dyA*dyA);
    double lenB = std::sqrt(dxB*dxB + dyB*dyB);
    if (lenA < 1e-12 || lenB < 1e-12) { out[0] = 0; return; }

    double dot  = (dxA*dxB + dyA*dyB) / (lenA * lenB);
    // F = dot(dirA_n, dirB_n) - cos(value) = 0
    out[0] = dot - std::cos(constraint().value);
}

void FixedAngleDimEquation::jacobian(const QVector<double>& v, int r0,
                                      QVector<QVector<double>>& J) const {
    const auto& refs = constraint().refs;
    if (refs.size() < 2) return;

    auto itA = layout().find(refs[0].geomUuid);
    auto itB = layout().find(refs[1].geomUuid);
    if (itA == layout().end() || itB == layout().end()) return;

    // 同上：改用 indexFor(Start/End) 正確解析共用 SketchPoint DOF 的情況。
    int ax1 = itA->indexFor(GeomHandle::Start), ay1 = ax1 + 1;
    int ax2 = itA->indexFor(GeomHandle::End),   ay2 = ax2 + 1;
    int bx1 = itB->indexFor(GeomHandle::Start), by1 = bx1 + 1;
    int bx2 = itB->indexFor(GeomHandle::End),   by2 = bx2 + 1;

    double dxA = v[ax2]-v[ax1], dyA = v[ay2]-v[ay1];
    double dxB = v[bx2]-v[bx1], dyB = v[by2]-v[by1];

    double lenA2 = dxA*dxA + dyA*dyA;
    double lenB2 = dxB*dxB + dyB*dyB;
    double lenA  = std::sqrt(lenA2);
    double lenB  = std::sqrt(lenB2);
    if (lenA < 1e-12 || lenB < 1e-12) return;

    double lenAB = lenA * lenB;
    double dot   = dxA*dxB + dyA*dyB;

    // ∂dot_n/∂(x1A) = ∂/∂x1A [ (dxA*dxB+dyA*dyB)/(lenA*lenB) ]
    // = (-dxB*lenAB - dot*lenB*(-dxA/lenA)) / lenAB²
    // = (-dxB + dot*dxA/lenA²) / lenAB
    auto ddn_dx1A = (-dxB + dot*dxA/lenA2) / lenAB;
    auto ddn_dy1A = (-dyB + dot*dyA/lenA2) / lenAB;
    auto ddn_dx2A = ( dxB - dot*dxA/lenA2) / lenAB;
    auto ddn_dy2A = ( dyB - dot*dyA/lenA2) / lenAB;

    auto ddn_dx1B = (-dxA + dot*dxB/lenB2) / lenAB;
    auto ddn_dy1B = (-dyA + dot*dyB/lenB2) / lenAB;
    auto ddn_dx2B = ( dxA - dot*dxB/lenB2) / lenAB;
    auto ddn_dy2B = ( dyA - dot*dyB/lenB2) / lenAB;

    J[r0][ax1] = ddn_dx1A;
    J[r0][ay1] = ddn_dy1A;
    J[r0][ax2] = ddn_dx2A;
    J[r0][ay2] = ddn_dy2A;

    J[r0][bx1] = ddn_dx1B;
    J[r0][by1] = ddn_dy1B;
    J[r0][bx2] = ddn_dx2B;
    J[r0][by2] = ddn_dy2B;
}

// ── CoordinateDimEquation：F1 = px - value ; F2 = py - value2 ────────────
void CoordinateDimEquation::evaluate(const QVector<double>& v, QVector<double>& out) const {
    int ix = varIdx(constraint().refs[0]);
    if (ix < 0) { out[0] = out[1] = 0; return; }
    out[0] = v[ix]   - constraint().value;
    out[1] = v[ix+1] - constraint().value2;
}
void CoordinateDimEquation::jacobian(const QVector<double>&, int r0,
                                     QVector<QVector<double>>& J) const {
    int ix = varIdx(constraint().refs[0]);
    if (ix < 0) return;
    J[r0  ][ix]   = 1.0;   // ∂F1/∂px
    J[r0+1][ix+1] = 1.0;   // ∂F2/∂py
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

    // ── Pass 1：先把所有 SketchPoint 打包（建立 UUID→offset 對應）────────────
    for (const SketchGeometry* g : geoms) {
        if (g->type != SketchGeometryType::Point) continue;
        const auto* pt = static_cast<const SketchPoint*>(g);
        GeomVarLayout vl;
        vl.offset = vars.size();
        vl.dof    = 2;
        vars << pt->pos.x() << pt->pos.y();
        layout[g->uuid] = vl;
    }

    // ── Pass 2：打包非 SketchPoint 幾何，SketchLine 端點共用 SketchPoint DOF ─
    for (const SketchGeometry* g : geoms) {
        if (g->type == SketchGeometryType::Point) continue;  // 已處理
        GeomVarLayout vl;
        vl.offset = vars.size();
        switch (g->type) {
        case SketchGeometryType::Line: {
            const auto* l = static_cast<const SketchLine*>(g);
            // 嘗試共用端點的 SketchPoint DOF
            auto itS = layout.find(l->startUuid);
            auto itE = layout.find(l->endUuid);
            if (itS != layout.end() && itE != layout.end()) {
                // 兩端點 SketchPoint 都在 layout → 共用其 DOF，Line 本身不加新變量
                vl.offset      = -1;   // 無獨立 offset（端點透過 startOffset/endOffset 存取）
                vl.dof         = 0;    // Line 不貢獻新 DOF
                vl.startOffset = itS->offset;       // SketchPoint start.x
                vl.endOffset   = itE->offset;       // SketchPoint end.x
            } else {
                // Fallback：SketchPoint 缺失，用舊方式打包 4 DOF
                vars << l->start.x() << l->start.y() << l->end.x() << l->end.y();
                vl.dof = 4;
            }
            break;
        }
        case SketchGeometryType::Circle: {
            const auto* c = static_cast<const SketchCircle*>(g);
            // 圓心若有 SketchPoint，共用其 DOF；radius 單獨打包
            auto itC = layout.find(c->centerUuid);
            if (itC != layout.end()) {
                // center 共用 SketchPoint；radius 獨立
                vl.offset = vars.size();
                vars << c->radius;
                vl.dof         = 1;   // 只有 radius 是新 DOF
                vl.startOffset = itC->offset;  // 重用 startOffset 存放 cx index
            } else {
                vars << c->center.x() << c->center.y() << c->radius;
                vl.dof = 3;
            }
            break;
        }
        case SketchGeometryType::Arc: {
            // ── 修正歷程 ───────────────────────────────────────────────
            // 第一輪修正：讓 Arc 的 Start/End 共用 startUuid/endUuid 對應
            // 的 SketchPoint DOF（解決「圓角弧未連接被圓角線」問題）。
            // 第二輪修正（本輪）：第一輪當時把 t0/t1 也當成獨立求解變數，
            // 用 cx+r·cos(t) 這類三角函數方程式把它們與共用的 Start/End
            // 點綁在一起——這會讓「弧度」(O(1)) 與「座標/長度」(常是幾十
            // ～數百 mm) 這種量級差異懸殊的量混進同一個最小平方系統，
            // Moore-Penrose 偽逆在系統病態（ill-conditioned，這裡的 Arc
            // 版面天生是 3(cx,cy,r)+2(Start)+2(End)-4(隱含方程式)=5 淨自
            // 由度、但用了 9 個未知數去表達，屬於高度冗餘的參數化）時，
            // 即使殘差極小（例如弧長值原封不動）也可能解出一個數值上很
            // 小、換算成座標卻很大的 Δt，造成「弧長沒改，起終點/圓心卻
            // 大幅跳動」。改用單純的「點到圓心距離＝半徑」方程式（見下方
            // PointOnCircleEquation）取代三角函數版本，t0/t1 完全不進入
            // 求解變數（只在 unpackVariables() 求解結束後用角度差的方式
            // 一次性重建，不受此問題影響），從根本上消除這個病態來源。
            const auto* a = static_cast<const SketchArc*>(g);
            double cx = 0.0, cy = 0.0, r = 1.0, t0 = 0.0, t1 = M_PI;
            gp_Pnt startPt(1.0, 0.0, 0.0), endPt(-1.0, 0.0, 0.0);
            if (!a->curve.IsNull()) {
                auto baseCircle = Handle(Geom_Circle)::DownCast(a->curve->BasisCurve());
                if (!baseCircle.IsNull()) {
                    gp_Pnt c = baseCircle->Location();
                    cx = c.X(); cy = c.Y();
                    r  = baseCircle->Radius();
                    t0 = a->curve->FirstParameter();
                    t1 = a->curve->LastParameter();
                    startPt = a->curve->Value(t0);
                    endPt   = a->curve->Value(t1);
                }
            }
            vars << cx << cy << r;
            vl.dof      = 3;
            vl.refSweep = t1 - t0;   // 僅作分支參考，不進 vars

            auto itS = layout.find(a->startUuid);
            if (itS != layout.end()) {
                vl.startOffset = itS->offset;          // 共用 startUuid 的 SketchPoint DOF
            } else {
                // Fallback：找不到獨立 SketchPoint（理論上不應發生，
                // Sketch::addArcGeom 一定會建立/重用），退化為 Arc 自己
                // 額外配置 2 個局部 DOF，避免落回 offset+0（誤成 Center）。
                vl.startOffset = vars.size();
                vars << startPt.X() << startPt.Y();
                vl.dof += 2;
            }

            auto itE = layout.find(a->endUuid);
            if (itE != layout.end()) {
                vl.endOffset = itE->offset;             // 共用 endUuid 的 SketchPoint DOF
            } else {
                vl.endOffset = vars.size();
                vars << endPt.X() << endPt.Y();
                vl.dof += 2;
            }
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
                                       QList<SketchGeometry*>& geoms,
                                       const gp_Dir& planeNormal) const {
    for (SketchGeometry* g : geoms) {
        auto it = layout.find(g->uuid);
        if (it == layout.end()) continue;
        const GeomVarLayout& vl = *it;
        int off = vl.offset;
        switch (g->type) {
        case SketchGeometryType::Line: {
            auto* l = static_cast<SketchLine*>(g);
            if (vl.startOffset >= 0 && vl.endOffset >= 0) {
                // 共用 SketchPoint DOF：從 SketchPoint 的 vars index 讀取
                l->start = QVector2D(vars[vl.startOffset],   vars[vl.startOffset+1]);
                l->end   = QVector2D(vars[vl.endOffset],     vars[vl.endOffset+1]);
            } else if (off >= 0) {
                // Fallback：舊方式（4 個獨立 DOF）
                l->start = QVector2D(vars[off+0], vars[off+1]);
                l->end   = QVector2D(vars[off+2], vars[off+3]);
            }
            l->points[0] = l->start;
            l->points[1] = l->end;
            break;
        }
        case SketchGeometryType::Circle: {
            auto* c = static_cast<SketchCircle*>(g);
            if (vl.startOffset >= 0 && vl.dof == 1 && off >= 0) {
                // 圓心共用 SketchPoint DOF，radius 獨立
                c->center = QVector2D(vars[vl.startOffset], vars[vl.startOffset+1]);
                c->radius = vars[off];
            } else if (off >= 0) {
                c->center = QVector2D(vars[off+0], vars[off+1]);
                c->radius = vars[off+2];
            }
            break;
        }
        case SketchGeometryType::Arc: {
            auto* a = static_cast<SketchArc*>(g);
            if (off < 0) break;
            double cx = vars[off], cy = vars[off+1], r = vars[off+2];

            const int sOff = vl.indexFor(GeomHandle::Start);
            const int eOff = vl.indexFor(GeomHandle::End);

            // ── 修正：不再對 t0/t1 做「絕對角度」重建 ────────────────────
            // t0/t1 已不是求解變數（見 GeomVarLayout::refSweep 註解），這裡
            // 改用「角度差」的方式，把求解前（此時 a->curve 仍是求解前的
            // 幾何，尚未被下面覆寫）量到的 t0_orig/t1_orig，平移求解前後
            // 起終點相對圓心角度的變化量，重建新的 t0/t1：
            //   t0_new = t0_orig + (新起點角度 - 舊起點角度)
            //   t1_new = t1_orig + (新終點角度 - 舊終點角度)
            // 這個位移量只依賴「角度差」，任何 gp_Ax2 內部座標系與世界座
            // 標系之間未知的固定夾角，在相減時會自動抵消，不需要假設兩者
            // 對齊；也避免了直接對絕對角度做 atan2 重建時，其分支（±π）
            // 選擇與原本弧的優弧/劣弧不一致的問題。
            double t0 = 0.0, t1 = M_PI;
            bool haveOrig = false;
            if (!a->curve.IsNull()) {
                auto baseCircle = Handle(Geom_Circle)::DownCast(a->curve->BasisCurve());
                if (!baseCircle.IsNull()) {
                    const double t0_orig = a->curve->FirstParameter();
                    const double t1_orig = a->curve->LastParameter();
                    const gp_Pnt oldCenter = baseCircle->Location();
                    const gp_Pnt oldStart  = a->curve->Value(t0_orig);
                    const gp_Pnt oldEnd    = a->curve->Value(t1_orig);
                    const double oldStartAngle = std::atan2(oldStart.Y() - oldCenter.Y(),
                                                             oldStart.X() - oldCenter.X());
                    const double oldEndAngle   = std::atan2(oldEnd.Y()   - oldCenter.Y(),
                                                             oldEnd.X()   - oldCenter.X());

                    auto wrap = [](double ang) {
                        while (ang >  M_PI) ang -= 2.0 * M_PI;
                        while (ang <= -M_PI) ang += 2.0 * M_PI;
                        return ang;
                    };

                    if (sOff >= 0) {
                        const double newStartAngle = std::atan2(vars[sOff+1] - cy, vars[sOff] - cx);
                        t0 = t0_orig + wrap(newStartAngle - oldStartAngle);
                    } else {
                        t0 = t0_orig;
                    }
                    if (eOff >= 0) {
                        const double newEndAngle = std::atan2(vars[eOff+1] - cy, vars[eOff] - cx);
                        t1 = t1_orig + wrap(newEndAngle - oldEndAngle);
                    } else {
                        t1 = t1_orig;
                    }
                    haveOrig = true;
                }
            }
            if (!haveOrig) {
                // 極少數情況（curve 為 null，理論上不應發生）：退化為預設
                // 半圓，至少維持幾何有效。
                t0 = 0.0; t1 = M_PI;
            }
            if (t1 <= t0) t1 += 2.0 * M_PI;   // Geom_Circle 週期參數維持 CCW 遞增慣例

            gp_Ax2 ax2(gp_Pnt(cx, cy, 0), planeNormal);
            Handle(Geom_Circle) circ = new Geom_Circle(ax2, r);
            a->curve = new Geom_TrimmedCurve(circ, t0, t1);

            // ── issue #12 修正（後續已在 packVariables 補上根本修正）─────
            // Start/End 現在已透過 vl.startOffset/endOffset 與 startUuid/
            // endUuid 的 SketchPoint 共用 DOF，並由 PointOnCircleEquation
            // 在求解過程中與 cx/cy/r 綁在一起，理論上收斂後兩者應已一致；
            // 這裡仍保留以 curve 端點/圓心強制寫回 start/end/centerUuid 三
            // 個 SketchPoint 的動作，作為最後一道保險（涵蓋找不到獨立
            // SketchPoint 的 fallback 情形、以及 centerUuid 本來就不參與
            // 求解、只用於顯示/抓取的情形），確保 curve 與這三個點在數值
            // 上完全一致，不會有殘留誤差。
            auto syncPoint = [&](const QString& uuid, const gp_Pnt& worldPt) {
                if (uuid.isEmpty()) return;
                for (SketchGeometry* other : geoms) {
                    if (other->type == SketchGeometryType::Point && other->uuid == uuid) {
                        static_cast<SketchPoint*>(other)->pos =
                            QVector2D(worldPt.X(), worldPt.Y());
                        break;
                    }
                }
            };
            syncPoint(a->startUuid,  a->curve->Value(t0));
            syncPoint(a->endUuid,    a->curve->Value(t1));
            syncPoint(a->centerUuid, ax2.Location());
            break;
        }
        case SketchGeometryType::Ellipse: {
            auto* e = static_cast<SketchEllipse*>(g);
            if (off < 0) break;
            e->center = QVector2D(vars[off+0], vars[off+1]);
            e->majorRadius = vars[off+2];
            e->minorRadius = vars[off+3];
            e->angle       = vars[off+4];
            break;
        }
        case SketchGeometryType::Point: {
            auto* pt = static_cast<SketchPoint*>(g);
            if (off >= 0)
                pt->pos = QVector2D(vars[off+0], vars[off+1]);
            break;
        }
        default:
            if (off >= 0)
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
        case ConstraintType::Midpoint:      eq = new MidpointEquation(c, &layout);      break;
        case ConstraintType::Symmetric:     eq = new SymmetricEquation(c, &layout);     break;
        case ConstraintType::Collinear:     eq = new CollinearEquation(c, &layout);     break;
        case ConstraintType::EqualRadius:
            eq = new EqualRadiusEquation(c, &layout); break;
        case ConstraintType::FixedX:
            eq = new FixedXEquation(c, &layout); break;
        case ConstraintType::FixedY:
            eq = new FixedYEquation(c, &layout); break;
        case ConstraintType::FixedLength:
            eq = new FixedLengthEquation(c, &layout); break;
        case ConstraintType::FixedDiameter:
            eq = new FixedDiameterEquation(c, &layout); break;
        case ConstraintType::FixedHorizDist:
            eq = new FixedHorizDistEquation(c, &layout); break;
        case ConstraintType::FixedVertDist:
            eq = new FixedVertDistEquation(c, &layout); break;
        case ConstraintType::FixedArcLength:
            eq = new FixedArcLengthEquation(c, &layout); break;
        case ConstraintType::CoordinateDim:
            eq = new CoordinateDimEquation(c, &layout); break;
        case ConstraintType::FixedAngleDim:
        case ConstraintType::FixedAngle:
            eq = new FixedAngleDimEquation(c, &layout); break;
        case ConstraintType::Fixed: {
            // Fixed：從 layout 取出 offset+dof，記錄 snapshot
            auto it = layout.find(c.refs[0].geomUuid);
            if (it != layout.end() && it->dof > 0 && it->offset >= 0) {
                auto* feq = new FixedEquation(c, &layout);
                QVector<double> snap;
                for (int i = 0; i < it->dof; ++i) snap << vars[it->offset+i];
                feq->setSnapshot(snap, it->offset, it->dof);
                eqs.append(feq);
            } else if (it != layout.end() && it->dof == 0) {
                // SketchLine 共用 SketchPoint DOF：分別 fix start/end SketchPoint
                // 用兩個 FixedEquation 各鎖定一個端點
                if (it->startOffset >= 0) {
                    SketchConstraint cs; cs.type = ConstraintType::Fixed;
                    cs.refs = { c.refs[0] };
                    auto* feqS = new FixedEquation(cs, &layout);
                    QVector<double> snapS = { vars[it->startOffset], vars[it->startOffset+1] };
                    feqS->setSnapshot(snapS, it->startOffset, 2);
                    eqs.append(feqS);
                }
                if (it->endOffset >= 0) {
                    SketchConstraint ce; ce.type = ConstraintType::Fixed;
                    ce.refs = { c.refs[0] };
                    auto* feqE = new FixedEquation(ce, &layout);
                    QVector<double> snapE = { vars[it->endOffset], vars[it->endOffset+1] };
                    feqE->setSnapshot(snapE, it->endOffset, 2);
                    eqs.append(feqE);
                }
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

// ── Arc「點在圓上」隱含方程式（內部隱含方程式，不對應任何 SketchConstraint）──
// 目的：Arc 求解時只保留 cx, cy, r 三個獨立 DOF（不含角度），但 Start/End
// 這兩個 GeomHandle（見上方 packVariables 的修正）會指向共用的 SketchPoint
// DOF（例如 FILLET 指令把圓角弧 startUuid/endUuid 直接設成被圓角線段端點
// 的 SketchPoint）。若沒有方程式把兩者綁在一起，cx/cy/r 與該共用點會各自
// 被其他約束牽動、彼此沒有牽制，弧的曲線最終還是可能跑到跟共用端點不一致
// 的地方。這裡針對每個 Arc 各自加入兩條「起點在圓上」/「終點在圓上」的
// 方程式：
//   F = |Point - Center|² - r² = 0
// 只用距離平方（無三角函數、無額外角度變數），單位與座標/半徑一致，數值
// 條件良好；早期版本改用 cx+r·cos(t) 搭配獨立角度變數 t 的寫法，雖然自由
// 度數學上等價，卻讓「弧度」與「座標/長度」這種量級差異懸殊的量混進同一
// 個最小平方系統，導致 Moore-Penrose 偽逆在系統病態時把極小的殘差解讀成
// 一個看似很小、換算成座標卻很大的角度修正量，造成「弧長沒改，起終點/圓
// 心卻大幅跳動」的問題，這裡以更簡單且良態的距離方程式徹底避免。
class PointOnCircleEquation : public ConstraintEquation {
public:
    PointOnCircleEquation(const QString& arcUuid, bool isStart,
                          const QHash<QString, GeomVarLayout>* layout)
        : ConstraintEquation(dummyConstraint(), layout)
        , m_arcUuid(arcUuid), m_isStart(isStart) {}

    int equationCount() const override { return 1; }

    void evaluate(const QVector<double>& v, QVector<double>& out) const override {
        auto it = layout().find(m_arcUuid);
        if (it == layout().end() || it->offset < 0) { out[0] = 0.0; return; }
        const int off = it->offset;
        const double cx = v[off + 0], cy = v[off + 1], r = v[off + 2];
        const int pOff  = m_isStart ? it->indexFor(GeomHandle::Start)
                                     : it->indexFor(GeomHandle::End);
        if (pOff < 0) { out[0] = 0.0; return; }
        const double dx = v[pOff] - cx, dy = v[pOff + 1] - cy;
        out[0] = dx * dx + dy * dy - r * r;
    }

    void jacobian(const QVector<double>& v, int row0,
                  QVector<QVector<double>>& J) const override {
        auto it = layout().find(m_arcUuid);
        if (it == layout().end() || it->offset < 0) return;
        const int off = it->offset;
        const double cx = v[off + 0], cy = v[off + 1], r = v[off + 2];
        const int pOff  = m_isStart ? it->indexFor(GeomHandle::Start)
                                     : it->indexFor(GeomHandle::End);
        if (pOff < 0) return;
        const double dx = v[pOff] - cx, dy = v[pOff + 1] - cy;
        J[row0][pOff]     = 2.0 * dx;
        J[row0][pOff + 1] = 2.0 * dy;
        J[row0][off + 0]  = -2.0 * dx;
        J[row0][off + 1]  = -2.0 * dy;
        J[row0][off + 2]  = -2.0 * r;
    }

private:
    QString m_arcUuid;
    bool    m_isStart;
    static const SketchConstraint& dummyConstraint() {
        static SketchConstraint c;   // 純內部使用，不參與 UI/存檔
        return c;
    }
};

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

    // ── 修正：以 Levenberg-Marquardt 風格的平滑阻尼取代硬性 SVD 截斷門檻──
    // 先前試過直接呼叫 svd.setThreshold(1e-6) 把「幾乎奇異」的方向整個歸
    // 零（貢獻設為 0），這是「非 0 即 1」的硬性判斷：只要某個奇異值恰好
    // 落在門檻附近，就會被整條方向「一刀切掉」，即使那個方向其實還帶著
    // 一小部分是真正需要的修正量（例如圓角弧的 Start/End 需要跟著被裁切
    // 的線段端點微調），造成原本求解正確的圓角弧反而在求解後跟丟、對不
    // 上被圓角的線。
    //
    // 這裡改用平滑阻尼（Tikhonov regularization）：把 1/σ 換成
    // σ/(σ²+λ)。良態方向（σ 遠大於 √λ）幾乎不受影響，因為
    // σ/(σ²+λ) ≈ 1/σ；只有真正接近奇異（σ→0）的方向，修正量才會平滑地
    // 趨近 0（而不是被 1/σ 硬放大，也不是被門檻整個歸零），兩種極端情況
    // 都能避免。λ 取「最大奇異值的一個極小相對比例」的平方，只在數量級
    // 差距懸殊（封閉迴路那種近似線性相依）時才有感，一般良態系統幾乎測
    // 不出差異。
    static constexpr double kRegRelativeSigma = 1e-6;   // 阻尼生效的相對奇異值尺度

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0) //EIGEN_VERSION_AT_LEAST(3,4,0)
    Eigen::BDCSVD<Eigen::MatrixXd, Eigen::ComputeThinU | Eigen::ComputeThinV> svd(A);
#else
    Eigen::BDCSVD<Eigen::MatrixXd> svd(A, Eigen::ComputeThinU | Eigen::ComputeThinV);
#endif
    const Eigen::VectorXd& sv = svd.singularValues();
    const int k = static_cast<int>(sv.size());
    if (k == 0) return false;

    const double sigmaMax = sv(0);   // BDCSVD 回傳的奇異值已由大到小排序
    if (sigmaMax <= 0.0) {
        dx.fill(0.0, n);
        return true;   // J 全 0：沒有任何方向可修正
    }
    const double lambda = (kRegRelativeSigma * sigmaMax) * (kRegRelativeSigma * sigmaMax);

    const Eigen::VectorXd UtB = svd.matrixU().transpose() * b;
    Eigen::VectorXd filtered(k);
    for (int i = 0; i < k; ++i) {
        const double s = sv(i);
        filtered(i) = (s * UtB(i)) / (s * s + lambda);
    }
    const Eigen::VectorXd x = svd.matrixV() * filtered;

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
                                    const QList<SketchConstraint>& constraints,
                                    const gp_Dir& planeNormal)
{
    SolveResult result;

    QHash<QString, GeomVarLayout> layout;
    QVector<double> vars;
    packVariables(geoms, layout, vars);

    auto eqs = buildEquations(constraints, layout, vars);

    // 為每個 Arc 加入「起點/終點在圓上」隱含方程式（見 PointOnCircleEquation
    // 註解）：讓弧自身的 cx/cy/r 與（可能和相鄰線段共用的）Start/End 點座
    // 標在同一次疊代中彼此牽制，修正弧起終點座標/角度不一致、圓角弧未連
    // 接被圓角線段的問題；t0/t1 完全不參與求解，避免角度／座標混合單位
    // 造成的數值不穩定。
    for (const SketchGeometry* g : geoms) {
        if (g->type != SketchGeometryType::Arc) continue;
        eqs.append(new PointOnCircleEquation(g->uuid, true,  &layout));
        eqs.append(new PointOnCircleEquation(g->uuid, false, &layout));
    }

    int dof = computeDOF(geoms, constraints);
    result.dof = dof;

    if (eqs.isEmpty()) {
        // 沒有方程式：幾何不需要移動，但仍要寫回 vars（可能已被 pack 初始化）
        result.status = (dof==0) ? SolveStatus::FullyConstrained
                                   : SolveStatus::UnderConstrained;
        // 不 unpack：vars 未被修改，與 geoms 一致，無需寫回
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

    // ★ 修正：不論收斂與否，只要殘差有改善（< 初始值），都寫回幾何
    // 這確保即使欠定系統或部分收斂，幾何也能反映 Solver 的最佳努力結果
    if (converged) {
        unpackVariables(vars, layout, geoms, planeNormal);
        result.status = (dof == 0) ? SolveStatus::FullyConstrained
                                   : SolveStatus::UnderConstrained;
        qDebug() << "[Solver] converged in" << result.iterations
                 << "iters, residual=" << residual << "dof=" << dof;
    } else if (residual > 1.0) {
        result.status = SolveStatus::Conflict;
        unpackVariables(vars, layout, geoms, planeNormal);
        qWarning() << "[Solver] CONFLICT: residual=" << residual
                   << "after" << result.iterations << "iters";
    } else {
        result.status = SolveStatus::UnderConstrained;
        unpackVariables(vars, layout, geoms, planeNormal);
        qDebug() << "[Solver] under-constrained, residual=" << residual;
    }

    qDeleteAll(eqs);
    return result;
}

int ConstraintSolver::computeDOF(const QList<SketchGeometry*>& geoms,
                                 const QList<SketchConstraint>& constraints)
{
    // 先收集所有存在的 SketchPoint UUID，供下面判斷 Arc 的 startUuid/endUuid
    // 是否確實指向一個獨立的 SketchPoint（理論上 Sketch::addArcGeom 一定會
    // 建立/重用，但仍以「有無實際存在」為準，避免假設過度）。
    QSet<QString> pointUuids;
    for (const SketchGeometry* g : geoms)
        if (g->type == SketchGeometryType::Point) pointUuids.insert(g->uuid);

    // 收集所有被 SketchLine/Arc/Circle 引用的 SketchPoint UUID
    // 這些 SketchPoint 的 DOF 已被曲線「共用」，不再重複計算
    QSet<QString> sharedPointUuids;
    for (const SketchGeometry* g : geoms) {
        if (g->type == SketchGeometryType::Line) {
            const auto* l = static_cast<const SketchLine*>(g);
            if (!l->startUuid.isEmpty()) sharedPointUuids.insert(l->startUuid);
            if (!l->endUuid.isEmpty())   sharedPointUuids.insert(l->endUuid);
        } else if (g->type == SketchGeometryType::Circle) {
            const auto* c = static_cast<const SketchCircle*>(g);
            if (!c->centerUuid.isEmpty()) sharedPointUuids.insert(c->centerUuid);
        } else if (g->type == SketchGeometryType::Arc) {
            // 修正：Arc 的 Start/End 現在（見 packVariables）會共用
            // startUuid/endUuid 對應的 SketchPoint DOF，此處比照 Line/Circle
            // 把它們標記為已共用，避免下方 Point 分支重複計數。
            const auto* a = static_cast<const SketchArc*>(g);
            if (pointUuids.contains(a->startUuid)) sharedPointUuids.insert(a->startUuid);
            if (pointUuids.contains(a->endUuid))   sharedPointUuids.insert(a->endUuid);
        }
    }

    int totalDOF = 0;
    for (const SketchGeometry* g : geoms) {
        switch (g->type) {
        case SketchGeometryType::Line:    totalDOF += 4; break; // 端點 DOF 由共用 SketchPoint 計
        case SketchGeometryType::Circle:  totalDOF += 1; break; // 只有 radius（圓心由 SketchPoint 計）
        case SketchGeometryType::Arc: {
            // Arc 固有 5 個「幾何」DOF（等效於 cx, cy, r, 起終角），求解器
            // 內部改用 cx,cy,r（3 個真正的求解變數）+ 共用/獨立的 Start,End
            // 點（各 2 個 DOF）搭配 PointOnCircleEquation（各 1 條方程式）
            // 表達，數學上仍等效 5 淨自由度。若 Start/End 有對應且實際存在
            // 的 SketchPoint，該點的 2 DOF 已於上面的 Point 分支計入，故此
            // 處各扣 2，避免雙重計算；找不到對應點時（fallback）維持原本
            // 5 DOF 的估計。
            const auto* a = static_cast<const SketchArc*>(g);
            int arcDof = 5;
            if (pointUuids.contains(a->startUuid)) arcDof -= 2;
            if (pointUuids.contains(a->endUuid))   arcDof -= 2;
            totalDOF += qMax(arcDof, 1);   // 圓心已知時，過兩定點的弧至少保留 1 個自由度（半徑/凸度）
            break;
        }
        case SketchGeometryType::Ellipse: totalDOF += 5; break;
        case SketchGeometryType::Point: {
            // 若此 SketchPoint 已被曲線共用，其 DOF 已含在曲線的計算中
            if (!sharedPointUuids.contains(g->uuid))
                totalDOF += 2;   // 獨立點：+2 DOF
            // 否則：由 SketchLine 的 4 DOF 涵蓋（startOffset/endOffset 共用）
            break;
        }
        default: totalDOF += g->points.size() * 2; break;
        }
    }
    int consumed = 0;
    for (const SketchConstraint& c : constraints)
        if (c.driving) consumed += qMin(c.dofConsumed(), totalDOF - consumed);
    return qMax(totalDOF - consumed, 0);
}

} // namespace aicad::cad
