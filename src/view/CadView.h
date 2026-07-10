/**
 * @file CadView.h
 * @brief CAD 視圖類別 (重構版)
 * @author Felicia
 * @date 2024-12-04
 */

#ifndef AICAD_VIEW_CADVIEW_H
#define AICAD_VIEW_CADVIEW_H

#include <QWidget>
#include <QPointF>
#include <QVector2D>
#include <QPushButton>

#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>

#include "osnap/OSnapManager.h"
#include "view/DimPreviewOverlay.h"

namespace aicad {
namespace osnap {
class OSnapManager;
}
// 前向宣告
namespace cad {
class Document;
class Sketch;
class Plane;
class GripManager;
}

namespace ui {
class GripEventFilter;
}

namespace view {

class RubberBand;
class ViewGrid;

/**
 * @brief 視圖類型
 */
enum class ViewType {
    None,
    Top,        ///< 俯視圖 (XY)
    Bottom,     ///< 仰視圖
    Front,      ///< 前視圖 (XZ)
    Back,       ///< 後視圖
    Right,      ///< 右視圖 (YZ)
    Left,       ///< 左視圖
    Isometric,  ///< 等角視圖
    Perspective ///< 透視視圖
};

/**
 * @brief 互動模式
 */
enum class InteractionMode {
    Idle,           ///< 閒置
    Sketching,      ///< 草圖繪製
    Selecting,      ///< 選擇物件
    Measuring,      ///< 測量
    GetPoint,       ///< 取得點輸入
    GetGeom,        ///< ✅ Task E: 點擊回報 geomUuid+handle（幾何約束互動選取）
    PlaceDimLine,   ///< ✅ Task E: 移動預覽尺寸線位置，點擊確認
    DimLineDrag,    ///< 拖曳已存在的尺寸線（移動尺寸線及數值位置）
    Navigation
};

/**
 * @brief CAD 視圖類別
 *
 * CadView 負責 3D 視覺化和基本互動:
 * - OCCT 渲染管理
 * - 滑鼠/鍵盤互動
 * - 視角控制
 * - 與 RubberBand 和 ViewGrid 協作
 *
 * 使用範例:
 * @code
 * CadView* view = new CadView(this);
 * view->setDocument(document);
 * view->setViewType(ViewType::Top);
 * view->fitAll();
 * @endcode
 */
class CadView : public QWidget {
    Q_OBJECT
    Q_PROPERTY(ViewType viewType READ viewType WRITE setViewType NOTIFY viewTypeChanged)
    Q_PROPERTY(InteractionMode mode READ mode WRITE setMode NOTIFY modeChanged)

public:

    struct OverlayEntry {
        Handle(AIS_InteractiveObject) obj;
        QList<int> modes;
        bool visible = true;  ///< false = eye-close 隱藏（displayAllFeatures 不重新 Display）
    };

    /**
     * @brief 建構子
     * @param parent 父 Widget
     */
    explicit CadView(QWidget* parent = nullptr);

    /**
     * @brief 解構子
     */
    ~CadView() override;

    void setGripManager(cad::GripManager* mgr, ui::GripEventFilter* filter);

    /// 目前是否有 active grips（Sketch / HAlign edit 共用）
    bool hasActiveGrips() const;

    /// 若目前有 active grips，將其全部關閉（含取消進行中的拖曳）。
    /// 回傳是否真的有 grips 被關閉（讓呼叫端決定是否還需要做其他取消動作）。
    bool turnOffActiveGrips();

    /**
     * @brief 設定關聯的文件
     * @param document 文件指標
     */
    void setDocument(cad::Document* document);

    /**
     * @brief 取得關聯的文件
     */
    cad::Document* document() const;

    /**
     * @brief 設定視圖類型
     * @param type 視圖類型
     */
    void setViewType(ViewType type);

    /**
     * @brief 取得當前視圖類型
     */
    ViewType viewType() const;

    /**
     * @brief 設定互動模式
     * @param mode 互動模式
     */
    void setMode(InteractionMode mode);

    /// 通知 CadView 目前是否有 ConstraintPickSession 在等待選取
    /// （GetGeom 模式下 command 已結束，需要區分 idle 與 pick 路徑）
    void setConstraintPickActive(bool active);

