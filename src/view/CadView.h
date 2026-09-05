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
#include <QPair>
#include <optional>
#include <QVector>

#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>
#include <TopoDS_Edge.hxx>

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
class AIS_DimensionLine;
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
    DimValueEdit,   ///< 雙擊尺寸線數值 → 行內編輯數值/表達式中
    Navigation,
    PickEdge        ///< ✅ Chamfer 等指令使用：點選任意已顯示實體 Feature 的邊
};

/**
 * @brief 一條被選取邊的參照（配合 CadView::beginEdgePicking() 使用）。
 *
 * featureId 是該邊所屬 Feature 的 id()（見 aisToFeatureId），edge 是
 * OCCT 的 TopoDS_Edge，屬於該 Feature 目前顯示中的 shape()。
 */
struct PickedEdgeRef {
    QString     featureId;
    TopoDS_Edge edge;
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

    /// 「尺寸參數選取插入」模式（見 dimensionRefPicked() 訊號說明）。啟用時，
    /// 點擊命中尺寸線／文字改為發出 dimensionRefPicked()，不做一般選取／
    /// 拖曳；不影響其他滑鼠操作（平移、縮放、一般幾何選取）。由
    /// DimExpressionDialog 在顯示/隱藏時開關。
    void setRefPickModeActive(bool active);
    bool isRefPickModeActive() const;

    /// 窗選 / 穿越窗選是否正在進行中（包含拖曳中或等待第二次點擊）。
    /// 供 GripEventFilter 判斷是否應暫時避開 grip 命中檢測，避免卡住流程。
    bool isBoxSelectArmed() const;

    /// 供互動命令（SketchSelectionPicker::PickMultiple／EraseCommand 模式 B
    /// 等「選取物件，Enter 確認」階段）宣告：目前是否允許在 GetGeom 模式下
    /// 點擊空白處啟動窗選／穿越窗選／籬選／多邊形選取。
    ///
    /// 背景：mousePressEvent() 原本只有在「沒有作用中命令」（!hasCmd，例如
    /// 選取物件後再輸入 MOVE 的『模式 A』預選）的情境下，點擊空白處才會呼叫
    /// beginBoxSelectCandidate() 啟動窗選；命令執行中（hasCmd==true）等待
    /// GEOM_PICKED 的「選取物件」階段，點擊空白處一律直接落入 handlePointInput()
    /// 送出空 UUID，窗選/籬選機制完全無法啟動。本旗標讓命令的選取子階段開始
    /// 前主動宣告「現在允許窗選」，mousePressEvent() 才會在 hasCmd==true 時
    /// 也走 beginBoxSelectCandidate() 路徑（sketchMode 固定為 true，additive
    /// 固定為 true——命令執行中的多選階段比照既有單擊 GEOM_PICKED 累加/切換
    /// 語意，不需要按 Shift 才能疊加）。呼叫端必須在選取子階段結束
    /// （confirmed/cancelled/cleanup）時呼叫 setCommandBoxSelectEligible(false)
    /// 關閉，避免残留影響其他不支援框選的 GetGeom 用途（例如 TRIM 逐段點選
    /// 迴圈、FILLET/CHAMFER 逐一點選單一物件）。
    void setCommandBoxSelectEligible(bool eligible);

    /// F8 正交鎖定（Ortho Lock）目前是否開啟。Sketch 與 Alignment edit 共用
    /// 同一個旗標：草圖橡皮筋取點、Grip 拖曳（含水平線形 IP 拖曳）皆會套用。
    bool isOrthoLocked() const;

