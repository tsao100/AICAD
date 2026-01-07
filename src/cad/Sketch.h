/**
 * @file Sketch.h
 * @brief Sketch 類別定義
 * @author TODO
 * @date 2026-01-07
 */

#ifndef AICAD_CAD_SKETCH_H
#define AICAD_CAD_SKETCH_H

#include <QObject>

namespace aicad {
namespace cad {

/**
 * @brief Sketch 類別
 * 
 * TODO: 添加類別說明
 */
class Sketch : public QObject {
    Q_OBJECT
    
public:
    explicit Sketch(QObject* parent = nullptr);
    ~Sketch() override;
    
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

#endif // AICAD_CAD_SKETCH_H
