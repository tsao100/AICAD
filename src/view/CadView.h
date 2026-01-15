/**
 * @file CadView.h
 * @brief CAD 視圖類別 (重構版)
 * @author Felicia
 * @date 2024-12-04
 */

#ifndef AICAD_VIEW_CADVIEW_H
#define AICAD_VIEW_CADVIEW_H

#include <QWidget>
#include <QVector2D>

#include <AIS_InteractiveContext.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>

namespace aicad {

// 前向宣告
namespace cad {
    class Document;
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
    GetPoint        ///< 取得點輸入
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
    /**
     * @brief 建構子
     * @param parent 父 Widget
     */
    explicit CadView(QWidget* parent = nullptr);
    
    /**
     * @brief 解構子
     */
    ~CadView() override;
    
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
    
    /**
     * @brief 刷新視圖
     */
    void refreshView();
    
    /**
     * @brief 適應所有物件到視圖
     */
    void fitAll();
    
    /**
     * @brief 螢幕座標轉平面座標
     * @param screenPos 螢幕座標
     * @return 平面座標
     */
    QVector2D screenToPlane(const QPoint& screenPos) const;
    
    /**
     * @brief 啟用/停用網格顯示
     * @param enabled 是否啟用
     */
    void setGridEnabled(bool enabled);
    
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
    
public Q_SLOTS:
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
    
Q_SIGNALS:
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
    void pointAcquired(QVector2D point);
    
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
    
    class Private;
    Private* d;
};

} // namespace view
} // namespace aicad

#endif // AICAD_VIEW_CADVIEW_H
