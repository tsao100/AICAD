/**
 * @file ui/MainWindow.h
 * @brief 簡化的主視窗（UI 系統版本）
 * @author James
 * @date 2025-01-07
 */

#ifndef AICAD_UI_MAINWINDOW_H
#define AICAD_UI_MAINWINDOW_H

#include <QMainWindow>
#include <QStatusBar>

namespace aicad {
namespace ui {

/**
 * @brief 簡化的主視窗
 * 
 * MainWindow 是應用程式的主要視窗：
 * - 提供基本的視窗框架
 * - 整合各個 UI 元件
 * - 處理視窗事件
 * 
 * 使用範例：
 * @code
 * MainWindow* mainWin = new MainWindow();
 * mainWin->show();
 * @endcode
 */
class MainWindow : public QMainWindow {
    Q_OBJECT
    
public:
    /**
     * @brief 建構子
     */
    explicit MainWindow();
    
    /**
     * @brief 解構子
     */
    ~MainWindow() override;
    
Q_SIGNALS:
    /**
     * @brief 視窗即將關閉時發出
     */
    void aboutToClose();
    
protected:
    /**
     * @brief 關閉事件處理
     */
    void closeEvent(QCloseEvent* event) override;
    
private:
    void setupUI();
    void setupCentralWidget();
    
    class Private;
    Private* d;
};

} // namespace ui
} // namespace aicad

#endif // AICAD_UI_MAINWINDOW_H