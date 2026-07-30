#include "DimPreviewOverlay.h"
#include "cad/Sketch.h"
#include "cad/sketch/SketchConstraint.h"

#include <QDebug>
#include <cmath>

#include <Graphic3d_ArrayOfPolylines.hxx>
#include <Graphic3d_ArrayOfSegments.hxx>
#include <Graphic3d_Group.hxx>
#include <Graphic3d_AspectLine3d.hxx>
#include <Graphic3d_Text.hxx>
#include <TCollection_ExtendedString.hxx>
#include <Graphic3d_AspectText3d.hxx>
#include <Prs3d_Presentation.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Quantity_Color.hxx>
#include <Aspect_TypeOfLine.hxx>
#include <Graphic3d_DisplayPriority.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <Precision.hxx>

namespace aicad::view {

using CT = cad::ConstraintType;
using DM = cad::DistanceMode;

// ─────────────────────────────────────────────────────────────────────────────
DimPreviewOverlay::DimPreviewOverlay(QObject* parent)
    : QObject(parent)
{}

DimPreviewOverlay::~DimPreviewOverlay()
{
    clearPrs();
}

void DimPreviewOverlay::setContext(const Handle(AIS_InteractiveContext)& ctx)
{
    m_context = ctx;
}

void DimPreviewOverlay::setSketch(cad::Sketch* sk)
{
    m_sketch = sk;
}

// ─────────────────────────────────────────────────────────────────────────────
void DimPreviewOverlay::setPreview(const PreviewInfo& info)
{
    m_info = info;
    if (m_info.valid) rebuild();
}

void DimPreviewOverlay::clearPreview()
{
    m_info.valid = false;
    clearPrs();
}

void DimPreviewOverlay::setMousePlanePt(const QVector2D& pt)
{
    m_mouse = pt;
    if (m_info.valid) rebuild();
}

// ─────────────────────────────────────────────────────────────────────────────
void DimPreviewOverlay::clearPrs()
{
    if (!m_prs.IsNull()) {
        m_prs->Clear();
        m_prs->Erase();
        m_prs.Nullify();
    }
    if (!m_context.IsNull())
        m_context->UpdateCurrentViewer();
}

// ─────────────────────────────────────────────────────────────────────────────
gp_Pnt DimPreviewOverlay::toWorld(const QVector2D& pt) const
{
    if (!m_sketch || !m_sketch->plane()) return gp_Pnt(pt.x(), pt.y(), 0);
    // Sketch::toWorld() is private; replicate via plane() like RubberBand::planeToWorld()
    auto* pl = m_sketch->plane();
    QVector3D w = pl->origin() + pl->xAxis() * pt.x() + pl->yAxis() * pt.y();
    return gp_Pnt(w.x(), w.y(), w.z());
}

// ─────────────────────────────────────────────────────────────────────────────
// helpers：在 Graphic3d_Group 中加入一條線段
void DimPreviewOverlay::addLine(const Handle(Graphic3d_Group)& grp,
                                const gp_Pnt& a, const gp_Pnt& b)
{
    Handle(Graphic3d_ArrayOfSegments) seg = new Graphic3d_ArrayOfSegments(2);
    seg->AddVertex(a);
    seg->AddVertex(b);
    grp->AddPrimitiveArray(seg);
}

// 在 tip 處加箭頭，dir 為箭頭方向（世界座標），size 為箭頭半長（mm）
void DimPreviewOverlay::addArrow3D(const Handle(Graphic3d_Group)& grp,
                                   const gp_Pnt& tip, const gp_Vec& dir, double size)
{
    gp_Vec u = dir;
    if (u.Magnitude() < Precision::Confusion()) return;
    u.Normalize();

    // 在草圖平面內求一個垂直 u 的向量
    // 用草圖法向量 n 叉 u 取垂直
    gp_Vec n(0, 0, 1);
    if (m_sketch && m_sketch->plane()) {
        auto* pl = m_sketch->plane();
        QVector3D nq = pl->normal();
        n = gp_Vec(nq.x(), nq.y(), nq.z());
    }
    gp_Vec perp = n.Crossed(u);
    if (perp.Magnitude() < Precision::Confusion()) {
        // fallback
        perp = gp_Vec(u.Y(), -u.X(), 0);
    }
    perp.Normalize();

    double len = size * 2.5;
    gp_Pnt base(tip.X() - u.X()*len, tip.Y() - u.Y()*len, tip.Z() - u.Z()*len);
    gp_Pnt w1(base.X() + perp.X()*size, base.Y() + perp.Y()*size, base.Z() + perp.Z()*size);
    gp_Pnt w2(base.X() - perp.X()*size, base.Y() - perp.Y()*size, base.Z() - perp.Z()*size);

    addLine(grp, tip, w1);
    addLine(grp, tip, w2);
}

// ─────────────────────────────────────────────────────────────────────────────
// 主要繪製：重建 OCCT Presentation
// ─────────────────────────────────────────────────────────────────────────────
// GDIM v2 Phase 5：自動吸附偏移量（見 DimPreviewOverlay.h 的說明）
float DimPreviewOverlay::snapOffset(float rawOffset) const
{
    if (!m_sketch) return rawOffset;

    // 容許誤差：以 rawOffset 量級的固定比例估計「大約幾個像素」，
    // 沒有 view 縮放資訊，只能取一個平面單位下的合理值（見 .h 說明的限制）
    constexpr float kSnapToleranceUnits = 3.0f;

    float best = rawOffset;
    float bestDelta = kSnapToleranceUnits;

    for (const auto& c : m_sketch->constraints()) {
        if (c.dimLineOffsetX == 0.0 && c.dimLineOffsetY == 0.0) continue;
        float mag = static_cast<float>(std::sqrt(
            c.dimLineOffsetX * c.dimLineOffsetX +
            c.dimLineOffsetY * c.dimLineOffsetY));
        float delta = std::abs(mag - std::abs(rawOffset));
        if (delta < bestDelta) {
            bestDelta = delta;
            best = mag;
        }
    }
    return best;
}

void DimPreviewOverlay::rebuild()
{
    if (m_context.IsNull() || !m_sketch) return;

    // 清除舊 presentation
    clearPrs();

    // 取得兩端點的草圖座標
    if (m_info.refs.size() < 2 &&
        m_info.type != CT::FixedLength &&
        m_info.type != CT::FixedDiameter &&
        m_info.type != CT::FixedRadius  &&
        m_info.type != CT::FixedArcLength &&
        m_info.type != CT::FixedAngleDim &&
        m_info.type != CT::FixedAngle   &&
        m_info.type != CT::CoordinateDim)
        return;

    // ── 計算幾何 ─────────────────────────────────────────────────────────────
    QVector2D A, B;

    if (m_info.type == CT::FixedLength) {
        // 單線段：取 start/end
        if (m_info.refs.isEmpty()) return;
        auto* geom = m_sketch->findGeometry(m_info.refs[0].geomUuid);
        auto* ln = dynamic_cast<const cad::SketchLine*>(geom);
        if (!ln) return;
        A = ln->start;
        B = ln->end;
    } else if (m_info.type == CT::FixedDiameter || m_info.type == CT::FixedRadius) {
        // 圓/弧幾何
        if (m_info.refs.isEmpty()) return;
        auto* geom = m_sketch->findGeometry(m_info.refs[0].geomUuid);
        QVector2D center; float r = 0;
        if (auto* c = dynamic_cast<const cad::SketchCircle*>(geom)) {
            center = c->center; r = c->radius;
        } else if (dynamic_cast<const cad::SketchArc*>(geom)) {
            cad::GeomRef cr(m_info.refs[0].geomUuid, cad::GeomHandle::Center);
            cad::GeomRef sr(m_info.refs[0].geomUuid, cad::GeomHandle::Start);
            center = cr.resolvePosition(m_sketch);
            r = (sr.resolvePosition(m_sketch) - center).length();
        }
        // dir  = 圓心→滑鼠（延伸線方向）
        // perp = dir 逆時針 90°（尺寸線方向）
        QVector2D dir = m_mouse - center;
        if (dir.length() < 1e-3f) dir = QVector2D(1, 0);
        dir.normalize();
        QVector2D perp(-dir.y(), dir.x());
        // A/B = 圓周上沿 perp 方向的兩端（延伸線起點，與尺寸線同方向）
        // FixedRadius: A = center（只畫半徑線，從圓心）
        A = (m_info.type == CT::FixedDiameter) ? center - perp * r : center;
        B = center + perp * r;
        // 把 dir/perp/r/center 存入臨時變數供 dA/dB 段使用
        m_dimDir  = dir;
        m_dimPerp = perp;
        m_dimR    = r;
        m_dimCenter = center;
    } else {
        // FixedDistance / HorizDist / VertDist / PointToLine / LineToLine
        if (m_info.refs.size() < 2) return;
        A = m_info.refs[0].resolvePosition(m_sketch);

        if (m_info.distMode == cad::DistanceMode::PointToLine) {
            // refs[1] = 整條線 WholeGeom → B = 投影點
            auto* geomB = m_sketch->findGeometry(m_info.refs[1].geomUuid);
            auto* ln    = dynamic_cast<const cad::SketchLine*>(geomB);
            if (!ln) return;
            // 投影公式：B = A + (A→線 的垂足)
            QVector2D AB = ln->end - ln->start;
            float len2   = QVector2D::dotProduct(AB, AB);
            float t      = (len2 > 1e-10f)
                           ? QVector2D::dotProduct(A - ln->start, AB) / len2
                           : 0.0f;
            B = ln->start + AB * t;
        } else if (m_info.distMode == cad::DistanceMode::LineToLine) {
            // refs[0]=線A, refs[1]=線B → A=線A起點, B=線A起點投影到線B
            auto* geomA = m_sketch->findGeometry(m_info.refs[0].geomUuid);
            auto* geomB = m_sketch->findGeometry(m_info.refs[1].geomUuid);
            auto* lnA   = dynamic_cast<const cad::SketchLine*>(geomA);
            auto* lnB   = dynamic_cast<const cad::SketchLine*>(geomB);
            if (!lnA || !lnB) return;
            A = lnA->start;
            QVector2D AB = lnB->end - lnB->start;
            float len2   = QVector2D::dotProduct(AB, AB);
            float t      = (len2 > 1e-10f)
                           ? QVector2D::dotProduct(A - lnB->start, AB) / len2
                           : 0.0f;
            B = lnB->start + AB * t;
        } else {
            B = m_info.refs[1].resolvePosition(m_sketch);
        }
    }

    // ── 計算尺寸線端點 dA/dB（草圖平面 2D）──────────────────────────────────
    QVector2D dA, dB;
    // 直徑/半徑：A/B 已是圓周上的端點，尺寸線就是 A→B，不需要延伸線
    const bool isDiamOrRadius =
        (m_info.type == CT::FixedDiameter || m_info.type == CT::FixedRadius);

    if (isDiamOrRadius) {
        // 正確的直徑標注幾何：
        //   延伸線方向 = dir（圓心→滑鼠），延伸線起點 = A, B（圓周）
        //   尺寸線方向 = perp（垂直 dir），尺寸線中點 = m_mouse
        //   dA = m_mouse - perp * r（尺寸線 A 端）
        //   dB = m_mouse + perp * r（尺寸線 B 端）
        //   延伸線：A→dA, B→dB（沿 dir）
        dA = m_mouse - m_dimPerp * m_dimR;
        dB = m_mouse + m_dimPerp * m_dimR;
    } else if (m_info.type == CT::FixedHorizDist) {
        // 水平距離
        float dimY = m_mouse.y();
        float minY = std::min(A.y(), B.y());
        float maxY = std::max(A.y(), B.y());
        if (dimY >= minY - 5.f && dimY <= maxY + 5.f) {
            float midY = (A.y() + B.y()) * 0.5f;
            dimY = (m_mouse.y() >= midY) ? maxY + 20.f : minY - 20.f;
        }
        dA = QVector2D(A.x(), dimY);
        dB = QVector2D(B.x(), dimY);
    } else if (m_info.type == CT::FixedVertDist) {
        // 垂直距離
        float dimX = m_mouse.x();
        float minX = std::min(A.x(), B.x());
        float maxX = std::max(A.x(), B.x());
        if (dimX >= minX - 5.f && dimX <= maxX + 5.f) {
            float midX = (A.x() + B.x()) * 0.5f;
            dimX = (m_mouse.x() >= midX) ? maxX + 20.f : minX - 20.f;
        }
        dA = QVector2D(dimX, A.y());
        dB = QVector2D(dimX, B.y());
    } else if (m_info.distMode == cad::DistanceMode::PointToLine
               || m_info.distMode == cad::DistanceMode::LineToLine) {
        // PointToLine / LineToLine：尺寸線沿 A→B 方向（垂足方向），
        // 偏移量由滑鼠到 AB 中點的垂直分量決定
        QVector2D ab  = B - A;
        float abLen   = ab.length();
        (void)abLen; // perpendicular direction unused; dA/dB set directly below
        // 尺寸線就是 A→B（垂距線），不再偏移
        dA = A;
        dB = B;
    } else if (m_info.type == CT::FixedX) {
        // FixedX：單點水平引線（點 → X=value 的投影點）
        if (!m_info.refs.isEmpty()) {
            A = m_info.refs[0].resolvePosition(m_sketch);
            B = QVector2D(static_cast<float>(m_info.value), A.y());
        }
        float dimY = m_mouse.y();
        dA = QVector2D(A.x(), dimY);
        dB = QVector2D(B.x(), dimY);
    } else if (m_info.type == CT::FixedY) {
        // FixedY：單點垂直引線（點 → Y=value 的投影點）
        if (!m_info.refs.isEmpty()) {
            A = m_info.refs[0].resolvePosition(m_sketch);
            B = QVector2D(A.x(), static_cast<float>(m_info.value));
        }
        float dimX = m_mouse.x();
        dA = QVector2D(dimX, A.y());
        dB = QVector2D(dimX, B.y());
    } else if (m_info.type == CT::FixedAngleDim || m_info.type == CT::FixedAngle) {
        // 角度：延伸線必須與被標註的兩條線平行——沿各自線方向、從兩線交點
        // （apex）向外延伸，而不是像線性尺寸那樣垂直於兩點連線。
        // 這裡採用與確認後的最終顯示（AIS_DimensionLine::drawAngleDim）相同的
        // 交點/方向計算方式，讓拖曳預覽與實際結果視覺一致。
        bool built = false;
        if (m_info.refs.size() >= 2) {
            auto* geomA = m_sketch->findGeometry(m_info.refs[0].geomUuid);
            auto* geomB = m_sketch->findGeometry(m_info.refs[1].geomUuid);
            auto* lnA = dynamic_cast<const cad::SketchLine*>(geomA);
            auto* lnB = dynamic_cast<const cad::SketchLine*>(geomB);

            if (lnA && lnB) {
                QVector2D dirA = lnA->end - lnA->start;
                QVector2D dirB = lnB->end - lnB->start;
                float lenA = dirA.length();
                float lenB = dirB.length();

                if (lenA > 1e-6f && lenB > 1e-6f) {
                    dirA /= lenA;
                    dirB /= lenB;

                    // 兩條無限延伸線的交點（apex）
                    float cross = dirA.x() * dirB.y() - dirA.y() * dirB.x();
                    QVector2D apex;
                    if (std::abs(cross) < 1e-6f) {
                        // 平行：退化為線 A 中點
                        apex = (lnA->start + lnA->end) * 0.5f;
                    } else {
                        QVector2D ab = lnB->start - lnA->start;
                        float t = (ab.x() * dirB.y() - ab.y() * dirB.x()) / cross;
                        apex = lnA->start + dirA * t;
                    }

                    // 角弧半徑：由滑鼠到 apex 的距離即時決定（拖曳調整大小）
                    float arcR = std::max(15.f, (m_mouse - apex).length());

                    A  = apex;
                    B  = apex;
                    dA = apex + dirA * arcR;
                    dB = apex + dirB * arcR;
                    built = true;
                }
            }
        }
        if (!built) {
            // fallback：找不到有效線幾何時，退化為舊版線性尺寸樣式
            if (m_info.refs.size() >= 2) {
                A = m_info.refs[0].resolvePosition(m_sketch);
                B = m_info.refs[1].resolvePosition(m_sketch);
            }
            QVector2D ab = B - A;
            float abLen = ab.length();
            QVector2D perpDir = (abLen < 1e-4f)
                ? QVector2D(0, 1)
                : QVector2D(-ab.y(), ab.x()) / abLen;
            QVector2D mid = (A + B) * 0.5f;
            float dot = QVector2D::dotProduct(m_mouse - mid, perpDir);
            if (dot < 0) perpDir = -perpDir;
            float offset = snapOffset(std::max(std::abs(dot), 15.f));
            dA = A + perpDir * offset;
            dB = B + perpDir * offset;
        }
    } else if (m_info.type == CT::FixedArcLength) {
        // 弧長：同心弧，此處用線性近似（真弧由 AIS 繪製）
        if (!m_info.refs.isEmpty()) {
            auto* geom = m_sketch->findGeometry(m_info.refs[0].geomUuid);
            if (auto* arc = dynamic_cast<const cad::SketchArc*>(geom)) {
                if (arc->points.size() >= 3) {
                    A = arc->points[0];
                    B = arc->points[1];
                }
            }
        }
        QVector2D ab = B - A;
        float abLen = ab.length();
        QVector2D perpDir = (abLen < 1e-4f)
            ? QVector2D(0, 1)
            : QVector2D(-ab.y(), ab.x()) / abLen;
        QVector2D mid = (A + B) * 0.5f;
        float dot = QVector2D::dotProduct(m_mouse - mid, perpDir);
        if (dot < 0) perpDir = -perpDir;
        float offset = snapOffset(std::max(std::abs(dot), 15.f));
        dA = A + perpDir * offset;
        dB = B + perpDir * offset;
    } else if (m_info.type == CT::CoordinateDim) {
        // 座標尺寸：從點畫兩條引線（水平 + 垂直），dA/dB 用水平線
        if (!m_info.refs.isEmpty()) {
            A = m_info.refs[0].resolvePosition(m_sketch);
        }
        B = QVector2D(static_cast<float>(m_info.value), A.y());
        dA = A;
        dB = B;
    } else {
        // FixedLength / FixedDistance(PointToPoint)
        QVector2D ab = B - A;
        float abLen = ab.length();
        QVector2D perpDir;
        if (abLen < 1e-4f) {
            perpDir = QVector2D(0, 1);
        } else {
            perpDir = QVector2D(-ab.y(), ab.x()) / abLen;
        }
        QVector2D mid = (A + B) * 0.5f;
        float dot = QVector2D::dotProduct(m_mouse - mid, perpDir);
        if (dot < 0) perpDir = -perpDir;
        float offset = std::abs(dot);
        if (offset < 5.f) offset = 20.f;
        offset = snapOffset(offset);
        dA = A + perpDir * offset;
        dB = B + perpDir * offset;
    }

    // ── 建立 OCCT Presentation ────────────────────────────────────────────────
    m_prs = new Prs3d_Presentation(
        m_context->MainPrsMgr()->StructureManager());

    Quantity_Color lineCol(0.0, 0.85, 0.0, Quantity_TOC_RGB);

    // ── 延伸線 + 尺寸線（Style 與 FixedDistance 相同：全實線，單一 polyline）────
    {
        Handle(Graphic3d_AspectLine3d) asp =
            new Graphic3d_AspectLine3d(lineCol, Aspect_TOL_SOLID, 1.5f);
        Handle(Graphic3d_Group) grp = m_prs->NewGroup();
        grp->SetGroupPrimitivesAspect(asp);

        gp_Pnt wA  = toWorld(A);  gp_Pnt wB  = toWorld(B);
        gp_Pnt wdA = toWorld(dA); gp_Pnt wdB = toWorld(dB);

        Handle(Graphic3d_ArrayOfPolylines) lines =
            new Graphic3d_ArrayOfPolylines(6, 3);
        // 延伸線 A→dA
        lines->AddBound(2); lines->AddVertex(wA);  lines->AddVertex(wdA);
        // 延伸線 B→dB
        lines->AddBound(2); lines->AddVertex(wB);  lines->AddVertex(wdB);
        // 尺寸線 dA→dB
        lines->AddBound(2); lines->AddVertex(wdA); lines->AddVertex(wdB);
        grp->AddPrimitiveArray(lines);

        // 箭頭：沿尺寸線方向（dA→dB）
        gp_Vec along(wdA, wdB);
        if (along.Magnitude() > Precision::Confusion()) {
            along.Normalize();
            addArrow3D(grp, wdA, along * -1.0);
            addArrow3D(grp, wdB, along);
        }
    }

    // ── 數值文字（紅色，字高 36，平行尺寸線，居中）──────────────────────────
    // 直徑/半徑：標籤放圓心；其他：放尺寸線中點
    {
        const bool isAngleType =
            (m_info.type == CT::FixedAngleDim || m_info.type == CT::FixedAngle);
        // 角度類型：m_info.value 內部為弧度，顯示時轉換為「度」並加上 ° 符號，
        // 與確認後的最終標籤（AIS_DimensionLine::drawAngleDim）格式一致。
        QString label = isAngleType
            ? QString::number(m_info.value * 180.0 / M_PI, 'f', 2) + QStringLiteral("°")
            : QString::number(m_info.value, 'f', 2);
        // 標籤放在尺寸線中點（dA/dB）：當滑鼠在圓外時尺寸線已平移，標籤跟著走
        QVector2D midDim = (dA + dB) * 0.5f;
        gp_Pnt wMid = toWorld(midDim);

        Handle(Graphic3d_Text) gtext = new Graphic3d_Text(36.0f);
        gtext->SetText(TCollection_ExtendedString(
            label.toUtf8().constData(), Standard_True));
        gtext->SetPosition(wMid);

        // 文字方向：X 軸對齊尺寸線方向
        gp_Vec dimVec(toWorld(dA), toWorld(dB));
        if (dimVec.Magnitude() > Precision::Confusion()) {
            dimVec.Normalize();
            gp_Vec zAxis(0, 0, 1);
            if (m_sketch && m_sketch->plane()) {
                auto* pl = m_sketch->plane();
                QVector3D nq = pl->normal();
                zAxis = gp_Vec(nq.x(), nq.y(), nq.z());
                zAxis.Normalize();
            }
            // 確保 dimVec 和 zAxis 不平行
            gp_Vec side = zAxis.Crossed(dimVec);
            if (side.Magnitude() > Precision::Confusion()) {
                gtext->SetOrientation(gp_Ax2(wMid, gp_Dir(zAxis), gp_Dir(dimVec)));
            }
        }
        gtext->SetHorizontalAlignment(Graphic3d_HTA_CENTER);
        gtext->SetVerticalAlignment(Graphic3d_VTA_CENTER);

        Handle(Graphic3d_AspectText3d) txtAsp = new Graphic3d_AspectText3d();
        txtAsp->SetColor(Quantity_Color(Quantity_NOC_RED));
        Handle(Graphic3d_Group) txtGrp = m_prs->NewGroup();
        txtGrp->SetGroupPrimitivesAspect(txtAsp);
        txtGrp->AddText(gtext);
    }

    // 置頂顯示
    m_prs->SetZLayer(Graphic3d_ZLayerId_Top);
    m_prs->SetDisplayPriority(Graphic3d_DisplayPriority_Topmost);
    m_prs->Display();
    m_context->UpdateCurrentViewer();
}

} // namespace aicad::view