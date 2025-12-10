/**
 * @file CadView.h
 * @brief 3D CAD 視圖 (重構版 - 使用新的視圖系統)
 * @author Felicia
 * @date 2024-12-04
 */

#ifndef CADVIEW_H
#define CADVIEW_H

#include <QWidget>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QTimer>
#include <QVector2D>
#include <QVector3D>

#include <AIS_InteractiveContext.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <AIS_Shape.hxx>
#include <AIS_ViewCube.hxx>

#include <TDF_Label.hxx>

// 前向宣告 - 避免循環依賴
namespace aicad {
    namespace cad {
        class Document;
        struct CustomPlane;
    }
    namespace view {
        class RubberBand;
        class GridOverlay;
        enum class RubberBandMode;
    }
}

class TDF_Label;
class TopoDS_Shape;

/**
 * @brief 視圖方向
 */
enum class SketchView {
    None,
    Top,
    Bottom,
    Front,
    Back,
    Right,
    Left,
    Isometric
};

/**
 * @brief CAD 互動模式
 */
enum class CadMode {
    Idle,           ///< 空閒模式
    Sketching,      ///< 草圖繪製模式
    Extruding,      ///< 擠出模式
    SelectingFace,  ///< 選擇面模式
    GetPoint        ///< 取點模式
};

/**
 * @brief 3D CAD 視圖類別 (重構版)
 * 
 * 主要變更:
 * - 使用 RubberBand 類別處理橡皮筋
 * - 使用 GridOverlay 類別處理網格
 * - 使用 CoordinateConverter 處理座標轉換
 * - 簡化事件處理邏輯
 * - 改善程式碼組織
 */
class CadView : public QWidget {
    Q_OBJECT

public:
    explicit CadView(QWidget* parent = nullptr);
    ~CadView() override;

    /**
     * @brief 設定文件
     */
    void setDocument(aicad::cad::Document* doc);
    
    /**
     * @brief 設定視圖方向
     */
    void setSketchView(SketchView view);
    
    /**
     * @brief 重新整理視圖
     */
    void refreshView();

    /**
     * @brief 取得 AIS 上下文
     */
    Handle(AIS_InteractiveContext) getContext() const { return m_context; }
    
    /**
     * @brief 取得 V3d 視圖
     */
    Handle(V3d_View) getView() const { return m_view; }

    /**
     * @brief 顯示所有特徵
     */
    void displayAllFeatures();
    
    /**
     * @brief 顯示單一特徵
     */
    void displayFeature(TDF_Label label);
    
    /**
     * @brief 高亮顯示特徵
     */
    void highlightFeature(int featureId);

    /**
     * @brief 設定互動模式
     */
    void setMode(CadMode mode);
    
    /**
     * @brief 取得當前模式
     */
    CadMode getMode() const { return m_mode; }

    /**
     * @brief 設定橡皮筋模式
     */
    void setRubberBandMode(aicad::view::RubberBandMode mode);
    
    /**
     * @brief 取得橡皮筋模式
     */
    aicad::view::RubberBandMode getRubberBandMode() const;
    
    /**
     * @brief 設定待處理的草圖
     */
    void setPendingSketch(TDF_Label sketch);

    /**
     * @brief 螢幕座標轉換到平面座標
     */
    QVector2D screenToPlane(const QPoint& screenPos);

    /**
     * @brief 取得當前視圖方向
     */
    SketchView getCurrentView() const { return m_currentView; }
    
    /**
     * @brief 適應所有物件到視圖
     */
    void fitAll();

    /**
     * @brief 取得草圖點列表 (可修改)
     */
    QVector<QVector2D>* getSketchPoints_() { return &m_sketchPoints; }
    
    /**
     * @brief 取得草圖點列表 (唯讀)
     */
    QVector<QVector2D> getSketchPoints() const { return m_sketchPoints; }

    /**
     * @brief 清除橡皮筋
     */
    void clearRubberBand();

Q_SIGNALS:
    /**
     * @brief 點被取得時發出
     */
    void pointAcquired(QVector2D point);
    
    /**
     * @brief GetPoint 被取消時發出
     */
    void getPointCancelled();
    
    /**
     * @brief GetPoint 模式下按鍵時發出
     */
    void getPointKeyPressed(QString key);
    
    /**
     * @brief 需要啟動輸入框時發出
     */
    void getPointActivateInput(QString key);
    
    /**
     * @brief 請求命令輸入焦點
     */
    void requestCommandInputFocus();
    
    /**
     * @brief 轉發按鍵事件到命令輸入
     */
    void forwardKeyToCommandInput(Qt::Key key, Qt::KeyboardModifiers modifiers);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

    QPaintEngine* paintEngine() const override { return nullptr; }

private:
    /**
     * @brief 初始化 OCCT 視圖
     */
    void initializeViewer();
    
    /**
     * @brief 建立折線形狀
     */
    TopoDS_Shape createPolylineShape(const QVector<QVector2D>& points, 
                                     const aicad::cad::CustomPlane& plane);
    
    /**
     * @brief 建立擠出形狀
     */
    TopoDS_Shape createExtrudeShape(TDF_Label sketchLabel, double height);
    
    /**
     * @brief 更新橡皮筋顯示
     */
    void updateRubberBand();
    
    /**
     * @brief 更新網格顯示
     */
    void updateGrid();
    
    /**
     * @brief 清除網格
     */
    void clearGrid();

    // OCCT 核心物件
    Handle(AIS_InteractiveContext) m_context;
    Handle(V3d_View) m_view;
    Handle(V3d_Viewer) m_viewer;
    Handle(AIS_ViewCube) m_viewCube;

    // 視圖系統物件 (新)
    aicad::view::RubberBand* m_rubberBand;
    aicad::view::GridOverlay* m_gridOverlay;

    // 文件與狀態
    aicad::cad::Document* m_document;
    TDF_Label m_pendingSketch;
    SketchView m_currentView;
    CadMode m_mode;

    // 滑鼠互動
    QPoint m_lastMousePos;
    bool m_mousePressed;
    Qt::MouseButton m_pressedButton;

    // 草圖點
    QVector<QVector2D> m_sketchPoints;
    QVector2D m_currentPoint;
    bool m_hasCurrentPoint;
    
    // 初始化標記
    bool m_viewInitialized;
};

#endif // CADVIEW_H