    /// InputJig（距離/角度輸入 Jig）目前是否顯示中。供 CommandLineWidget
    /// 的全域按鍵攔截判斷是否該把按鍵放行給 CadView／InputJig 自己處理，
    /// 而不是被導向命令列輸入框（見 UIManager::setupCommandLine）。
    bool isInputJigVisible() const;
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
     * @brief GDIM 無選單版第 1 節規則表：「整個圓／整條弧」的位置推論需要
     *        偵測「遊標落在圓/弧內部（可能遠離邊界曲線本身）」，但 OCCT
     *        AIS_Shape 的預設選取靈敏度只涵蓋邊界曲線附近（幾個像素內），
     *        無法偵測「圓內部但遠離邊界」的 hover/點擊。
     *
     *        啟用後，hover（mouseMoveEvent）與點擊（handlePointInput）在
     *        既有 OSnap／OCCT DetectedInteractive 皆未命中時，會額外對目前
     *        草圖的所有 Circle／Arc 做「距圓心 vs 半徑」的幾何式命中測試，
     *        取代 OCCT 只認邊界曲線的限制，讓「圓內任一點」都能被視為
     *        hover/點擊到該圓（Arc 則是「貼近圓心、在半徑範圍內」）。
     *
     *        僅供 GeneralDimCommand（GDIM）在 execute()/cleanup() 開關，
     *        避免影響 Erase／LeaderNote 等其他同樣使用 GetGeom 模式的指令。
     */
    void setGdimWholeGeomHitTestEnabled(bool enabled);

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

    /// 目前的選取過濾器（供 FeatureBrowser 等外部元件判斷是否正在等待
    /// 平面選取，例如 Create Sketch 命令互動選平面時 — 見 issue #10）
    QString selectionFilter() const { return m_selectionFilter; }

    /**
     * @brief 高亮顯示可選取的平面
     */
    void highlightSelectablePlanes(bool highlight);

    bool isReferencePlane(const Handle(AIS_Shape)& shape);

    QString identifyPlane(const Handle(AIS_Shape)& shape);

    /**
     * @brief 對所有已顯示的實體 Feature（非 Sketch）開啟邊（TopAbs_EDGE）
     *        子形選取，並切換到 InteractionMode::PickEdge。
     *
     * 供 ChamferCommand 等指令使用。呼叫端（Command::cleanup()）之後應
     * 呼叫 endEdgePicking() 並自行把 InteractionMode 換回進入指令前的模式
     * （比照 GetPoint 模式既有慣例，見 Alignment3DAddVProfileCommand）。
     */
    void beginEdgePicking();

    /**
     * @brief 結束邊選取：還原所有實體 Feature 為整體（whole-shape）選取
     *        模式，並清空目前 AIS 選取集合。不會自動還原 InteractionMode。
     */
    void endEdgePicking();

    /**
     * @brief 取得目前（AIS_InteractiveContext 選取集合中）已選取的邊。
     *        僅在 beginEdgePicking() 之後、endEdgePicking() 之前呼叫有意義。
     */
    QVector<PickedEdgeRef> pickedEdges() const;

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
     * @brief displayAllFeatures() 完成時發出（每次呼叫皆會 RemoveAll() 整個
     *        context 後重建）。凡是「不屬於 Feature::m_aisShapes、由外部
     *        模組（例如 ConstraintOverlayManager 的束制符號／尺寸標註、
     *        SketchPanel 疊加圖層）另外管理」的 AIS 物件，都會被 RemoveAll()
     *        清掉且不會被 displayAllFeatures() 自動還原，必須在收到此訊號後
     *        自行重新顯示，否則會出現「編輯草圖時束制符號忽然消失」之類的
     *        問題（見 UIManager 對此訊號的訂閱）。
     */
    void featuresRedisplayed();

    /**
     * @brief PickEdge 模式下每次點擊命中/取消一條邊時發出（用於更新指令提示文字，
     *        例如「已選取 N 條邊」）。實際選取集合請呼叫 pickedEdges() 取得。
     */
    void edgePicked();

    /**
     * @brief 尺寸參數「選取插入」模式（見 setRefPickModeActive()）啟用時，
     *        點擊命中一條尺寸線／文字所發出，帶出該尺寸對應的約束 UUID。
     *        用於 DimExpressionDialog：使用者可以不打字，直接點選草圖裡
     *        其他既有的尺寸標註，把它的自動命名參數（d1、a1…）插入正在
     *        編輯的運算式裡。
     */
    void dimensionRefPicked(const QString& constraintUuid);

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

    /// 雙擊尺寸線數值文字進入行內編輯，使用者按 Enter 或滑鼠右鍵確認新數值/表達式後發出
    /// （CoordinateDim 等雙數值類型以 "x,y" 逗號分隔）
    void dimValueEditCommitted(const QString& constraintUuid, const QString& newExprOrValue);

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

