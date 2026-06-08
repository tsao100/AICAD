/**
 * @file SketchEntity.h
 * @brief SketchEntity 類別定義
 * @author TODO
 * @date 2026-01-07
 */

#ifndef AICAD_CAD_SKETCHENTITY_H
#define AICAD_CAD_SKETCHENTITY_H

#include <QObject>

namespace aicad {
namespace cad {

/**
 * @brief SketchEntity 類別
 * 
 * TODO: 添加類別說明
 */
class SketchEntity : public QObject {
    Q_OBJECT
    
public:
    explicit SketchEntity(QObject* parent = nullptr);
    ~SketchEntity() override;
    
    // TODO: 添加公開方法
    
Q_SIGNALS:
    // TODO: 添加信號
    
private:
    // TODO: 添加私有成員
    
    class Private;
    Private* d;
};

} // namespace cad
} // namespace aicad

#endif // AICAD_CAD_SKETCHENTITY_H
