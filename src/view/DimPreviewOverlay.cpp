#include "DimPreviewOverlay.h"
#include "cad/Sketch.h"
#include "cad/sketch/SketchConstraint.h"

#include <QDebug>
#include <algorithm>
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
    // ⚠️ 修正：單點 X/Y 座標標註（CT::FixedX / CT::FixedY）只有 1 個 ref，
    // 先前這份允許清單漏列這兩個型別，導致 refs.size()<2 時一律在此
    // return，永遠到不了下面第 317/326 行真正處理 FixedX/FixedY 的程式碼
    // ——這正是「單點 X/Y/XY 預覽沒有顯示」其中 X、Y 兩種完全沒顯示的
    // 成因（XY 是 CoordinateDim，本來就在清單內，所以只有 X/Y 受影響）。
    if (m_info.refs.size() < 2 &&
        m_info.type != CT::FixedLength &&
        m_info.type != CT::FixedDiameter &&
        m_info.type != CT::FixedRadius  &&
        m_info.type != CT::FixedArcLength &&
        m_info.type != CT::FixedAngleDim &&
        m_info.type != CT::FixedAngle   &&
        m_info.type != CT::FixedX       &&
        m_info.type != CT::FixedY       &&
        m_info.type != CT::CoordinateDim)
        return;

    // ── 計算幾何 ─────────────────────────────────────────────────────────────
    QVector2D A, B;
    // 弧長（FixedArcLength）預覽專用：真正的弧線幾何（圓心/半徑/掃角），
    // 讓下方繪製區塊能畫出真正的同心弧，而不是把 dA/dB 當成直線兩端點
    // （見下方 FixedArcLength 分支與繪製區塊的說明）。
    bool      isArcLengthPreview = false;
    // 角度（FixedAngleDim/FixedAngle）預覽專用：與 isArcLengthPreview 共用
    // 下方同一套「畫同心弧」繪製區塊——角弧的圓心就是兩線交點（apex），
    // 半徑是拖曳中的 arcDimR，這樣預覽跟確認後的最終顯示
    // （AIS_DimensionLine::drawAngleDim）才會是同一種弧線畫法，而不是像先前
    // 那樣退化成一條直的 dA→dB 弦線（使用者回報「預覽的尺寸線應該採用
    // 弧線」正是指這個）。
    bool      isAngleArcPreview = false;
    QVector2D arcCenter;
    double    arcR = 0.0, arcDimR = 0.0, arcAngStart = 0.0, arcAngEnd = 0.0;

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
        // dir  = 圓心→滑鼠（半徑：也是尺寸線本身的方向；直徑：延伸線方向）
        // perp = dir 逆時針 90°（直徑尺寸線方向）
        QVector2D dir = m_mouse - center;
        if (dir.length() < 1e-3f) dir = QVector2D(1, 0);
        dir.normalize();
        QVector2D perp(-dir.y(), dir.x());
        // A/B：
        //   FixedRadius   → 無選單版需求：尺寸線方向＝圓心→滑鼠方向本身
        //                    （不是垂直方向），A=圓心, B=圓線上一點
        //   FixedDiameter → 維持原本「尺寸線垂直於 dir」的樣式不變
        A = (m_info.type == CT::FixedRadius) ? center
            : center - perp * r;
        B = (m_info.type == CT::FixedRadius) ? center + dir * r
                                              : center + perp * r;
        // 把 dir/perp/r/center 存入臨時變數供 dA/dB 段使用
        m_dimDir  = dir;
        m_dimPerp = perp;
        m_dimR    = r;
        m_dimCenter = center;
    } else if (m_info.type == CT::FixedArcLength) {
        // ⚠️ 修正：弧長（FixedArcLength）只有 1 個 ref（單一弧），跟 X/Y/
        // CoordinateDim 一樣，先前這個「計算幾何」的 if-else 鏈沒有專屬
        // 分支，導致一律落入下面 else（FixedDistance 等雙點/雙幾何專用）
        // 的 `if (refs.size()<2) return;`，在真正畫出預覽之前就被擋掉——
        // 這正是「弧長約束的尺寸線沒有預覽」的成因（下面第 430 行附近的
        // FixedArcLength 專屬繪製分支其實邏輯是對的，但根本執行不到）。
        // 這裡不需要真的算 A/B（下面第 430 行附近的分支會自己用
        // m_info.refs[0] 重新解析弧的起點/終點/圓心），只需要放行即可。
        if (m_info.refs.isEmpty()) return;
    } else if (m_info.type == CT::FixedX || m_info.type == CT::FixedY ||
               m_info.type == CT::CoordinateDim) {
        // ⚠️ 修正：這三種都只有 1 個 ref（單一點），下面的 dA/dB 計算段
        // （第 324/333/434 行附近）各自會用 m_info.refs[0] 重新解析出 A/B，
        // 這裡不需要（也不能，因為只有 1 個 ref）重複計算。先前程式碼沒有
        // 這個分支，導致這三種型別全部落入下方 else（FixedDistance 等
        // 雙點/雙幾何專用）的 `if (refs.size()<2) return;`，在真正畫出
        // 預覽之前就被擋掉——這正是「單點 X/Y/XY 預覽沒有顯示」的成因。
        if (m_info.refs.isEmpty()) return;
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
        //
        // 半徑（無選單版需求）：無延線，尺寸線本身就是 A(圓心)→B(圓線上)，
        // 因此 dA/dB 直接等於 A/B，讓下方共用繪製區塊算出的「延伸線
        // A→dA、B→dB」自然退化為零長度（=無延線）。
        if (m_info.type == CT::FixedRadius) {
            dA = A;
            dB = B;
        } else {
            dA = m_mouse - m_dimPerp * m_dimR;
            dB = m_mouse + m_dimPerp * m_dimR;
        }
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
        // FixedX：單點水平引線（點 → 沿水平方向跟隨滑鼠的端點）
        // ⚠️ 修正：先前 B = QVector2D(m_info.value, A.y())，但 m_info.value
        // 就是這個點「目前」的 X 座標本身（= A.x()），所以 B 永遠等於 A，
        // dA/dB 因此永遠退化成同一個點（零長度），完全不會顯示——這正是
        // 「X 預覽不明確」的成因。改成從點沿水平方向、依滑鼠目前 X 位置
        // 決定引線長度，讓預覽真正跟著滑鼠移動。
        if (!m_info.refs.isEmpty())
            A = m_info.refs[0].resolvePosition(m_sketch);
        B = A;  // 單點型別沒有「B」，設成 = A 讓延伸線 B→dB 退化成零長度
        dA = A;
        dB = QVector2D(m_mouse.x(), A.y());
    } else if (m_info.type == CT::FixedY) {
        // FixedY：單點垂直引線（點 → 沿垂直方向跟隨滑鼠的端點）
        // ⚠️ 修正：同上，B 先前恆等於 A，改成沿垂直方向跟隨滑鼠 Y 位置。
        if (!m_info.refs.isEmpty())
            A = m_info.refs[0].resolvePosition(m_sketch);
        B = A;
        dA = A;
        dB = QVector2D(A.x(), m_mouse.y());
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
                    arcDimR   = std::max(15.0, static_cast<double>((m_mouse - apex).length()));
                    arcCenter = apex;

                    // 角弧掃角範圍：兩條「線」（不是射線）在 apex 交叉，
                    // 實際上把平面分成 4 個扇區（±dirA 與 ±dirB 兩兩相鄰
                    // 圍成），對角的兩個扇區角度相同、相鄰的兩個扇區互為
                    // 補角。夾角標註該畫哪一個扇區沒有唯一答案，必須由滑鼠
                    // 落在哪個扇區來決定——不能不管滑鼠位置、永遠只挑
                    // +dirA、+dirB 之間較小的那一段（那樣兩線用不同順序點
                    // 選、或滑鼠實際落在補角那一側時，畫出來的角弧就會跟
                    // 滑鼠位置對不上，也就是「不是臨近滑鼠點下的位置」）。
                    // 做法：算出 4 條射線角度（dirA、-dirA、dirB、-dirB）、
                    // 排序，再找滑鼠方向落在哪兩條相鄰射線之間，那一段就是
                    // 要畫的扇區——相鄰兩射線間的扇區必然 ≤180°，不需要再
                    // 另外判斷「較大/較小」。
                    auto norm2pi = [](double a) {
                        while (a < 0.0)        a += 2.0 * M_PI;
                        while (a >= 2.0 * M_PI) a -= 2.0 * M_PI;
                        return a;
                    };
                    double rays[4] = {
                        norm2pi(std::atan2(static_cast<double>(dirA.y()),  static_cast<double>(dirA.x()))),
                        norm2pi(std::atan2(static_cast<double>(-dirA.y()), static_cast<double>(-dirA.x()))),
                        norm2pi(std::atan2(static_cast<double>(dirB.y()),  static_cast<double>(dirB.x()))),
                        norm2pi(std::atan2(static_cast<double>(-dirB.y()), static_cast<double>(-dirB.x())))
                    };
                    std::sort(std::begin(rays), std::end(rays));

                    QVector2D toMouse = m_mouse - apex;
                    double a0, a1;
                    if (toMouse.lengthSquared() < 1e-8f) {
                        // 滑鼠剛好在 apex 上（尚未有明確方向）：退回兩線
                        // 直接夾角（angA/angB 較小的那一段）當預設值。
                        a0 = rays[0]; a1 = rays[1];
                    } else {
                        double angM = norm2pi(std::atan2(static_cast<double>(toMouse.y()),
                                                          static_cast<double>(toMouse.x())));
                        a0 = rays[3] - 2.0 * M_PI;  // 預設：落在「繞回第一段」的扇區
                        a1 = rays[0];
                        for (int i = 0; i < 3; ++i) {
                            if (angM >= rays[i] && angM < rays[i + 1]) {
                                a0 = rays[i]; a1 = rays[i + 1];
                                break;
                            }
                        }
                    }
                    arcAngStart = a0;
                    arcAngEnd   = a1;

                    A  = apex;
                    B  = apex;
                    dA = QVector2D(static_cast<float>(apex.x() + arcDimR * std::cos(a0)),
                                   static_cast<float>(apex.y() + arcDimR * std::sin(a0)));
                    dB = QVector2D(static_cast<float>(apex.x() + arcDimR * std::cos(a1)),
                                   static_cast<float>(apex.y() + arcDimR * std::sin(a1)));
                    isAngleArcPreview = true;
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
        // 弧長：真正的同心弧（半徑略大於原弧），不是直線近似。
        // ⚠️ 修正：先前這裡用「弦 A-B 的垂直偏移」畫一條直線來近似弧長尺寸線，
        // 使用者反映「有預覽了，但尺寸線不是弧線」——這裡改成跟
        // AIS_DimensionLine::drawArcLengthDimension() 一致的做法：算出圓心、
        // 半徑、掃角，實際畫一段同心弧（見下方繪製區塊的 isArcLengthPreview
        // 特殊分支），而不是走共用的直線 dA→dB 畫法。
        const cad::SketchArc* arcGeomForSweep = nullptr;
        if (!m_info.refs.isEmpty()) {
            auto* geom = m_sketch->findGeometry(m_info.refs[0].geomUuid);
            if (auto* ag = dynamic_cast<const cad::SketchArc*>(geom)) {
                cad::GeomRef sr(m_info.refs[0].geomUuid, cad::GeomHandle::Start);
                cad::GeomRef er(m_info.refs[0].geomUuid, cad::GeomHandle::End);
                cad::GeomRef cr(m_info.refs[0].geomUuid, cad::GeomHandle::Center);
                A = sr.resolvePosition(m_sketch);
                B = er.resolvePosition(m_sketch);
                arcCenter = cr.resolvePosition(m_sketch);
                arcGeomForSweep = ag;
                isArcLengthPreview = true;
            }
        }
        if (isArcLengthPreview) {
            arcR = static_cast<double>((A - arcCenter).length());
            // 同心弧偏移半徑：依滑鼠與圓心的距離決定「往外偏移多少」，
            // 讓使用者可以透過滑鼠遠近直接看到同心弧被推開的距離
            // （與 FixedRadius/FixedDiameter 用滑鼠距離決定偏移量一致）。
            double mouseDist = static_cast<double>((m_mouse - arcCenter).length());
            double extraR = std::max(std::abs(mouseDist - arcR), 5.0);
            arcDimR = arcR + extraR;

            // arcAngStart 固定對應 A 點角度、arcAngEnd 固定對應 B 點角度
            // （這樣延伸線 A→dA、B→dB 才不會畫錯邊/交叉），但兩者之間到底
            // 要走 CCW 還是 CW，必須由「弧實際掃過的那一段」決定——不能像
            // 先前那樣「一律假設 CCW、angEnd 不斷 +2π 直到大於 angStart」，
            // 那個假設在弧本身是走 CW（或掃角超過半圈）時會抓到另一側完全
            // 沒有弧存在的那段，畫出的同心弧因此出現在錯的一邊。
            // 改成：實際從 arc->curve 取樣一個弧上真正的中點，看它落在
            // 「CCW（A→B 遞增角度）」還是「CW（A→B 遞減角度）」這兩段的哪一段，
            // 用那一段當作掃角範圍。
            arcAngStart = std::atan2(static_cast<double>(A.y() - arcCenter.y()),
                                      static_cast<double>(A.x() - arcCenter.x()));
            double angB  = std::atan2(static_cast<double>(B.y() - arcCenter.y()),
                                       static_cast<double>(B.x() - arcCenter.x()));

            double angBccw = angB;
            while (angBccw <= arcAngStart) angBccw += 2.0 * M_PI;

            bool useCcw = true;   // 找不到 curve 可採樣時，退回原本的 CCW 假設
            if (arcGeomForSweep && !arcGeomForSweep->curve.IsNull() && m_sketch && m_sketch->plane()) {
                double midParam = 0.5 * (arcGeomForSweep->curve->FirstParameter() +
                                          arcGeomForSweep->curve->LastParameter());
                gp_Pnt wMid = arcGeomForSweep->curve->Value(midParam);
                auto* pl = m_sketch->plane();
                QVector3D rel = QVector3D(static_cast<float>(wMid.X()),
                                           static_cast<float>(wMid.Y()),
                                           static_cast<float>(wMid.Z())) - pl->origin();
                QVector2D localMid(QVector3D::dotProduct(rel, pl->xAxis()),
                                    QVector3D::dotProduct(rel, pl->yAxis()));
                double midAngle = std::atan2(
                    static_cast<double>(localMid.y() - arcCenter.y()),
                    static_cast<double>(localMid.x() - arcCenter.x()));
                double normMid = midAngle;
                while (normMid < arcAngStart) normMid += 2.0 * M_PI;
                while (normMid >= arcAngStart + 2.0 * M_PI) normMid -= 2.0 * M_PI;
                // CCW 這一段的範圍是 [arcAngStart, angBccw]；中點若落在裡面，
                // 弧就是走 CCW，否則走 CW（另一段）。
                useCcw = (normMid <= angBccw);
            }

            if (useCcw) {
                arcAngEnd = angBccw;
            } else {
                double angBcw = angB;
                while (angBcw >= arcAngStart) angBcw -= 2.0 * M_PI;
                arcAngEnd = angBcw;
            }
            // dA/dB 仍然算出來，供延伸線與標籤定位使用（同心弧的兩端點）
            dA = QVector2D(static_cast<float>(arcCenter.x() + arcDimR * std::cos(arcAngStart)),
                           static_cast<float>(arcCenter.y() + arcDimR * std::sin(arcAngStart)));
            dB = QVector2D(static_cast<float>(arcCenter.x() + arcDimR * std::cos(arcAngEnd)),
                           static_cast<float>(arcCenter.y() + arcDimR * std::sin(arcAngEnd)));
        }
    } else if (m_info.type == CT::CoordinateDim) {
        // 座標尺寸（XY）：點 → 直接跟隨滑鼠位置的對角引線
        // ⚠️ 修正：先前 B = QVector2D(m_info.value, A.y())，m_info.value
        // 是這個點目前的 X 座標本身（= A.x()），導致 B 恆等於 A，dA=dB=A，
        // 完全零長度——這正是「XY 預覽沒有顯示」的成因（比 FixedX/FixedY
        // 更嚴重：那兩個好歹還有 dimY/dimX 撐出一點點，這裡是徹底重合）。
        // 改成直接以滑鼠目前位置為對角引線的終點，讓預覽正確跟著滑鼠移動。
        if (!m_info.refs.isEmpty())
            A = m_info.refs[0].resolvePosition(m_sketch);
        B = A;  // 單點型別沒有「B」，設成 = A 讓延伸線 B→dB 退化成零長度
        dA = A;
        dB = m_mouse;
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
    if (isArcLengthPreview || isAngleArcPreview) {
        // ⚠️ 弧長／角度共用同一段畫法：尺寸線本身必須是一段真正的弧，不能
        // 沿用下面共用的「dA→dB 直線」畫法——那是給線性尺寸用的，角度／
        // 弧長用會變成「有預覽了，但尺寸線不是弧線」。做法與
        // AIS_DimensionLine::drawArcLengthDimension() 一致：取樣同心弧上的
        // 點連成折線，延伸線則是原弧端點→同心弧端點的放射線段。
        Handle(Graphic3d_AspectLine3d) asp =
            new Graphic3d_AspectLine3d(lineCol, Aspect_TOL_SOLID, 1.5f);
        Handle(Graphic3d_Group) grp = m_prs->NewGroup();
        grp->SetGroupPrimitivesAspect(asp);

        const int nSeg = 30;
        Handle(Graphic3d_ArrayOfPolylines) arcLine =
            new Graphic3d_ArrayOfPolylines(nSeg + 1, 1);
        arcLine->AddBound(nSeg + 1);
        for (int i = 0; i <= nSeg; ++i) {
            double a = arcAngStart + (arcAngEnd - arcAngStart) * i / nSeg;
            gp_Pnt p(arcCenter.x() + arcDimR * std::cos(a),
                     arcCenter.y() + arcDimR * std::sin(a), 0.0);
            arcLine->AddVertex(toWorld(QVector2D(static_cast<float>(p.X()),
                                                  static_cast<float>(p.Y()))));
        }
        grp->AddPrimitiveArray(arcLine);

        // 延伸線：原弧端點（A/B，半徑 arcR）→ 同心弧端點（dA/dB，半徑 arcDimR）
        Handle(Graphic3d_ArrayOfPolylines) extLines =
            new Graphic3d_ArrayOfPolylines(4, 2);
        extLines->AddBound(2); extLines->AddVertex(toWorld(A));  extLines->AddVertex(toWorld(dA));
        extLines->AddBound(2); extLines->AddVertex(toWorld(B));  extLines->AddVertex(toWorld(dB));
        grp->AddPrimitiveArray(extLines);

        // 箭頭：沿同心弧在兩端點的切線方向（垂直於半徑方向），指向弧內側
        auto tangentAt = [&](double ang, bool towardEnd) -> gp_Vec {
            gp_Vec radial(std::cos(ang), std::sin(ang), 0.0);
            gp_Vec tangent(-radial.Y(), radial.X(), 0.0);   // CCW 切線
            return towardEnd ? tangent : tangent * -1.0;
        };
        gp_Pnt wdA = toWorld(dA), wdB = toWorld(dB);
        addArrow3D(grp, wdA, tangentAt(arcAngStart, /*towardEnd=*/false) * -1.0);
        addArrow3D(grp, wdB, tangentAt(arcAngEnd,   /*towardEnd=*/true));
    } else {
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
        // 半徑（無選單版需求）：圓心側無箭頭，只在圓線側畫一個箭頭。
        gp_Vec along(wdA, wdB);
        if (along.Magnitude() > Precision::Confusion()) {
            along.Normalize();
            if (m_info.type != CT::FixedRadius)
                addArrow3D(grp, wdA, along * -1.0);   // 圓心側箭頭（半徑不畫）
            addArrow3D(grp, wdB, along);              // 圓線側箭頭
        }
    }

    // ── 數值文字（紅色，字高 36，平行尺寸線，居中）──────────────────────────
    // 直徑/半徑：標籤放圓心；其他：放尺寸線中點
    {
        const bool isAngleType =
            (m_info.type == CT::FixedAngleDim || m_info.type == CT::FixedAngle);
        // 角度類型：m_info.value 內部為弧度，顯示時轉換為「度」並加上 ° 符號，
        // 與確認後的最終標籤（AIS_DimensionLine::drawAngleDim）格式一致。
        // CoordinateDim（XY）：顯示 (X, Y) 兩個值，而不是只有 measureCurrentValue()
        // 回傳的單一 X 值，避免預覽標籤只有一半資訊、跟「XY」的名稱對不上。
        // FixedX/FixedY/CoordinateDim 標籤格式統一為 (X,-) / (-,Y) / (X,Y)，
        // 讓使用者一眼就能分辨這是單一 X、單一 Y、還是兩者都標的座標尺寸。
        QString label;
        if (isAngleType)
            label = QString::number(m_info.value * 180.0 / M_PI, 'f', 2) + QStringLiteral("°");
        else if (m_info.type == CT::FixedX)
            label = QString("(%1,-)").arg(A.x(), 0, 'f', 2);
        else if (m_info.type == CT::FixedY)
            label = QString("(-,%1)").arg(A.y(), 0, 'f', 2);
        else if (m_info.type == CT::CoordinateDim)
            label = QString("(%1,%2)").arg(A.x(), 0, 'f', 2).arg(A.y(), 0, 'f', 2);
        else if (isArcLengthPreview)
            label = "~" + QString::number(m_info.value, 'f', 2);   // 與 drawArcLengthDimension() 的 "~" 前綴一致
        else
            label = QString::number(m_info.value, 'f', 2);
        // 標籤放在尺寸線中點：弧長／角度都要用「弧中點角度」而非直線中點
        // （dA+dB)/2，否則標籤會落在弦的中點而不是弧線本身上，跟彎曲的
        // 尺寸線對不齊。
        QVector2D midDim;
        if (isArcLengthPreview || isAngleArcPreview) {
            double midAng = (arcAngStart + arcAngEnd) * 0.5;
            midDim = QVector2D(static_cast<float>(arcCenter.x() + arcDimR * std::cos(midAng)),
                                static_cast<float>(arcCenter.y() + arcDimR * std::sin(midAng)));
        } else {
            midDim = (dA + dB) * 0.5f;
        }
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