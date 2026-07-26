/**
 * @file Railway3DAlignmentRenderer.h
 * @brief Railway 資料夾節點 eyeOpen 觸發的「全部線路 3D Alignment」疊加顯示。
 * @author AICAD Team
 */
#pragma once

#include <QObject>
#include <QList>
#include <QMetaObject>

#include <AIS_InteractiveObject.hxx>
#include <AIS_Shape.hxx>

namespace aicad {
namespace railway {
class TrackCenterLine;
}
namespace view {

class CadView;

/**
 * @brief 將目前文件中所有 TrackCenterLine 的 3D Alignment（水平線形 E,N ＋
 *        垂直線形高程 Z）繪製成折線疊加在 CadView 中。
 *
 * 由 FeatureBrowser 的 Railway 資料夾節點 eyeOpen 觸發（見
 * Document::onVisibilityChanged 對 "__railway_folder__" 的處理）。
 * 顯示期間，個別 TrackCenterLine／VAlignment 節點會暫時 eyeClose
 * （由 Document 端統一處理，本類別只負責 3D 折線的建立與清除）。
 *
 * 取樣策略
 * ────────
 *   沿各 TrackCenterLine 的每個水平線形元素（切線／圓弧／緩和曲線）分段，
 *   每段依該處平面半徑（HorizontalAlignment::getRadius）與縱斷面豎曲線
 *   近似半徑（VerticalAlignment::getRadius）取兩者較小者，決定該段的
 *   取樣間距：半徑越小（曲率越大）取樣越密，直線／緩坡則使用較大間距。
 *   元素邊界（TS/SC/CS/ST、豎曲線起訖）恆為取樣點，確保幾何轉折不失真。
 *
 * 即時同步
 * ────────
 *   顯示期間會訂閱每條 TrackCenterLine::dataChanged()（水平／垂直線形改變
 *   時皆會轉發此訊號，見 RailwayAlignment.h 建構子的 connect），一旦觸發即
 *   重新取樣、重繪折線，讓 3D Alignment 與線形編輯保持同步。
 */
class Railway3DAlignmentRenderer : public QObject
{
    Q_OBJECT

public:
    explicit Railway3DAlignmentRenderer(CadView* cadView, QObject* parent = nullptr);
    ~Railway3DAlignmentRenderer() override;

    /**
     * @brief 依目前線路清單重建並顯示所有 3D Alignment 折線，
     *        並訂閱各線路的 dataChanged() 以便日後線形改變時自動同步。
     *        會先清除先前顯示的折線與訂閱（若有）再重新建立。
     */
    void showAll(const QList<railway::TrackCenterLine*>& tcls);

    /** 移除目前顯示的所有 3D Alignment 折線，並取消訂閱。 */
    void clear();

    bool isVisible() const { return m_visible; }

    /**
     * @brief 目前使用者在 3D 檢視中，透過點擊本疊加折線所選取的
     *        TrackCenterLine（可多選，沿用 CadView 既有的 AIS 選取機制／
     *        Shift 疊加選取）。
     *
     *        實作方式：每條折線建立時已透過
     *        CadView::registerSketchGeomAIS() 登錄反查資料，這裡直接檢查
     *        每個 overlay AIS 物件是否位於 AIS_InteractiveContext 目前的
     *        選取集合中（IsSelected()）。
     */
    QList<railway::TrackCenterLine*> selectedTcls() const;

private:
    /** 為單一 TrackCenterLine 建立 3D 折線（局部座標，Z 為實際高程）。 */
    Handle(AIS_Shape) build3DPolyline(const railway::TrackCenterLine* tcl) const;

    /** 依目前 m_tcls 重新計算並重繪折線（不動訂閱），供 dataChanged 觸發。 */
    void rebuildOverlays();

    /** 只移除目前的疊加折線物件，不動訂閱／m_tcls。 */
    void clearOverlaysOnly();

    CadView* m_cadView = nullptr;
    QList<Handle(AIS_InteractiveObject)> m_overlays;
    QList<railway::TrackCenterLine*>     m_overlayTcl;   ///< 與 m_overlays 一一對應
                                                          ///< （build3DPolyline() 可能對
                                                          ///< 某些 tcl 回傳 null 而跳過，
                                                          ///< 故不可假設與 m_tcls 索引相同）
    QList<railway::TrackCenterLine*>     m_tcls;         ///< 目前訂閱同步中的線路清單
    QList<QMetaObject::Connection>       m_connections;  ///< dataChanged 訂閱控制代碼
    bool m_visible = false;
};

} // namespace view
} // namespace aicad
