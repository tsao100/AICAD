/**
 * @file LispBindings.h
 * @brief LispBindings 類別定義
 * @author TODO
 * @date 2026-01-07
 */

#ifndef AICAD_SCRIPTING_LISPBINDINGS_H
#define AICAD_SCRIPTING_LISPBINDINGS_H

#include <QObject>

namespace aicad {
namespace scripting {

/**
 * @brief LispBindings 類別
 * 
 * TODO: 添加類別說明
 */
class LispBindings : public QObject {
    Q_OBJECT
    
public:
    explicit LispBindings(QObject* parent = nullptr);
    ~LispBindings() override;
    
    // TODO: 添加公開方法
    
Q_SIGNALS:
    // TODO: 添加信號
    
private:
    // TODO: 添加私有成員
    
    class Private;
    Private* d;
};

} // namespace scripting
} // namespace aicad

#endif // AICAD_SCRIPTING_LISPBINDINGS_H
