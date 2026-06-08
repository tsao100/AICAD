#include "DimensionLineAIS.h"
#include "../Sketch.h"

#include <Graphic3d_ArrayOfPolylines.hxx>
#include <Graphic3d_Group.hxx>
#include <Graphic3d_AspectLine3d.hxx>
#include <Graphic3d_Text.hxx>
#include <Graphic3d_AspectText3d.hxx>
#include <Prs3d_Text.hxx>
#include <Prs3d_TextAspect.hxx>
#include <Prs3d_Drawer.hxx>
#include <SelectMgr_EntityOwner.hxx>
#include <Select3D_SensitiveBox.hxx>
#include <Bnd_Box.hxx>
#include <Quantity_Color.hxx>
#include <TCollection_ExtendedString.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Vec2d.hxx>
#include <QDebug>
#include <cmath>

IMPLEMENT_STANDARD_RTTIEXT(aicad::cad::AIS_DimensionLine, AIS_InteractiveObject)

namespace aicad::cad {

AIS_DimensionLine::AIS_DimensionLine(
        const SketchConstraint& c,
        const QList<SketchGeometry*>& geoms,
        const gp_Trsf& sketchToWorld,
        SolveStatus status)
    : m_constraint(c)
    , m_geoms(geoms)
    , m_sketchToWorld(sketchToWorld)
    , m_status(status)
{
    SetInfiniteState(Standard_False);
}

void AIS_DimensionLine::Update(
        const SketchConstraint& c,
        const QList<SketchGeometry*>& geoms,
        SolveStatus status)
{
    m_constraint = c;
    m_geoms      = geoms;
    m_status     = status;
    m_hasRefPos  = false;  // 清除舊的端點快取，待呼叫端重新設定
}

QString AIS_DimensionLine::labelText() const {
    const double v = m_constraint.value;
    QString base;
    switch (m_constraint.type) {
    case ConstraintType::FixedDiameter:
        base = QString("Ø%1").arg(v, 0, 'f', 2);
        break;
    case ConstraintType::CoordinateDim:
        // drawCoordinateDimension 各自畫 X/Y 標籤；此處回傳 X 標籤
        base = QString("X=%1").arg(v, 0, 'f', 2);
        break;
    default:
        base = QString::number(v, 'f', 2);
        break;
    }
    if (!m_constraint.paramExpr.isEmpty())
        return QString("%1 = %2").arg(m_constraint.paramExpr, base);
    return base;
}

// ─────────────────────────────────────────────────────────────────────────────
// 輔助：從約束的幾何參考取得兩個世界座標點
// ─────────────────────────────────────────────────────────────────────────────

void AIS_DimensionLine::setRefPositions(const QVector2D& p1, const QVector2D& p2)
{
    m_hasRefPos = true;
    m_refPos1   = p1;
    m_refPos2   = p2;
}

bool AIS_DimensionLine::getRefPoints(gp_Pnt& p1, gp_Pnt& p2) const {
    // 若已明確設定端點位置（例如 FixedDistance 端點 handle），優先使用
    if (m_hasRefPos) {
        gp_Pnt pt1(m_refPos1.x(), m_refPos1.y(), 0.0);
        gp_Pnt pt2(m_refPos2.x(), m_refPos2.y(), 0.0);
        pt1.Transform(m_sketchToWorld);
        pt2.Transform(m_sketchToWorld);
        p1 = pt1; p2 = pt2;
        return true;
    }
    // 從 m_geoms 取幾何的中心點作為端點
    if (m_geoms.isEmpty()) return false;

    auto geomCenter = [](const SketchGeometry* g) -> QVector2D {
        if (!g || g->points.isEmpty()) return {};
        QVector2D c(0, 0);
        for (const QVector2D& p : g->points) c += p;
        return c / static_cast<float>(g->points.size());
    };

    QVector2D c1 = geomCenter(m_geoms.value(0));
    QVector2D c2 = m_geoms.size() > 1 ? geomCenter(m_geoms.value(1)) : c1 + QVector2D(m_constraint.value, 0);

    gp_Pnt pt1(c1.x(), c1.y(), 0.0);
    gp_Pnt pt2(c2.x(), c2.y(), 0.0);
    pt1.Transform(m_sketchToWorld);
    pt2.Transform(m_sketchToWorld);
    p1 = pt1;
    p2 = pt2;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// 顏色
// ─────────────────────────────────────────────────────────────────────────────

static Quantity_Color dimColor(bool driving, SolveStatus s) {
    if (!driving) return Quantity_Color(Quantity_NOC_GRAY60);
    switch (s) {
    case SolveStatus::FullyConstrained:  return Quantity_Color(Quantity_NOC_GREEN4);
    case SolveStatus::UnderConstrained:  return Quantity_Color(Quantity_NOC_CYAN2);
    case SolveStatus::OverConstrained:   return Quantity_Color(Quantity_NOC_RED);
    default:                             return Quantity_Color(Quantity_NOC_YELLOW);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Compute — 根據約束類型繪製尺寸線
// ─────────────────────────────────────────────────────────────────────────────

void AIS_DimensionLine::Compute(
        const Handle(PrsMgr_PresentationManager)&,
        const Handle(Prs3d_Presentation)& prs,
        const Standard_Integer /*mode*/)
{
    prs->Clear();

    switch (m_constraint.type) {
    case ConstraintType::FixedRadius:
        drawRadiusDimension(prs);  break;
    case ConstraintType::FixedX:
        drawHorizontalDim(prs);    break;
    case ConstraintType::FixedY:
        drawVerticalDim(prs);      break;
    case ConstraintType::FixedAngleDim:
    case ConstraintType::FixedAngle:
        drawAngleDim(prs);         break;
    case ConstraintType::FixedLength:
        drawLengthDimension(prs);  break;
    case ConstraintType::FixedDiameter:
        drawDiameterDimension(prs); break;
    case ConstraintType::FixedHorizDist:
        drawHorizontalDim(prs);    break;  // 複用既有
    case ConstraintType::FixedVertDist:
        drawVerticalDim(prs);      break;  // 複用既有
    case ConstraintType::FixedArcLength:
        drawArcLengthDimension(prs); break;
    case ConstraintType::CoordinateDim:
        drawCoordinateDimension(prs); break;
    case ConstraintType::FixedDistance:
    default:
        drawLinearDimension(prs);  break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 尺寸線繪製輔助 lambda
// ─────────────────────────────────────────────────────────────────────────────

static Handle(Graphic3d_AspectText3d) makeTextAspect(const Quantity_Color& col)
{
    Handle(Graphic3d_AspectText3d) asp = new Graphic3d_AspectText3d();
    asp->SetColor(col);
    return asp;
}

static void addArrow(const Handle(Prs3d_Presentation)& prs,
                     const gp_Pnt& tip, const gp_Vec& dir,
                     const Quantity_Color& col)
{
    Handle(Graphic3d_Group) grp = prs->NewGroup();
    Handle(Graphic3d_AspectLine3d) asp =
        new Graphic3d_AspectLine3d(col, Aspect_TOL_SOLID, 1.0f);
    grp->SetPrimitivesAspect(asp);

    gp_Vec side = dir.Crossed(gp_Vec(0, 0, 1));
    side.Normalize();
    double aLen = 2.0, aWid = 0.8;

    gp_Pnt base = tip.Translated(dir * aLen);
    gp_Pnt l    = base.Translated(side *  aWid);
    gp_Pnt r    = base.Translated(side * -aWid);

    Handle(Graphic3d_ArrayOfPolylines) seg =
        new Graphic3d_ArrayOfPolylines(4, 2);
    seg->AddBound(3);
    seg->AddVertex(l); seg->AddVertex(tip); seg->AddVertex(r);
    seg->AddBound(1);
    seg->AddVertex(base);
    grp->AddPrimitiveArray(seg);
}

static void addDimLine(const Handle(Prs3d_Presentation)& prs,
                       const gp_Pnt& p1, const gp_Pnt& p2,
                       double offset,
                       const Quantity_Color& col,
                       const QString& label)
{
    gp_Vec up(0, 0, 1);
    gp_Vec along(p2.X() - p1.X(), p2.Y() - p1.Y(), p2.Z() - p1.Z());
    if (along.Magnitude() < Precision::Confusion()) return;
    along.Normalize();

    gp_Vec perp = along.Crossed(up);
    if (perp.Magnitude() < Precision::Confusion())
        perp = gp_Vec(0, 1, 0);
    perp.Normalize();

    gp_Pnt d1 = p1.Translated(perp * offset);
    gp_Pnt d2 = p2.Translated(perp * offset);

    // 主尺寸線（綠色）
    Quantity_Color lineCol(0.0, 0.8, 0.0, Quantity_TOC_RGB);
    Handle(Graphic3d_Group) grp = prs->NewGroup();
    Handle(Graphic3d_AspectLine3d) asp =
        new Graphic3d_AspectLine3d(lineCol, Aspect_TOL_SOLID, 1.5f);
    grp->SetPrimitivesAspect(asp);

    Handle(Graphic3d_ArrayOfPolylines) line =
        new Graphic3d_ArrayOfPolylines(6, 3);
    // 延伸線 p1→d1
    line->AddBound(2); line->AddVertex(p1); line->AddVertex(d1);
    // 延伸線 p2→d2
    line->AddBound(2); line->AddVertex(p2); line->AddVertex(d2);
    // 尺寸線 d1→d2
    line->AddBound(2); line->AddVertex(d1); line->AddVertex(d2);
    grp->AddPrimitiveArray(line);

    // 箭頭（綠色）
    addArrow(prs, d1, along,       lineCol);
    addArrow(prs, d2, along * -1., lineCol);

    // 標籤文字（紅色，字高 36，平行尺寸線，居中）
    if (!label.isEmpty()) {
        gp_Pnt mid(
            (d1.X() + d2.X()) * 0.5,
            (d1.Y() + d2.Y()) * 0.5,
            (d1.Z() + d2.Z()) * 0.5);

        // 使用 Graphic3d_Text 支援旋轉（平行尺寸線方向）
        Handle(Graphic3d_Text) gtext = new Graphic3d_Text(36.0f);
        gtext->SetText(TCollection_ExtendedString(label.toUtf8().constData(), Standard_True));
        gtext->SetPosition(mid);
        // 設定文字朝向：X 軸對齊尺寸線方向（along），Z 軸為螢幕外
        gp_Vec d1d2(d1, d2);
        if (d1d2.Magnitude() > Precision::Confusion()) {
            d1d2.Normalize();
            gp_Vec zAxis(0, 0, 1);
            gp_Vec yAxis = zAxis.Crossed(d1d2);
            if (yAxis.Magnitude() > Precision::Confusion()) {
                yAxis.Normalize();
                gtext->SetOrientation(gp_Ax2(mid, gp_Dir(zAxis), gp_Dir(d1d2)));
            }
        }
        gtext->SetHorizontalAlignment(Graphic3d_HTA_CENTER);
        gtext->SetVerticalAlignment(Graphic3d_VTA_CENTER);
        Handle(Graphic3d_Group) txtGrp = prs->NewGroup();
        txtGrp->SetGroupPrimitivesAspect(makeTextAspect(Quantity_Color(Quantity_NOC_RED)));
        txtGrp->AddText(gtext);
    }
}

// 明確指定尺寸線端點（d1、d2）的繪製版本，延伸線從 p1→d1、p2→d2
static void addDimLineExplicit(const Handle(Prs3d_Presentation)& prs,
                                const gp_Pnt& p1, const gp_Pnt& p2,
                                const gp_Pnt& d1, const gp_Pnt& d2,
                                const Quantity_Color& col,
                                const QString& label)
{
    gp_Vec along(d1, d2);
    if (along.Magnitude() < Precision::Confusion()) return;
    along.Normalize();

    Quantity_Color lineColEx(0.0, 0.8, 0.0, Quantity_TOC_RGB);
    Handle(Graphic3d_Group) grp = prs->NewGroup();
    Handle(Graphic3d_AspectLine3d) asp =
        new Graphic3d_AspectLine3d(lineColEx, Aspect_TOL_SOLID, 1.5f);
    grp->SetPrimitivesAspect(asp);

    Handle(Graphic3d_ArrayOfPolylines) line =
        new Graphic3d_ArrayOfPolylines(6, 3);
    line->AddBound(2); line->AddVertex(p1); line->AddVertex(d1);
    line->AddBound(2); line->AddVertex(p2); line->AddVertex(d2);
    line->AddBound(2); line->AddVertex(d1); line->AddVertex(d2);
    grp->AddPrimitiveArray(line);

    addArrow(prs, d1, along,       lineColEx);
    addArrow(prs, d2, along * -1., lineColEx);

    if (!label.isEmpty()) {
        gp_Pnt mid(
            (d1.X() + d2.X()) * 0.5,
            (d1.Y() + d2.Y()) * 0.5,
            (d1.Z() + d2.Z()) * 0.5);
        Handle(Graphic3d_Text) gtext = new Graphic3d_Text(36.0f);
        gtext->SetText(TCollection_ExtendedString(label.toUtf8().constData(), Standard_True));
        gtext->SetPosition(mid);
        gp_Vec d1d2(d1, d2);
        if (d1d2.Magnitude() > Precision::Confusion()) {
            d1d2.Normalize();
            gp_Vec zAxis(0, 0, 1);
            gtext->SetOrientation(gp_Ax2(mid, gp_Dir(zAxis), gp_Dir(d1d2)));
        }
        gtext->SetHorizontalAlignment(Graphic3d_HTA_CENTER);
        gtext->SetVerticalAlignment(Graphic3d_VTA_CENTER);
        Handle(Graphic3d_Group) txtGrp = prs->NewGroup();
        txtGrp->SetGroupPrimitivesAspect(makeTextAspect(Quantity_Color(Quantity_NOC_RED)));
        txtGrp->AddText(gtext);
    }
}

// 使用者拖曳偏移向量版本（世界座標偏移 gp_Vec）
static void addDimLineWithOffset(const Handle(Prs3d_Presentation)& prs,
                                  const gp_Pnt& p1, const gp_Pnt& p2,
                                  const gp_Vec& offsetVec,
                                  const Quantity_Color& col,
                                  const QString& label)
{
    // 延伸線必須垂直於 p1p2 連線：將 offsetVec 投影到 p1p2 的法向量上
    gp_Vec along(p1, p2);
    if (along.Magnitude() < Precision::Confusion()) {
        gp_Pnt d1 = p1.Translated(offsetVec);
        gp_Pnt d2 = p2.Translated(offsetVec);
        addDimLineExplicit(prs, p1, p2, d1, d2, col, label);
        return;
    }
    along.Normalize();
    gp_Vec up(0, 0, 1);
    gp_Vec perp = along.Crossed(up);
    perp.Normalize();
    double projDist = offsetVec.Dot(perp);
    if (std::abs(projDist) < Precision::Confusion()) projDist = 8.0;
    gp_Pnt d1 = p1.Translated(perp * projDist);
    gp_Pnt d2 = p2.Translated(perp * projDist);
    addDimLineExplicit(prs, p1, p2, d1, d2, col, label);
}

void AIS_DimensionLine::drawLinearDimension(const Handle(Prs3d_Presentation)& prs) {
    gp_Pnt p1, p2;
    if (!getRefPoints(p1, p2)) return;
    // 若使用者已拖曳設定偏移，用 XY 偏移向量；否則用預設垂直偏移
    double offDist = m_offsetDist;
    if (m_dimOffsetX != 0.0 || m_dimOffsetY != 0.0) {
        // 將草圖平面偏移轉為世界座標偏移向量
        gp_Pnt op(m_dimOffsetX, m_dimOffsetY, 0.0);
        op.Transform(m_sketchToWorld);
        gp_Pnt orig(0.0, 0.0, 0.0);
        orig.Transform(m_sketchToWorld);
        gp_Vec offsetVec(orig, op);
        addDimLineWithOffset(prs, p1, p2, offsetVec,
                             dimColor(m_constraint.driving, m_status),
                             labelText());
        return;
    }
    addDimLine(prs, p1, p2, offDist,
               dimColor(m_constraint.driving, m_status),
               labelText());
}

void AIS_DimensionLine::drawHorizontalDim(const Handle(Prs3d_Presentation)& prs) {
    gp_Pnt p1, p2;
    if (!getRefPoints(p1, p2)) return;

    // 水平距離：尺寸線平行草圖 X 軸（草圖 Y 固定）
    // m_dimOffsetY：尺寸線草圖 Y 相對 AB 中點 Y 的偏移（與 overlay 同基準）
    double rawOffset = (m_dimOffsetY != 0.0) ? m_dimOffsetY : m_offsetDist;

    // 草圖座標系基向量（世界座標）
    gp_Pnt skO(0.0, 0.0, 0.0); skO.Transform(m_sketchToWorld);
    gp_Pnt skX(1.0, 0.0, 0.0); skX.Transform(m_sketchToWorld);
    gp_Pnt skY(0.0, 1.0, 0.0); skY.Transform(m_sketchToWorld);
    gp_Vec xAxis(skO, skX);
    gp_Vec yAxis(skO, skY);

    // 反求 p1/p2 的草圖座標
    gp_Vec v1(skO, p1), v2(skO, p2);
    double p1x = v1.Dot(xAxis), p1y = v1.Dot(yAxis);
    double p2x = v2.Dot(xAxis), p2y = v2.Dot(yAxis);

    // 尺寸線 Y = AB 中點 Y + rawOffset（與 CadView/overlay 的 offset 基準一致）
    double abMidY = (p1y + p2y) * 0.5;
    double dimY   = abMidY + rawOffset;

    gp_Pnt d1(skO.XYZ() + xAxis.XYZ() * p1x + yAxis.XYZ() * dimY);
    gp_Pnt d2(skO.XYZ() + xAxis.XYZ() * p2x + yAxis.XYZ() * dimY);
    // 延伸線：p1→d1 長 |p1y - dimY|，p2→d2 長 |p2y - dimY|（兩條不等長）

    addDimLineExplicit(prs, p1, p2, d1, d2,
                       dimColor(m_constraint.driving, m_status),
                       labelText());
}

void AIS_DimensionLine::drawVerticalDim(const Handle(Prs3d_Presentation)& prs) {
    gp_Pnt p1, p2;
    if (!getRefPoints(p1, p2)) return;

    // 垂直距離：尺寸線平行草圖 Y 軸（草圖 X 固定）
    double rawOffset = (m_dimOffsetX != 0.0) ? m_dimOffsetX : m_offsetDist;

    gp_Pnt skO(0.0, 0.0, 0.0); skO.Transform(m_sketchToWorld);
    gp_Pnt skX(1.0, 0.0, 0.0); skX.Transform(m_sketchToWorld);
    gp_Pnt skY(0.0, 1.0, 0.0); skY.Transform(m_sketchToWorld);
    gp_Vec xAxis(skO, skX);
    gp_Vec yAxis(skO, skY);

    gp_Vec v1(skO, p1), v2(skO, p2);
    double p1x = v1.Dot(xAxis), p1y = v1.Dot(yAxis);
    double p2x = v2.Dot(xAxis), p2y = v2.Dot(yAxis);

    // 尺寸線 X = AB 中點 X + rawOffset（與 CadView/overlay 的 offset 基準一致）
    double abMidX = (p1x + p2x) * 0.5;
    double dimX   = abMidX + rawOffset;

    gp_Pnt d1(skO.XYZ() + xAxis.XYZ() * dimX + yAxis.XYZ() * p1y);
    gp_Pnt d2(skO.XYZ() + xAxis.XYZ() * dimX + yAxis.XYZ() * p2y);
    // 延伸線：p1→d1 長 |p1x - dimX|，p2→d2 長 |p2x - dimX|（兩條不等長）

    addDimLineExplicit(prs, p1, p2, d1, d2,
                       dimColor(m_constraint.driving, m_status),
                       labelText());
}

void AIS_DimensionLine::drawRadiusDimension(const Handle(Prs3d_Presentation)& prs) {
    // 半徑：從圓心到圓周（方向由 m_dimOffsetX/Y 決定）
    if (m_geoms.isEmpty()) return;
    auto* circ = dynamic_cast<const SketchCircle*>(m_geoms[0]);
    if (!circ) return;

    double r = m_constraint.value;

    // 方向（草圖平面內），優先用 offset，否則草圖 X 軸
    gp_Vec sk_dir(1.0, 0.0, 0.0);
    if (m_dimOffsetX != 0.0 || m_dimOffsetY != 0.0) {
        gp_Vec off(m_dimOffsetX, m_dimOffsetY, 0.0);
        if (off.Magnitude() > Precision::Confusion())
            sk_dir = off.Normalized();
    }

    gp_Pnt sk_ctr(circ->center.x(), circ->center.y(), 0.0);
    gp_Pnt sk_edge(sk_ctr.X() + sk_dir.X() * r,
                   sk_ctr.Y() + sk_dir.Y() * r, 0.0);
    sk_ctr.Transform(m_sketchToWorld);
    sk_edge.Transform(m_sketchToWorld);

    addDimLine(prs, sk_ctr, sk_edge, 0.0,
               dimColor(m_constraint.driving, m_status),
               "R " + labelText());
}

void AIS_DimensionLine::drawAngleDim(const Handle(Prs3d_Presentation)& prs) {
    // 角度：簡化為標籤顯示
    gp_Pnt p1, p2;
    if (!getRefPoints(p1, p2)) return;
    addDimLine(prs, p1, p2, m_offsetDist,
               dimColor(m_constraint.driving, m_status),
               labelText() + "°");
}

// ── General Dimension 新增繪製函式 ────────────────────────────────────────

void AIS_DimensionLine::drawLengthDimension(const Handle(Prs3d_Presentation)& prs) {
    // 線段長度：端點必定取 refs[0] 的 Start/End（不依中心計算）
    if (m_geoms.isEmpty()) return;
    auto* g = m_geoms[0];
    if (!g || g->points.size() < 2) {
        drawLinearDimension(prs);
        return;
    }
    gp_Pnt p1(g->points[0].x(), g->points[0].y(), 0.0);
    gp_Pnt p2(g->points[1].x(), g->points[1].y(), 0.0);
    p1.Transform(m_sketchToWorld);
    p2.Transform(m_sketchToWorld);

    // ★ 與 drawLinearDimension 相同：優先使用使用者點擊確認的偏移（m_dimOffsetX/Y）
    if (m_dimOffsetX != 0.0 || m_dimOffsetY != 0.0) {
        gp_Pnt op(m_dimOffsetX, m_dimOffsetY, 0.0);
        op.Transform(m_sketchToWorld);
        gp_Pnt orig(0.0, 0.0, 0.0);
        orig.Transform(m_sketchToWorld);
        gp_Vec offsetVec(orig, op);
        addDimLineWithOffset(prs, p1, p2, offsetVec,
                             dimColor(m_constraint.driving, m_status),
                             labelText());
        return;
    }
    addDimLine(prs, p1, p2, m_offsetDist,
               dimColor(m_constraint.driving, m_status),
               labelText());
}

void AIS_DimensionLine::drawDiameterDimension(const Handle(Prs3d_Presentation)& prs) {
    // m_dimOffsetX/Y = commit 時 (mouse - circCenter)（草圖座標）
    // 幾何（與 rubberband DimPreviewOverlay 完全一致）：
    //   dir    = normalize(offset)         ← 延伸線方向（圓心→滑鼠）
    //   perp   = (-dir.y, dir.x)           ← 尺寸線方向（垂直延伸線）
    //   A/B    = center ± perp*r           ← 延伸線起點（圓周上，沿 perp）
    //   dimMid = center + offset           ← 尺寸線中點（= commit 時滑鼠）
    //   dA/dB  = dimMid ± perp*r          ← 尺寸線端點
    //   Style  = addDimLineExplicit（與 FixedDistance 相同）
    if (m_geoms.isEmpty()) return;
    auto* circ = dynamic_cast<const SketchCircle*>(m_geoms[0]);
    if (!circ) return;

    double r = m_constraint.value / 2.0;

    // ── 草圖座標計算 ─────────────────────────────────────────────────────────
    gp_Vec2d sk_off(m_dimOffsetX, m_dimOffsetY);
    gp_Vec2d sk_dir(1.0, 0.0);
    if (sk_off.Magnitude() > Precision::Confusion())
        sk_dir = sk_off.Normalized();
    gp_Vec2d sk_perp(-sk_dir.Y(), sk_dir.X());

    gp_Pnt2d sk_ctr2(circ->center.x(), circ->center.y());
    gp_Pnt2d sk_A  (sk_ctr2.X() - sk_perp.X() * r, sk_ctr2.Y() - sk_perp.Y() * r);
    gp_Pnt2d sk_B  (sk_ctr2.X() + sk_perp.X() * r, sk_ctr2.Y() + sk_perp.Y() * r);
    gp_Pnt2d sk_mid(sk_ctr2.X() + sk_off.X(),       sk_ctr2.Y() + sk_off.Y());
    gp_Pnt2d sk_dA (sk_mid.X() - sk_perp.X() * r,   sk_mid.Y() - sk_perp.Y() * r);
    gp_Pnt2d sk_dB (sk_mid.X() + sk_perp.X() * r,   sk_mid.Y() + sk_perp.Y() * r);

    // ── 轉世界座標 ───────────────────────────────────────────────────────────
    auto toW = [&](const gp_Pnt2d& p) {
        gp_Pnt w(p.X(), p.Y(), 0.0);
        w.Transform(m_sketchToWorld);
        return w;
    };
    gp_Pnt wA  = toW(sk_A);  gp_Pnt wB  = toW(sk_B);
    gp_Pnt wdA = toW(sk_dA); gp_Pnt wdB = toW(sk_dB);

    // ── 用 addDimLineExplicit 繪製（與 FixedDistance 完全相同的 style）────────
    Quantity_Color col = dimColor(m_constraint.driving, m_status);
    addDimLineExplicit(prs, wA, wB, wdA, wdB, col, labelText());
}

void AIS_DimensionLine::drawArcLengthDimension(const Handle(Prs3d_Presentation)& prs) {
    // 弧長：以同心弧（半徑略大）為尺寸線，標籤加 ~ 前綴
    gp_Pnt ctr, dummy;
    if (!getRefPoints(ctr, dummy)) return;
    // 簡化：用線性尺寸線顯示（弧幾何複雜，實際實作可再改進）
    if (m_geoms.isEmpty()) return;
    auto* g = m_geoms[0];
    if (!g || g->points.size() < 2) return;
    gp_Pnt p1(g->points.first().x(), g->points.first().y(), 0.0);
    gp_Pnt p2(g->points.last().x(),  g->points.last().y(),  0.0);
    p1.Transform(m_sketchToWorld);
    p2.Transform(m_sketchToWorld);
    addDimLine(prs, p1, p2, m_offsetDist,
               dimColor(m_constraint.driving, m_status),
               "~" + labelText());
}

void AIS_DimensionLine::drawCoordinateDimension(const Handle(Prs3d_Presentation)& prs) {
    // 座標尺寸：從點畫兩條引線（水平 X，垂直 Y），各自加標籤
    gp_Pnt pt, dummy;
    if (!getRefPoints(pt, dummy)) return;
    Quantity_Color col = dimColor(m_constraint.driving, m_status);
    gp_Pnt origin(0.0, 0.0, pt.Z());
    // 水平引線（X 軸方向）
    gp_Pnt px(m_constraint.value, pt.Y(), pt.Z());
    addDimLine(prs, pt, px, 0.0, col,
               QString("X=%1").arg(m_constraint.value, 0, 'f', 2));
    // 垂直引線（Y 軸方向）
    gp_Pnt py(pt.X(), m_constraint.value2, pt.Z());
    addDimLine(prs, pt, py, 0.0, col,
               QString("Y=%1").arg(m_constraint.value2, 0, 'f', 2));
}

// ─────────────────────────────────────────────────────────────────────────────
// labelPosition3D — 計算數值標籤的世界座標中心
// 與各 draw*Dimension 中 mid 的計算邏輯保持一致
// ─────────────────────────────────────────────────────────────────────────────

gp_Pnt AIS_DimensionLine::labelPosition3D() const
{
    // 特殊情況：半徑 / 直徑的標籤位置
    if (m_constraint.type == ConstraintType::FixedRadius) {
        if (!m_geoms.isEmpty()) {
            auto* circ = dynamic_cast<const SketchCircle*>(m_geoms[0]);
            if (circ) {
                double r = m_constraint.value;
                gp_Vec2d sk_dir(1.0, 0.0);
                if (m_dimOffsetX != 0.0 || m_dimOffsetY != 0.0) {
                    gp_Vec2d off(m_dimOffsetX, m_dimOffsetY);
                    if (off.Magnitude() > Precision::Confusion())
                        sk_dir = off.Normalized();
                }
                // 標籤在圓心到邊的中點
                gp_Pnt sk_mid(circ->center.x() + sk_dir.X() * r * 0.5,
                              circ->center.y() + sk_dir.Y() * r * 0.5, 0.0);
                sk_mid.Transform(m_sketchToWorld);
                return sk_mid;
            }
        }
    }

    if (m_constraint.type == ConstraintType::FixedDiameter) {
        if (!m_geoms.isEmpty()) {
            auto* circ = dynamic_cast<const SketchCircle*>(m_geoms[0]);
            if (circ) {
                // 標籤在 dimMid（= center + offset）轉世界
                gp_Pnt sk_mid(circ->center.x() + m_dimOffsetX,
                              circ->center.y() + m_dimOffsetY, 0.0);
                sk_mid.Transform(m_sketchToWorld);
                return sk_mid;
            }
        }
    }

    // 通用：算出尺寸線兩端點 d1/d2，標籤在中點
    gp_Pnt p1, p2;
    if (!getRefPoints(p1, p2)) return gp_Pnt(0, 0, 0);

    const ConstraintType ct = m_constraint.type;
    if (ct == ConstraintType::FixedX || ct == ConstraintType::FixedHorizDist) {
        // 水平尺寸線：d1/d2 的 Y = abMidY + rawOffset
        double rawOffset = (m_dimOffsetY != 0.0) ? m_dimOffsetY : m_offsetDist;
        gp_Pnt skO(0, 0, 0); skO.Transform(m_sketchToWorld);
        gp_Pnt skX(1, 0, 0); skX.Transform(m_sketchToWorld);
        gp_Pnt skY(0, 1, 0); skY.Transform(m_sketchToWorld);
        gp_Vec xAxis(skO, skX), yAxis(skO, skY);
        gp_Vec v1(skO, p1), v2(skO, p2);
        double p1x = v1.Dot(xAxis), p2x = v2.Dot(xAxis);
        double p1y = v1.Dot(yAxis), p2y = v2.Dot(yAxis);
        double dimY = (p1y + p2y) * 0.5 + rawOffset;
        gp_Pnt d1(skO.XYZ() + xAxis.XYZ() * p1x + yAxis.XYZ() * dimY);
        gp_Pnt d2(skO.XYZ() + xAxis.XYZ() * p2x + yAxis.XYZ() * dimY);
        return gp_Pnt((d1.X()+d2.X())*0.5, (d1.Y()+d2.Y())*0.5, (d1.Z()+d2.Z())*0.5);
    }

    if (ct == ConstraintType::FixedY || ct == ConstraintType::FixedVertDist) {
        // 垂直尺寸線：d1/d2 的 X = abMidX + rawOffset
        double rawOffset = (m_dimOffsetX != 0.0) ? m_dimOffsetX : m_offsetDist;
        gp_Pnt skO(0, 0, 0); skO.Transform(m_sketchToWorld);
        gp_Pnt skX(1, 0, 0); skX.Transform(m_sketchToWorld);
        gp_Pnt skY(0, 1, 0); skY.Transform(m_sketchToWorld);
        gp_Vec xAxis(skO, skX), yAxis(skO, skY);
        gp_Vec v1(skO, p1), v2(skO, p2);
        double p1x = v1.Dot(xAxis), p2x = v2.Dot(xAxis);
        double p1y = v1.Dot(yAxis), p2y = v2.Dot(yAxis);
        double dimX = (p1x + p2x) * 0.5 + rawOffset;
        gp_Pnt d1(skO.XYZ() + xAxis.XYZ() * dimX + yAxis.XYZ() * p1y);
        gp_Pnt d2(skO.XYZ() + xAxis.XYZ() * dimX + yAxis.XYZ() * p2y);
        return gp_Pnt((d1.X()+d2.X())*0.5, (d1.Y()+d2.Y())*0.5, (d1.Z()+d2.Z())*0.5);
    }

    // 線性（FixedDistance、FixedLength、FixedAngleDim、FixedArcLength）：
    // 尺寸線中點 = mid(p1,p2) + perp * offset
    if (m_dimOffsetX != 0.0 || m_dimOffsetY != 0.0) {
        gp_Pnt op(m_dimOffsetX, m_dimOffsetY, 0.0);
        op.Transform(m_sketchToWorld);
        gp_Pnt orig(0.0, 0.0, 0.0);
        orig.Transform(m_sketchToWorld);
        gp_Vec offsetVec(orig, op);

        gp_Vec along(p1, p2);
        if (along.Magnitude() > Precision::Confusion()) {
            along.Normalize();
            gp_Vec perp = along.Crossed(gp_Vec(0, 0, 1));
            if (perp.Magnitude() > Precision::Confusion()) perp.Normalize();
            double projDist = offsetVec.Dot(perp);
            if (std::abs(projDist) < Precision::Confusion()) projDist = 8.0;
            gp_Pnt d1 = p1.Translated(perp * projDist);
            gp_Pnt d2 = p2.Translated(perp * projDist);
            return gp_Pnt((d1.X()+d2.X())*0.5, (d1.Y()+d2.Y())*0.5, (d1.Z()+d2.Z())*0.5);
        }
    }

    // fallback：預設 perp 偏移
    gp_Vec along(p1, p2);
    if (along.Magnitude() < Precision::Confusion())
        return gp_Pnt((p1.X()+p2.X())*0.5, (p1.Y()+p2.Y())*0.5, (p1.Z()+p2.Z())*0.5);
    along.Normalize();
    gp_Vec perp = along.Crossed(gp_Vec(0, 0, 1));
    if (perp.Magnitude() < Precision::Confusion()) perp = gp_Vec(0, 1, 0);
    perp.Normalize();
    gp_Pnt d1 = p1.Translated(perp * m_offsetDist);
    gp_Pnt d2 = p2.Translated(perp * m_offsetDist);
    return gp_Pnt((d1.X()+d2.X())*0.5, (d1.Y()+d2.Y())*0.5, (d1.Z()+d2.Z())*0.5);
}

// ─────────────────────────────────────────────────────────────────────────────
// ComputeSelection — 只對數值標籤建立 sensitive region
// 尺寸線本體、延伸線不可 hover / 選取，避免覆蓋幾何元素
// ─────────────────────────────────────────────────────────────────────────────

void AIS_DimensionLine::ComputeSelection(
        const Handle(SelectMgr_Selection)& sel,
        const Standard_Integer /*mode*/)
{
    // 計算標籤中心世界座標
    gp_Pnt labelPt = labelPosition3D();

    // 建立以標籤為中心的小方框（±labelHalfSize mm），僅此區域可 hover / 選取
    constexpr double labelHalfSize = 8.0;  // mm，可視字高調整
    Bnd_Box box;
    box.Add(gp_Pnt(labelPt.X() - labelHalfSize,
                   labelPt.Y() - labelHalfSize,
                   labelPt.Z() - labelHalfSize));
    box.Add(gp_Pnt(labelPt.X() + labelHalfSize,
                   labelPt.Y() + labelHalfSize,
                   labelPt.Z() + labelHalfSize));

    Handle(SelectMgr_EntityOwner) owner =
        new SelectMgr_EntityOwner(this, 5);
    Handle(Select3D_SensitiveBox) sens =
        new Select3D_SensitiveBox(owner, box);
    sel->Add(sens);
}


// ─────────────────────────────────────────────────────────────────────────────
// Phase 3B：尺寸線拖曳支援
// ─────────────────────────────────────────────────────────────────────────────

void AIS_DimensionLine::setDimLineOffset(double offsetX, double offsetY)
{
    m_dimOffsetX = offsetX;
    m_dimOffsetY = offsetY;
}

gp_Pnt AIS_DimensionLine::dimLineAnchorPoint3D() const
{
    return labelPosition3D();
}

} // namespace aicad::cad