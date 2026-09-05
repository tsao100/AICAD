/**
 * @file InputJigOverlay.h
 * @brief InputJig「即時顯示」部分（距離／角度數值＋尺寸線／尺寸弧線）的
 *        OCCT 原生渲染，取代原本疊在 CadView 上的 Qt widget（QFrame/QLabel
 *        + 自繪 JigLeaderLine）。
 *
 * 架構與職責分工比照 DimPreviewOverlay（見該檔開頭說明：完全不使用 Qt
 * widget overlay，避免 WA_PaintOnScreen native window 的 child widget
 * 繪製問題）：
 *
 *   - 本類別（InputJigOverlay）：純顯示，用 Prs3d_Presentation 直接畫在
 *     3D 場景中——距離的延伸線／尺寸線／端點刻度／數值文字、角度的
 *     延伸線／尺寸弧線／端點刻度／數值文字。因為根本不是 Qt widget，
 *     天生就不會攔截任何滑鼠事件，不需要（也沒有）WA_TransparentFor-
 *     MouseEvents 這類補救措施。
 *   - InputJig（既有類別，未變動職責）：純輸入，兩個 QLineEdit 負責「使用
 *     者正在輸入覆寫值」這件事——OCCT 沒有可編輯文字元件，鍵盤輸入
 *     （游標、選取、退格、IME）仍需要真正的 QLineEdit。呼叫端（CadView）
 *     讓這兩個 widget 平常保持隱藏，只在使用者實際開始打字
 *     （beginTypedInput）或 Tab 切換聚焦時才顯示成一個貼著錨點的小方框；
 *     其餘時間（單純移動滑鼠、即時追蹤數值）畫面上完全沒有 Qt widget，
 *     由本類別以 3D 文字顯示。
 *
 * 座標系：本類別的輸入一律是「世界座標」（gp_Pnt／gp_Dir），不是螢幕像素
 * 也不是草圖平面 2D 座標——呼叫端（CadView）自己視所在模式（Sketch 平面／
 * Alignment 對齊平面／Grip 拖曳的既有世界座標）換算好再傳進來，本類別不
 * 需要知道「目前是哪個模式」。
 *
 * 尺寸縮放：延伸線偏移量／尺寸弧半徑／端點刻度／文字高度全部換算成「固定
 * 螢幕像素大小」，不隨縮放改變——比照傳統 CAD 尺寸標註在畫面上的視覺習慣
 * （原本 Qt widget 版本本來就是用固定像素偏移量），也讓同一套視覺在草圖
 * （單位通常很小）與鐵路線形編輯（單位通常是公尺、數百甚至數千）兩種懸殊
 * 的比例尺情境下都合理可讀。換算方式：用 setView() 提供的 V3d_View，在
 * 每次 rebuild() 時用兩次 View::Convert()（world→screen，與 CadView::
 * planeToScreen() 用的是同一顆 API，本專案既有、已驗證可行的做法）量出
 * 「目前視角在這個點附近，1 個世界單位對應多少螢幕像素」，再反推「想要
 * 的螢幕像素大小」對應的世界單位長度——而不是引入本專案還沒有先例、風險
 * 較高的 Graphic3d_TMF_ZoomPers whole-structure zoom-persistence 機制
 * （目前唯一的 TransformPersistence 用例是 AIS_ViewCube 的
 * Graphic3d_TMF_TriedronPers「釘在螢幕角落」，性質不同：那是整個物件跟著
 * 螢幕走，這裡是尺寸線兩端點要跟著世界座標縮放、只有裝飾性的偏移量/刻度/
 * 文字大小想要固定，兩者混用需要更細緻的 per-group persistence）。
 *
 * @author AICAD Team
 */
#pragma once

#include <QObject>
#include <QString>

#include <AIS_InteractiveContext.hxx>
#include <Prs3d_Presentation.hxx>
#include <Graphic3d_Group.hxx>
#include <V3d_View.hxx>
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>
#include <Quantity_Color.hxx>

namespace aicad {
namespace view {

class InputJigOverlay : public QObject
{
    Q_OBJECT
public:
    explicit InputJigOverlay(QObject* parent = nullptr);
    ~InputJigOverlay() override;

    /// 設定 OCCT context（由 CadView 在建構時呼叫，比照 DimPreviewOverlay）。
    void setContext(const Handle(AIS_InteractiveContext)& ctx);

