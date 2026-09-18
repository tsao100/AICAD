#include "view/InputJigOverlay.h"

#include <cmath>

#include <Graphic3d_ArrayOfSegments.hxx>
#include <Graphic3d_AspectLine3d.hxx>
#include <Graphic3d_Text.hxx>
#include <Graphic3d_AspectText3d.hxx>
#include <Font_FontAspect.hxx>
#include <TCollection_ExtendedString.hxx>
#include <Aspect_TypeOfLine.hxx>
#include <Graphic3d_DisplayPriority.hxx>
#include <gp_Vec.hxx>
#include <gp_Ax2.hxx>
#include <Precision.hxx>

namespace aicad {
namespace view {

namespace {
double clampD(double v, double lo, double hi) { return std::max(lo, std::min(hi, v)); }
} // namespace

InputJigOverlay::InputJigOverlay(QObject* parent)
    : QObject(parent)
{}

InputJigOverlay::~InputJigOverlay()
{
    clearPrs();
}

void InputJigOverlay::setContext(const Handle(AIS_InteractiveContext)& ctx)
{
    m_context = ctx;
}

void InputJigOverlay::setView(const Handle(V3d_View)& view)
{
    m_view = view;
}

// ─────────────────────────────────────────────────────────────────────────
double InputJigOverlay::pixelsPerWorldUnit(const gp_Pnt& at) const
{
    if (m_view.IsNull()) return 1.0;   // 優雅降級：等同直接使用世界單位

    Standard_Integer sx0 = 0, sy0 = 0, sx1 = 0, sy1 = 0;
    m_view->Convert(at.X(), at.Y(), at.Z(), sx0, sy0);
    // 沿平面 X 軸量測一個世界單位的投影像素距離；用 m_xAxis 而非任意方向，
    // 確保「在這個點附近」量出的比例（近似 orthographic/local-linear）。
    const gp_Pnt probe = at.Translated(gp_Vec(m_xAxis) * 1.0);
    m_view->Convert(probe.X(), probe.Y(), probe.Z(), sx1, sy1);

    const double pixelDist = std::hypot(double(sx1 - sx0), double(sy1 - sy0));
    if (pixelDist < 1e-9) return 1.0;  // 退化（例如世界單位在螢幕上收縮為 0）
    return pixelDist;                  // 1 世界單位 = pixelDist 像素
}

double InputJigOverlay::worldLengthForPixels(double pixels, const gp_Pnt& at) const
{
    const double ppu = pixelsPerWorldUnit(at);
    if (ppu < 1e-9) return pixels;     // 極端退化保底，避免除以零
    return pixels / ppu;
}

// ─────────────────────────────────────────────────────────────────────────
void InputJigOverlay::showLive(const gp_Pnt& basePt, const gp_Pnt& endPt,
                               const gp_Dir& planeXAxis, const gp_Dir& planeYAxis,
                               double distance, double angleDeg, bool azimuth,
                               const QString& distText, const QString& angleText)
{
    m_base      = basePt;
    m_end       = endPt;
    m_xAxis     = planeXAxis;
    m_yAxis     = planeYAxis;
    m_distance  = distance;
    m_angleDeg  = angleDeg;
    m_azimuth   = azimuth;
    m_distText  = distText;
    m_angleText = angleText;
    m_visible   = true;
    rebuild();
}

void InputJigOverlay::updateText(const QString& distText, const QString& angleText)
{
    if (!m_visible) return;
    m_distText  = distText;
    m_angleText = angleText;
    rebuild();
}

void InputJigOverlay::hide()
{
    m_visible = false;
    clearPrs();
}

void InputJigOverlay::clearPrs()
{
    if (!m_prs.IsNull()) {
        m_prs->Clear();
        m_prs->Erase();
        m_prs.Nullify();
    }
    if (!m_context.IsNull())
        m_context->UpdateCurrentViewer();
}

// ─────────────────────────────────────────────────────────────────────────
void InputJigOverlay::addLine(const Handle(Graphic3d_Group)& grp,
                              const gp_Pnt& a, const gp_Pnt& b)
{
    Handle(Graphic3d_ArrayOfSegments) seg = new Graphic3d_ArrayOfSegments(2);
    seg->AddVertex(a);
    seg->AddVertex(b);
    grp->AddPrimitiveArray(seg);
}

void InputJigOverlay::addText(const QString& text, const gp_Pnt& pos,
                              const gp_Dir& xDir, const gp_Dir& normal,
                              double height, const Quantity_Color& color)
{
    Handle(Graphic3d_Text) gtext = new Graphic3d_Text(static_cast<float>(height));
    gtext->SetText(TCollection_ExtendedString(text.toUtf8().constData(), Standard_True));
    gtext->SetPosition(pos);

    gp_Vec side = gp_Vec(normal).Crossed(gp_Vec(xDir));
    if (side.Magnitude() > Precision::Confusion())
        gtext->SetOrientation(gp_Ax2(pos, normal, xDir));
    gtext->SetHorizontalAlignment(Graphic3d_HTA_CENTER);
    gtext->SetVerticalAlignment(Graphic3d_VTA_CENTER);

    Handle(Graphic3d_AspectText3d) txtAsp = new Graphic3d_AspectText3d();
    txtAsp->SetColor(color);
    // ⚠️ 需求：角度值／線長值文字加粗——本類別僅用 addText() 畫這兩種
    // 文字（見類別說明），故直接固定套用 Font_FA_Bold，不另外加參數。
    txtAsp->SetTextFontAspect(Font_FA_Bold);
    Handle(Graphic3d_Group) txtGrp = m_prs->NewGroup();
    txtGrp->SetGroupPrimitivesAspect(txtAsp);
    txtGrp->AddText(gtext);
}

// ─────────────────────────────────────────────────────────────────────────
void InputJigOverlay::rebuild()
{
    if (m_context.IsNull() || !m_visible) return;

    clearPrs();
    m_prs = new Prs3d_Presentation(m_context->MainPrsMgr()->StructureManager());

    const gp_Dir normal = [&]() -> gp_Dir {
        gp_Vec n = gp_Vec(m_xAxis).Crossed(gp_Vec(m_yAxis));
        if (n.Magnitude() < Precision::Confusion()) return gp_Dir(0, 0, 1);
        return gp_Dir(n);
    }();

    // 固定螢幕像素大小的裝飾性尺寸（見標頭檔類別說明）：以 m_base 附近的
    // 縮放比例換算，兩個標籤/線段共用同一個比例，避免各自量測產生的
    // 微小不一致。
    //
    // ⚠️ 文字高度不適用這套換算——見下方 kTextHeight 的說明。
    const double offsetPx = 28.0;   // 距離標籤偏移量
    const double arcRadiusPx = 26.0; // 角度弧線半徑（退化保底用，見下方 len）
    const double extLenPx = 14.0;    // 角度延伸線超出弧線的一小段（僅供標籤位置用）
    const double tickPx = 4.0;       // 端點刻度
    const double gapPx = 3.0, overshootPx = 3.0; // 距離延伸線間隙/外伸

    const double offset    = worldLengthForPixels(offsetPx, m_base);
    const double tick      = worldLengthForPixels(tickPx, m_base);
    const double gap       = worldLengthForPixels(gapPx, m_base);
    const double overshoot = worldLengthForPixels(overshootPx, m_base);
    const double extLen    = worldLengthForPixels(extLenPx, m_base);

    // ⚠️ 需求：角度延伸線應該與拉出的線長相等，亦即角度尺寸線（弧）半徑
    // 等於拉出的線長——先前 arcRadius 是跟其他裝飾性尺寸（端點刻度、間隙
    // 等）一樣換算成「固定螢幕像素大小」，跟橡皮筋實際拉出的長度完全無關，
    // 使用者拉長／縮短線段時角度弧線大小不會跟著變化。改成直接採用
    // 橡皮筋長度（m_base→m_end 的距離）本身當半徑；只有在長度趨近 0（尚
    // 未拖出可辨識方向）時，才退回原本固定像素大小的預設值，避免半徑為 0
    // 時角度延伸線／弧線／刻度全部退化成同一個點而完全不可見。
    const double len = gp_Vec(m_base, m_end).Magnitude();
    const double arcRadius = (len > Precision::Confusion())
                                 ? len
                                 : worldLengthForPixels(arcRadiusPx, m_base);

    // BUG FIX（縮放後文字消失）：Graphic3d_Text::Height() 的官方文件（見
    // OCCT 標頭 Graphic3d_Text.hxx）明確標注「Relative to the Normalized
    // Projection Coordinates (NPC) Space」——這不是世界座標長度，而是相對
    // 於（正規化後、與視窗實際像素數無關的）投影座標空間的一個穩定小常數。
    // 先前這裡誤用 worldLengthForPixels() 把它當成跟延伸線/尺寸線一樣的
    // 「世界單位長度」來換算：縮小視角時 1 世界單位對應的像素數趨近 0，
    // worldLengthForPixels() 反推出來的「世界單位長度」就會暴衝到極大值；
    // 放大視角時則反過來趨近 0——兩種情況都超出 Graphic3d_Text 能正確
    // 處理的合理範圍，導致文字消失或無法正確繪製。
    //
    // 修正：直接採用固定常數，不隨縮放重新換算——效果上這正是原本想要的
    // 「固定螢幕大小、不隨縮放改變」。數值依需求指定為 18.0f（先前暫用
    // DimPreviewOverlay 的 36.0f，使用者已明確指定角度值／線長值的字高
    // 改為 18.0f）。
    constexpr float kTextHeight = 18.0f;

    // 顏色：view 背景是 Quantity_NOC_GRAY80（見 CadView.cpp 建立 context
    // 處）。角度值／線長值文字依需求改為黑色加粗（先前為紅色一般字重，
    // 跟 DimPreviewOverlay 文字用色一致；使用者已明確指定改為黑色加粗，
    // 見 addText() 內 SetTextFontAspect(Font_FA_Bold) 的設定）。延伸線用
    // 稍暗一階的琥珀色維持主從視覺層次（尺寸線最搶眼，輔助延伸線次之），
    // 仍遠比灰階色系醒目，不受本次文字顏色調整影響。
    Quantity_Color dimColor(Quantity_NOC_YELLOW);
    Quantity_Color extColor(1.0, 0.75, 0.0, Quantity_TOC_RGB);
    Quantity_Color textColor(Quantity_NOC_BLACK);

    // ── 距離：延伸線＋尺寸線＋端點刻度＋文字 ─────────────────────────────
    {
        gp_Vec lineVec(m_base, m_end);
        // len 已在上方（供角度弧半徑使用）算過一次，這裡直接沿用同一個值，
        // 避免重複計算、也避免內層變數遮蔽（shadow）外層的 len。
        if (len > Precision::Confusion()) {
            gp_Dir dirV(lineVec);
            // 垂直於橡皮筋、且落在工作平面內的方向。
            gp_Dir n = gp_Dir(normal.Crossed(gp_Vec(dirV)));

            const gp_Pnt foot0 = m_base.Translated(gp_Vec(n) * offset);
            const gp_Pnt foot1 = m_end.Translated(gp_Vec(n) * offset);

            Handle(Graphic3d_AspectLine3d) extAsp =
                new Graphic3d_AspectLine3d(extColor, Aspect_TOL_SOLID, 1.0f);
            Handle(Graphic3d_Group) extGrp = m_prs->NewGroup();
            extGrp->SetGroupPrimitivesAspect(extAsp);
            addLine(extGrp, m_base.Translated(gp_Vec(n) * gap),
                            foot0.Translated(gp_Vec(n) * overshoot));
            addLine(extGrp, m_end.Translated(gp_Vec(n) * gap),
                            foot1.Translated(gp_Vec(n) * overshoot));

            Handle(Graphic3d_AspectLine3d) dimAsp =
                new Graphic3d_AspectLine3d(dimColor, Aspect_TOL_SOLID, 1.3f);
            Handle(Graphic3d_Group) dimGrp = m_prs->NewGroup();
            dimGrp->SetGroupPrimitivesAspect(dimAsp);
            addLine(dimGrp, foot0, foot1);

            // 端點刻度：45° 斜刻（土木製圖慣用），方向 = dirV+n 正規化。
            gp_Vec tv = gp_Vec(dirV) + gp_Vec(n);
            if (tv.Magnitude() > Precision::Confusion()) {
                gp_Dir tu(tv);
                addLine(dimGrp, foot0.Translated(gp_Vec(tu) * -tick), foot0.Translated(gp_Vec(tu) * tick));
                addLine(dimGrp, foot1.Translated(gp_Vec(tu) * -tick), foot1.Translated(gp_Vec(tu) * tick));
            }

            const gp_Pnt mid((m_base.X() + m_end.X()) / 2.0,
                             (m_base.Y() + m_end.Y()) / 2.0,
                             (m_base.Z() + m_end.Z()) / 2.0);
            const gp_Pnt labelPos = mid.Translated(gp_Vec(n) * offset);
            addText(m_distText, labelPos, dirV, normal, kTextHeight, textColor);
        }
    }

    // ── 角度：延伸線＋尺寸弧線＋端點刻度＋文字 ────────────────────────────
    {
        // 角度量測基準（跟 CadView 計算 liveAngle 用的慣例一致，兩邊必須
        // 對齊，否則這裡畫出來的方向會跟輸入框顯示的角度值對不上）：
        //   • 一般模式：角度值是與 X 軸的夾角，0°＝+X，逆時針為正
        //     （標準數學角慣例）。
        //   • 方位角模式：以 Y 正向（北）為零，順時針計算，0°＝+Y，
        //     角度增加時往 +X 方向轉（= 順時針）。
        auto jigDirAt = [&](double thetaDeg) -> gp_Dir {
            const double rad = thetaDeg * M_PI / 180.0;
            double cx, cy;
            if (m_azimuth) { cx = std::sin(rad); cy = std::cos(rad); } // 0°=+Y(北)，順時針為正
            else            { cx = std::cos(rad); cy = std::sin(rad); } // 0°=+X，逆時針為正
            gp_Vec v = gp_Vec(m_xAxis) * cx + gp_Vec(m_yAxis) * cy;
            return gp_Dir(v);
        };

        const gp_Dir refDir = jigDirAt(0.0);
        const gp_Dir curDir = jigDirAt(m_angleDeg);

        // ⚠️ 需求：角度值及其尺寸線（弧）永遠顯示在小於 180° 的那一側——
        // 先前弧線掃描路徑／標籤角平分線都直接用 m_angleDeg（可能是
        // 0°~360° 或負值），當 |m_angleDeg| > 180° 時會畫出跨越半圓以上
        // 的「優角」弧線，視覺上又長又繞遠路，標籤位置也會落在不直觀的
        // 另一側。把 m_angleDeg 正規化到 (-180°, 180°]，量出的就是
        // refDir→curDir「較短」路徑的夾角（帶正負號＝該往哪個方向轉），
        // 拿這個值取代 m_angleDeg 用在「弧線掃描路徑」與「標籤角平分線
        // 位置」——refDir／curDir 兩條延伸線的方向本身不受影響（sin/cos
        // 是週期函數，jigDirAt(sweep) 跟 jigDirAt(m_angleDeg) 指向同一個
        // 實際方向，只有「畫弧線時要繞哪條路徑過去」跟著改變）。
        double sweep = std::fmod(m_angleDeg, 360.0);
        if (sweep > 180.0) sweep -= 360.0;
        else if (sweep <= -180.0) sweep += 360.0;

        Handle(Graphic3d_AspectLine3d) extAsp =
            new Graphic3d_AspectLine3d(extColor, Aspect_TOL_SOLID, 1.0f);
        Handle(Graphic3d_Group) extGrp = m_prs->NewGroup();
        extGrp->SetGroupPrimitivesAspect(extAsp);
        // ⚠️ 需求：延伸線長度＝弧線半徑＝拉出的線長（見上方 arcRadius 的
        // 說明）——先前這裡是 `arcRadius + extLen`，讓延伸線刻意超出弧線
        // 一小段（常見土木製圖習慣，方便標示端點），但使用者明確要求兩者
        // 相等，故拿掉這段額外的 extLen 外伸，延伸線恰好畫到弧線所在半徑。
        addLine(extGrp, m_base, m_base.Translated(gp_Vec(refDir) * arcRadius));
        addLine(extGrp, m_base, m_base.Translated(gp_Vec(curDir) * arcRadius));

        Handle(Graphic3d_AspectLine3d) dimAsp =
            new Graphic3d_AspectLine3d(dimColor, Aspect_TOL_SOLID, 1.3f);
        Handle(Graphic3d_Group) dimGrp = m_prs->NewGroup();
        dimGrp->SetGroupPrimitivesAspect(dimAsp);

        const int steps = std::max(4, static_cast<int>(std::abs(sweep) / 6.0) + 4);
        gp_Pnt prevPt;
        for (int i = 0; i <= steps; ++i) {
            const double t = sweep * (static_cast<double>(i) / steps);
            const gp_Pnt pt = m_base.Translated(gp_Vec(jigDirAt(t)) * arcRadius);
            if (i > 0) addLine(dimGrp, prevPt, pt);
            prevPt = pt;
        }

        auto drawTick = [&](const gp_Dir& dir) {
            const gp_Pnt pt = m_base.Translated(gp_Vec(dir) * arcRadius);
            const gp_Dir perp = gp_Dir(normal.Crossed(gp_Vec(dir)));
            addLine(dimGrp, pt.Translated(gp_Vec(perp) * -tick), pt.Translated(gp_Vec(perp) * tick));
        };
        drawTick(refDir);
        drawTick(curDir);

        // 標籤放在角平分線方向（弧線中央）——用 sweep（較短路徑夾角）而不是
        // m_angleDeg，理由同上：標籤要落在實際畫出來的那段弧線中央，而不是
        // 「機械地從 0° 到 m_angleDeg」可能繞遠路那一側的中點。
        const gp_Dir bisector = jigDirAt(sweep / 2.0);
        const gp_Pnt labelPos = m_base.Translated(gp_Vec(bisector) * (arcRadius + extLen * 0.6));
        addText(m_angleText, labelPos, bisector, normal, kTextHeight, textColor);
    }

    m_prs->SetZLayer(Graphic3d_ZLayerId_Top);
    m_prs->SetDisplayPriority(Graphic3d_DisplayPriority_Topmost);
    m_prs->Display();
    m_context->UpdateCurrentViewer();
}

} // namespace view
} // namespace aicad
