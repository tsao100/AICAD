/**
 * @file CadView.h
 * @brief CadView 類別定義
 * @author TODO
 * @date 2026-01-07
 */

#ifndef AICAD_VIEW_CADVIEW_H
#define AICAD_VIEW_CADVIEW_H

#include <QObject>

namespace aicad {
namespace view {

/**
 * @brief CadView 類別
 * 
 * TODO: 添加類別說明
 */
class CadView : public QObject {
    Q_OBJECT
    
public:
    explicit CadView(QObject* parent = nullptr);
    ~CadView() override;
    
    // TODO: 添加公開方法
    
Q_SIGNALS:
    // TODO: 添加信號
    
private:
    // TODO: 添加私有成員
    
    class Private;
    Private* d;
};

} // namespace view
} // namespace aicad

#endif // AICAD_VIEW_CADVIEW_H