    /**
     * @brief 滑鼠雙擊事件（雙擊尺寸線數值 → 進入行內編輯）
     */
    void mouseDoubleClickEvent(QMouseEvent* event) override;

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


public:
    /**
     * @brief 供 CadView 之外的呼叫端（目前是 UIManager）觸發與
     *        performEscapeCancel() 完全相同的「完整取消」流程。
     *
     * 命令列輸入框（CommandInputEdit）持有鍵盤焦點時，ESC 會先落在該
     * QLineEdit 自己的 keyPressEvent（清除文字、emit escapePressed()），
     * 完全不會經過 CadView::keyPressEvent()，因此 InputJig 隱藏／橡皮筋
     * 清除／POINT_CANCELLED／關閉 grips 這一整套流程原本永遠不會被觸發到
     * ——即使 InputJig 正顯示著、命令正在等待取點。UIManager 收到
     * CommandInputEdit::escapePressed 後改呼叫這個方法，補上第四個呼叫點。
     */
    void requestEscapeCancel() { performEscapeCancel(); }

    /**
     * @brief GDIM v2 Phase 7：碰撞偵測（Collision Avoidance 的偵測部分）
     *
     * 掃描目前 context() 中所有已顯示的 AIS_DimensionLine，把每個數值
     * 標籤（見 AIS_DimensionLine::labelRegions()）投影到螢幕座標
     * （view()->Convert()），做簡單的 2D AABB 相交測試，回傳重疊的
     * 標註 uuid 配對清單。
     *
     * 實作限制：沒有精確字型量測，文字寬高用「字元數 × 固定像素」估計
     * （見 .cpp 的 kCharPxWidth/kLabelPxHeight），屬近似值，不是像素級
     * 精確的碰撞判定；足以偵測「明顯重疊」，但抓不到剛好擦邊的情況。
     */
    struct AnnotationCollision { QString uuidA, uuidB; };
    QList<AnnotationCollision> checkAnnotationCollisions() const;

private:
    /**
     * @brief 執行「完整取消」：與舊行為「InputJig 作用中按 ESC 兩次」/
     *        鍵盤 ESC 落在 CadView 本身（非 Jig 的行內編輯欄）時完全相同的
     *        一次性動作 —— 清橡皮筋、隱藏並重置 InputJig、依 mode 發佈
     *        POINT_CANCELLED、關閉所有作用中的 grips。
     *
     * 統一供四個呼叫點使用，確保「InputJig 作用中按一次 ESC」與
     * 「InputJig 作用中點一次滑鼠右鍵」都等同於這個完整流程（而不是舊版
     * 需要再按第二次 ESC 才會執行到的部分）：
     *   1. keyPressEvent() 的 Key_Escape 分支（Jig 未取得焦點時，ESC 直接
     *      落在 CadView 本身）。
     *   2. InputJig::cancelled 訊號（Jig 的行內編輯欄有焦點，攔截了 ESC）。
     *   3. mousePressEvent() 偵測到 Jig 顯示中的滑鼠右鍵。
     *   4. requestEscapeCancel()（命令列輸入框持有焦點時，經由 UIManager
     *      轉發 CommandInputEdit::escapePressed 呼叫）。
     */
    void performEscapeCancel();

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
     * @brief GetGeom 模式下，滑鼠右鍵結束選取（等同於送出空字串的 Enter）。
     *
     * 從 mousePressEvent() 與 mouseReleaseEvent() 兩處呼叫（見兩者呼叫點的
     * 註解）：主要邏輯掛在 mousePressEvent()，mouseReleaseEvent() 那份是保
     * 險用的重複呼叫——若命令列的等待輸入狀態已經在 press 階段被消費過，
     * isWaitingForInput() 這裡會是 false，本函式會安全地直接回傳 false，
     * 不會重複觸發 executeCommand("")。
     *
     * @return true 表示已處理（呼叫端應 event->accept() 並 return）。
     */
    bool tryEndGetGeomSelectionViaRightClick(QMouseEvent* event);

