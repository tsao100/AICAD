/**
 * @file LispEditorDock.h
 * @brief Lisp 編輯器的 QDockWidget 外殼：自行建立 / 取得，不需要修改 MainWindow 或 UIManager
 */
#ifndef AICAD_UI_LISPEDITORDOCK_H
#define AICAD_UI_LISPEDITORDOCK_H

#include <QDockWidget>

class QMainWindow;

namespace aicad {
namespace ui {

class LispEditorWidget;

class LispEditorDock : public QDockWidget {
    Q_OBJECT
public:
    /**
     * 取得（必要時建立）掛在 mainWindow 上的 Lisp 編輯器 dock。
     * 第一次建立時為浮動視窗（編輯器需要較大的空間），使用者可以自行拖進主視窗停靠。
     */
    static LispEditorDock* instance(QMainWindow* mainWindow);

    /// 找出應用程式的主視窗（可見的 QMainWindow）；找不到回傳 nullptr
    static QMainWindow* findMainWindow();

    LispEditorWidget* editor() const { return m_editor; }

    /// 顯示並聚焦；若 visible 已是 true 就只是 raise
    void showAndFocus();

    /// 顯示 / 隱藏切換
    void toggle();

private:
    explicit LispEditorDock(QMainWindow* mainWindow);

    LispEditorWidget* m_editor = nullptr;
};

} // namespace ui
} // namespace aicad

#endif