    /// 設定 view（供固定螢幕像素大小的換算用，見類別說明）。
    void setView(const Handle(V3d_View)& view);

    /**
     * @param basePt    橡皮筋起點（世界座標）。
     * @param endPt     橡皮筋終點／目前滑鼠或覆寫後的最終點（世界座標）。
     * @param planeXAxis 目前工作平面的 X 軸方向（世界座標）——角度 0°
     *                  （數學模式）／90°（方位角模式，"east"）對應此方向。
     * @param planeYAxis 目前工作平面的 Y 軸方向（世界座標）——角度 0°
     *                  （方位角模式，"north"）對應此方向；法向量由
     *                  planeXAxis × planeYAxis 內部推導，不需另外傳入，
     *                  避免呼叫端各自算 normal 卻跟 xAxis/yAxis 不一致。
     *                  Sketch 模式用 Sketch::plane() 的 xAxis()/yAxis()；
     *                  Alignment/一般模式可用世界 X/Y 軸；Grip 拖曳用
     *                  GripManager::planeXAxis()/planeYAxis()。
     * @param distance  即時距離（未套用覆寫的滑鼠原始值）。
     * @param angleDeg  即時角度（度，未套用覆寫的滑鼠原始值）。
     * @param azimuth   true=方位角（正北=0，順時針為正）；false=數學角。
     * @param distText  距離標籤要顯示的文字——由呼叫端決定內容：未鎖定時
     *                  通常是 distance 格式化字串；鎖定中（使用者正在打字
     *                  覆寫）則是 InputJig 目前 QLineEdit 的文字內容，讓
     *                  3D 文字即時反映使用者輸入，看起來像直接在場景裡打字。
     * @param angleText 角度標籤文字，規則同上。
     */
    void showLive(const gp_Pnt& basePt, const gp_Pnt& endPt,
                  const gp_Dir& planeXAxis, const gp_Dir& planeYAxis,
                  double distance, double angleDeg, bool azimuth,
                  const QString& distText, const QString& angleText);

    /**
     * @brief 只更新兩個標籤文字，沿用上一次 showLive() 的幾何（端點/軸向/
     *        距離/角度數值）。供使用者打字（InputJig::liveTextChanged）
     *        時呼叫——這種情況下線段本身的端點位置本來就還沒變（要等下一
     *        次滑鼠移動才會重新算，跟原本橡皮筋本身「打字不會馬上移動
     *        端點、要等下一次滑鼠事件」的既有行為一致，見 CadView.cpp
     *        呼叫端註解），只有文字內容需要立即反映使用者剛打的字。
     *        showLive() 從未呼叫過（m_visible==false）時，此呼叫為 no-op。
     */
    void updateText(const QString& distText, const QString& angleText);

    void hide();
    bool isVisible() const { return m_visible; }

private:
    void rebuild();
    void clearPrs();

    /// 目前視角在 @p at 附近，1 個世界單位對應多少螢幕像素（見類別說明的
    /// 「尺寸縮放」段落）。view 未設定或退化時回傳 1.0（等同直接使用世界
    /// 單位，優雅降級，不會除以零或當掉）。
    double pixelsPerWorldUnit(const gp_Pnt& at) const;

    /// 把「想要的螢幕像素大小」換算成目前視角下對應的世界單位長度。
    double worldLengthForPixels(double pixels, const gp_Pnt& at) const;

    void addLine(const Handle(Graphic3d_Group)& grp, const gp_Pnt& a, const gp_Pnt& b);
    void addText(const QString& text, const gp_Pnt& pos,
                 const gp_Dir& xDir, const gp_Dir& normal,
                 double height, const Quantity_Color& color);

    Handle(AIS_InteractiveContext) m_context;
    Handle(V3d_View)               m_view;
    Handle(Prs3d_Presentation)     m_prs;

    gp_Pnt  m_base, m_end;
    gp_Dir  m_xAxis{1.0, 0.0, 0.0};
    gp_Dir  m_yAxis{0.0, 1.0, 0.0};
    double  m_distance = 0.0;
    double  m_angleDeg = 0.0;
    bool    m_azimuth  = false;
    QString m_distText, m_angleText;
    bool    m_visible  = false;
};

} // namespace view
} // namespace aicad