    /**
     * @brief 通用版：只要命令輸入框裡已經有使用者打好、還沒按下 Enter 的
     *        文字，滑鼠右鍵就等同送出那段文字（等同真的按下 Enter）。
     *
     * 與 tryEndGetGeomSelectionViaRightClick() 不同的地方：後者只在
     * GetGeom 模式下、且只送出「空字串」（結束選取）；本函式不限制
     * d->mode，且是把「目前打好的文字」原樣送出（例如 TRACKEXTRACT 提示
     * 等待 A/AUTO 時，打了 A 還沒按 Enter，右鍵應該直接送出 "A"，而不是
     * 被取消指令邏輯打斷；Sketch edit 中還沒有指令在跑、只是剛打了指令名
     * 稱如 "LINE" 時，isWaitingForInput() 是 false，但一樣要能用右鍵送
     * 出，故不檢查這個狀態，只檢查輸入框裡「有沒有文字」）。
     *
     * 輸入框裡沒有文字時回傳 false，交給後續既有邏輯（GetGeom 空白 Enter
     * 結束選取、Yes/No 確認、或取消目前指令）處理，行為不變。
     *
     * @return true 表示已處理（呼叫端應 event->accept() 並 return）。
     */
    bool tryEndPendingTextInputViaRightClick(QMouseEvent* event);

    /**
     * @brief 滑鼠右鍵＝「送出目前命令列輸入框內容」（等同按下 Enter）。
     *
     * 適用範圍：命令正在等待 InputType::YesNo（目前僅 MIRROR 的「是否
     * 刪除原物件」階段使用，未來其他 Yes/No 提示的命令可直接沿用）。與
     * tryEndGetGeomSelectionViaRightClick() 不同的地方：後者只送出固定的
     * 空字串，本函式改為呼叫 CommandInputEdit::submitCurrentLine()，會
     * 把使用者「已經打在輸入框但還沒按 Enter」的內容一併送出（例如先打
     * "y" 再按右鍵，等同打完 "y" 再按 Enter，而不是被忽略掉、只送出空
     * 字串變成預設的 No）。輸入框為空時 submitCurrentLine() 本身就會送
     * 出空字串套用預設值，效果與 tryEndGetGeomSelectionViaRightClick()
     * 一致。
     *
     * 同樣從 mousePressEvent() 與 mouseReleaseEvent() 兩處呼叫，道理與
     * tryEndGetGeomSelectionViaRightClick() 相同（保險用重複呼叫，
     * isWaitingForInput() 消費過一次後第二次呼叫安全地 no-op）。
     *
     * @return true 表示已處理（呼叫端應 event->accept() 並 return）。
     */
    bool tryConfirmYesNoViaRightClick(QMouseEvent* event);

    /**
     * @brief 處理物件選擇
     */
    void handleObjectSelection(const QPoint& screenPos);

    /**
     * @brief GDIM 專用：對草圖所有 Circle／Arc 做「距圓心 vs 半徑」的幾何式
     *        命中測試，補足 OCCT AIS_Shape 邊界曲線選取靈敏度無法涵蓋「圓/弧
     *        內部」的缺口（見 setGdimWholeGeomHitTestEnabled() 說明）。
     *
     *        只在既有 OSnap／OCCT DetectedInteractive 皆未命中（geomUuid 仍為
     *        空）時才呼叫。多個候選時，取「距圓心的距離 相對於半徑 的比例」
     *        最小者（即最貼近該幾何「感應核心」的一個），以合理處理巢狀圓的
     *        情況。
     *
     * @return {geomUuid, GeomHandle::WholeGeom} 的 pair；找不到則回傳
     *         std::nullopt。
     */
    std::optional<QPair<QString, int>> gdimInteriorHitTest(const QVector2D& planePt) const;

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

    // ── 尺寸線行內數值編輯（雙擊觸發）────────────────────────────────────────
    /// 雙擊尺寸線數值時呼叫：於該標籤畫面座標處顯示行內編輯欄，預填目前的
    /// 數值/表達式文字。Enter 或滑鼠右鍵確認、ESC 取消。
    void startDimValueEdit(const Handle(aicad::cad::AIS_DimensionLine)& dimAIS);
    /// 確認編輯：讀取編輯欄文字，發出 dimValueEditCommitted，關閉編輯欄。
    void commitDimValueEdit();
    /// 取消編輯：不套用任何變更，直接關閉編輯欄。
    void cancelDimValueEdit();

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