    /// 窗選 / 穿越窗選是否正在進行中（包含拖曳中或等待第二次點擊）。
    /// 供 GripEventFilter 判斷是否應暫時避開 grip 命中檢測，避免卡住流程。
    bool isBoxSelectArmed() const;
    /// 取消進行中的窗選/穿越窗選/籬選/多邊形選取（供 ESC 全域處理路徑呼叫，
    /// 例如 UIManager 對 CommandInputEdit::escapePressed 的處理）。
    void cancelActiveBoxSelect();

    /// ✅ Task E: 啟動 PlaceDimLine 模式，設定用於計算偏移的錨點（兩端點中心，草圖平面座標）
    void beginPlaceDimLine(const QVector2D& anchorPos2D);

    /// GDIM: 草圖平面座標 → 螢幕像素座標
    QPoint planeToScreen(const QVector2D& planePt) const;

    /**
     * @brief GDIM 尺寸線即時預覽
     *
     * 使用透明 child overlay widget（DimPreviewOverlay）繪製，
     * 避免 WA_PaintOnScreen 導致 QPainter 失效的問題。
     */
    using DimPreviewInfo = DimPreviewOverlay::PreviewInfo;
    void setDimPreview(const DimPreviewInfo& info);
    void clearDimPreview();

    /**
     * @brief 取得當前互動模式
     */
    InteractionMode mode() const;

    /**
     * @brief 取得 OCCT 互動上下文
     */
    Handle(AIS_InteractiveContext) context() const;

    /**
     * @brief 取得 OCCT 視圖
     */
    Handle(V3d_View) view() const;

    /**
     * @brief 取得橡皮筋物件
     */
    RubberBand* rubberBand() const;

    /**
     * @brief 顯示所有特徵
     */
    void displayAllFeatures();

    QString findFeatureIdByAIS(
        const Handle(AIS_Shape)& aisShape) const;

    /**
     * @brief 為單一 sketch 幾何的 AIS 物件登錄/更新 pick 反查表
     *        （aisToFeatureId / aisToGeomUuid / aisToGeomIndex）。
     *
     * 用於 Sketch::rebuildShapesOnly() 局部重建 AIS handle 後，
     * 只同步真正被替換掉的那幾個 entry，避免每次 solve 都呼叫
     * 整個 displayAllFeatures()（RemoveAll + 全部重新 Display）。
     *
     * @param obj        新（或沿用的舊）AIS handle
     * @param featureId  所屬 Feature::id()
     * @param geomUuid   幾何 UUID（供 SKETCH_GEOM_SELECTED 高亮使用）
     * @param geomIndex  幾何在 sketch->aisShapes() 中的 index（供 GripManager 使用）
     */
    void registerSketchGeomAIS(const Handle(AIS_InteractiveObject)& obj,
                                const QString& featureId,
                                const QString& geomUuid,
                                int geomIndex);

    /// 從 pick 反查表移除單一 AIS 物件的登錄（幾何被刪除、或 handle 被替換掉時呼叫）。
    void unregisterSketchGeomAIS(const Handle(AIS_InteractiveObject)& obj);

    /**
     * @brief 刷新視圖
     */
    void refreshView();

    /**
     * @brief 適應所有物件到視圖
     */
    void fitAll();

    /// 登錄「常駐」AIS 物件，displayAllFeatures 的 RemoveAll 後會自動重新顯示
    void addOverlayAIS(const Handle(AIS_InteractiveObject)& obj,
                       const QList<int>& activationModes = {});
    /// 設定 overlay 物件的 eye-close 可見性。
    /// visible=false 時從 OCCT context 移除但保留 overlayObjects 登錄，
    /// 使 displayAllFeatures() 重建時不重新顯示它。
    void setOverlayAISVisible(const Handle(AIS_InteractiveObject)& obj, bool visible);
    /// 移除登錄並從 context 移除
    void removeOverlayAIS(const Handle(AIS_InteractiveObject)& obj);

    /**
     * @brief 螢幕座標轉平面座標
     * @param screenPos 螢幕座標
     * @return 平面座標
     */
    QVector2D screenToPlane(const QPoint& screenPos) const;
    QPointF screenToPlaneD(const QPoint& screenPos) const;   // double 精度版（Alignment 用）

    /**
     * @brief 啟用/停用網格顯示
     * @param enabled 是否啟用
     */
    void setGridEnabled(bool enabled);

