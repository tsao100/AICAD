/**
 * @file Settings.h
 * @brief Settings 類別定義
 * @author TODO
 * @date 2026-01-07
 */

#ifndef AICAD_CORE_SETTINGS_H
#define AICAD_CORE_SETTINGS_H

#include <QObject>

namespace aicad {
namespace core {

/**
 * @brief Settings 類別
 * 
 * TODO: 添加類別說明
 */
class Settings : public QObject {
    Q_OBJECT
    
public:
    explicit Settings(QObject* parent = nullptr);
    ~Settings() override;
    
    // TODO: 添加公開方法
    
Q_SIGNALS:
    // TODO: 添加信號
    
private:
    // TODO: 添加私有成員
    
    class Private;
    Private* d;
};

} // namespace core
} // namespace aicad

#endif // AICAD_CORE_SETTINGS_H
