/**
 * @file LispBindings.h
 * @brief AICAD API 的 Lisp 綁定
 * @author Daney
 * @date 2024-12-04
 */

#ifndef AICAD_SCRIPTING_LISPBINDINGS_H
#define AICAD_SCRIPTING_LISPBINDINGS_H

#include <QObject>
#include <QString>

namespace aicad {

// 前向宣告
namespace core {
    class Application;
}

namespace scripting {

class LispEngine;

/**
 * @brief Lisp API 綁定
 * 
 * LispBindings 將 AICAD 的 C++ API 綁定到 Lisp:
 * - 文件操作 (sketch-create, extrude-create 等)
 * - 幾何操作 (line, rectangle, circle 等)
 * - 視圖控制 (view-set, view-fit 等)
 * - 查詢功能 (feature-list, feature-info 等)
 * 
 * 使用範例：
 * @code
 * LispBindings* bindings = new LispBindings(lispEngine, app);
 * bindings->registerAll();
 * 
 * // 現在可以在 Lisp 中使用：
 * // (sketch-create "XY" "MySketch")
 * // (rectangle 0 0 100 100)
 * @endcode
 */
class LispBindings : public QObject {
    Q_OBJECT
    
public:
    /**
     * @brief 建構子
     * @param engine Lisp 引擎
     * @param app 應用程式實例
     * @param parent 父物件
     */
    explicit LispBindings(LispEngine* engine,
                         core::Application* app,
                         QObject* parent = nullptr);
    
    /**
     * @brief 解構子
     */
    ~LispBindings() override;
    
    /**
     * @brief 註冊所有 API 函式
     */
    void registerAll();
    
    /**
     * @brief 註冊文件相關函式
     * 
     * - sketch-create (plane name)
     * - extrude-create (sketch-id height name)
     * - feature-delete (feature-id)
     */
    void registerDocumentAPI();
    
    /**
     * @brief 註冊幾何繪圖函式
     * 
     * - line (x1 y1 x2 y2)
     * - rectangle (x1 y1 x2 y2)
     * - circle (x y radius)
     * - arc (x y radius start-angle end-angle)
     * - polyline (&rest points)
     */
    void registerGeometryAPI();
    
    /**
     * @brief 註冊視圖控制函式
     * 
     * - view-set (view-name)  ; "TOP", "FRONT", "RIGHT", "ISOMETRIC"
     * - view-fit ()
     * - view-zoom (factor)
     */
    void registerViewAPI();
    
    /**
     * @brief 註冊查詢函式
     * 
     * - feature-list ()  ; 返回所有特徵 ID
     * - feature-info (feature-id)  ; 返回特徵資訊
     * - feature-count ()
     * - sketch-polylines (sketch-id)  ; 返回草圖的折線
     */
    void registerQueryAPI();
    
    /**
     * @brief 註冊實用工具函式
     * 
     * - getpoint (&optional base-point message)
     * - command (cmd-name &rest args)
     * - print-message (message)
     * - distance (x1 y1 x2 y2)
     */
    void registerUtilityAPI();
    
private:
    class Private;
    Private* d;
};

} // namespace scripting
} // namespace aicad

#endif // AICAD_SCRIPTING_LISPBINDINGS_H