    /**
     * @brief 設定 TM2 參考座標原點（東向 E, 北向 N），供狀態列顯示相對座標用。
     *        不影響幾何計算（幾何管道已全程使用 double）。
     */
    /// @deprecated Phase 2: 改由 ProjectOrigin::instance().setOrigin() 驅動。
    ///             UIManager 在收到 PROJECT_ORIGIN_CHANGED 後呼叫此函式作為相容層。
    void setCoordinateOffset(double easting, double northing);
    /// @deprecated 改用 ProjectOrigin::instance().originE()
    double coordinateOffsetE() const;
    /// @deprecated 改用 ProjectOrigin::instance().originN()
    double coordinateOffsetN() const;

    /**
     * @brief 檢查網格是否啟用
     */
    bool isGridEnabled() const;

    // ✅ 新增：檢查視圖是否已初始化
    /**
     * @brief 檢查視圖是否已完成初始化
     * @return 已初始化回傳 true
     */
    bool isViewInitialized() const;

    /**
     * @brief 設定選取過濾器
     * @param filter "plane", "edge", "face", "all"
     */
    void setSelectionFilter(const QString& filter);

    /**
     * @brief 高亮顯示可選取的平面
     */
    void highlightSelectablePlanes(bool highlight);

    bool isReferencePlane(const Handle(AIS_Shape)& shape);

    QString identifyPlane(const Handle(AIS_Shape)& shape);

    ViewGrid* grid() const;  // Add this public method declaration

    osnap::OSnapManager* snapManager() const;

    // 新增剖面方法
    void setSectionPlane(const gp_Pln& plane);
    void clearSectionPlane();
    bool isSectionActive() const;
    /**
     * @brief 回傳目前選取的草圖幾何 UUID 列表
     *        供 SketchPanel 的約束按鈕使用
     */
    QStringList selectedGeomUuids() const;

    /// 若目前滑鼠偵測（hover/點擊）的物件是尺寸線（AIS_DimensionLine，
    /// 例如尺寸文字），回傳其對應的約束 UUID；否則回傳空字串。
    /// 供 ERASE 等命令在 GetGeom 互動模式下點擊尺寸文字時使用。
    QString detectedConstraintUuid() const;

    void clearSketchGeomSelection();

    /// 在草圖模式下顯示 X 軸 / Y 軸 / 原點（可選取，供束制使用）
    /// 由 UIManager::onSketchEditStarted 呼叫。
    void showSketchAxes(cad::Sketch* sketch);

    /// 移除草圖軸 AIS（離開草圖模式時呼叫）
    void hideSketchAxes();

    QJsonObject saveViewState() const;
    void restoreViewState(const QJsonObject& state);

    /// 顯示 H-Alignment edit 模式的返回按鈕（右上角）
    void showReturnAlignmentButton();
    /// 隱藏返回按鈕
    void hideReturnAlignmentButton();
    /// 控制 status bar 游標座標顯示（true = 不顯示）
    void setSuppressCoordDisplay(bool suppress);

public Q_SLOTS:

    void onSketchRebuilt();

    /**
     * @brief 設定視圖為俯視圖
     */
    void setTopView();

    /**
     * @brief 設定視圖為前視圖
     */
    void setFrontView();

    /**
     * @brief 設定視圖為右視圖
     */
    void setRightView();

    /**
     * @brief 設定視圖為等角視圖
     */
    void setIsometricView();

    void alignToPlane(const cad::Plane* plane);

    void onFinishSketchClicked();

    /** 顯示偵測到的 region（半透明填色面） */
    void displaySketchRegions(
        const QVector<aicad::cad::SketchRegion>& regions,
        const cad::Sketch* sketch);

    /** 清除所有 region 高亮 */
    void clearSketchRegions();

    /** 高亮選取的 region */
    void highlightSketchRegion(const QString& regionUuid);
Q_SIGNALS:
    /// 返回按鈕被按下（結束 H-Alignment edit 模式）
    void returnAlignmentRequested();

    /**
     * @brief 視圖類型改變時發出
     * @param type 新的視圖類型
     */
    void viewTypeChanged(ViewType type);

    /**
     * @brief 互動模式改變時發出
     * @param mode 新的互動模式
     */
    void modeChanged(InteractionMode mode);

    /**
     * @brief 取得點時發出
     * @param point 平面座標
     */
    void pointAcquired(QPointF point);   // double 精度（Alignment 用）

    /**
     * @brief 取得點時（帶草圖點 ID）發出 — 用於距離/角度約束選點
     * @param point     平面座標
     * @param geomUuid  snap 到的草圖幾何 UUID（若無法辨識則為空）
     * @param geomHandle snap 到的端點 handle（-1 = WholeGeom）
     */
    void geomRefPicked(QPointF point, QString geomUuid, int geomHandle);

