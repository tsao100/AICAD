/**
 * @file MainWindow.h
 * @brief MainWindow 類別定義
 * @author TODO
 * @date 2026-01-07
 */

#ifndef AICAD_UI_MAINWINDOW_H
#define AICAD_UI_MAINWINDOW_H

#include <QObject>

namespace aicad {
namespace ui {

/**
 * @brief MainWindow 類別
 * 
 * TODO: 添加類別說明
 */
class MainWindow : public QObject {
    Q_OBJECT
    
public:
    explicit MainWindow(QObject* parent = nullptr);
    ~MainWindow() override;
    
    // TODO: 添加公開方法
    
Q_SIGNALS:
    // TODO: 添加信號
    
private:
    // TODO: 添加私有成員
    
    class Private;
    Private* d;
};

} // namespace ui
} // namespace aicad

#endif // AICAD_UI_MAINWINDOW_H