    /// ✅ Task E: PlaceDimLine 模式 — 滑鼠移動時的預覽偏移
    void dimLinePosPreview(double offsetX, double offsetY);

    /// ✅ Task E: PlaceDimLine 模式 — 滑鼠點擊確認偏移
    void dimLinePosConfirmed(double offsetX, double offsetY);

    /// 拖曳現有尺寸線 — 開始（攜帶約束 UUID）
    void dimLineDragStarted(const QString& constraintUuid);
    /// 拖曳現有尺寸線 — 即時更新偏移
    void dimLineDragging(const QString& constraintUuid, double offsetX, double offsetY);
    /// 拖曳現有尺寸線 — 放開確認
    void dimLineDragFinished(const QString& constraintUuid, double offsetX, double offsetY);

    /**
     * @brief 取得點被取消時發出
     */
    void pointCancelled();

    /**
     * @brief 按鍵輸入時發出 (用於座標輸入)
     * @param key 按鍵字元
     */
    void keyInputReceived(QString key);

    /**
     * @brief 物件被選擇時發出
     * @param objectId 物件 ID
     */
    void objectSelected(int objectId);

    /**
     * @brief 視圖被點擊時發出
     * @param screenPos 螢幕座標
     * @param button 滑鼠按鈕
     */
    void viewClicked(QPoint screenPos, Qt::MouseButton button);

    // ✅ 新增：視圖初始化完成信號
    /**
     * @brief 視圖初始化完成時發出
     *
     * 當 OCCT 視圖完全準備好可以顯示內容時發出此信號。
     * UIManager 可以監聽此信號來初始化參考幾何。
     */
    void viewInitialized();

    /**
     * @brief 平面被選取時發出
     */
    void planeSelected(const QString& planeName);

    /**
     * @brief Sketch editing finished
     */
    void sketchFinished();

    /** 草圖模式下偵測到區域，提供高亮與選取 */
    void sketchRegionsDetected(const QVector<aicad::cad::SketchRegion>& regions);

    /** 使用者在草圖模式點選了某個 region */
    void sketchRegionPicked(const aicad::cad::SketchRegion& region);

    void statusMessageRequested(const QString& msg, int timeoutMs);

protected:
    /**
     * @brief 繪製事件
     */
    void paintEvent(QPaintEvent* event) override;

    /**
     * @brief 大小改變事件
     */
    void resizeEvent(QResizeEvent* event) override;

    /**
     * @brief 滑鼠按下事件
     */
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    void enterEvent(QEnterEvent* event) override;
#else
    void enterEvent(QEvent* event) override;
#endif
    void mousePressEvent(QMouseEvent* event) override;

    /**
     * @brief 滑鼠移動事件
     */
    void mouseMoveEvent(QMouseEvent* event) override;

    /**
     * @brief 滑鼠釋放事件
     */
    void mouseReleaseEvent(QMouseEvent* event) override;

    void handleViewCubeClick(const QPoint& pos);
    void startViewCubeAnimation();

    /**
     * @brief 滾輪事件
     */
    void wheelEvent(QWheelEvent* event) override;

    /**
     * @brief 按鍵按下事件
     */
    void keyPressEvent(QKeyEvent* event) override;

    /**
     * @brief 返回空的繪製引擎 (由 OCCT 處理)
     */
    QPaintEngine* paintEngine() const override { return nullptr; }

    void showEvent(QShowEvent* event) override;


private:
    /**
     * @brief 初始化 OCCT 視圖器
     */
    void initializeViewer();

    /**
     * @brief 更新視角投影
     */
    void updateProjection();

    /**
     * @brief 處理點輸入
     */
    void handlePointInput(const QPoint& screenPos);

    /**
     * @brief 處理物件選擇
     */
    void handleObjectSelection(const QPoint& screenPos);

    /**
     * @brief 將 Qt 座標轉換為 OCCT 座標
     */
    void qtToOCCT(const QPoint& qtPos, Standard_Integer& occX, Standard_Integer& occY) const;

    // ── 窗選 / 穿越窗選（Window / Crossing box selection）────────────────────
    /// 記錄拖曳候選起點（點擊到空白處時呼叫）。screenPos 為 Qt 邏輯座標。
    /// sketchMode 決定放開時未形成拖曳的 fallback 行為，以及框選結果要不要
    /// 發布 SKETCH_GEOM_SELECTED。
    void beginBoxSelectCandidate(const QPoint& screenPos, bool additive, bool sketchMode);
    /// mouseMoveEvent 呼叫；超過拖曳門檻後才會顯示選取框並依方向決定窗選/穿越窗選樣式。
    void updateBoxSelectDrag(const QPoint& screenPos);
    /// mouseReleaseEvent 呼叫；若曾形成拖曳則執行框選，否則比照原本單擊行為。
    void finishBoxSelect(const QPoint& screenPos);
    /// 取消進行中的框選候選（ESC、模式切換等情況呼叫）。
    void cancelBoxSelectCandidate();
    /// 還原框選開始前的選取狀態（供預覽取消/疊加選取的最終合併使用）。
    void restoreBoxSelectSavedSelection();
    /// 依窗選/穿越窗選規則對矩形範圍套用選取（供預覽與正式完成共用，確保
    /// Shift（疊加）狀態在拖曳過程中與最終結果完全一致）。additive 為 true 時，
    /// 會先還原成框選開始前的原始選取，再以 Add scheme 疊加矩形命中的物件，
    /// 避免已選取的物件在預覽或重複框選過程中被誤判為未選取。
    void applyBoxSelectionScheme(Standard_Integer x0, Standard_Integer y0,
                                  Standard_Integer x1, Standard_Integer y1,
                                  bool additive);
    /// 執行矩形選取並比照既有單擊選取邏輯發布事件（物理像素座標，供 OCCT SelectRectangle 使用）。
    void performRectangleSelection(Standard_Integer x0, Standard_Integer y0,
                                    Standard_Integer x1, Standard_Integer y1,
                                    bool additive, bool sketchMode);
    /// 收集目前 context 選取結果並發布事件（矩形／籬選／多邊形選取共用邏輯）。
    void publishBoxSelectionResult(bool sketchMode);

    // ── 籬選(Fence) / 多邊形窗選(WPolygon) / 多邊形框選(CPolygon) ─────────────
    // 在矩形窗選提示「指定對角點或 [籬選(F)/多邊形窗選(WP)/多邊形框選(CP)]:」
    // 階段，透過命令列輸入 F/WP/CP 切換為多點式選取：每次左鍵點一下新增一個
    // 頂點，Enter 或 Space 完成，ESC 取消。
    enum class BoxSelectShape { Rectangle, Fence, WPolygon, CPolygon };
    /// 由命令列 F/WP/CP 選項觸發，切換為籬選/多邊形窗選/多邊形框選的頂點收集模式。
    void beginBoxSelectShapeMode(BoxSelectShape shape);
    /// 新增一個多邊形/籬選頂點（點擊確認）。
    void addBoxSelectPolyVertex(const QPoint& screenPos);
    /// 更新多邊形/籬選的橡皮筋預覽線＋即時高亮（尚未確認的最後一段）。
    void updateBoxSelectPolyPreview(const QPoint& screenPos);
    /// 完成多邊形/籬選並執行選取（Enter / Space 觸發）。
    void finishBoxSelectPolygon();
    /// 依窗選/穿越窗選規則對多邊形/籬選範圍套用選取（供預覽與正式完成共用）。
    void applyPolygonSelectionScheme(const QVector<QPoint>& ptsQt, bool crossing, bool additive);
    /// 取得目前視角對應的參考平面（與 screenToPlane() 相同規則，供窗選/籬選/多邊形視覺共用）。
    cad::Plane* boxSelectReferencePlane() const;

    QString m_selectionFilter;
    QVector<Handle(AIS_Shape)> m_referencePlanes;  // 儲存參考平面

    QPushButton*        m_finishSketchButton;
    QPushButton*        m_returnAlignmentButton = nullptr;  ///< H-Alignment edit 返回按鈕
    bool                m_suppressCoordDisplay  = false;    ///< true = 不在 status bar 顯示游標座標
    DimPreviewOverlay*  m_dimOverlay = nullptr;   ///< GDIM 尺寸線預覽 overlay
    bool m_viewReadyPublished = false;

    void showFinishSketchButton();
    void hideFinishSketchButton();

    void showSketchContextMenu(const QPoint& screenPos);

    // ── Object Snap ──────────────────────────────────────────
    aicad::osnap::OSnapManager* m_snapManager = nullptr;

    /// 從 snap 取得目前座標（優先 snap，退而其次用 mouse pick）
    gp_Pnt currentInputPoint(int mouseX, int mouseY) const;    

    class Private;
    Private* d;
};

} // namespace view
} // namespace aicad

#endif // AICAD_VIEW_CADVIEW_H
